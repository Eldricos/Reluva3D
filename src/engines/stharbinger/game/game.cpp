// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stharbinger/game/game.h"

#include "common/fs.h"
#include "common/log.h"
#include "common/text.h"
#include "engines/stharbinger/formats/audio.h"
#include "engines/stharbinger/harbinger.h"
#include "engines/stharbinger/platform.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace reluva::stharbinger {

namespace {

// The renderer runs every 20 ms; script timers tick every 100 ms (DEEP.EXE
// derives both from a 250 Hz timer).
const uint32_t kStepMillis = 20;
const uint32_t kStepsPerTimerTick = 5;
const uint32_t kFadeStepMillis = 50;
const size_t kMaxAmbients = 2;

const char* const kHelp = "Game keys:\n"
                          "  left click       use the hotspot under the mouse, or pick a reply\n"
                          "  F2               show/hide hotspot rectangles\n"
                          "  F10              list the variables that are set (on the console)\n"
                          "  M                the deck map (Esc closes it)\n"
                          "  right click, Esc the game menu (save, load, quit ...); Esc also skips videos\n";

} // namespace

Game::Game(Harbinger* vm)
    : vm_(vm), system_(vm->getSystem()), resources_(vm->getResources()), scenes_(vm->getScenes()) {}

Game::~Game() {
    if (system_->getMixer())
        system_->getMixer()->stopAll();
}

int Game::run(int startScene) {
    system_->initGraphics(kScreenWidth, kScreenHeight);
    if (vm_->getOptions().debug)
        std::printf("%s", kHelp);
    // The game starts with its menu unless a scene was asked for. The original
    // skips the menu on its very first run and goes straight to the intro; so
    // does "Play exactly as the original".
    std::string base = system_->getSavePath();
    std::error_code ec;
    bool firstRun = !base.empty() && !std::filesystem::exists(std::filesystem::path(base) / "dlas.ds9", ec);
    loadSettings();
    if (startScene < 0 && firstRun && vm_->getOptions().original) {
        int opening = vm_->findScene("OPENING");
        start(opening < 0 ? 0 : opening);
        saveSettings();
    } else if (startScene < 0) {
        draw::create(screen_, kScreenWidth, kScreenHeight);
        draw::create(cleanScreen_, kScreenWidth, kScreenHeight);
        if (!cursorsLoaded_)
            loadCursors();
        openMenu(true);
    } else {
        start(startScene);
    }

    nextStepTime_ = system_->getMillis();
    while (!quit_ && !vm_->shouldQuit()) {
        InputEvent event;
        while (system_->pollEvent(event))
            handleEvent(event);

        uint32_t now = system_->getMillis();
        int steps = 0;
        while (int32_t(now - nextStepTime_) >= 0 && steps < 10) {
            step();
            nextStepTime_ += kStepMillis;
            steps++;
        }
        if (steps == 10)
            nextStepTime_ = now; // fell behind (a slow load): don't race to catch up
        if (steps && !screenFrozen_) {
            system_->copyRectToScreen(screen_);
            system_->updateScreen();
        }
        system_->delayMillis(2);
    }
    return 0;
}

// ---- Test and debugging hooks ---------------------------------------------

void Game::start(int scene) {
    draw::create(screen_, kScreenWidth, kScreenHeight);
    draw::create(cleanScreen_, kScreenWidth, kScreenHeight);
    if (!cursorsLoaded_)
        loadCursors();
    applyPalette();
    enterScene(scene);
    switchScenes();
}

void Game::runSteps(int steps) {
    for (int i = 0; i < steps; i++)
        step();
}

void Game::click(int x, int y) {
    InputEvent event;
    event.type = InputEvent::kLeftDown;
    event.mouse = Point{x, y};
    handleEvent(event);
    switchScenes();
}

int32_t Game::getVariable(const std::string& name) const {
    auto it = variables_.find(fs::toLower(name));
    return it == variables_.end() ? 0 : it->second;
}

const std::string& Game::getSceneName() const {
    static const std::string none;
    return scene_ ? scene_->name : none;
}

// ---- Scenes ---------------------------------------------------------------

void Game::enterScene(int index) {
    sceneIndex_ = index;
    scene_ = &scenes_[index];
    log::debug(1, "Entering scene %s (slot %d)", scene_->name.c_str(), scene_->slot);

    // Everything but the variables starts afresh: the game reloads the script and
    // frees the hotspots and timers of the previous scene.
    buildLabels();
    hotspots_.assign(scene_->lists[kListHotspots].size(), Hotspot());
    timers_.assign(scene_->lists[kListTimers].size(), Timer());
    setupAnims();
    if (draw::empty(background_))
        draw::create(background_, kScreenWidth, kScreenHeight, 0); // the game's buffer always exists
    hoverHotspot_ = -1;
    status_.clear();
    updateCaption();

    thread_ = Thread();
    thread_.active = true;
    runThread();
}

