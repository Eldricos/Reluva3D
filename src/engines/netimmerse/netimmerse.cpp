// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// NetImmerse engine, for games built on Numerical Design's NetImmerse,
// starting with LEGO Alpha Team (docs/engines/alphateam.md). Clean-room:
// everything comes from the games' data, observing them run, or study of
// owned executables; never from NetImmerse SDK source. See CONTRIBUTING.md.
//
// For now the engine is a data viewer (viewers.h): the archives, models,
// textures, sounds and text. Tab moves between its pages, Esc goes back to
// the launcher.

#include "engines/engine.h"
#include "engines/netimmerse/pac_archive.h"
#include "engines/netimmerse/viewers.h"
#include "common/audio/mixer.h"
#include "common/fs.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva {

namespace {

using netimmerse::PacArchive;

// Built with `reluva --hash`: each entry needs the size and the MD5 of the
// first kDetectionHashBytes bytes.
//
// Detection keys on the language's archive, which also tells the languages
// apart; the executable, which a patch would replace, is only required to
// exist. See docs/formats/alphateam/install.md.
const std::vector<GameVariant> kVariants = {
    // English Windows retail CD, volume LEGOATEN (October 2026).
    {"legoalphateam", "LEGO Alpha Team", "en", "windows",
     {{"Locale_en.pac", "333f2476ec15acf658af87f09dcbca43", 82323720}, {"LEGOATeam.exe", "", -1}}},
};

// The same game by file names alone, for releases whose hashes aren't in
// kVariants yet (other languages, patches). Used only when nothing there
// matches.
const std::vector<GameVariant> kFallback = {
    {"legoalphateam", "LEGO Alpha Team (unknown release)", "", "windows",
     {{"LEGOATeam.exe", "", -1}, {"Bhvr.pac", "", -1}}},
};

class NetImmerseEngine : public Engine {
public:
    NetImmerseEngine(System& system, GameTarget target) : sys_(system), target_(std::move(target)) {}

    Error run() override {
        using namespace netimmerse;
        std::vector<std::pair<std::string, std::string>> lines;
        std::vector<std::string> errors;
        openArchives(lines, errors);
        if (archives_.empty())
            errors.push_back("No .pac archives could be read in " + target_.path.string());

        screen_.create(kScreenW, kScreenH, PixelFormat::RGBA32);
        // One mixer for every page, for as long as the engine runs; without
        // an audio device the pages show and don't play.
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

        ViewerContext ctx{sys_, screen_, target_.description, {}, files_, audioOut ? &mixer : nullptr};
        for (const auto& [name, pac] : archives_)
            ctx.archives[name] = pac;
        std::vector<std::unique_ptr<Page>> pages;
        pages.push_back(std::make_unique<ArchivesPage>(ctx, std::move(lines), std::move(errors)));
        pages.push_back(std::make_unique<ModelPage>(ctx));
        pages.push_back(std::make_unique<TexturePage>(ctx));
        pages.push_back(std::make_unique<SoundPage>(ctx));
        pages.push_back(std::make_unique<TextPage>(ctx));

        size_t page = 0;
        pages[page]->activate();
        while (true) {
            Event ev;
            while (sys_.pollEvent(ev)) {
                if (ev.type == EventType::Quit || ev.type == EventType::ReturnToLauncher ||
                    (ev.type == EventType::KeyDown && ev.key == key::Escape)) {
                    pages[page]->deactivate();
                    return Error::success();
                }
                if (ev.type == EventType::KeyDown && ev.key == key::Tab) {
                    pages[page]->deactivate();
                    do
                        page = (page + 1) % pages.size();
                    while (pages[page]->empty());
                    pages[page]->activate();
                    continue;
                }
                pages[page]->handleEvent(ev);
            }
            pages[page]->update(sys_.getMillis());
            sys_.delayMillis(5);
        }
    }

private:
    // Every .pac in the game folder, opened and joined in one search set;
    // with what each holds, counted by extension.
    void openArchives(std::vector<std::pair<std::string, std::string>>& lines, std::vector<std::string>& errors) {
        std::vector<std::filesystem::path> paths;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(target_.path, ec), end; !ec && it != end; it.increment(ec))
            if (fs::toLower(it->path().extension().string()) == ".pac")
                paths.push_back(it->path());
        std::sort(paths.begin(), paths.end());

        for (const auto& path : paths) {
            auto pac = std::make_unique<PacArchive>();
            if (Error err = pac->open(path)) {
                errors.push_back(err.message);
                continue;
            }
            std::map<std::string, size_t> byExtension;
            for (const auto& e : pac->entries()) {
                if (e.size == 0)
                    continue; // the placeholders some archives list for themselves
                const size_t dot = e.name.rfind('.');
                ++byExtension[dot == std::string::npos ? std::string("(none)") : e.name.substr(dot + 1)];
            }
            std::string holds;
            for (const auto& [ext, count] : byExtension)
                holds += (holds.empty() ? "" : ", ") + std::to_string(count) + " " + ext;
            const std::string name = path.filename().string();
            lines.emplace_back(name, holds);
            archives_[name] = pac.get();
            files_.add(std::move(pac), 0);
        }
    }

    System& sys_;
    GameTarget target_;
    Surface screen_;
    SearchSet files_;
    std::map<std::string, const PacArchive*> archives_; // owned by files_
};

class NetImmerseMetaEngine : public MetaEngine {
public:
    const char* id() const override { return "netimmerse"; }
    const char* name() const override { return "NetImmerse"; }

    std::vector<DetectedGame> detect(const std::filesystem::path& dir) const override {
        auto found = detectFromTable(id(), kVariants, dir);
        return found.empty() ? detectFromTable(id(), kFallback, dir) : found;
    }

    std::unique_ptr<Engine> create(System& system, const GameTarget& target) const override {
        return std::make_unique<NetImmerseEngine>(system, target);
    }
};

} // namespace

const MetaEngine& netImmerseMetaEngine() {
    static const NetImmerseMetaEngine instance;
    return instance;
}

} // namespace reluva
