// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Menu page: the game's own menus (vm/ui.qvm) run by the QVM
// interpreter, their pictures drawn by the GPU renderer.

#include "engines/idtech3/pages/menu_viewer.h"

#include "engines/idtech3/eliteforce/game_host.h"

#include "common/font.h"
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

MenuViewer::MenuViewer(ViewerContext& ctx) : Page(ctx) {
    available_ = ctx.game.code == GameCode::EliteForce && ctx.files.has("vm/ui.qvm");
}

MenuViewer::~MenuViewer() { deactivate(); }

void MenuViewer::activate() {
    const VideoSettings video = videoSettings(ctx_.cvars);
    frameW_ = video.width;
    frameH_ = video.height;
    ctx_.sys.setVideoMode(frameW_, frameH_, video.fullscreen);
    if (Error err = ctx_.sys.init3D(frameW_, frameH_, renderer_)) {
        renderer_ = nullptr;
        ctx_.sys.initGraphics(ctx_.screen.w, ctx_.screen.h, PixelFormat::RGBA32);
        clear(16, 18, 24);
        text(4, 4, "The Menu page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        setTitle("3D rendering not available");
        return;
    }
    overlay_.create(frameW_, frameH_, PixelFormat::RGBA32);
    menus_.attach(renderer_, frameW_, frameH_);
    shownMs_ = 0;
    // The menus draw their own pointer, and have text fields.
    ctx_.sys.setMouseCursorVisible(false);
    ctx_.sys.setTextInput(true);
    if (!menus_.running() && menus_.error().empty())
        start();
    else if (ctx_.matchEnded && menus_.running())
        menus_.show(MenuLayer::kMainMenu); // back from a match: the main menu
    ctx_.matchEnded = false;
    setTitle(menus_.error().empty() ? "Elite Force's own menus (vm/ui.qvm)" : "stopped");
}

void MenuViewer::start() {
    // The server list: on a socket of its own, if the system has a network.
    if (!browser_) {
        std::unique_ptr<net::UdpSocket> socket;
        if (!ctx_.sys.openUdp(0, socket) && socket) {
            browser_ = std::make_unique<ServerBrowser>(std::move(socket));
            browser_->setBroadcasts(ctx_.sys.broadcastAddresses());
        }
    }
    menus_.browser = browser_.get();
    menus_.start(MenuLayer::kMainMenu);
}

void MenuViewer::checkCommands() {
    // The commands the menus have run since the last look: one that
    // starts a level (Engage runs "map <name>", after setting the game's
    // cvars) shows it on the Play page.
    for (const std::string& line : menus_.takeCommands()) {
        lastCommand_ = line;
        while (!lastCommand_.empty() && (lastCommand_.back() == '\n' || lastCommand_.back() == ' '))
            lastCommand_.pop_back();
        lastCommandMs_ = ctx_.sys.getMillis();
        // Each command of the line: one starting a level takes the menus
        // to the Play page; the ones after it (adding bots...) are run
        // there once the level has started.
        for (const std::string& command : splitCommands(line)) {
            const std::vector<std::string> words = splitCommand(command);
            if (words.empty())
                continue;
            const std::string verb = fs::toLower(words[0]);
            if (verb == "quit") {
                // The menus' Quit: back to the start, as the original's
                // program ended; they start afresh next time (left as they
                // are, they'd come back still quitting).
                ctx_.requestPage = "Start";
                menus_.reset();
                return;
            } else if (verb == "vid_restart") {
                // The video settings changed: the page starts its drawing over.
                restart_ = true;
            } else if (verb == "connect" && words.size() >= 2) {
                // Joining a server (the server list's Engage).
                ctx_.requestPage = "Play";
                ctx_.requestConnect = words[1];
                ctx_.requestMap.clear();
                ctx_.requestCommands.clear();
            } else if (verb == "demo" && words.size() >= 2) {
                // A recorded match (the Demos menu's).
                ctx_.requestPage = "Play";
                ctx_.requestDemo = words[1];
                ctx_.requestConnect.clear();
                ctx_.requestMap.clear();
                ctx_.requestCommands.clear();
            } else if ((verb == "map" || verb == "devmap" || verb == "spmap" || verb == "spdevmap") && words.size() >= 2) {
                // A solo match (spmap) or not, as the original's map
                // commands set it; the level starts afresh on the Play page.
                eliteforce::GameHost::applyMapCommand(cvars_, verb);
                ctx_.requestPage = "Play";
                ctx_.requestMap = words[1];
                ctx_.requestCommands.clear();
            } else if (!ctx_.requestMap.empty() && verb != "wait") {
                ctx_.requestCommands.push_back(command);
            }
        }
    }
}

void MenuViewer::deactivate() {
    // Settings and bindings changed in the menus, kept.
    saveConfig(ctx_);
    if (renderer_) {
        ctx_.sys.setMouseCursorVisible(true);
        ctx_.sys.setTextInput(false);
    }
    menus_.detach();
    renderer_ = nullptr;
}

bool MenuViewer::takesKey(const Event& ev) const {
    // Esc belongs to the menus; with Ctrl it leaves, as elsewhere.
    return menus_.running() && !(ev.mods & keymod::Ctrl);
}

void MenuViewer::handleEvent(const Event& ev) {
    if (ev.type == EventType::KeyDown && ev.key == 'r' && (ev.mods & keymod::Ctrl)) {
        // Ctrl+R: start the menus again, stopped or not.
        menus_.reset();
        start();
        return;
    }
    menus_.handleEvent(ev);
    checkCommands();
}

void MenuViewer::update(uint64_t nowMs) {
    if (!renderer_)
        return;
    if (!shownMs_)
        shownMs_ = nowMs;
    if (restart_) {
        // As the original's vid_restart: the menus start over too (at the
        // main menu), so they lay themselves out for the new size.
        restart_ = false;
        deactivate();
        menus_.reset();
        activate();
        if (!renderer_)
            return;
        // What the menus chose, shown for a while (r_fullscreen is theirs:
        // Apply sets it from the Full Screen line).
        lastCommand_ = "vid_restart: " + std::to_string(frameW_) + "x" + std::to_string(frameH_) +
                       (videoSettings(ctx_.cvars).fullscreen ? ", full screen" : ", in a window");
        lastCommandMs_ = nowMs;
    }
    if (browser_)
        browser_->update(nowMs);

    renderer_->setGamma(brightness(ctx_));
    renderer_->beginFrame(gfx::identity(), {0, 0, 0}, {0, 0, 0});
    menus_.frame(nowMs);
    checkCommands();

    // Errors, and for a while how to leave.
    std::fill(overlay_.pixels.begin(), overlay_.pixels.end(), 0);
    std::vector<std::string> lines;
    if (!menus_.error().empty()) {
        lines.push_back(menus_.error());
        if (!menus_.reportPath().empty())
            lines.push_back("Details: " + menus_.reportPath());
        lines.push_back("Ctrl+R: start them again.");
    }
    if (menus_.running() && !menus_.ui()->unhandled().empty()) {
        // Calls the menus made that Reluva answers with 0 for now.
        std::string calls = "System calls not implemented yet:";
        for (const auto& [number, count] : menus_.ui()->unhandled())
            calls += " " + std::to_string(number) + " (x" + std::to_string(count) + ")";
        lines.push_back(calls);
    }
    if (!lastCommand_.empty() && nowMs - lastCommandMs_ < kHintMs)
        lines.push_back("The menus ran: " + lastCommand_);
    if (nowMs - shownMs_ < kHintMs)
        lines.push_back("Keys and mouse go to the menus. Ctrl+Tab: next page, Ctrl+Esc: launcher, Ctrl+R: restart.");
    const int lh = font::kLineHeight;
    for (size_t i = 0; i < lines.size(); ++i) {
        const int y = frameH_ - int(lines.size() - i) * lh - 4;
        const int w = std::min(frameW_, int(font::length(lines[i])) * font::kAdvance + 8);
        for (int yy = y - 2; yy < y + lh; ++yy) {
            uint8_t* p = overlay_.row(yy);
            for (int x = 0; x < w; ++x)
                p[x * 4 + 3] = 170;
        }
        font::drawText(overlay_, 4, y, lines[i],
                       i == 0 && !menus_.error().empty() ? Color{255, 120, 120} : Color{230, 230, 230});
    }
    renderer_->endFrame(&overlay_);
}

} // namespace reluva::idtech3
