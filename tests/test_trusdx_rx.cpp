// SPDX-License-Identifier: GPL-3.0-or-later
#include "trusdx_rx.h"

#include <array>
#include <cassert>
#include <initializer_list>
#include <string>
#include <vector>

int main() {
    TruSdxRx parser;
    std::string cat;
    std::vector<int16_t> audio;
    const auto feed = [&](std::initializer_list<uint8_t> chunk) {
        std::array<uint8_t, 32> replies;
        std::array<int16_t, 224> pcm;
        const auto out = parser.consume(chunk.begin(), chunk.size(), replies.data(), pcm.data());
        cat.append(replies.begin(), replies.begin() + out.cat_bytes);
        audio.insert(audio.end(), pcm.begin(), pcm.begin() + out.audio_samples);
        return out.input_samples;
    };
    assert(feed({';', 'U'}) == 0);
    assert(feed({'S', 128, 192}) == 2);
    assert(feed({255, ';', 'F', 'A', '0', '0', '0'}) == 1);
    assert(feed({'7', '0', '4', '0', '0', '0', '0', ';', 'U'}) == 0);
    assert(feed({'A', '1', ';', 'U', 'S', 0, 128, ';', 'M', 'D', '2', ';'}) == 2);
    assert(cat == "FA0007040000;UA1;MD2;");
    assert(audio.size() == 25);
    assert(audio.front() == 0 && audio[1] > 0);
    assert(audio[12] > 29000 && audio[24] < 0);

    TruSdxRx rate;
    const uint8_t prefix[]{'U', 'S', 128};
    std::array<uint8_t, 4> reply;
    std::array<int16_t, 21> pcm;
    assert(rate.consume(prefix, sizeof(prefix), reply.data(), pcm.data()).audio_samples == 0);
    std::size_t frames = 0;
    const uint8_t silence = 128;
    for (int i = 0; i < TruSdxRx::input_rate; ++i) {
        auto result = rate.consume(&silence, 1, reply.data(), pcm.data());
        assert(result.cat_bytes == 0 && result.input_samples == 1);
        for (std::size_t j = 0; j < result.audio_samples; ++j) assert(pcm[j] == 0);
        frames += result.audio_samples;
    }
    assert(frames == TruSdxRx::output_rate);
}
