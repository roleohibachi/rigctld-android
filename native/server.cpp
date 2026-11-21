// SPDX-License-Identifier: GPL-3.0-or-later
#include "server.h"

#include <array>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <unistd.h>

extern "C" {
#include "rigctl_parse.h"

// The CLI and the shared parser use these globals from libhamlib.
extern int lock_mode;
extern int is_rigctld;
extern powerstat_t rig_powerstat;
int embedded_stream_start(void);
void embedded_stream_set_client(int client_id);
void embedded_stream_close_client(int client_id);
void embedded_stream_stop(void);
}

namespace {
constexpr std::size_t max_clients = 16;
constexpr std::size_t max_command_bytes = 4096;
constexpr std::size_t max_reply_bytes = 65536;

class Socket {
public:
    explicit Socket(int fd = -1) : fd_(fd) {}
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    ~Socket() { reset(); }
    int get() const { return fd_; }
    void reset(int fd = -1) {
        if (fd_ >= 0) close(fd_);
        fd_ = fd;
    }

private:
    int fd_;
};

struct Client {
    Socket socket;
    std::string command;
    std::string reply;
    int vfo_mode = 0;
    int extended_response = 0;
    char response_separator = '\n';
    bool close_after_reply = false;

    void disconnect() {
        socket.reset();
        command.clear();
        reply.clear();
        vfo_mode = 0;
        extended_response = 0;
        response_separator = '\n';
        close_after_reply = false;
    }
};

struct RadioControl {
    const RigctldServer::Radio& radio;
    Client* ptt_owner = nullptr;
    vfo_t ptt_vfo = RIG_VFO_CURR;
    std::chrono::steady_clock::time_point ptt_deadline{};

    int release(RIG* rig) {
        if (!ptt_owner) return RIG_OK;
        const int result = rig_set_ptt(rig, ptt_vfo, RIG_PTT_OFF);
        if (result == RIG_OK) ptt_owner = nullptr;
        return result;
    }
};

struct CommandContext {
    RadioControl& control;
    Client& client;
};
thread_local CommandContext* current_command = nullptr;

bool parse_command(RIG* rig, Client& client, RadioControl& control) {
    const auto close_file = [](FILE* file) { fclose(file); };
    std::unique_ptr<FILE, decltype(close_file)> input(
        fmemopen(client.command.data(), client.command.size(), "r"), close_file);
    if (!input) return false;

    char* reply = nullptr;
    std::size_t length = 0;
    FILE* output = open_memstream(&reply, &length);
    if (!output) return false;
    CommandContext context{control, client};
    current_command = &context;
    embedded_stream_set_client(client.socket.get());
    const int result = rigctl_parse(
        rig, input.get(), output, nullptr, 0, nullptr, 1, 0, &client.vfo_mode,
        '\r', &client.extended_response, &client.response_separator, 0);
    current_command = nullptr;
    if (result == -RIG_EIO) fprintf(output, "RPRT %d\n", result);
    const int output_status = fclose(output);
    std::unique_ptr<char, decltype(&free)> reply_storage(reply, free);
    client.command.clear();
    if (output_status != 0 || length > max_reply_bytes) return false;
    if (length != 0) client.reply.assign(reply_storage.get(), length);
    client.close_after_reply = result == RIGCTL_PARSE_END || result == RIGCTL_PARSE_ERROR;
    return !client.close_after_reply || !client.reply.empty();
}

bool read_command(RIG* rig, Client& client, RadioControl& control) {
    while (client.command.size() < max_command_bytes) {
        char byte = 0;
        const ssize_t received = recv(client.socket.get(), &byte, sizeof(byte), 0);
        if (received < 0) {
            if (errno == EINTR) continue;
            return errno == EAGAIN || errno == EWOULDBLOCK;
        }
        if (received == 0 || byte == '\0') return false;
        client.command.push_back(byte);
        if (byte == '\n') return parse_command(rig, client, control);
    }
    return false;
}

bool write_reply(Client& client) {
    const ssize_t sent =
        send(client.socket.get(), client.reply.data(), client.reply.size(), MSG_NOSIGNAL);
    if (sent < 0) return errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK;
    client.reply.erase(0, static_cast<std::size_t>(sent));
    return !client.close_after_reply || !client.reply.empty();
}

std::string system_error(const std::string& action) {
    const int error = errno;
    return action + ": " + std::strerror(error);
}

int configure_serial(RIG* rig, const char* key, const std::string& value) {
    if (value.empty()) return RIG_OK;
    const token_t token = rig_token_lookup(rig, key);
    return token == RIG_CONF_END ? -RIG_EINVAL : rig_set_conf(rig, token, value.c_str());
}
}

