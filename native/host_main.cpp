// SPDX-License-Identifier: GPL-3.0-or-later
#include "server.h"

#include <charconv>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string_view>

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
    if (port == 0 || argc > 3 || (argc == 3 && policy != "receive-only" && policy != "ptt")) {
        std::cerr << "Usage: rigctld-host [port [receive-only|ptt]]\n";
        return 2;
    }
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    RigctldServer::Radio radio;
    radio.enforce_radio_policy = !policy.empty();
    radio.allow_ptt = policy == "ptt";
    const auto error = server.run(port, radio, [port] {
        std::cout << "Listening on 127.0.0.1:" << port << std::endl;
    });
    if (error.empty()) return 0;
    std::cerr << error << '\n';
    return 1;
}
