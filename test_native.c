#include "binpack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double get_time_sec(void) {
    return (double)clock() / (double)CLOCKS_PER_SEC;
}

static uint8_t* read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t*)malloc(sz);
    if (buf) {
        fread(buf, 1, sz, f);
        *out_size = (size_t)sz;
    }
    fclose(f);
    return buf;
}

void bench_file(const char *filename) {
    size_t size = 0;
    uint8_t *data = read_file(filename, &size);
    if (!data) {
        fprintf(stderr, "Error: cannot open file '%s'\n", filename);
        return;
    }

    printf("----------------------------------------------------------------------\n");
    printf("File    : %s (%zu bytes / %.2f MB)\n", filename, size, (double)size / (1024.0 * 1024.0));

    /* 1. Profiler Latency (10,000 iterations for microsecond measurement) */
    int iterations = 10000;
    double t0 = get_time_sec();
    binpack_mode_t detected = BINPACK_MODE_GENERIC;
    for (int i = 0; i < iterations; i++) {
        detected = binpack_detect_mode(data, size);
    }
    double t_detect = (get_time_sec() - t0) / iterations;

    printf("Mode    : %s\n", binpack_mode_name(detected));
    printf("Profiler: %.3f us (%.0f ops/sec)\n", 
           t_detect * 1e6, 1.0 / (t_detect > 1e-9 ? t_detect : 1e-9));

    /* 2. Filter Throughput & Roundtrip */
    uint8_t *filtered = (uint8_t*)malloc(size + 16);
    uint8_t *restored = (uint8_t*)malloc(size + 16);

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
        printf("Filter  : BCJ x86 [Encode: %.1f MB/s | Decode: %.1f MB/s]\n",
               (size / (1024.0 * 1024.0)) / (tb_enc > 1e-6 ? tb_enc : 1e-6),
               (size / (1024.0 * 1024.0)) / (tb_dec > 1e-6 ? tb_dec : 1e-6));
        printf("Verify  : %s\n", match ? "PASS (100% bit-exact)" : "FAIL");
    } else if (detected == BINPACK_MODE_STRIDE_4 || detected == BINPACK_MODE_STRIDE_8 || detected == BINPACK_MODE_STRIDE_2) {
        size_t stride = (detected == BINPACK_MODE_STRIDE_8) ? 8 : ((detected == BINPACK_MODE_STRIDE_4) ? 4 : 2);
        double tb0 = get_time_sec();
        binpack_transpose(data, filtered, size, stride);
        double tb_enc = get_time_sec() - tb0;

        double td0 = get_time_sec();
        binpack_itranspose(filtered, restored, size, stride);
        double tb_dec = get_time_sec() - td0;

        int match = (memcmp(data, restored, size) == 0);
        printf("Filter  : Stride-%zu [Transpose: %.1f MB/s | Inverse: %.1f MB/s]\n",
               stride,
               (size / (1024.0 * 1024.0)) / (tb_enc > 1e-6 ? tb_enc : 1e-6),
               (size / (1024.0 * 1024.0)) / (tb_dec > 1e-6 ? tb_dec : 1e-6));
        printf("Verify  : %s\n", match ? "PASS (100% bit-exact)" : "FAIL");
    } else if (detected == BINPACK_MODE_DELTA_2) {
        double tb0 = get_time_sec();
        binpack_filter_delta_encode(data, filtered, size, 2);
        double tb_enc = get_time_sec() - tb0;

        double td0 = get_time_sec();
        binpack_filter_delta_decode(filtered, restored, size, 2);
        double tb_dec = get_time_sec() - td0;

        int match = (memcmp(data, restored, size) == 0);
        printf("Filter  : Delta-2 [Encode: %.1f MB/s | Decode: %.1f MB/s]\n",
               (size / (1024.0 * 1024.0)) / (tb_enc > 1e-6 ? tb_enc : 1e-6),
               (size / (1024.0 * 1024.0)) / (tb_dec > 1e-6 ? tb_dec : 1e-6));
        printf("Verify  : %s\n", match ? "PASS (100% bit-exact)" : "FAIL");
    } else {
        printf("Filter  : Passthrough (Raw)\n");
        printf("Verify  : PASS\n");
    }

    free(data);
    free(filtered);
    free(restored);
}

int main(int argc, char **argv) {
    printf("BINPACK v2 - Native C Performance Benchmark\n");
    if (argc > 1) {
        for (int i = 1; i < argc; i++) bench_file(argv[i]);
    } else {
        bench_file("ooffice");
        bench_file("x-ray");
        bench_file("mr");
        bench_file("sensor_data.raw");
        bench_file("telemetry_16bit.raw");
    }
    printf("----------------------------------------------------------------------\n");
    return 0;
}
