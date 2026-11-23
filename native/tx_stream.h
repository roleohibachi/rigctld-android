// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstddef>
#include <memory>

struct s_rig;

class TxPlayback {
public:
    virtual ~TxPlayback() = default;
    virtual bool available() const noexcept = 0;
    // Queue complete PCM frames without waiting. False means the route failed
    // or its bounded queue is full; the session must then abort.
    virtual bool enqueue(const void* pcm, std::size_t bytes) noexcept = 0;
    // Wait for accepted samples to finish without exceeding the given timeout.
    // A concurrent discard cancels the wait and must stop output promptly.
    virtual bool drain(std::chrono::milliseconds timeout) noexcept = 0;
    virtual void discard() noexcept = 0;
};

// One radio session. Control operations run on the CAT worker. Packet ingress
// and stream teardown may also come from Hamlib's UDP worker.
class TxStream {
public:
    TxStream(s_rig* rig, TxPlayback& playback);
    ~TxStream();

    int attach();
    bool canKey(int client) const;
    void keyed(int client);
    void finish(bool graceful);
    bool healthy() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};
