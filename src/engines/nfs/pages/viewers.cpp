// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/nfs/pages/viewers.h"

#include "engines/nfs/ui/play_page.h"

#include <cstdlib>
#include <cstring>

#include "common/events.h"
#include "graphics/font.h"
#include "common/fs.h"

#include <algorithm>
#include <cmath>

namespace reluva::nfs {

namespace {

using namespace page;

std::string join(const std::vector<std::string> &names) {
    std::string out;
    for (const std::string &n : names)
        out += (out.empty() ? "" : " ") + n;
    return out;
}

// The part of a path after its last '/'.
std::string baseName(const std::string &path) {
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

// --- Info ---------------------------------------------------------------------

InfoPage::InfoPage(const char *gameName, GameFolders folders) : _gameName(gameName), _folders(std::move(folders)) {
    _tracks = trackFolders(_folders.install);
    _cars = carFolders(_folders.install);
    _music = filesWithExtension(_folders.cd, "GameData/Audio/pc", ".mus");
    _movies = filesWithExtension(_folders.cd, "FeData/movies", ".mad");
}

void InfoPage::draw(Surface &s) {
    frame(s, _gameName, "");
    constexpr int kValueX = kMargin + 10 * font::kAdvance;
    const size_t width = static_cast<size_t>((s.w - kValueX - kMargin) / font::kAdvance);
    int y = 8 + font::kLineHeight * 2;
    auto line = [&](const std::string &label, const std::string &value, Color color) {
        font::drawText(s, kMargin, y, label, kLabelColor);
        for (size_t i = 0; i < value.size() || i == 0; i += width) {
            font::drawText(s, kValueX, y, value.substr(i, width), color);
            y += font::kLineHeight;
        }
    };

    line("Install", _folders.install.string(), kTextColor);
    if (_folders.cd.empty()) {
        line("CD files", "not found: no music or movies", kWarnColor);
        line("", "Set cdpath=<folder> in this game's section of reluva.ini,", kLabelColor);
        line("", "or put a copy of the CD beside the install.", kLabelColor);
    } else {
        line("CD files", _folders.cd.string(), kTextColor);
        line("", "(" + _folders.cdSource + ")", kLabelColor);
        line("Music", std::to_string(_music.size()) + " streams", kTextColor);
        line("Movies", std::to_string(_movies.size()) + " files", kTextColor);
    }
    y += font::kLineHeight;
    line("Tracks", std::to_string(_tracks.size()) + ": " + join(_tracks), kTextColor);
    line("Cars", std::to_string(_cars.size()) + ": " + join(_cars), kTextColor);
}

// --- Textures -----------------------------------------------------------------

TexturePage::TexturePage(const GameFiles &files) : _files(files) {
    _banks = files.withExtensions({".fsh", ".qfs"});
    if (!_banks.empty())
        load(0);
}

void TexturePage::load(int bank) {
    _bank = bank;
    _shape = 0;
    _loaded = {};
    _error.clear();
    std::vector<uint8_t> data;
    if (Error err = _files.read(_banks[bank], data))
        _error = err.message;
    else if (Error err2 = parseShapeBank(std::move(data), _loaded))
        _error = err2.message;
    decodeCurrent();
}

void TexturePage::decodeCurrent() {
    _image = {};
    _imageError.clear();
    if (_shape < static_cast<int>(_loaded.shapes.size()))
        if (Error err = decodeShape(_loaded.shapes[_shape], _image))
            _imageError = err.message;
}

void TexturePage::handleKey(uint32_t key, uint16_t) {
    const int banks = static_cast<int>(_banks.size());
    const int shapes = static_cast<int>(_loaded.shapes.size());
    switch (key) {
    case key::kDown:
        load((_bank + 1) % banks);
        break;
    case key::kUp:
        load((_bank + banks - 1) % banks);
        break;
    case key::kPageDown:
        load(std::min(_bank + 10, banks - 1));
        break;
    case key::kPageUp:
        load(std::max(_bank - 10, 0));
        break;
    case key::kRight:
        if (shapes) {
            _shape = (_shape + 1) % shapes;
            decodeCurrent();
        }
        break;
    case key::kLeft:
        if (shapes) {
            _shape = (_shape + shapes - 1) % shapes;
            decodeCurrent();
        }
        break;
    case key::kHome:
        _shape = 0;
        decodeCurrent();
        break;
    case key::kEnd:
        _shape = std::max(shapes - 1, 0);
        decodeCurrent();
        break;
    case 'b':
        _background = (_background + 1) % 3;
        break;
    case 'a':
        _alpha = !_alpha;
        break;
    default:
        break;
    }
}

void TexturePage::draw(Surface &s) {
    frame(s, "Textures  " + std::to_string(_bank + 1) + "/" + std::to_string(_banks.size()) + "  " + _banks[_bank].name,
          "Up/Down: file  Left/Right: picture  B: background  A: alpha");
    int y = 8 + font::kLineHeight;
    if (!_error.empty()) {
        text(s, kMargin, y, _error, kWarnColor);
        return;
    }
    if (_loaded.shapes.empty()) {
        text(s, kMargin, y, "No pictures", kLabelColor);
        return;
    }
    const Shape &shape = _loaded.shapes[_shape];
    std::string info = std::to_string(_shape + 1) + "/" + std::to_string(_loaded.shapes.size()) + "  \"" + shape.name +
                       "\"  " + shapeTypeName(shape.type) + "  " + std::to_string(shape.width) + "x" +
                       std::to_string(shape.height) + "  centre " + std::to_string(shape.centerX) + "," +
                       std::to_string(shape.centerY) + "  pos " + std::to_string(shape.x) + "," +
                       std::to_string(shape.y);
    if (!shape.palette.empty())
        info += "  palette " + std::to_string(shape.palette.size() / 4);
    text(s, kMargin, y, info, kTextColor);
    y += font::kLineHeight;
    std::string extra;
    for (const std::string &t : shape.texts)
        extra += "\"" + t + "\" ";
    for (const auto &b : shape.unknown)
        extra += "[block " + std::to_string(b.type) + ", " + std::to_string(b.data.size()) + " bytes] ";
    if (!extra.empty())
        text(s, kMargin, y, extra, kLabelColor);
    y += font::kLineHeight + 4;
    if (!_imageError.empty()) {
        text(s, kMargin, y, _imageError, kWarnColor);
        return;
    }

    // The picture, scaled to fit the space left: up to 4x larger for
    // small ones, nearest pixel.
    const int areaW = s.w - 2 * kMargin;
    const int areaH = s.h - y - font::kLineHeight - 12;
    const int w = std::max(_image.w, 1);
    const int h = std::max(_image.h, 1);
    double scale = std::min(static_cast<double>(areaW) / w, static_cast<double>(areaH) / h);
    scale = scale >= 1 ? std::min(std::floor(scale), 4.0) : scale;
    const int dw = std::max(1, static_cast<int>(w * scale));
    const int dh = std::max(1, static_cast<int>(h * scale));
    const int x0 = kMargin + (areaW - dw) / 2;
    const int y0 = y + (areaH - dh) / 2;
    for (int dy = 0; dy < dh; ++dy) {
        uint8_t *row = s.row(y0 + dy);
        const int sy = std::min(static_cast<int>(dy / scale), h - 1);
        for (int dx = 0; dx < dw; ++dx) {
            const int sx = std::min(static_cast<int>(dx / scale), w - 1);
            const uint8_t *src = _image.pixel(sx, sy);
            uint8_t *p = row + (x0 + dx) * 4;
            uint8_t bg = _background == 1 ? 0 : _background == 2 ? 255 : (((dx >> 3) ^ (dy >> 3)) & 1 ? 96 : 160);
            const unsigned a = _alpha ? src[3] : 255;
            for (int c = 0; c < 3; ++c)
                p[c] = static_cast<uint8_t>((src[c] * a + bg * (255 - a)) / 255);
            p[3] = 255;
        }
    }
}

// --- Cockpits -----------------------------------------------------------------

namespace {

// A convex quad, filled where a pixel's centre is inside all four edges.
void fillQuad(Surface &s, const Needle &n, const uint8_t color[3]) {
    const float x0 = std::min({n.x[0], n.x[1], n.x[2], n.x[3]});
    const float x1 = std::max({n.x[0], n.x[1], n.x[2], n.x[3]});
    const float y0 = std::min({n.y[0], n.y[1], n.y[2], n.y[3]});
    const float y1 = std::max({n.y[0], n.y[1], n.y[2], n.y[3]});
    for (int y = std::max(0, int(std::floor(y0))); y <= std::min(s.h - 1, int(std::ceil(y1))); ++y)
        for (int x = std::max(0, int(std::floor(x0))); x <= std::min(s.w - 1, int(std::ceil(x1))); ++x) {
            const float px = x + 0.5f;
            const float py = y + 0.5f;
            bool positive = false;
            bool negative = false;
            for (int i = 0; i < 4; ++i) {
                const int j = (i + 1) % 4;
                const float c = (n.x[j] - n.x[i]) * (py - n.y[i]) - (n.y[j] - n.y[i]) * (px - n.x[i]);
                (c > 0 ? positive : negative) |= c != 0;
            }
            if (positive && negative)
                continue;
            uint8_t *p = s.row(y) + x * 4;
            p[0] = color[0];
            p[1] = color[1];
            p[2] = color[2];
        }
}

// A picture turned clockwise by `turns` about its pivot (px, py), with the
// pivot put at (x, y) and pixels picked nearest, as the game turns the
// wheel (fsh.md).
void blitTurned(Surface &s, const image::Image &img, int px, int py, int x, int y, float turns) {
    const float a = turns * 6.28318531f;
    const float c = std::cos(a);
    const float sn = std::sin(a);
    const float reach = std::hypot(float(std::max(px, img.w - px)), float(std::max(py, img.h - py))) + 1;
    for (int dy = std::max(0, int(y - reach)); dy <= std::min(s.h - 1, int(y + reach)); ++dy)
        for (int dx = std::max(0, int(x - reach)); dx <= std::min(s.w - 1, int(x + reach)); ++dx) {
            // Back from the screen into the picture.
            const float ox = dx + 0.5f - x;
            const float oy = dy + 0.5f - y;
            const int sx = int(std::floor(px + ox * c + oy * sn));
            const int sy = int(std::floor(py - ox * sn + oy * c));
            if (sx < 0 || sy < 0 || sx >= img.w || sy >= img.h)
                continue;
            const uint8_t *src = img.pixel(sx, sy);
            uint8_t *d = s.row(dy) + dx * 4;
            const int alpha = src[3];
            for (int k = 0; k < 3; ++k)
                d[k] = static_cast<uint8_t>((src[k] * alpha + d[k] * (255 - alpha)) / 255);
        }
}
} // namespace

CockpitPage::CockpitPage(const GameFiles &files) : _files(files) {
    for (const FileRef &f : files.withExtensions({".qfs"}))
        if (fs::toLower(baseName(f.name)) == "dash.qfs")
            _dashes.push_back(f);
    if (!_dashes.empty())
        load(0);
}

void CockpitPage::load(int index) {
    _index = index;
    _error.clear();
    _dash = _wheel = {};
    _gears = {};
    _haveGauges = false;
    std::vector<uint8_t> data;
    ShapeBank bank;
    if (Error err = _files.read(_dashes[index], data)) {
        _error = err.message;
        return;
    }
    if (Error err = parseShapeBank(std::move(data), bank)) {
        _error = err.message;
        return;
    }
    // Picture 0000 is the dash, 0001 the wheel, turned about its centre
    // and placed by its position; the gear pictures all go where geaR's
    // position says.
    for (const Shape &shape : bank.shapes) {
        if (shape.name == "0000") {
            if (Error err = decodeShape(shape, _dash))
                _error = err.message;
            _haveGauges = readCockpit(shape, _cockpit);
        } else if (shape.name == "0001" && !decodeShape(shape, _wheel)) {
            _wheelX = shape.x;
            _wheelY = shape.y;
            _wheelPivotX = shape.centerX;
            _wheelPivotY = shape.centerY;
        }
        for (size_t i = 0; i < _gears.size(); ++i)
            if (shape.name == kGearPictures[i] && !decodeShape(shape, _gears[i]) && i == 0) {
                _gearX = shape.x;
                _gearY = shape.y;
            }
    }
    if (_dash.w == 0 && _error.empty())
        _error = "no picture 0000";
}

void CockpitPage::handleKey(uint32_t key, uint16_t) {
    const int count = static_cast<int>(_dashes.size());
    switch (key) {
    case key::kRight:
        load((_index + 1) % count);
        break;
    case key::kLeft:
        load((_index + count - 1) % count);
        break;
    case ' ':
        _sweeping = !_sweeping;
        break;
    case 'a':
        _sweeping = false;
        _steering = std::max(_steering - 16, -128);
        break;
    case 'd':
        _sweeping = false;
        _steering = std::min(_steering + 16, 127);
        break;
    case 'g':
        _gear = (_gear + 1) % static_cast<int>(_gears.size());
        break;
    case key::kUp:
        _sweeping = false;
        _position = std::min(_position + 0.05f, 1.0f);
        break;
    case key::kDown:
        _sweeping = false;
        _position = std::max(_position - 0.05f, 0.0f);
        break;
    default:
        break;
    }
}

bool CockpitPage::tick(uint64_t nowMs) {
    const uint64_t elapsed = _lastMs ? nowMs - _lastMs : 0;
    _lastMs = nowMs;
    if (!_sweeping)
        return false;
    // Up and back down in 4 seconds, the wheel turning to and fro.
    _position += static_cast<float>(elapsed) / 2000 * (_position < 0 ? -1 : 1);
    if (_position > 1)
        _position = -1 + (_position - 1);
    else if (_position < -1 || (_position < 0 && _position > -0.0001f))
        _position = 0;
    _steering = static_cast<int>(std::lround(127 * std::sin(_position * 3.14159265f)));
    return true;
}

void CockpitPage::draw(Surface &s) {
    const std::string keys = "Left/Right: car  Space: sweep  Up/Down: needles  A/D: steer  G: gear";
    std::string title = "Cockpits";
    if (_index >= 0)
        title += "  " + std::to_string(_index + 1) + "/" + std::to_string(_dashes.size()) + "  " + _dashes[_index].name;
    frame(s, title, keys);
    if (!_error.empty()) {
        text(s, kMargin, 8 + font::kLineHeight, _error, kWarnColor);
        return;
    }
    // The dash fills the 640x480 frame; the text goes over it.
    // The game's order: the dash, the needles, the gear, the wheel.
    image::blit(s, _dash, 0, 0);
    const float t = std::fabs(_position);
    float speed = 0;
    std::string readings = "no gauges";
    if (_haveGauges) {
        uint8_t color[3] = {255, 255, 255};
        if (_cockpit.colorX >= 0 && _cockpit.colorY >= 0 && _cockpit.colorX < _dash.w && _cockpit.colorY < _dash.h)
            std::copy_n(_dash.pixel(_cockpit.colorX, _cockpit.colorY), 3, color);
        char line[96] = "";
        for (const Gauge *g : {&_cockpit.speedometer, &_cockpit.tachometer})
            if (g->shown()) {
                const float reading = g->low + (g->high - g->low) * t;
                fillQuad(s, needle(*g, _cockpit.width, reading), color);
                if (g == &_cockpit.speedometer)
                    speed = metresPerSecond(*g, reading);
                char part[48];
                if (g == &_cockpit.speedometer)
                    std::snprintf(part, sizeof part, "%.0f %s  ", reading, g->kmh ? "km/h" : "mph");
                else
                    std::snprintf(part, sizeof part, "%.0f rpm", reading * 1000);
                std::strncat(line, part, sizeof line - std::strlen(line) - 1);
            }
        if (line[0])
            readings = line;
    }
    image::blit(s, _gears[_gear], _gearX, _gearY);
    const float turns = wheelTurns(_steering, speed);
    blitTurned(s, _wheel, _wheelPivotX, _wheelPivotY, _wheelX, _wheelY, turns);
    char wheel[64];
    std::snprintf(wheel, sizeof wheel, "  steering %d (%.0f deg)", _steering, turns * 360);
    readings += wheel;
    if (_gears[_gear].w > 0)
        readings += std::string("  gear ") + "RN123456"[_gear];
    // The title, the readings and the keys over the dash, on strips.
    fill(s, 0, 0, s.w, 2 * font::kLineHeight + 12, kBackground);
    text(s, kMargin, 8, title, kTitleColor);
    text(s, kMargin, 8 + font::kLineHeight, readings, kTextColor);
    fill(s, 0, s.h - font::kLineHeight - 8, s.w, font::kLineHeight + 8, kBackground);
    text(s, kMargin, s.h - font::kLineHeight - 4, keys + "   Tab: next page   Esc: back", kLabelColor);
}

// --- Strings ------------------------------------------------------------------

StringPage::StringPage(const GameFiles &files) : _files(files) {
    _tables = files.textTables();
    for (const FileRef &f : files.withExtensions({"arial10b.ffn"})) {
        std::vector<uint8_t> data;
        Font font;
        if (!files.read(f, data) && !parseFont(data, font)) {
            _font = std::move(font);
            break;
        }
    }
    // English first, if there is one.
    int start = 0;
    for (size_t i = 0; i < _tables.size(); ++i)
        if (fs::toLower(baseName(_tables[i].name)) == "text.eng")
            start = static_cast<int>(i);
    if (!_tables.empty())
        load(start);
}

void StringPage::load(int table) {
    _table = table;
    _top = 0;
    _loaded = {};
    _error.clear();
    std::vector<uint8_t> data;
    if (Error err = _files.read(_tables[table], data))
        _error = err.message;
    else if (Error err2 = parseTextTable(data, _loaded))
        _error = err2.message;
}

int StringPage::linesPerPage() const {
    const int lineHeight = _gameFont && _font ? _font->lineStep() : font::kLineHeight;
    return std::max(1, (480 - 3 * font::kLineHeight - 24) / lineHeight);
}

void StringPage::handleKey(uint32_t key, uint16_t) {
    const int tables = static_cast<int>(_tables.size());
    const int lastTop = std::max(0, static_cast<int>(_loaded.strings.size()) - linesPerPage());
    switch (key) {
    case key::kRight:
        load((_table + 1) % tables);
        break;
    case key::kLeft:
        load((_table + tables - 1) % tables);
        break;
    case key::kDown:
        _top = std::min(_top + 1, lastTop);
        break;
    case key::kUp:
        _top = std::max(_top - 1, 0);
        break;
    case key::kPageDown:
        _top = std::min(_top + linesPerPage(), lastTop);
        break;
    case key::kPageUp:
        _top = std::max(_top - linesPerPage(), 0);
        break;
    case key::kHome:
        _top = 0;
        break;
    case key::kEnd:
        _top = lastTop;
        break;
    case 'f':
        _gameFont = !_gameFont;
        _top = std::min(_top, std::max(0, static_cast<int>(_loaded.strings.size()) - linesPerPage()));
        break;
    default:
        break;
    }
}

void StringPage::draw(Surface &s) {
    frame(s, "Strings  " + _tables[_table].name + "  (" + std::to_string(_loaded.strings.size()) + ")",
          "Left/Right: language  Up/Down/PgUp/PgDn: scroll  F: font");
    int y = 8 + font::kLineHeight * 2;
    if (!_error.empty()) {
        text(s, kMargin, y, _error, kWarnColor);
        return;
    }
    const bool game = _gameFont && _font;
    const int lineHeight = game ? _font->lineStep() : font::kLineHeight;
    constexpr int kTextX = kMargin + 6 * font::kAdvance;
    const int end = std::min(_top + linesPerPage(), static_cast<int>(_loaded.strings.size()));
    for (int i = _top; i < end; ++i, y += lineHeight) {
        font::drawText(s, kMargin, y + (lineHeight - font::kLineHeight) / 2, std::to_string(i), kLabelColor);
        const std::string &str = _loaded.strings[i];
        if (game) {
            // Cut at the screen's edge.
            std::string shown;
            int w = 0;
            for (char c : str) {
                w += _font->glyphs[static_cast<uint8_t>(c)].advance;
                if (kTextX + w > s.w - kMargin)
                    break;
                shown += c;
            }
            _font->draw(s, kTextX, y, shown, kTextColor);
        } else {
            text(s, kTextX, y, cp1252ToUtf8(str), kTextColor);
        }
    }
}

// --- Fonts --------------------------------------------------------------------

FontPage::FontPage(const GameFiles &files) : _files(files) {
    _fonts = files.withExtensions({".ffn"});
    if (!_fonts.empty())
        load(0);
}

void FontPage::load(int font) {
    _font = font;
    _loaded = {};
    _error.clear();
    std::vector<uint8_t> data;
    if (Error err = _files.read(_fonts[font], data))
        _error = err.message;
    else if (Error err2 = parseFont(data, _loaded))
        _error = err2.message;
}

void FontPage::handleKey(uint32_t key, uint16_t) {
    const int fonts = static_cast<int>(_fonts.size());
    switch (key) {
    case key::kRight:
        load((_font + 1) % fonts);
        break;
    case key::kLeft:
        load((_font + fonts - 1) % fonts);
        break;
    case '+':
    case '=':
        _scale = std::min(_scale + 1, 3);
        break;
    case '-':
        _scale = std::max(_scale - 1, 1);
        break;
    default:
        break;
    }
}

void FontPage::draw(Surface &s) {
    frame(s, "Fonts  " + std::to_string(_font + 1) + "/" + std::to_string(_fonts.size()) + "  " + _fonts[_font].name,
          "Left/Right: font  +/-: size");
    int y = 8 + font::kLineHeight;
    if (!_error.empty()) {
        text(s, kMargin, y, _error, kWarnColor);
        return;
    }
    int glyphs = 0;
    for (const auto &g : _loaded.glyphs)
        glyphs += g.width > 0;
    text(s, kMargin, y,
         std::to_string(glyphs) + " characters  line height " + std::to_string(_loaded.lineHeight) + " + " +
             std::to_string(_loaded.lineGap) + "  pen " + std::to_string(_loaded.penX) + "," +
             std::to_string(_loaded.penY) + "  flags " + std::to_string(_loaded.flags) + "  atlas " +
             std::to_string(_loaded.atlasWidth) + "x" + std::to_string(_loaded.atlasHeight) + "  size " +
             std::to_string(_scale) + "x",
         kLabelColor);
    y += font::kLineHeight + 6;

    // Every character, 32 to a line (fewer if they don't fit), and a
    // sample, drawn at 1x then scaled up into the screen.
    const int areaW = (s.w - 2 * kMargin) / _scale;
    const int areaH = (s.h - y - font::kLineHeight - 8) / _scale;
    std::vector<std::string> lines;
    for (int perLine = 32; perLine >= 4; perLine /= 2) {
        lines.clear();
        bool fits = true;
        for (int first = 32; first < 256; first += perLine) {
            std::string line;
            for (int c = first; c < first + perLine; ++c)
                if (_loaded.has(static_cast<uint8_t>(c)))
                    line += static_cast<char>(c);
            fits &= _loaded.measure(line) <= areaW;
            lines.push_back(line);
        }
        if (fits)
            break;
    }
    lines.push_back("");
    lines.push_back("Need For Speed III: Hot Pursuit");
    lines.push_back("0123456789 km/h MPH 1st 2nd 3rd");
    Surface layer;
    layer.create(std::max(areaW, 1), std::max(areaH, 1), PixelFormat::kRGBA32);
    clear(layer);
    const int lineHeight = std::max(_loaded.lineStep(), 1);
    for (size_t i = 0; i < lines.size(); ++i)
        _loaded.draw(layer, 0, static_cast<int>(i) * lineHeight, lines[i], {255, 255, 255});
    for (int dy = 0; dy < layer.h * _scale && y + dy < s.h; ++dy) {
        const uint8_t *src = layer.row(dy / _scale);
        uint8_t *dst = s.row(y + dy) + kMargin * 4;
        for (int dx = 0; dx < layer.w * _scale; ++dx)
            std::copy_n(src + (dx / _scale) * 4, 4, dst + dx * 4);
    }
}

// --- All pages ----------------------------------------------------------------

std::vector<std::unique_ptr<Page>> makePages(const char *gameName, const GameFolders &folders, const GameFiles &files,
                                             audio::Mixer *mixer, const std::filesystem::path &saveFolder,
                                             const std::map<std::string, std::string> &settings) {
    std::vector<std::unique_ptr<Page>> pages;
    pages.push_back(std::make_unique<InfoPage>(gameName, folders));
    const auto filter = settings.find("texturefilter");
    g_pagesPointSampled = filter != settings.end() && filter->second == "point";
    auto play = makePlayPage(folders, saveFolder, mixer, settings);
    play->setKeysLine("Keys and mouse go to the game   Tab: next page   Ctrl+F5: back");
    pages.push_back(std::move(play));
    pages.push_back(std::make_unique<TrackPage>(files, folders, mixer));
    pages.push_back(std::make_unique<CarPage>(files));
    pages.push_back(std::make_unique<TexturePage>(files));
    pages.push_back(std::make_unique<CockpitPage>(files));
    pages.push_back(std::make_unique<StringPage>(files));
    pages.push_back(std::make_unique<FontPage>(files));
    pages.push_back(std::make_unique<SoundPage>(files, mixer));
    pages.push_back(std::make_unique<MusicPage>(files, mixer));
    pages.push_back(std::make_unique<MoviePage>(files, mixer));
    std::erase_if(pages, [](const std::unique_ptr<Page> &p) { return p->empty(); });
    return pages;
}

} // namespace reluva::nfs
