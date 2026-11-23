// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// LEGO Loco: the engine's MetaEngine (its name, the releases it recognises,
// making an Engine to run one) and its Engine, which runs the game's own
// program (docs/engines/legoloco/systems/running.md).

#include "engines/engine.h"
#include "engines/legoloco/host/player.h"

#include "audio/mixer.h"
#include "common/fs.h"
#include "common/meeting.h"
#include "common/portmap.h"
#include "common/settings.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace reluva::legoloco {

namespace {

// The releases it recognises, as `reluva --hash <files...>` prints them.
// The CD and an install have the same layout, and the installer copies the
// same files whichever of the CD's nine languages is chosen (it writes the
// choice into Exe/LEGO.INI), so the one entry stands for all of them.
// Its settings: a meeting server ("host" or "host:port", set in the
// config file: the game has nowhere to type it), and whether to ask the
// router for the port while hosting ("1" or "0").
constexpr const char *kMeetingServer = "meeting_server";
constexpr const char *kMapPort = "map_port";
// The desktop the game fills: "1024x768" (as it was played, its boards
// fitting it) or "800x600" (its least, the boards scrolled).
constexpr const char *kDesktop = "desktop";
// The text's font: "arial" (the player's own Arial where it's installed,
// else Liberation Sans) or "liberation" (always Liberation Sans).
constexpr const char *kFont = "font";
// Windows' colours: "98" (its standard scheme, of the game's time) or "xp"
// (Windows XP's blue, as in a recording of the original).
constexpr const char *kColours = "colours";

const std::vector<GameVariant> kVariants = {
    {"legoloco",
     "LEGO Loco (1.0.18.2)",
     "en",
     "windows",
     {{"Exe/Loco.exe", "695dd274531337b1b600fd55f8f51482", 1117696},
      {"art-res/resource.RFH", "e7f73852368fbd30ca42b6d527b25242", 85647}}},
};

/**
 * The game running: its own program, loco.exe (Player), its frames shown
 * and its sound played, the player's mouse and keys passed on.
 */
class LegolocoEngine : public Engine {
public:
    LegolocoEngine(System &system, const GameTarget &target) : _sys(system), _target(target) {}

