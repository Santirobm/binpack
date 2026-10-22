/**
 * BINPACK: High-Performance Polymorphic Binary Compression Library
 * Native C99 Implementation
 * 
 * Copyright (c) 2026 BINPACK Project Contributors
 * Licensed under the MIT License
 */

#include "binpack.h"
#include <zstd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Safe unaligned little-endian reads and writes */
static inline uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static inline int16_t read_i16_le(const uint8_t *p) {
    return (int16_t)read_u16_le(p);
}

static inline void write_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static inline uint64_t read_u64_be(const uint8_t *p) {
    return ((uint64_t)p[0] << 56) |
           ((uint64_t)p[1] << 48) |
           ((uint64_t)p[2] << 40) |
           ((uint64_t)p[3] << 32) |
           ((uint64_t)p[4] << 24) |
           ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] << 8)  |
           ((uint64_t)p[7]);
}

static inline void write_u64_be(uint8_t *p, uint64_t v) {
    p[0] = (uint8_t)(v >> 56);
    p[1] = (uint8_t)(v >> 48);
    p[2] = (uint8_t)(v >> 40);
    p[3] = (uint8_t)(v >> 32);
    p[4] = (uint8_t)(v >> 24);
    p[5] = (uint8_t)(v >> 16);
    p[6] = (uint8_t)(v >> 8);
    p[7] = (uint8_t)(v);
}

/* ========================================================================= */
/* Self-Contained Standard SHA-256 Implementation (Zero External Crypto Lib)  */
/* ========================================================================= */

typedef struct {
    uint8_t  data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} sha256_ctx_t;

#define ROTRIGHT(a,b) (((a) >> (b)) | ((a) << (32-(b))))
#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x,2) ^ ROTRIGHT(x,13) ^ ROTRIGHT(x,22))
#define EP1(x) (ROTRIGHT(x,6) ^ ROTRIGHT(x,11) ^ ROTRIGHT(x,25))
#define SIG0(x) (ROTRIGHT(x,7) ^ ROTRIGHT(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x,17) ^ ROTRIGHT(x,19) ^ ((x) >> 10))

