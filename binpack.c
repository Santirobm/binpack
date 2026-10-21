/**
 * BINPACK: High-Performance Polymorphic Binary Compression Library
 * Native C99 Implementation
 * 
 * Copyright (c) 2026 BINPACK Project Contributors
 * Licensed under the MIT License
 */

#include "binpack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* binpack_mode_name(binpack_mode_t mode) {
    switch (mode) {
        case BINPACK_MODE_GENERIC:  return "Generic Binary (Direct Passthrough)";
        case BINPACK_MODE_BCJ_X86:  return "Machine Code x86/x64 (BCJ Filter)";
        case BINPACK_MODE_BCJ_ARM:  return "Machine Code ARM/ARM64 (ARM Filter)";
        case BINPACK_MODE_DELTA_2:  return "Audio 16-bit / Medical Resonance (Delta-2)";
        case BINPACK_MODE_STRIDE_4: return "Numeric / 32-bit Floats (Stride-4 Transpose)";
        case BINPACK_MODE_STRIDE_8: return "Doubles / 64-bit Timestamps (Stride-8 Transpose)";
        case BINPACK_MODE_FWHT:     return "Boolean Spectral Matrix (FWHT)";
        case BINPACK_MODE_STRIDE_2: return "Structured 16-bit Tuples (Stride-2 Transpose)";
        default:                    return "Unknown Mode";
    }
}

/* Branch conversion for x86/x64 (Relative CALL 0xE8 and JMP 0xE9 normalization) */
size_t binpack_filter_bcj_x86(uint8_t *data, size_t size, int encoding) {
    if (size < 5) return size;
    size_t i = 0;
    while (i <= size - 5) {
        uint8_t b = data[i];
        if (b == 0xE8 || b == 0xE9) {
            uint32_t src = (uint32_t)data[i+1] |
                           ((uint32_t)data[i+2] << 8) |
                           ((uint32_t)data[i+3] << 16) |
                           ((uint32_t)data[i+4] << 24);
            uint32_t dest = encoding ? (src + (uint32_t)(i + 5)) : (src - (uint32_t)(i + 5));
            data[i+1] = (uint8_t)(dest);
            data[i+2] = (uint8_t)(dest >> 8);
            data[i+3] = (uint8_t)(dest >> 16);
            data[i+4] = (uint8_t)(dest >> 24);
            i += 5;
        } else {
            i++;
        }
    }
    return size;
}

/* Branch conversion for ARM 32-bit (BL instruction normalization) */
size_t binpack_filter_bcj_arm(uint8_t *data, size_t size, int encoding) {
    if (size < 4) return size;
    for (size_t i = 0; i + 4 <= size; i += 4) {
        if (data[i + 3] == 0xEB) { /* BL opcode */
            uint32_t src = (uint32_t)data[i] |
                           ((uint32_t)data[i+1] << 8) |
                           ((uint32_t)data[i+2] << 16);
            uint32_t dest = encoding ? (src + (uint32_t)((i + 8) >> 2)) : (src - (uint32_t)((i + 8) >> 2));
            data[i]   = (uint8_t)(dest);
            data[i+1] = (uint8_t)(dest >> 8);
            data[i+2] = (uint8_t)(dest >> 16);
        }
    }
    return size;
}

/* Finite-difference delta filters for continuous signals */
void binpack_filter_delta_encode(const uint8_t *src, uint8_t *dst, size_t size, size_t dist) {
    size_t i;
    for (i = 0; i < dist && i < size; i++) dst[i] = src[i];
    for (; i < size; i++) {
        dst[i] = (uint8_t)(src[i] - src[i - dist]);
    }
}

void binpack_filter_delta_decode(const uint8_t *src, uint8_t *dst, size_t size, size_t dist) {
    size_t i;
    for (i = 0; i < dist && i < size; i++) dst[i] = src[i];
    for (; i < size; i++) {
        dst[i] = (uint8_t)(dst[i - dist] + src[i]);
    }
}

/* Multi-channel stride transpositions */
void binpack_transpose(const uint8_t *src, uint8_t *dst, size_t size, size_t stride) {
    size_t pad = (stride - (size % stride)) % stride;
    size_t total = size + pad;
    size_t chunk = total / stride;

    for (size_t s = 0; s < stride; s++) {
        uint8_t *out_ch = dst + s * chunk;
        for (size_t i = 0; i < chunk; i++) {
            size_t src_idx = i * stride + s;
            out_ch[i] = (src_idx < size) ? src[src_idx] : 0;
        }
    }
}

