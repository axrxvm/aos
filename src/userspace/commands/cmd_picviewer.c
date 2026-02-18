/*
 * === AOS HEADER BEGIN ===
 * src/userspace/commands/cmd_picviewer.c
 * Copyright (c) 2024 - 2026 Aarav Mehta and aOS Contributors
 * Licensed under CC BY-NC 4.0
 * aOS Version : 0.9.0
 * === AOS HEADER END ===
 */

#include <command_registry.h>
#include <vga.h>
#include <string.h>
#include <stdlib.h>
#include <syscall.h>
#include <vmm.h>
#include <keyboard.h>
#include <shell.h>
#include <serial.h>

extern void kprint(const char* str);

typedef struct {
    uint32_t i0;
    uint32_t i1;
    uint32_t frac;
} sample_map_t;

typedef struct {
    const uint8_t* data;
    uint32_t size;
    uint32_t pixel_offset;
    const uint8_t* pixel_data;
    uint32_t pixel_data_size;
    uint32_t width;
    uint32_t height;
    uint16_t bpp;
    uint32_t compression;
    uint32_t dib_size;
    uint32_t row_stride;
    int top_down;
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t alpha_mask;
    const uint8_t* palette;
    uint8_t palette_stride;
    uint32_t palette_colors;
    uint8_t* decoded_indices; // For RLE4/RLE8 streams.
} bmp_image_t;

#define BMP_COMP_RGB             0U
#define BMP_COMP_RLE8            1U
#define BMP_COMP_RLE4            2U
#define BMP_COMP_BITFIELDS       3U
#define BMP_COMP_ALPHABITFIELDS  6U

#define BMP_MAX_DIMENSION        8192U