extern "C" int rigctld_embedded_authorize(const char* command, const char* argument, vfo_t vfo) {
    const std::string_view name(command);
    if (name == "halt" || name == "pause" || name == "test") return -RIG_EACCESS;
    if (current_command == nullptr) return -RIG_EINTERNAL;
    auto& [control, client] = *current_command;
    if (name.substr(0, 7) == "stream_") {
        return ((control.radio.model == RIG_MODEL_DUMMY && control.radio.path.empty())
            || control.radio.usb_audio_rx) ? RIG_OK : -RIG_ENAVAIL;
    }
    if (control.radio.path.empty() && !control.radio.enforce_radio_policy) return RIG_OK;

    // Physical radios expose the CAT operations modem clients need. Raw CAT,
    // configuration writes and independent keying paths remain disabled.
    constexpr std::string_view cat_commands[] = {
        "get_freq", "set_freq", "get_mode", "set_mode", "get_ptt", "get_info",
        "get_vfo", "set_vfo", "get_split_vfo", "set_split_vfo", "get_vfo_info",
        "get_rig_info", "get_vfo_list", "get_modes", "get_mode_bandwidths",
        "dump_caps", "dump_state", "chk_vfo", "set_vfo_opt", "client_version",
        "hamlib_version", "get_powerstat", "set_separator", "get_separator"
    };
    if (std::find(std::begin(cat_commands), std::end(cat_commands), name) != std::end(cat_commands)) {
        if ((name == "set_freq" || name == "set_mode" || name == "set_vfo" || name == "set_split_vfo")
            && control.ptt_owner != nullptr && control.ptt_owner != &client) return -RIG_EACCESS;
        return RIG_OK;
    }
    if (name != "set_ptt") return -RIG_EACCESS;
    if (argument == nullptr || std::strlen(argument) != 1 || argument[0] < '0' || argument[0] > '3') {
        return -RIG_EINVAL;
    }
    if (argument[0] == '0') {
        return control.ptt_owner == nullptr || vfo == control.ptt_vfo ? RIG_OK : -RIG_EACCESS;
    }
    if (!control.radio.allow_ptt || (control.ptt_owner != nullptr &&
        (control.ptt_owner != &client || vfo != control.ptt_vfo))) {
        return -RIG_EACCESS;
    }
    if (control.ptt_owner == nullptr) {
        control.ptt_deadline = std::chrono::steady_clock::now() + std::chrono::minutes(3);
        control.ptt_vfo = vfo;
        // A failed reply can still mean the rig keyed. Keep ownership until an
        // explicit unkey succeeds or the service attempts recovery.
        control.ptt_owner = &client;
    }
    return RIG_OK;
}

extern "C" void rigctld_embedded_completed(const char* command, const char* argument, int result) {
    if (result == RIG_OK && std::string_view(command) == "set_ptt" && argument && argument[0] == '0') {
        current_command->control.ptt_owner = nullptr;
    }
}

