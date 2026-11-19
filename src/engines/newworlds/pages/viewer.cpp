// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The New Worlds data viewer, in reluva-viewer: it opens every .cat archive
// in the game folder and shows the missions, models, textures, maps,
// sounds, strings or fonts (see viewers.h for the keys). Tab switches
// between them; Esc leaves.

#include "common/settings.h"
#include "engines/viewer.h"
#include "engines/newworlds/pages/viewers.h"
#include "common/fs.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <memory>

namespace reluva {

namespace {

using newworlds::CatArchive;
using newworlds::MapViewer;
using newworlds::MissionViewer;
using newworlds::ModelViewer;
using newworlds::SoundViewer;
using newworlds::FontViewer;
using newworlds::StringViewer;
using newworlds::TextureViewer;

constexpr int kScreenW = 640;
constexpr int kScreenH = 480;

class NewWorldsViewer : public Engine {
public:
    NewWorldsViewer(System &system, GameTarget target) : _sys(system), _target(std::move(target)) {}

    Error run() override {
        if (Error err = openArchives())
            return err;
        if (Error err = _sys.initGraphics(kScreenW, kScreenH, PixelFormat::kRGBA32))
            return err;
        _screen.create(kScreenW, kScreenH, PixelFormat::kRGBA32);
        ModelViewer models(_sys, _screen, _archives);
        TextureViewer textures(_sys, _screen, _archives);
        MapViewer maps(_sys, _screen, _archives);
        const std::vector<newworlds::Level> levels = readLevels();
        maps.setLevels(levels);
        // One mixer for every page, playing for as long as the viewer runs;
        // without an audio device the pages show and don't play.
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
        audio::Mixer *const output = audioOut ? &mixer : nullptr;
        SoundViewer sounds(_sys, _screen, _archives, output, _target.path);
        StringViewer strings(_sys, _screen, _archives);
        FontViewer fonts(_sys, _screen, _archives);
        MissionViewer missions(_sys, _screen, _archives, levels, output, _target.path);
        constexpr int kViewers = 7;
        const size_t counts[kViewers] = {missions.count(), models.count(),  textures.count(), maps.count(),
                                         sounds.count(),   strings.count(), fonts.count()};
        size_t total = 0;
        for (size_t c : counts)
            total += c;
        if (total == 0)
            return Error::fail("No missions, models, textures, maps, sounds, strings or fonts found in " +
                               _target.path.string());
        // Viewer 0 shows missions, 1 models, 2 textures, 3 maps, 4 sounds, 5 strings, 6 fonts;
        // Tab moves to the next one that has something to show. They draw
        // into the screen, so it must exist before the first activate().
        int viewer = 0;
        while (counts[viewer] == 0)
            viewer = (viewer + 1) % kViewers;
        auto activate = [&] {
            if (viewer == 0)
                missions.activate();
            else if (viewer == 1)
                models.activate();
            else if (viewer == 2)
                textures.activate();
            else if (viewer == 3)
                maps.activate();
            else if (viewer == 4)
                sounds.activate();
            else if (viewer == 5)
                strings.activate();
            else
                fonts.activate();
        };
        activate();

        while (true) {
            Event ev;
            while (_sys.pollEvent(ev)) {
                if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                    return Error::success();
                // While the missions page is playing, Escape and Tab are
                // the game's.
                const bool game = viewer == 0 && missions.playing();
                if (ev.type == EventType::kKeyDown && ev.key == key::kEscape && !game)
                    return Error::success();
                if (ev.type == EventType::kKeyDown && ev.key == key::kTab && !game) {
                    if (viewer == 0)
                        missions.deactivate();
                    else if (viewer == 4)
                        sounds.deactivate();
                    do
                        viewer = (viewer + 1) % kViewers;
                    while (counts[viewer] == 0);
                    activate();
                    continue;
                }
                if (viewer == 0)
                    missions.handleEvent(ev);
                else if (viewer == 1)
                    models.handleEvent(ev);
                else if (viewer == 2 && ev.type == EventType::kKeyDown)
                    textures.handleKey(ev.key);
                else if (viewer == 3)
                    maps.handleEvent(ev);
                else if (viewer == 4 && ev.type == EventType::kKeyDown)
                    sounds.handleKey(ev.key);
                else if (viewer == 5 && ev.type == EventType::kKeyDown)
                    strings.handleKey(ev.key);
                else if (viewer == 6 && ev.type == EventType::kKeyDown)
                    fonts.handleKey(ev.key);
            }
            if (viewer == 0)
                missions.update(_sys.getMillis());
            else if (viewer == 1)
                models.update(_sys.getMillis());
            else if (viewer == 3)
                maps.update(_sys.getMillis());
            else if (viewer == 4)
                sounds.update(_sys.getMillis());
            _sys.updateScreen(_screen);
            _sys.delayMillis(10);
        }
    }

private:
    // The main menu's mission list, if the game folder has one; a missing
    // or unreadable level.dat only loses the missions' names.
    std::vector<newworlds::Level> readLevels() const {
        std::vector<newworlds::Level> levels;
        const auto path = fs::findFile(_target.path, "level.dat");
        if (!path)
            return levels;
        std::ifstream in(*path, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (newworlds::parseLevelList(text, levels))
            levels.clear();
        return levels;
    }

    Error openArchives() {
        std::vector<std::filesystem::path> paths;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(_target.path, ec), end; !ec && it != end; it.increment(ec))
            if (fs::toLower(it->path().extension().string()) == ".cat")
                paths.push_back(it->path());
        if (paths.empty())
            return Error::fail("No .cat archives in " + _target.path.string());
        std::sort(paths.begin(), paths.end());

        for (const auto &p : paths) {
            auto cat = std::make_unique<CatArchive>();
            if (Error err = cat->open(p))
                return err;
            _archives.push_back(std::move(cat));
        }
        return Error::success();
    }

    System &_sys;
    GameTarget _target;
    Surface _screen;
    newworlds::Archives _archives;
};

class NewWorldsMetaViewer : public MetaViewer {
public:
    const char *engineId() const override { return "newworlds"; }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<NewWorldsViewer>(system, target);
    }
};

} // namespace

const MetaViewer &newWorldsMetaViewer() {
    static const NewWorldsMetaViewer kInstance;
    return kInstance;
}

} // namespace reluva
