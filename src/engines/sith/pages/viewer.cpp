// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Sith's data viewer, as reluva-viewer runs it: Tab moves between the
// pages, Esc leaves.

#include "common/events.h"
#include "common/settings.h"
#include "common/fs.h"
#include "engines/sith/formats/bitmap.h"
#include "engines/sith/pages/pages.h"
#include "engines/viewer.h"
#include "graphics/font.h"

#include <algorithm>
#include <utility>

namespace reluva {

namespace sith {

Error ViewerData::open(const std::filesystem::path &install, const std::map<std::string, std::string> &settings) {
    if (Error err = files.open(install))
        return err;
    folders = findFolders(install, settings);
    colormaps.clear();
    for (const auto &gob : files.archives())
        for (const auto &entry : gob->entries()) {
            if (!fs::toLower(entry.name).ends_with(".cmp"))
                continue;
            std::vector<uint8_t> bytes;
            Colormap cmp;
            if (gob->read(entry.name, bytes) || parseColormap(bytes, cmp))
                continue;
            colormaps.emplace_back(entry.name, std::move(cmp));
        }
    std::sort(colormaps.begin(), colormaps.end(),
              [](const auto &a, const auto &b) { return fs::toLower(a.first) < fs::toLower(b.first); });

    palettes.clear();
    const int ui = findColormap("uicolormap.cmp");
    if (ui >= 0)
        palettes.emplace_back(colormaps[size_t(ui)].first, colormaps[size_t(ui)].second.palette);
    for (size_t i = 0; i < colormaps.size(); ++i)
        if (int(i) != ui)
            palettes.emplace_back(colormaps[i].first, colormaps[i].second.palette);
    for (const FileRef &file : filesEnding(files, ".bm")) {
        std::vector<uint8_t> bytes;
        Bitmap bm;
        if (!file.first->read(file.second, bytes) && !parseBitmap(bytes, bm) && bm.palette)
            palettes.emplace_back(file.second, *bm.palette);
    }
    return Error::success();
}

int ViewerData::findColormap(std::string_view name) const {
    const std::string want = fs::toLower(name);
    for (size_t i = 0; i < colormaps.size(); ++i)
        if (fs::toLower(colormaps[i].first).ends_with(want))
            return int(i);
    return -1;
}

void fillRect(Surface &s, int x, int y, int w, int h, Color c) {
    const int x0 = std::max(x, 0);
    const int y0 = std::max(y, 0);
    const int x1 = std::min(x + w, s.w);
    const int y1 = std::min(y + h, s.h);
    for (int py = y0; py < y1; ++py) {
        uint8_t *p = s.row(py) + size_t(x0) * 4;
        for (int px = x0; px < x1; ++px, p += 4) {
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = 255;
        }
    }
}

void drawStatus(Surface &s, std::string_view line) {
    fillRect(s, 0, s.h - 14, s.w, 14, {0, 0, 0});
    font::drawText(s, 4, s.h - 12, line, {230, 230, 230});
}

std::vector<FileRef> filesEnding(const GameFiles &files, std::string_view ext) {
    std::vector<FileRef> out;
    for (const auto &gob : files.archives())
        for (const auto &e : gob->entries())
            if (fs::toLower(e.name).ends_with(ext))
                out.emplace_back(gob.get(), e.name);
    return out;
}

std::vector<std::unique_ptr<Page>> makePages(const ViewerData &data) {
    std::vector<std::unique_ptr<Page>> pages;
    pages.push_back(makePicturePage(data));
    pages.push_back(makeMaterialPage(data));
    pages.push_back(makeColormapPage(data));
    pages.push_back(makeModelPage(data));
    pages.push_back(makeSpritePage(data));
    pages.push_back(makeFontPage(data));
    pages.push_back(makeStringPage(data));
    pages.push_back(makeSoundPage(data));
    pages.push_back(makeSoundClassPage(data));
    pages.push_back(makeEpisodePage(data));
    pages.push_back(makeCutScenePage(data));
    return pages;
}

bool stepIndex(uint32_t pressed, size_t n, size_t &index) {
    if (n == 0)
        return false;
    if (pressed == key::kRight)
        index = (index + 1) % n;
    else if (pressed == key::kLeft)
        index = (index + n - 1) % n;
    else if (pressed == key::kPageDown)
        index = std::min(index + 10, n - 1);
    else if (pressed == key::kPageUp)
        index = index >= 10 ? index - 10 : 0;
    else if (pressed == key::kHome)
        index = 0;
    else if (pressed == key::kEnd)
        index = n - 1;
    else
        return false;
    return true;
}

} // namespace sith

namespace {

class SithViewer : public Engine {
public:
    SithViewer(System &system, GameTarget target) : _sys(system), _target(std::move(target)) {}

    Error run() override {
        if (Error err = _data.open(_target.path, _target.settings))
            return err;
        // One mixer for as long as the viewer runs; without an audio device
        // the sounds and cut scenes are shown and not heard.
        audio::Mixer mixer(_sys.audioRate());
        mixer.setMasterVolume(settings::volume(_target.settings, settings::kMasterVolume));
        const bool audioOut = !audio::startMixerOutput(_sys, mixer);
        struct AudioStop {
            System &sys;
            bool on;
            ~AudioStop() {
                if (on)
                    sys.stopAudio();
            }
        } audioStop{_sys, audioOut};
        if (audioOut)
            _data.mixer = &mixer;

        std::vector<std::unique_ptr<sith::Page>> pages = sith::makePages(_data);
        std::erase_if(pages, [](const auto &p) { return p->count() == 0; });
        if (pages.empty())
            return Error::fail("Nothing to show in " + _target.path.string());
        if (Error err = _sys.initGraphics(sith::kViewerW, sith::kViewerH, PixelFormat::kRGBA32))
            return err;
        Surface screen;
        screen.create(sith::kViewerW, sith::kViewerH, PixelFormat::kRGBA32);

        size_t page = 0;
        auto show = [&] {
            pages[page]->draw(screen);
            _sys.setWindowTitle(std::string("Reluva - ") + _target.description + " - " + pages[page]->name() + " (" +
                                pages[page]->keys() + ", Tab: next page)");
        };
        show();
        while (true) {
            Event ev;
            while (_sys.pollEvent(ev)) {
                if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                    return Error::success();
                if (ev.type != EventType::kKeyDown)
                    continue;
                if (ev.key == key::kEscape) {
                    pages[page]->leave();
                    return Error::success();
                }
                if (ev.key == key::kTab) {
                    pages[page]->leave();
                    page = (page + 1) % pages.size();
                    show();
                } else if (pages[page]->handleKey(ev.key))
                    show();
            }
            if (pages[page]->update(_sys.getMillis()))
                show();
            _sys.updateScreen(screen);
            _sys.delayMillis(10);
        }
    }

private:
    System &_sys;
    GameTarget _target;
    sith::ViewerData _data;
};

class SithMetaViewer : public MetaViewer {
public:
    const char *engineId() const override { return "sith"; }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<SithViewer>(system, target);
    }
};

} // namespace

const MetaViewer &sithMetaViewer() {
    static const SithMetaViewer kInstance;
    return kInstance;
}

} // namespace reluva
