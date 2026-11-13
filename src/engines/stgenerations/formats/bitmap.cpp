// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/formats/bitmap.h"

#include "image/bmp.h"
#include "common/log.h"

#include <cstring>

namespace reluva::stgenerations {

bool BitmapDecoder::loadStream(SeekableReadStream& stream) { return load(stream, true); }

bool BitmapDecoder::loadDIB(SeekableReadStream& stream) { return load(stream, false); }

bool BitmapDecoder::load(SeekableReadStream& stream, bool file) {
    surface_.free();
    paletteColors_ = 0;
    palette_ = Palette();
    const std::vector<uint8_t> data = stream.readAll();
    image::Bitmap bmp;
    const Error err = file ? image::decodeBmp(data, bmp) : image::decodeDib(data.data(), data.size(), bmp);
    if (err) {
        log::warning("%s", err.message.c_str());
        return false;
    }
    const Surface& s = bmp.surface;
    if (bmp.indexed()) {
        surface_.create(s.w, s.h, Canvas::kClut8);
        for (int y = 0; y < s.h; y++)
            std::memcpy(surface_.getBasePtr(0, y), s.row(y), size_t(s.w));
        palette_ = bmp.palette;
        paletteColors_ = uint16_t(bmp.paletteColors);
    } else {
        surface_.create(s.w, s.h, Canvas::kArgb32);
        for (int y = 0; y < s.h; y++) {
            const uint8_t* p = s.row(y);
            for (int x = 0; x < s.w; x++)
                surface_.setPixel(x, y, argb(p[x * 4], p[x * 4 + 1], p[x * 4 + 2]));
        }
    }
    return true;
}

} // namespace reluva::stgenerations