static uint16_t read_u16_le(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int32_t read_s32_le(const uint8_t* p) {
    return (int32_t)read_u32_le(p);
}

static uint32_t min_u32(uint32_t a, uint32_t b) {
    return (a < b) ? a : b;
}

static void bmp_release(bmp_image_t* bmp) {
    if (!bmp) {
        return;
    }
    if (bmp->decoded_indices) {
        kfree(bmp->decoded_indices);
        bmp->decoded_indices = NULL;
    }
}

static int masks_are_valid(uint32_t red, uint32_t green, uint32_t blue, uint32_t alpha) {
    if (!red || !green || !blue) {
        return 0;
    }
    if ((red & green) || (red & blue) || (green & blue)) {
        return 0;
    }
    if (alpha && ((alpha & red) || (alpha & green) || (alpha & blue))) {
        return 0;
    }
    return 1;
}

static int bmp_compute_row_stride(uint32_t width, uint16_t bpp, uint32_t* out_stride) {
    uint32_t row_stride = 0;

    if (!out_stride || width == 0) {
        return -1;
    }

    if (bpp <= 8U) {
        uint32_t bits_per_row = width * (uint32_t)bpp;
        row_stride = ((bits_per_row + 31U) / 32U) * 4U;
    } else if (bpp == 16U) {
        row_stride = ((width * 2U) + 3U) & ~3U;
    } else if (bpp == 24U) {
        row_stride = ((width * 3U) + 3U) & ~3U;
    } else if (bpp == 32U) {
        row_stride = width * 4U;
    } else {
        return -1;
    }

    *out_stride = row_stride;
    return 0;
}

static void bmp_set_decoded_index(bmp_image_t* bmp, uint32_t file_row, uint32_t x, uint8_t idx) {
    if (!bmp || !bmp->decoded_indices || x >= bmp->width || file_row >= bmp->height) {
        return;
    }

    uint32_t y = bmp->top_down ? file_row : (bmp->height - 1U - file_row);
    bmp->decoded_indices[y * bmp->width + x] = idx;
}

static int bmp_decode_rle_stream(bmp_image_t* bmp) {
    if (!bmp || !bmp->pixel_data || !bmp->pixel_data_size) {
        return -1;
    }
    if (!(bmp->compression == BMP_COMP_RLE8 || bmp->compression == BMP_COMP_RLE4)) {
        return -1;
    }

    if (bmp->width > (0xFFFFFFFFU / bmp->height)) {
        return -1;
    }

    uint32_t decoded_size = bmp->width * bmp->height;
    bmp->decoded_indices = (uint8_t*)kmalloc(decoded_size);
    if (!bmp->decoded_indices) {
        return -1;
    }
    memset(bmp->decoded_indices, 0, decoded_size);

    const uint8_t* src = bmp->pixel_data;
    uint32_t i = 0;
    uint32_t x = 0;
    uint32_t file_row = 0;
    int done = 0;

    while (!done && i + 1U < bmp->pixel_data_size) {
        uint8_t count = src[i++];
        uint8_t value = src[i++];

        if (count != 0U) {
            if (bmp->compression == BMP_COMP_RLE8) {
                for (uint32_t j = 0; j < (uint32_t)count; j++) {
                    bmp_set_decoded_index(bmp, file_row, x + j, value);
                }
            } else {
                uint8_t hi = (value >> 4) & 0x0FU;
                uint8_t lo = value & 0x0FU;
                for (uint32_t j = 0; j < (uint32_t)count; j++) {
                    bmp_set_decoded_index(bmp, file_row, x + j, (j & 1U) ? lo : hi);
                }
            }

            x += (uint32_t)count;
            if (x > bmp->width) {
                x = bmp->width;
            }
            continue;
        }

        if (value == 0U) {
            // End of line
            x = 0;
            file_row++;
            if (file_row >= bmp->height) {
                done = 1;
            }
            continue;
        }

        if (value == 1U) {
            // End of bitmap
            done = 1;
            continue;
        }

        if (value == 2U) {
            // Delta
            if (i + 1U >= bmp->pixel_data_size) {
                bmp_release(bmp);
                return -1;
            }
            uint8_t dx = src[i++];
            uint8_t dy = src[i++];
            x += (uint32_t)dx;
            if (x > bmp->width) {
                x = bmp->width;
            }
            file_row += (uint32_t)dy;
            if (file_row >= bmp->height) {
                done = 1;
            }
            continue;
        }

        // Absolute mode
        uint32_t absolute_count = (uint32_t)value;
        if (bmp->compression == BMP_COMP_RLE8) {
            if (i + absolute_count > bmp->pixel_data_size) {
                bmp_release(bmp);
                return -1;
            }
            for (uint32_t j = 0; j < absolute_count; j++) {
                bmp_set_decoded_index(bmp, file_row, x + j, src[i + j]);
            }
            i += absolute_count;
            if (absolute_count & 1U) {
                if (i >= bmp->pixel_data_size) {
                    bmp_release(bmp);
                    return -1;
                }
                i++;
            }
        } else {
            uint32_t packed_bytes = (absolute_count + 1U) / 2U;
            if (i + packed_bytes > bmp->pixel_data_size) {
                bmp_release(bmp);
                return -1;
            }
            for (uint32_t j = 0; j < absolute_count; j++) {
                uint8_t byte = src[i + (j >> 1)];
                uint8_t idx = (j & 1U) ? (byte & 0x0FU) : ((byte >> 4) & 0x0FU);
                bmp_set_decoded_index(bmp, file_row, x + j, idx);
            }
            i += packed_bytes;
            if (packed_bytes & 1U) {
                if (i >= bmp->pixel_data_size) {
                    bmp_release(bmp);
                    return -1;
                }
                i++;
            }
        }

        x += absolute_count;
        if (x > bmp->width) {
            x = bmp->width;
        }
    }

    return 0;
}

static uint8_t scale_masked_component(uint32_t pixel, uint32_t mask) {
    if (mask == 0U) {
        return 0;
    }

    uint32_t shift = 0U;
    while (((mask >> shift) & 1U) == 0U && shift < 32U) {
        shift++;
    }

    uint32_t shifted_mask = mask >> shift;
    uint32_t bits = 0U;
    while ((shifted_mask & 1U) == 1U) {
        bits++;
        shifted_mask >>= 1U;
    }

    if (bits == 0U || bits > 16U) {
        return 0;
    }

    uint32_t value = (pixel & mask) >> shift;
    uint32_t max_value = (1U << bits) - 1U;
    return (uint8_t)((value * 255U) / max_value);
}

static int parse_bmp(const uint8_t* file_data, uint32_t file_size, bmp_image_t* out) {
    if (!file_data || !out || file_size < 26U) {
        return -1;
    }

    memset(out, 0, sizeof(*out));

    if (file_data[0] != 'B' || file_data[1] != 'M') {
        return -1;
    }

    uint32_t declared_size = read_u32_le(file_data + 2);
    uint32_t pixel_offset = read_u32_le(file_data + 10);
    uint32_t dib_size = read_u32_le(file_data + 14);

    if (declared_size && (declared_size > file_size || declared_size < pixel_offset)) {
        return -1;
    }
    if (pixel_offset >= file_size) {
        return -1;
    }
    if (dib_size < 12U || 14U + dib_size > file_size) {
        return -1;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    int top_down = 0;
    uint16_t planes = 0;
    uint16_t bpp = 0;
    uint32_t compression = BMP_COMP_RGB;
    uint32_t colors_used = 0;
    uint32_t image_size = 0;
    uint8_t palette_stride = (dib_size == 12U) ? 3U : 4U;

    if (dib_size == 12U) {
        // OS/2 BITMAPCOREHEADER
        width = read_u16_le(file_data + 18);
        height = read_u16_le(file_data + 20);
        planes = read_u16_le(file_data + 22);
        bpp = read_u16_le(file_data + 24);
        compression = BMP_COMP_RGB;
    } else {
        int32_t width_signed = read_s32_le(file_data + 18);
        int32_t height_signed = read_s32_le(file_data + 22);
        planes = read_u16_le(file_data + 26);
        bpp = read_u16_le(file_data + 28);
        compression = read_u32_le(file_data + 30);
        image_size = read_u32_le(file_data + 34);
        colors_used = read_u32_le(file_data + 46);

        if (width_signed <= 0 || height_signed == 0) {
            return -1;
        }
        width = (uint32_t)width_signed;
        top_down = (height_signed < 0) ? 1 : 0;
        height = (height_signed < 0) ? (uint32_t)(-height_signed) : (uint32_t)height_signed;
    }

    if (planes != 1U || width == 0U || height == 0U) {
        return -1;
    }
    if (width > BMP_MAX_DIMENSION || height > BMP_MAX_DIMENSION) {
        return -1;
    }

    if (dib_size == 12U) {
        if (!(bpp == 1U || bpp == 4U || bpp == 8U || bpp == 24U)) {
            return -1;
        }
    } else {
        if (!(bpp == 1U || bpp == 2U || bpp == 4U || bpp == 8U || bpp == 16U || bpp == 24U || bpp == 32U)) {
            return -1;
        }

        if (!(compression == BMP_COMP_RGB ||
              compression == BMP_COMP_RLE8 ||
              compression == BMP_COMP_RLE4 ||
              compression == BMP_COMP_BITFIELDS ||
              compression == BMP_COMP_ALPHABITFIELDS)) {
            return -1;
        }

        if (top_down && (compression == BMP_COMP_RLE8 || compression == BMP_COMP_RLE4)) {
            return -1;
        }
        if (compression == BMP_COMP_RLE8 && bpp != 8U) {
            return -1;
        }
        if (compression == BMP_COMP_RLE4 && bpp != 4U) {
            return -1;
        }
        if ((compression == BMP_COMP_BITFIELDS || compression == BMP_COMP_ALPHABITFIELDS) &&
            !(bpp == 16U || bpp == 32U)) {
            return -1;
        }
    }

    uint32_t row_stride = 0;
    if (bmp_compute_row_stride(width, bpp, &row_stride) < 0) {
        return -1;
    }

    const uint8_t* palette = NULL;
    uint32_t palette_colors = 0;

    if (bpp <= 8U) {
        uint32_t palette_offset = 14U + dib_size;
        uint32_t max_palette = 1U << bpp;

        if (palette_offset > pixel_offset || palette_stride == 0U) {
            return -1;
        }

        uint32_t palette_bytes = pixel_offset - palette_offset;
        if ((palette_bytes % palette_stride) != 0U) {
            return -1;
        }

        uint32_t entries_available = palette_bytes / palette_stride;
        if (colors_used == 0U) {
            palette_colors = min_u32(entries_available, max_palette);
        } else {
            if (colors_used > entries_available || colors_used > max_palette) {
                return -1;
            }
            palette_colors = colors_used;
        }

        if (palette_colors == 0U || palette_colors > 256U) {
            return -1;
        }

        palette = file_data + palette_offset;
    }

    uint32_t pixel_data_size = file_size - pixel_offset;
    if (compression == BMP_COMP_RGB || compression == BMP_COMP_BITFIELDS || compression == BMP_COMP_ALPHABITFIELDS) {
        if (height > 0U && row_stride > (0xFFFFFFFFU / height)) {
            return -1;
        }
        uint32_t required_size = row_stride * height;
        if (required_size > pixel_data_size) {
            return -1;
        }
        pixel_data_size = required_size;
    } else {
        if (image_size && image_size <= pixel_data_size) {
            pixel_data_size = image_size;
        }
    }

    uint32_t red_mask = 0U;
    uint32_t green_mask = 0U;
    uint32_t blue_mask = 0U;
    uint32_t alpha_mask = 0U;

    if (bpp == 16U || bpp == 32U) {
        if (compression == BMP_COMP_BITFIELDS || compression == BMP_COMP_ALPHABITFIELDS) {
            uint32_t mask_offset = 0U;
            if (dib_size >= 52U) {
                mask_offset = 14U + 40U;
            } else {
                mask_offset = 14U + dib_size;
            }

            if (mask_offset + 12U > pixel_offset || mask_offset + 12U > file_size) {
                return -1;
            }

            red_mask = read_u32_le(file_data + mask_offset);
            green_mask = read_u32_le(file_data + mask_offset + 4U);
            blue_mask = read_u32_le(file_data + mask_offset + 8U);

            if (compression == BMP_COMP_ALPHABITFIELDS) {
                if (mask_offset + 16U > pixel_offset || mask_offset + 16U > file_size) {
                    return -1;
                }
                alpha_mask = read_u32_le(file_data + mask_offset + 12U);
            } else if (dib_size >= 56U && mask_offset + 16U <= pixel_offset && mask_offset + 16U <= file_size) {
                alpha_mask = read_u32_le(file_data + mask_offset + 12U);
            }
        } else {
            if (bpp == 16U) {
                red_mask = 0x7C00U;
                green_mask = 0x03E0U;
                blue_mask = 0x001FU;
            } else {
                red_mask = 0x00FF0000U;
                green_mask = 0x0000FF00U;
                blue_mask = 0x000000FFU;
                alpha_mask = 0xFF000000U;
            }
        }

        if (!masks_are_valid(red_mask, green_mask, blue_mask, alpha_mask)) {
            return -1;
        }
    }

    out->data = file_data;
    out->size = file_size;
    out->pixel_offset = pixel_offset;
    out->pixel_data = file_data + pixel_offset;
    out->pixel_data_size = pixel_data_size;
    out->width = width;
    out->height = height;
    out->bpp = bpp;
    out->compression = compression;
    out->dib_size = dib_size;
    out->row_stride = row_stride;
    out->top_down = top_down;
    out->red_mask = red_mask;
    out->green_mask = green_mask;
    out->blue_mask = blue_mask;
    out->alpha_mask = alpha_mask;
    out->palette = palette;
    out->palette_stride = palette_stride;
    out->palette_colors = palette_colors;

    if (compression == BMP_COMP_RLE8 || compression == BMP_COMP_RLE4) {
        if (bmp_decode_rle_stream(out) < 0) {
            bmp_release(out);
            return -1;
        }
    }

    return 0;
}

static rgb_color_t bmp_get_pixel_rgb(const bmp_image_t* bmp, uint32_t x, uint32_t y) {
    rgb_color_t rgb = {0, 0, 0};

    if (!bmp || x >= bmp->width || y >= bmp->height) {
        return rgb;
    }

    if (bmp->bpp <= 8U && bmp->palette) {
        uint8_t idx = 0;

        if ((bmp->compression == BMP_COMP_RLE8 || bmp->compression == BMP_COMP_RLE4) && bmp->decoded_indices) {
            idx = bmp->decoded_indices[y * bmp->width + x];
        } else {
            uint32_t src_y = bmp->top_down ? y : (bmp->height - 1U - y);
            const uint8_t* row = bmp->pixel_data + (src_y * bmp->row_stride);

            if (bmp->bpp == 1U) {
                uint8_t byte = row[x >> 3];
                uint8_t shift = 7U - (uint8_t)(x & 7U);
                idx = (byte >> shift) & 0x01U;
            } else if (bmp->bpp == 2U) {
                uint8_t byte = row[x >> 2];
                uint8_t shift = (uint8_t)(6U - ((x & 3U) * 2U));
                idx = (byte >> shift) & 0x03U;
            } else if (bmp->bpp == 4U) {
                uint8_t byte = row[x >> 1];
                idx = ((x & 1U) == 0U) ? ((byte >> 4) & 0x0FU) : (byte & 0x0FU);
            } else {
                idx = row[x];
            }
        }

        if ((uint32_t)idx >= bmp->palette_colors) {
            idx = 0;
        }

        const uint8_t* e = bmp->palette + ((uint32_t)idx * bmp->palette_stride);
        rgb.b = e[0];
        rgb.g = e[1];
        rgb.r = e[2];
        return rgb;
    }

    uint32_t src_y = bmp->top_down ? y : (bmp->height - 1U - y);
    const uint8_t* row = bmp->pixel_data + (src_y * bmp->row_stride);

    if (bmp->bpp == 24U) {
        const uint8_t* p = row + (x * 3U);
        rgb.b = p[0];
        rgb.g = p[1];
        rgb.r = p[2];
        return rgb;
    }

    if (bmp->bpp == 32U) {
        if (bmp->compression == 3U) {
            uint32_t pixel = read_u32_le(row + (x * 4U));
            rgb.r = scale_masked_component(pixel, bmp->red_mask);
            rgb.g = scale_masked_component(pixel, bmp->green_mask);
            rgb.b = scale_masked_component(pixel, bmp->blue_mask);
        } else {
            const uint8_t* p = row + (x * 4U);
            rgb.b = p[0];
            rgb.g = p[1];
            rgb.r = p[2];
        }
        return rgb;
    }

    if (bmp->bpp == 16U) {
        uint32_t pixel = (uint32_t)read_u16_le(row + (x * 2U));
        rgb.r = scale_masked_component(pixel, bmp->red_mask);
        rgb.g = scale_masked_component(pixel, bmp->green_mask);
        rgb.b = scale_masked_component(pixel, bmp->blue_mask);
        return rgb;
    }

    return rgb;
}

static uint32_t rgb_to_mode_color(const rgb_color_t* rgb, const vga_mode_info_t* mode) {
    if (!rgb || !mode) {
        return 0;
    }

    switch (mode->bpp) {
        case 8:
            // Mode 13h uses a deterministic 6x6x6-style palette in this driver.
            return vga_rgb_to_256_palette(*rgb);
        case 16:
            return (uint32_t)vga_rgb_to_rgb565(*rgb);
        case 24:
        case 32:
            return vga_rgb_to_rgb888(*rgb);
        default:
            return 0;
    }
}

static void fill_scale_map(sample_map_t* map, uint32_t dst_size, uint32_t src_size) {
    if (!map || dst_size == 0U) {
        return;
    }

    if (dst_size == 1U || src_size <= 1U) {
        map[0].i0 = 0;
        map[0].i1 = 0;
        map[0].frac = 0;
        return;
    }

    uint32_t range_fp = (src_size - 1U) << 16;
    uint32_t step_fp = range_fp / (dst_size - 1U);

    for (uint32_t i = 0; i < dst_size; i++) {
        uint32_t fp = i * step_fp;
        uint32_t i0 = fp >> 16;
        uint32_t i1 = min_u32(i0 + 1U, src_size - 1U);

        map[i].i0 = i0;
        map[i].i1 = i1;
        map[i].frac = fp & 0xFFFFU;
    }

    map[dst_size - 1U].i0 = src_size - 1U;
    map[dst_size - 1U].i1 = src_size - 1U;
    map[dst_size - 1U].frac = 0U;
}

static rgb_color_t bilinear_sample(const bmp_image_t* bmp, const sample_map_t* x_map, const sample_map_t* y_map,
                                   uint32_t x, uint32_t y) {
    rgb_color_t c00 = bmp_get_pixel_rgb(bmp, x_map[x].i0, y_map[y].i0);
    rgb_color_t c10 = bmp_get_pixel_rgb(bmp, x_map[x].i1, y_map[y].i0);
    rgb_color_t c01 = bmp_get_pixel_rgb(bmp, x_map[x].i0, y_map[y].i1);
    rgb_color_t c11 = bmp_get_pixel_rgb(bmp, x_map[x].i1, y_map[y].i1);

    uint32_t fx = x_map[x].frac;
    uint32_t fy = y_map[y].frac;
    uint32_t inv_fx = 65536U - fx;
    uint32_t inv_fy = 65536U - fy;

    uint32_t r0 = (((uint32_t)c00.r * inv_fx) + ((uint32_t)c10.r * fx)) >> 16;
    uint32_t g0 = (((uint32_t)c00.g * inv_fx) + ((uint32_t)c10.g * fx)) >> 16;
    uint32_t b0 = (((uint32_t)c00.b * inv_fx) + ((uint32_t)c10.b * fx)) >> 16;

    uint32_t r1 = (((uint32_t)c01.r * inv_fx) + ((uint32_t)c11.r * fx)) >> 16;
    uint32_t g1 = (((uint32_t)c01.g * inv_fx) + ((uint32_t)c11.g * fx)) >> 16;
    uint32_t b1 = (((uint32_t)c01.b * inv_fx) + ((uint32_t)c11.b * fx)) >> 16;

    rgb_color_t out;
    out.r = (uint8_t)((((uint32_t)r0 * inv_fy) + ((uint32_t)r1 * fy)) >> 16);
    out.g = (uint8_t)((((uint32_t)g0 * inv_fy) + ((uint32_t)g1 * fy)) >> 16);
    out.b = (uint8_t)((((uint32_t)b0 * inv_fy) + ((uint32_t)b1 * fy)) >> 16);

    return out;
}

static rgb_color_t nearest_sample(const bmp_image_t* bmp, const sample_map_t* x_map, const sample_map_t* y_map,
                                  uint32_t x, uint32_t y) {
    return bmp_get_pixel_rgb(bmp, x_map[x].i0, y_map[y].i0);
}

static int mode_is_valid_for_picview(const vga_mode_info_t* info, uint16_t requested_mode) {
    if (!info || info->type != VGA_MODE_GRAPHICS || info->width == 0 || info->height == 0) {
        return 0;
    }

    if (!(info->bpp == 8 || info->bpp == 16 || info->bpp == 24 || info->bpp == 32)) {
        return 0;
    }

    if (info->bpp >= 16) {
        uint32_t min_pitch = info->width * (uint32_t)(info->bpp / 8);
        if (info->pitch < min_pitch) {
            return 0;
        }
    }

    if (requested_mode == VGA_MODE_320x200x256) {
        return (info->bpp == 8 && info->width == 320 && info->height == 200);
    }

    // For VBE targets, enforce true-color only. This avoids broken indexed paths.
    return (info->bpp == 24 || info->bpp == 32);
}

static int try_mode(uint16_t mode) {
    if (!vga_set_mode(mode)) {
        return 0;
    }

    vga_mode_info_t* info = vga_get_mode_info();
    return mode_is_valid_for_picview(info, mode);
}

static int select_best_graphics_mode(void) {
    static const uint16_t preferred_truecolor_modes[] = {
        VBE_MODE_1024x768x16M,
        VBE_MODE_800x600x16M,
        VBE_MODE_640x480x16M
    };

    for (uint32_t i = 0; i < (sizeof(preferred_truecolor_modes) / sizeof(preferred_truecolor_modes[0])); i++) {
        if (try_mode(preferred_truecolor_modes[i])) {
            return 0;
        }
    }

    // Guaranteed fallback path that this kernel configures deterministically.
    return try_mode(VGA_MODE_320x200x256) ? 0 : -1;
}

static void reset_to_text_mode(void) {
    if (vga_set_mode(0x03)) {
        vga_init();
        vga_clear();
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
}

static void picview_wait_exit_key(void) {
    serial_puts("picview: Press ESC, Enter, Space, q, or x to exit\n");

    for (int i = 0; i < 12; i++) {
        keyboard_get_scancode();
    }

    while (!shell_is_cancelled()) {
        uint8_t scan = keyboard_get_scancode();
        if (scan == 0x01 || scan == 0x1C || scan == 0x39 || scan == 0x10 || scan == 0x2D) {
            return;
        }
    }
}

static int render_bmp_scaled(const bmp_image_t* bmp) {
    if (select_best_graphics_mode() < 0) {
        return -1;
    }

    vga_mode_info_t* mode = vga_get_mode_info();
    if (!mode || mode->type != VGA_MODE_GRAPHICS || mode->width == 0 || mode->height == 0) {
        return -1;
    }

    uint32_t screen_w = mode->width;
    uint32_t screen_h = mode->height;

    uint32_t draw_w = screen_w;
    uint32_t draw_h = (bmp->height * screen_w) / bmp->width;

    if (draw_h == 0U) {
        draw_h = 1U;
    }

    if (draw_h > screen_h) {
        draw_h = screen_h;
        draw_w = (bmp->width * screen_h) / bmp->height;
        if (draw_w == 0U) {
            draw_w = 1U;
        }
    }

    uint32_t off_x = (screen_w - draw_w) / 2U;
    uint32_t off_y = (screen_h - draw_h) / 2U;

    vga_clear_screen(0);

    sample_map_t* x_map = (sample_map_t*)kmalloc(sizeof(sample_map_t) * draw_w);
    sample_map_t* y_map = (sample_map_t*)kmalloc(sizeof(sample_map_t) * draw_h);

    if (!x_map || !y_map) {
        if (x_map) {
            kfree(x_map);
        }
        if (y_map) {
            kfree(y_map);
        }
        return -1;
    }

    fill_scale_map(x_map, draw_w, bmp->width);
    fill_scale_map(y_map, draw_h, bmp->height);

    // Preserve hard edges for indexed/low-color BMPs.
    int use_bilinear = (bmp->bpp >= 24U);

    for (uint32_t y = 0; y < draw_h; y++) {
        if ((y & 0x0FU) == 0U && shell_is_cancelled()) {
            kfree(x_map);
            kfree(y_map);
            return -1;
        }

        for (uint32_t x = 0; x < draw_w; x++) {
            rgb_color_t rgb = use_bilinear
                ? bilinear_sample(bmp, x_map, y_map, x, y)
                : nearest_sample(bmp, x_map, y_map, x, y);
            uint32_t color = rgb_to_mode_color(&rgb, mode);
            vga_plot_pixel((uint16_t)(off_x + x), (uint16_t)(off_y + y), color);
        }
    }

    kfree(x_map);
    kfree(y_map);
    return 0;
}

static void cmd_picview(const char* args) {
    if (!args || !*args) {
        kprint("Usage: picview <path-to-bmp>");
        return;
    }

    while (*args == ' ' || *args == '\t') {
        args++;
    }

    if (!*args) {
        kprint("Usage: picview <path-to-bmp>");
        return;
    }

    char path[256];
    uint32_t path_len = 0;
    while (args[path_len] && args[path_len] != ' ' && args[path_len] != '\t' && path_len < sizeof(path) - 1U) {
        path[path_len] = args[path_len];
        path_len++;
    }
    path[path_len] = '\0';

    if (path_len == 0U) {
        kprint("picview: invalid path");
        return;
    }

    int fd = sys_open(path, O_RDONLY);
    if (fd < 0) {
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_puts("picview: cannot open '");
        vga_puts(path);
        vga_puts("'\n");
        vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }

    int file_size = sys_lseek(fd, 0, SEEK_END);
    if (file_size <= 0) {
        sys_close(fd);
        kprint("picview: invalid file size");
        return;
    }

    if (sys_lseek(fd, 0, SEEK_SET) < 0) {
        sys_close(fd);
        kprint("picview: failed to seek file");
        return;
    }

    uint8_t* file_data = (uint8_t*)kmalloc((size_t)file_size);
    if (!file_data) {
        sys_close(fd);
        kprint("picview: out of memory");
        return;
    }

    int bytes_read = sys_read(fd, file_data, (uint32_t)file_size);
    sys_close(fd);

    if (bytes_read != file_size) {
        kfree(file_data);
        kprint("picview: failed to read BMP file");
        return;
    }

    bmp_image_t bmp;
    memset(&bmp, 0, sizeof(bmp));
    if (parse_bmp(file_data, (uint32_t)file_size, &bmp) < 0) {
        bmp_release(&bmp);
        kfree(file_data);
        kprint("picview: unsupported BMP (supports CORE/INFO, 1/2/4/8/16/24/32-bit, RGB/RLE4/RLE8/bitfields)");
        return;
    }

    if (render_bmp_scaled(&bmp) < 0) {
        bmp_release(&bmp);
        kfree(file_data);
        reset_to_text_mode();
        kprint("picview: failed to render image");
        return;
    }

    picview_wait_exit_key();
    reset_to_text_mode();

    bmp_release(&bmp);
    kfree(file_data);

    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    kprint("picview: image closed");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

void cmd_module_picviewer_register(void) {
    command_register_with_category(
        "picview",
        "<path>",
        "View BMP image in graphics mode",
        "Graphics",
        cmd_picview
    );
}
