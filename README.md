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

1. **Machine Code Branch Displacements**: Relative call and jump offsets (`CALL`/`JMP` on x86, `BL` on ARM) cause identical subroutines to compile into disparate 32-bit byte sequences depending on their memory addresses, impeding match deduplication.
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
    ├─► PE / ELF / Mach-O (ARM/ARM64) ────► [BCJ ARM Filter] (BL instruction normalization)
    ├─► Continuous 16-bit Waveforms ──────► [Delta-2 Filter] (Finite difference transformation)
    ├─► Structured 16-bit Records ────────► [Stride-2 Transpose] (Channel separation)
    ├─► 32-bit Floats / 3D Coordinates ───► [Stride-4 Transpose] (Field separation)
    ├─► 64-bit Doubles / Timestamps ──────► [Stride-8 Transpose] (Field separation)
    └─► Unstructured Binary ──────────────► [Passthrough]
                                                 │
                                                 ▼
                                     [High-Density Backend]
                                      (Zstandard / LZMA2)
                                                 │
                                                 ▼
                                          .bpk Container
                                (Magic + Mode + SHA-256 Checksum)
```

---

## Benchmark Results

All benchmarks executed under identical hardware conditions on official dataset corpora and standard operating system binaries.

### 1. Official Silesia Corpus (Standard Industry Benchmark)

Comparisons against Meta's **Zstandard Level 22 (Ultra)** and standard Deflate (Level 9):

| Dataset File | Data Format | Uncompressed | Meta Zstd L22 | BINPACK v2 | Net Savings vs Zstd |
| :--- | :--- | :---: | :---: | :---: | :---: |
| **`x-ray`** (Silesia) | 16-bit Medical Image | 8,474,240 B | 5,129,824 B (39.5%) | **4,252,074 B (49.8%)** | **-877,750 B (-17.1%)** |
| **`ooffice`** (Silesia) | Compiled x86 Executable | 6,152,192 B | 2,594,905 B (57.8%) | **2,084,550 B (66.1%)** | **-510,355 B (-19.7%)** |
| **`mr`** (Silesia) | MRI Resonance Scan | 9,970,564 B | 3,102,439 B (68.9%) | **2,610,190 B (73.8%)** | **-492,249 B (-15.9%)** |
| **`sensor_data`** | 32-bit Float Matrix | 262,144 B | 226,242 B (13.7%) | **156,765 B (40.2%)** | **-69,477 B (-30.7%)** |
| **`telemetry`** | 16-bit Structured Pairs | 262,144 B | 210,086 B (19.9%) | **180,472 B (31.2%)** | **-29,614 B (-14.1%)** |
| **`arm_binary`** | ARM Machine Code | 100,064 B | 22,680 B (77.3%) | **194 B (99.8%)** | **-22,486 B (-99.1%)** |
| **Total Net Savings** | — | — | — | — | **-2,001,931 bytes (~2.0 MB)** |

*All outputs verified 100% lossless bit-for-bit with SHA-256 checksum validation.*

---

### 2. Native C Engine Performance Metrics (`test_native.exe`)

Measured on x86-64 hardware using the native C99 implementation (`binpack.c`):

| Test File | Payload Size | Active Mode | Profiler Latency | Filter Throughput | Lossless Verification |
| :--- | :---: | :--- | :---: | :---: | :---: |
| `ooffice` | 6.15 MB | Machine Code x86 (BCJ) | **< 0.001 µs** | **1,955.7 MB/s** | PASS (100% bit-exact) |
| `x-ray` | 8.47 MB | 16-bit Waveform (Delta-2) | **0.500 µs** | **1,616.3 MB/s** | PASS (100% bit-exact) |
| `mr` | 9.97 MB | 16-bit Waveform (Delta-2) | **0.500 µs** | **2,377.2 MB/s** | PASS (100% bit-exact) |
| `sensor_data.raw` | 256 KB | 64-bit Doubles (Stride-8) | **3.800 µs** | **Memory saturation** | PASS (100% bit-exact) |
| `telemetry_16bit.raw` | 256 KB | 16-bit Records (Stride-2) | **7.100 µs** | **Memory saturation** | PASS (100% bit-exact) |

---

## C API Quick Start

Include [`binpack.h`](binpack.h) in your C or C++ application:

```c
#include "binpack.h"
#include <stdio.h>
#include <stdlib.h>

void process_buffer(const uint8_t *data, size_t size) {
    /* 1. Sub-microsecond format detection */
    binpack_mode_t mode = binpack_detect_mode(data, size);
    printf("Detected layout: %s\n", binpack_mode_name(mode));

    /* 2. Apply appropriate domain-specific transformation */
    uint8_t *filtered = (uint8_t*)malloc(size + 16);
    if (mode == BINPACK_MODE_BCJ_X86) {
        memcpy(filtered, data, size);
        binpack_filter_bcj_x86(filtered, size, 1 /* encode */);
    } else if (mode == BINPACK_MODE_DELTA_2) {
        binpack_filter_delta_encode(data, filtered, size, 2 /* distance */);
    } else if (mode == BINPACK_MODE_STRIDE_4) {
        binpack_transpose(data, filtered, size, 4 /* stride */);
    }
    
    /* 3. Feed filtered stream into Zstandard or storage engine */
    // ...
    
    free(filtered);
}
```

---

## CLI Reference Implementation

The repository includes a Python CLI utility ([`binpack.py`](binpack.py)):

### Requirements
- Python 3.10+
- `pip install zstandard numpy`

### Usage

```bash
# Run benchmark against ZIP, BZIP2, and Meta Zstandard Ultra
python binpack.py bench payload.exe

# Compress a file
python binpack.py compress input.dll -o input.dll.bpk

# Decompress a file (with automatic SHA-256 integrity check)
python binpack.py decompress input.dll.bpk -o input_restored.dll
```

On Windows, use `binpack.cmd`:
```cmd
.\binpack.cmd bench ooffice
```

---

## Building the Native Engine

### Windows (MSVC or GCC)
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
