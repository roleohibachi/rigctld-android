// SPDX-License-Identifier: GPL-3.0-or-later
#include "server.h"
#include "tx_stream.h"

#include <hamlib/rig.h>

#include <jni.h>
#include <linux/usbdevice_fs.h>
#include <poll.h>
#include <sys/ioctl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr int packets_per_transfer = 8;
constexpr int max_packet_size = 1024;

struct Transfer {
    std::unique_ptr<usbdevfs_urb, decltype(&std::free)> urb{
        static_cast<usbdevfs_urb*>(std::calloc(1, sizeof(usbdevfs_urb)
            + packets_per_transfer * sizeof(usbdevfs_iso_packet_desc))), std::free};
    std::array<unsigned char, packets_per_transfer * max_packet_size> bytes;
    bool pending = false;
    int tx_frames = 0;
    unsigned tx_generation = 0;
};

int usb_ioctl(int fd, unsigned long request, void* argument) {
    int result;
    do {
        result = ioctl(fd, request, argument);
    } while (result < 0 && errno == EINTR);
    return result;
}

struct TransferQueue {
    int fd;
    std::array<Transfer, 4> transfers;

    int submit(Transfer& transfer) const {
        if (usb_ioctl(fd, USBDEVFS_SUBMITURB, transfer.urb.get()) < 0) return errno;
        transfer.pending = true;
        return 0;
    }

    ~TransferQueue() {
        // DISCARDURB kills the pending transfer synchronously. Reap completion records
        // before Java resets and releases the interface on the same descriptor.
        for (auto& transfer : transfers) {
            if (transfer.pending) usb_ioctl(fd, USBDEVFS_DISCARDURB, transfer.urb.get());
        }
        void* completed;
        while (usb_ioctl(fd, USBDEVFS_REAPURBNDELAY, &completed) == 0) {}
    }
};

// The diagnostic maps each accepted network frame to a frame in a completed
// USB transfer. Only zeros are accepted, including from other local clients.
class SilentUsbPlayback final : public TxPlayback {
public:
    bool available() const noexcept override {
        std::lock_guard lock(mutex);
        return ready;
    }

    bool enqueue(const void* pcm, std::size_t bytes) noexcept override {
        const auto* data = static_cast<const unsigned char*>(pcm);
        std::lock_guard lock(mutex);
        if (!ready || bytes % 2 || bytes / 2 > static_cast<std::size_t>(4800 - queued)
            || std::any_of(data, data + bytes, [](unsigned char byte) { return byte != 0; })) {
            return false;
        }
        queued += static_cast<int>(bytes / 2);
        return true;
    }

    bool drain(std::chrono::milliseconds timeout) noexcept override {
        std::unique_lock lock(mutex);
        const unsigned draining = generation;
        changed.wait_for(lock, timeout, [&] {
            return !ready || generation != draining || queued + in_flight == 0;
        });
        return ready && generation == draining && queued + in_flight == 0;
    }

    void discard() noexcept override {
        std::lock_guard lock(mutex);
        ++generation;
        queued = 0;
        in_flight = 0;
        changed.notify_all();
    }

    void submit(Transfer& transfer) {
        std::lock_guard lock(mutex);
        transfer.tx_frames = std::min(queued, packets_per_transfer * 48);
        transfer.tx_generation = generation;
        queued -= transfer.tx_frames;
        in_flight += transfer.tx_frames;
    }

    void complete(const Transfer& transfer) {
        std::lock_guard lock(mutex);
        ready = true;
        if (transfer.tx_generation != generation) return;
        in_flight -= transfer.tx_frames;
        delivered += transfer.tx_frames;
        changed.notify_all();
    }

    void disconnect() {
        std::lock_guard lock(mutex);
        ready = false;
        changed.notify_all();
    }

    int framesDelivered() const {
        std::lock_guard lock(mutex);
        return delivered;
    }

private:
    mutable std::mutex mutex;
    std::condition_variable changed;
    bool ready = false;
    int queued = 0;
    int in_flight = 0;
    int delivered = 0;
    unsigned generation = 0;
};

constexpr uint16_t playback_probe_port = 14533;
constexpr int playback_probe_frames = 48000;

