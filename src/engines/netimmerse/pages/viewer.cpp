// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The NetImmerse engine's data viewer (viewers.h), as reluva-viewer runs
// it: LEGO Alpha Team's archives, models, textures, fonts, sounds and
// text, with or without the game's program. Tab moves between the pages;
// Esc goes back to reluva-viewer's list.

#include "engines/viewer.h"
#include "engines/netimmerse/formats/pac_archive.h"
#include "engines/netimmerse/pages/viewers.h"
#include "audio/mixer.h"
#include "common/fs.h"
#include "common/settings.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva {

namespace {

using netimmerse::PacArchive;

class NetImmerseViewer : public Engine {
public:
    NetImmerseViewer(System &system, GameTarget target) : _sys(system), _target(std::move(target)) {}

    Error run() override {
        // One mixer for the pages that play sounds (sounds, models' clips);
        // without an audio device nothing plays.
        _mixer = std::make_unique<audio::Mixer>(_sys.audioRate());
        _mixer->setMasterVolume(settings::volume(_target.settings, settings::kMasterVolume));
        const bool audioOut = !audio::startMixerOutput(_sys, *_mixer);
        struct AudioStop {
            System &sys;
            bool on;
            ~AudioStop() {
                if (on)
                    sys.stopAudio();
            }
        } audioStop{_sys, audioOut};
        return pages(audioOut ? _mixer.get() : nullptr);
    }

private:
    // The pages, until Esc or the window is closed.
    Error pages(audio::Mixer *mixer) {
        using namespace netimmerse;
        std::vector<std::pair<std::string, std::string>> lines;
        std::vector<std::string> errors;
        openArchives(lines, errors);
        if (_archives.empty())
            errors.push_back("No .pac archives could be read in " + _target.path.string());

        _screen.create(kScreenW, kScreenH, PixelFormat::kRGBA32);
        ViewerContext ctx{_sys, _screen, _target.description, _archives, _files, mixer};
        std::vector<std::unique_ptr<Page>> pages;
        pages.push_back(std::make_unique<ArchivesPage>(ctx, std::move(lines), std::move(errors)));
        pages.push_back(std::make_unique<ModelPage>(ctx));
        pages.push_back(std::make_unique<TexturePage>(ctx));
        pages.push_back(std::make_unique<FontPage>(ctx));
        pages.push_back(std::make_unique<SoundPage>(ctx));
        pages.push_back(std::make_unique<TextPage>(ctx));

        size_t page = 0;
        pages[page]->activate();
        while (true) {
            Event ev;
            while (_sys.pollEvent(ev)) {
                if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher ||
                    (ev.type == EventType::kKeyDown && ev.key == key::kEscape)) {
                    pages[page]->deactivate();
                    return Error::success();
                }
                if (ev.type == EventType::kKeyDown && ev.key == key::kTab) {
                    pages[page]->deactivate();
                    do
                        page = (page + 1) % pages.size();
                    while (pages[page]->empty());
                    pages[page]->activate();
                    continue;
                }
                pages[page]->handleEvent(ev);
            }
            pages[page]->update(_sys.getMillis());
            _sys.delayMillis(5);
        }
    }

    // Every .pac in the game folder, opened and joined in one search set;
    // with what each holds, counted by extension.
    void openArchives(std::vector<std::pair<std::string, std::string>> &lines, std::vector<std::string> &errors) {
        std::vector<std::filesystem::path> paths;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(_target.path, ec), end; !ec && it != end; it.increment(ec))
            if (fs::toLower(it->path().extension().string()) == ".pac")
                paths.push_back(it->path());
        std::sort(paths.begin(), paths.end());

        for (const auto &path : paths) {
            auto pac = std::make_unique<PacArchive>();
            if (Error err = pac->open(path)) {
                errors.push_back(err.message);
                continue;
            }
            std::map<std::string, size_t> byExtension;
            for (const auto &e : pac->entries()) {
                if (e.size == 0)
                    continue; // the placeholders some archives list for themselves
                const size_t dot = e.name.rfind('.');
                ++byExtension[dot == std::string::npos ? std::string("(none)") : e.name.substr(dot + 1)];
            }
            std::string holds;
            for (const auto &[ext, count] : byExtension)
                holds += (holds.empty() ? "" : ", ") + std::to_string(count) + " " + ext;
            const std::string name = path.filename().string();
            lines.emplace_back(name, holds);
            _archives[name] = pac.get();
            _files.add(std::move(pac), 0);
        }
    }

    System &_sys;
    GameTarget _target;
    Surface _screen;
    SearchSet _files;
    std::unique_ptr<audio::Mixer> _mixer;
    std::map<std::string, const PacArchive *> _archives; // owned by _files
};

class NetImmerseMetaViewer : public MetaViewer {
public:
    const char *engineId() const override { return "netimmerse"; }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<NetImmerseViewer>(system, target);
    }
};

} // namespace

const MetaViewer &netImmerseMetaViewer() {
    static const NetImmerseMetaViewer kInstance;
    return kInstance;
}

} // namespace reluva
