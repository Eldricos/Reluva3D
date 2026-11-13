// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// NetImmerse engine, for games built on Numerical Design's NetImmerse,
// starting with LEGO Alpha Team (docs/engines/netimmerse/README.md). Clean-room:
// everything comes from the games' data, observing them run, or study of
// owned executables; never from NetImmerse SDK source. See CONTRIBUTING.md.
//
// LEGO Alpha Team is played by running its own program in the x86
// interpreter (alphateam/player.h), after the CD's intro movie if it's
// beside the game (video/movie.h). F12 pauses it and opens the data viewer
// (viewers.h): the archives, models, textures, sounds and text. Tab moves
// between the viewer's pages; Esc goes back to the game. Without the
// program, the engine is the viewer alone, and Esc goes back to the
// launcher.

#include "engines/engine.h"
#include "engines/netimmerse/alphateam/player.h"
#include "engines/netimmerse/formats/pac_archive.h"
#include "engines/netimmerse/pages/viewers.h"
#include "audio/mixer.h"
#include "common/fs.h"
#include "common/settings.h"
#include "video/movie.h"

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
// exist. See docs/engines/netimmerse/games/legoalphateam/formats/install.md.
const std::vector<GameVariant> kVariants = {
    // English Windows retail CD, volume LEGOATEN (October 2026).
    {"legoalphateam",
     "LEGO Alpha Team",
     "en",
     "windows",
     {{"Locale_en.pac", "333f2476ec15acf658af87f09dcbca43", 82323720}, {"LEGOATeam.exe", "", -1}}},
};

// The same game by file names alone, for releases whose hashes aren't in
// kVariants yet (other languages, patches). Used only when nothing there
// matches.
const std::vector<GameVariant> kFallback = {
    {"legoalphateam",
     "LEGO Alpha Team (unknown release)",
     "",
     "windows",
     {{"LEGOATeam.exe", "", -1}, {"Bhvr.pac", "", -1}}},
};

class NetImmerseEngine : public Engine {
public:
    NetImmerseEngine(System &system, GameTarget target) : _sys(system), _target(std::move(target)) {}