void Game::leaveScene() {
    thread_ = Thread();
    hotspots_.clear();
    timers_.clear();
    anims_.clear();
    if (audio::Mixer* mixer = system_->getMixer()) {
        for (audio::SoundHandle h : ambients_)
            mixer->stop(h);
    }
    ambients_.clear();
    ambientSounds_.clear();
}

bool Game::soundsPlaying() {
    audio::Mixer* mixer = system_->getMixer();
    if (!mixer)
        return false;
    oneShots_.erase(
        std::remove_if(oneShots_.begin(), oneShots_.end(), [&](audio::SoundHandle h) { return !mixer->isPlaying(h); }),
        oneShots_.end());
    return !oneShots_.empty();
}

void Game::switchScenes() {
    // A scene's entry script can itself switch scenes; follow the chain. Like the
    // game, wait for one-shot sounds (a door closing ...) to finish first.
    for (int guard = 0; pendingScene_ >= 0 && guard < 100; guard++) {
        if (soundsPlaying())
            return;
        int next = pendingScene_;
        pendingScene_ = -1;
        leaveScene();
        enterScene(next);
    }
}

void Game::setBackground(int index, bool loadPalette) {
    const auto& list = scene_->lists[kListBackgrounds];
    if (index < 0 || index >= int(list.size()))
        return;
    backgroundIndex_ = index;
    Palette palette = palette_;
    bool paletteLoaded = false;
    if (!resources_.loadBackground(list[index].name, background_, palette, paletteLoaded)) {
        log::warning("Background %s not found", list[index].name.c_str());
        draw::create(background_, kScreenWidth, kScreenHeight, 0);
    }
    if (tricorderIcon_) {
        if (!tricorderRle_.frameCount())
            if (auto s = resources_.openFile("ANIMS/TRSI/TRSICON.RLE"))
                tricorderRle_.load(*s);
        tricorderRle_.drawFrame(0, background_);
    }
    // The palette changes only when the script asks for it.
    if (loadPalette && paletteLoaded) {
        paletteBackground_ = index;
        palette_ = palette;
        fadeLevel_ = 256;
        applyPalette();
    }
    // Shown at once, with the animations as they are, and screen updates resume.
    workBufferStale_ = true;
    renderAnims(false);
    draw::blit(screen_, workBuffer_, 0, 0);
    screenFrozen_ = false;
    system_->copyRectToScreen(screen_);
    updateCaption();
}

void Game::playSound(int index, const Argument* mode) {
    audio::Mixer* mixer = system_->getMixer();
    const auto& list = scene_->lists[kListSounds];
    if (!mixer || index < 0 || index >= int(list.size()))
        return;
    int kind = mode && mode->type == kArgSymbol ? int(mode->value & 0xFF) : -1;
    if (kind == 2)
        return;
    if (kind == 0) {
        // AMBIENT: loops until the scene changes; at most two at a time.
        ambients_.erase(std::remove_if(ambients_.begin(), ambients_.end(),
                                       [&](audio::SoundHandle h) { return !mixer->isPlaying(h); }),
                        ambients_.end());
        if (ambients_.size() >= kMaxAmbients) {
            log::debug(1, "Too many ambient sounds; %s not played", list[index].name.c_str());
            return;
        }
    }
    std::vector<uint8_t> data;
    if (!resources_.loadSound(list[index].name, data)) {
        log::debug(1, "Sound %s not found", list[index].name.c_str());
        return;
    }
    audio::SoundHandle h = mixer->play(makeRawStream(std::move(data), kind == 0 ? 0 : 1), audio::SoundType::Effects);
    if (kind == 0) {
        ambients_.push_back(h);
        ambientSounds_.push_back(index);
    } else if (kind == 1)
        oneShots_.push_back(h); // ASNEEDED sounds hold up the next scene change
}

std::string Game::listName(ListKind kind, int index) const {
    const auto& list = scene_->lists[kind];
    return index >= 0 && index < int(list.size()) ? list[index].name : std::string("?");
}

// ---- Input ------------------------------------------------------------------

int Game::findHotspotAt(int x, int y) const {
    // The lowest enabled hotspot wins; edges are inclusive (0x18DAB).
    for (int i = 0; i < int(hotspots_.size()); i++) {
        const Hotspot& h = hotspots_[i];
        if (h.defined && h.enabled && x >= h.x && x <= h.x + h.w && y >= h.y && y <= h.y + h.h)
            return i;
    }
    return -1;
}

