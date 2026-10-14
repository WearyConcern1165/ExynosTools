#include "layer_bcn_cpu_decoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "bcdec.h"

namespace {

uint32_t bcn_block_size(VkFormat format) {
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
            return 8;
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK:
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return 16;
        default:
            return 0;
    }
}

bool is_supported_output_format(VkFormat format) {
    return bcn_cpu_output_texel_size(format) != 0;
}

bool checked_mul_size(size_t a, size_t b, size_t* out) {
    if (!out) {
        return false;
    }
    if (a != 0 && b > (std::numeric_limits<size_t>::max() / a)) {
        return false;
    }
    *out = a * b;
    return true;
}

bool checked_add_size(size_t a, size_t b, size_t* out) {
    if (!out || b > (std::numeric_limits<size_t>::max() - a)) {
        return false;
    }
    *out = a + b;
    return true;
}

void clear_alpha_for_rgb_only_bc1(uint8_t* block, uint32_t pitch) {
    for (uint32_t y = 0; y < 4; ++y) {
        uint8_t* row = block + y * pitch;
        for (uint32_t x = 0; x < 4; ++x) {
            row[x * 4 + 3] = 0xff;
        }
    }
}

bool is_srgb_compressed_format(VkFormat format) {
    switch (format) {
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            return true;
        default:
            return false;
    }
}

uint8_t srgb_to_linear_byte(uint8_t value) {
    float s = static_cast<float>(value) / 255.0f;
    float linear = s <= 0.04045f ? (s / 12.92f) : std::pow((s + 0.055f) / 1.055f, 2.4f);
    linear = std::clamp(linear, 0.0f, 1.0f);
    return static_cast<uint8_t>(linear * 255.0f + 0.5f);
}

void convert_srgb_block_to_linear(uint8_t* block, uint32_t pitch) {
    for (uint32_t y = 0; y < 4; ++y) {
        uint8_t* row = block + y * pitch;
        for (uint32_t x = 0; x < 4; ++x) {
            row[x * 4 + 0] = srgb_to_linear_byte(row[x * 4 + 0]);
            row[x * 4 + 1] = srgb_to_linear_byte(row[x * 4 + 1]);
            row[x * 4 + 2] = srgb_to_linear_byte(row[x * 4 + 2]);
        }
    }
}

void decode_block_to_rgba8(VkFormat format, const uint8_t* src, uint8_t* dst, uint32_t pitch) {
    switch (format) {
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
            bcdec_bc1(src, dst, static_cast<int>(pitch));
            clear_alpha_for_rgb_only_bc1(dst, pitch);
            break;
        case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
            bcdec_bc1(src, dst, static_cast<int>(pitch));
            break;
        case VK_FORMAT_BC2_UNORM_BLOCK:
        case VK_FORMAT_BC2_SRGB_BLOCK:
            bcdec_bc2(src, dst, static_cast<int>(pitch));
            break;
        case VK_FORMAT_BC3_UNORM_BLOCK:
        case VK_FORMAT_BC3_SRGB_BLOCK:
            bcdec_bc3(src, dst, static_cast<int>(pitch));
            break;
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK: {
            const bool signed_format = format == VK_FORMAT_BC4_SNORM_BLOCK;
            uint8_t packed[16]{};
            bcdec_bc4(src, packed, 4, signed_format ? 1 : 0);
            for (uint32_t y = 0; y < 4; ++y) {
                uint8_t* row = dst + y * pitch;
                for (uint32_t x = 0; x < 4; ++x) {
                    row[x * 4] = packed[y * 4 + x];
                    row[x * 4 + 1] = 0;
                    row[x * 4 + 2] = 0;
                    row[x * 4 + 3] = signed_format ? 127u : 255u;
                }
            }
            break;
        }
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK: {
            const bool signed_format = format == VK_FORMAT_BC5_SNORM_BLOCK;
            uint8_t packed[32]{};
            bcdec_bc5(src, packed, 8, signed_format ? 1 : 0);
            for (uint32_t y = 0; y < 4; ++y) {
                uint8_t* row = dst + y * pitch;
                for (uint32_t x = 0; x < 4; ++x) {
                    row[x * 4] = packed[(y * 4 + x) * 2];
                    row[x * 4 + 1] = packed[(y * 4 + x) * 2 + 1];
                    row[x * 4 + 2] = 0;
                    row[x * 4 + 3] = signed_format ? 127u : 255u;
                }
            }
            break;
        }
        case VK_FORMAT_BC7_UNORM_BLOCK:
        case VK_FORMAT_BC7_SRGB_BLOCK:
            bcdec_bc7(src, dst, static_cast<int>(pitch));
            break;
        default:
            break;
    }
}

void copy_rgba8_to_output(
    VkFormat output_format,
    const uint8_t* rgba,
    uint8_t* dst,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t dst_pitch) {
    if (x >= width || y >= height) {
        return;
    }
    switch (output_format) {
        case VK_FORMAT_R8_UNORM:
        case VK_FORMAT_R8_SNORM:
            dst[y * dst_pitch + x] = rgba[0];
            break;
        case VK_FORMAT_R8G8_UNORM:
        case VK_FORMAT_R8G8_SNORM: {
            uint8_t* texel = dst + y * dst_pitch + x * 2u;
            texel[0] = rgba[0];
            texel[1] = rgba[1];
            break;
        }
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_R8G8B8A8_SNORM:
            std::memcpy(dst + y * dst_pitch + x * 4u, rgba, 4);
            break;
        default:
            break;
    }
}

}  // namespace

