// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/resources.h"

#include "common/fs.h"
#include "common/zip.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

namespace reluva::clue {

namespace {

uint16_t u16(const std::vector<uint8_t>& d, size_t o) { return uint16_t(d[o] | d[o + 1] << 8); }
uint32_t u32(const std::vector<uint8_t>& d, size_t o) { return uint32_t(u16(d, o)) | uint32_t(u16(d, o + 2)) << 16; }
int16_t s16(const std::vector<uint8_t>& d, size_t o) { return int16_t(u16(d, o)); }

constexpr size_t kPackHeader = 12;
constexpr size_t kDriveHeader = 12;
constexpr size_t kDriveEntry = 148; // u32 0, FILETIME, offset, size, 128-byte name
constexpr size_t kDriveName = 128;

} // namespace

Error unpack(const std::vector<uint8_t>& data, std::vector<uint8_t>& out) {
    if (data.size() < 4 || std::memcmp(data.data(), "PACK", 4) != 0) {
        out = data;
        return Error::success();
    }
    if (data.size() < kPackHeader)
        return Error::fail("PACK header cut short");
    const uint32_t packed = u32(data, 4), size = u32(data, 8);
    if (packed > data.size() - kPackHeader)
        return Error::fail("PACK data cut short");
    return inflateZlib(data.data() + kPackHeader, packed, size, out);
}

Error DataDrive::open(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return Error::fail("can't open " + path.string());
    const std::vector<uint8_t> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<uint8_t> data;
    if (Error err = unpack(raw, data))
        return Error::fail(path.filename().string() + ": " + err.message);
    if (Error err = load(std::move(data)))
        return Error::fail(path.filename().string() + ": " + err.message);
    path_ = path;
    return Error::success();
}

Error DataDrive::load(std::vector<uint8_t> data) {
    entries_.clear();
    data_ = std::move(data);
    if (data_.size() >= 4 && std::memcmp(data_.data(), "RESI", 4) == 0)
        return Error::fail("a RESI file (the menus' and dialogs' layout), not a DDRV archive");
    if (data_.size() < kDriveHeader + 4 || std::memcmp(data_.data(), "DDRV", 4) != 0)
        return Error::fail("not a DDRV archive");
    const uint32_t directory = u32(data_, 4), count = u32(data_, 8);
    if (directory > data_.size() - 4 || (data_.size() - directory - 4) / kDriveEntry < count)
        return Error::fail("DDRV directory cut short");
    entries_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const size_t e = directory + i * kDriveEntry;
        Entry entry;
        entry.time = uint64_t(u32(data_, e + 4)) | uint64_t(u32(data_, e + 8)) << 32;
        entry.offset = u32(data_, e + 12);
        entry.size = u32(data_, e + 16);
        const char* name = reinterpret_cast<const char*>(data_.data() + e + 20);
        entry.name.assign(name, strnlen(name, kDriveName));
        if (entry.offset > data_.size())
            return Error::fail("DDRV entry " + entry.name + " lies beyond the end");
        entries_.push_back(std::move(entry));
    }
    return Error::success();
}

const DataDrive::Entry* DataDrive::find(std::string_view name) const {
    const std::string want = fs::toLower(name);
    for (const Entry& e : entries_)
        if (fs::toLower(e.name) == want)
            return &e;
    return nullptr;
}

std::vector<uint8_t> DataDrive::read(const Entry& entry) const {
    const size_t end = std::min<size_t>(data_.size(), size_t(entry.offset) + entry.size);
    return {data_.begin() + entry.offset, data_.begin() + std::max<size_t>(end, entry.offset)};
}

Error decodeImage(const std::vector<uint8_t>& r, Image& out) {
    if (r.size() < 52 || std::memcmp(r.data(), "EIMG", 4) != 0)
        return Error::fail("not an EIMG record");
    out = {};
    out.version = int(u32(r, 4));
    int w = 0, h = 0;
    size_t dataAt = 0, dataSize = 0;
    uint8_t rowEnd = 0;
    if (out.version == 0x200) {
        if (r.size() < 57)
            return Error::fail("EIMG header cut short");
        w = int(u32(r, 16));
        h = int(u32(r, 20));
        out.cellW = w;
        out.cellH = h;
        dataSize = u32(r, 53);
        dataAt = 57;
        rowEnd = 0x80;
    } else if (out.version == 0x100) {
        w = u16(r, 12);
        h = u16(r, 14);
        out.offsetX = s16(r, 36);
        out.offsetY = s16(r, 38);
        out.cellW = u16(r, 40);
        out.cellH = u16(r, 42);
        dataSize = u32(r, 48);
        dataAt = 52;
        rowEnd = 0x00;
    } else {
        return Error::fail("EIMG version " + std::to_string(out.version) + " isn't known");
    }
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096)
        return Error::fail("EIMG size " + std::to_string(w) + "x" + std::to_string(h) + " is out of range");
    if (dataSize > r.size() - dataAt)
        return Error::fail("EIMG pixels cut short");
    out.pixels.w = w;
    out.pixels.h = h;
    out.pixels.rgba.assign(size_t(w) * h * 4, 0);
    // Per row: n (1-127) pixels follow, 0x80 | n skips n, and rowEnd ends
    // the row. Pixels are RGB565.
    const uint8_t* p = r.data() + dataAt;
    const uint8_t* end = p + dataSize;
    for (int y = 0; y < h && p < end; ++y) {
        int x = 0;
        while (p < end) {
            const uint8_t c = *p++;
            if (c == rowEnd)
                break;
            if (c & 0x80) {
                x += c & 0x7f;
                continue;
            }
            if (size_t(end - p) < size_t(c) * 2)
                return Error::fail("EIMG pixels cut short");
            for (int i = 0; i < c; ++i, p += 2, ++x) {
                if (x >= w)
                    return Error::fail("EIMG row runs past the image's width");
                const uint16_t v = uint16_t(p[0] | p[1] << 8);
                uint8_t* px = out.pixels.pixel(x, y);
                px[0] = uint8_t((v >> 11) * 255 / 31);
                px[1] = uint8_t(((v >> 5) & 63) * 255 / 63);
                px[2] = uint8_t((v & 31) * 255 / 31);
                px[3] = 255;
            }
        }
    }
    return Error::success();
}

