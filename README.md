# TitanNTFS: Sovereign Clean-Room NTFS Engine for Windows & Modern Platforms

[![TitanNTFS CI](https://github.com/MicaNT-Kernel/TitanNTFS/actions/workflows/ci.yml/badge.svg)](https://github.com/MicaNT-Kernel/TitanNTFS/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-orange.svg)]()
[![Clean-Room Certified](https://img.shields.io/badge/Clean--Room-Sentinel%20Audited-brightgreen.svg)](CLEAN_ROOM_PROVENANCE.md)

**TitanNTFS** is a modern, high-performance, clean-room implementation of the **New Technology File System (NTFS)** architecture. Built in pure ISO C++23 from first principles, TitanNTFS provides a lightning-fast, zero-dependency alternative to legacy NTFS implementations, delivering standalone volume formatting, Alternate Data Streams (ADS), Master File Table ($MFT) record manipulation, and Update Sequence Array (USA) fixup validation.

---

## 🌟 Key Features

- **Format Any Drive or Image to NTFS**: Instant volume formatting for external SSDs, USB drives, and `.img`/`.vhdx` images.
- **Update Sequence Array (USA) Fixup Protection**: Full generation and verification of USN fixup arrays across 512-byte sector boundaries to protect against torn writes.
- **Alternate Data Streams (ADS)**: Native support for multi-stream file attributes (e.g. `file.txt:Zone.Identifier`).
- **Run-Length Encoded Data Runs**: LCN / VCN cluster extent mapping for non-resident data streams.
- **Master File Table ($MFT) Engine**: Clean-room 1024-byte record layout for resident and non-resident attributes.
- **Strict Clean-Room Provenance**: 100% written from scratch with zero proprietary code exposure. Verified on every commit by our Clean-Room Sentinel.

---

## 🛠️ CLI Utilities

### 1. `titanntfs_format`
Format any disk image or physical drive to NTFS:
```bash
titanntfs_format my_disk.img --size 128 --cluster 4096 --label "Storage"
```

### 2. `titanntfs_check`
Mount and inspect NTFS volume boot records and MFT integrity:
```bash
titanntfs_check my_disk.img
```

---

## 🚀 Building & Running Tests

### Prerequisites
- CMake 3.24+
- Modern C++23 Compiler (MSVC 2022 v17.8+, Clang 18+, or GCC 14+)

### Build
```bash
cmake -B build -G "Ninja" -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Run Test Battery
```bash
./build/titanntfs_tests
```

### Run Clean-Room Sentinel Audit
```bash
node scripts/clean_room_sentinel.js --local
```

---

## ⚖️ Legal & Clean-Room Architecture
TitanNTFS is an independent clean-room implementation developed for functional interoperability under 17 U.S.C. § 1201(f) and SCOTUS fair use doctrine (*Google v. Oracle*). See [CLEAN_ROOM_PROVENANCE.md](CLEAN_ROOM_PROVENANCE.md) for full compliance declarations.

---

## 📄 License
Licensed under the Apache License, Version 2.0.
Copyright © 2026 Daniel Elliott <ssfdre38@gmail.com>.
