/**
 * BINPACK: High-Performance Polymorphic Binary Compression Library
 * Native C99 Implementation
 * 
 * Copyright (c) 2026 BINPACK Project Contributors
 * Licensed under the MIT License
 */

#ifndef BINPACK_H
#define BINPACK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BINPACK_VERSION_MAJOR 2
#define BINPACK_VERSION_MINOR 1
#define BINPACK_MAGIC "BPK2"
#define BINPACK_HEADER_SIZE 22

typedef enum {
    BINPACK_MODE_GENERIC   = 0, /* Raw passthrough */
    BINPACK_MODE_BCJ_X86   = 1, /* x86/x64 CALL/JMP offset normalization */
    BINPACK_MODE_BCJ_ARM   = 2, /* ARM 32-bit (A32/Thumb) BL branch normalization */
    BINPACK_MODE_DELTA_2   = 3, /* 16-bit smooth waveform finite difference */
    BINPACK_MODE_STRIDE_4  = 4, /* 32-bit float / coordinate transposition */
    BINPACK_MODE_STRIDE_8  = 5, /* 64-bit double / timestamp transposition */
    BINPACK_MODE_FWHT      = 6, /* Reserved / Boolean Spectral Mode */
    BINPACK_MODE_STRIDE_2  = 7, /* 16-bit tuple / struct transposition */
    BINPACK_MODE_BCJ_ARM64 = 8, /* ARM64 (AArch64) BL branch normalization */
    BINPACK_MODE_BCJ_RISCV = 9  /* RISC-V (RV32/RV64 JAL) branch normalization */
} binpack_mode_t;

/* Mode introspection */
const char* binpack_mode_name(binpack_mode_t mode);

/* Safe buffer size calculation for transposed buffers: returns size rounded up to multiple of stride */
size_t binpack_padded_size(size_t size, size_t stride);

/* Profiler: Inspects buffer structure in sub-microsecond time */
binpack_mode_t binpack_detect_mode(const uint8_t *data, size_t size);

/* Pre-filters (SIMD / Cache-optimized, 100% lossless & bit-exact reversible) */
size_t binpack_filter_bcj_x86(uint8_t *data, size_t size, int encoding);
size_t binpack_filter_bcj_arm(uint8_t *data, size_t size, int encoding);
size_t binpack_filter_bcj_arm64(uint8_t *data, size_t size, int encoding);
size_t binpack_filter_bcj_riscv(uint8_t *data, size_t size, int encoding);

/* Zero-Regression Evaluator: verifies filter yields compression gain vs raw passthrough */
int    binpack_evaluate_filter_gain(const uint8_t *sample, size_t sample_sz, binpack_mode_t mode);

void   binpack_filter_delta_encode(const uint8_t *src, uint8_t *dst, size_t size, size_t dist);
void   binpack_filter_delta_decode(const uint8_t *src, uint8_t *dst, size_t size, size_t dist);

/* Note: 'dst' for binpack_transpose must have at least binpack_padded_size(size, stride) bytes allocated */
void   binpack_transpose(const uint8_t *src, uint8_t *dst, size_t size, size_t stride);
void   binpack_itranspose(const uint8_t *src, uint8_t *dst, size_t size, size_t stride);

/* High-Level Compression & Decompression Pipeline (Native C, zero Python runtime required) */

/* Returns conservative upper bound of compressed output buffer size */
size_t binpack_compress_bound(size_t src_size);

/* Compresses src into .bpk format written into dst.
 * Returns bytes written, or 0 on error.
 * force_mode: pass (binpack_mode_t)-1 or BINPACK_MODE_GENERIC.. to force, or -1 for autodetection. */
size_t binpack_compress(const uint8_t *src, size_t src_size,
                        uint8_t *dst, size_t dst_capacity,
                        int level, int force_mode,
                        binpack_mode_t *out_mode);

/* Inspects .bpk package header without decompressing. Returns 0 on success, non-zero on error. */
int    binpack_inspect_header(const uint8_t *pkg, size_t pkg_size,
                              binpack_mode_t *out_mode,
                              uint8_t *out_level,
                              uint64_t *out_orig_size);

/* Decompresses .bpk package pkg into dst.
 * Returns uncompressed bytes written into dst, or 0 on error / checksum failure. */
size_t binpack_decompress(const uint8_t *pkg, size_t pkg_size,
                          uint8_t *dst, size_t dst_capacity);

#ifdef __cplusplus
}
#endif

#endif /* BINPACK_H */