void binpack_itranspose(const uint8_t *src, uint8_t *dst, size_t size, size_t stride) {
    size_t pad = (stride - (size % stride)) % stride;
    size_t total = size + pad;
    size_t chunk = total / stride;

    size_t out_idx = 0;
    for (size_t i = 0; i < chunk; i++) {
        for (size_t s = 0; s < stride; s++) {
            if (out_idx < size) {
                dst[out_idx++] = src[s * chunk + i];
            }
        }
    }
}

/* Calculates maximum symbol concentration (top-2 frequencies) across channels in sub-microsecond time */
static float calc_max_channel_concentration(const uint8_t *data, size_t size, size_t stride) {
    size_t sample_len = (size < 4096) ? size : 4096;
    float max_concentration = 0.0f;
    uint32_t counts[256];

    for (size_t s = 0; s < stride; s++) {
        memset(counts, 0, sizeof(counts));
        size_t ch_len = 0;

        for (size_t i = s; i < sample_len; i += stride) {
            counts[data[i]]++;
            ch_len++;
        }
        if (ch_len == 0) continue;

        /* Find top 2 values */
        uint32_t top1 = 0, top2 = 0;
        for (int b = 0; b < 256; b++) {
            if (counts[b] > top1) {
                top2 = top1;
                top1 = counts[b];
            } else if (counts[b] > top2) {
                top2 = counts[b];
            }
        }

        float c = (float)(top1 + top2) / (float)ch_len;
        if (c > max_concentration) {
            max_concentration = c;
        }
    }
    return max_concentration;
}

/* Profiler: Inspects payload in microseconds to select optimal pre-filter */
binpack_mode_t binpack_detect_mode(const uint8_t *data, size_t size) {
    if (size < 64) return BINPACK_MODE_GENERIC;

    /* 1. PE Executable */
    if (data[0] == 'M' && data[1] == 'Z' && size > 0x40) {
        uint32_t pe_offset = *(const uint32_t*)(data + 0x3C);
        if (pe_offset + 6 < size && data[pe_offset] == 'P' && data[pe_offset+1] == 'E' &&
            data[pe_offset+2] == 0 && data[pe_offset+3] == 0) {
            uint16_t machine = *(const uint16_t*)(data + pe_offset + 4);
            if (machine == 0x01c0 || machine == 0x01c2 || machine == 0x01c4 || machine == 0xaa64) {
                return BINPACK_MODE_BCJ_ARM;
            }
            return BINPACK_MODE_BCJ_X86;
        }
    }

    /* 2. ELF Executable */
    if (data[0] == 0x7F && data[1] == 'E' && data[2] == 'L' && data[3] == 'F' && size > 20) {
        uint16_t machine = *(const uint16_t*)(data + 18);
        if (machine == 0x28 || machine == 0xB7) return BINPACK_MODE_BCJ_ARM;
        return BINPACK_MODE_BCJ_X86;
    }

    /* 3. Mach-O Executable */
    if (size > 8) {
        uint32_t magic = *(const uint32_t*)data;
        if (magic == 0xfeedface || magic == 0xfeedfacf || magic == 0xcefaedfe || magic == 0xcffaedfe) {
            uint32_t cputype = *(const uint32_t*)(data + 4);
            if (cputype == 12 || cputype == 0x0100000c) return BINPACK_MODE_BCJ_ARM;
            return BINPACK_MODE_BCJ_X86;
        }
    }

    /* 4. Smooth 16-bit waveform (Audio PCM, MRI scan) */
    if (size >= 512 && (size % 2 == 0)) {
        size_t sample_len = (size < 2048 ? size : 2048) / 2;
        int64_t diff_sum = 0;
        for (size_t i = 0; i + 1 < sample_len; i++) {
            int16_t v1 = *(const int16_t*)(data + i * 2);
            int16_t v2 = *(const int16_t*)(data + (i + 1) * 2);
            diff_sum += (v2 > v1 ? v2 - v1 : v1 - v2);
        }
        if ((diff_sum / (sample_len - 1)) < 10000) {
            return BINPACK_MODE_DELTA_2;
        }
    }

    /* 5. Stride-8 (Doubles, timestamps) */
    if (size >= 512 && (size % 8 == 0)) {
        if (calc_max_channel_concentration(data, size, 8) > 0.40f) {
            return BINPACK_MODE_STRIDE_8;
        }
    }

    /* 6. Stride-4 (Floats, coordinates) */
    if (size >= 256 && (size % 4 == 0)) {
        if (calc_max_channel_concentration(data, size, 4) > 0.30f) {
            return BINPACK_MODE_STRIDE_4;
        }
    }

    /* 7. Stride-2 (16-bit structured records) */
    if (size >= 256 && (size % 2 == 0)) {
        if (calc_max_channel_concentration(data, size, 2) > 0.20f) {
            return BINPACK_MODE_STRIDE_2;
        }
    }

    return BINPACK_MODE_GENERIC;
}
