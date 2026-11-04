// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/types.h"
#include <array>
#include <string_view>
namespace reluva::font {
// Deliberately small TrueType subset: printable ASCII, Unicode cmap format 4,
// simple quadratic outlines. Instructions are bounded and skipped, never run.
class TrueTypeAscii {
public:
    Error read(const std::vector<uint8_t>& bytes,int pixelHeight);
    bool loaded() const { return height_!=0; }
    int measure(std::string_view text) const;
    int draw(Surface& surface,int x,int baseline,std::string_view text,Color color,int maxWidth) const;
private:
    struct Glyph {int left=0,top=0,w=0,h=0,advance=0;std::vector<uint8_t> alpha;};
    std::array<Glyph,95> glyphs_;
    int height_=0;
};
} // namespace reluva::font
