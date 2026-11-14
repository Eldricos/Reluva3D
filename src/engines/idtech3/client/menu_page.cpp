// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Menu page: the game's own menus (vm/ui.qvm) run by the QVM
// interpreter, their pictures drawn by the GPU renderer.

#include "engines/idtech3/client/menu_page.h"

#include "engines/idtech3/eliteforce/game_host.h"

#include "graphics/font.h"
#include "common/developer.h"
#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace reluva::idtech3 {

namespace {

constexpr uint64_t kHintMs = 6000;

} // namespace

MenuPage::MenuPage(PageContext &ctx) : Page(ctx) {
    _available = (ctx.game.code == GameCode::kEliteForce || ctx.game.code == GameCode::kJediOutcast) &&
                 ctx.files.has("vm/ui.qvm");
}

MenuPage::~MenuPage() {
    deactivate();
}

void MenuPage::activate() {
    const VideoSettings video = videoSettings(_ctx.cvars);
    _frameW = video.width;
    _frameH = video.height;
    _ctx.sys.setVideoMode(_frameW, _frameH, video.fullscreen);
    if (Error err = _ctx.sys.init3D(_frameW, _frameH, _renderer)) {
        _renderer = nullptr;
        _ctx.sys.initGraphics(_ctx.screen.w, _ctx.screen.h, PixelFormat::kRGBA32);
        clear(16, 18, 24);
        text(4, 4, "The Menu page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        setTitle("3D rendering not available");
        return;
    }
    _overlay.create(_frameW, _frameH, PixelFormat::kRGBA32);
    _menus.attach(_renderer, _frameW, _frameH);
    _shownMs = 0;
    // The menus draw their own pointer, and have text fields.
    _ctx.sys.setMouseCursorVisible(false);
    _ctx.sys.setTextInput(true);
    if (!_musicIntro.empty() || !_musicLoop.empty())
        playMusic(_musicIntro, _musicLoop); // back from another page: from the start again
    if (!_menus.running() && _menus.error().empty())
        start();
    else if (_ctx.matchEnded && _menus.running())
        _menus.show(MenuLayer::kMainMenu); // back from a match: the main menu
    _ctx.matchEnded = false;
    setTitle(_menus.error().empty() ? "the game's own menus (vm/ui.qvm)" : "stopped");
}

void MenuPage::start() {
    // The server list: on a socket of its own, if the system has a network.
    if (!_browser) {
        std::unique_ptr<net::UdpSocket> socket;
        if (!_ctx.sys.openUdp(0, socket) && socket) {
            _browser = std::make_unique<ServerBrowser>(std::move(socket));
            _browser->setBroadcasts(_ctx.sys.broadcastAddresses());
        }
    }
    _menus.browser = _browser.get();
    _menus.configure = [this](MenuHost &ui) {
        ui.backgroundTrack = [this](const std::string &intro, const std::string &loop) { playMusic(intro, loop); };
    };
    _menus.start(MenuLayer::kMainMenu);
}

void MenuPage::playMusic(const std::string &intro, const std::string &loop) {
    _musicIntro = intro;
    _musicLoop = loop;
    if (!_ctx.mixer)
        return;
    if (!_music)
        _music = std::make_unique<SoundSystem>(*_ctx.mixer, _soundLibrary);
    if (intro.empty() && loop.empty())
        _music->stopBackgroundTrack();
    else
        _music->startBackgroundTrack(intro, loop);
}

void MenuPage::checkCommands() {
    // The commands the menus have run since the last look: one that
    // starts a level (Engage runs "map <name>", after setting the game's
    // cvars) shows it on the Play page.
    for (const std::string &line : _menus.takeCommands()) {
        _lastCommand = line;
        while (!_lastCommand.empty() && (_lastCommand.back() == '\n' || _lastCommand.back() == ' '))
            _lastCommand.pop_back();
        _lastCommandMs = _ctx.sys.getMillis();
        // Each command of the line: one starting a level takes the menus
        // to the Play page; the ones after it (adding bots...) are run
        // there once the level has started.
        for (const std::string &command : splitCommands(line)) {
            const std::vector<std::string> words = splitCommand(command);
            if (words.empty())
                continue;
            const std::string verb = fs::toLower(words[0]);
            if (verb == "quit") {
                // The menus' Quit: back to the start, as the original's
                // program ended; they start afresh next time (left as they
                // are, they'd come back still quitting).
                _ctx.requestPage = "Start";
                _menus.reset();
                return;
            } else if (verb == "music") {
                // Jedi Outcast's menus start theirs so; one name is the
                // loop too, as the original's command takes it.
                if (words.size() >= 2)
                    playMusic(words[1], words.size() >= 3 ? words[2] : words[1]);
            } else if (verb == "vid_restart") {
                // The video settings changed: the page starts its drawing over;
                // a mod chosen (fs_game), the game's files too.
                _restart = true;
                _ctx.requestRestart = true;
            } else if (verb == "connect" && words.size() >= 2) {
                // Joining a server (the server list's Engage).
                _ctx.requestPage = "Play";
                _ctx.requestConnect = words[1];
                _ctx.requestMap.clear();
                _ctx.requestCommands.clear();
            } else if (verb == "demo" && words.size() >= 2) {
                // A recorded match (the Demos menu's).
                _ctx.requestPage = "Play";
                _ctx.requestDemo = words[1];
                _ctx.requestConnect.clear();
                _ctx.requestMap.clear();
                _ctx.requestCommands.clear();
            } else if ((verb == "map" || verb == "devmap" || verb == "spmap" || verb == "spdevmap") &&
                       words.size() >= 2) {
                // A solo match (spmap) or not, as the original's map
                // commands set it; the level starts afresh on the Play page.
                eliteforce::GameHost::applyMapCommand(_cvars, verb);
                _ctx.requestPage = "Play";
                _ctx.requestMap = words[1];
                _ctx.requestCommands.clear();
            } else if (!_ctx.requestMap.empty() && verb != "wait") {
                _ctx.requestCommands.push_back(command);
            }
        }
    }
}

void MenuPage::deactivate() {
    // Settings and bindings changed in the menus, kept.
    saveConfig(_ctx);
    // Not under other pages: a match plays its own.
    _music.reset();
    if (_ctx.mixer)
        _ctx.mixer->setTypeVolume(audio::SoundType::kMusic, audio::kMaxVolume);
    if (_renderer) {
        _ctx.sys.setMouseCursorVisible(true);
        _ctx.sys.setTextInput(false);
    }
    _menus.detach();
    _renderer = nullptr;
}

bool MenuPage::takesKey(const Event &ev) const {
    // Esc belongs to the menus; with Ctrl it leaves, as elsewhere.
    return _menus.running() && !(ev.mods & keymod::kCtrl);
}

void MenuPage::handleEvent(const Event &ev) {
    if (ev.type == EventType::kJoyButtonDown || ev.type == EventType::kJoyButtonUp || ev.type == EventType::kJoyAxis ||
        ev.type == EventType::kJoyHat || ev.type == EventType::kJoyRemoved) {
        _joystick.event(ev);
        return;
    }
    if (kDeveloperKeys && ev.type == EventType::kKeyDown && ev.key == 'r' && (ev.mods & keymod::kCtrl)) {
        // Ctrl+R: start the menus again, stopped or not.
        _menus.reset();
        start();
        return;
    }
    _menus.handleEvent(ev);
    checkCommands();
}

void MenuPage::update(uint64_t nowMs) {
    if (!_renderer)
        return;
    if (!_shownMs)
        _shownMs = nowMs;
    if (_restart) {
        // As the original's vid_restart: the menus start over too (at the
        // main menu), so they lay themselves out for the new size.
        _restart = false;
        deactivate();
        _menus.reset();
        activate();
        if (!_renderer)
            return;
        // What the menus chose, shown for a while (r_fullscreen is theirs:
        // Apply sets it from the Full Screen line).
        _lastCommand = "vid_restart: " + std::to_string(_frameW) + "x" + std::to_string(_frameH) +
                       (videoSettings(_ctx.cvars).fullscreen ? ", full screen" : ", in a window");
        _lastCommandMs = nowMs;
    }
    if (_browser)
        _browser->update(nowMs);
    if (_music) {
        // At s_musicvolume, 0.25 unless set; the effects scale themselves.
        const float volume = _cvars.find("s_musicvolume") ? _cvars.value("s_musicvolume") : 0.25f;
        _ctx.mixer->setTypeVolume(audio::SoundType::kMusic,
                                  int(std::clamp(volume, 0.0f, 1.0f) * float(audio::kMaxVolume)));
        _music->setTime(nowMs);
        _music->endFrame();
    }

    _renderer->setGamma(brightness(_ctx));
    _renderer->beginFrame(gfx::identity(), {0, 0, 0}, {0, 0, 0});
    // The joystick's keys, as the original's engine passes them to the
    // menus (binding JOY1... in the Controls menu, moving with the arrows).
    for (const auto &[k, down] : _joystick.frame(_ctx.cvars).keys)
        _menus.key(k, down);
    _menus.frame(nowMs);
    checkCommands();

    // Errors, and for a while how to leave.
    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), 0);
    std::vector<std::string> lines;
    if (!_menus.error().empty()) {
        lines.push_back(_menus.error());
        if (!_menus.reportPath().empty())
            lines.push_back("Details: " + _menus.reportPath());
        lines.push_back(kDeveloperKeys ? "Ctrl+R: start them again." : "Ctrl+Esc: leave.");
    }
    if (_menus.running() && !_menus.ui()->unhandled().empty()) {
        // Calls the menus made that Reluva answers with 0 for now.
        std::string calls = "System calls not implemented yet:";
        for (const auto &[number, count] : _menus.ui()->unhandled())
            calls += " " + std::to_string(number) + " (x" + std::to_string(count) + ")";
        lines.push_back(calls);
    }
    if (!_lastCommand.empty() && nowMs - _lastCommandMs < kHintMs)
        lines.push_back("The menus ran: " + _lastCommand);
    if (nowMs - _shownMs < kHintMs)
        lines.push_back(kDeveloperKeys ? "Keys and mouse go to the menus. Ctrl+Esc: launcher, Ctrl+R: restart."
                                       : "Keys and mouse go to the menus. Ctrl+Esc: launcher.");
    const int lh = font::kLineHeight;
    for (size_t i = 0; i < lines.size(); ++i) {
        const int y = _frameH - int(lines.size() - i) * lh - 4;
        const int w = std::min(_frameW, int(font::length(lines[i])) * font::kAdvance + 8);
        for (int yy = y - 2; yy < y + lh; ++yy) {
            uint8_t *p = _overlay.row(yy);
            for (int x = 0; x < w; ++x)
                p[x * 4 + 3] = 170;
        }
        font::drawText(_overlay, 4, y, lines[i],
                       i == 0 && !_menus.error().empty() ? Color{255, 120, 120} : Color{230, 230, 230});
    }
    _renderer->endFrame(&_overlay);
}

} // namespace reluva::idtech3
