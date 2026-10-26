// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/viewers.h"

#include "common/audio/decode.h"
#include "common/font.h"
#include "common/fs.h"
#include "common/text.h"
#include "engines/awe/tables.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace reluva::awe {

namespace {

bool hasExtension(const std::string& name, std::initializer_list<const char*> exts) {
    const std::string lower = fs::toLower(name);
    for (const char* e : exts)
        if (lower.size() > std::strlen(e) && lower.compare(lower.size() - std::strlen(e), std::string::npos, e) == 0)
            return true;
    return false;
}

bool startsWith(const std::string& name, const char* prefix) {
    const std::string lower = fs::toLower(name);
    return lower.compare(0, std::strlen(prefix), prefix) == 0;
}

// The game's files under `folders` with one of `exts`, sorted.
std::vector<std::string> listFiles(const Archive& files, std::initializer_list<const char*> folders,
                                   std::initializer_list<const char*> exts) {
    std::vector<std::string> out;
    for (const std::string& n : files.list()) {
        bool inFolder = folders.size() == 0;
        for (const char* f : folders)
            inFolder |= startsWith(n, f);
        if (inFolder && hasExtension(n, exts))
            out.push_back(n);
    }
    std::sort(out.begin(), out.end(),
              [](const std::string& a, const std::string& b) { return fs::toLower(a) < fs::toLower(b); });
    return out;
}

std::string shorten(const std::string& s, size_t max) {
    return s.size() <= max ? s : s.substr(0, max - 3) + "...";
}

} // namespace

// --- Page ---------------------------------------------------------------------

void Page::begin2D() {
    ctx_.sys.initGraphics(kScreenW, kScreenH, PixelFormat::RGBA32);
    ctx_.sys.setMouseCursorVisible(true);
}

void Page::clear(uint8_t r, uint8_t g, uint8_t b) {
    for (size_t i = 0; i < ctx_.screen.pixels.size(); i += 4) {
        ctx_.screen.pixels[i] = r;
        ctx_.screen.pixels[i + 1] = g;
        ctx_.screen.pixels[i + 2] = b;
        ctx_.screen.pixels[i + 3] = 255;
    }
}

void Page::fill(int x, int y, int w, int h, Color c, uint8_t alpha) {
    Surface& s = ctx_.screen;
    for (int yy = std::max(0, y); yy < std::min(s.h, y + h); yy++)
        for (int xx = std::max(0, x); xx < std::min(s.w, x + w); xx++) {
            uint8_t* p = s.row(yy) + xx * 4;
            p[0] = uint8_t((c.r * alpha + p[0] * (255 - alpha)) / 255);
            p[1] = uint8_t((c.g * alpha + p[1] * (255 - alpha)) / 255);
            p[2] = uint8_t((c.b * alpha + p[2] * (255 - alpha)) / 255);
        }
}

void Page::frame(int x, int y, int w, int h, Color c) {
    fill(x, y, w, 1, c);
    fill(x, y + h - 1, w, 1, c);
    fill(x, y, 1, h, c);
    fill(x + w - 1, y, 1, h, c);
}

int Page::text(int x, int y, const std::string& s, Color c, int scale) {
    return font::drawText(ctx_.screen, x, y, s, c, scale);
}

void Page::label(int x, int y, const std::string& s, Color c) {
    fill(x - 2, y - 1, int(font::length(s)) * font::kAdvance + 4, font::kLineHeight + 1, {0, 0, 0}, 170);
    text(x, y, s, c);
}

void Page::help(const std::string& keys) {
    fill(0, kScreenH - font::kLineHeight - 3, kScreenW, font::kLineHeight + 3, {0, 0, 0}, 200);
    text(4, kScreenH - font::kLineHeight - 1, keys + "   Tab: next page   Esc: launcher", {170, 190, 210});
}

