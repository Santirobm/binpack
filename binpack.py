#!/usr/bin/env python3
"""
BINPACK - High-Performance Polymorphic Binary Pre-filtering Codec
Reference CLI Implementation

Copyright (c) 2026 Santiago Robledo
Licensed under the MIT License
"""

import os
import sys
import time
import struct
import hashlib
import argparse
import collections
import zlib
import bz2
import zstandard as zstd
import numpy as np

def fwht(a):
    h = 1
    a = np.array(a, dtype=np.int64)
    n = len(a)
    while h < n:
        for i in range(0, n, h * 2):
            for j in range(i, i + h):
                x = a[j]; y = a[j + h]
                a[j] = x + y; a[j + h] = x - y
        h *= 2
    return a

def ifwht(a):
    return fwht(a) / len(a)

class BinPackCodecV2:
    MAGIC = b"BPK2"
    
    MODE_GENERIC   = 0
    MODE_BCJ_X86   = 1
    MODE_BCJ_ARM   = 2
    MODE_DELTA_2   = 3
    MODE_STRIDE_4  = 4
    MODE_STRIDE_8  = 5
    MODE_FWHT      = 6
    MODE_STRIDE_2  = 7
    MODE_BCJ_ARM64 = 8
    MODE_BCJ_RISCV = 9

    MODE_NAMES = {
        MODE_GENERIC  : "Generic Binary (Direct Passthrough)",
        MODE_BCJ_X86  : "Machine Code PE/ELF x86/x64 (BCJ Filter)",
        MODE_BCJ_ARM  : "Machine Code ARM32 (ARM Filter)",
        MODE_BCJ_ARM64: "Machine Code ARM64 (AArch64 Filter)",
        MODE_BCJ_RISCV: "Machine Code RISC-V (RV32/RV64 JAL Filter)",
        MODE_DELTA_2  : "16-bit Waveform / Medical Resonance (Delta-2)",
        MODE_STRIDE_2 : "Structured 16-bit Records (Stride-2 Transpose)",
        MODE_STRIDE_4 : "32-bit Floats / Coordinates (Stride-4 Transpose)",
        MODE_STRIDE_8 : "64-bit Doubles / Timestamps (Stride-8 Transpose)",
        MODE_FWHT     : "Boolean Spectral Matrix (FWHT)"
    }

    def __init__(self, level=19):
        self.level = level
        self.cctx = zstd.ZstdCompressor(level=self.level)
        self.dctx = zstd.ZstdDecompressor()

    @staticmethod
    def _find_code_bounds(data: bytes) -> tuple:
        n = len(data)
        # 1. PE Section Header Table (.text / IMAGE_SCN_MEM_EXECUTE)
        if n >= 0x40 and data[:2] == b"MZ":
            pe_off = struct.unpack("<I", data[0x3C:0x40])[0]
            if pe_off <= n - 24 and data[pe_off:pe_off+4] == b"PE\x00\x00":
                num_sec = struct.unpack("<H", data[pe_off+6:pe_off+8])[0]
                opt_sz = struct.unpack("<H", data[pe_off+20:pe_off+22])[0]
                sec_tbl = pe_off + 24 + opt_sz
                for i in range(num_sec):
                    entry = sec_tbl + i * 40
                    if entry + 40 > n:
                        break
                    raw_sz, raw_ptr = struct.unpack("<II", data[entry+16:entry+24])
                    charact = struct.unpack("<I", data[entry+36:entry+40])[0]
                    is_code = (data[entry:entry+5] == b".text") or bool(charact & 0x20000000)
                    if is_code and raw_sz > 0 and raw_ptr < n:
                        if raw_ptr + raw_sz <= n:
                            return raw_ptr, raw_sz
        # 2. ELF Program Header Table (PT_LOAD + PF_X)
        if n >= 56 and data[:4] == b"\x7fELF":
            elf_class = data[4]
            if elf_class == 1 and n >= 52:
                phoff = struct.unpack("<I", data[28:32])[0]
                phentsize = struct.unpack("<H", data[42:44])[0]
                phnum = struct.unpack("<H", data[44:46])[0]
                if phentsize >= 32 and phoff < n:
                    for i in range(phnum):
                        entry = phoff + i * phentsize
                        if entry + 32 > n:
                            break
                        p_type, p_offset, _, p_filesz, p_flags = struct.unpack("<IIIII", data[entry:entry+20] + data[entry+24:entry+28])
                        if p_type == 1 and (p_flags & 1) and p_filesz > 0 and p_offset + p_filesz <= n:
                            return p_offset, p_filesz
            elif elf_class == 2 and n >= 64:
                phoff = struct.unpack("<Q", data[32:40])[0]
                phentsize = struct.unpack("<H", data[54:56])[0]
                phnum = struct.unpack("<H", data[56:58])[0]
                if phentsize >= 56 and phoff < n:
                    for i in range(phnum):
                        entry = phoff + i * phentsize
                        if entry + 56 > n:
                            break
                        p_type, p_flags = struct.unpack("<II", data[entry:entry+8])
                        p_offset = struct.unpack("<Q", data[entry+8:entry+16])[0]
                        p_filesz = struct.unpack("<Q", data[entry+32:entry+40])[0]
                        if p_type == 1 and (p_flags & 1) and p_filesz > 0 and p_offset + p_filesz <= n:
                            return p_offset, p_filesz
        return 0, n

    @staticmethod
    def _filter_bcj_x86(data: bytes, encoding: bool) -> bytes:
        offset, length = BinPackCodecV2._find_code_bounds(data)
        d = bytearray(data)
        i = offset
        end = offset + length
        while i <= end - 5:
            b = d[i]
            if b == 0xE8 or b == 0xE9:
                src = d[i+1] | (d[i+2] << 8) | (d[i+3] << 16) | (d[i+4] << 24)
                dest = (src + (i + 5)) & 0xFFFFFFFF if encoding else (src - (i + 5)) & 0xFFFFFFFF
                d[i+1] = dest & 0xFF
                d[i+2] = (dest >> 8) & 0xFF
                d[i+3] = (dest >> 16) & 0xFF
                d[i+4] = (dest >> 24) & 0xFF
                i += 5
            else:
                i += 1
        return bytes(d)

    @staticmethod
    def _filter_bcj_arm(data: bytes, encoding: bool) -> bytes:
        offset, length = BinPackCodecV2._find_code_bounds(data)
        d = bytearray(data)
        end = offset + length
        for i in range(offset, end - 3, 4):
            if d[i + 3] == 0xEB:  # ARM32 BL opcode
                src = d[i] | (d[i+1] << 8) | (d[i+2] << 16)
                offset_val = (i + 8) >> 2
                dest = (src + offset_val) & 0xFFFFFF if encoding else (src - offset_val) & 0xFFFFFF
                d[i] = dest & 0xFF
                d[i+1] = (dest >> 8) & 0xFF
                d[i+2] = (dest >> 16) & 0xFF
        return bytes(d)

    @staticmethod
    def _filter_bcj_arm64(data: bytes, encoding: bool) -> bytes:
        offset, length = BinPackCodecV2._find_code_bounds(data)
        d = bytearray(data)
        end = offset + length
        for i in range(offset, end - 3, 4):
            instr = d[i] | (d[i+1] << 8) | (d[i+2] << 16) | (d[i+3] << 24)
            if (instr >> 26) == 0x25:  # AArch64 BL instruction
                src = instr & 0x03FFFFFF
                offset_val = i >> 2
                dest = (src + offset_val) & 0x03FFFFFF if encoding else (src - offset_val) & 0x03FFFFFF
                instr = (instr & 0xFC000000) | dest
                d[i] = instr & 0xFF
                d[i+1] = (instr >> 8) & 0xFF
                d[i+2] = (instr >> 16) & 0xFF
                d[i+3] = (instr >> 24) & 0xFF
        return bytes(d)

    @staticmethod
    def _filter_bcj_riscv(data: bytes, encoding: bool) -> bytes:
        offset, length = BinPackCodecV2._find_code_bounds(data)
        d = bytearray(data)
        end = offset + length
        for i in range(offset, end - 3, 4):
            instr = d[i] | (d[i+1] << 8) | (d[i+2] << 16) | (d[i+3] << 24)
            if (instr & 0x7F) == 0x6F:  # RISC-V JAL opcode
                offset20 = (((instr >> 31) & 1) << 19) | \
                           (((instr >> 12) & 0xFF) << 11) | \
                           (((instr >> 20) & 1) << 10) | \
                           ((instr >> 21) & 0x3FF)
                dest = (offset20 + (i >> 1)) & 0xFFFFF if encoding else (offset20 - (i >> 1)) & 0xFFFFF
                instr = (instr & 0x00000FFF) | \
                        (((dest >> 19) & 1) << 31) | \
                        ((dest & 0x3FF) << 21) | \
                        (((dest >> 10) & 1) << 20) | \
                        (((dest >> 11) & 0xFF) << 12)
                d[i]   = instr & 0xFF
                d[i+1] = (instr >> 8) & 0xFF
                d[i+2] = (instr >> 16) & 0xFF
                d[i+3] = (instr >> 24) & 0xFF
        return bytes(d)

    @staticmethod
    def _filter_delta_encode(data: bytes, dist: int = 2) -> bytes:
        if dist <= 0 or not data:
            return data
        d = bytearray(len(data))
        d[:dist] = data[:dist]
        for i in range(dist, len(data)):
            d[i] = (data[i] - data[i - dist]) & 0xFF
        return bytes(d)

    @staticmethod
    def _filter_delta_decode(data: bytes, dist: int = 2) -> bytes:
        if dist <= 0 or not data:
            return data
        d = bytearray(len(data))
        d[:dist] = data[:dist]
        for i in range(dist, len(data)):
            d[i] = (d[i - dist] + data[i]) & 0xFF
        return bytes(d)

    def _max_channel_concentration(self, data: bytes, stride: int, max_samples=8192) -> float:
        if stride <= 0 or not data:
            return 0.0
        sample_len = min(len(data), max_samples)
        max_c = 0.0
        for offset in range(stride):
            ch = data[offset:sample_len:stride]
            if not ch:
                continue
            counts = collections.Counter(ch)
            top2 = sum(c for _, c in counts.most_common(2)) / len(ch)
            if top2 > max_c:
                max_c = top2
        return max_c

    def detect_mode(self, data: bytes):
        n = len(data)
        if n < 64:
            return self.MODE_GENERIC
            
        # 1. PE Executable Header Inspection (Windows)
        if data[:2] == b"MZ" and n >= 0x40:
            pe_offset = struct.unpack("<I", data[0x3C:0x40])[0]
            if pe_offset <= n - 24 and data[pe_offset:pe_offset+4] == b"PE\x00\x00":
                machine = struct.unpack("<H", data[pe_offset+4:pe_offset+6])[0]
                if machine == 0xaa64:
                    return self.MODE_BCJ_ARM64
                if machine in (0x01c0, 0x01c2, 0x01c4):
                    return self.MODE_BCJ_ARM
                if machine in (0x5032, 0x5064):
                    return self.MODE_BCJ_RISCV
                return self.MODE_BCJ_X86
                
        # 2. ELF Executable Header Inspection (Linux / Android)
        if data[:4] == b"\x7fELF" and n >= 20:
            e_machine = struct.unpack("<H", data[18:20])[0]
            if e_machine == 0xF3:
                return self.MODE_BCJ_RISCV
            if e_machine == 0xB7:
                return self.MODE_BCJ_ARM64
            if e_machine == 0x28:
                return self.MODE_BCJ_ARM
            return self.MODE_BCJ_X86

        # 3. Mach-O Executable Header Inspection (macOS / iOS)
        if n >= 8 and data[:4] in (b"\xfe\xed\xfa\xce", b"\xfe\xed\xfa\xcf", b"\xce\xfa\xed\xfe", b"\xcf\xfa\xed\xfe"):
            cputype = struct.unpack("<I", data[4:8])[0]
            if cputype == 0x0100000c:
                return self.MODE_BCJ_ARM64
            if cputype == 12:
                return self.MODE_BCJ_ARM
            return self.MODE_BCJ_X86

        # 4. Smooth 16-bit Continuous Waveforms (Audio PCM, MRI imaging)
        if n >= 512:
            sample_len = min(n, 2048) // 2
            # Check for UTF-16 false positives: high byte being zero
            zero_high = sum(1 for i in range(sample_len) if data[i * 2 + 1] == 0)
            if zero_high < (sample_len * 7) // 10:
                diffs = []
                for i in range(0, sample_len * 2 - 2, 2):
                    v1 = struct.unpack("<h", data[i:i+2])[0]
                    v2 = struct.unpack("<h", data[i+2:i+4])[0]
                    diffs.append(abs(v2 - v1))
                avg_diff = sum(diffs) / max(1, len(diffs))
                if avg_diff < 10000:
                    return self.MODE_DELTA_2

        # 5. Stride-8 (64-bit Doubles, Timestamps, Pointers)
        if n >= 512:
            if self._max_channel_concentration(data, 8) > 0.40:
                return self.MODE_STRIDE_8

        # 6. Stride-4 (32-bit Floats, 3D Coordinates)
        if n >= 256:
            if self._max_channel_concentration(data, 4) > 0.30:
                return self.MODE_STRIDE_4

        # 7. Stride-2 (16-bit Structured Records, RGB565)
        if n >= 256:
            if self._max_channel_concentration(data, 2) > 0.20:
                return self.MODE_STRIDE_2

        return self.MODE_GENERIC

    def _transpose(self, data: bytes, stride: int) -> bytes:
        if stride <= 1 or not data:
            return data
        n = len(data)
        pad = (stride - (n % stride)) % stride
        padded = data + (b"\x00" * pad)
        channels = [padded[i::stride] for i in range(stride)]
        return b"".join(channels)

    def _itranspose(self, transposed: bytes, stride: int, orig_len: int) -> bytes:
        if stride <= 1 or not transposed:
            return transposed[:orig_len]
        chunk = len(transposed) // stride
        channels = [transposed[i*chunk : (i+1)*chunk] for i in range(stride)]
        out = bytearray(orig_len)
        idx = 0
        for i in range(chunk):
            for s in range(stride):
                if idx < orig_len:
                    out[idx] = channels[s][i]
                    idx += 1
        return bytes(out)

    def compress(self, data: bytes, force_mode=None) -> tuple:
        orig_len = len(data)
        sha256_full = hashlib.sha256(data).digest()
        mode = self.detect_mode(data) if force_mode is None else force_mode
        
        # Apply zero-loss reversible pre-filters
        if mode == self.MODE_BCJ_X86:
            filtered = self._filter_bcj_x86(data, encoding=True)
        elif mode == self.MODE_BCJ_ARM:
            filtered = self._filter_bcj_arm(data, encoding=True)
        elif mode == self.MODE_BCJ_ARM64:
            filtered = self._filter_bcj_arm64(data, encoding=True)
        elif mode == self.MODE_BCJ_RISCV:
            filtered = self._filter_bcj_riscv(data, encoding=True)
        elif mode == self.MODE_DELTA_2:
            filtered = self._filter_delta_encode(data, 2)
        elif mode == self.MODE_STRIDE_2:
            filtered = self._transpose(data, 2)
        elif mode == self.MODE_STRIDE_4:
            filtered = self._transpose(data, 4)
        elif mode == self.MODE_STRIDE_8:
            filtered = self._transpose(data, 8)
        else:
            filtered = data

        compressed_core = self.cctx.compress(filtered)
            
        header = bytearray(self.MAGIC)
        header.append(mode)
        header.append(self.level)
        header.extend(struct.pack(">Q", orig_len))
        header.extend(sha256_full[:8])
        
        return bytes(header + compressed_core), mode

    def decompress(self, package: bytes) -> bytes:
        if len(package) < 22:
            raise ValueError("Invalid format: payload smaller than BPK2 header")
        if package[:4] != self.MAGIC:
            raise ValueError("Invalid format: missing BPK2 magic header")
        mode = package[4]
        # package[5] is compression level
        orig_len = struct.unpack(">Q", package[6:14])[0]
        expected_sha = package[14:22]
        payload = package[22:]
        
        decomp = self.dctx.decompress(payload)

        # Invert the pre-filters
        if mode == self.MODE_BCJ_X86:
            final_data = self._filter_bcj_x86(decomp, encoding=False)
        elif mode == self.MODE_BCJ_ARM:
            final_data = self._filter_bcj_arm(decomp, encoding=False)
        elif mode == self.MODE_BCJ_ARM64:
            final_data = self._filter_bcj_arm64(decomp, encoding=False)
        elif mode == self.MODE_BCJ_RISCV:
            final_data = self._filter_bcj_riscv(decomp, encoding=False)
        elif mode == self.MODE_DELTA_2:
            final_data = self._filter_delta_decode(decomp, 2)
        elif mode == self.MODE_STRIDE_2:
            final_data = self._itranspose(decomp, 2, orig_len)
        elif mode == self.MODE_STRIDE_4:
            final_data = self._itranspose(decomp, 4, orig_len)
        elif mode == self.MODE_STRIDE_8:
            final_data = self._itranspose(decomp, 8, orig_len)
        else:
            final_data = decomp
            
        actual_sha = hashlib.sha256(final_data).digest()
        if actual_sha[:8] != expected_sha:
            raise ValueError("Checksum verification failure: payload corrupted")
        if len(final_data) != orig_len:
            raise ValueError("Length mismatch: decompressed size does not match header")
        return final_data