int send_probe_audio(const std::atomic<bool>& stop) {
    std::unique_ptr<RIG, decltype(&rig_cleanup)> rig(rig_init(RIG_MODEL_NETRIGCTL), rig_cleanup);
    if (!rig) return -RIG_ENOMEM;
    const std::string endpoint = "127.0.0.1:" + std::to_string(playback_probe_port);
    for (const auto& [key, value] : {std::pair{"rig_pathname", endpoint.c_str()},
                                     std::pair{"timeout", "500"}, std::pair{"retry", "0"}}) {
        int result = rig_set_conf(rig.get(), rig_token_lookup(rig.get(), key), value);
        if (result != RIG_OK) return result;
    }
    int result = rig_open(rig.get());
    if (result != RIG_OK) return result;
    std::unique_ptr<rig_stream_config, decltype(&rig_stream_config_free)> config(
        rig_stream_config_alloc(), rig_stream_config_free);
    if (!config) return -RIG_ENOMEM;
    config->type = RIG_STREAM_TYPE_AUDIO_TX;
    config->format = RIG_STREAM_FORMAT_PCM_S16;
    config->sample_rate = 48000;
    config->channels = 1;
    rig_stream_t* handle = nullptr;
    result = rig_stream_open(rig.get(), config.get(), &handle);
    if (result != RIG_OK) return result;
    const auto close = [&](rig_stream_t* stream) { rig_stream_close(rig.get(), stream); };
    std::unique_ptr<rig_stream_t, decltype(close)> stream(handle, close);
    if (stop) return -RIG_EIO;
    // The only server at this port is created below with model 1 and no
    // physical CAT path. Its PTT commands affect Hamlib's dummy backend.
    result = rig_set_ptt(rig.get(), RIG_VFO_CURR, RIG_PTT_ON);
    if (result != RIG_OK) return result;
    constexpr std::array<int16_t, 480> silence{};
    constexpr int blocks = playback_probe_frames / silence.size();
    rig_stream_write_info end{};
    end.flags = RIG_STREAM_TIME_FLAG_EOB;
    auto deadline = Clock::now();

    for (int block = 0; block < blocks; ++block) {
        if (stop) return -RIG_EIO; // closing this session aborts dummy PTT and its queue
        size_t written = 0;
        result = rig_stream_write(rig.get(), stream.get(), silence.data(), sizeof(silence),
                                  &written, 0, block == blocks - 1 ? &end : nullptr);
        if (result != RIG_OK) return result;
        if (written != sizeof(silence)) return -RIG_EIO;
        deadline += 10ms;
        std::this_thread::sleep_until(deadline);
    }
    result = rig_set_ptt(rig.get(), RIG_VFO_CURR, RIG_PTT_OFF);
    if (result != RIG_OK) return result;
    ptt_t ptt;
    result = rig_get_ptt(rig.get(), RIG_VFO_CURR, &ptt);
    if (result != RIG_OK) return result;
    return ptt == RIG_PTT_OFF ? RIG_OK : -RIG_EIO;
}

struct PlaybackProbeSession {
    SilentUsbPlayback playback;
    RigctldServer server;
    std::atomic<bool> server_ready{false};
    std::atomic<bool> server_exited{false};
    std::atomic<bool> stop{false};
    std::atomic<bool> finished{false};
    int result = -RIG_EIO;
    std::string server_error;
    std::thread server_worker;
    std::thread client_worker;

    bool start() {
        try {
            server_worker = std::thread([this] {
                RigctldServer::Radio dummy;
                dummy.enforce_radio_policy = true;
                dummy.allow_ptt = true;
                dummy.tx_playback = &playback;
                server_error = server.run(playback_probe_port, dummy, [this] { server_ready = true; });
                server_exited = true;
            });
            client_worker = std::thread([this] {
                while (!stop && !server_exited && !(server_ready && playback.available())) {
                    std::this_thread::sleep_for(5ms);
                }
                if (!stop && !server_exited) result = send_probe_audio(stop);
                finished = true;
            });
        } catch (const std::system_error&) {
            return false;
        }
        return true;
    }

    void close() {
        stop = true;
        playback.disconnect();
        if (client_worker.joinable()) client_worker.join();
        server.stop();
        if (server_worker.joinable()) server_worker.join();
    }

    ~PlaybackProbeSession() { close(); }
};

struct Levels {
    int frames = 0;
    int nonzero = 0;
    int peak = 0;
    double squares = 0;

    void add(const unsigned char* data, int byte_count, int channels) {
        frames += byte_count / (channels * 2);
        for (int offset = 0; offset < byte_count; offset += 2) {
            int sample = data[offset] | (data[offset + 1] << 8);
            if (sample >= 0x8000) sample -= 0x10000;
            nonzero += sample != 0;
            peak = std::max(peak, std::abs(sample));
            squares += static_cast<double>(sample) * sample;
        }
    }