Error parseAnimation(const std::vector<uint8_t>& r, Animation& out) {
    out = {};
    size_t p = 0;
    const auto need = [&](size_t n) { return r.size() >= p && r.size() - p >= n; };
    if (!need(7) || std::memcmp(r.data(), "ANIM", 4) != 0)
        return Error::fail("not an ANIM record");
    if (u16(r, 4) != 0x200)
        return Error::fail("ANIM version " + std::to_string(u16(r, 4)) + " isn't known");
    p = 6;
    const auto text = [&](std::string& s) {
        if (!need(1) || !need(1 + size_t(r[p])))
            return false;
        s.assign(reinterpret_cast<const char*>(r.data() + p + 1), r[p]);
        p += 1 + r[p];
        return true;
    };
    if (!text(out.name))
        return Error::fail("ANIM name cut short");
    // Unnamed records hold 128 bytes of the fill Visual C++'s debug heap
    // leaves in new memory.
    if (std::all_of(out.name.begin(), out.name.end(), [](char c) { return uint8_t(c) == 0xcd; }))
        out.name.clear();
    if (!need(23))
        return Error::fail("ANIM header cut short");
    out.kind[0] = r[p];
    out.kind[1] = r[p + 1];
    const int layerCount = r[p + 2];
    const int frameCount = u16(r, p + 3), imageCount = u16(r, p + 5);
    out.width = u16(r, p + 7);
    out.height = u16(r, p + 9);
    out.framesPerSecond = int(u32(r, p + 11));
    if (out.framesPerSecond <= 0 || out.framesPerSecond > 1000)
        return Error::fail("ANIM speed " + std::to_string(out.framesPerSecond) + " is out of range");
    p += 23; // and a value not understood (136 in every record), and the layers' size
    for (int i = 0; i < imageCount; ++i) {
        std::string name;
        if (!text(name))
            return Error::fail("ANIM image names cut short");
        out.images.push_back(std::move(name));
    }
    // Each layer: its number (u32) and one 10-byte part per frame.
    out.layers.resize(size_t(layerCount));
    for (int l = 0; l < layerCount; ++l) {
        if (!need(4 + size_t(frameCount) * 10))
            return Error::fail("ANIM layers cut short");
        p += 4;
        for (int f = 0; f < frameCount; ++f, p += 10) {
            Animation::Part part{u16(r, p), s16(r, p + 2), s16(r, p + 4), u16(r, p + 6), u16(r, p + 8)};
            if (part.image >= out.images.size() && part.image != Animation::kNoImage)
                return Error::fail("ANIM part's image " + std::to_string(part.image) + " is out of range");
            out.layers[size_t(l)].push_back(part);
        }
    }
    out.frames.resize(size_t(frameCount));
    if (!need(8) || std::memcmp(r.data() + p, "ISOA", 4) != 0)
        return Error::success();
    out.isometric = true;
    const uint32_t size = u32(r, p + 4);
    p += 8;
    if (!need(size))
        return Error::fail("ISOA block cut short");
    const size_t blockEnd = p + size;
    // The short form: 10 bytes a frame, the anchor and zeros. The long one:
    // 24 bytes and a number of 7-byte extras.
    out.shortForm = size == size_t(frameCount) * 10;
    for (Animation::Frame& f : out.frames) {
        if (blockEnd - p < (out.shortForm ? 10u : 24u))
            return Error::fail("ISOA frames cut short");
        f.anchorX = s16(r, p);
        f.anchorY = s16(r, p + 2);
        if (out.shortForm) {
            p += 10;
            continue;
        }
        for (int i = 0; i < 5; ++i)
            f.unknown[i] = u16(r, p + 4 + 2 * size_t(i));
        f.solid = s16(r, p + 14);
        f.sizeX = u16(r, p + 16);
        f.sizeY = u16(r, p + 18);
        f.height = u16(r, p + 20);
        const int extras = u16(r, p + 22);
        p += 24;
        if (size_t(blockEnd - p) < size_t(extras) * 7)
            return Error::fail("ISOA extras cut short");
        for (int i = 0; i < extras; ++i, p += 7) {
            std::array<uint8_t, 7> x;
            std::copy(r.begin() + long(p), r.begin() + long(p) + 7, x.begin());
            f.extras.push_back(x);
        }
    }
    if (p != blockEnd)
        return Error::fail("ISOA block's frames don't fill it");
    return Error::success();
}

bool splitResourceName(std::string_view full, ResourceName& out) {
    const size_t colon = full.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 >= full.size())
        return false;
    out.drive = std::string(full.substr(0, colon));
    out.name = std::string(full.substr(colon + 1));
    return true;
}

} // namespace reluva::clue
