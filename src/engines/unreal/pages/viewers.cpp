// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's shared drawing, and its Packages, Textures and Sounds
// pages.

#include "engines/unreal/pages/viewers.h"

#include "engines/unreal/formats/properties.h"
#include "engines/unreal/formats/sound.h"
#include "engines/unreal/formats/texture.h"
#include "engines/unreal/render/procedural.h"

#include "audio/decode.h"
#include "graphics/font.h"
#include "common/fs.h"
#include "image/image.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace reluva::unreal {

// --- PackageViewer ----------------------------------------------------------------------

PackageViewer::PackageViewer(ViewContext &ctx) : Page(ctx), _names(ctx.packages.names()) {
}

void PackageViewer::handleEvent(const Event &ev) {
    if (_names.empty())
        return;
    const Package *pkg = _ctx.packages.get(_names[size_t(_index)]);
    const int exports = pkg ? int(pkg->exports().size()) : 0;
    const int page = 30;
    int sel = _selected;
    if (ev.type == EventType::kMouseWheel) {
        sel -= ev.wheelY * 3;
    } else if (ev.type != EventType::kKeyDown) {
        return;
    } else if (ev.key == key::kLeft || ev.key == key::kRight) {
        const int n = int(_names.size());
        _index = (_index + (ev.key == key::kLeft ? n - 1 : 1)) % n;
        _selected = _top = 0;
        _showProps = false;
        draw();
        return;
    } else if (ev.key == key::kUp) {
        --sel;
    } else if (ev.key == key::kDown) {
        ++sel;
    } else if (ev.key == key::kPageUp) {
        sel -= page;
    } else if (ev.key == key::kPageDown) {
        sel += page;
    } else if (ev.key == key::kHome) {
        sel = 0;
    } else if (ev.key == key::kEnd) {
        sel = exports - 1;
    } else if (ev.key == key::kReturn || ev.key == key::kKpEnter) {
        _showProps = !_showProps;
    } else {
        return;
    }
    _selected = std::clamp(sel, 0, std::max(0, exports - 1));
    draw();
}

void PackageViewer::draw() {
    clear(16, 18, 24);
    if (_names.empty())
        return;
    const std::string &name = _names[size_t(_index)];
    setTitle(name + " (" + std::to_string(_index + 1) + "/" + std::to_string(_names.size()) + ")");
    const int lh = font::kLineHeight;
    const Package *pkg = _ctx.packages.get(name);
    text(4, 4,
         name + _ctx.packages.extension(name) + "  " + std::to_string(_index + 1) + "/" +
             std::to_string(_names.size()));
    if (!pkg) {
        text(4, 4 + lh, _ctx.packages.error(name), {255, 120, 120});
        return;
    }
    char line[200];
    std::snprintf(line, sizeof line, "version %d, licensee %d, flags %08x: %zu names, %zu imports, %zu exports",
                  pkg->version(), pkg->licensee(), pkg->flags(), pkg->names().size(), pkg->imports().size(),
                  pkg->exports().size());
    text(4, 4 + lh, line, {150, 200, 255});

    const int listTop = 4 + 3 * lh;
    const int rows = (_ctx.screen.h - listTop) / lh;
    const int listRows = _showProps ? rows / 2 : rows;
    if (_selected < _top)
        _top = _selected;
    if (_selected >= _top + listRows)
        _top = _selected - listRows + 1;
    const auto &exports = pkg->exports();
    for (int i = 0; i < listRows && _top + i < int(exports.size()); ++i) {
        const int e = _top + i;
        const int y = listTop + i * lh;
        if (e == _selected)
            fill(0, y - 1, _ctx.screen.w, lh, 40, 50, 80);
        std::snprintf(line, sizeof line, "%5d %-16s %-40s %8d", e, pkg->className(e + 1).c_str(),
                      pkg->path(e + 1).c_str(), exports[size_t(e)].serialSize);
        text(4, y, line);
    }
    if (!_showProps || exports.empty())
        return;
    // The selected export's properties under the list.
    int y = listTop + listRows * lh + lh / 2;
    std::vector<uint8_t> data;
    PropertyList props;
    Error err = pkg->readExport(_selected, data);
    if (!err) {
        Reader r(data, size_t(exports[size_t(_selected)].serialOffset));
        err = beginObject(*pkg, _selected, r, props);
        if (!err) {
            std::snprintf(line, sizeof line, "%zu properties, then %zu bytes of the class's own data",
                          props.items.size(), r.remaining());
            text(4, y, line, {150, 200, 255});
        }
    }
    if (err) {
        text(4, y, err.message, {255, 120, 120});
        return;
    }
    for (const Property &p : props.items) {
        y += lh;
        if (y + lh > _ctx.screen.h)
            break;
        std::string label = p.name;
        if (p.arrayIndex)
            label += "[" + std::to_string(p.arrayIndex) + "]";
        text(4, y,
             label + " (" + (p.structName.empty() ? typeName(p.type) : p.structName) + ") = " + describe(*pkg, p));
    }
}

