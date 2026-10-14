// Copyright 2026 X360 Mobile. SPDX-License-Identifier: BSD-3-Clause
#include "layer_bcn_cpu_decoder.h"
#include <array>
#include <cstdio>

int main() {
    constexpr uint16_t selectors = 0x9365;
    bool correct = true;
    for (uint32_t channels : {1u, 2u}) {
        std::array<uint8_t, 16> payload{};
        for (uint32_t channel = 0; channel < channels; ++channel) {
            payload[channel * 8] = 255;
            uint64_t indices = 0;
            for (uint32_t pixel = 0; pixel < 16; ++pixel)
                indices |= uint64_t(((selectors >> pixel) & 1u) ^ channel) << (pixel * 3);
            for (uint32_t byte = 0; byte < 6; ++byte)
                payload[channel * 8 + 2 + byte] = uint8_t(indices >> (byte * 8));
        }
        BcnCpuDecodeRegion region{};
        region.compressed_format = channels == 1 ? VK_FORMAT_BC4_UNORM_BLOCK : VK_FORMAT_BC5_UNORM_BLOCK;
        region.output_format = channels == 1 ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8G8_UNORM;
        region.width = region.height = 4;
        std::vector<uint8_t> output;
        if (!bcn_cpu_decode_region(payload.data(), channels * 8, region, &output) ||
            output.size() != 16 * channels) return 2;
        for (uint32_t pixel = 0; pixel < 16; ++pixel) {
            for (uint32_t channel = 0; channel < channels; ++channel) {
                const uint8_t expected = (((selectors >> pixel) & 1u) ^ channel) ? 0 : 255;
                if (output[pixel * channels + channel] != expected) {
                    std::printf("FAIL BC%u pixel=%u channel=%u expected=%u actual=%u\n",
                        channels + 3, pixel, channel, expected, output[pixel * channels + channel]);
                    correct = false;
                }
            }
        }
    }
    if (correct) std::puts("PASS: BC4/BC5 UNORM, all 16 texels and channels");
    return correct ? 0 : 1;
}
