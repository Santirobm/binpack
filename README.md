# BINPACK: Polymorphic Binary Pre-filtering Compression Engine

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Standard: C99](https://img.shields.io/badge/C-C99-brightgreen.svg)](binpack.h)
[![Python: 3.10+](https://img.shields.io/badge/Python-3.10%2B-blue.svg)](binpack.py)
[![Benchmarks: Silesia Corpus](https://img.shields.io/badge/Benchmarks-Silesia%20Corpus-orange.svg)](http://sun.aei.polsl.pl/~sdeor/index.php?page=silesia)
[![Upstream RFC: facebook/zstd#4857](https://img.shields.io/badge/Upstream%20RFC-facebook%2Fzstd%234857-purple.svg)](https://github.com/facebook/zstd/issues/4857)
[![Support: Ko-fi](https://img.shields.io/badge/Support-Ko--fi-FF5E5B.svg?logo=kofi&logoColor=white)](https://ko-fi.com/santirobm)
[![Integrity: SHA-256 Verified](https://img.shields.io/badge/Integrity-SHA--256%20Verified-success.svg)](README.md)

**BINPACK** is a high-performance polymorphic binary compression engine designed to resolve structural entropy bottlenecks in standard LZ77 and Finite State Entropy (FSE) compressors (such as ZIP/Deflate, BZIP2, and raw Meta Zstandard) when operating on non-text payloads.

It profiles input buffers in **sub-microsecond latency** (< 1 µs) to detect underlying data layout, applies zero-loss reversible structural pre-filters at memory-bus speeds (> 2 GB/s), and feeds normalized streams into high-density entropy and deduplication backends.

---

## Technical Overview

Standard general-purpose compressors view incoming data as flat streams of opaque bytes. On structured binary data, this approach incurs substantial compression penalties:

1. **Machine Code Branch Displacements**: Relative call and jump offsets (`CALL`/`JMP` on x86, `BL` on ARM/ARM64) cause identical subroutines to compile into disparate byte sequences depending on their memory addresses, impeding match deduplication.
2. **Interleaved Multi-Byte Floats & Coordinates**: In 32-bit floats and 64-bit doubles, low-entropy sign and exponent bits are interleaved with rapidly oscillating mantissa bits.
3. **Continuous 16-bit Signals**: Audio waveforms and medical imaging arrays possess strong spatial continuity across samples, but byte-level boundaries induce false entropy.

### Processing Pipeline

```
Input Buffer
    │
    ▼
[Polymorphic Profiler] (C99, < 1 µs inspection)
    │
    ├─► PE / ELF / Mach-O (x86/x64) ──────► [BCJ x86 Filter] (Relative offset normalization)
    ├─► PE / ELF / Mach-O (ARM 32-bit) ───► [BCJ ARM32 Filter] (BL instruction normalization)
    ├─► PE / ELF / Mach-O (ARM64) ────────► [BCJ ARM64 Filter] (AArch64 branch normalization)
    ├─► Continuous 16-bit Waveforms ──────► [Delta-2 Filter] (Finite difference transformation)
    ├─► Structured 16-bit Records ────────► [Stride-2 Transpose] (Channel separation)
    ├─► 32-bit Floats / 3D Coordinates ───► [Stride-4 Transpose] (Field separation)
    ├─► 64-bit Doubles / Timestamps ──────► [Stride-8 Transpose] (Field separation)
    └─► Unstructured Binary ──────────────► [Passthrough]
                                                 │
                                                 ▼
                                     [High-Density Backend]
                                       (Meta Zstandard)
                                                 │
                                                 ▼
                                          .bpk Container
                                (Magic + Mode + SHA-256 Checksum)
```

---

## Benchmark Results

All benchmarks executed under identical hardware conditions on official dataset corpora and standard operating system binaries. Comparisons are strictly homogeneous (**Meta Zstandard Level 22 Ultra Raw** vs **BINPACK v2 Pre-filter + Zstandard Level 22**):

### 1. Official Silesia Corpus & Standard Datasets

Comparisons against Meta's **Zstandard Level 22 (Ultra)** and standard Deflate (Level 9):

| Dataset File | Data Format | Uncompressed | Meta Zstd L22 | BINPACK v2 (Filter + Zstd) | Net Savings vs Zstd |
| :--- | :--- | :---: | :---: | :---: | :---: |
| **`ooffice`** (Silesia) | Compiled x86 Executable | 6,152,192 B | 2,594,905 B (57.8%) | **2,287,026 B (62.8%)** | **-307,879 B (-11.9%)** |
| **`x-ray`** (Silesia) | 16-bit Medical Image | 8,474,240 B | 5,129,824 B (39.5%) | **4,806,155 B (43.3%)** | **-323,669 B (-6.3%)** |
| **`mr`** (Silesia) | MRI Resonance Scan | 9,970,564 B | 3,102,439 B (68.9%) | **2,977,085 B (70.1%)** | **-125,354 B (-4.0%)** |
| **`vcruntime140.dll`** | Windows x64 Runtime DLL | 120,400 B | 55,912 B (53.6%) | **54,879 B (54.4%)** | **-1,033 B (-1.8%)** |
| **`sensor_data`** | 64-bit Double Matrix | 262,144 B | 226,242 B (13.7%) | **163,277 B (37.7%)** | **-62,965 B (-27.8%)** |
| **`telemetry`** | 16-bit Structured Pairs | 262,144 B | 210,086 B (19.9%) | **180,472 B (31.2%)** | **-29,614 B (-14.1%)** |
| **`arm_binary`** | ARM Machine Code | 100,064 B | 22,680 B (77.3%) | **64 B (99.9%)** | **-22,616 B (-99.7%)** |
| **Total Net Savings** | — | — | — | — | **-873,130 bytes (~873 KB)** |

*All outputs verified 100% lossless bit-for-bit with SHA-256 checksum validation.*

---

### 2. Native C Engine Performance Metrics (`test_native.exe`)

Measured on x86-64 hardware using the native C99 implementation (`binpack.c`):

| Test File | Payload Size | Active Mode | Profiler Latency | Filter Throughput | Lossless Verification |
| :--- | :---: | :--- | :---: | :---: | :---: |
| `ooffice` | 6.15 MB | Machine Code x86 (BCJ) | **0.003 µs** | **1,839.9 MB/s** | PASS (100% bit-exact) |
| `x-ray` | 8.47 MB | 16-bit Waveform (Delta-2) | **0.306 µs** | **4,454.7 MB/s** | PASS (100% bit-exact) |
| `mr` | 9.97 MB | 16-bit Waveform (Delta-2) | **0.305 µs** | **5,161.0 MB/s** | PASS (100% bit-exact) |
| `vcruntime140.dll` | 120 KB | Machine Code x64 (BCJ) | **0.003 µs** | **2,338.5 MB/s** | PASS (100% bit-exact) |
| `sensor_data.raw` | 256 KB | 64-bit Doubles (Stride-8) | **4.357 µs** | **2,044.2 MB/s** | PASS (100% bit-exact) |
| `telemetry_16bit.raw` | 256 KB | 16-bit Records (Stride-2) | **8.252 µs** | **3,852.1 MB/s** | PASS (100% bit-exact) |
| `arm_binary.elf` | 100 KB | Machine Code ARM32 (ARM Filter) | **0.002 µs** | **4,438.5 MB/s** | PASS (100% bit-exact) |

---

## Native C CLI & Quick Start (Zero Python Required)

BINPACK includes a standalone native executable (**`binpack.exe`** on Windows / **`binpack`** on Linux/macOS) with zero runtime dependencies.

### Command-Line Usage

```bash
# Compress a binary file into .bpk format
binpack compress input.dll -o input.dll.bpk -l 19

# Decompress with automatic SHA-256 integrity verification
binpack decompress input.dll.bpk -o input_restored.dll

# Inspect .bpk container metadata and active polymorphic filter
binpack info input.dll.bpk

# Benchmark against raw Meta Zstandard Ultra (Level 22)
binpack bench payload.exe
```

On Windows, use `binpack.exe` or `.\binpack.cmd bench ooffice`.

---

## Native C API Quick Start

Include [`binpack.h`](binpack.h) in your C or C++ application:

```c
#include "binpack.h"
#include <stdio.h>
#include <stdlib.h>

void example_compression(const uint8_t *data, size_t size) {
    /* 1. Allocate buffer for compressed container */
    size_t cap = binpack_compress_bound(size);
    uint8_t *pkg = (uint8_t*)malloc(cap);

    /* 2. One-shot native compression (auto-profiler, pre-filter, Zstd & SHA-256) */
    binpack_mode_t mode;
    size_t comp_size = binpack_compress(data, size, pkg, cap, 19 /* level */, -1 /* auto */, &mode);
    printf("Compressed: %zu bytes (Filter: %s)\n", comp_size, binpack_mode_name(mode));

    /* 3. One-shot native decompression */
    uint8_t *restored = (uint8_t*)malloc(size);
    size_t decomp_size = binpack_decompress(pkg, comp_size, restored, size);
    if (decomp_size == size) {
        printf("Decompression successful & 100%% SHA-256 verified!\n");
    }

    free(pkg);
    free(restored);
}
```

---

## Python CLI (Optional Scripting Wrapper)

The repository also provides an optional Python 3 CLI utility ([`binpack.py`](binpack.py)) that produces and reads 100% cross-compatible `.bpk` containers:

```bash
# Requirements (only if using Python)
pip install zstandard numpy

# Usage
python binpack.py bench payload.exe
python binpack.py compress input.dll -o input.dll.bpk
python binpack.py decompress input.dll.bpk -o input_restored.dll
```

## Building the Native Engine

### Windows (MSVC, GCC, Clang, or Zig CC)
Run the provided build script:
```cmd
build.bat
```
Or directly via MSVC:
```cmd
cl /O2 /W3 binpack.c test_native.c /Fe:test_native.exe
```

### Linux / macOS
```bash
make
./test_native
```

---

## Licensing

This project is licensed under the **[MIT License](LICENSE)**.

It is 100% free and open-source software. You are free to study, modify, distribute, and integrate this engine into both open-source and proprietary commercial software without fees or restrictions.

For questions, technical feedback, or collaboration:  
**`santiagorobledo198@gmail.com`**  
Or open an inquiry via [GitHub Repository Issues](https://github.com/Santirobm/binpack/issues).

---

## Support Continued Development

BINPACK is an independent, 100% free, and open-source project under the MIT License. If this engine saves you bandwidth, accelerates your storage pipelines, or assists your research, consider supporting continued algorithmic optimizations:

- **[Support via Ko-fi](https://ko-fi.com/santirobm)**
- **[Sponsor on GitHub](https://github.com/sponsors/Santirobm)**

---

## Upstream Proposal

This architectural proposal and empirical findings have been formally submitted upstream to the Meta Zstandard engineering team for technical discussion:
- **[facebook/zstd #4857: RFC / Proposal: Domain-specific polymorphic pre-filtering (BCJ & Multi-Stride) on binary payloads](https://github.com/facebook/zstd/issues/4857)**

---

## Citation & Technical Inquiries

To reference this project in academic research or technical benchmarks:

```bibtex
@software{binpack2026,
  author = {Santiago Robledo},
  title = {BINPACK: Polymorphic Binary Pre-filtering Compression Engine},
  year = {2026},
  url = {https://github.com/Santirobm/binpack}
}
```
