// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sfc/formats/sprite_archive.h"

#include "common/bytes.h"
#include "common/stream.h"

#include <unordered_map>
#include <utility>

namespace reluva::sfc {

namespace {

constexpr size_t kFileHeader = 14;
constexpr size_t kImageHeader = 20;
constexpr size_t kPaletteBytes = 512; // an 8-bit image's 256 RGB555 colours

// Each run's code: its top two bits say what follows, the rest count
// pixels. In 16-bit images a code is 16 bits, in 8-bit ones a byte.
constexpr unsigned kLiteral = 0; // that many pixels follow
constexpr unsigned kFill = 1;    // one pixel follows, repeated
constexpr unsigned kSkip = 3;    // that many transparent pixels, nothing follows

void putRgb555(uint8_t *p, uint16_t c) {
    const auto expand = [](unsigned v) { return static_cast<uint8_t>((v << 3) | (v >> 2)); };
    p[0] = expand((c >> 10) & 31);
    p[1] = expand((c >> 5) & 31);
    p[2] = expand(c & 31);
    p[3] = 255;
}

std::string invalid(const SpriteArchive::Image &image, const char *what) {
    return "sprites.q3: the image at " + std::to_string(image.offset) + " " + what;
}

} // namespace

Error SpriteArchive::open(const std::filesystem::path &path) {
    std::vector<uint8_t> data;
    if (!readFile(path, data))
        return Error::fail("Can't read " + path.string());
    return open(std::move(data));
}

Error SpriteArchive::open(std::vector<uint8_t> data) {
    _data = std::move(data);
    _images.clear();
    if (_data.size() < kFileHeader || readLE32(_data.data()) != tagLE('M', 'D', 'M', '!'))
        return Error::fail("sprites.q3: not an MDM! file");
    _objects = readLE32(_data.data() + 10);
    if (_objects < kFileHeader || _objects > _data.size())
        return Error::fail("sprites.q3: its objects' offset is past its end");

    // The images fill the space between the header and the objects.
    size_t at = kFileHeader;
    while (at < _objects) {
        if (_objects - at < kImageHeader)
            return Error::fail("sprites.q3: an image's header at " + std::to_string(at) + " runs into the objects");
        const uint8_t *h = _data.data() + at;
        Image image;
        image.offset = static_cast<uint32_t>(at);
        image.width = readLE16(h);
        image.height = readLE16(h + 2);
        image.hotX = static_cast<int16_t>(readLE16(h + 4));
        image.hotY = static_cast<int16_t>(readLE16(h + 6));
        image.bits = static_cast<uint8_t>(readLE16(h + 10));
        const uint32_t rest = readLE32(h + 16);
        if (image.bits != 8 && image.bits != 16)
            return Error::fail(invalid(image, "has neither 8 nor 16 bits a pixel"));
        if (rest > _objects - at - kImageHeader)
            return Error::fail(invalid(image, "runs into the objects"));
        image.size = static_cast<uint32_t>(kImageHeader + rest);
        _images.push_back(image);
        at += image.size;
    }
    findNames();
    return Error::success();
}

// An object naming an image keeps the name (a 32-bit length, then the
// characters), four zero bytes, then the image's size and offset. Images
// referred to only by their size and offset (an animation's frames, a
// button's states) keep no name.
void SpriteArchive::findNames() {
    std::unordered_map<uint32_t, size_t> byOffset;
    for (size_t i = 0; i < _images.size(); ++i)
        byOffset[_images[i].offset] = i;
    const uint8_t *d = _data.data();
    for (size_t p = _objects + 8; p + 8 <= _data.size(); ++p) {
        const auto it = byOffset.find(readLE32(d + p + 4));
        if (it == byOffset.end() || readLE32(d + p) != _images[it->second].size || readLE32(d + p - 4) != 0)
            continue;
        const size_t end = p - 4;
        size_t start = end;
        while (start > _objects + 4 && d[start - 1] >= 0x20 && d[start - 1] < 0x7f && end - start < 255)
            --start;
        // The longest printable run may take in the length's own bytes;
        // the name is the run's tail that its length counts.
        for (size_t s = start; s < end; ++s) {
            if (readLE32(d + s - 4) == end - s) {
                Image &image = _images[it->second];
                if (image.name.empty())
                    image.name.assign(reinterpret_cast<const char *>(d + s), end - s);
                break;
            }
        }
    }
}

Error SpriteArchive::decode(const Image &image, Surface &out) const {
    out.create(image.width, image.height, PixelFormat::kRGBA32);
    const size_t end = image.offset + image.size;
    const bool wide = image.bits == 16;
    size_t at = image.offset + kImageHeader;
    uint16_t palette[256] = {};
    if (!wide) {
        if (end - at < kPaletteBytes)
            return Error::fail(invalid(image, "is too short for its palette"));
        for (int i = 0; i < 256; ++i)
            palette[i] = readLE16(_data.data() + at + 2 * i);
        at += kPaletteBytes;
    }
    const size_t rows = at;
    const size_t data = rows + 4 * size_t(image.height);
    if (data > end)
        return Error::fail(invalid(image, "is too short for its rows' table"));
    // A row's offset counts its codes' units: 16-bit words, or bytes.
    const size_t unit = wide ? 2 : 1;
    for (int y = 0; y < image.height; ++y) {
        size_t p = data + unit * readLE32(_data.data() + rows + 4 * size_t(y));
        const size_t rowEnd =
            y + 1 < image.height ? data + unit * readLE32(_data.data() + rows + 4 * size_t(y + 1)) : end;
        if (p > rowEnd || rowEnd > end)
            return Error::fail(invalid(image, "has a row outside it"));
        uint8_t *px = out.row(y);
        int x = 0;
        const auto pixel = [&]() -> uint16_t {
            const uint16_t v = wide ? readLE16(_data.data() + p) : palette[_data[p]];
            p += unit;
            return v;
        };
        while (p < rowEnd) {
            const unsigned code = wide ? readLE16(_data.data() + p) : _data[p];
            p += unit;
            const unsigned kind = code >> (wide ? 14 : 6);
            const int n = static_cast<int>(code & (wide ? 0x3fffu : 0x3fu));
            if (x + n > image.width)
                return Error::fail(invalid(image, "has a row wider than the image"));
            if (kind == kSkip) {
                x += n;
                continue;
            }
            if (kind != kLiteral && kind != kFill)
                return Error::fail(invalid(image, "has a run of an unknown kind"));
            const size_t needs = unit * (kind == kFill ? 1 : size_t(n));
            if (rowEnd - p < needs)
                return Error::fail(invalid(image, "has a run past its row's end"));
            if (kind == kFill) {
                const uint16_t c = pixel();
                for (int i = 0; i < n; ++i)
                    putRgb555(px + 4 * (x + i), c);
            } else {
                for (int i = 0; i < n; ++i)
                    putRgb555(px + 4 * (x + i), pixel());
            }
            x += n;
        }
        if (x != image.width)
            return Error::fail(invalid(image, "has a row narrower than the image"));
    }
    return Error::success();
}

} // namespace reluva::sfc
