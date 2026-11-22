// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Island: the engine's MetaEngine (its name, the releases it recognises,
// making an Engine to run one) and its Engine, which plays the game's own
// program in the x86 interpreter (docs/engines/legoisland/systems/running.md).

#include "audio/mixer.h"
#include "common/fs.h"
#include "common/settings.h"
#include "engines/engine.h"
#include "engines/legoisland/detection.h"
#include "engines/legoisland/host/player.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace reluva::legoisland {

namespace {

// Frames a second, unless the settings say otherwise: about 180 degrees a
// second turning, as the game turns 9 a frame (inferred: what it was
// made for isn't known).
constexpr int kDefaultFrameRate = 20;

/**
 * The game running: its program played (Player), its frames shown, the
 * player's input passed on, until it ends or the player leaves.
 */
class LegoislandEngine : public Engine {
public:
    LegoislandEngine(System &system, const GameTarget &target) : _sys(system), _target(target) {}

    Error run() override {
        if (!fs::findFile(_target.path, "ISLE.EXE") || !fs::findFile(_target.path, "LEGO1.DLL"))
            return Error::fail("LEGO Island's program, ISLE.EXE and LEGO1.DLL, isn't in " + _target.path.string());
        // Without an audio device nothing is heard; the game still plays.
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
        return play();
    }

private:
    Error play() {
        Player player(_target.path, _target.savePath, _audioOut ? _mixer.get() : nullptr);
        const auto rate = _target.settings.find("framerate");
        player.setFrameRate(rate == _target.settings.end() ? kDefaultFrameRate : std::atoi(rate->second.c_str()));
        // The setup's choices, each where it's set; unset, the program's
        // own default (but for the island's model, GameProcess's).
        auto set = [&](const char *key) -> const std::string * {
            const auto it = _target.settings.find(key);
            return it == _target.settings.end() ? nullptr : &it->second;
        };
        if (const std::string *v = set("islandquality"))
            player.setSetting("Island Quality", *v);
        if (const std::string *v = set("islandtexture"))
            player.setSetting("Island Texture", *v);
        for (const auto &[key, name] :
             {std::pair{"music", "Music"}, {"sound3d", "3DSound"}, {"drawcursor", "Draw Cursor"}})
            if (const std::string *v = set(key))
                player.setSetting(name, *v != "0" ? "YES" : "NO");
        // A joystick, if asked for: the first one connected, as joystick 0.
        if (const std::string *v = set("usejoystick"); v && *v != "0") {
            player.setSetting("UseJoystick", "YES");
            player.setSetting("JoystickIndex", "0");
            player.setJoystick(true);
        }
        player.start();
        int width = 640;
        int height = 480;
        auto screen = [&] {
            _sys.initGraphics(width, height, PixelFormat::kRGBA32);
            _screen.create(width, height, PixelFormat::kRGBA32);
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
                    width = w;
                    height = h;
                    screen();
                }
                // 0xAARRGGBB to R, G, B, A in memory.
                for (int y = 0; y < height; ++y) {
                    uint8_t *row = _screen.row(y);
                    for (int x = 0; x < width; ++x, row += 4) {
                        const uint32_t v = pixels[size_t(y) * size_t(width) + size_t(x)];
                        row[0] = uint8_t(v >> 16);
                        row[1] = uint8_t(v >> 8);
                        row[2] = uint8_t(v);
                        row[3] = 255;
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

    // Whether to leave after the window was closed (or, `toLauncher`, the
    // launcher was asked for): the player is asked, as what the game hasn't
    // saved would be lost.
    bool confirmQuit(bool toLauncher) {
        return _sys.confirmQuit(_target.description +
                                    " is still running. What the game hasn't saved will be lost.\n\n" +
                                    (toLauncher ? "Return to the launcher anyway?" : "Quit anyway?"),
                                toLauncher);
    }

    System &_sys;
    GameTarget _target;
    Surface _screen;
    std::unique_ptr<audio::Mixer> _mixer;
    bool _audioOut = false;
};

/**
 * The engine as the launcher sees it: its name, the releases it
 * recognises, and making a LegoislandEngine to run one.
 */
class LegoislandMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "legoisland"; }
    const char *name() const override { return "LEGO Island"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override { return detectGame(id(), dir); }

    // The game turns the player a set amount each frame it shows, however
    // long the frame took (systems/running.md): its frame rate is how fast
    // it turns. The first choice is the default.
    std::vector<GameOption> engineOptions() const override {
        GameOption o;
        o.key = "framerate";
        o.label = "Frame rate";
        o.tooltip = "At most this many frames a second. The game turns the player a set amount each frame, so "
                    "the more frames, the faster it turns, too fast on today's machines.";
        o.choices = {{std::to_string(kDefaultFrameRate), "20 a second"},
                     {"15", "15 a second"},
                     {"30", "30 a second"},
                     {"60", "60 a second"},
                     {"0", "As many as it draws"}};
        // The setup's (CONFIG.EXE's) choices that matter here; its video
        // memory, palette and device ones are the stand-ins' to decide.
        auto choice = [](const char *key, const char *label, const char *tooltip,
                         std::vector<std::pair<std::string, std::string>> choices) {
            GameOption c;
            c.key = key;
            c.label = label;
            c.tooltip = tooltip;
            c.choices = std::move(choices);
            return c;
        };
        auto flag = [](const char *key, const char *label, const char *tooltip, bool on) {
            GameOption c;
            c.key = key;
            c.label = label;
            c.tooltip = tooltip;
            c.defaultOn = on;
            return c;
        };
        return {o,
                choice("islandquality", "Island model quality",
                       "The setup's Island Model Quality: High draws the island's detailed model, its roads marked.",
                       {{"2", "High"}, {"1", "Medium"}, {"0", "Fast"}}),
                choice("islandtexture", "Island texture quality", "The setup's Island Texture Quality.",
                       {{"1", "High"}, {"0", "Fast"}}),
                flag("music", "Music", "The game's music.", true),
                flag("sound3d", "3D sound", "Sounds placed around the player, as the setup's 3D Sound.", true),
                flag("drawcursor", "The game's own cursor",
                     "The game draws its own pointer, as the setup's Draw Cursor.", false),
                flag("usejoystick", "Use a joystick",
                     "The first joystick or game controller connected steers, as the setup's Use Joystick.", false)};
    }

    std::vector<ImportPlan> planImport(const std::filesystem::path &source) const override {
        const std::vector<GamePart> parts = findGameParts(source);
        if (parts.empty())
            return {};
        ImportPlan plan;
        plan.game = DetectedGame{id(), kGameId, "LEGO Island", "en", "windows"};
        for (const GamePart &part : parts) {
            addPartToImport(plan, part);
            // Which release, from the game's own files where they are.
            if (part.kind == GamePart::kInstall)
                if (const std::vector<DetectedGame> found = detectGame(id(), part.dir); !found.empty())
                    plan.game = found[0];
        }
        noteImport(plan);
        plan.addPatchLabel.clear(); // the game had no patches
        plan.addFolderLabel = "Add CD or installed game...";
        plan.addFolderTooltip = "Copy the game's CD too (or a copy of it), or the game as its installer left it";
        return {plan};
    }

    bool addToImport(ImportPlan &plan, const std::filesystem::path &file, std::string &message) const override {
        const std::vector<GamePart> parts = findGameParts(file);
        if (parts.empty()) {
            message = "not LEGO Island's CD, nor the game as its installer left it";
            return false;
        }
        const size_t before = plan.files.size();
        for (const GamePart &part : parts)
            addPartToImport(plan, part);
        if (plan.files.size() == before) {
            message = "its files are being copied already";
            return false;
        }
        noteImport(plan);
        return true;
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<LegoislandEngine>(system, target);
    }
};

} // namespace

} // namespace reluva::legoisland

namespace reluva {

const MetaEngine &legoislandMetaEngine() {
    static const legoisland::LegoislandMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
