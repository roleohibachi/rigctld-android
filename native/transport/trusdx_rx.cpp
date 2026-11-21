// SPDX-License-Identifier: GPL-3.0-or-later
#include "trusdx_rx.h"

TruSdxRx::Output TruSdxRx::consume(const uint8_t* input, std::size_t count,
                                   uint8_t* cat, int16_t* audio) {
    Output output;
    for (std::size_t i = 0; i < count; ++i) {
        const uint8_t byte = input[i];
        if (frame_ == Frame::Audio) {
            if (byte == ';') {
                frame_ = Frame::Start;
                continue;
            }
            const int sample = (int(byte) - 128) * 256;
            ++output.input_samples;
            if (have_previous_) {
                while (phase_ < output_rate) {
                    audio[output.audio_samples++] = previous_ + (sample - previous_) * int64_t(phase_) / output_rate;
                    phase_ += input_rate;
                }
                phase_ -= output_rate;
            }
            previous_ = sample;
            have_previous_ = true;
            continue;
        }
        if (frame_ == Frame::Start) {
            if (byte == ';') continue;
            if (byte == 'U') {
                frame_ = Frame::Prefix;
                continue;
            }
        } else if (frame_ == Frame::Prefix) {
            if (byte == 'S') {
                frame_ = Frame::Audio;
                continue;
            }
            cat[output.cat_bytes++] = 'U';
        }
        cat[output.cat_bytes++] = byte;
        frame_ = byte == ';' ? Frame::Start : Frame::Cat;
    }
    return output;
}
