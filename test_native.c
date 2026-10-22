#include "binpack.h"
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

static uint8_t* read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t*)malloc((size_t)sz);
    if (buf) {
        if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
            free(buf);
            fclose(f);
            return NULL;
        }
        *out_size = (size_t)sz;
    }
    fclose(f);
    return buf;
}

void bench_file(const char *filename) {
    size_t size = 0;
    uint8_t *data = read_file(filename, &size);
    if (!data) {
        fprintf(stderr, "[-] Skipping '%s': file not found or inaccessible.\n", filename);
        return;
    }

    printf("======================================================================\n");
    printf("[*] BENCHMARKING NATIVE C ENGINE: %s (%zu bytes / ~%.1f KB)\n",
           filename, size, (double)size / 1024.0);
    printf("======================================================================\n");

    /* 1. Profiler Latency (10,000 iterations for microsecond precision) */
    int iterations = 10000;
    double t0 = get_time_sec();
    binpack_mode_t detected = BINPACK_MODE_GENERIC;
    for (int i = 0; i < iterations; i++) {
        detected = binpack_detect_mode(data, size);
    }
    double t_detect = (get_time_sec() - t0) / (double)iterations;

    printf("[+] Detected Mode     : %s\n", binpack_mode_name(detected));
    printf("[+] Profiler Latency  : %.3f microseconds (%.0f detections/second)\n", 
           t_detect * 1e6, 1.0 / (t_detect > 1e-9 ? t_detect : 1e-9));

    /* 2. Filter Throughput & Roundtrip Integrity */
    size_t alloc_size = binpack_padded_size(size, 8) + 64;
    uint8_t *filtered = (uint8_t*)malloc(alloc_size);
    uint8_t *restored = (uint8_t*)malloc(alloc_size);
    if (!filtered || !restored) {
        fprintf(stderr, "Error: out of memory.\n");
        free(data);
        if (filtered) free(filtered);
        if (restored) free(restored);
        return;
    }

    if (detected == BINPACK_MODE_BCJ_X86) {
        memcpy(filtered, data, size);
        double tb0 = get_time_sec();
        binpack_filter_bcj_x86(filtered, size, 1);
        double tb_enc = get_time_sec() - tb0;

        memcpy(restored, filtered, size);
        double td0 = get_time_sec();
        binpack_filter_bcj_x86(restored, size, 0);
        double tb_dec = get_time_sec() - td0;

        int match = (memcmp(data, restored, size) == 0);
        double mb = (double)size / (1024.0 * 1024.0);
        printf("[+] Filter Speed (BCJ x86): Encode: %.1f MB/s | Decode: %.1f MB/s\n",
               mb / (tb_enc > 1e-7 ? tb_enc : 1e-7),
               mb / (tb_dec > 1e-7 ? tb_dec : 1e-7));
        printf("[+] Lossless Integrity    : %s\n", match ? "PASS (100% Bit-Exact)" : "FAIL");
    } else if (detected == BINPACK_MODE_BCJ_ARM) {
        memcpy(filtered, data, size);
        double tb0 = get_time_sec();
        binpack_filter_bcj_arm(filtered, size, 1);
        double tb_enc = get_time_sec() - tb0;

        memcpy(restored, filtered, size);
        double td0 = get_time_sec();
        binpack_filter_bcj_arm(restored, size, 0);
        double tb_dec = get_time_sec() - td0;

        int match = (memcmp(data, restored, size) == 0);
        double mb = (double)size / (1024.0 * 1024.0);
        printf("[+] Filter Speed (BCJ ARM32): Encode: %.1f MB/s | Decode: %.1f MB/s\n",
               mb / (tb_enc > 1e-7 ? tb_enc : 1e-7),
               mb / (tb_dec > 1e-7 ? tb_dec : 1e-7));
        printf("[+] Lossless Integrity      : %s\n", match ? "PASS (100% Bit-Exact)" : "FAIL");
    } else if (detected == BINPACK_MODE_BCJ_ARM64) {
        memcpy(filtered, data, size);
        double tb0 = get_time_sec();
        binpack_filter_bcj_arm64(filtered, size, 1);
        double tb_enc = get_time_sec() - tb0;

        memcpy(restored, filtered, size);
        double td0 = get_time_sec();
        binpack_filter_bcj_arm64(restored, size, 0);
        double tb_dec = get_time_sec() - td0;

        int match = (memcmp(data, restored, size) == 0);
        double mb = (double)size / (1024.0 * 1024.0);
        printf("[+] Filter Speed (BCJ ARM64): Encode: %.1f MB/s | Decode: %.1f MB/s\n",
               mb / (tb_enc > 1e-7 ? tb_enc : 1e-7),
               mb / (tb_dec > 1e-7 ? tb_dec : 1e-7));
        printf("[+] Lossless Integrity      : %s\n", match ? "PASS (100% Bit-Exact)" : "FAIL");
    } else if (detected == BINPACK_MODE_STRIDE_4 || detected == BINPACK_MODE_STRIDE_8 || detected == BINPACK_MODE_STRIDE_2) {
        size_t stride = (detected == BINPACK_MODE_STRIDE_8) ? 8 : ((detected == BINPACK_MODE_STRIDE_4) ? 4 : 2);
        double tb0 = get_time_sec();
        binpack_transpose(data, filtered, size, stride);
        double tb_enc = get_time_sec() - tb0;

        double td0 = get_time_sec();
        binpack_itranspose(filtered, restored, size, stride);
        double tb_dec = get_time_sec() - td0;

        int match = (memcmp(data, restored, size) == 0);
        double mb = (double)size / (1024.0 * 1024.0);
        printf("[+] Filter Speed (Stride-%zu): Transpose: %.1f MB/s | Inverse: %.1f MB/s\n",
               stride,
               mb / (tb_enc > 1e-7 ? tb_enc : 1e-7),
               mb / (tb_dec > 1e-7 ? tb_dec : 1e-7));
        printf("[+] Lossless Integrity      : %s\n", match ? "PASS (100% Bit-Exact)" : "FAIL");
    } else if (detected == BINPACK_MODE_DELTA_2) {
        double tb0 = get_time_sec();
        binpack_filter_delta_encode(data, filtered, size, 2);
        double tb_enc = get_time_sec() - tb0;

        double td0 = get_time_sec();
        binpack_filter_delta_decode(filtered, restored, size, 2);
        double tb_dec = get_time_sec() - td0;

        int match = (memcmp(data, restored, size) == 0);
        double mb = (double)size / (1024.0 * 1024.0);
        printf("[+] Filter Speed (Delta-2)  : Encode: %.1f MB/s | Decode: %.1f MB/s\n",
               mb / (tb_enc > 1e-7 ? tb_enc : 1e-7),
               mb / (tb_dec > 1e-7 ? tb_dec : 1e-7));
        printf("[+] Lossless Integrity      : %s\n", match ? "PASS (100% Bit-Exact)" : "FAIL");
    } else {
        printf("[+] Filter Action           : Passthrough (Raw Stream)\n");
        printf("[+] Lossless Integrity      : PASS (Direct)\n");
    }
    printf("\n");

    free(data);
    free(filtered);
    free(restored);
}

int main(int argc, char **argv) {
    printf("======================================================================\n");
    printf("BINPACK v2: NATIVE C HIGH-PERFORMANCE BENCHMARK SUITE\n");
    printf("Sub-microsecond Profiler & Multi-Gigabyte/s Memory Pre-filters\n");
    printf("======================================================================\n\n");

    if (argc > 1) {
        for (int i = 1; i < argc; i++) bench_file(argv[i]);
    } else {
        bench_file("ooffice");
        bench_file("x-ray");
        bench_file("mr");
        bench_file("vcruntime140.dll");
        bench_file("sensor_data.raw");
        bench_file("telemetry_16bit.raw");
        bench_file("arm_binary.elf");
    }
    return 0;
}
