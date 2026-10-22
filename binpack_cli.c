/**
 * BINPACK: High-Performance Polymorphic Binary Compression CLI
 * 100% Native C99 Standalone Tool (Zero Python Runtime Required)
 * 
 * Copyright (c) 2026 BINPACK Project Contributors
 * Licensed under the MIT License
 */

#include "binpack.h"
#include <zstd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static double get_time_sec(void) {
    static LARGE_INTEGER freq;
    static int init = 0;
    if (!init) {
        QueryPerformanceFrequency(&freq);
        init = 1;
    }
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart;
}
#else
#include <time.h>
static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
#endif

static uint8_t* read_entire_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    uint8_t *buf = (uint8_t*)malloc((size_t)sz);
    if (buf) {
        if (sz > 0 && fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
            free(buf);
            fclose(f);
            return NULL;
        }
        *out_size = (size_t)sz;
    }
    fclose(f);
    return buf;
}

static int write_entire_file(const char *path, const uint8_t *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    if (size > 0 && fwrite(data, 1, size, f) != size) {
        fclose(f);
        return -2;
    }
    fclose(f);
    return 0;
}

static void print_usage(const char *prog) {
    printf("BINPACK v%d.%d - High-Performance Polymorphic Binary Compressor (Native C)\n",
           BINPACK_VERSION_MAJOR, BINPACK_VERSION_MINOR);
    printf("Usage: %s <command> [options]\n\n", prog);
    printf("Commands:\n");
    printf("  compress   <input> [-o output.bpk] [-l level]   Compress file into .bpk format\n");
    printf("  decompress <input.bpk> [-o restored_file]       Decompress .bpk container (SHA-256 verified)\n");
    printf("  bench      <input>                              Benchmark against raw Meta Zstandard Ultra\n");
    printf("  info       <input.bpk>                          Inspect .bpk container metadata\n");
    printf("  version                                         Display version information\n\n");
    printf("Options:\n");
    printf("  -o <path>    Specify output file path\n");
    printf("  -l <1-22>    Compression level (default: 19)\n");
}

static int cmd_compress(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Error: missing input file for compression.\n");
        return 1;
    }
    const char *input_path = argv[2];
    char default_out[1024];
    snprintf(default_out, sizeof(default_out), "%s.bpk", input_path);
    const char *output_path = default_out;
    int level = 19;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        } else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
            level = atoi(argv[++i]);
        }
    }

    size_t in_size = 0;
    uint8_t *in_data = read_entire_file(input_path, &in_size);
    if (!in_data) {
        fprintf(stderr, "Error: cannot open or read file '%s'.\n", input_path);
        return 1;
    }

    size_t out_cap = binpack_compress_bound(in_size);
    uint8_t *out_data = (uint8_t*)malloc(out_cap);
    if (!out_data) {
        fprintf(stderr, "Error: out of memory.\n");
        free(in_data);
        return 1;
    }

    binpack_mode_t detected_mode = BINPACK_MODE_GENERIC;
    double t0 = get_time_sec();
    size_t comp_size = binpack_compress(in_data, in_size, out_data, out_cap, level, -1, &detected_mode);
    double elapsed = get_time_sec() - t0;

    if (comp_size == 0) {
        fprintf(stderr, "Error: compression failed.\n");
        free(in_data);
        free(out_data);
        return 1;
    }

    if (write_entire_file(output_path, out_data, comp_size) != 0) {
        fprintf(stderr, "Error: cannot write output file '%s'.\n", output_path);
        free(in_data);
        free(out_data);
        return 1;
    }

    double ratio = in_size > 0 ? (1.0 - (double)comp_size / (double)in_size) * 100.0 : 0.0;
    double speed = ((double)in_size / (1024.0 * 1024.0)) / (elapsed > 1e-7 ? elapsed : 1e-7);

    printf("Compressed: %s -> %s\n", input_path, output_path);
    printf("Original  : %zu bytes\n", in_size);
    printf("Final     : %zu bytes (%.2f%% reduction)\n", comp_size, ratio);
    printf("Filter    : %s\n", binpack_mode_name(detected_mode));
    printf("Throughput: %.1f MB/s in %.1f ms\n", speed, elapsed * 1000.0);

    free(in_data);
    free(out_data);
    return 0;
}

