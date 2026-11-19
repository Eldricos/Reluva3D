// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/formats/bitmap.h"

#include "image/bmp.h"
#include "common/log.h"

#include <cstring>

namespace reluva::stgenerations {

bool BitmapDecoder::loadStream(SeekableReadStream &stream) {
    return load(stream, true);
}

bool BitmapDecoder::loadDIB(SeekableReadStream &stream) {
    return load(stream, false);
}

bool BitmapDecoder::load(SeekableReadStream &stream, bool file) {
    _surface.free();
    _paletteColors = 0;
    _palette = Palette();
    const std::vector<uint8_t> data = stream.readAll();
    image::Bitmap bmp;
    const Error err = file ? image::decodeBmp(data, bmp) : image::decodeDib(data.data(), data.size(), bmp);
    if (err) {
        log::warning("%s", err.message.c_str());
        return false;
    }
    const Surface &s = bmp.surface;
    if (bmp.indexed()) {
        _surface.create(s.w, s.h, Canvas::kClut8);
        for (int y = 0; y < s.h; y++)
            std::memcpy(_surface.getBasePtr(0, y), s.row(y), size_t(s.w));
        _palette = bmp.palette;
        _paletteColors = uint16_t(bmp.paletteColors);
    } else {
        _surface.create(s.w, s.h, Canvas::kArgb32);
        for (int y = 0; y < s.h; y++) {
            const uint8_t *p = s.row(y);
            for (int x = 0; x < s.w; x++)
                _surface.setPixel(x, y, argb(p[x * 4], p[x * 4 + 1], p[x * 4 + 2]));
        }
    }
    return true;
}

} // namespace reluva::stgenerations
