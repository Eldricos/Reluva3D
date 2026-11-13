// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The AWE Productions engine (clean-room reimplementation): Agatha
// Christie: Murder on the Orient Express. See docs/engines/awe/README.md.
//
// Everything in this engine must come from the game's data files, observed
// behaviour of the original, or documented reverse engineering of a legally
// owned copy. Never from leaked source code. See CONTRIBUTING.md.
//
// For now the engine is a data viewer (viewers.h): scenes, pictures,
// models with their animations, text and sounds. Tab moves between them;
// Esc goes back to the launcher.

#include "engines/engine.h"

#include "common/archive.h"
#include "audio/mixer.h"
#include "common/settings.h"
#include "engines/awe/pages/viewers.h"

#include <cstdio>

namespace reluva {

namespace {

// Detection table. Built with `reluva --hash`: each entry needs the size and
// the MD5 of the first kDetectionHashBytes bytes. Detection keys on the
// item list: a patch is likelier to replace the executable than the text.
const std::vector<GameVariant> kVariants = {
    {"motoe",
     "Agatha Christie: Murder on the Orient Express",
     "en",
     "windows",
     {{"motoe.exe", "cdfcba4e6c8c7cee637959bc78bd46e7", 684032},
      {"Text/itemlist.txt", "a2a0d2cd331a6fb36ffa79dced219aa5", 37328}}},
};
// Any other release: the same files, whatever their contents.
const std::vector<GameVariant> kFallback = {
    {"motoe",
     "Agatha Christie: Murder on the Orient Express (unknown release)",
     "",
     "windows",
     {{"motoe.exe", "", -1}, {"Text/itemlist.txt", "", -1}, {"granny2.dll", "", -1}}},
};

class AweEngine : public Engine {
public:
    AweEngine(System &system, GameTarget target) : _sys(system), _target(std::move(target)) {}

    Error run() override {
        if (Error err = _files.open(_target.path))
            return err;
        _screen.create(awe::kScreenW, awe::kScreenH, PixelFormat::kRGBA32);
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
        awe::GameFonts fonts;
        const bool haveFonts = !fonts.load(_files);
        awe::ViewerContext ctx{
            _sys, _files, audioOut ? &mixer : nullptr, _screen, _target.savePath, haveFonts ? &fonts : nullptr, {}, -1};
        int shots = 0;

        std::vector<std::unique_ptr<awe::Page>> pages;
        pages.push_back(std::make_unique<awe::ScenePage>(ctx));
        pages.push_back(std::make_unique<awe::ModelPage>(ctx));
        pages.push_back(std::make_unique<awe::MoviePage>(ctx));
        pages.push_back(std::make_unique<awe::PicturePage>(ctx));
        pages.push_back(std::make_unique<awe::TextPage>(ctx));
        pages.push_back(std::make_unique<awe::SoundPage>(ctx));
        size_t page = 0;
        while (page < pages.size() && pages[page]->count() == 0)
            page++;
        if (page == pages.size())
            return Error::fail("No scenes, models, pictures, text or sounds found in " + _target.path.string());
        pages[page]->activate();

        // Frames shown in the last second, after the title.
        uint64_t second = _sys.getMillis();
        int frames = 0;
        while (true) {
            if (const uint64_t now = _sys.getMillis(); now - second >= 1000) {
                ctx.fps = int(frames * 1000 / (now - second));
                ctx.setTitle(ctx.title);
                second = now, frames = 0;
            }
            frames++;
            Event ev;
            while (_sys.pollEvent(ev)) {
                if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher ||
                    (ev.type == EventType::kKeyDown && ev.key == key::kEscape && !pages[page]->wantsEscape())) {
                    pages[page]->deactivate();
                    return Error::success();
                }
                // F12: a screenshot, into the game's own folder.
                if (ev.type == EventType::kKeyDown && ev.key == key::kF1 + 11) {
                    char name[48];
                    std::snprintf(name, sizeof name, "screenshots/%s_%03d.tga", pages[page]->name(), ++shots);
                    pages[page]->requestScreenshot(_target.savePath / name);
                    continue;
                }
                if (ev.type == EventType::kKeyDown && ev.key == key::kTab) {
                    pages[page]->deactivate();
                    do
                        page = (page + 1) % pages.size();
                    while (pages[page]->count() == 0);
                    pages[page]->activate();
                    continue;
                }
                pages[page]->handleEvent(ev);
                if (pages[page]->quitRequested()) {
                    pages[page]->deactivate();
                    return Error::success();
                }
            }
            pages[page]->update(_sys.getMillis());
            // 3D pages are paced by presenting (vsync); the rest needn't spin.
            if (!pages[page]->uses3D())
                _sys.delayMillis(10);
        }
    }

private:
    System &_sys;
    GameTarget _target;
    DirectoryArchive _files;
    Surface _screen;
};

class AweMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "awe"; }
    const char *name() const override { return "AWE (Agatha Christie)"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        std::vector<DetectedGame> found = detectFromTable(id(), kVariants, dir);
        if (found.empty())
            found = detectFromTable(id(), kFallback, dir);
        return found;
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<AweEngine>(system, target);
    }
};

} // namespace

const MetaEngine &aweMetaEngine() {
    static const AweMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
