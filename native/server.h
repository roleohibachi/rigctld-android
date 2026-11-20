// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

class RigctldServer {
public:
    struct Radio {
        unsigned model = 1;
        std::string path;
        std::string baud;
        std::string handshake;
        bool allow_ptt = false;
        bool enforce_radio_policy = false;
    };

    // One worker calls run(). Any thread may request stop.
    std::string run(uint16_t port, const Radio& radio, const std::function<void()>& on_listening);
    void stop() noexcept;

private:
    static_assert(std::atomic<bool>::is_always_lock_free);
    std::atomic<bool> stopping_{false};
};