std::string RigctldServer::run(uint16_t port, const Radio& radio, const std::function<void()>& on_listening) {
    if (stopping_) return {};
    Socket listener(socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
    if (listener.get() < 0) return system_error("Create CAT socket");
    const int reuse = 1;
    if (setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
        return system_error("Configure CAT socket");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        return system_error("Bind 127.0.0.1:" + std::to_string(port));
    }

    lock_mode = 0;
    is_rigctld = 1;
    rig_powerstat = RIG_POWER_ON;
    std::unique_ptr<RIG, decltype(&rig_cleanup)> rig(rig_init(radio.model), rig_cleanup);
    if (!rig) return "Unknown Hamlib model " + std::to_string(radio.model);
    if (!radio.path.empty()) {
        if (rig->caps->port_type != RIG_PORT_SERIAL) return "Select a serial Hamlib model";
        for (const auto& [key, value] : std::array{
            std::pair{"rig_pathname", &radio.path},
            std::pair{"serial_speed", &radio.baud},
            std::pair{"serial_handshake", &radio.handshake}
        }) {
            const int result = configure_serial(rig.get(), key, *value);
            if (result != RIG_OK) return std::string(key) + ": " + rigerror(result);
        }
    }
    const int open_status = rig_open(rig.get());
    if (open_status != RIG_OK) return std::string("Open Hamlib rig: ") + rigerror(open_status);

    std::string error;
    if (radio.usb_audio_rx && embedded_audio_attach(audio_, rig.get()) != RIG_OK) {
        error = "Attach USB receive audio";
    } else if (listen(listener.get(), static_cast<int>(max_clients)) != 0) {
        error = system_error("Listen on CAT socket");
    } else if (embedded_stream_start() != 0) {
        error = "Initialize Hamlib streaming";
    } else {
        if (!stopping_) on_listening();
        std::array<Client, max_clients> clients;
        std::array<pollfd, max_clients + 1> descriptors{};
        RadioControl control{radio};
        while (!stopping_) {
            if (control.ptt_owner && std::chrono::steady_clock::now() >= control.ptt_deadline) {
                if (control.release(rig.get()) != RIG_OK) {
                    error = "PTT timeout. Unable to confirm release";
                    break;
                }
            }
            descriptors[0] = {listener.get(), POLLIN, 0};
            for (std::size_t i = 0; i < clients.size(); ++i) {
                const auto event = clients[i].reply.empty() ? POLLIN : POLLOUT;
                descriptors[i + 1] = {clients[i].socket.get(), static_cast<short>(event), 0};
            }
            const int ready = poll(descriptors.data(), descriptors.size(), 100);
            if (ready < 0 && errno == EINTR) continue;
            if (ready < 0) {
                error = system_error("Wait for CAT sockets");
                break;
            }
            if (descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                error = "CAT listener closed unexpectedly";
                break;
            }
            for (std::size_t i = 0; i < clients.size() && !stopping_; ++i) {
                const auto events = descriptors[i + 1].revents;
                if (events == 0) continue;
                auto& client = clients[i];
                bool keep = false;
                if (events & POLLOUT) {
                    keep = write_reply(client);
                } else if (events & POLLIN) {
                    keep = read_command(rig.get(), client, control);
                }
                if (!keep) {
                    if (control.ptt_owner == &client && control.release(rig.get()) != RIG_OK) {
                        error = "Client disconnected. Unable to confirm PTT release";
                        stopping_ = true;
                    }
                    embedded_stream_close_client(client.socket.get());
                    client.disconnect();
                }
            }
            if (!stopping_ && (descriptors[0].revents & POLLIN)) {
                const int fd = accept4(listener.get(), nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
                if (fd >= 0) {
                    bool accepted = false;
                    for (auto& client : clients) {
                        if (client.socket.get() >= 0) continue;
                        client.socket.reset(fd);
                        accepted = true;
                        break;
                    }
                    if (!accepted) close(fd);
                }
            }
        }
        if (control.release(rig.get()) != RIG_OK) error = "Unable to confirm PTT release";
        embedded_stream_stop();
    }
    rig_close(rig.get());
    return error;
}

RigctldServer::RigctldServer() : audio_(embedded_audio_create()) {}

RigctldServer::~RigctldServer() {
    embedded_audio_destroy(audio_);
}

void RigctldServer::stop() noexcept {
    stopping_ = true;
}
