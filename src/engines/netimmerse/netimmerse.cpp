// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// NetImmerse engine, for games built on Numerical Design's NetImmerse,
// starting with LEGO Alpha Team (docs/engines/alphateam.md). Clean-room:
// everything comes from the games' data, observing them run, or study of
// owned executables; never from NetImmerse SDK source. See CONTRIBUTING.md.
//
// LEGO Alpha Team is played by running its own program in the x86
// interpreter (alphateam/player.h), after the CD's intro movie if it's
// beside the game (common/video/movie.h). F12 pauses it and opens the data viewer
// (viewers.h): the archives, models, textures, sounds and text. Tab moves
// between the viewer's pages; Esc goes back to the game. Without the
// program, the engine is the viewer alone, and Esc goes back to the
// launcher.

#include "engines/engine.h"
#include "engines/netimmerse/alphateam/player.h"
#include "engines/netimmerse/pac_archive.h"
#include "engines/netimmerse/viewers.h"
#include "common/audio/mixer.h"
#include "common/fs.h"
#include "common/video/movie.h"

#include <algorithm>
#include <cstdio>
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
        // One mixer for the game and the viewer's pages, for as long as the
        // engine runs; without an audio device nothing plays.
        mixer_ = std::make_unique<audio::Mixer>(sys_.audioRate());
        audioOut_ = !audio::startMixerOutput(sys_, *mixer_);
        struct AudioStop {
            System& sys;
            bool on;
            ~AudioStop() {
                if (on)
                    sys.stopAudio();
            }
        } audioStop{sys_, audioOut_};
        if (!fs::findFile(target_.path, "LEGOATeam.exe")) {
            bool quit = false;
            return viewer(quit);
        }
        // The CD's intro movie first, if it's been copied beside the game
        // (the installer leaves it on the CD); a key or click skips it.
        if (const auto intro = fs::findFile(target_.path, "lmiintro.avi")) {
            bool quit = false;
            if (Error err = video::playMovie(sys_, audioOut_ ? mixer_.get() : nullptr, *intro, quit))
                std::fprintf(stderr, "%s\n", err.message.c_str());
            if (quit)
                return Error::success();
        }
        return play();
    }

private:
    // The game, until it ends or the player leaves. The mouse is held by
    // the window (the game reads its movement and draws its own pointer).
    Error play() {
        netimmerse::alphateam::Player player(target_.path, target_.savePath, audioOut_ ? mixer_.get() : nullptr);
        player.start();
        playing_ = true;
        struct Stopped {
            bool& playing;
            ~Stopped() { playing = false; }
        } stopped{playing_};
        int width = 640, height = 480;
        auto screen = [&] {
            sys_.initGraphics(width, height, PixelFormat::RGBA32);
            screen_.create(width, height, PixelFormat::RGBA32);
            sys_.setMouseCursorVisible(false);
            sys_.setRelativeMouse(true);
        };
        screen();
        sys_.updateScreen(screen_); // black while it loads
        std::vector<uint32_t> pixels;
        while (true) {
            Event ev;
            while (sys_.pollEvent(ev)) {
                if ((ev.type == EventType::Quit || ev.type == EventType::ReturnToLauncher) &&
                    !confirmQuit(ev.type == EventType::ReturnToLauncher))
                    continue;
                if (ev.type == EventType::Quit || ev.type == EventType::ReturnToLauncher)
                    return Error::success();
                if (ev.type == EventType::KeyDown && ev.key == key::F1 + 11 && !ev.repeat) { // F12: the viewer
                    player.pause(true);
                    sys_.setRelativeMouse(false);
                    sys_.setMouseCursorVisible(true);
                    bool quit = false;
                    if (Error e = viewer(quit); e || quit)
                        return e;
                    screen();
                    sys_.updateScreen(screen_);
                    player.pause(false);
                    continue;
                }
                player.input(ev);
            }
            int w = 0, h = 0;
            if (player.frame(pixels, w, h)) {
                if (w != width || h != height) {
                    width = w, height = h;
                    screen();
                }
                // 0xAARRGGBB to R, G, B, A in memory.
                for (int y = 0; y < height; ++y) {
                    uint8_t* row = screen_.row(y);
                    for (int x = 0; x < width; ++x, row += 4) {
                        const uint32_t v = pixels[size_t(y) * size_t(width) + size_t(x)];
                        row[0] = uint8_t(v >> 16), row[1] = uint8_t(v >> 8), row[2] = uint8_t(v), row[3] = 255;
                    }
                }
                sys_.updateScreen(screen_);
            }
            if (const std::string out = player.takeOutput(); !out.empty())
                std::fputs(out.c_str(), stderr);
            bool ended = false;
            std::string error;
            if (player.finished(ended, error))
                return error.empty() ? Error::success() : Error::fail(target_.description + " stopped: " + error);
            sys_.delayMillis(5);
        }
    }

    // Whether to leave after the window was closed (or, `toLauncher`,
    // Ctrl+F5 was pressed): while the game runs, the player is asked, as
    // what it hasn't saved would be lost.
    bool confirmQuit(bool toLauncher) {
        return !playing_ ||
               sys_.confirmQuit(target_.description +
                                    " is still running. Anything since the game last saved (it saves on finishing a "
                                    "mission, and on leaving through its Exit Game) will be lost.\n\n" +
                                    (toLauncher ? "Return to the launcher anyway?" : "Quit anyway?"),
                                toLauncher);
    }

    // The data viewer's pages, until Esc (or the player quits: `quit`).
    Error viewer(bool& quit) {
        using namespace netimmerse;
        std::vector<std::pair<std::string, std::string>> lines;
        std::vector<std::string> errors;
        if (archives_.empty())
            openArchives(lines, errors);
        else
            for (const auto& [name, pac] : archives_)
                lines.emplace_back(name, std::to_string(pac->entries().size()) + " entries");
        if (archives_.empty())
            errors.push_back("No .pac archives could be read in " + target_.path.string());

        screen_.create(kScreenW, kScreenH, PixelFormat::RGBA32);
        ViewerContext ctx{sys_, screen_, target_.description, {}, files_, audioOut_ ? mixer_.get() : nullptr};
        for (const auto& [name, pac] : archives_)
            ctx.archives[name] = pac;
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
            while (sys_.pollEvent(ev)) {
                if ((ev.type == EventType::Quit || ev.type == EventType::ReturnToLauncher) &&
                    !confirmQuit(ev.type == EventType::ReturnToLauncher))
                    continue;
                if (ev.type == EventType::Quit || ev.type == EventType::ReturnToLauncher ||
                    (ev.type == EventType::KeyDown && (ev.key == key::Escape || ev.key == key::F1 + 11))) {
                    quit = ev.type == EventType::Quit || ev.type == EventType::ReturnToLauncher;
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
    bool playing_ = false; // the game is running (play())
    Surface screen_;
    SearchSet files_;
    std::unique_ptr<audio::Mixer> mixer_;
    bool audioOut_ = false;
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
