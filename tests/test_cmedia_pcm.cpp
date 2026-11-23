// SPDX-License-Identifier: GPL-3.0-or-later
#include "cmedia_pcm.h"

#include <array>
#include <cassert>

int main() {
    CMediaPcm pcm;
    constexpr std::array<unsigned char, 6> mono{0x00, 0x80, 0xff, 0x7f, 0xcd, 0xab};
    constexpr std::array<unsigned char, 16> stereo{
        0x00, 0x80, 0x00, 0x80, 0xff, 0x7f, 0xff, 0x7f,
        0xcd, 0xab, 0xcd, 0xab, 0, 0, 0, 0};
    std::array<unsigned char, 16> output;
    assert(!pcm.push(mono.data(), 1));
    assert(pcm.push(mono.data(), mono.size()));
    assert(pcm.render(output.data(), output.size() / 4) == 3);
    assert(output == stereo);
    assert(pcm.pending() == 0);

    // Position the cursor at the end of the ring, then split one write
    // and one read across the boundary.
    constexpr std::array<unsigned char, (CMediaPcm::capacity - 4) * 2> zeros{};
    std::array<unsigned char, (CMediaPcm::capacity - 4) * 4> scratch;
    assert(pcm.push(zeros.data(), zeros.size()));
    assert(pcm.render(scratch.data(), scratch.size() / 4) == CMediaPcm::capacity - 4);
    assert(pcm.push(mono.data(), mono.size()));
    assert(pcm.render(output.data(), output.size() / 4) == 3);
    assert(output == stereo);

    constexpr std::array<unsigned char, CMediaPcm::capacity * 2> full{};
    assert(pcm.push(full.data(), full.size()));
    assert(!pcm.push(mono.data(), mono.size()));
    assert(pcm.pending() == CMediaPcm::capacity);
    pcm.clear();
    output.fill(0xff);
    assert(pcm.render(output.data(), output.size() / 4) == 0);
    assert((output == std::array<unsigned char, 16>{}));
}
