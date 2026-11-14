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

const char *const kHelp = "Game keys:\n"
                          "  left click       use the hotspot under the mouse, or pick a reply\n"
                          "  F2               show/hide hotspot rectangles\n"
                          "  F10              list the variables that are set (on the console)\n"
                          "  M                the deck map (Esc closes it)\n"
                          "  right click, Esc the game menu (save, load, quit ...); Esc also skips videos\n";

} // namespace

Game::Game(Harbinger *vm)
    : _vm(vm), _system(vm->getSystem()), _resources(vm->getResources()), _scenes(vm->getScenes()) {
}

Game::~Game() {
    if (_system->getMixer())
        _system->getMixer()->stopAll();
}

int Game::run(int startScene) {
    _system->initGraphics(kScreenWidth, kScreenHeight);
    updateCaption(); // the game menu, where most starts land, sets none of its own
    if (_vm->getOptions().debug)
        std::printf("%s", kHelp);
    // The game starts with its menu unless a scene was asked for. The original
    // skips the menu on its very first run and goes straight to the intro; so
    // does "Play exactly as the original".
    std::string base = _system->getSavePath();
    std::error_code ec;
    bool firstRun = !base.empty() && !std::filesystem::exists(std::filesystem::path(base) / "dlas.ds9", ec);
    loadSettings();
    if (startScene < 0 && firstRun && _vm->getOptions().original) {
        int opening = _vm->findScene("OPENING");
        start(opening < 0 ? 0 : opening);
        saveSettings();
    } else if (startScene < 0) {
        draw::create(_screen, kScreenWidth, kScreenHeight);
        draw::create(_cleanScreen, kScreenWidth, kScreenHeight);
        if (!_cursorsLoaded)
            loadCursors();
        openMenu(true);
    } else {
        start(startScene);
    }

    _nextStepTime = _system->getMillis();
    while (!_quit && !_vm->shouldQuit()) {
        InputEvent event;
        while (_system->pollEvent(event))
            handleEvent(event);

        uint32_t now = _system->getMillis();
        int steps = 0;
        while (int32_t(now - _nextStepTime) >= 0 && steps < 10) {
            step();
            _nextStepTime += kStepMillis;
            steps++;
        }
        if (steps == 10)
            _nextStepTime = now; // fell behind (a slow load): don't race to catch up
        if (steps && !_screenFrozen) {
            _system->copyRectToScreen(_screen);
            _system->updateScreen();
        }
        _system->delayMillis(2);
    }
    return 0;
}

// ---- Test and debugging hooks ---------------------------------------------