uint32_t bcn_cpu_output_texel_size(VkFormat output_format) {
    switch (output_format) {
        case VK_FORMAT_R8_UNORM:
        case VK_FORMAT_R8_SNORM:
            return 1;
        case VK_FORMAT_R8G8_UNORM:
        case VK_FORMAT_R8G8_SNORM:
            return 2;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_R8G8B8A8_SNORM:
            return 4;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return 8;
        default:
            return 0;
    }
}

bool bcn_cpu_decode_region(
    const uint8_t* src_base,
    size_t src_size,
    const BcnCpuDecodeRegion& region,
    std::vector<uint8_t>* out_pixels) {
    if (!src_base || !out_pixels || region.width == 0 || region.height == 0) {
        return false;
    }
    if (!is_supported_output_format(region.output_format)) {
        return false;
    }

    const uint32_t block_size = bcn_block_size(region.compressed_format);
    const uint32_t texel_size = bcn_cpu_output_texel_size(region.output_format);
    if (block_size == 0 || texel_size == 0) {
        return false;
    }

    const uint32_t row_texels =
        region.buffer_row_length ? region.buffer_row_length : region.width;
    const uint32_t image_rows =
        region.buffer_image_height ? region.buffer_image_height : region.height;
    const uint32_t blocks_per_row = (row_texels + 3u) / 4u;
    const uint32_t blocks_per_image = blocks_per_row * ((image_rows + 3u) / 4u);
    const uint32_t copy_blocks_x = (region.width + 3u) / 4u;
    const uint32_t copy_blocks_y = (region.height + 3u) / 4u;

    size_t compressed_footprint = 0;
    if (!checked_mul_size(blocks_per_image, block_size, &compressed_footprint)) {
        return false;
    }
    size_t required_end = 0;
    if (!checked_add_size(static_cast<size_t>(region.buffer_offset), compressed_footprint, &required_end) ||
        required_end > src_size) {
        return false;
    }

    size_t row_bytes = 0;
    size_t output_size = 0;
    if (!checked_mul_size(region.width, texel_size, &row_bytes) ||
        !checked_mul_size(row_bytes, region.height, &output_size)) {
        return false;
    }
    out_pixels->assign(output_size, 0);

    const uint8_t* src_origin = src_base + static_cast<size_t>(region.buffer_offset);
    const uint32_t dst_pitch = static_cast<uint32_t>(row_bytes);
    uint8_t rgba_block[4 * 4 * 4]{};
    uint16_t rgba16f_block[4 * 4 * 4]{};

    for (uint32_t block_y = 0; block_y < copy_blocks_y; ++block_y) {
        for (uint32_t block_x = 0; block_x < copy_blocks_x; ++block_x) {
            const uint32_t block_index = block_y * blocks_per_row + block_x;
            const uint8_t* src_block = src_origin + static_cast<size_t>(block_index) * block_size;

            if (region.output_format == VK_FORMAT_R16G16B16A16_SFLOAT) {
                const bool signed_format = region.compressed_format == VK_FORMAT_BC6H_SFLOAT_BLOCK;
                bcdec_bc6h_half(src_block, rgba16f_block, 4 * 3, signed_format ? 1 : 0);
                for (uint32_t py = 0; py < 4; ++py) {
                    for (uint32_t px = 0; px < 4; ++px) {
                        const uint32_t dst_x = block_x * 4u + px;
                        const uint32_t dst_y = block_y * 4u + py;
                        if (dst_x >= region.width || dst_y >= region.height) {
                            continue;
                        }
                        const uint32_t src_pixel = (py * 4u + px) * 3u;
                        auto* dst_texel = reinterpret_cast<uint16_t*>(
                            out_pixels->data() + dst_y * dst_pitch + dst_x * 8u);
                        dst_texel[0] = rgba16f_block[src_pixel + 0];
                        dst_texel[1] = rgba16f_block[src_pixel + 1];
                        dst_texel[2] = rgba16f_block[src_pixel + 2];
                        dst_texel[3] = 0x3c00u;
                    }
                }
                continue;
            }

            std::fill(std::begin(rgba_block), std::end(rgba_block), 0);
            decode_block_to_rgba8(region.compressed_format, src_block, rgba_block, 4 * 4);
            if (is_srgb_compressed_format(region.compressed_format) &&
                region.output_format == VK_FORMAT_R8G8B8A8_UNORM) {
                convert_srgb_block_to_linear(rgba_block, 4 * 4);
            }
            for (uint32_t py = 0; py < 4; ++py) {
                for (uint32_t px = 0; px < 4; ++px) {
                    const uint32_t dst_x = block_x * 4u + px;
                    const uint32_t dst_y = block_y * 4u + py;
                    const uint8_t* rgba = rgba_block + (py * 4u + px) * 4u;
                    copy_rgba8_to_output(
                        region.output_format,
                        rgba,
                        out_pixels->data(),
                        dst_x,
                        dst_y,
                        region.width,
                        region.height,
                        dst_pitch);
                }
            }
        }
    }

    return true;
}
