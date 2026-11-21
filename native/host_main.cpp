// SPDX-License-Identifier: GPL-3.0-or-later
#include "server.h"

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
}

int main(int argc, char** argv) {
    uint16_t port = 4532;
    if (argc >= 2) {
        const char* end = argv[1] + std::strlen(argv[1]);
        const auto conversion = std::from_chars(argv[1], end, port);
        if (conversion.ec != std::errc{} || conversion.ptr != end) port = 0;
    }
    const std::string_view policy = argc == 3 ? argv[2] : "";
    if (port == 0 || argc > 3 || (argc == 3 && policy != "receive-only" && policy != "ptt"
                                 && policy != "audio-fixture")) {
        std::cerr << "Usage: rigctld-host [port [receive-only|ptt|audio-fixture]]\n";
        return 2;
    }
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    RigctldServer::Radio radio;
    radio.enforce_radio_policy = !policy.empty();
    radio.allow_ptt = policy == "ptt";
    radio.usb_audio_rx = policy == "audio-fixture";
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
