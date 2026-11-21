// SPDX-License-Identifier: GPL-3.0-or-later
#include <hamlib/rig.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <memory>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 3) {
        std::fprintf(stderr, "Usage: test-dummy-tx-loopback HOST:PORT [960-byte S16LE sample block]\n");
        return 2;
    }
    rig_set_debug(RIG_DEBUG_NONE);
    std::unique_ptr<RIG, decltype(&rig_cleanup)> rig(rig_init(RIG_MODEL_NETRIGCTL), rig_cleanup);
    if (!rig) return 1;

    const auto check = [](const char* operation, int result) {
        if (result == RIG_OK) return true;
        std::fprintf(stderr, "%s: %s\n", operation, rigerror(result));
        return false;
    };
    if (!check("Set server endpoint", rig_set_conf(rig.get(),
               rig_token_lookup(rig.get(), "rig_pathname"), argv[1]))
        || !check("Connect to server", rig_open(rig.get()))) return 1;

    std::unique_ptr<rig_stream_config, decltype(&rig_stream_config_free)> config(
        rig_stream_config_alloc(), rig_stream_config_free);
    if (!config) return 1;
    config->format = RIG_STREAM_FORMAT_PCM_S16;
    config->sample_rate = 48000;
    config->channels = 1;

    const auto close_stream = [&](rig_stream_t* stream) { rig_stream_close(rig.get(), stream); };
    rig_stream_t* handle = nullptr;
    config->type = RIG_STREAM_TYPE_AUDIO_RX;
    if (!check("Open audio RX", rig_stream_open(rig.get(), config.get(), &handle))) return 1;
    std::unique_ptr<rig_stream_t, decltype(close_stream)> rx(handle, close_stream);
    config->type = RIG_STREAM_TYPE_AUDIO_TX;
    if (!check("Open audio TX", rig_stream_open(rig.get(), config.get(), &handle))) return 1;
    std::unique_ptr<rig_stream_t, decltype(close_stream)> tx(handle, close_stream);

    // Two nonzero signed S16LE samples, independent of native byte order.
    constexpr std::array<unsigned char, 4> pattern{0x00, 0x20, 0x00, 0xe0};
    std::array<unsigned char, 960> send;
    for (size_t i = 0; i < send.size(); i += pattern.size()) {
        std::copy(pattern.begin(), pattern.end(), send.begin() + i);
    }
    if (argc == 3) {
        std::ifstream file(argv[2], std::ios::binary);
        if (!file.read(reinterpret_cast<char*>(send.data()), send.size()) || file.peek() != EOF
            || std::all_of(send.begin(), send.end(), [](auto value) { return value == 0; })) {
            std::fprintf(stderr, "Provide one nonsilent, 10 ms PCM block at 48 kHz mono S16LE\n");
            return 2;
        }
    }
    std::array<unsigned char, 1920> receive;
    std::vector<unsigned char> received_pcm;
    for (int attempt = 0; attempt < 12; ++attempt) {
        size_t written = 0;
        if (!check("Write audio TX", rig_stream_write(rig.get(), tx.get(), send.data(), send.size(),
                                                    &written, 250, nullptr))) return 1;
        if (written != send.size()) {
            std::fprintf(stderr, "Short audio write: %zu of %zu bytes\n", written, send.size());
            return 1;
        }
        size_t received = 0;
        const int result = rig_stream_read(rig.get(), rx.get(), receive.data(), receive.size(),
                                          &received, 250, nullptr);
        if (result == -RIG_ETIMEOUT) continue;
        if (!check("Read audio RX", result)) return 1;
        received_pcm.insert(received_pcm.end(), receive.begin(), receive.begin() + received);
        if (std::search(received_pcm.begin(), received_pcm.end(), send.begin(), send.end())
            != received_pcm.end()) {
            std::puts("480 exact PCM samples returned through dummy loopback");
            return 0;
        }
    }
    std::fprintf(stderr, "Transmitted PCM did not return through dummy loopback\n");
    return 1;
}
