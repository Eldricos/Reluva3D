// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/fsh.h"

#include "engines/nfs/refpack.h"

#include <algorithm>
#include <cstring>

namespace reluva::nfs {

namespace {

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) {
    return p[0] | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}

// Bytes per pixel of picture and palette types; 0 for others.
int bytesPerPixel(uint8_t type) {
    switch (type) {
    case kShape8Bit:
        return 1;
    case kShape16Bit565:
    case kShape16Bit1555:
    case kPalette16Bit1555:
        return 2;
    case kShape24Bit:
    case kPalette24Bit:
        return 3;
    case kShape32Bit:
    case kPalette32Bit:
        return 4;
    default:
        return 0;
    }
}

// The length of text in a fixed-size field, up to its first 0.
size_t fieldLength(const uint8_t* p, size_t size) {
    return static_cast<size_t>(std::find(p, p + size, 0) - p);
}

uint8_t expand5(unsigned v) { return static_cast<uint8_t>(v << 3 | v >> 2); }
uint8_t expand6(unsigned v) { return static_cast<uint8_t>(v << 2 | v >> 4); }

// One pixel of a picture or palette type as RGBA.
void toRgba(uint8_t type, const uint8_t* p, uint8_t* out) {
    switch (type) {
    case kShape16Bit565: {
        const unsigned v = le16(p);
        out[0] = expand5(v >> 11);
        out[1] = expand6((v >> 5) & 0x3f);
        out[2] = expand5(v & 0x1f);
        out[3] = 255;
        break;
    }
    case kShape16Bit1555:
    case kPalette16Bit1555: {
        const unsigned v = le16(p);
        out[0] = expand5((v >> 10) & 0x1f);
        out[1] = expand5((v >> 5) & 0x1f);
        out[2] = expand5(v & 0x1f);
        out[3] = v & 0x8000 ? 255 : 0;
        break;
    }
    case kShape24Bit:
    case kPalette24Bit:
        out[0] = p[2];
        out[1] = p[1];
        out[2] = p[0];
        out[3] = 255;
        break;
    case kShape32Bit:
    case kPalette32Bit:
        out[0] = p[2];
        out[1] = p[1];
        out[2] = p[0];
        out[3] = p[3];
        break;
    default:
        std::memset(out, 0, 4);
    }
}

} // namespace

const char* shapeTypeName(uint8_t type) {
    switch (type) {
    case kShape8Bit:
        return "8-bit";
    case kShape16Bit565:
        return "16-bit 5:6:5";
    case kShape16Bit1555:
        return "16-bit 1:5:5:5";
    case kShape24Bit:
        return "24-bit";
    case kShape32Bit:
        return "32-bit";
    default:
        return "unknown";
    }
}

