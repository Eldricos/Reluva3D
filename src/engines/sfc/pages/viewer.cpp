// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Starfleet Command's data viewer, in reluva-viewer: the pictures, fonts,
// models, tables and sounds of a game's folder (pages/viewers.h has each
// page's keys). Tab moves between the pages, Esc leaves.

#include "audio/mixer.h"
#include "common/fs.h"
#include "common/settings.h"
#include "engines/sfc/pages/viewers.h"
#include "engines/viewer.h"
#include "graphics/font.h"

#include <algorithm>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace reluva::sfc {

void Page::clear(Color c) {
    fill(0, 0, _ctx.screen.w, _ctx.screen.h, c);
}

void Page::fill(int x, int y, int w, int h, Color c) {
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(_ctx.screen.w, x + w);
    const int y1 = std::min(_ctx.screen.h, y + h);
    for (int yy = y0; yy < y1; ++yy) {
        uint8_t *p = _ctx.screen.row(yy) + 4 * x0;
        for (int xx = x0; xx < x1; ++xx, p += 4) {
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = 255;
        }
    }
}

void Page::text(int x, int y, const std::string &s, Color c) {
    font::drawText(_ctx.screen, x, y, s, c);
}

void Page::setTitle(const std::string &detail) {
    _ctx.sys.setWindowTitle(
        ("Reluva - " + _ctx.title + " - " + name() + (detail.empty() ? "" : ": " + detail)).c_str());
}

std::vector<std::filesystem::path> filesWithExtension(const std::filesystem::path &dir, const std::string &ext) {
    std::vector<std::filesystem::path> out;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec) && fs::toLower(it->path().extension().string()) == ext)
            out.push_back(it->path().lexically_relative(dir));
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
        return fs::toLower(a.generic_string()) < fs::toLower(b.generic_string());
    });
    return out;
}

Error runPages(ViewerContext &ctx, std::vector<std::unique_ptr<Page>> &pages) {
    size_t page = 0;
    while (page < pages.size() && pages[page]->count() == 0)
        ++page;
    if (page == pages.size())
        return Error::fail("Nothing of Starfleet Command's to show in " + ctx.root.string());
    pages[page]->activate();
    // Leaving a 3D page: its renderer goes, the 2D screen comes back.
    const auto switchTo = [&](size_t next) -> Error {
        const bool was3D = pages[page]->uses3D();
        pages[page]->deactivate();
        if (was3D)
            if (Error err = ctx.sys.initGraphics(kScreenW, kScreenH, PixelFormat::kRGBA32))
                return err;
        page = next;
        pages[page]->activate();
        return Error::success();
    };
    struct Leave { // a 3D page's textures and meshes go before the system's renderer
        std::vector<std::unique_ptr<Page>> &pages;
        ~Leave() {
            for (auto &p : pages)
                p->deactivate();
        }
    } leave{pages};

    while (true) {
        Event ev;
        while (ctx.sys.pollEvent(ev)) {
            if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                return Error::success();
            if (ev.type == EventType::kKeyDown && ev.key == key::kEscape)
                return Error::success();
            if (ev.type == EventType::kKeyDown && ev.key == key::kTab) {
                size_t next = page;
                do
                    next = (next + 1) % pages.size();
                while (pages[next]->count() == 0);
                if (Error err = switchTo(next))
                    return err;
                continue;
            }
            pages[page]->handleEvent(ev);
        }
        pages[page]->update(ctx.sys.getMillis());
        ctx.sys.updateScreen(ctx.screen);
        // A 3D page is paced by presenting (vsync); the rest needn't spin.
        if (!pages[page]->uses3D())
            ctx.sys.delayMillis(10);
    }
}

namespace {

class SfcViewer : public Engine {
public:
    SfcViewer(System &system, GameTarget target) : _sys(system), _target(std::move(target)) {}

    Error run() override {
        if (Error err = _sys.initGraphics(kScreenW, kScreenH, PixelFormat::kRGBA32))
            return err;
        _screen.create(kScreenW, kScreenH, PixelFormat::kRGBA32);

        audio::Mixer mixer(_sys.audioRate());
        mixer.setMasterVolume(settings::volume(_target.settings, settings::kMasterVolume));
        const bool audioOut = !audio::startMixerOutput(_sys, mixer);
        struct AudioOff { // stops the output before the mixer goes
            System &sys;
            bool on;
            ~AudioOff() {
                if (on)
                    sys.stopAudio();
            }
        } audioOff{_sys, audioOut};

        ViewerContext ctx{_sys, _screen, _target.path, audioOut ? &mixer : nullptr, _target.description};
        std::vector<std::unique_ptr<Page>> pages;
        pages.push_back(std::make_unique<ImagePage>(ctx));
        pages.push_back(std::make_unique<FontPage>(ctx));
        pages.push_back(std::make_unique<ModelPage>(ctx));
        pages.push_back(std::make_unique<TablePage>(ctx));
        pages.push_back(std::make_unique<SoundPage>(ctx));
        return runPages(ctx, pages);
    }

private:
    System &_sys;
    GameTarget _target;
    Surface _screen;
};

class SfcMetaViewer : public MetaViewer {
public:
    const char *engineId() const override { return "sfc"; }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<SfcViewer>(system, target);
    }
};

} // namespace

} // namespace reluva::sfc

namespace reluva {

const MetaViewer &sfcMetaViewer() {
    static const sfc::SfcMetaViewer kInstance;
    return kInstance;
}

} // namespace reluva