static int cmd_decompress(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Error: missing input file for decompression.\n");
        return 1;
    }
    const char *input_path = argv[2];
    char default_out[1024];
    size_t in_len = strlen(input_path);
    if (in_len > 4 && strcmp(input_path + in_len - 4, ".bpk") == 0) {
        snprintf(default_out, sizeof(default_out), "%.*s", (int)(in_len - 4), input_path);
    } else {
        snprintf(default_out, sizeof(default_out), "%s.out", input_path);
    }
    const char *output_path = default_out;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        }
    }

    size_t pkg_size = 0;
    uint8_t *pkg_data = read_entire_file(input_path, &pkg_size);
    if (!pkg_data) {
        fprintf(stderr, "Error: cannot open or read package file '%s'.\n", input_path);
        return 1;
    }

    binpack_mode_t mode = BINPACK_MODE_GENERIC;
    uint8_t level = 0;
    uint64_t orig_size = 0;
    if (binpack_inspect_header(pkg_data, pkg_size, &mode, &level, &orig_size) != 0) {
        fprintf(stderr, "Error: '%s' is not a valid BPK2 container.\n", input_path);
        free(pkg_data);
        return 1;
    }

    uint8_t *restored_data = (uint8_t*)malloc((size_t)orig_size + 16);
    if (!restored_data && orig_size > 0) {
        fprintf(stderr, "Error: out of memory for restored buffer.\n");
        free(pkg_data);
        return 1;
    }

    double t0 = get_time_sec();
    size_t decomp_size = binpack_decompress(pkg_data, pkg_size, restored_data, (size_t)orig_size + 16);
    double elapsed = get_time_sec() - t0;

    if (decomp_size == 0 && orig_size > 0) {
        fprintf(stderr, "Error: decompression or SHA-256 checksum verification failed.\n");
        free(pkg_data);
        free(restored_data);
        return 1;
    }

    if (write_entire_file(output_path, restored_data, decomp_size) != 0) {
        fprintf(stderr, "Error: cannot write restored file '%s'.\n", output_path);
        free(pkg_data);
        free(restored_data);
        return 1;
    }

    double speed = ((double)decomp_size / (1024.0 * 1024.0)) / (elapsed > 1e-7 ? elapsed : 1e-7);
    printf("Decompressed: %s -> %s (%zu bytes)\n", input_path, output_path, decomp_size);
    printf("Integrity   : SHA-256 OK (Lossless bit-exact verified)\n");
    printf("Throughput  : %.1f MB/s in %.1f ms\n", speed, elapsed * 1000.0);

    free(pkg_data);
    free(restored_data);
    return 0;
}

static int cmd_info(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Error: missing input file for inspection.\n");
        return 1;
    }
    const char *input_path = argv[2];
    size_t pkg_size = 0;
    uint8_t *pkg_data = read_entire_file(input_path, &pkg_size);
    if (!pkg_data) {
        fprintf(stderr, "Error: cannot open file '%s'.\n", input_path);
        return 1;
    }

    binpack_mode_t mode = BINPACK_MODE_GENERIC;
    uint8_t level = 0;
    uint64_t orig_size = 0;
    if (binpack_inspect_header(pkg_data, pkg_size, &mode, &level, &orig_size) != 0) {
        fprintf(stderr, "Error: '%s' is not a valid BPK2 container.\n", input_path);
        free(pkg_data);
        return 1;
    }

    printf("======================================================================\n");
    printf("CONTAINER: %s\n", input_path);
    printf("======================================================================\n");
    printf("Magic Header    : BPK2\n");
    printf("Active Filter   : %s (Mode %d)\n", binpack_mode_name(mode), (int)mode);
    printf("Zstd Level      : %d\n", (int)level);
    printf("Compressed Size : %zu bytes\n", pkg_size);
    printf("Original Size   : %llu bytes\n", (unsigned long long)orig_size);
    if (orig_size > 0) {
        double ratio = (1.0 - (double)pkg_size / (double)orig_size) * 100.0;
        printf("Net Reduction   : %.2f%%\n", ratio);
    }
    printf("SHA-256 (64-bit): ");
    for (int i = 14; i < 22; i++) printf("%02x", pkg_data[i]);
    printf("\n");

    free(pkg_data);
    return 0;
}