Error parseShapeBank(std::vector<uint8_t> data, ShapeBank& out) {
    out = {};
    if (Error err = unpackIfRefPack(data))
        return err;
    auto bad = [](const std::string& why) { return Error::fail("Not a valid .fsh file: " + why); };
    if (data.size() < 16 || std::memcmp(data.data(), "SHPI", 4) != 0)
        return bad("no SHPI header");
    const uint32_t count = le32(&data[8]);
    out.id.assign(reinterpret_cast<const char*>(&data[12]), 4);
    if (16 + static_cast<uint64_t>(count) * 8 > data.size())
        return bad("its directory runs past the end");

    std::vector<uint32_t> offsets(count);
    for (uint32_t i = 0; i < count; ++i)
        offsets[i] = le32(&data[20 + i * 8]);
    // A shape's last block runs to the next shape (or the end of the file).
    std::vector<uint32_t> sorted = offsets;
    sorted.push_back(static_cast<uint32_t>(data.size()));
    std::sort(sorted.begin(), sorted.end());

    for (uint32_t i = 0; i < count; ++i) {
        Shape shape;
        shape.name.assign(reinterpret_cast<const char*>(&data[16 + i * 8]), fieldLength(&data[16 + i * 8], 4));
        const uint32_t limit = *std::upper_bound(sorted.begin(), sorted.end(), offsets[i]);

        size_t pos = offsets[i];
        bool first = true;
        while (true) {
            // Pictures and palettes have a 16-byte header, other blocks 8.
            if (pos + 8 > limit || (first && pos + 16 > limit))
                return bad("shape " + shape.name + " runs past its space");
            const uint8_t type = data[pos];
            const uint32_t next = data[pos + 1] | data[pos + 2] << 8 | data[pos + 3] << 16;
            const size_t end = next ? pos + next : limit;
            if (end > limit || end < pos + 8)
                return bad("shape " + shape.name + " has a bad block size");
            const int w = le16(&data[pos + 4]), h = le16(&data[pos + 6]);

            if (first) {
                const int bpp = bytesPerPixel(type);
                if (bpp == 0 || type == kPalette16Bit1555 || type == kPalette24Bit || type == kPalette32Bit)
                    return bad("shape " + shape.name + " has unknown type " + std::to_string(type));
                const size_t bytes = static_cast<size_t>(w) * h * bpp;
                if (pos + 16 + bytes > end)
                    return bad("shape " + shape.name + "'s pixels run past its block");
                shape.type = type;
                shape.width = w;
                shape.height = h;
                shape.centerX = static_cast<int16_t>(le16(&data[pos + 8]));
                shape.centerY = static_cast<int16_t>(le16(&data[pos + 10]));
                shape.x = static_cast<int16_t>(le16(&data[pos + 12]));
                shape.y = static_cast<int16_t>(le16(&data[pos + 14]));
                shape.pixels.assign(data.begin() + pos + 16, data.begin() + pos + 16 + bytes);
                first = false;
            } else if (type == kPalette16Bit1555 || type == kPalette24Bit || type == kPalette32Bit) {
                // Width is the number of colours.
                const int bpp = bytesPerPixel(type);
                if (pos + 16 > end || pos + 16 + static_cast<size_t>(w) * bpp > end)
                    return bad("shape " + shape.name + "'s palette runs past its block");
                shape.palette.resize(static_cast<size_t>(w) * 4);
                for (int c = 0; c < w; ++c)
                    toRgba(type, &data[pos + 16 + c * bpp], &shape.palette[c * 4]);
            } else if (type == kShapeText) {
                // The text's length, then the text.
                const uint32_t length = le32(&data[pos + 4]);
                if (pos + 8 + length > end)
                    return bad("shape " + shape.name + "'s text runs past its block");
                std::string s(reinterpret_cast<const char*>(&data[pos + 8]), fieldLength(&data[pos + 8], length));
                while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' '))
                    s.pop_back();
                shape.texts.push_back(std::move(s));
            } else {
                shape.unknown.push_back({type, std::vector<uint8_t>(data.begin() + pos + 4, data.begin() + end)});
            }
            if (!next)
                break;
            pos += next;
        }
        out.shapes.push_back(std::move(shape));
    }
    return Error::success();
}

Error decodeShape(const Shape& shape, image::Image& out) {
    out.w = shape.width;
    out.h = shape.height;
    out.rgba.assign(static_cast<size_t>(out.w) * out.h * 4, 0);
    const size_t count = static_cast<size_t>(out.w) * out.h;
    if (shape.type == kShape8Bit) {
        if (shape.palette.empty())
            return Error::fail("shape " + shape.name + " has no palette");
        const size_t colours = shape.palette.size() / 4;
        for (size_t i = 0; i < count; ++i) {
            const uint8_t index = shape.pixels[i];
            if (index < colours)
                std::memcpy(&out.rgba[i * 4], &shape.palette[index * 4], 4);
        }
        return Error::success();
    }
    const int bpp = bytesPerPixel(shape.type);
    if (bpp == 0)
        return Error::fail("shape " + shape.name + " has unknown type " + std::to_string(shape.type));
    for (size_t i = 0; i < count; ++i)
        toRgba(shape.type, &shape.pixels[i * bpp], &out.rgba[i * 4]);
    return Error::success();
}

} // namespace reluva::nfs
