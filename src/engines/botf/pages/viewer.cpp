// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Birth of the Federation's data viewer, in reluva-viewer: the images of
// stbof.res and alt.res, stbof.res's fonts, the sounds of sfx.snd, the
// language's speech and music.snd, stbof.res's text and data tables, and
// its window layouts (see viewers.h for each page's keys). Tab moves
// between the pages, Esc leaves.

#include "audio/mixer.h"
#include "common/settings.h"
#include "common/zip.h"
#include "engines/botf/formats/alt_res.h"
#include "engines/botf/formats/snd_archive.h"
#include "engines/botf/game_files.h"
#include "engines/botf/pages/viewers.h"
#include "engines/viewer.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace reluva {

namespace {

using botf::AltResArchive;
using botf::DataViewer;
using botf::FontViewer;
using botf::GameFiles;
using botf::ImageViewer;
using botf::LayoutViewer;
using botf::SndArchive;
using botf::SoundViewer;
using botf::TextViewer;

class BotfViewer : public Engine {
public:
    BotfViewer(System &system, GameTarget target) : _sys(system), _target(std::move(target)) {}

    Error run() override {
        const GameFiles files(_target.path);
        if (Error err = openArchives(files))
            return err;
        if (Error err = _sys.initGraphics(botf::kScreenW, botf::kScreenH, PixelFormat::kRGBA32))
            return err;
        _screen.create(botf::kScreenW, botf::kScreenH, PixelFormat::kRGBA32);

        // One mixer for as long as the viewer runs; without an audio device
        // sounds are listed and not played.
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

        std::vector<const Archive *> imageArchives = {&_stbof};
        if (_alt)
            imageArchives.push_back(_alt.get());
        ImageViewer images(_sys, _screen, imageArchives);
        std::vector<std::pair<std::string, const SndArchive *>> soundArchives;
        for (const auto &[name, snd] : _sounds)
            soundArchives.push_back({name, snd.get()});
        SoundViewer sounds(_sys, _screen, soundArchives, audioOut ? &mixer : nullptr);
        FontViewer fonts(_sys, _screen, _stbof);
        TextViewer texts(_sys, _screen, _stbof);
        DataViewer data(_sys, _screen, _stbof);
        LayoutViewer layouts(_sys, _screen, _stbof);

        // Page 0 shows images, 1 fonts, 2 sounds, 3 text, 4 data, 5 layouts;
        // Tab moves to the next one that has something to show.
        constexpr int kPages = 6;
        const size_t counts[kPages] = {images.count(), fonts.count(), sounds.count(),
                                       texts.count(),  data.count(),  layouts.count()};
        bool any = false;
        for (const size_t c : counts)
            any = any || c > 0;
        if (!any)
            return Error::fail("Nothing to show in " + _target.path.string());
        int page = 0;
        while (counts[page] == 0)
            ++page;
        const auto activate = [&] {
            if (page == 0)
                images.activate();
            else if (page == 1)
                fonts.activate();
            else if (page == 2)
                sounds.activate();
            else if (page == 3)
                texts.activate();
            else if (page == 4)
                data.activate();
            else
                layouts.activate();
        };
        activate();
        while (true) {
            Event ev;
            while (_sys.pollEvent(ev)) {
                if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                    return Error::success();
                if (ev.type != EventType::kKeyDown)
                    continue;
                if (ev.key == key::kEscape)
                    return Error::success();
                if (ev.key == key::kTab) {
                    if (page == 2)
                        sounds.deactivate();
                    do
                        page = (page + 1) % kPages;
                    while (counts[page] == 0);
                    activate();
                    continue;
                }
                if (page == 0)
                    images.handleKey(ev.key);
                else if (page == 1)
                    fonts.handleKey(ev.key);
                else if (page == 2)
                    sounds.handleKey(ev.key);
                else if (page == 3)
                    texts.handleKey(ev.key);
                else if (page == 4)
                    data.handleKey(ev.key);
                else
                    layouts.handleKey(ev.key);
            }
            if (page == 2)
                sounds.update(_sys.getMillis());
            _sys.updateScreen(_screen);
            _sys.delayMillis(10);
        }
    }

private:
    // stbof.res is needed; alt.res and the sound archives are shown when
    // found (music.snd is often only on the CD).
    Error openArchives(const GameFiles &files) {
        const auto stbof = files.find("stbof.res");
        if (!stbof)
            return Error::fail("No stbof.res in " + _target.path.string());
        if (Error err = _stbof.open(*stbof))
            return err;
        if (const auto alt = files.find("alt.res")) {
            _alt = std::make_unique<AltResArchive>();
            if (Error err = _alt->open(*alt))
                return err;
        }
        const std::pair<std::string, bool> sndFiles[] = {
            {"sfx.snd", false}, {files.speechFile(), false}, {"music.snd", true}};
        for (const auto &[name, music] : sndFiles) {
            const auto path = files.find(name);
            if (!path)
                continue;
            auto snd = std::make_unique<SndArchive>();
            if (Error err = snd->open(*path, music))
                return err;
            _sounds.push_back({name, std::move(snd)});
        }
        return Error::success();
    }

    System &_sys;
    GameTarget _target;
    Surface _screen;
    ZipArchive _stbof;
    std::unique_ptr<AltResArchive> _alt;
    std::vector<std::pair<std::string, std::unique_ptr<SndArchive>>> _sounds;
};

class BotfMetaViewer : public MetaViewer {
public:
    const char *engineId() const override { return "botf"; }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<BotfViewer>(system, target);
    }
};

} // namespace

const MetaViewer &botfMetaViewer() {
    static const BotfMetaViewer kInstance;
    return kInstance;
}

} // namespace reluva