void Game::start(int scene) {
    draw::create(_screen, kScreenWidth, kScreenHeight);
    draw::create(_cleanScreen, kScreenWidth, kScreenHeight);
    if (!_cursorsLoaded)
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

int32_t Game::getVariable(const std::string &name) const {
    auto it = _variables.find(fs::toLower(name));
    return it == _variables.end() ? 0 : it->second;
}

const std::string &Game::getSceneName() const {
    static const std::string kNone;
    return _scene ? _scene->name : kNone;
}

// ---- Scenes ---------------------------------------------------------------

void Game::enterScene(int index) {
    _sceneIndex = index;
    _scene = &_scenes[index];
    log::debug(1, "Entering scene %s (slot %d)", _scene->name.c_str(), _scene->slot);

    // Everything but the variables starts afresh: the game reloads the script and
    // frees the hotspots and timers of the previous scene.
    buildLabels();
    _hotspots.assign(_scene->lists[kListHotspots].size(), Hotspot());
    _timers.assign(_scene->lists[kListTimers].size(), Timer());
    setupAnims();
    if (draw::empty(_background))
        draw::create(_background, kScreenWidth, kScreenHeight, 0); // the game's buffer always exists
    _hoverHotspot = -1;
    _status.clear();
    updateCaption();

    _thread = Thread();
    _thread.active = true;
    runThread();
}

void Game::leaveScene() {
    _thread = Thread();
    _hotspots.clear();
    _timers.clear();
    _anims.clear();
    if (audio::Mixer *mixer = _system->getMixer()) {
        for (audio::SoundHandle h : _ambients)
            mixer->stop(h);
    }
    _ambients.clear();
    _ambientSounds.clear();
}

bool Game::soundsPlaying() {
    audio::Mixer *mixer = _system->getMixer();
    if (!mixer)
        return false;
    _oneShots.erase(
        std::remove_if(_oneShots.begin(), _oneShots.end(), [&](audio::SoundHandle h) { return !mixer->isPlaying(h); }),
        _oneShots.end());
    return !_oneShots.empty();
}

void Game::switchScenes() {
    // A scene's entry script can itself switch scenes; follow the chain. Like the
    // game, wait for one-shot sounds (a door closing ...) to finish first.
    for (int guard = 0; _pendingScene >= 0 && guard < 100; guard++) {
        if (soundsPlaying())
            return;
        int next = _pendingScene;
        _pendingScene = -1;
        leaveScene();
        enterScene(next);
    }
}

void Game::setBackground(int index, bool loadPalette) {
    const auto &list = _scene->lists[kListBackgrounds];
    if (index < 0 || index >= int(list.size()))
        return;
    _backgroundIndex = index;
    Palette palette = _palette;
    bool paletteLoaded = false;
    if (!_resources.loadBackground(list[index].name, _background, palette, paletteLoaded)) {
        log::warning("Background %s not found", list[index].name.c_str());
        draw::create(_background, kScreenWidth, kScreenHeight, 0);
    }
    if (_tricorderIcon) {
        if (!_tricorderRle.frameCount())
            if (auto s = _resources.openFile("ANIMS/TRSI/TRSICON.RLE"))
                _tricorderRle.load(*s);
        _tricorderRle.drawFrame(0, _background);
    }
    // The palette changes only when the script asks for it.
    if (loadPalette && paletteLoaded) {
        _paletteBackground = index;
        _palette = palette;
        _fadeLevel = 256;
        applyPalette();
    }
    // Shown at once, with the animations as they are, and screen updates resume.
    _workBufferStale = true;
    renderAnims(false);
    draw::blit(_screen, _workBuffer, 0, 0);
    _screenFrozen = false;
    _system->copyRectToScreen(_screen);
    updateCaption();
}

void Game::playSound(int index, const Argument *mode) {
    audio::Mixer *mixer = _system->getMixer();
    const auto &list = _scene->lists[kListSounds];
    if (!mixer || index < 0 || index >= int(list.size()))
        return;
    int kind = mode && mode->type == kArgSymbol ? int(mode->value & 0xFF) : -1;
    if (kind == 2)
        return;
    if (kind == 0) {
        // AMBIENT: loops until the scene changes; at most two at a time.
        _ambients.erase(std::remove_if(_ambients.begin(), _ambients.end(),
                                       [&](audio::SoundHandle h) { return !mixer->isPlaying(h); }),
                        _ambients.end());
        if (_ambients.size() >= kMaxAmbients) {
            log::debug(1, "Too many ambient sounds; %s not played", list[index].name.c_str());
            return;
        }
    }
    std::vector<uint8_t> data;
    if (!_resources.loadSound(list[index].name, data)) {
        log::debug(1, "Sound %s not found", list[index].name.c_str());
        return;
    }
    audio::SoundHandle h = mixer->play(makeRawStream(std::move(data), kind == 0 ? 0 : 1), audio::SoundType::kEffects);
    if (kind == 0) {
        _ambients.push_back(h);
        _ambientSounds.push_back(index);
    } else if (kind == 1)
        _oneShots.push_back(h); // ASNEEDED sounds hold up the next scene change
}

std::string Game::listName(ListKind kind, int index) const {
    const auto &list = _scene->lists[kind];
    return index >= 0 && index < int(list.size()) ? list[index].name : std::string("?");
}

// ---- Input ------------------------------------------------------------------

int Game::findHotspotAt(int x, int y) const {
    // The lowest enabled hotspot wins; edges are inclusive (0x18DAB).
    for (int i = 0; i < int(_hotspots.size()); i++) {
        const Hotspot &h = _hotspots[i];
        if (h.defined && h.enabled && x >= h.x && x <= h.x + h.w && y >= h.y && y <= h.y + h.h)
            return i;
    }
    return -1;
}

void Game::handleEvent(const InputEvent &event) {
    if (_credits.active) {
        // Esc ends the scroll.
        if (event.type == InputEvent::kQuit)
            _quit = true;
        else if (event.type == InputEvent::kKeyDown && event.key == key::kEscape &&
                 _credits.phase != CreditsScreen::kFadeOut) {
            _credits.phase = CreditsScreen::kFadeOut;
            _credits.phaseStart = _millis;
        }
        return;
    }
    if (_deckMap) {
        if (event.type == InputEvent::kQuit)
            _quit = true;
        else if (event.type == InputEvent::kKeyDown &&
                 (event.key == key::kEscape || (event.key == 'm' && !_vm->getOptions().original)))
            _deckMap = false;
        return;
    }
    if (_menu.active) {
        handleMenuEvent(event);
        return;
    }
    switch (event.type) {
    case InputEvent::kQuit:
        _quit = true;
        break;
    case InputEvent::kKeyDown:
        if (event.key == key::kEscape) {
            // Esc skips a video; in a scene it also opens the game menu (an added
            // convenience: the original only uses the right button).
            if (_video.active) {
                finishVideo(true);
                if (_thread.active)
                    runThread();
                switchScenes();
            } else if (!_arcade && !_conv.active && !_vm->getOptions().original) {
                requestMenu();
            }
        } else if (event.key == 'm') {
            // The deck map, where the game allows it.
            if (_mapEnabled && !_thread.active && !_conv.active && !_arcade && !_video.active && _mapLocation != 0xFF)
                openDeckMap();
        } else if (event.key == (key::kF1 + 1) && _vm->getOptions().debug) {
            _showHotspots = !_showHotspots;
        } else if (event.key == (key::kF1 + 9) && _vm->getOptions().debug) {
            std::printf("Variables set in %s:\n", _scene->name.c_str());
            for (const auto &v : _variables)
                if (v.second)
                    std::printf("  %-16s %u\n", v.first.c_str(), v.second);
        }
        break;
    case InputEvent::kMouseMove: {
        _mouse = event.mouse;
        if (_conv.menuWaiting) {
            Point m = cursorHitPoint(true);
            _conv.hover = menuEntryAt(m.x, m.y);
        }
        Point p = cursorHitPoint(false);
        if (findHotspotAt(p.x, p.y) != _hoverHotspot) {
            _hoverHotspot = findHotspotAt(p.x, p.y);
            updateCaption();
        }
        break;
    }
    case InputEvent::kLeftDown: {
        _mouse = event.mouse;
        if (_arcade) {
            _arcadeLeft = true; // phaser
            break;
        }
        if (_video.active)
            break; // only Esc skips a video
        if (_conv.active) {
            handleConversationClick(_mouse.x, _mouse.y);
            if (!_conv.active && _thread.active)
                runThread();
            break;
        }
        // Clicks count only while the script is idle; they are not queued.
        if (_thread.active || _pendingScene >= 0)
            break;
        Point p = cursorHitPoint(false);
        int i = findHotspotAt(p.x, p.y);
        if (i >= 0) {
            log::debug(1, "Hotspot %s", listName(kListHotspots, i).c_str());
            _status.clear();
            jumpToLabel(_hotspots[i].label);
            runThread();
            _hoverHotspot = findHotspotAt(_mouse.x, _mouse.y);
            updateCaption();
        }
        break;
    }
    case InputEvent::kRightDown:
        _mouse = event.mouse;
        if (_arcade) {
            _arcadeRight = true; // torpedoes
        } else if (_conv.active) {
            // In the reply menu: the conversation ends (rudely) and the menu opens;
            // resuming runs CONVERSE again.
            if (_conv.menuWaiting && _mapEnabled) {
                uint32_t converse = _actionSeqPc;
                _conv.menuWaiting = false;
                abortConversation(true, -1);
                _thread.active = true;
                _thread.pc = converse;
                _thread.wait = kWaitNone;
                openMenu(false);
            }
        } else if (!_video.active) {
            requestMenu();
        }
        break;
    default:
        break;
    }
}

// ---- Clock --------------------------------------------------------------------

void Game::step() {
    _stepCount++;
    _millis += kStepMillis;
    updateTick();

    if (_credits.active) {
        updateCredits();
        return;
    }
    if (_deckMap) {
        // The game waits while the map is up.
        draw::blit(_screen, _deckMapFrame, 0, 0);
        return;
    }
    if (_menu.active) {
        // The game waits; the menu redraws for the name entry's blinking. The
        // cursor goes over a fresh copy of the menu picture every step.
        if (_menu.mode == GameMenu::kNameEntry && _stepCount % 10 == 0)
            drawMenuScreen();
        draw::blit(_screen, _menu.frame, 0, 0);
        drawCursor(_screen);
        return;
    }

    if (_arcade) {
        updateArcade();
        if (!_arcade && !_video.active && _thread.active)
            runThread();
        draw::blit(_cleanScreen, _screen, 0, 0);
        if (_arcade)
            drawCursor(_screen);
        switchScenes();
        return;
    }
    if (_video.active) {
        // Nothing else runs while a video plays.
        updateVideo();
        if (!_video.active && _thread.active)
            runThread();
        switchScenes();
        return;
    }

    // Erase, draw and advance the animations in the work buffer.
    renderAnims(true);
    draw::blit(_screen, _workBuffer, 0, 0);
    if (_showHotspots) {
        uint8_t on = uint8_t(_palette.findBestColor(0, 255, 0));
        uint8_t off = uint8_t(_palette.findBestColor(255, 0, 0));
        for (const Hotspot &h : _hotspots)
            if (h.defined && h.w > 0 && h.h > 0)
                draw::frameRect(_screen, h.x, h.y, h.w + 1, h.h + 1, h.enabled ? on : off);
    }
    drawMenu(_screen);
    updateFade();
    cyclePalette();

    // Timers tick while the script idles or waits for an animation.
    bool idle = !_thread.active || _thread.wait == kWaitAnim;
    if (_stepCount % kStepsPerTimerTick == 0 && idle && _pendingScene < 0)
        tickTimers();

    if (_conv.active)
        updateConversation();
    if (_thread.active && !_conv.active && !_video.active)
        runThread();
    switchScenes();

    // The hotspot under the cursor may change without the mouse moving.
    Point p = cursorHitPoint(false);
    int hover = findHotspotAt(p.x, p.y);
    if (hover != _hoverHotspot) {
        _hoverHotspot = hover;
        updateCaption();
    }
    draw::blit(_cleanScreen, _screen, 0, 0);
    if (!_video.active)
        drawCursor(_screen);
}

void Game::tickTimers() {
    // One tick: count down until the first timer fires (0x23E40).
    for (Timer &t : _timers) {
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
    if (!_fade.active)
        return;
    int step = int((_millis - _fade.startMillis) / kFadeStepMillis);
    if (step >= _fade.steps) {
        _fade.active = false;
        _fadeLevel = _fade.in ? 256 : 0;
    } else {
        int level = step * 256 / _fade.steps;
        _fadeLevel = _fade.in ? level : 256 - level;
    }
    applyPalette();
}

void Game::applyPalette() {
    uint8_t colors[768];
    const uint8_t *src = _palette.data();
    for (int i = 0; i < 768; i++)
        colors[i] = uint8_t(src[i] * _fadeLevel / 256);
    _system->setPalette(colors, 0, 256);
}

void Game::updateCaption() {
    // The details are for debugging; the game itself only has its name.
    if (!_vm->getOptions().debug) {
        _system->setWindowTitle("Reluva - Star Trek: Deep Space Nine - Harbinger");
        return;
    }
    std::string caption = "Reluva - Harbinger | " + (_scene ? _scene->name : std::string());
    if (_hoverHotspot >= 0 && _hoverHotspot < int(_hotspots.size()))
        caption += text::format(" | %s [%s]", listName(kListHotspots, _hoverHotspot).c_str(),
                                getCursorName(_hotspots[_hoverHotspot].cursor));
    if (!_status.empty())
        caption += " | " + _status;
    _system->setWindowTitle(caption);
}

} // namespace reluva::stharbinger