    Error run() override {
        // One mixer for the game and the viewer's pages, for as long as the
        // engine runs; without an audio device nothing plays.
        _mixer = std::make_unique<audio::Mixer>(_sys.audioRate());
        _mixer->setMasterVolume(settings::volume(_target.settings, settings::kMasterVolume));
        _audioOut = !audio::startMixerOutput(_sys, *_mixer);
        struct AudioStop {
            System &sys;
            bool on;
            ~AudioStop() {
                if (on)
                    sys.stopAudio();
            }
        } audioStop{_sys, _audioOut};
        if (!fs::findFile(_target.path, "LEGOATeam.exe")) {
            bool quit = false;
            return viewer(quit);
        }
        // The CD's intro movie first, if it's been copied beside the game
        // (the installer leaves it on the CD); a key or click skips it.
        if (const auto intro = fs::findFile(_target.path, "lmiintro.avi")) {
            bool quit = false;
            if (Error err = video::playMovie(_sys, _audioOut ? _mixer.get() : nullptr, *intro, quit))
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
        netimmerse::alphateam::Player player(_target.path, _target.savePath, _audioOut ? _mixer.get() : nullptr);
        player.start();
        _playing = true;
        struct Stopped {
            bool &playing;
            ~Stopped() { playing = false; }
        } stopped{_playing};
        int width = 640;
        int height = 480;
        auto screen = [&] {
            _sys.initGraphics(width, height, PixelFormat::kRGBA32);
            _screen.create(width, height, PixelFormat::kRGBA32);
            _sys.setMouseCursorVisible(false);
            _sys.setRelativeMouse(true);
        };
        screen();
        _sys.updateScreen(_screen); // black while it loads
        std::vector<uint32_t> pixels;
        // The game's frames a second, in the window's title: those it
        // shows (flips), counted over each second.
        uint64_t rateFrom = _sys.getMillis();
        uint64_t rateFrames = player.framesShown();
        auto showRate = [&](const std::string &rate) {
            _sys.setWindowTitle("Reluva - " + _target.description + (rate.empty() ? "" : " - " + rate));
        };
        showRate("");
        while (true) {
            if (const uint64_t now = _sys.getMillis(); now - rateFrom >= 1000) {
                const uint64_t frames = player.framesShown();
                char rate[48];
                std::snprintf(rate, sizeof rate, "%.1f frames a second",
                              double(frames - rateFrames) * 1000.0 / double(now - rateFrom));
                showRate(frames > 0 ? rate : "");
                rateFrom = now, rateFrames = frames;
            }
            Event ev;
            while (_sys.pollEvent(ev)) {
                if ((ev.type == EventType::Quit || ev.type == EventType::ReturnToLauncher) &&
                    !confirmQuit(ev.type == EventType::ReturnToLauncher))
                    continue;
                if (ev.type == EventType::Quit || ev.type == EventType::ReturnToLauncher)
                    return Error::success();
                if (ev.type == EventType::KeyDown && ev.key == key::F1 + 11 && !ev.repeat) { // F12: the viewer
                    player.pause(true);
                    _sys.setRelativeMouse(false);
                    _sys.setMouseCursorVisible(true);
                    bool quit = false;
                    if (Error e = viewer(quit); e || quit)
                        return e;
                    screen();
                    _sys.updateScreen(_screen);
                    player.pause(false);
                    rateFrom = _sys.getMillis(), rateFrames = player.framesShown();
                    continue;
                }
                player.input(ev);
            }
            int w = 0;
            int h = 0;
            if (player.frame(pixels, w, h)) {
                if (w != width || h != height) {
                    width = w, height = h;
                    screen();
                }
                // 0xAARRGGBB to R, G, B, A in memory.
                for (int y = 0; y < height; ++y) {
                    uint8_t *row = _screen.row(y);
                    for (int x = 0; x < width; ++x, row += 4) {
                        const uint32_t v = pixels[size_t(y) * size_t(width) + size_t(x)];
                        row[0] = uint8_t(v >> 16), row[1] = uint8_t(v >> 8), row[2] = uint8_t(v), row[3] = 255;
                    }
                }
                _sys.updateScreen(_screen);
            }
            if (const std::string out = player.takeOutput(); !out.empty())
                std::fputs(out.c_str(), stderr);
            bool ended = false;
            std::string error;
            if (player.finished(ended, error))
                return error.empty() ? Error::success() : Error::fail(_target.description + " stopped: " + error);
            _sys.delayMillis(5);
        }
    }

    // Whether to leave after the window was closed (or, `toLauncher`,
    // Ctrl+F5 was pressed): while the game runs, the player is asked, as
    // what it hasn't saved would be lost.
    bool confirmQuit(bool toLauncher) {
        return !_playing ||
               _sys.confirmQuit(_target.description +
                                    " is still running. Anything since the game last saved (it saves on finishing a "
                                    "mission, and on leaving through its Exit Game) will be lost.\n\n" +
                                    (toLauncher ? "Return to the launcher anyway?" : "Quit anyway?"),
                                toLauncher);
    }

    // The data viewer's pages, until Esc (or the player quits: `quit`).
    Error viewer(bool &quit) {
        using namespace netimmerse;
        std::vector<std::pair<std::string, std::string>> lines;
        std::vector<std::string> errors;
        if (_archives.empty())
            openArchives(lines, errors);
        else
            for (const auto &[name, pac] : _archives)
                lines.emplace_back(name, std::to_string(pac->entries().size()) + " entries");
        if (_archives.empty())
            errors.push_back("No .pac archives could be read in " + _target.path.string());

        _screen.create(kScreenW, kScreenH, PixelFormat::kRGBA32);
        ViewerContext ctx{_sys, _screen, _target.description, {}, _files, _audioOut ? _mixer.get() : nullptr};
        for (const auto &[name, pac] : _archives)
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
            while (_sys.pollEvent(ev)) {
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
    bool _playing = false; // the game is running (play())
    Surface _screen;
    SearchSet _files;
    std::unique_ptr<audio::Mixer> _mixer;
    bool _audioOut = false;
    std::map<std::string, const PacArchive *> _archives; // owned by files_
};

class NetImmerseMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "netimmerse"; }
    const char *name() const override { return "NetImmerse"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        auto found = detectFromTable(id(), kVariants, dir);
        return found.empty() ? detectFromTable(id(), kFallback, dir) : found;
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<NetImmerseEngine>(system, target);
    }
};

} // namespace

const MetaEngine &netImmerseMetaEngine() {
    static const NetImmerseMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
