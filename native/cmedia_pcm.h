// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

// The Hamlib stream carries 48 kHz mono S16LE. C-Media's output
// alternate setting accepts the same sample rate with two channels.
class CMediaPcm {
public:
    static constexpr std::size_t capacity = 4800;  // 100 ms at 48 kHz

    bool push(const void* pcm, std::size_t bytes) {
        if (bytes % 2 != 0 || bytes / 2 > capacity - count_) return false;
        const auto* input = static_cast<const unsigned char*>(pcm);
        for (std::size_t offset = 0; offset < bytes; offset += 2) {
            samples_[(head_ + count_) % capacity] = {input[offset], input[offset + 1]};
            ++count_;
        }
        return true;
    }

    // Render one USB transfer. Unused frames are filled with silence so
    // startup, early completion and empty queues use the same format.
    std::size_t render(unsigned char* stereo, std::size_t frames) {
        std::size_t taken = std::min(count_, frames);
        for (std::size_t i = 0; i < taken; ++i) {
            const auto& sample = samples_[(head_ + i) % capacity];
            stereo[4 * i] = stereo[4 * i + 2] = sample[0];
            stereo[4 * i + 1] = stereo[4 * i + 3] = sample[1];
        }
        std::fill(stereo + 4 * taken, stereo + 4 * frames, 0);
        head_ = (head_ + taken) % capacity;
        count_ -= taken;
        return taken;
    }

    std::size_t pending() const { return count_; }

    void clear() {
        head_ = 0;
        count_ = 0;
    }

private:
    std::array<std::array<unsigned char, 2>, capacity> samples_{};
    std::size_t head_ = 0;
    std::size_t count_ = 0;
};