namespace {

// The packages of one kind (".utx"), then the maps, then the code
// packages, which can carry their own textures and sounds too.
std::vector<std::string> browsedPackages(const PackageSet &packages, const char *extension) {
    std::vector<std::string> out = packages.names(extension);
    for (std::string &n : packages.mapNames())
        out.push_back(std::move(n));
    for (std::string &n : packages.names(".u"))
        out.push_back(std::move(n));
    return out;
}

// The sound packages, then the music ones (.umx), then the rest.
std::vector<std::string> soundPackages(const PackageSet &packages) {
    std::vector<std::string> out = browsedPackages(packages, ".uax");
    std::vector<std::string> music = packages.names(".umx");
    out.insert(out.begin() + std::ptrdiff_t(packages.names(".uax").size()), music.begin(), music.end());
    return out;
}

} // namespace

// --- ObjectBrowser ----------------------------------------------------------------------

ObjectBrowser::ObjectBrowser(const PackageSet &packages, std::vector<std::string> names,
                             bool (*wanted)(std::string_view))
    : _packages(packages), _wanted(wanted), _names(std::move(names)) {
}

bool ObjectBrowser::load(int package) {
    _package = package;
    _objects.clear();
    _object = 0;
    const Package *pkg = _packages.get(_names[size_t(package)]);
    if (!pkg)
        return false;
    for (size_t i = 0; i < pkg->exports().size(); ++i)
        if (_wanted(pkg->className(int32_t(i) + 1)))
            _objects.push_back(int(i));
    return !_objects.empty();
}

bool ObjectBrowser::stepPackage(int step) {
    const int n = int(_names.size());
    if (n == 0)
        return false;
    int p = _package < 0 ? 0 : _package;
    if (_package >= 0)
        p = (p + step % n + n) % n;
    const int dir = step < 0 ? -1 : 1;
    for (int tries = 0; tries < n; ++tries, p = (p + dir + n) % n)
        if (load(p))
            return true;
    return false;
}

bool ObjectBrowser::step(int step) {
    if (_package < 0)
        return stepPackage(0);
    const int n = int(_objects.size());
    const int next = _object + step;
    if (next >= 0 && next < n) {
        _object = next;
        return true;
    }
    // Past either end: on into the next or previous package.
    if (!stepPackage(step < 0 ? -1 : 1))
        return false;
    if (step < 0)
        _object = int(_objects.size()) - 1;
    return true;
}

const Package *ObjectBrowser::package() const {
    return _package < 0 || _objects.empty() ? nullptr : _packages.get(_names[size_t(_package)]);
}

int ObjectBrowser::exportIndex() const {
    return _objects.empty() ? -1 : _objects[size_t(_object)];
}

std::string ObjectBrowser::where() const {
    if (_package < 0)
        return {};
    return std::to_string(_object + 1) + "/" + std::to_string(_objects.size()) + " in " + _names[size_t(_package)] +
           " (" + std::to_string(_package + 1) + "/" + std::to_string(_names.size()) + ")";
}

