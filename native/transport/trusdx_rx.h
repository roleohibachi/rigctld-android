// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

// The radio reserves ';' as a delimiter and prefixes unsigned PCM with US.
// Outputs can require count + 1 CAT bytes and count * 7 PCM samples.
class TruSdxRx {
public:
    static constexpr int input_rate = 7825;
    static constexpr int output_rate = 48000;

    struct Output {
        std::size_t cat_bytes = 0;
        std::size_t audio_samples = 0;
        std::size_t input_samples = 0;
    };

    Output consume(const uint8_t* input, std::size_t count, uint8_t* cat, int16_t* audio);

private:
    enum class Frame { Start, Prefix, Cat, Audio };
    Frame frame_ = Frame::Start;
    bool have_previous_ = false;
    int previous_ = 0;
    int phase_ = 0;
};
