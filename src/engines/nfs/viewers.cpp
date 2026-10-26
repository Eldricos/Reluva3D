// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/viewers.h"

#include "common/events.h"
#include "common/font.h"
#include "common/fs.h"

#include <algorithm>
#include <cmath>

namespace reluva::nfs {

namespace {

constexpr int kMargin = 16;
constexpr Color kBackground{16, 20, 32};
constexpr Color kTitleColor{255, 200, 64};
constexpr Color kLabelColor{150, 160, 180};
constexpr Color kTextColor{230, 230, 230};
constexpr Color kWarnColor{255, 120, 100};

void fill(Surface& s, int x0, int y0, int w, int h, Color c) {
    for (int y = std::max(y0, 0); y < std::min(y0 + h, s.h); ++y) {
        uint8_t* p = s.row(y) + std::max(x0, 0) * 4;
        for (int x = std::max(x0, 0); x < std::min(x0 + w, s.w); ++x, p += 4) {
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = 255;
        }
    }
}

void clear(Surface& s) { fill(s, 0, 0, s.w, s.h, kBackground); }

// A line of text with the built-in font, cut to fit the screen.
void text(Surface& s, int x, int y, const std::string& utf8, Color c) {
    const size_t room = static_cast<size_t>(std::max(0, s.w - x - kMargin) / font::kAdvance);
    font::drawText(s, x, y, font::length(utf8) > room ? utf8.substr(0, room) : utf8, c);
}

// A page's title line and, at the bottom, its keys.
void frame(Surface& s, const std::string& title, const std::string& keys) {
    clear(s);
    text(s, kMargin, 8, title, kTitleColor);
    text(s, kMargin, s.h - font::kLineHeight - 4, keys + "   Tab: next page   Esc: launcher", kLabelColor);
}

std::string join(const std::vector<std::string>& names) {
    std::string out;
    for (const std::string& n : names)
        out += (out.empty() ? "" : " ") + n;
    return out;
}

// The part of a path after its last '/'.
std::string baseName(const std::string& path) {
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

// --- Info ---------------------------------------------------------------------

InfoPage::InfoPage(const char* gameName, GameFolders folders)
    : gameName_(gameName), folders_(std::move(folders)) {
    tracks_ = trackFolders(folders_.install);
    cars_ = carFolders(folders_.install);
    music_ = filesWithExtension(folders_.cd, "GameData/Audio/pc", ".mus");
    movies_ = filesWithExtension(folders_.cd, "FeData/movies", ".mad");
}

void InfoPage::draw(Surface& s) {
    frame(s, gameName_, "");
    constexpr int kValueX = kMargin + 10 * font::kAdvance;
    const size_t width = static_cast<size_t>((s.w - kValueX - kMargin) / font::kAdvance);
    int y = 8 + font::kLineHeight * 2;
    auto line = [&](const std::string& label, const std::string& value, Color color) {
        font::drawText(s, kMargin, y, label, kLabelColor);
        for (size_t i = 0; i < value.size() || i == 0; i += width) {
            font::drawText(s, kValueX, y, value.substr(i, width), color);
            y += font::kLineHeight;
        }
    };

    line("Install", folders_.install.string(), kTextColor);
    if (folders_.cd.empty()) {
        line("CD files", "not found: no music or movies", kWarnColor);
        line("", "Set cdpath=<folder> in this game's section of reluva.ini,", kLabelColor);
        line("", "or put a copy of the CD beside the install.", kLabelColor);
    } else {
        line("CD files", folders_.cd.string(), kTextColor);
        line("", "(" + folders_.cdSource + ")", kLabelColor);
        line("Music", std::to_string(music_.size()) + " streams", kTextColor);
        line("Movies", std::to_string(movies_.size()) + " files", kTextColor);
    }
    y += font::kLineHeight;
    line("Tracks", std::to_string(tracks_.size()) + ": " + join(tracks_), kTextColor);
    line("Cars", std::to_string(cars_.size()) + ": " + join(cars_), kTextColor);
}

// --- Textures -----------------------------------------------------------------

TexturePage::TexturePage(const GameFiles& files) : files_(files) {
    banks_ = files.withExtensions({".fsh", ".qfs"});
    if (!banks_.empty())
        load(0);
}

void TexturePage::load(int bank) {
    bank_ = bank;
    shape_ = 0;
    loaded_ = {};
    error_.clear();
    std::vector<uint8_t> data;
    if (Error err = files_.read(banks_[bank], data))
        error_ = err.message;
    else if (Error err2 = parseShapeBank(std::move(data), loaded_))
        error_ = err2.message;
    decodeCurrent();
}

void TexturePage::decodeCurrent() {
    image_ = {};
    imageError_.clear();
    if (shape_ < static_cast<int>(loaded_.shapes.size()))
        if (Error err = decodeShape(loaded_.shapes[shape_], image_))
            imageError_ = err.message;
}

void TexturePage::handleKey(uint32_t key, uint16_t) {
    const int banks = static_cast<int>(banks_.size());
    const int shapes = static_cast<int>(loaded_.shapes.size());
    switch (key) {
    case key::Down: load((bank_ + 1) % banks); break;
    case key::Up: load((bank_ + banks - 1) % banks); break;
    case key::PageDown: load(std::min(bank_ + 10, banks - 1)); break;
    case key::PageUp: load(std::max(bank_ - 10, 0)); break;
    case key::Right:
        if (shapes) {
            shape_ = (shape_ + 1) % shapes;
            decodeCurrent();
        }
        break;
    case key::Left:
        if (shapes) {
            shape_ = (shape_ + shapes - 1) % shapes;
            decodeCurrent();
        }
        break;
    case key::Home:
        shape_ = 0;
        decodeCurrent();
        break;
    case key::End:
        shape_ = std::max(shapes - 1, 0);
        decodeCurrent();
        break;
    case 'b': background_ = (background_ + 1) % 3; break;
    case 'a': alpha_ = !alpha_; break;
    default: break;
    }
}

void TexturePage::draw(Surface& s) {
    frame(s, "Textures  " + std::to_string(bank_ + 1) + "/" + std::to_string(banks_.size()) + "  " +
                 banks_[bank_].name,
          "Up/Down: file  Left/Right: picture  B: background  A: alpha");
    int y = 8 + font::kLineHeight;
    if (!error_.empty()) {
        text(s, kMargin, y, error_, kWarnColor);
        return;
    }
    if (loaded_.shapes.empty()) {
        text(s, kMargin, y, "No pictures", kLabelColor);
        return;
    }
    const Shape& shape = loaded_.shapes[shape_];
    std::string info = std::to_string(shape_ + 1) + "/" + std::to_string(loaded_.shapes.size()) + "  \"" +
                       shape.name + "\"  " + shapeTypeName(shape.type) + "  " + std::to_string(shape.width) +
                       "x" + std::to_string(shape.height) + "  centre " + std::to_string(shape.centerX) + "," +
                       std::to_string(shape.centerY) + "  pos " + std::to_string(shape.x) + "," +
                       std::to_string(shape.y);
    if (!shape.palette.empty())
        info += "  palette " + std::to_string(shape.palette.size() / 4);
    text(s, kMargin, y, info, kTextColor);
    y += font::kLineHeight;
    std::string extra;
    for (const std::string& t : shape.texts)
        extra += "\"" + t + "\" ";
    for (const auto& b : shape.unknown)
        extra += "[block " + std::to_string(b.type) + ", " + std::to_string(b.data.size()) + " bytes] ";
    if (!extra.empty())
        text(s, kMargin, y, extra, kLabelColor);
    y += font::kLineHeight + 4;
    if (!imageError_.empty()) {
        text(s, kMargin, y, imageError_, kWarnColor);
        return;
    }

    // The picture, scaled to fit the space left: up to 4x larger for
    // small ones, nearest pixel.
    const int areaW = s.w - 2 * kMargin, areaH = s.h - y - font::kLineHeight - 12;
    const int w = std::max(image_.w, 1), h = std::max(image_.h, 1);
    double scale = std::min(static_cast<double>(areaW) / w, static_cast<double>(areaH) / h);
    scale = scale >= 1 ? std::min(std::floor(scale), 4.0) : scale;
    const int dw = std::max(1, static_cast<int>(w * scale)), dh = std::max(1, static_cast<int>(h * scale));
    const int x0 = kMargin + (areaW - dw) / 2, y0 = y + (areaH - dh) / 2;
    for (int dy = 0; dy < dh; ++dy) {
        uint8_t* row = s.row(y0 + dy);
        const int sy = std::min(static_cast<int>(dy / scale), h - 1);
        for (int dx = 0; dx < dw; ++dx) {
            const int sx = std::min(static_cast<int>(dx / scale), w - 1);
            const uint8_t* src = image_.pixel(sx, sy);
            uint8_t* p = row + (x0 + dx) * 4;
            uint8_t bg = background_ == 1 ? 0 : background_ == 2 ? 255 : (((dx >> 3) ^ (dy >> 3)) & 1 ? 96 : 160);
            const unsigned a = alpha_ ? src[3] : 255;
            for (int c = 0; c < 3; ++c)
                p[c] = static_cast<uint8_t>((src[c] * a + bg * (255 - a)) / 255);
            p[3] = 255;
        }
    }
}

// --- Strings ------------------------------------------------------------------

StringPage::StringPage(const GameFiles& files) : files_(files) {
    tables_ = files.textTables();
    for (const FileRef& f : files.withExtensions({"arial10b.ffn"})) {
        std::vector<uint8_t> data;
        Font font;
        if (!files.read(f, data) && !parseFont(data, font)) {
            font_ = std::move(font);
            break;
        }
    }
    // English first, if there is one.
    int start = 0;
    for (size_t i = 0; i < tables_.size(); ++i)
        if (fs::toLower(baseName(tables_[i].name)) == "text.eng")
            start = static_cast<int>(i);
    if (!tables_.empty())
        load(start);
}

void StringPage::load(int table) {
    table_ = table;
    top_ = 0;
    loaded_ = {};
    error_.clear();
    std::vector<uint8_t> data;
    if (Error err = files_.read(tables_[table], data))
        error_ = err.message;
    else if (Error err2 = parseTextTable(data, loaded_))
        error_ = err2.message;
}

int StringPage::linesPerPage() const {
    const int lineHeight = gameFont_ && font_ ? font_->lineHeight : font::kLineHeight;
    return std::max(1, (480 - 3 * font::kLineHeight - 24) / lineHeight);
}

void StringPage::handleKey(uint32_t key, uint16_t) {
    const int tables = static_cast<int>(tables_.size());
    const int lastTop = std::max(0, static_cast<int>(loaded_.strings.size()) - linesPerPage());
    switch (key) {
    case key::Right: load((table_ + 1) % tables); break;
    case key::Left: load((table_ + tables - 1) % tables); break;
    case key::Down: top_ = std::min(top_ + 1, lastTop); break;
    case key::Up: top_ = std::max(top_ - 1, 0); break;
    case key::PageDown: top_ = std::min(top_ + linesPerPage(), lastTop); break;
    case key::PageUp: top_ = std::max(top_ - linesPerPage(), 0); break;
    case key::Home: top_ = 0; break;
    case key::End: top_ = lastTop; break;
    case 'f':
        gameFont_ = !gameFont_;
        top_ = std::min(top_, std::max(0, static_cast<int>(loaded_.strings.size()) - linesPerPage()));
        break;
    default: break;
    }
}

void StringPage::draw(Surface& s) {
    frame(s, "Strings  " + tables_[table_].name + "  (" + std::to_string(loaded_.strings.size()) + ")",
          "Left/Right: language  Up/Down/PgUp/PgDn: scroll  F: font");
    int y = 8 + font::kLineHeight * 2;
    if (!error_.empty()) {
        text(s, kMargin, y, error_, kWarnColor);
        return;
    }
    const bool game = gameFont_ && font_;
    const int lineHeight = game ? font_->lineHeight : font::kLineHeight;
    constexpr int kTextX = kMargin + 6 * font::kAdvance;
    const int end = std::min(top_ + linesPerPage(), static_cast<int>(loaded_.strings.size()));
    for (int i = top_; i < end; ++i, y += lineHeight) {
        font::drawText(s, kMargin, y + (lineHeight - font::kLineHeight) / 2, std::to_string(i), kLabelColor);
        const std::string& str = loaded_.strings[i];
        if (game) {
            // Cut at the screen's edge.
            std::string shown;
            int w = 0;
            for (char c : str) {
                w += font_->glyphs[static_cast<uint8_t>(c)].advance;
                if (kTextX + w > s.w - kMargin)
                    break;
                shown += c;
            }
            font_->draw(s, kTextX, y, shown, kTextColor);
        } else {
            text(s, kTextX, y, cp1252ToUtf8(str), kTextColor);
        }
    }
}

// --- Fonts --------------------------------------------------------------------

FontPage::FontPage(const GameFiles& files) : files_(files) {
    fonts_ = files.withExtensions({".ffn"});
    if (!fonts_.empty())
        load(0);
}

void FontPage::load(int font) {
    font_ = font;
    loaded_ = {};
    error_.clear();
    std::vector<uint8_t> data;
    if (Error err = files_.read(fonts_[font], data))
        error_ = err.message;
    else if (Error err2 = parseFont(data, loaded_))
        error_ = err2.message;
}

void FontPage::handleKey(uint32_t key, uint16_t) {
    const int fonts = static_cast<int>(fonts_.size());
    switch (key) {
    case key::Right: load((font_ + 1) % fonts); break;
    case key::Left: load((font_ + fonts - 1) % fonts); break;
    case '+':
    case '=': scale_ = std::min(scale_ + 1, 3); break;
    case '-': scale_ = std::max(scale_ - 1, 1); break;
    default: break;
    }
}

void FontPage::draw(Surface& s) {
    frame(s, "Fonts  " + std::to_string(font_ + 1) + "/" + std::to_string(fonts_.size()) + "  " + fonts_[font_].name,
          "Left/Right: font  +/-: size");
    int y = 8 + font::kLineHeight;
    if (!error_.empty()) {
        text(s, kMargin, y, error_, kWarnColor);
        return;
    }
    int glyphs = 0;
    for (const auto& g : loaded_.glyphs)
        glyphs += g.width > 0;
    text(s, kMargin, y,
         std::to_string(glyphs) + " characters  line height " + std::to_string(loaded_.lineHeight) + "  flags " +
             std::to_string(loaded_.flags) + "  atlas " + std::to_string(loaded_.atlasWidth) + "x" +
             std::to_string(loaded_.atlasHeight) + "  size " + std::to_string(scale_) + "x",
         kLabelColor);
    y += font::kLineHeight + 6;

    // Every character, 32 to a line (fewer if they don't fit), and a
    // sample, drawn at 1x then scaled up into the screen.
    const int areaW = (s.w - 2 * kMargin) / scale_, areaH = (s.h - y - font::kLineHeight - 8) / scale_;
    std::vector<std::string> lines;
    for (int perLine = 32; perLine >= 4; perLine /= 2) {
        lines.clear();
        bool fits = true;
        for (int first = 32; first < 256; first += perLine) {
            std::string line;
            for (int c = first; c < first + perLine; ++c)
                if (loaded_.has(static_cast<uint8_t>(c)))
                    line += static_cast<char>(c);
            fits &= loaded_.measure(line) <= areaW;
            lines.push_back(line);
        }
        if (fits)
            break;
    }
    lines.push_back("");
    lines.push_back("Need For Speed III: Hot Pursuit");
    lines.push_back("0123456789 km/h MPH 1st 2nd 3rd");
    Surface layer;
    layer.create(std::max(areaW, 1), std::max(areaH, 1), PixelFormat::RGBA32);
    clear(layer);
    const int lineHeight = std::max(loaded_.lineHeight, 1);
    for (size_t i = 0; i < lines.size(); ++i)
        loaded_.draw(layer, 0, static_cast<int>(i) * lineHeight, lines[i], {255, 255, 255});
    for (int dy = 0; dy < layer.h * scale_ && y + dy < s.h; ++dy) {
        const uint8_t* src = layer.row(dy / scale_);
        uint8_t* dst = s.row(y + dy) + kMargin * 4;
        for (int dx = 0; dx < layer.w * scale_; ++dx)
            std::copy_n(src + (dx / scale_) * 4, 4, dst + dx * 4);
    }
}

// --- All pages ----------------------------------------------------------------

std::vector<std::unique_ptr<Page>> makePages(const char* gameName, const GameFolders& folders,
                                             const GameFiles& files) {
    std::vector<std::unique_ptr<Page>> pages;
    pages.push_back(std::make_unique<InfoPage>(gameName, folders));
    pages.push_back(std::make_unique<TexturePage>(files));
    pages.push_back(std::make_unique<StringPage>(files));
    pages.push_back(std::make_unique<FontPage>(files));
    std::erase_if(pages, [](const std::unique_ptr<Page>& p) { return p->empty(); });
    return pages;
}

} // namespace reluva::nfs