int Page::step(uint32_t key, int index, int count) {
    if (count <= 0)
        return 0;
    int to = index;
    if (key == key::Left)
        to = index - 1;
    else if (key == key::Right)
        to = index + 1;
    else if (key == key::PageUp)
        to = index - 10;
    else if (key == key::PageDown)
        to = index + 10;
    else if (key == key::Home)
        to = 0;
    else if (key == key::End)
        to = count - 1;
    else
        return 0;
    to = ((to % count) + count) % count;
    return to - index == 0 ? 0 : to - index;
}

const image::Image& Page::picture(const std::string& name) {
    const std::string key = normalizeArchiveName(name);
    auto it = pictures_.find(key);
    if (it != pictures_.end())
        return it->second;
    if (pictures_.size() > 64)
        pictures_.clear();
    image::Image& img = pictures_[key];
    std::vector<uint8_t> data;
    if (ctx_.files.read(key, data) || image::decode(data, img))
        img = image::Image();
    return img;
}

// --- Pictures ---------------------------------------------------------------------

PicturePage::PicturePage(ViewerContext& ctx) : Page(ctx) {
    names_ = listFiles(ctx.files, {"locations/", "interface/", "inventory/", "elements/", "models/"},
                       {".jpg", ".png", ".tga", ".bmp"});
}

void PicturePage::activate() {
    begin2D();
    show();
}

void PicturePage::show() {
    clear(24, 24, 28);
    if (names_.empty())
        return;
    const std::string& name = names_[size_t(index_)];
    const image::Image& img = picture(name);
    if (img.w > 0) {
        // Scaled down (nearest) to fit, over a checkerboard that shows alpha.
        const int areaH = kScreenH - 2 * font::kLineHeight - 8;
        const float scale = std::min({1.0f, float(kScreenW) / float(img.w), float(areaH) / float(img.h)});
        const int w = std::max(1, int(float(img.w) * scale)), h = std::max(1, int(float(img.h) * scale));
        const int x0 = (kScreenW - w) / 2, y0 = font::kLineHeight + 4 + (areaH - h) / 2;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                const uint8_t* src = img.pixel(std::min(img.w - 1, int(float(x) / scale)),
                                               std::min(img.h - 1, int(float(y) / scale)));
                const uint8_t check = ((x / 8 + y / 8) & 1) ? 90 : 60;
                uint8_t* dst = ctx_.screen.row(y0 + y) + (x0 + x) * 4;
                for (int c = 0; c < 3; c++)
                    dst[c] = uint8_t((src[c] * src[3] + check * (255 - src[3])) / 255);
            }
        label(4, 2,
              name + "  " + std::to_string(img.w) + "x" + std::to_string(img.h) + "  (" + std::to_string(index_ + 1) +
                  "/" + std::to_string(names_.size()) + ")");
    } else {
        label(4, 2, name + ": can't be decoded", {255, 120, 120});
    }
    help("Left/Right: picture  PgUp/PgDn: 10  Home/End");
    present();
    ctx_.sys.setWindowTitle("Pictures: " + name);
}

void PicturePage::handleEvent(const Event& ev) {
    if (ev.type != EventType::KeyDown)
        return;
    if (const int d = step(ev.key, index_, int(names_.size()))) {
        index_ += d;
        show();
    }
}

// --- Scenes ---------------------------------------------------------------------

ScenePage::ScenePage(ViewerContext& ctx) : Page(ctx) { names_ = listFiles(ctx.files, {"locations/"}, {".ac2"}); }

void ScenePage::activate() {
    begin2D();
    load();
}

void ScenePage::load() {
    selected_ = -1;
    error_.clear();
    scene_ = Ac2Scene();
    if (names_.empty())
        return draw();
    std::vector<uint8_t> data;
    if (Error e = ctx_.files.read(names_[size_t(index_)], data))
        error_ = e.message;
    else if (Error e2 = parseAc2(data, scene_))
        error_ = e2.message;
    draw();
}