    double rms(int channels) const {
        return frames == 0 ? 0 : std::sqrt(squares / (frames * channels));
    }
};

bool is_cancelled(JNIEnv* env, jobject cancelled, jmethodID get) {
    return env->CallBooleanMethod(cancelled, get) || env->ExceptionCheck();
}
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_rigctld_android_UsbAudioMonitor_nativeMeasure(
    JNIEnv* env, jclass, jint fd, jint endpoint, jint packet_size, jint channels,
    jobject cancelled, jobject update, jlong server_handle, jobject playback) {
    auto* server = reinterpret_cast<RigctldServer*>(server_handle);
    auto* audio = server ? server->audio() : nullptr;
    const bool silent_output = endpoint == 0x01 && packet_size == 192 && channels == 2
        && !server && !playback && !update;
    if (fd < 0 || packet_size <= 0 || packet_size > max_packet_size
        || (channels != 1 && channels != 2) || (!silent_output && (endpoint < 0x81 || endpoint > 0x8f))
        || (server && (!audio || channels != 1))) {
        return env->NewStringUTF("Invalid USB audio parameters.");
    }
    jmethodID get = env->GetMethodID(env->GetObjectClass(cancelled), "get", "()Z");
    if (!get) return nullptr;
    jmethodID accept = nullptr;
    if (update) {
        accept = env->GetMethodID(env->GetObjectClass(update), "accept", "(Ljava/lang/Object;)V");
        if (!accept) return nullptr;
    }
    std::array<unsigned char, packets_per_transfer * max_packet_size> playback_buffer;
    jmethodID play = nullptr;
    jobject playback_samples = nullptr;
    if (playback) {
        play = env->GetMethodID(env->GetObjectClass(playback), "write", "(Ljava/nio/ByteBuffer;I)V");
        if (!play) return nullptr;
        playback_samples = env->NewDirectByteBuffer(playback_buffer.data(), playback_buffer.size());
        if (!playback_samples) return nullptr;
    }

    TransferQueue queue{fd, {}};
    int error = 0;
    const char* operation = "queue USB audio";
    for (auto& transfer : queue.transfers) {
        if (!transfer.urb) return env->NewStringUTF("Could not allocate USB transfer buffers.");
        if (silent_output) transfer.bytes.fill(0);
        auto& urb = *transfer.urb;
        urb.type = USBDEVFS_URB_TYPE_ISO;
        urb.endpoint = endpoint;
        urb.flags = USBDEVFS_URB_ISO_ASAP;
        urb.buffer = transfer.bytes.data();
        urb.buffer_length = packet_size * packets_per_transfer;
        urb.number_of_packets = packets_per_transfer;
        urb.usercontext = &transfer;
        for (int i = 0; i < packets_per_transfer; ++i) urb.iso_frame_desc[i].length = packet_size;
        error = queue.submit(transfer);
        if (error != 0) break;
    }

    Levels total;
    Levels recent;
    int packets = 0;
    int packet_errors = 0;
    const auto start = Clock::now();
    const auto end = audio ? Clock::time_point::max() : start + (silent_output ? 3s : 5s);
    auto last_frames = start;
    auto next_update = start + 250ms;
    bool reported_ready = false;
    std::unique_ptr<PlaybackProbeSession> probe;
    if (silent_output && !error && !is_cancelled(env, cancelled, get)) {
        probe = std::make_unique<PlaybackProbeSession>();
        if (!probe->start()) return env->NewStringUTF("Could not start the dummy network test.");
    }
    char message[256];
    while (error == 0 && Clock::now() < end && !is_cancelled(env, cancelled, get)) {
        if (probe && probe->finished) break;
        if ((audio && Clock::now() - last_frames > 2s)
            || (silent_output && Clock::now() - last_frames > 250ms)) {
            operation = silent_output ? "complete USB output frames" : "receive frames";
            error = ETIMEDOUT;
            break;
        }
        pollfd waiter{fd, POLLOUT, 0};
        int ready = poll(&waiter, 1, 100);
        if (ready == 0 || (ready < 0 && errno == EINTR)) continue;
        operation = "wait for USB audio";
        if (ready < 0) {
            error = errno;
            break;
        }
        usbdevfs_urb* completed = nullptr;
        operation = "reap USB audio";
        if (usb_ioctl(fd, USBDEVFS_REAPURBNDELAY, &completed) < 0) {
            if (errno == EAGAIN && !(waiter.revents & (POLLERR | POLLHUP | POLLNVAL))) continue;
            error = errno == EAGAIN ? ENODEV : errno;
            break;
        }
        auto& transfer = *static_cast<Transfer*>(completed->usercontext);
        transfer.pending = false;
        if (completed->status != 0) {
            error = -completed->status;
            break;
        }
        int playback_bytes = 0;
        for (int i = 0; i < packets_per_transfer; ++i) {
            const auto& packet = completed->iso_frame_desc[i];
            if (!audio) ++packets;
            if (silent_output && (packet.status != 0
                    || packet.actual_length != static_cast<unsigned>(packet_size))) {
                ++packet_errors;
                error = packet.status < 0 ? -packet.status : EIO;
                break;
            }
            if (packet.status != 0 || packet.actual_length > static_cast<unsigned>(packet_size)
                || packet.actual_length % (channels * 2) != 0) {
                if (!audio) ++packet_errors;
                continue;
            }
            const auto* data = transfer.bytes.data() + i * packet_size;
            if (audio && packet.actual_length > 0) {
                embedded_audio_push(audio, data, packet.actual_length);
                last_frames = Clock::now();
                if (playback) {
                    std::memcpy(playback_buffer.data() + playback_bytes, data, packet.actual_length);
                    playback_bytes += packet.actual_length;
                }
            } else if (!audio) {
                total.add(data, packet.actual_length, channels);
                recent.add(data, packet.actual_length, channels);
                if (silent_output) last_frames = Clock::now();
            }
        }
        if (error) break;
        if (probe) {
            probe->playback.complete(transfer);
            probe->playback.submit(transfer);
        }
        if (playback_bytes > 0) {
            env->CallVoidMethod(playback, play, playback_samples, playback_bytes);
            if (env->ExceptionCheck()) return nullptr;
        }
        if (update && ((audio && !reported_ready && last_frames != start)
                       || (!audio && Clock::now() >= next_update))) {
            if (audio) {
                std::snprintf(message, sizeof(message), "C-Media USB receive ready · 48 kHz mono PCM16");
                reported_ready = true;
            } else {
                std::snprintf(message, sizeof(message), "%d frames | Current peak %d | RMS %.0f",
                    total.frames, recent.peak, recent.rms(channels));
            }
            jstring progress = env->NewStringUTF(message);
            if (!progress) return nullptr;
            env->CallVoidMethod(update, accept, progress);
            env->DeleteLocalRef(progress);
            if (env->ExceptionCheck()) return nullptr;
            recent = {};
            next_update += 250ms;
        }
        operation = "queue USB audio";
        error = queue.submit(transfer);
    }
    if (env->ExceptionCheck()) return nullptr;
    if (probe) {
        probe->close();
        if (!is_cancelled(env, cancelled, get) && !error) {
            if (!probe->server_error.empty()) return env->NewStringUTF(probe->server_error.c_str());
            if (probe->result != RIG_OK || probe->playback.framesDelivered() != playback_probe_frames) {
                std::snprintf(message, sizeof(message),
                    "Rigctld TX silence check failed: %d/%d network frames reached USB. Hamlib: %s",
                    probe->playback.framesDelivered(), playback_probe_frames, rigerror(probe->result));
                return env->NewStringUTF(message);
            }
        }
    }
    if (!audio && error == 0 && total.frames == 0 && !is_cancelled(env, cancelled, get)) {
        operation = silent_output ? "complete USB output frames" : "receive frames";
        error = ETIMEDOUT;
    }
    char status[120];
    if (error != 0) {
        std::snprintf(status, sizeof(status), "Could not %s (USB error %d).", operation, error);
    } else if (is_cancelled(env, cancelled, get)) {
        std::snprintf(status, sizeof(status), "Monitor stopped.");
    } else if (silent_output) {
        std::snprintf(status, sizeof(status),
            "Rigctld TX silence check passed: %d network frames reached USB. Dummy PTT off.",
            playback_probe_frames);
    } else {
        std::snprintf(status, sizeof(status), "%s", total.nonzero == 0 ? "All samples were zero." : "Samples received.");
    }
    if (env->ExceptionCheck()) return nullptr;
    if (audio) return env->NewStringUTF(status);
    std::snprintf(message, sizeof(message),
        "%s\n48 kHz / %d ch / PCM16 | Frames %d | Nonzero samples %d\nPeak %d | RMS %.0f | USB packet errors %d/%d",
        status, channels, total.frames, total.nonzero, total.peak, total.rms(channels), packet_errors, packets);
    return env->NewStringUTF(message);
}
