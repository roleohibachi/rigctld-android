// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include "embedded_audio.h"

class TxPlayback;

class RigctldServer {
public:
    struct Radio {
        unsigned model = 1;
        std::string path;
        std::string baud;
        std::string handshake;
        bool allow_ptt = false;
        bool enforce_radio_policy = false;
        bool usb_audio_rx = false;
        bool dummy_tx_loopback = false;
        TxPlayback* tx_playback = nullptr;
    };

    RigctldServer();
    ~RigctldServer();
    embedded_audio* audio() noexcept { return audio_; }

    // One worker calls run(). Any thread may request stop.
    std::string run(uint16_t port, const Radio& radio, const std::function<void()>& on_listening);
    void stop() noexcept;

private:
    static_assert(std::atomic<bool>::is_always_lock_free);
    std::atomic<bool> stopping_{false};
    embedded_audio* audio_;
};