def cmd_compress(args):
    input_file = args.input
    output_file = args.output or (input_file + ".bpk")
    level = args.level
    
    if not os.path.exists(input_file):
        sys.stderr.write(f"Error: input file '{input_file}' not found.\n")
        sys.exit(1)
        
    with open(input_file, "rb") as f:
        data = f.read()
    orig_size = len(data)
    
    codec = BinPackCodecV2(level=level)
    t0 = time.time()
    package, mode = codec.compress(data)
    elapsed = time.time() - t0
    
    with open(output_file, "wb") as f:
        f.write(package)
    final_size = len(package)
    ratio = (1 - final_size / orig_size) * 100 if orig_size > 0 else 0.0
    speed = (orig_size / (1024 * 1024)) / max(0.0001, elapsed)
    
    print(f"Compressed: {input_file} -> {output_file}")
    print(f"Original  : {orig_size:,} bytes")
    print(f"Final     : {final_size:,} bytes ({ratio:.2f}% reduction)")
    print(f"Filter    : {codec.MODE_NAMES.get(mode, 'Unknown')}")
    print(f"Throughput: {speed:.1f} MB/s in {elapsed*1000:.1f} ms")

def cmd_decompress(args):
    input_file = args.input
    if not os.path.exists(input_file):
        sys.stderr.write(f"Error: input file '{input_file}' not found.\n")
        sys.exit(1)
        
    default_out = input_file[:-4] if input_file.endswith(".bpk") else (input_file + ".out")
    output_file = args.output or default_out
    
    with open(input_file, "rb") as f:
        package = f.read()
    
    codec = BinPackCodecV2()
    t0 = time.time()
    try:
        restored = codec.decompress(package)
        elapsed = time.time() - t0
    except Exception as e:
        sys.stderr.write(f"Decompression error: {e}\n")
        sys.exit(1)
        
    with open(output_file, "wb") as f:
        f.write(restored)
    speed = (len(restored) / (1024 * 1024)) / max(0.0001, elapsed)
    print(f"Decompressed: {input_file} -> {output_file} ({len(restored):,} bytes)")
    print(f"Integrity   : SHA-256 OK (Lossless verified)")
    print(f"Throughput  : {speed:.1f} MB/s in {elapsed*1000:.1f} ms")

