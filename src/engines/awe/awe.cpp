// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The AWE Productions engine (clean-room reimplementation): Agatha
// Christie: Murder on the Orient Express. See docs/engines/awe.md.
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
#include "common/audio/mixer.h"
#include "common/fs.h"
#include "engines/awe/viewers.h"

#include <cstdio>
#include <fstream>
#include <iterator>

namespace reluva {

namespace {

// Detection table. Built with `reluva --hash`: each entry needs the size and
// the MD5 of the first kDetectionHashBytes bytes. Detection keys on the
// item list: a patch is likelier to replace the executable than the text.
const std::vector<GameVariant> kVariants = {
    {"motoe", "Agatha Christie: Murder on the Orient Express", "en", "windows",
     {{"motoe.exe", "cdfcba4e6c8c7cee637959bc78bd46e7", 684032},
      {"Text/itemlist.txt", "a2a0d2cd331a6fb36ffa79dced219aa5", 37328}}},
};
// Any other release: the same files, whatever their contents.
const std::vector<GameVariant> kFallback = {
    {"motoe", "Agatha Christie: Murder on the Orient Express (unknown release)", "", "windows",
     {{"motoe.exe", "", -1}, {"Text/itemlist.txt", "", -1}, {"granny2.dll", "", -1}}},
};

class AweEngine : public Engine {
public:
    AweEngine(System& system, GameTarget target) : sys_(system), target_(std::move(target)) {}

    Error run() override {
        if (Error err = files_.open(target_.path))
            return err;
        // The models need the game's granny2.dll (for decompressing them);
        // without it the other pages still work.
        std::unique_ptr<awe::GrannyDll> granny;
        if (const auto dll = fs::findFile(target_.path, "granny2.dll")) {
            std::ifstream in(*dll, std::ios::binary);
            const std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            granny = std::make_unique<awe::GrannyDll>();
            if (granny->load(data))
                granny.reset();
        }
        screen_.create(awe::kScreenW, awe::kScreenH, PixelFormat::RGBA32);
        audio::Mixer mixer(sys_.audioRate());
        const bool audioOut = !audio::startMixerOutput(sys_, mixer);
        struct AudioStop {
            System& sys;
            bool on;
            ~AudioStop() {
                if (on)
                    sys.stopAudio();
            }
        } audioStop{sys_, audioOut};
        awe::GameFonts fonts;
        const bool haveFonts = !fonts.load(files_);
        awe::ViewerContext ctx{sys_,           files_, granny.get(), audioOut ? &mixer : nullptr, screen_,
                               target_.savePath, haveFonts ? &fonts : nullptr};
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
            return Error::fail("No scenes, models, pictures, text or sounds found in " + target_.path.string());
        pages[page]->activate();

        while (true) {
            Event ev;
            while (sys_.pollEvent(ev)) {
                if (ev.type == EventType::Quit || ev.type == EventType::ReturnToLauncher ||
                    (ev.type == EventType::KeyDown && ev.key == key::Escape && !pages[page]->wantsEscape())) {
                    pages[page]->deactivate();
                    return Error::success();
                }
                // F12: a screenshot, into the game's own folder.
                if (ev.type == EventType::KeyDown && ev.key == key::F1 + 11) {
                    char name[48];
                    std::snprintf(name, sizeof name, "screenshots/%s_%03d.tga", pages[page]->name(), ++shots);
                    pages[page]->requestScreenshot(target_.savePath / name);
                    continue;
                }
                if (ev.type == EventType::KeyDown && ev.key == key::Tab) {
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
            pages[page]->update(sys_.getMillis());
            sys_.delayMillis(10);
        }
    }

private:
    System& sys_;
    GameTarget target_;
    DirectoryArchive files_;
    Surface screen_;
};

class AweMetaEngine : public MetaEngine {
public:
    const char* id() const override { return "awe"; }
    const char* name() const override { return "AWE (Agatha Christie)"; }

    std::vector<DetectedGame> detect(const std::filesystem::path& dir) const override {
        std::vector<DetectedGame> found = detectFromTable(id(), kVariants, dir);
        if (found.empty())
            found = detectFromTable(id(), kFallback, dir);
        return found;
    }

    std::unique_ptr<Engine> create(System& system, const GameTarget& target) const override {
        return std::make_unique<AweEngine>(system, target);
    }
};

} // namespace

const MetaEngine& aweMetaEngine() {
    static const AweMetaEngine instance;
    return instance;
}

} // namespace reluva