    Error run() override {
        if (!fs::findFile(_target.path, "Exe/Loco.exe"))
            return Error::fail("LEGO Loco's program, Exe/Loco.exe, isn't in " + _target.path.string());
        // Without an audio device nothing plays.
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
    // The game, until it ends or the player leaves. It draws its own
    // pointer (a hand) where the mouse is; the system's shows Windows'
    // when the game shows that.
    Error play() {
        Player player(_target.path, _target.savePath, _audioOut ? _mixer.get() : nullptr, network());
        int width = 1024;
        int height = 768;
        if (const auto it = _target.settings.find(kDesktop); it != _target.settings.end() && it->second == "800x600")
            width = 800, height = 600;
        player.setDesktop(width, height);
        const auto setting = [&](const char *key, const char *otherwise) {
            const auto it = _target.settings.find(key);
            return it == _target.settings.end() ? std::string(otherwise) : it->second;
        };
        player.setLook(setting(kColours, "98") == "xp" ? x86::Gdi::ColourScheme::kWindowsXp
                                                       : x86::Gdi::ColourScheme::kWindows98,
                       setting(kFont, "arial") == "arial");
        player.start();
        Cursor pointer;
        bool pointerShown = false;
        auto screen = [&] {
            _sys.initGraphics(width, height, PixelFormat::kRGBA32);
            _screen.create(width, height, PixelFormat::kRGBA32);
            _sys.setMouseCursorVisible(pointerShown);
        };
        screen();
        _sys.setWindowTitle("Reluva - " + _target.description);
        _sys.updateScreen(_screen); // black while it starts
        std::vector<uint32_t> pixels;
        while (true) {
            Event ev;
            while (_sys.pollEvent(ev)) {
                if ((ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher) &&
                    !confirmQuit(ev.type == EventType::kReturnToLauncher))
                    continue;
                if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher)
                    return Error::success();
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
            if (player.pointer(pointer, pointerShown)) {
                _sys.setMouseCursor(pointerShown ? &pointer : nullptr);
                _sys.setMouseCursorVisible(pointerShown);
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

    // Its network games over the system's UDP sockets: a search goes to
    // each network's broadcast address, to this machine's own copies, and
    // to the meeting server if one's set.
    std::shared_ptr<x86::DirectPlay::Network> network() {
        auto network = std::make_shared<x86::DirectPlay::Network>();
        System &sys = _sys;
        network->open = [&sys](uint16_t port) {
            std::unique_ptr<net::UdpSocket> socket;
            sys.openUdp(port, socket);
            return socket;
        };
        network->broadcast = _sys.broadcastAddresses();
        network->broadcast.push_back(0xffffffffu);
        network->broadcast.push_back(0x7f000001u);
        const auto setting = [&](const char *key) {
            auto it = _target.settings.find(key);
            return it == _target.settings.end() ? std::string() : it->second;
        };
        if (const std::string meeting = setting(kMeetingServer); !meeting.empty()) {
            const auto [host, port] = net::splitHostPort(meeting, net::kMeetingPort);
            net::Address at;
            at.port = port;
            if (net::parseAddress(meeting, net::kMeetingPort, at) || _sys.resolveHost(host, at.ip))
                network->meeting = at;
            else
                std::fprintf(stderr, "LEGO Loco: can't find the meeting server %s\n", meeting.c_str());
        }
        if (setting(kMapPort) != "0")
            network->mapPort = [&sys](uint16_t port) -> std::shared_ptr<void> {
                return std::make_shared<net::PortMapper>(net::PortMapper::through(sys), port);
            };
        return network;
    }

    // Whether to leave after the window was closed (or, `toLauncher`,
    // Ctrl+F5 was pressed): the player is asked, as a layout not saved
    // would be lost.
    bool confirmQuit(bool toLauncher) {
        return _sys.confirmQuit(_target.description + " is still running. A layout not saved will be lost.\n\n" +
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
 * recognises, and making a LegolocoEngine to run one.
 */
class LegolocoMetaEngine : public MetaEngine {
public:
    const char *id() const override { return "legoloco"; }
    const char *name() const override { return "LEGO Loco"; }

    std::vector<DetectedGame> detect(const std::filesystem::path &dir) const override {
        return detectFromTable(id(), kVariants, dir);
    }

    std::vector<GameOption> engineOptions() const override {
        return {{kDesktop,
                 "Screen size",
                 "The size of the game's screen. It was made for 1024 by 768, where its boards fit; at 800 by\n"
                 "600 they scroll.",
                 true,
                 {{"1024x768", "1024 by 768"}, {"800x600", "800 by 600"}}},
                {kFont,
                 "Text font",
                 "The font the game's text is drawn in. It asks for Arial: your own, where it's installed, is\n"
                 "as the original; Liberation Sans, built in, is as wide.",
                 true,
                 {{"arial", "Arial if installed"}, {"liberation", "Liberation Sans"}}},
                {kColours,
                 "Windows colours",
                 "Windows' own colours, where the game shows them (the name box's selection, the window's\n"
                 "frame): Windows 98's, of the game's time, or Windows XP's.",
                 true,
                 {{"98", "Windows 98"}, {"xp", "Windows XP"}}},
                {kMapPort,
                 "Open the port on the router when hosting",
                 "When hosting a network game, ask the router (UPnP or NAT-PMP) to pass the game's port on to\n"
                 "this computer, so players across the Internet can reach it directly.",
                 true,
                 {}}};
    }

    std::unique_ptr<Engine> create(System &system, const GameTarget &target) const override {
        return std::make_unique<LegolocoEngine>(system, target);
    }
};

} // namespace

} // namespace reluva::legoloco

namespace reluva {

const MetaEngine &legolocoMetaEngine() {
    static const legoloco::LegolocoMetaEngine kInstance;
    return kInstance;
}

} // namespace reluva
