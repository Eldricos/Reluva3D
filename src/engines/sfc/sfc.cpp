// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Starfleet Command: the engine's MetaEngine (its name, the releases it recognises,
// making an Engine to run one) and its Engine, which runs the game's own
// program.

#include "audio/mixer.h"
#include "common/detection.h"
#include "common/fs.h"
#include "common/installshield.h"
#include "common/settings.h"
#include "engines/engine.h"
#include "common/stream.h"
#include "engines/sfc/formats/intro_movie.h"
#include "engines/sfc/host/game_process.h"
#include "engines/sfc/host/player.h"
#include "video/movie.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace reluva::sfc {

namespace {

// The releases it recognises: files that must be there, with the MD5 of
// their start and their size, as `reluva --hash <files...>` prints them.
// A folder matching every file of a variant is that release.
// The 2.0.3.6 patch rewrites both files, so a patched install is an unknown
// release until its own are hashed.
const std::vector<GameVariant> kVariants = {
    {"sfc2",
     "Star Trek: Starfleet Command II (1.0)",
     "en",
     "windows",
     {{"StarFleet2.exe", "e78f2d2c9c9c51f2077bb5c10cbaf617", 4149248},
      {"Assets/Sprites/sprites.q3", "7eaeae9272f5d82ac6523a3d6191a45a", 78253026}}},
};

// The same game by its files' names alone, for releases not in kVariants:
// used only when nothing there matches.
const std::vector<GameVariant> kFallback = {
    {"sfc2",
     "Star Trek: Starfleet Command II (unknown release)",
     "",
     "windows",
     {{"StarFleet2.exe", "", -1}, {"Assets/Sprites/sprites.q3", "", -1}}},
};

// The file groups the CDs' setup installs (all but its script kit's,
// which Reluva doesn't use), and readme.txt, loose on the first CD.
const std::vector<std::string> kSetupGroups = {
    "Program Executable Files", "Music", "Voices", "Intro Movie", "Install Files", "Ereg", "Extras", "ReadMe"};

/**
 * The game running: its own StarFleet2.exe in the x86 emulator (Player),
 * its frames in the window, its sound to the mixer, the mouse and keys
 * passed to it.
 */
class SfcEngine : public Engine {
public:
    SfcEngine(System &system, const GameTarget &target) : _sys(system), _target(target) {}

    Error run() override {
        if (!fs::findFile(_target.path, GameProcess::kProgram))
            return Error::fail(std::string("Starfleet Command II's program, ") + GameProcess::kProgram + ", isn't in " +
                               _target.path.string());
        // Its sound, through Miles, to the mixer; without an audio device
        // nothing plays.
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
        // The opening movie first, inside SFC2Intro.exe, as the original's
        // launcher plays it on each start; a key or click skips it.
        if (const auto intro = fs::findFile(_target.path, "SFC2Intro.exe")) {
            std::vector<uint8_t> program;
            std::vector<uint8_t> movie;
            bool quit = false;
            if (!readFile(*intro, program) || introMovie(program, movie))
                std::fprintf(stderr, "%s holds no movie Reluva reads\n", intro->string().c_str());
            else if (Error err = video::playMovie(_sys, audioOut ? _mixer.get() : nullptr, std::move(movie),
                                                  "SFC2Intro.exe", quit))
                std::fprintf(stderr, "%s\n", err.message.c_str());
            if (quit)
                return Error::success();
        }
        Player player(_target.path, _target.savePath, audioOut ? _mixer.get() : nullptr);
        player.setNetwork(_sys.sockets(), _sys.broadcastAddresses());
        player.start();
        int width = 800; // the game's own resolution, until its first frame says
        int height = 600;
        // The game draws its own pointer, where the mouse is on its screen.
        auto screen = [&] {
            _sys.initGraphics(width, height, PixelFormat::kRGBA32);
            _screen.create(width, height, PixelFormat::kRGBA32);
            _sys.setMouseCursorVisible(false);
        };
        screen();
        _sys.setWindowTitle("Reluva - " + _target.description);
        _sys.updateScreen(_screen); // black while it loads
        std::vector<uint32_t> pixels;
        while (true) {
            Event ev;
            while (_sys.pollEvent(ev)) {
                if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher) {
                    if (confirmQuit(ev.type == EventType::kReturnToLauncher))
                        return Error::success();
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

private:
    // Whether to leave after the window was closed (or, `toLauncher`,
    // Ctrl+F5 was pressed), as what the game hasn't saved is lost.
    bool confirmQuit(bool toLauncher) {
        return _sys.confirmQuit(_target.description + " is still running. Anything it hasn't saved will be lost.\n\n" +
                                    (toLauncher ? "Return to the launcher anyway?" : "Quit anyway?"),
                                toLauncher);
    }

    System &_sys;
    GameTarget _target;
    Surface _screen;
    std::unique_ptr<audio::Mixer> _mixer;
};

/**
 * The engine as the launcher sees it: its name, the releases it
 * recognises, and making a SfcEngine to run one.
 */
class SfcMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "sfc"; }
    const char *name() const override { return "Starfleet Command"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        auto found = detectFromTable(id(), kVariants, dir);
        if (found.empty())
            found = detectFromTable(id(), kFallback, dir);
        return found;
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<SfcEngine>(system, target);
    }

    // From the first CD (data1.hdr and data1.cab): planned once the second
    // (data2.cab) is added too, as the voices and the opening movie are on it.
    std::vector<ImportPlan> planImport(const std::filesystem::path &source) const override {
        const auto header = fs::findFile(source, "data1.hdr");
        if (!header)
            return {};
        auto cabinet = std::make_shared<InstallShieldCabinet>();
        if (readInstallShieldCabinet(*header, *cabinet))
            return {};
        const std::vector<InstallShieldGroup> groups = installShieldGroups(*cabinet, kSetupGroups);
        if (groups.empty())
            return {};
        const GameVariant *variant = nullptr;
        for (const auto *table : {&kVariants, &kFallback})
            for (const GameVariant &v : *table)
                if (!variant && installShieldMatches(v, *cabinet, groups))
                    variant = &v;
        if (!variant)
            return {};
        ImportPlan plan;
        plan.game = {id(), variant->gameId, variant->description, variant->language, variant->platform};
        planInstallShield(plan, cabinet, groups);
        plan.addPatchLabel.clear();
        plan.addFolderLabel = "Add other disc...";
        plan.addFolderTooltip = "Copy the game's second CD too (or a copy of it)";
        return {plan};
    }

    bool addToImport(ImportPlan &plan, const std::filesystem::path &folder, std::string &message) const override {
        if (!plan.cabinet) {
            message = "not a file Reluva can use for this game";
            return false;
        }
        return addInstallShieldVolumes(plan, folder, installShieldGroups(*plan.cabinet, kSetupGroups), message);
    }
};

} // namespace

} // namespace reluva::sfc

namespace reluva {

const MetaEngine &sfcMetaEngine() {
    static const sfc::SfcMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