void ScenePage::draw() {
    clear(0, 0, 0);
    const std::vector<const Ac2Record*> objects = scene_.objects();
    // Pictures back to front: the original sorts them by depth, the
    // background furthest (-10000).
    std::vector<const Ac2Record*> pictures;
    for (const Ac2Record* o : objects)
        if (hasExtension(o->text("file"), {".jpg", ".png", ".tga", ".bmp"}))
            pictures.push_back(o);
    std::stable_sort(pictures.begin(), pictures.end(),
                     [](const Ac2Record* a, const Ac2Record* b) { return a->f("z") < b->f("z"); });
    for (const Ac2Record* o : pictures) {
        const image::Image& img = picture(o->text("file"));
        if (img.w > 0)
            image::blit(ctx_.screen, img, int(o->f("x")), int(o->f("y")));
    }
    if (hotspots_)
        for (const Ac2Record* o : objects) {
            const int l = o->i("left"), t = o->i("top"), r = o->i("right"), b = o->i("bottom");
            if (o->tag != "SPOT" || r <= l || b <= t)
                continue;
            frame(l, t, r - l, b - t, {255, 220, 60});
            const Ac2Record* hot = o->part("HOTS");
            label(l + 2, t + 2, hot && !hot->text("name").empty() ? hot->text("name") : o->text("script"),
                  {255, 220, 60});
        }
    if (selected_ >= 0 && size_t(selected_) < objects.size()) {
        const Ac2Record* o = objects[size_t(selected_)];
        const image::Image& img = picture(o->text("file"));
        if (img.w > 0)
            frame(int(o->f("x")), int(o->f("y")), img.w, img.h, {90, 255, 120});
        // Its fields, as the dump shows them.
        int y = kScreenH - 2 * font::kLineHeight - 6;
        std::vector<std::string> lines;
        std::string line;
        for (const auto& [name, v] : o->fields) {
            const std::string s = v.str();
            if (s == "0" || s == "\"\"" || s == "[]")
                continue;
            const std::string item = name + "=" + shorten(s, 60) + "  ";
            if (font::length(line + item) * font::kAdvance > size_t(kScreenW - 8))
                lines.push_back(line), line.clear();
            line += item;
        }
        for (const Ac2Record& p : o->parts) {
            line += p.tag + "{";
            for (const auto& [name, v] : p.fields)
                if (v.str() != "0" && v.str() != "\"\"" && v.str() != "[]")
                    line += name + "=" + shorten(v.str(), 40) + " ";
            line += "} ";
        }
        lines.push_back(line);
        y -= int(lines.size()) * font::kLineHeight;
        for (const std::string& l : lines) {
            label(4, y, l, {150, 255, 170});
            y += font::kLineHeight;
        }
    }
    if (info_) {
        std::vector<std::string> lines;
        lines.push_back((names_.empty() ? std::string("No scenes") : names_[size_t(index_)]) + "  (" +
                        std::to_string(index_ + 1) + "/" + std::to_string(names_.size()) + ")");
        if (!error_.empty())
            lines.push_back(error_);
        std::map<std::string, int> tags;
        for (const Ac2Record* o : objects)
            tags[o->tag]++;
        std::string counts = std::to_string(objects.size()) + " objects:";
        for (const auto& [t, n] : tags)
            counts += " " + std::to_string(n) + " " + t;
        lines.push_back(counts);
        if (const Ac2Record* cam = scene_.camera()) {
            char buf[160];
            std::snprintf(buf, sizeof buf, "camera at (%g, %g, %g), turned (%g, %g, %g), fov %.1f deg", cam->f("x"),
                          cam->f("y"), cam->f("z"), cam->f("pitch"), cam->f("yaw"), cam->f("roll"),
                          cam->f("fov") * 57.29578f);
            lines.push_back(buf);
        }
        for (const Ac2Record& r : scene_.records)
            if (r.tag == "MUSC" || r.tag == "EVNT")
                if (const Ac2Value* v = r.fields.empty() ? nullptr : &r.fields[0].second; v && !v->pairs.empty())
                    lines.push_back(r.tag + " " + shorten(v->str(), 150));
        for (const Ac2Record* o : objects)
            if (o->tag == "NPCX" || o->tag == "PCXX") {
                char buf[200];
                std::snprintf(buf, sizeof buf, "%s %s at (%g, %g, %g), facing %g", o->tag.c_str(),
                              o->text("file").c_str(), o->f("x"), o->f("y"), o->f("z"), o->f("rotY"));
                lines.push_back(buf);
            }
        int y = 2;
        for (const std::string& l : lines) {
            label(4, y, shorten(l, 168), l == error_ ? Color{255, 120, 120} : Color{230, 230, 230});
            y += font::kLineHeight;
        }
    }
    help("Left/Right: scene  Up/Down: object  H: hotspots  I: info");
    present();
    ctx_.sys.setWindowTitle("Scenes: " + (names_.empty() ? std::string() : names_[size_t(index_)]));
}