void Game::handleEvent(const InputEvent& event) {
    if (credits_.active) {
        // Esc ends the scroll.
        if (event.type == InputEvent::kQuit)
            quit_ = true;
        else if (event.type == InputEvent::kKeyDown && event.key == key::Escape &&
                 credits_.phase != CreditsScreen::kFadeOut) {
            credits_.phase = CreditsScreen::kFadeOut;
            credits_.phaseStart = millis_;
        }
        return;
    }
    if (deckMap_) {
        if (event.type == InputEvent::kQuit)
            quit_ = true;
        else if (event.type == InputEvent::kKeyDown &&
                 (event.key == key::Escape || (event.key == 'm' && !vm_->getOptions().original)))
            deckMap_ = false;
        return;
    }
    if (menu_.active) {
        handleMenuEvent(event);
        return;
    }
    switch (event.type) {
    case InputEvent::kQuit: quit_ = true; break;
    case InputEvent::kKeyDown:
        if (event.key == key::Escape) {
            // Esc skips a video; in a scene it also opens the game menu (an added
            // convenience: the original only uses the right button).
            if (video_.active) {
                finishVideo(true);
                if (thread_.active)
                    runThread();
                switchScenes();
            } else if (!arcade_ && !conv_.active && !vm_->getOptions().original) {
                requestMenu();
            }
        } else if (event.key == 'm') {
            // The deck map, where the game allows it.
            if (mapEnabled_ && !thread_.active && !conv_.active && !arcade_ && !video_.active && mapLocation_ != 0xFF)
                openDeckMap();
        } else if (event.key == (key::F1 + 1) && vm_->getOptions().debug) {
            showHotspots_ = !showHotspots_;
        } else if (event.key == (key::F1 + 9) && vm_->getOptions().debug) {
            std::printf("Variables set in %s:\n", scene_->name.c_str());
            for (const auto& v : variables_)
                if (v.second)
                    std::printf("  %-16s %u\n", v.first.c_str(), v.second);
        }
        break;
    case InputEvent::kMouseMove: {
        mouse_ = event.mouse;
        if (conv_.menuWaiting) {
            Point m = cursorHitPoint(true);
            conv_.hover = menuEntryAt(m.x, m.y);
        }
        Point p = cursorHitPoint(false);
        if (findHotspotAt(p.x, p.y) != hoverHotspot_) {
            hoverHotspot_ = findHotspotAt(p.x, p.y);
            updateCaption();
        }
        break;
    }
    case InputEvent::kLeftDown: {
        mouse_ = event.mouse;
        if (arcade_) {
            arcadeLeft_ = true; // phaser
            break;
        }
        if (video_.active)
            break; // only Esc skips a video
        if (conv_.active) {
            handleConversationClick(mouse_.x, mouse_.y);
            if (!conv_.active && thread_.active)
                runThread();
            break;
        }
        // Clicks count only while the script is idle; they are not queued.
        if (thread_.active || pendingScene_ >= 0)
            break;
        Point p = cursorHitPoint(false);
        int i = findHotspotAt(p.x, p.y);
        if (i >= 0) {
            log::debug(1, "Hotspot %s", listName(kListHotspots, i).c_str());
            status_.clear();
            jumpToLabel(hotspots_[i].label);
            runThread();
            hoverHotspot_ = findHotspotAt(mouse_.x, mouse_.y);
            updateCaption();
        }
        break;
    }
    case InputEvent::kRightDown:
        mouse_ = event.mouse;
        if (arcade_) {
            arcadeRight_ = true; // torpedoes
        } else if (conv_.active) {
            // In the reply menu: the conversation ends (rudely) and the menu opens;
            // resuming runs CONVERSE again.
            if (conv_.menuWaiting && mapEnabled_) {
                uint32_t converse = actionSeqPc_;
                conv_.menuWaiting = false;
                abortConversation(true, -1);
                thread_.active = true;
                thread_.pc = converse;
                thread_.wait = kWaitNone;
                openMenu(false);
            }
        } else if (!video_.active) {
            requestMenu();
        }
        break;
    default: break;
    }
}

// ---- Clock --------------------------------------------------------------------

