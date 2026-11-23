// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#pragma once
#include "common/types.h"
#include <array>
#include <map>
#include <span>
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
// Bounded Unicode subset for native text views. Composite XY-offset outlines
// are flattened independently; font hint instructions are never executed.
class TrueTypeUnicode {
public:
    Error read(const std::vector<uint8_t>& bytes,int pixelHeight,std::span<const uint32_t> characters);
    Error addFallback(const std::vector<uint8_t>& bytes,std::span<const uint32_t> characters);
    bool loaded() const {return height_!=0;}
    bool has(uint32_t cp) const {return glyphs_.contains(cp);}
    int advance(uint32_t cp) const;
    void draw(Surface& surface,int x,int baseline,uint32_t cp,Color color,bool bold=false,bool italic=false) const;
private:
    struct Glyph {int left=0,top=0,w=0,h=0,advance=0;std::vector<uint8_t> alpha;};
    std::map<uint32_t,Glyph> glyphs_;
    int height_=0;
};
} // namespace reluva::font