// --- TextureViewer ----------------------------------------------------------------------

TextureViewer::TextureViewer(ViewContext &ctx)
    : Page(ctx), _browser(ctx.packages, browsedPackages(ctx.packages, ".utx"), isTextureClass) {
}

void TextureViewer::activate() {
    if (!_started) {
        _started = true;
        _browser.stepPackage(0);
    }
    select();
}

void TextureViewer::handleEvent(const Event &ev) {
    if (ev.type != EventType::kKeyDown)
        return;
    if (ev.key == key::kLeft || ev.key == key::kRight)
        _browser.step(ev.key == key::kLeft ? -1 : 1);
    else if (ev.key == key::kPageUp || ev.key == key::kPageDown)
        _browser.stepPackage(ev.key == key::kPageUp ? -1 : 1);
    else if (ev.key == key::kSpace && _anim)
        _paused = !_paused;
    else
        return;
    select();
}

// Fire, water, wet, ice and FLC textures play.
void TextureViewer::update(uint64_t nowMs) {
    const float dt = _lastMs ? std::min(0.1f, float(nowMs - _lastMs) / 1000.0f) : 0.0f;
    _lastMs = nowMs;
    if (_anim && !_paused && _anim->advance(dt)) {
        _img = _anim->image();
        draw();
    }
}

// Reads the texture the browser is on, and its animation if it has one.
void TextureViewer::select() {
    _anim.reset();
    _img = {};
    _info = {};
    _error.clear();
    if (const Package *pkg = _browser.package()) {
        const int index = _browser.exportIndex();
        if (Error err = decodeTexture(_ctx.packages, *pkg, index, _img, &_info)) {
            _error = err.message;
        } else if (isAnimatedTextureClass(pkg->className(index + 1))) {
            std::string why;
            _anim = openAnimatedTexture(_ctx.packages, *pkg, index, &why);
            if (_anim)
                _img = _anim->image();
            else
                _info.standIn += " (doesn't play: " + why + ")";
        }
    }
    draw();
}

void TextureViewer::draw() {
    Surface &s = _ctx.screen;
    // Checkerboard, so transparency shows.
    for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < s.w; ++x) {
            const uint8_t v = ((x / 16 + y / 16) & 1) ? 70 : 50;
            uint8_t *p = s.row(y) + x * 4;
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }
    const int lh = font::kLineHeight;
    fill(0, 0, s.w, 3 * lh, 16, 18, 24);
    const Package *pkg = _browser.package();
    if (!pkg) {
        text(4, 4, "No textures found");
        return;
    }
    const int index = _browser.exportIndex();
    const std::string name = pkg->path(index + 1);
    setTitle(name + " (" + _browser.where() + ")");
    text(4, 4, pkg->className(index + 1) + " " + name + "  " + _browser.where());

    if (!_error.empty()) {
        text(4, 4 + lh, _error, {255, 120, 120});
        return;
    }
    const image::Image &img = _img;
    const TextureInfo &info = _info;
    // Whole multiples up to 4x when small, nearest-neighbour shrinking when large.
    const int areaW = s.w;
    const int areaH = s.h - 3 * lh;
    float scale = std::min(float(areaW) / float(img.w), float(areaH) / float(img.h));
    scale = scale >= 1 ? std::min(4.0f, float(int(scale))) : scale;
    image::Image shown;
    shown.w = std::max(1, int(float(img.w) * scale));
    shown.h = std::max(1, int(float(img.h) * scale));
    shown.rgba.resize(size_t(shown.w) * size_t(shown.h) * 4);
    for (int y = 0; y < shown.h; ++y)
        for (int x = 0; x < shown.w; ++x) {
            const int sx = std::min(img.w - 1, int(float(x) / scale));
            const int sy = std::min(img.h - 1, int(float(y) / scale));
            std::copy_n(img.pixel(sx, sy), 4, shown.pixel(x, y));
        }
    image::blit(s, shown, (areaW - shown.w) / 2, 3 * lh + (areaH - shown.h) / 2);
    char line[200];
    std::snprintf(line, sizeof line, "%dx%d %s, %d mipmaps%s%s%s", info.width, info.height, formatName(info.format),
                  info.mipCount, info.masked ? ", masked" : "", info.paletteName.empty() ? "" : ", palette ",
                  info.paletteName.c_str());
    text(4, 4 + lh, line, {150, 200, 255});
    if (_anim)
        text(4, 4 + 2 * lh,
             std::string("Stored without pixels: playing its ") + _anim->kind() + (_paused ? " (paused" : " (") +
                 ", Space pauses)",
             {255, 200, 120});
    else if (!info.standIn.empty())
        text(4, 4 + 2 * lh, "Stored without pixels: " + info.standIn, {255, 200, 120});
}