static int cmd_bench(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Error: missing test file for benchmark.\n");
        return 1;
    }
    const char *input_path = argv[2];
    size_t in_size = 0;
    uint8_t *in_data = read_entire_file(input_path, &in_size);
    if (!in_data) {
        fprintf(stderr, "Error: cannot open file '%s'.\n", input_path);
        return 1;
    }

    printf("================================================================================\n");
    printf("NATIVE C BENCHMARK: %s (%zu bytes / %.2f MB)\n",
           input_path, in_size, (double)in_size / (1024.0 * 1024.0));
    printf("================================================================================\n");

    /* 1. Raw Meta Zstandard Ultra Level 22 */
    size_t zstd_bound = ZSTD_compressBound(in_size);
    uint8_t *zstd_out = (uint8_t*)malloc(zstd_bound);
    double tz0 = get_time_sec();
    size_t zstd_size = ZSTD_compress(zstd_out, zstd_bound, in_data, in_size, 22);
    double tz_elapsed = get_time_sec() - tz0;

    /* 2. BINPACK v2 (Prefilter + Zstandard Level 22) */
    size_t bpk_bound = binpack_compress_bound(in_size);
    uint8_t *bpk_out = (uint8_t*)malloc(bpk_bound);
    binpack_mode_t mode = BINPACK_MODE_GENERIC;
    double tb0 = get_time_sec();
    size_t bpk_size = binpack_compress(in_data, in_size, bpk_out, bpk_bound, 22, -1, &mode);
    double tb_elapsed = get_time_sec() - tb0;

    /* 3. Verification */
    uint8_t *rec_data = (uint8_t*)malloc(in_size + 16);
    double td0 = get_time_sec();
    size_t rec_size = binpack_decompress(bpk_out, bpk_size, rec_data, in_size + 16);
    double td_elapsed = get_time_sec() - td0;

    int match = (rec_size == in_size && memcmp(in_data, rec_data, in_size) == 0);

    printf("%-5s | %-28s | %-14s | %-9s | %s\n", "Rank", "Compressor", "Final Size", "Ratio", "Comp. Time");
    printf("--------------------------------------------------------------------------------\n");
    printf("%-5s | %-28s | %6zu bytes   | %-9s | %s\n", "-", "Uncompressed (Original)", in_size, "0.0%", "-");

    double r_zstd = (1.0 - (double)zstd_size / (double)in_size) * 100.0;
    double r_bpk  = (1.0 - (double)bpk_size  / (double)in_size) * 100.0;

    if (bpk_size <= zstd_size) {
        printf("#%-4d | %-28s | %6zu bytes   | %5.2f%%   | %7.1f ms  [#1 Lowest Size]\n",
               1, "BINPACK v2 (Filter + Zstd)", bpk_size, r_bpk, tb_elapsed * 1000.0);
        printf("#%-4d | %-28s | %6zu bytes   | %5.2f%%   | %7.1f ms\n",
               2, "Meta Zstandard (Level 22)", zstd_size, r_zstd, tz_elapsed * 1000.0);
    } else {
        printf("#%-4d | %-28s | %6zu bytes   | %5.2f%%   | %7.1f ms  [#1 Lowest Size]\n",
               1, "Meta Zstandard (Level 22)", zstd_size, r_zstd, tz_elapsed * 1000.0);
        printf("#%-4d | %-28s | %6zu bytes   | %5.2f%%   | %7.1f ms\n",
               2, "BINPACK v2 (Filter + Zstd)", bpk_size, r_bpk, tb_elapsed * 1000.0);
    }

    printf("================================================================================\n");
    printf("Active Filter : %s\n", binpack_mode_name(mode));
    printf("Decomp. Time  : %.2f ms (%s)\n",
           td_elapsed * 1000.0, match ? "Lossless SHA-256 Bit-Exact OK" : "FAILED INTEGRITY");

    if (bpk_size < zstd_size) {
        size_t diff = zstd_size - bpk_size;
        printf("Result        : BINPACK outperforms Meta Zstandard Ultra by %zu bytes (%.1f%% smaller)\n",
               diff, ((double)diff / (double)zstd_size) * 100.0);
    } else if (bpk_size == zstd_size) {
        printf("Result        : Parity with Meta Zstandard Ultra\n");
    } else {
        size_t diff = bpk_size - zstd_size;
        printf("Result        : Meta Zstandard Ultra is %zu bytes smaller\n", diff);
    }

    free(in_data);
    free(zstd_out);
    free(bpk_out);
    free(rec_data);
    return match ? 0 : 2;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];
    if (strcmp(cmd, "compress") == 0) {
        return cmd_compress(argc, argv);
    } else if (strcmp(cmd, "decompress") == 0) {
        return cmd_decompress(argc, argv);
    } else if (strcmp(cmd, "bench") == 0) {
        return cmd_bench(argc, argv);
    } else if (strcmp(cmd, "info") == 0) {
        return cmd_info(argc, argv);
    } else if (strcmp(cmd, "version") == 0 || strcmp(cmd, "-v") == 0 || strcmp(cmd, "--version") == 0) {
        printf("BINPACK v%d.%d (Native C Engine with Zstandard v%s)\n",
               BINPACK_VERSION_MAJOR, BINPACK_VERSION_MINOR, ZSTD_versionString());
        return 0;
    } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    } else {
        fprintf(stderr, "Unknown command: '%s'\n", cmd);
        print_usage(argv[0]);
        return 1;
    }
}