void ScenePage::handleEvent(const Event& ev) {
    if (ev.type != EventType::KeyDown)
        return;
    if (const int d = step(ev.key, index_, int(names_.size()))) {
        index_ += d;
        load();
        return;
    }
    const int n = int(scene_.objects().size());
    if (ev.key == key::Up || ev.key == key::Down) {
        if (n > 0)
            selected_ = ev.key == key::Down ? (selected_ + 1 >= n ? -1 : selected_ + 1)
                                            : (selected_ < 0 ? n - 1 : selected_ - 1);
        draw();
    } else if (ev.key == 'h') {
        hotspots_ = !hotspots_;
        draw();
    } else if (ev.key == 'i') {
        info_ = !info_;
        draw();
    }
}

// --- Text ---------------------------------------------------------------------

TextPage::TextPage(ViewerContext& ctx) : Page(ctx) { names_ = listFiles(ctx.files, {"text/"}, {".txt"}); }

void TextPage::activate() {
    begin2D();
    load();
}

void TextPage::load() {
    scroll_ = 0;
    lines_.clear();
    if (names_.empty())
        return draw();
    std::vector<uint8_t> data;
    if (Error e = ctx_.files.read(names_[size_t(index_)], data)) {
        lines_.push_back(e.message);
        return draw();
    }
    const std::string text = decodeText(data);
    const size_t width = size_t(kScreenW - 8) / font::kAdvance;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        // A table's empty rows are only tabs.
        if (line.find_first_not_of("\t ") == std::string::npos && !line.empty()) {
            start = end + 1;
            continue;
        }
        for (size_t i; (i = line.find('\t')) != std::string::npos;)
            line.replace(i, 1, " | ");
        // Wrapped to the screen's width.
        do {
            size_t cut = line.size();
            if (font::length(line) > width) {
                cut = 0;
                for (size_t chars = 0, i = 0; i < line.size() && chars < width; chars++)
                    text::nextUtf8(line, i), cut = i;
            }
            lines_.push_back(line.substr(0, cut));
            line = cut < line.size() ? "    " + line.substr(cut) : std::string();
        } while (!line.empty());
        start = end + 1;
    }
    draw();
}

void TextPage::draw() {
    clear(20, 22, 30);
    const int rows = (kScreenH - 2 * font::kLineHeight - 8) / font::kLineHeight;
    label(4, 2,
          (names_.empty() ? std::string("No text files") : names_[size_t(index_)]) + "  (" +
              std::to_string(index_ + 1) + "/" + std::to_string(names_.size()) + "), lines " +
              std::to_string(scroll_ + 1) + "-" + std::to_string(std::min(int(lines_.size()), scroll_ + rows)) +
              " of " + std::to_string(lines_.size()),
          {255, 230, 150});
    for (int i = 0; i < rows && size_t(scroll_ + i) < lines_.size(); i++)
        text(4, font::kLineHeight + 6 + i * font::kLineHeight, lines_[size_t(scroll_ + i)]);
    help("Left/Right: file  Up/Down, PgUp/PgDn (with Shift: file by 10): scroll");
    present();
    ctx_.sys.setWindowTitle("Text: " + (names_.empty() ? std::string() : names_[size_t(index_)]));
}

