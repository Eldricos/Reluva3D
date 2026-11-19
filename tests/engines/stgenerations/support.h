// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// What the Star Trek: Generations tests share (STG-Engine's test helpers).

#pragma once

#include "common/stream.h"
#include "tests/support/test.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

// CHECK(a == b), saying what each side was when they differ (for numbers).
#define CHECK_EQUAL(a, b)                                                                                              \
    do {                                                                                                               \
        const auto checkA_ = (a);                                                                                      \
        const auto checkB_ = (b);                                                                                      \
        if (!(checkA_ == checkB_)) {                                                                                   \
            std::fprintf(stderr, "%s:%d: CHECK_EQUAL failed: %s == %s\n", __FILE__, __LINE__, #a, #b);                 \
            ++::reluva::test::failures();                                                                              \
        }                                                                                                              \
    } while (0)

namespace reluva::stgenerations::stgtest {

// A folder for a test's files, made empty and removed afterwards.
class TempDir {
public:
    TempDir() {
        static int next = 0;
        _path = std::filesystem::temp_directory_path() /
                ("reluva-stg-test-" + std::to_string(reinterpret_cast<uintptr_t>(this)) + "-" + std::to_string(++next));
        std::error_code ec;
        std::filesystem::remove_all(_path, ec);
        std::filesystem::create_directories(_path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(_path, ec);
    }
    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;
    const std::filesystem::path &path() const { return _path; }
    // A file relative to the folder, its folders made.
    void writeFile(const std::string &name, const std::vector<uint8_t> &data) const {
        const std::filesystem::path p = _path / name;
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        reluva::writeFile(p, data);
    }
    void writeFile(const std::string &name, const std::string &data) const {
        writeFile(name, std::vector<uint8_t>(data.begin(), data.end()));
    }

private:
    std::filesystem::path _path;
};

// An 8-bit BMP of width x height pixels (rows top first), or RLE8 data as
// given; two colours: 0 black, 1 R 255 G 128 B 0.
inline std::vector<uint8_t> makeBMP8(int width, int height, const std::vector<uint8_t> &pixels, bool rle) {
    MemoryWriteStream w;
    const int pitch = (width + 3) & ~3;
    std::vector<uint8_t> data;
    if (rle) {
        data = pixels;
    } else {
        // Bottom-up rows, padded to 4 bytes.
        for (int y = height - 1; y >= 0; y--)
            for (int x = 0; x < pitch; x++)
                data.push_back(x < width ? pixels[size_t(y) * width + x] : 0);
    }
    const uint32_t offset = 14 + 40 + 4 * 2;
    w.writeByte('B');
    w.writeByte('M');
    w.writeUint32LE(offset + uint32_t(data.size()));
    w.writeUint32LE(0);
    w.writeUint32LE(offset);
    w.writeUint32LE(40);
    w.writeUint32LE(uint32_t(width));
    w.writeUint32LE(uint32_t(height));
    w.writeUint16LE(1);
    w.writeUint16LE(8);
    w.writeUint32LE(rle ? 1 : 0);
    w.writeUint32LE(uint32_t(data.size()));
    w.writeUint32LE(0);
    w.writeUint32LE(0);
    w.writeUint32LE(2); // colours used
    w.writeUint32LE(0);
    // The palette, B G R x.
    w.writeUint32LE(0x00000000);
    w.writeUint32LE(0x00FF8000); // R 255, G 128, B 0
    w.write(data.data(), uint32_t(data.size()));
    return w.data();
}

} // namespace reluva::stgenerations::stgtest