void Game::step() {
    stepCount_++;
    millis_ += kStepMillis;
    updateTick();

    if (credits_.active) {
        updateCredits();
        return;
    }
    if (deckMap_) {
        // The game waits while the map is up.
        draw::blit(screen_, deckMapFrame_, 0, 0);
        return;
    }
    if (menu_.active) {
        // The game waits; the menu redraws for the name entry's blinking. The
        // cursor goes over a fresh copy of the menu picture every step.
        if (menu_.mode == GameMenu::kNameEntry && stepCount_ % 10 == 0)
            drawMenuScreen();
        draw::blit(screen_, menu_.frame, 0, 0);
        drawCursor(screen_);
        return;
    }

    if (arcade_) {
        updateArcade();
        if (!arcade_ && !video_.active && thread_.active)
            runThread();
        draw::blit(cleanScreen_, screen_, 0, 0);
        if (arcade_)
            drawCursor(screen_);
        switchScenes();
        return;
    }
    if (video_.active) {
        // Nothing else runs while a video plays.
        updateVideo();
        if (!video_.active && thread_.active)
            runThread();
        switchScenes();
        return;
    }

    // Erase, draw and advance the animations in the work buffer.
    renderAnims(true);
    draw::blit(screen_, workBuffer_, 0, 0);
    // Beams that have run their time are wiped from the work buffer with the
    // background, a little wider than drawn (0x22AE3).
    for (Beam& b : beams_) {
        if (--b.steps > 0 || draw::empty(workBuffer_))
            continue;
        int x0 = std::max(0, std::min(b.x0, b.x1) - 3), x1 = std::min(kScreenWidth, std::max(b.x0, b.x1) + 4);
        int y0 = std::max(0, std::min(b.y0, b.y1) - 6), y1 = std::min(kScreenHeight, std::max(b.y0, b.y1) + 9);
        for (int y = y0; y < y1 && y < background_.h; y++)
            for (int x = x0; x < x1 && x < background_.w; x++)
                *draw::at(workBuffer_, x, y) = *draw::at(background_, x, y);
    }
    beams_.erase(std::remove_if(beams_.begin(), beams_.end(), [](const Beam& b) { return b.steps <= 0; }),
                 beams_.end());
    if (showHotspots_) {
        uint8_t on = uint8_t(palette_.findBestColor(0, 255, 0));
        uint8_t off = uint8_t(palette_.findBestColor(255, 0, 0));
        for (const Hotspot& h : hotspots_)
            if (h.defined && h.w > 0 && h.h > 0)
                draw::frameRect(screen_, h.x, h.y, h.w + 1, h.h + 1, h.enabled ? on : off);
    }
    drawMenu(screen_);
    updateFade();
    cyclePalette();

    // Timers tick while the script idles or waits for an animation.
    bool idle = !thread_.active || thread_.wait == kWaitAnim;
    if (stepCount_ % kStepsPerTimerTick == 0 && idle && pendingScene_ < 0)
        tickTimers();

    if (conv_.active)
        updateConversation();
    if (thread_.active && !conv_.active && !video_.active)
        runThread();
    switchScenes();

    // The hotspot under the cursor may change without the mouse moving.
    Point p = cursorHitPoint(false);
    int hover = findHotspotAt(p.x, p.y);
    if (hover != hoverHotspot_) {
        hoverHotspot_ = hover;
        updateCaption();
    }
    draw::blit(cleanScreen_, screen_, 0, 0);
    if (!video_.active)
        drawCursor(screen_);
}

void Game::tickTimers() {
    // One tick: count down until the first timer fires (0x23E40).
    for (Timer& t : timers_) {
        if (!t.defined || !t.active || t.count == 0)
            continue;
        if (--t.count == 0) {
            log::debug(2, "Timer fired");
            jumpToLabel(t.label);
            return;
        }
    }
}

void Game::updateFade() {
    if (!fade_.active)
        return;
    int step = int((millis_ - fade_.startMillis) / kFadeStepMillis);
    if (step >= fade_.steps) {
        fade_.active = false;
        fadeLevel_ = fade_.in ? 256 : 0;
    } else {
        int level = step * 256 / fade_.steps;
        fadeLevel_ = fade_.in ? level : 256 - level;
    }
    applyPalette();
}

void Game::applyPalette() {
    uint8_t colors[768];
    const uint8_t* src = palette_.data();
    for (int i = 0; i < 768; i++)
        colors[i] = uint8_t(src[i] * fadeLevel_ / 256);
    system_->setPalette(colors, 0, 256);
}

void Game::updateCaption() {
    // The details are for debugging; the game itself only has its name.
    if (!vm_->getOptions().debug) {
        system_->setWindowTitle("Reluva - Star Trek: Deep Space Nine - Harbinger");
        return;
    }
    std::string caption = "Reluva - Harbinger | " + (scene_ ? scene_->name : std::string());
    if (hoverHotspot_ >= 0 && hoverHotspot_ < int(hotspots_.size()))
        caption += text::format(" | %s [%s]", listName(kListHotspots, hoverHotspot_).c_str(),
                                getCursorName(hotspots_[hoverHotspot_].cursor));
    if (!status_.empty())
        caption += " | " + status_;
    system_->setWindowTitle(caption);
}

} // namespace reluva::stharbinger