void TextPage::handleEvent(const Event& ev) {
    if (ev.type != EventType::KeyDown)
        return;
    const int rows = (kScreenH - 2 * font::kLineHeight - 8) / font::kLineHeight;
    const int maxScroll = std::max(0, int(lines_.size()) - rows);
    const bool shift = ev.mods & keymod::Shift;
    if (ev.key == key::Left || ev.key == key::Right || ((ev.key == key::PageUp || ev.key == key::PageDown) && shift)) {
        if (const int d = step(ev.key, index_, int(names_.size()))) {
            index_ += d;
            load();
        }
    } else if (ev.key == key::Up || ev.key == key::Down || ev.key == key::PageUp || ev.key == key::PageDown ||
               ev.key == key::Home || ev.key == key::End) {
        const int by = ev.key == key::Up ? -1 : ev.key == key::Down ? 1 : ev.key == key::PageUp ? -rows
                     : ev.key == key::PageDown ? rows : ev.key == key::Home ? -int(lines_.size()) : int(lines_.size());
        scroll_ = std::clamp(scroll_ + by, 0, maxScroll);
        draw();
    }
}

// --- Sounds ---------------------------------------------------------------------

SoundPage::SoundPage(ViewerContext& ctx) : Page(ctx) {
    names_ = listFiles(ctx.files, {"sound/", "models/"}, {".wav", ".mp3"});
}

void SoundPage::activate() {
    begin2D();
    draw();
}

void SoundPage::deactivate() {
    if (ctx_.mixer)
        ctx_.mixer->stopAll();
    playing_ = audio::kNoSound;
}

void SoundPage::play() {
    info_.clear();
    std::vector<uint8_t> data;
    auto sound = std::make_shared<audio::Sound>();
    if (Error e = ctx_.files.read(names_[size_t(index_)], data)) {
        info_ = e.message;
    } else if (Error e2 = audio::decodeSound(data, *sound)) {
        info_ = e2.message;
    } else {
        char buf[128];
        std::snprintf(buf, sizeof buf, "%s, %d Hz, %d channel%s, %.2f s", audio::formatName(sound->format),
                      sound->rate, sound->channels, sound->channels == 1 ? "" : "s",
                      sound->rate ? double(sound->frames()) / sound->rate : 0.0);
        info_ = buf;
        if (ctx_.mixer) {
            ctx_.mixer->stopAll();
            playing_ = ctx_.mixer->play(sound, audio::SoundType::Effects);
        } else {
            info_ += " (no audio device)";
        }
    }
    draw();
}

void SoundPage::draw() {
    clear(18, 24, 22);
    const int rows = (kScreenH - 4 * font::kLineHeight - 8) / font::kLineHeight;
    const int first = std::clamp(index_ - rows / 2, 0, std::max(0, int(names_.size()) - rows));
    label(4, 2, std::to_string(names_.size()) + " sounds" + (info_.empty() ? "" : ":  " + info_), {255, 230, 150});
    for (int i = 0; i < rows && first + i < int(names_.size()); i++) {
        const bool current = first + i == index_;
        const int y = 2 * font::kLineHeight + 4 + i * font::kLineHeight;
        if (current)
            fill(0, y - 1, kScreenW, font::kLineHeight, {60, 80, 70});
        text(8, y, names_[size_t(first + i)], current ? Color{255, 255, 255} : Color{190, 200, 195});
    }
    help("Up/Down (Left/Right): sound  PgUp/PgDn: 10  Space/Enter: play  S: stop");
    present();
    ctx_.sys.setWindowTitle("Sounds");
}

void SoundPage::handleEvent(const Event& ev) {
    if (ev.type != EventType::KeyDown || names_.empty())
        return;
    const uint32_t k = ev.key == key::Up ? key::Left : ev.key == key::Down ? key::Right : ev.key;
    if (const int d = step(k, index_, int(names_.size()))) {
        index_ += d;
        info_.clear();
        draw();
    } else if (ev.key == key::Space || ev.key == key::Return) {
        play();
    } else if (ev.key == 's' && ctx_.mixer) {
        ctx_.mixer->stopAll();
    }
}

} // namespace reluva::awe