// --- SoundViewer ------------------------------------------------------------------------

namespace {
// Sounds and Music (a tracker module, rendered to play: other UE1 games').
bool isSoundClass(std::string_view c) {
    const std::string l = fs::toLower(c);
    return l == "sound" || l == "music";
}
} // namespace

SoundViewer::SoundViewer(ViewContext &ctx)
    : Page(ctx), _browser(ctx.packages, soundPackages(ctx.packages), isSoundClass) {
}

SoundViewer::~SoundViewer() {
    deactivate();
}

void SoundViewer::activate() {
    if (!_started) {
        _started = true;
        _browser.stepPackage(0);
    }
    _status.clear();
    draw();
}

void SoundViewer::deactivate() {
    if (_ctx.mixer && _playing != audio::kNoSound)
        _ctx.mixer->stop(_playing);
    _playing = audio::kNoSound;
}

void SoundViewer::handleEvent(const Event &ev) {
    if (ev.type != EventType::kKeyDown || ev.repeat)
        return;
    if (ev.key == key::kLeft || ev.key == key::kRight)
        _browser.step(ev.key == key::kLeft ? -1 : 1);
    else if (ev.key == key::kPageUp || ev.key == key::kPageDown)
        _browser.stepPackage(ev.key == key::kPageUp ? -1 : 1);
    else if (ev.key == key::kSpace || ev.key == key::kReturn) {
        play();
    } else
        return;
    if (ev.key != key::kSpace && ev.key != key::kReturn)
        _status.clear();
    draw();
}

void SoundViewer::play() {
    deactivate();
    const Package *pkg = _browser.package();
    if (!pkg)
        return;
    auto sound = std::make_shared<audio::Sound>();
    std::string from;
    if (Error err = loadSound(_ctx.packages, *pkg, _browser.exportIndex(), *sound, &from)) {
        _status = err.message;
    } else {
        char line[160];
        std::snprintf(line, sizeof line, "%s, %d Hz, %d channel%s, %.2f s", audio::formatName(sound->format),
                      sound->rate, sound->channels, sound->channels == 1 ? "" : "s",
                      sound->rate ? double(sound->frames()) / sound->rate : 0.0);
        _status = line;
        if (!from.empty())
            _status += ", streamed from " + std::filesystem::path(from).filename().string();
        // The Fallen's streamed sounds are its music.
        const auto type = from.empty() ? audio::SoundType::kEffects : audio::SoundType::kMusic;
        if (_ctx.mixer)
            _playing = _ctx.mixer->play(sound, type);
        else
            _status += " (no audio output)";
    }
}

void SoundViewer::draw() {
    clear(16, 18, 24);
    const Package *pkg = _browser.package();
    if (!pkg) {
        text(4, 4, "No sounds found");
        return;
    }
    const int lh = font::kLineHeight;
    const int index = _browser.exportIndex();
    const std::string name = pkg->path(index + 1);
    setTitle(name + " (" + _browser.where() + ")");
    text(4, 4, name + "  " + _browser.where());
    text(4, 4 + lh, std::to_string(pkg->exports()[size_t(index)].serialSize) + " bytes. Space plays it.",
         {150, 200, 255});
    if (!_status.empty())
        text(4, 4 + 3 * lh, _status);
}

} // namespace reluva::unreal
