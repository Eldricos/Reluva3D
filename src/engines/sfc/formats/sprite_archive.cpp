// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sfc/formats/sprite_archive.h"

#include "common/bytes.h"
#include "common/stream.h"

#include <algorithm>
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
    return readObjects();
}

// The objects' section: its size (to the file's end) and its count of
// records, then the records, each starting with its kind (16 bits), its
// number (32: 1, 2, 3... in order), 0x99 (16), and its name (a 32-bit
// length, then the characters). What follows depends on the kind, and
// isn't worked out but for an image's (kind 1): four zero bytes, then the
// image's size and offset. So a record ends where the next one's number
// is found.
Error SpriteArchive::readObjects() {
    _records.clear();
    const uint8_t *d = _data.data();
    if (_data.size() - _objects < 8 || readLE32(d + _objects) != _data.size() - _objects)
        return Error::fail("sprites.q3: its objects' section isn't the rest of the file");
    const uint32_t count = readLE32(d + _objects + 4);
    std::unordered_map<uint32_t, size_t> byOffset;
    for (size_t i = 0; i < _images.size(); ++i)
        byOffset[_images[i].offset] = i;
    size_t at = _objects + 8;
    for (uint32_t id = 1; id <= count; ++id) {
        // Its kind (below 64), its number and the tag, the first after the
        // record before.
        uint8_t want[6];
        writeLE32(want, id);
        want[4] = 0x99;
        want[5] = 0;
        auto found = _data.begin() + long(at);
        for (;;) {
            found = std::search(found, _data.end(), want, want + 6);
            if (found == _data.end() || readLE16(&*found - 2) < 64)
                break;
            ++found;
        }
        const size_t start = size_t(found - _data.begin()) - 2;
        if (found == _data.end() || start < at || start + 12 > _data.size())
            return Error::fail("sprites.q3: object " + std::to_string(id) + " of " + std::to_string(count) +
                               " not found");
        if (!_records.empty())
            _records.back().size = static_cast<uint32_t>(start - _records.back().offset);
        Object o;
        o.kind = readLE16(d + start);
        o.id = id;
        o.offset = static_cast<uint32_t>(start);
        const uint32_t length = readLE32(d + start + 8);
        if (length > _data.size() - start - 12)
            return Error::fail("sprites.q3: object " + std::to_string(id) + "'s name runs past the end");
        o.name.assign(reinterpret_cast<const char *>(d + start + 12), length);
        // An image's record names the image at its size and offset.
        const size_t fields = start + 12 + length;
        if (o.kind == 1 && fields + 12 <= _data.size() && readLE32(d + fields) == 0) {
            const auto it = byOffset.find(readLE32(d + fields + 8));
            if (it != byOffset.end() && _images[it->second].size == readLE32(d + fields + 4) &&
                _images[it->second].name.empty())
                _images[it->second].name = o.name;
        }
        _records.push_back(std::move(o));
        at = start + 8;
    }
    if (!_records.empty())
        _records.back().size = static_cast<uint32_t>(_data.size() - _records.back().offset);
    for (Object &o : _records) {
        if (o.kind == kKindImage) {
            const size_t fields = o.offset + 12 + o.name.size();
            if (fields + 12 <= o.offset + o.size)
                if (const auto it = byOffset.find(readLE32(_data.data() + fields + 8)); it != byOffset.end())
                    o.image = static_cast<int32_t>(it->second);
        } else {
            readFields(o);
        }
    }
    return Error::success();
}

// The fields after a record's name, as far as they're worked out: the
// program's class (a 32-bit length, then the characters), then for a
// group (kinds 3 and 17) a word whose use isn't known, the count of its
// children and each child's record, x, y and depth (32 bits each); for a
// button or picture (kinds 8 and 10), at the record's end, three lists of
// image records (each a count, then the records). What lies between
// isn't read. Where the fields don't fit, the record is left without them.
void SpriteArchive::readFields(Object &o) {
    const uint8_t *d = _data.data();
    const size_t end = size_t(o.offset) + o.size;
    size_t at = o.offset + 12 + o.name.size();
    if (end - at < 4 || readLE32(d + at) > end - at - 4)
        return;
    const uint32_t classLength = readLE32(d + at);
    o.className.assign(reinterpret_cast<const char *>(d + at + 4), classLength);
    at += 4 + classLength;
    const auto isImage = [this](uint32_t id) {
        const Object *r = object(id);
        return r && r->kind == kKindImage;
    };
    if (o.kind == kKindScreen || o.kind == kKindButtonGroup) {
        if (end - at < 8)
            return;
        const uint32_t count = readLE32(d + at + 4);
        if (count > (end - at - 8) / 16)
            return;
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t *c = d + at + 8 + 16 * size_t(i);
            Child child{readLE32(c), static_cast<int32_t>(readLE32(c + 4)), static_cast<int32_t>(readLE32(c + 8)),
                        readLE32(c + 12)};
            if (!object(child.id)) {
                o.children.clear();
                return;
            }
            o.children.push_back(child);
        }
    } else if (o.kind == kKindButton || o.kind == kKindPicture) {
        // The three lists end the record: the start found is the latest
        // from which they run exactly to its end.
        for (size_t start = end >= 12 ? end - 12 : 0; start + 1 > at; --start) {
            std::vector<uint32_t> lists[3];
            size_t p = start;
            bool fits = true;
            for (auto &list : lists) {
                if (end - p < 4 || readLE32(d + p) > (end - p - 4) / 4) {
                    fits = false;
                    break;
                }
                const uint32_t n = readLE32(d + p);
                for (uint32_t i = 0; i < n && fits; ++i) {
                    const uint32_t frame = readLE32(d + p + 4 + 4 * size_t(i));
                    fits = isImage(frame);
                    list.push_back(frame);
                }
                p += 4 + 4 * size_t(n);
                if (!fits)
                    break;
            }
            if (fits && p == end) {
                for (int i = 0; i < 3; ++i)
                    o.frames[i] = std::move(lists[i]);
                return;
            }
            if (start == 0)
                break;
        }
    }
}

Error SpriteArchive::compose(uint32_t id, Surface &out, int x, int y) const {
    return compose(id, out, x, y, 0);
}

Error SpriteArchive::compose(uint32_t id, Surface &out, int x, int y, int depth) const {
    const Object *o = object(id);
    if (!o || depth > 32) // no tree in the files is that deep; a loop would be
        return Error::success();
    if (o->kind == kKindImage) {
        if (o->image < 0)
            return Error::success();
        const Image &image = _images[size_t(o->image)];
        Surface picture;
        if (Error err = decode(image, picture))
            return err;
        // Its pixels over what's there, by their alpha (all or nothing).
        const int left = x - image.hotX;
        const int top = y - image.hotY;
        for (int py = std::max(0, -top); py < picture.h && top + py < out.h; ++py) {
            const uint8_t *src = picture.row(py);
            uint8_t *dst = out.row(top + py);
            for (int px = std::max(0, -left); px < picture.w && left + px < out.w; ++px)
                if (src[4 * px + 3])
                    std::copy(src + 4 * px, src + 4 * px + 4, dst + 4 * (left + px));
        }
        return Error::success();
    }
    if (!o->children.empty()) {
        std::vector<Child> order = o->children;
        std::stable_sort(order.begin(), order.end(), [](const Child &a, const Child &b) { return a.depth > b.depth; });
        for (const Child &c : order)
            if (Error err = compose(c.id, out, x + c.x, y + c.y, depth + 1))
                return err;
        return Error::success();
    }
    if (!o->frames[0].empty())
        return compose(o->frames[0].front(), out, x, y, depth + 1);
    return Error::success();
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