static const uint32_t sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static void sha256_transform(sha256_ctx_t *ctx, const uint8_t data[]) {
    uint32_t a, b, c, d, e, f, g, h, i, j, t1, t2, m[64];

    for (i = 0, j = 0; i < 16; ++i, j += 4)
        m[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j + 1] << 16) | ((uint32_t)data[j + 2] << 8) | ((uint32_t)data[j + 3]);
    for ( ; i < 64; ++i)
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (i = 0; i < 64; ++i) {
        t1 = h + EP1(e) + CH(e,f,g) + sha256_k[i] + m[i];
        t2 = EP0(a) + MAJ(a,b,c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(sha256_ctx_t *ctx) {
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

static void sha256_update(sha256_ctx_t *ctx, const uint8_t data[], size_t len) {
    for (size_t i = 0; i < len; ++i) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

static void sha256_final(sha256_ctx_t *ctx, uint8_t hash[]) {
    uint32_t i = ctx->datalen;

    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80;
        while (i < 56) ctx->data[i++] = 0x00;
    } else {
        ctx->data[i++] = 0x80;
        while (i < 64) ctx->data[i++] = 0x00;
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }

    ctx->bitlen += (uint64_t)ctx->datalen * 8;
    ctx->data[63] = (uint8_t)(ctx->bitlen);
    ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
    ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
    ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
    ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
    ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
    ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
    ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
    sha256_transform(ctx, ctx->data);

    for (i = 0; i < 4; ++i) {
        hash[i]      = (uint8_t)((ctx->state[0] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 4]  = (uint8_t)((ctx->state[1] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 8]  = (uint8_t)((ctx->state[2] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 12] = (uint8_t)((ctx->state[3] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 16] = (uint8_t)((ctx->state[4] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 20] = (uint8_t)((ctx->state[5] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 24] = (uint8_t)((ctx->state[6] >> (24 - i * 8)) & 0x000000ff);
        hash[i + 28] = (uint8_t)((ctx->state[7] >> (24 - i * 8)) & 0x000000ff);
    }
}

static void sha256_compute(const uint8_t *data, size_t len, uint8_t out[32]) {
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, out);
}

/* ========================================================================= */
/* Mode Introspection & Profiler                                             */
/* ========================================================================= */

const char* binpack_mode_name(binpack_mode_t mode) {
    switch (mode) {
        case BINPACK_MODE_GENERIC:   return "Generic Binary (Direct Passthrough)";
        case BINPACK_MODE_BCJ_X86:   return "Machine Code x86/x64 (BCJ Filter)";
        case BINPACK_MODE_BCJ_ARM:   return "Machine Code ARM32 (ARM Filter)";
        case BINPACK_MODE_BCJ_ARM64: return "Machine Code ARM64 (AArch64 Filter)";
        case BINPACK_MODE_DELTA_2:   return "Audio 16-bit / Medical Resonance (Delta-2)";
        case BINPACK_MODE_STRIDE_4:  return "Numeric / 32-bit Floats (Stride-4 Transpose)";
        case BINPACK_MODE_STRIDE_8:  return "Doubles / 64-bit Timestamps (Stride-8 Transpose)";
        case BINPACK_MODE_FWHT:      return "Boolean Spectral Matrix (FWHT)";
        case BINPACK_MODE_STRIDE_2:  return "Structured 16-bit Tuples (Stride-2 Transpose)";
        default:                     return "Unknown Mode";
    }
}

size_t binpack_padded_size(size_t size, size_t stride) {
    if (stride == 0) return size;
    size_t rem = size % stride;
    return rem == 0 ? size : (size + (stride - rem));
}

/* Branch conversion for x86/x64 (Relative CALL 0xE8 and JMP 0xE9 normalization) */
size_t binpack_filter_bcj_x86(uint8_t *data, size_t size, int encoding) {
    if (size < 5) return size;
    size_t i = 0;
    while (i <= size - 5) {
        uint8_t b = data[i];
        if (b == 0xE8 || b == 0xE9) {
            uint32_t src = read_u32_le(data + i + 1);
            uint32_t dest = encoding ? (src + (uint32_t)(i + 5)) : (src - (uint32_t)(i + 5));
            write_u32_le(data + i + 1, dest);
            i += 5;
        } else {
            i++;
        }
    }
    return size;
}

/* Branch conversion for ARM 32-bit (BL instruction normalization: opcode byte 3 == 0xEB) */
size_t binpack_filter_bcj_arm(uint8_t *data, size_t size, int encoding) {
    if (size < 4) return size;
    for (size_t i = 0; i + 4 <= size; i += 4) {
        if (data[i + 3] == 0xEB) { /* ARM32 BL opcode */
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

/* Branch conversion for ARM64 / AArch64 (BL instruction normalization: top 6 bits == 0x25) */
size_t binpack_filter_bcj_arm64(uint8_t *data, size_t size, int encoding) {
    if (size < 4) return size;
    for (size_t i = 0; i + 4 <= size; i += 4) {
        uint32_t instr = read_u32_le(data + i);
        /* AArch64 BL instruction: bits 31-26 are 100101 (0x25) */
        if ((instr >> 26) == 0x25) {
            uint32_t src = instr & 0x03FFFFFF;
            uint32_t dest = encoding ? (src + (uint32_t)(i >> 2)) : (src - (uint32_t)(i >> 2));
            instr = (instr & 0xFC000000) | (dest & 0x03FFFFFF);
            write_u32_le(data + i, instr);
        }
    }
    return size;
}

/* Finite-difference delta filters for continuous signals */
void binpack_filter_delta_encode(const uint8_t *src, uint8_t *dst, size_t size, size_t dist) {
    if (dist == 0 || size == 0) {
        if (src != dst && size > 0) memcpy(dst, src, size);
        return;
    }
    size_t i;
    for (i = 0; i < dist && i < size; i++) dst[i] = src[i];
    for (; i < size; i++) {
        dst[i] = (uint8_t)(src[i] - src[i - dist]);
    }
}

void binpack_filter_delta_decode(const uint8_t *src, uint8_t *dst, size_t size, size_t dist) {
    if (dist == 0 || size == 0) {
        if (src != dst && size > 0) memcpy(dst, src, size);
        return;
    }
    size_t i;
    for (i = 0; i < dist && i < size; i++) dst[i] = src[i];
    for (; i < size; i++) {
        dst[i] = (uint8_t)(dst[i - dist] + src[i]);
    }
}

/* Multi-channel stride transpositions */
void binpack_transpose(const uint8_t *src, uint8_t *dst, size_t size, size_t stride) {
    if (stride <= 1 || size == 0) {
        if (src != dst && size > 0) memcpy(dst, src, size);
        return;
    }
    size_t total = binpack_padded_size(size, stride);
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
    if (stride <= 1 || size == 0) {
        if (src != dst && size > 0) memcpy(dst, src, size);
        return;
    }
    size_t total = binpack_padded_size(size, stride);
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
    if (stride == 0 || size == 0) return 0.0f;
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

/* Profiler: Inspects payload in sub-microsecond time to select optimal pre-filter */
binpack_mode_t binpack_detect_mode(const uint8_t *data, size_t size) {
    if (size < 64) return BINPACK_MODE_GENERIC;

    /* 1. PE Executable */
    if (size >= 0x40 && data[0] == 'M' && data[1] == 'Z') {
        uint32_t pe_offset = read_u32_le(data + 0x3C);
        if (pe_offset <= size - 24 &&
            data[pe_offset] == 'P' && data[pe_offset+1] == 'E' &&
            data[pe_offset+2] == 0 && data[pe_offset+3] == 0) {
            uint16_t machine = read_u16_le(data + pe_offset + 4);
            if (machine == 0xaa64) {
                return BINPACK_MODE_BCJ_ARM64;
            }
            if (machine == 0x01c0 || machine == 0x01c2 || machine == 0x01c4) {
                return BINPACK_MODE_BCJ_ARM;
            }
            return BINPACK_MODE_BCJ_X86;
        }
    }

    /* 2. ELF Executable */
    if (size >= 20 && data[0] == 0x7F && data[1] == 'E' && data[2] == 'L' && data[3] == 'F') {
        uint16_t machine = read_u16_le(data + 18);
        if (machine == 0xB7) return BINPACK_MODE_BCJ_ARM64;
        if (machine == 0x28) return BINPACK_MODE_BCJ_ARM;
        return BINPACK_MODE_BCJ_X86;
    }

    /* 3. Mach-O Executable */
    if (size >= 8) {
        uint32_t magic = read_u32_le(data);
        if (magic == 0xfeedface || magic == 0xfeedfacf || magic == 0xcefaedfe || magic == 0xcffaedfe) {
            uint32_t cputype = read_u32_le(data + 4);
            if (cputype == 0x0100000c) return BINPACK_MODE_BCJ_ARM64;
            if (cputype == 12) return BINPACK_MODE_BCJ_ARM;
            return BINPACK_MODE_BCJ_X86;
        }
    }

    /* 4. Smooth 16-bit waveform (Audio PCM, MRI scan) */
    if (size >= 512) {
        size_t sample_len = (size < 2048 ? size : 2048) / 2;
        /* Distinguish from UTF-16 text by verifying high bytes aren't predominantly zero */
        size_t zero_high = 0;
        for (size_t i = 0; i < sample_len; i++) {
            if (data[i * 2 + 1] == 0x00) zero_high++;
        }
        if (zero_high < (sample_len * 7) / 10) {
            int64_t diff_sum = 0;
            for (size_t i = 0; i + 1 < sample_len; i++) {
                int16_t v1 = read_i16_le(data + i * 2);
                int16_t v2 = read_i16_le(data + (i + 1) * 2);
                diff_sum += (v2 > v1 ? v2 - v1 : v1 - v2);
            }
            if ((diff_sum / (int64_t)(sample_len - 1)) < 10000) {
                return BINPACK_MODE_DELTA_2;
            }
        }
    }

    /* 5. Stride-8 (Doubles, timestamps, 64-bit records) */
    if (size >= 512) {
        if (calc_max_channel_concentration(data, size, 8) > 0.40f) {
            return BINPACK_MODE_STRIDE_8;
        }
    }

    /* 6. Stride-4 (Floats, coordinates, 32-bit records) */
    if (size >= 256) {
        if (calc_max_channel_concentration(data, size, 4) > 0.30f) {
            return BINPACK_MODE_STRIDE_4;
        }
    }

    /* 7. Stride-2 (16-bit structured records, RGB565) */
    if (size >= 256) {
        if (calc_max_channel_concentration(data, size, 2) > 0.20f) {
            return BINPACK_MODE_STRIDE_2;
        }
    }

    return BINPACK_MODE_GENERIC;
}

/* ========================================================================= */
/* High-Level Native Compression & Decompression Pipeline                    */
/* ========================================================================= */

size_t binpack_compress_bound(size_t src_size) {
    size_t padded = binpack_padded_size(src_size, 8);
    return BINPACK_HEADER_SIZE + ZSTD_compressBound(padded);
}

int binpack_inspect_header(const uint8_t *pkg, size_t pkg_size,
                           binpack_mode_t *out_mode,
                           uint8_t *out_level,
                           uint64_t *out_orig_size) {
    if (!pkg || pkg_size < BINPACK_HEADER_SIZE) return -1;
    if (memcmp(pkg, BINPACK_MAGIC, 4) != 0) return -2;
    if (out_mode) *out_mode = (binpack_mode_t)pkg[4];
    if (out_level) *out_level = pkg[5];
    if (out_orig_size) *out_orig_size = read_u64_be(pkg + 6);
    return 0;
}

size_t binpack_compress(const uint8_t *src, size_t src_size,
                        uint8_t *dst, size_t dst_capacity,
                        int level, int force_mode,
                        binpack_mode_t *out_mode) {
    if (!src || !dst || dst_capacity < BINPACK_HEADER_SIZE) return 0;
    if (level < 1) level = 19;
    if (level > 22) level = 22;

    binpack_mode_t mode = (force_mode >= 0) ? (binpack_mode_t)force_mode : binpack_detect_mode(src, src_size);
    if (out_mode) *out_mode = mode;

    size_t padded_max = binpack_padded_size(src_size, 8);
    uint8_t *filtered = (uint8_t*)malloc(padded_max + 16);
    if (!filtered) return 0;

    size_t filtered_size = src_size;

    switch (mode) {
        case BINPACK_MODE_BCJ_X86:
            memcpy(filtered, src, src_size);
            binpack_filter_bcj_x86(filtered, src_size, 1);
            filtered_size = src_size;
            break;
        case BINPACK_MODE_BCJ_ARM:
            memcpy(filtered, src, src_size);
            binpack_filter_bcj_arm(filtered, src_size, 1);
            filtered_size = src_size;
            break;
        case BINPACK_MODE_BCJ_ARM64:
            memcpy(filtered, src, src_size);
            binpack_filter_bcj_arm64(filtered, src_size, 1);
            filtered_size = src_size;
            break;
        case BINPACK_MODE_DELTA_2:
            binpack_filter_delta_encode(src, filtered, src_size, 2);
            filtered_size = src_size;
            break;
        case BINPACK_MODE_STRIDE_2:
            binpack_transpose(src, filtered, src_size, 2);
            filtered_size = binpack_padded_size(src_size, 2);
            break;
        case BINPACK_MODE_STRIDE_4:
            binpack_transpose(src, filtered, src_size, 4);
            filtered_size = binpack_padded_size(src_size, 4);
            break;
        case BINPACK_MODE_STRIDE_8:
            binpack_transpose(src, filtered, src_size, 8);
            filtered_size = binpack_padded_size(src_size, 8);
            break;
        default:
            memcpy(filtered, src, src_size);
            filtered_size = src_size;
            break;
    }

    size_t zstd_cap = dst_capacity - BINPACK_HEADER_SIZE;
    size_t c_size = ZSTD_compress(dst + BINPACK_HEADER_SIZE, zstd_cap, filtered, filtered_size, level);
    free(filtered);

    if (ZSTD_isError(c_size)) return 0;

    /* Compute SHA-256 */
    uint8_t full_hash[32];
    sha256_compute(src, src_size, full_hash);

    /* Write 22-byte header */
    memcpy(dst, BINPACK_MAGIC, 4);
    dst[4] = (uint8_t)mode;
    dst[5] = (uint8_t)level;
    write_u64_be(dst + 6, (uint64_t)src_size);
    memcpy(dst + 14, full_hash, 8);

    return BINPACK_HEADER_SIZE + c_size;
}

size_t binpack_decompress(const uint8_t *pkg, size_t pkg_size,
                          uint8_t *dst, size_t dst_capacity) {
    if (!pkg || !dst || pkg_size < BINPACK_HEADER_SIZE) return 0;
    if (memcmp(pkg, BINPACK_MAGIC, 4) != 0) return 0;

    binpack_mode_t mode = (binpack_mode_t)pkg[4];
    uint64_t orig_size = read_u64_be(pkg + 6);
    const uint8_t *expected_hash = pkg + 14;
    const uint8_t *payload = pkg + BINPACK_HEADER_SIZE;
    size_t payload_size = pkg_size - BINPACK_HEADER_SIZE;

    if (dst_capacity < (size_t)orig_size) return 0;

    size_t padded_max = binpack_padded_size((size_t)orig_size, 8);
    uint8_t *temp = (uint8_t*)malloc(padded_max + 16);
    if (!temp) return 0;

    size_t decomp_size = ZSTD_decompress(temp, padded_max + 16, payload, payload_size);
    if (ZSTD_isError(decomp_size)) {
        free(temp);
        return 0;
    }

    switch (mode) {
        case BINPACK_MODE_BCJ_X86:
            memcpy(dst, temp, (size_t)orig_size);
            binpack_filter_bcj_x86(dst, (size_t)orig_size, 0);
            break;
        case BINPACK_MODE_BCJ_ARM:
            memcpy(dst, temp, (size_t)orig_size);
            binpack_filter_bcj_arm(dst, (size_t)orig_size, 0);
            break;
        case BINPACK_MODE_BCJ_ARM64:
            memcpy(dst, temp, (size_t)orig_size);
            binpack_filter_bcj_arm64(dst, (size_t)orig_size, 0);
            break;
        case BINPACK_MODE_DELTA_2:
            binpack_filter_delta_decode(temp, dst, (size_t)orig_size, 2);
            break;
        case BINPACK_MODE_STRIDE_2:
            binpack_itranspose(temp, dst, (size_t)orig_size, 2);
            break;
        case BINPACK_MODE_STRIDE_4:
            binpack_itranspose(temp, dst, (size_t)orig_size, 4);
            break;
        case BINPACK_MODE_STRIDE_8:
            binpack_itranspose(temp, dst, (size_t)orig_size, 8);
            break;
        default:
            memcpy(dst, temp, (size_t)orig_size);
            break;
    }
    free(temp);

    /* Verify SHA-256 */
    uint8_t full_hash[32];
    sha256_compute(dst, (size_t)orig_size, full_hash);
    if (memcmp(full_hash, expected_hash, 8) != 0) {
        return 0; /* Integrity verification failed */
    }

    return (size_t)orig_size;
}
