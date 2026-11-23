// SPDX-License-Identifier: GPL-3.0-or-later
#include "server.h"

#ifndef __ANDROID__
#include "tx_stream.h"
#include <mutex>
#endif
#include <charconv>
#include <array>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string_view>
#include <thread>

namespace {
RigctldServer server;

void stop(int) {
    server.stop();
}

#ifndef __ANDROID__
class FakePlayback final : public TxPlayback {
public:
    bool available() const noexcept override { return connected_; }

    bool enqueue(const void* pcm, std::size_t bytes) noexcept override {
        std::lock_guard lock(mutex_);
        const auto* samples = static_cast<const unsigned char*>(pcm);
        // The fixture's marker simulates a failure while a route is in use.
        if (bytes >= 2 && samples[0] == 0xff && samples[1] == 0xff) connected_ = false;
        if (!connected_ || bytes > 4800 - pending_) return false;
        pending_ += bytes;
        return true;
    }

    bool drain(std::chrono::milliseconds) noexcept override {
        std::lock_guard lock(mutex_);
        if (!connected_) return false;
        report("Played");
        return true;
    }

    void discard() noexcept override {
        std::lock_guard lock(mutex_);
        report("Discarded");
    }

private:
    void report(const char* operation) {
        if (pending_) std::cout << operation << ' ' << pending_ << " TX bytes" << std::endl;
        pending_ = 0;
    }

    std::atomic<bool> connected_{true};
    std::mutex mutex_;
    std::size_t pending_ = 0;
};
#endif
}

int main(int argc, char** argv) {
    uint16_t port = 4532;
    if (argc >= 2) {
        const char* end = argv[1] + std::strlen(argv[1]);
        const auto conversion = std::from_chars(argv[1], end, port);
        if (conversion.ec != std::errc{} || conversion.ptr != end) port = 0;
    }
    const std::string_view policy = argc == 3 ? argv[2] : "";
#ifndef __ANDROID__
    FakePlayback playback;
    const bool tx_fixture = policy == "tx-fixture";
#else
    const bool tx_fixture = false;
#endif
    if (port == 0 || argc > 3 || (argc == 3 && policy != "receive-only" && policy != "ptt"
                                 && policy != "audio-fixture" && policy != "dummy-tx-loopback" && !tx_fixture)) {
        std::cerr << "Usage: rigctld-host [port [receive-only|ptt|audio-fixture|dummy-tx-loopback]]\n";
        return 2;
    }
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    RigctldServer::Radio radio;
    radio.enforce_radio_policy = !policy.empty();
    radio.allow_ptt = policy == "ptt" || tx_fixture;
    radio.usb_audio_rx = policy == "audio-fixture";
    radio.dummy_tx_loopback = policy == "dummy-tx-loopback";
#ifndef __ANDROID__
    if (tx_fixture) radio.tx_playback = &playback;
#endif
    std::atomic<bool> finished{false};
    std::thread producer;
    if (radio.usb_audio_rx) {
        producer = std::thread([&] {
            std::array<int16_t, 480> block;
            block.fill(1234);
            while (!finished) {
                embedded_audio_push(server.audio(), block.data(), sizeof(block));
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
    const auto error = server.run(port, radio, [port] {
        std::cout << "Listening on 127.0.0.1:" << port << std::endl;
    });
    finished = true;
    if (producer.joinable()) producer.join();
    if (error.empty()) return 0;
    std::cerr << error << '\n';
    return 1;
}