def cmd_bench(args):
    input_file = args.input
    if not os.path.exists(input_file):
        sys.stderr.write(f"Error: input file '{input_file}' not found.\n")
        sys.exit(1)
        
    with open(input_file, "rb") as f:
        data = f.read()
    n = len(data)
    
    print("=" * 80)
    print(f"BENCHMARK: {os.path.basename(input_file)} ({n:,} bytes / {n/(1024*1024):.2f} MB)")
    print("=" * 80)
    
    # 1. ZIP (Deflate level 9)
    t0 = time.time()
    z_bytes = zlib.compress(data, level=9)
    t_zip = time.time() - t0
    
    # 2. BZIP2 (level 9)
    t0 = time.time()
    b_bytes = bz2.compress(data, compresslevel=9)
    t_bz2 = time.time() - t0
    
    # 3. Zstandard Ultra (level 22)
    zstd_ultra = zstd.ZstdCompressor(level=22)
    t0 = time.time()
    zu_bytes = zstd_ultra.compress(data)
    t_zstd = time.time() - t0
    
    # 4. BINPACK v2 (Prefilter + Zstandard Level 22)
    codec = BinPackCodecV2(level=22)
    t0 = time.time()
    bpk_bytes, mode = codec.compress(data)
    t_bpk = time.time() - t0
    
    t0 = time.time()
    rec = codec.decompress(bpk_bytes)
    t_decomp = time.time() - t0
    assert rec == data, "Integrity verification failed: decompressed data does not match original"
    
    competitors = [
        ("ZIP (Deflate Level 9)", len(z_bytes), t_zip),
        ("BZIP2 (Level 9)", len(b_bytes), t_bz2),
        ("Meta Zstandard (Level 22)", len(zu_bytes), t_zstd),
        ("BINPACK v2 (Filter + Zstd)", len(bpk_bytes), t_bpk)
    ]
    competitors.sort(key=lambda x: x[1])
    
    print(f"{'Rank':<5} | {'Compressor':<28} | {'Final Size':<14} | {'Ratio':<9} | {'Comp. Time'}")
    print("-" * 80)
    print(f"{'-':<5} | {'Uncompressed (Original)':<28} | {n:6,} bytes   | {'0.0%':<9} | {'-'}")
    
    for rank, (name, sz, el) in enumerate(competitors, 1):
        pct = (1 - sz / n) * 100
        tag = "  [#1 Lowest Size]" if rank == 1 else ""
        print(f"#{rank:<4} | {name:<28} | {sz:6,} bytes   | {pct:5.2f}%   | {el*1000:7.1f} ms{tag}")
        
    print("=" * 80)
    print(f"Active Filter : {codec.MODE_NAMES.get(mode, 'Unknown')}")
    print(f"Decomp. Time  : {t_decomp*1000:.2f} ms (Lossless SHA-256 verified)")
    
    if len(bpk_bytes) < len(zu_bytes):
        diff = len(zu_bytes) - len(bpk_bytes)
        print(f"Result        : BINPACK outperforms Meta Zstandard Ultra by {diff:,} bytes ({diff/len(zu_bytes)*100:.1f}% smaller)")
    elif len(bpk_bytes) == len(zu_bytes):
        print(f"Result        : Parity with Meta Zstandard Ultra")
    else:
        diff = len(bpk_bytes) - len(zu_bytes)
        print(f"Result        : Meta Zstandard Ultra is {diff:,} bytes smaller")

def main():
    parser = argparse.ArgumentParser(description="BINPACK - High-Performance Polymorphic Binary Compressor")
    subparsers = parser.add_subparsers(dest="command", help="Available subcommands")
    
    p_comp = subparsers.add_parser("compress", help="Compress a file into .bpk format")
    p_comp.add_argument("input", help="Source file path")
    p_comp.add_argument("-o", "--output", help="Destination .bpk path")
    p_comp.add_argument("-l", "--level", type=int, default=19, help="Compression level (1-22, default 19)")
    
    p_decomp = subparsers.add_parser("decompress", help="Decompress a .bpk container")
    p_decomp.add_argument("input", help="Input .bpk file path")
    p_decomp.add_argument("-o", "--output", help="Restored file path")
    
    p_bench = subparsers.add_parser("bench", help="Benchmark against ZIP, BZIP2, and Meta Zstandard")
    p_bench.add_argument("input", help="Test file path")
    
    args = parser.parse_args()
    if args.command == "compress":
        cmd_compress(args)
    elif args.command == "decompress":
        cmd_decompress(args)
    elif args.command == "bench":
        cmd_bench(args)
    else:
        parser.print_help()

if __name__ == "__main__":
    main()
