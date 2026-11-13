// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/game/generations.h"
#include "common/bytes.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#include "engines/stgenerations/audio.h"
#include "engines/stgenerations/debug.h"
#include "common/serializer.h"
#include "engines/stgenerations/platform.h"
#include "engines/stgenerations/game/dissolve.h"
#include "engines/stgenerations/formats/locations.h"
#include "video/avi.h"

namespace reluva::stgenerations {

namespace {

const uint32_t kFrameMillis = 1000 / 60;

const double kPi = 3.14159265358979323846;
// Walking: turning in radians a second, looking in pitch a second.
const double kTurnSpeed = 2.0;
const double kLookSpeed = 0.8;

} // namespace

Generations::Generations(Platform *system, Options options)
    : _system(system), _eventMan(system), _options(std::move(options)), _random(0) {
    if (audio::Mixer *mixer = _system->getMixer()) {
        _mixerWrapper = std::make_unique<Mixer>(*mixer);
        _mixer = _mixerWrapper.get();
        _mixer->setEnabled(_system->hasSoundOutput());
    }
}

Generations::~Generations() {
    if (_mixer)
        _mixer->stopAll();
}

void Generations::pauseEngine(bool pause) {
    if (pause)
        _pauseLevel++;
    else if (_pauseLevel > 0)
        _pauseLevel--;
    if (((pause && _pauseLevel == 1) || (!pause && _pauseLevel == 0)) && _mixer)
        _mixer->mixer().setAllPaused(pause);
}

Error Generations::run() {
    _screen.reset(new Screen(_system));
    _screen->init();
    initPalette();
    initCursor();

    _resources.addSearchPaths(_options.gameDir, _options.cd1, _options.cd2);
    _resources.checkDataFiles();
    _resources.openArchives();
    _resources.scan();
    _resources.logInventory();
    loadFont();
    for (const Location &location : getLocations()) {
        ResourceRef ref;
        if (findLocationLevel(_resources, location, ref))
            _missions.push_back({location.folder, ref});
    }

    // Found by its executable alone: it may hold what this doesn't know.
    if (_options.unknownVariant)
        log::warning("This version of %s is not known; it may not run as it should", _options.description.c_str());

    log::debug(1, "%d images and %d sounds found", (int)_resources.getImages().size(),
               (int)_resources.getSounds().size());

    // The volumes, whatever runs: the game, or the data viewer.
    loadSettings();
    if (Error err = start())
        return err;

    uint32_t lastFrame = _system->getMillis();
    while (!shouldQuit()) {
        Event event;
        while (_eventMan->pollEvent(event))
            handleEvent(event);

        uint32_t now = _system->getMillis();
        uint32_t delta = now - lastFrame;
        lastFrame = now;

        if (!isPaused())
            drawFrame(delta);

        uint32_t elapsed = _system->getMillis() - now;
        if (elapsed < kFrameMillis)
            _system->delayMillis(kFrameMillis - elapsed);
    }

    if (_mixer)
        _mixer->stopAll();
    return Error::success();
}

void Generations::initPalette() {
    Palette &pal = _screen->getPalette();
    pal.setColor(kColorBlack, 0, 0, 0);
    for (int i = kColorStarFirst; i <= kColorStarLast; i++) {
        uint8_t v = (uint8_t)(i * 255 / kColorStarLast);
        pal.setColor(i, v, v, (uint8_t)std::min(255, v + 24));
    }
    for (int r = 0; r < 6; r++) {
        for (int g = 0; g < 6; g++) {
            for (int b = 0; b < 6; b++)
                pal.setColor(kColorCubeFirst + r * 36 + g * 6 + b, (uint8_t)(r * 51), (uint8_t)(g * 51),
                             (uint8_t)(b * 51));
        }
    }
    // LCARS-ish orange for the frame.
    pal.setColor(kColorFrame, 255, 153, 0);
    pal.setColor(kColorWhite, 255, 255, 255);
    _screen->applyPalette();
}

void Generations::initCursor() {
    _targetCursor = true;
    setCursor(false);
    _system->showMouse(true);
}

void Generations::setCursor(bool target) {
    // A crosshair to aim with (the original's target.bmp) or an arrow
    // (select.bmp), drawn in the palette's white on its black. Index 0 is
    // transparent.
    if (target == _targetCursor)
        return;
    _targetCursor = target;
    const Palette &palette = _screen->getPalette();
    uint8_t white = palette.findBestColor(255, 255, 255);
    uint8_t black = palette.findBestColor(0, 0, 0);
    if (!white)
        white = 1;
    if (!black)
        black = white;
    const int size = 15;
    uint8_t cursor[size * size] = {};
    if (target) {
        for (int i = 0; i < size; i++) {
            if (i == size / 2)
                continue;
            cursor[(size / 2) * size + i] = white;
            cursor[i * size + size / 2] = white;
        }
        _system->setMouseCursor(cursor, size, size, size / 2, size / 2, 0);
        return;
    }
    // An arrow 11 rows tall, outlined.
    for (int y = 0; y < 11; y++) {
        int width = y < 8 ? y + 1 : 11 - y;
        for (int x = 0; x < width; x++)
            cursor[y * size + x] = x == 0 || x == width - 1 || y == 10 ? black : white;
    }
    _system->setMouseCursor(cursor, size, size, 0, 0, 0);
}

void Generations::loadFont() {
    ResourceRef ref = {"COMMON.DAT", "FONTS/ACTION.FNT"};
    std::unique_ptr<SeekableReadStream> stream = _resources.open(ref);
    if (!stream || !_font.load(*stream))
        log::warning("Could not load %s; text will not be shown", ref.toString().c_str());

    // The font's colour indices are assumed to refer to the shared palette
    // used by the images in COMMON.DAT.
    _hasFontPalette = _resources.loadArchivePalette("COMMON.DAT", _fontPalette);
    if (!_hasFontPalette)
        log::warning("No palette found in COMMON.DAT; text colours will be approximate");
}

void Generations::handleEvent(const Event &event) {
    if (_menuOpen) {
        handleMenuEvent(event);
        return;
    }
    if (_combat) {
        switch (event.type) {
        case kEventKeydown:
            _combat->keyDown(event.kbd);
            break;
        case kEventKeyup:
            _combat->keyUp(event.kbd);
            break;
        case kEventMousemove:
            _combat->mouseMove(event.mouse.x, event.mouse.y);
            break;
        case kEventLbuttondown:
            _combat->mouseDown(event.mouse.x, event.mouse.y);
            break;
        case kEventLbuttonup:
            _combat->mouseUp(event.mouse.x, event.mouse.y);
            break;
        default:
            break;
        }
        return;
    }
    if (_bridgeMode) {
        switch (event.type) {
        case kEventKeydown:
            _bridge->keyDown(event.kbd);
            break;
        case kEventMousemove:
            _bridge->mouseMove(event.mouse.x, event.mouse.y);
            break;
        case kEventLbuttondown:
            _bridge->mouseDown(event.mouse.x, event.mouse.y);
            break;
        case kEventLbuttonup:
            _bridge->mouseUp(event.mouse.x, event.mouse.y);
            break;
        default:
            break;
        }
        return;
    }
    if (_awaitingProceed) {
        switch (event.type) {
        case kEventMousemove:
            _mouseX = event.mouse.x;
            _mouseY = event.mouse.y;
            return;
        case kEventLbuttondown:
            _proceedPressed = isOverProceed(event.mouse.x, event.mouse.y);
            return;
        case kEventLbuttonup:
            if (_proceedPressed && isOverProceed(event.mouse.x, event.mouse.y))
                proceed();
            _proceedPressed = false;
            return;
        case kEventKeydown:
            if (event.kbd.keycode == reluva::key::kReturn || event.kbd.keycode == reluva::key::kKpEnter ||
                event.kbd.keycode == reluva::key::kSpace) {
                proceed();
                return;
            }
            if (event.kbd.keycode != reluva::key::kEscape)
                return;
            break;
        default:
            if (event.type != kEventFocusLost && event.type != kEventFocusGained)
                return;
            break;
        }
    }
    bool walking = _level && _levelPage == kLevelPage3D;
    bool bracket = event.kbd.keycode == '[' || event.kbd.keycode == ']';
    if (walking && bracket && !(event.kbd.flags & kKbdShift) && _mission &&
        (event.type == kEventKeydown || event.type == kEventKeyup)) {
        // The weapon's power level (0x44A1B7 and 0x44A159), if it has one;
        // with Shift, or without, they change pages.
        const Mission::Item *item = _mission->getItem(_mission->getActiveItem());
        if (item && item->maxLevel > 0) {
            if (event.type == kEventKeydown) {
                _mission->setItemLevel(item->level + (event.kbd.keycode == ']' ? 1 : -1));
                updateWalkStatus();
            }
            return;
        }
    }
    if (walking && (event.type == kEventKeydown || event.type == kEventKeyup) &&
        handleWalkKey(event.kbd, event.type == kEventKeydown))
        return;

    switch (event.type) {
    case kEventKeydown:
        keyDown(event.kbd);
        break;
    case kEventMousemove:
        _mouseX = event.mouse.x;
        _mouseY = event.mouse.y;
        if (_hudScreen)
            _hudScreen->mouseMove(event.mouse.x, event.mouse.y);
        break;
    case kEventLbuttonup:
        if (_hudScreen && _mission)
            _hudScreen->mouseUp(*_mission, event.mouse.x, event.mouse.y);
        viewButton(1, false);
        break;
    case kEventRbuttonup:
        viewButton(2, false);
        break;
    case kEventMbuttonup:
        viewButton(4, false);
        break;
    case kEventMbuttondown:
        if (_level && _levelPage == kLevelPage3D && event.mouse.y < HudScreen::kView.bottom)
            viewButton(4, true);
        break;
    case kEventLbuttondown:
    case kEventRbuttondown:
        _mouseX = event.mouse.x;
        _mouseY = event.mouse.y;
        if (_level && _levelPage == kLevelPage3D) {
            bool right = event.type == kEventRbuttondown;
            if (_hudScreen && _mission && event.mouse.y >= HudScreen::kView.bottom)
                _hudScreen->mouseDown(*_mission, event.mouse.x, event.mouse.y, right);
            else
                viewButton(right ? 2 : 1, true);
            break;
        }
        if (event.type == kEventLbuttondown)
            click(event.mouse.x, event.mouse.y);
        break;
    case kEventFocusLost:
        releaseWalkKeys();
        pauseEngine(true);
        break;
    case kEventFocusGained:
        pauseEngine(false);
        _dirty = true;
        break;
    default:
        break;
    }
}

void Generations::drawFrame(uint32_t delta) {
    // Clear the subtitle once its line has finished playing.
    if (_missionSubtitle) {
        if (_levelTime >= _subtitleUntil) {
            _subtitle.clear();
            _missionSubtitle = false;
        }
    } else if (!_subtitle.empty() && !_mixer->isSoundHandleActive(_soundHandle)) {
        _subtitle.clear();
        _dirty = true;
    }

    if (_menuOpen) {
        if (_dirty) {
            _menu->draw(_screen->getSurface());
            _screen->update();
            _dirty = false;
        }
        return;
    }
    if (_combat) {
        updateCombat(delta);
        return;
    }
    if (_bridgeMode) {
        updateBridge(delta);
        return;
    }
    if (_playingMission && _mission && _mission->isFinished()) {
        missionOver();
        return;
    }
    if (_awaitingProceed && _mission) {
        // The original's loading screen: the briefing until PROCEED.
        drawBriefing(_screen->getSurface(), true, _proceedPressed && isOverProceed(_mouseX, _mouseY));
        _screen->getPalette() = _briefing.getPalette();
        _screen->applyPalette();
        _paletteBlended = true;
        setCursor(false);
        _screen->update();
        _dirty = false;
        return;
    }
    if (_level && _levelPage == kLevelPage3D && _hudScreen) {
        drawMissionScreen(delta);
        _screen->update();
        _dirty = false;
        return;
    }
    if (_level && _levelPage == kLevelPage3D) {
        _levelTime += delta;
        updateWalk(delta);
        Canvas view;
        view.create(Screen::kWidth, Screen::kHeight - kTextPanelHeight, Canvas::kClut8);
        _actorSprites.clear();
        if (_actors)
            _actors->getSprites(_camera.x, _camera.y, _walker->getActiveZones(), _actorSprites);
        if (_mission)
            _mission->getProjectiles().getSprites(_actorSprites);
        prepareAim(view.w, view.h);
        _levelRenderer->render(_camera, view, _levelTime);
        drawHands(view, delta);
        if (_mission && _mission->getHealth() != _shownHealth)
            updateWalkStatus();
        _screen->getSurface().copyRectToSurface(view, 0, 0, Rect(0, 0, view.w, view.h));
        _screen->getSurface().fillRect(Rect(0, view.h, Screen::kWidth, Screen::kHeight),
                                       _screen->getPalette().findBestColor(0, 0, 0));
        applyScreenEffects();
    } else if (!drawPage(delta)) {
        return;
    }

    drawTextPanel();
    _screen->update();
    _dirty = false;
}

void Generations::drawMissionScreen(uint32_t delta) {
    _levelTime += delta;
    updateWalk(delta);

    // The view is the top 640 x 252 (the original renders 640 x 320 and
    // shows the middle, the same picture).
    Canvas view;
    view.create(HudScreen::kView.width(), HudScreen::kView.height(), Canvas::kClut8);
    _actorSprites.clear();
    if (_actors)
        _actors->getSprites(_camera.x, _camera.y, _walker->getActiveZones(), _actorSprites);
    _mission->getProjectiles().getSprites(_actorSprites);
    prepareAim(view.w, view.h);
    _levelRenderer->render(_camera, view, _levelTime);
    drawHands(view, delta);

    // Shaking moves the whole view; the underwater ripple (VIEW_OVERLAY)
    // shifts each row, a wave 168 rows long rolling down a row every 64 ms.
    Hud &hud = _mission->getHud();
    int shakeX;
    int shakeY;
    hud.getShakeOffset(shakeX, shakeY);
    Canvas &screen = _screen->getSurface();
    uint8_t black = _screen->getPalette().findBestColor(0, 0, 0);
    for (int y = 0; y < view.h; y++) {
        int dx = shakeX;
        if (hud.hasRipple())
            dx += (int)std::lround(5.5 * std::sin(2 * 3.14159265358979 * (y + _levelTime / 64.0) / 168) + 0.5);
        int sy = y - shakeY;
        uint8_t *out = (uint8_t *)screen.getBasePtr(0, y);
        const uint8_t *src = sy >= 0 && sy < view.h ? (const uint8_t *)view.getBasePtr(0, sy) : nullptr;
        for (int x = 0; x < view.w; x++) {
            int sx = x - dx;
            out[x] = src && sx >= 0 && sx < view.w ? src[sx] : black;
        }
    }

    _hudScreen->draw(screen, *_mission, *_level, _walker->getX(), _walker->getY(), _walker->getFeet(), _camera.yaw,
                     _levelTime);
    _hudScreen->drawDrag(screen, *_mission);
    if (_mission->getHealth() != _shownHealth)
        updateWalkStatus();

    // One line of text: what is said, or else the status.
    if (_font.isLoaded()) {
        const Palette &pal = _screen->getPalette();
        uint8_t remap[256];
        for (int i = 0; i < 256; i++) {
            uint8_t r;
            uint8_t g;
            uint8_t b;
            if (_hasFontPalette)
                _fontPalette.getColor(i, r, g, b);
            else
                r = g = b = (uint8_t)i;
            remap[i] = pal.findBestColor(r, g, b);
        }
        const std::string &text = _subtitle.empty() ? _status : _subtitle;
        std::vector<std::string> lines = _font.wordWrap(text, HudScreen::kTextLine.width());
        if (!lines.empty())
            _font.drawString(screen, lines[0], HudScreen::kTextLine.left, HudScreen::kTextLine.top, remap);
    }
    applyScreenEffects();
}

void Generations::applyScreenEffects() {
    if (!_mission || !_level)
        return;
    // 0x43199A: each slot blends every colour towards its own, slot 0 last.
    Hud &hud = _mission->getHud();
    bool any = false;
    Palette blended = _level->getPalette();
    for (int slot = Hud::kEffectSlots - 1; slot >= 0; slot--) {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        int k = hud.getEffectWeight(slot, r, g, b);
        if (k <= 0)
            continue;
        any = true;
        for (int i = 0; i < 256; i++) {
            uint8_t pr;
            uint8_t pg;
            uint8_t pb;
            blended.getColor(i, pr, pg, pb);
            blended.setColor(i, (uint8_t)((pr * (128 - k) + r * k) >> 7), (uint8_t)((pg * (128 - k) + g * k) >> 7),
                             (uint8_t)((pb * (128 - k) + b * k) >> 7));
        }
    }

    // A transition (0x91-0x93): the briefing, or the game, dissolving.
    Mission::TransitionView transition = _mission->getTransitionView();
    if (transition.active) {
        any = true;
        drawTransition(transition, blended);
    } else if (!_transitionFrom.empty()) {
        _transitionFrom.free();
    }
    if (any || _paletteBlended) {
        _screen->getPalette() = blended;
        _screen->applyPalette();
    }
    _paletteBlended = any;
}

void Generations::drawTransition(const Mission::TransitionView &transition, Palette &palette) {
    Canvas &screen = _screen->getSurface();
    bool briefing = transition.kind == Mission::TransitionView::kBriefing && _briefingLoaded;
    if (transition.phase == 1 && briefing) {
        // The briefing in its own colours, speckled.
        drawBriefing(screen, _proceedLoaded, false);
        palette = _briefing.getPalette();
        dissolve::speckle(screen, palette.findBestColor(transition.r, transition.g, transition.b), transition.amount,
                          transition.style, _dissolveRandom);
        return;
    }
    uint8_t colour = palette.findBestColor(transition.r, transition.g, transition.b);
    uint8_t black = palette.findBestColor(0, 0, 0);
    bool ending = transition.kind == Mission::TransitionView::kEnding;
    auto copyScreen = [&screen](Canvas &to) {
        if (to.w != screen.w || to.h != screen.h)
            to.create(screen.w, screen.h, Canvas::kClut8);
        to.copyRectToSurface(screen, 0, 0, Rect(0, 0, screen.w, screen.h));
    };

    switch (transition.phase) {
    case 1:
        // The game speckled; it is what the crossfade starts from.
        copyScreen(_transitionFrom);
        dissolve::speckle(screen, colour, transition.amount, transition.style, _dissolveRandom);
        break;
    case 2: {
        // The first picture to the second (the game, or black for the
        // ending), under a quarter's speckles.
        Canvas to;
        copyScreen(to);
        if (ending)
            to.fillRect(Rect(0, 0, to.w, to.h), black);
        if (briefing) {
            // The briefing's colours brought into the game's.
            Palette from = _briefing.getPalette();
            uint8_t remap[256];
            for (int i = 0; i < 256; i++) {
                uint8_t r;
                uint8_t g;
                uint8_t b;
                from.getColor(i, r, g, b);
                remap[i] = palette.findBestColor(r, g, b);
            }
            drawBriefing(screen, _proceedLoaded, false);
            for (int y = 0; y < screen.h; y++) {
                uint8_t *row = (uint8_t *)screen.getBasePtr(0, y);
                for (int x = 0; x < screen.w; x++)
                    row[x] = remap[row[x]];
            }
        } else if (_transitionFrom.w == screen.w && _transitionFrom.h == screen.h) {
            screen.copyRectToSurface(_transitionFrom, 0, 0, Rect(0, 0, screen.w, screen.h));
        }
        dissolve::crossfade(screen, screen, to, transition.amount, _dissolveRandom);
        dissolve::speckle(screen, colour, 0x100, transition.style, _dissolveRandom);
        break;
    }
    case 3:
        // The second picture clearing.
        if (ending)
            screen.fillRect(Rect(0, 0, screen.w, screen.h), black);
        dissolve::speckle(screen, colour, transition.amount, transition.style, _dissolveRandom);
        break;
    default:
        if (ending)
            screen.fillRect(Rect(0, 0, screen.w, screen.h), black);
        break;
    }
}

void Generations::drawBriefing(Canvas &screen, bool withProceed, bool pressed) {
    const Canvas &picture = _briefing.getSurface();
    screen.fillRect(Rect(0, 0, screen.w, screen.h), 0);
    screen.copyRectToSurface(picture, std::max(0, (screen.w - picture.w) / 2), std::max(0, (screen.h - picture.h) / 2),
                             Rect(0, 0, std::min<int>(picture.w, screen.w), std::min<int>(picture.h, screen.h)));
    if (!withProceed)
        return;
    // PROCEED.BMP: pressed above, normal below; magenta is see-through.
    const Canvas &button = _proceed.getSurface();
    const Palette &from = _proceed.getPalette();
    const Palette &to = _briefing.getPalette();
    int height = button.h / 2;
    for (int y = 0; y < height; y++) {
        int sy = pressed ? y : y + height;
        int dy = _proceedY + y;
        if (dy < 0 || dy >= screen.h)
            continue;
        const uint8_t *src = (const uint8_t *)button.getBasePtr(0, sy);
        uint8_t *out = (uint8_t *)screen.getBasePtr(0, dy);
        for (int x = 0; x < button.w; x++) {
            int dx = _proceedX + x;
            if (dx < 0 || dx >= screen.w)
                continue;
            uint8_t r;
            uint8_t g;
            uint8_t b;
            from.getColor(src[x], r, g, b);
            if (r == 255 && g == 0 && b == 255)
                continue;
            out[dx] = to.findBestColor(r, g, b);
        }
    }
}

bool Generations::isOverProceed(int x, int y) const {
    const Canvas &button = _proceed.getSurface();
    return x >= _proceedX && x < _proceedX + button.w && y >= _proceedY && y < _proceedY + button.h / 2;
}

void Generations::proceed() {
    _awaitingProceed = false;
    _proceedPressed = false;
    ResourceRef ref;
    if (findSoundFile("PROCEED.WAV", "COMMON.DAT", ref)) {
        if (std::unique_ptr<SoundStream> stream = _resources.loadSound(ref))
            _mixer->playStream(Mixer::kSFXSoundType, nullptr, std::move(stream));
    }
    if (!_mission)
        return;
    _mission->start();
    double yaw;
    if (_mission->takePlayerYaw(yaw))
        _camera.yaw = yaw;
}

void Generations::drawTextPanel() {
    if (!_font.isLoaded())
        return;

    // Translate the font's colours into whatever palette is current, so
    // text keeps its look over every image.
    const Palette &pal = _screen->getPalette();
    uint8_t remap[256];
    for (int i = 0; i < 256; i++) {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        if (_hasFontPalette)
            _fontPalette.getColor(i, r, g, b);
        else
            r = g = b = (uint8_t)i;
        remap[i] = pal.findBestColor(r, g, b);
    }
    uint8_t background = pal.findBestColor(0, 0, 0);
    uint8_t highlight = pal.findBestColor(255, 153, 0);

    Canvas &s = _screen->getSurface();
    int top = Screen::kHeight - kTextPanelHeight;
    s.fillRect(Rect(0, top, Screen::kWidth, Screen::kHeight), background);
    s.hLine(0, top, Screen::kWidth - 1, highlight);

    const int margin = 8;
    int y = top + 4;
    _font.drawString(s, _status, margin, y, remap);
    y += _font.getHeight();
    for (const std::string &line : _font.wordWrap(_subtitle, Screen::kWidth - 2 * margin)) {
        if (y + _font.getHeight() > Screen::kHeight)
            break;
        _font.drawString(s, line, margin, y, remap);
        y += _font.getHeight();
    }
}

void Generations::playMission(int index) {
    _missionChoice = index;
    _missionMessage.clear();
    _subtitle.clear();
    _missionSubtitle = false;
    showLevel(_missions[index].level);
    if (!_level)
        return;
    _playingMission = true;
    showLevelPage(kLevelPage3D);
}

void Generations::leaveMission(const std::string &message) {
    if (_mission)
        _carriedArray = _mission->getCarriedArray();
    _playingMission = false;
    _awaitingProceed = false;
    _subtitle.clear();
    _missionSubtitle = false;
    _missionMessage = message;
    log::debug(1, "%s", message.c_str());
    _mixer->stopAll();
    clearView();
}

void Generations::showLevel(const ResourceRef &ref, bool run) {
    std::unique_ptr<SeekableReadStream> stream = _resources.open(ref);
    _hudScreen.reset();
    _heldItem.load(_resources, _levelRef, 0);
    _mission.reset();
    _actors.reset();
    _walker.reset();
    _levelRenderer.reset();
    _level.reset(new LevelFile());
    if (!stream || !_level->load(*stream)) {
        _level.reset();
        initPalette();
        _status = ref.toString() + "  (could not decode)";
        return;
    }
    _levelName = ref.toString();
    _levelRef = ref;
    _punching = false;
    _punchImage.clear();
    _levelTextures = _level->getTextureIds();
    _levelRenderer.reset(new LevelRenderer(*_level));
    _walker.reset(new Walker(*_level));
    loadActors(ref);
    _camera = _levelRenderer->getStartCamera(Walker::kEyeHeight);
    _walker->place(_camera.x, _camera.y, _camera.z - Walker::kEyeHeight);
    // The mission's briefing picture, which waits for PROCEED and then
    // gives way to the game in the opening transition.
    {
        size_t slash = ref.name.find_last_of("/\\");
        std::string folder = ref.name.substr(0, slash + 1);
        ResourceRef briefing = {ref.archive, folder + "BRIEFING.BMP"};
        _briefingLoaded = _proceedLoaded = false;
        for (const ResourceRef &image : _resources.getImages()) {
            if (image.archive == briefing.archive && text::toLower(image.name) == text::toLower(briefing.name))
                _briefingLoaded = _resources.loadImage(image, _briefing) && _briefing.hasPalette();
            else if (text::toLower(image.archive) == "common.dat" && text::toLower(image.name) == "proceed.bmp")
                _proceedLoaded = _resources.loadImage(image, _proceed) && _proceed.hasPalette();
        }
        _proceedX = _proceedY = 0;
        std::string name = text::toLower(folder.substr(0, folder.empty() ? 0 : folder.size() - 1));
        name = name.substr(name.find_last_of("/\\") == std::string::npos ? 0 : name.find_last_of("/\\") + 1);
        bool placed = false;
        for (const Location &location : getLocations()) {
            if (text::toLower(location.folder) == name) {
                _proceedX = location.proceedX;
                _proceedY = location.proceedY;
                placed = true;
            }
        }
        _proceedLoaded = _proceedLoaded && placed;
    }
    bool wait = run && _briefingLoaded && _proceedLoaded;
    startMission(ref, run && !wait);
    _awaitingProceed = wait && _mission;
    _proceedPressed = false;
    _hudScreen.reset();
    _heldItem.load(_resources, _levelRef, 0);
    if (_mission) {
        _hudScreen.reset(new HudScreen(_resources, ref, *_levelScript, _level->getPalette()));
        if (!_hudScreen->isAvailable())
            _hudScreen.reset();
        _levelRenderer->setMapWalls(&_level->getWalls());
    }
    _camera.x = _walker->getX();
    _camera.y = _walker->getY();
    _camera.z = _walker->getEye();
    _levelRenderer->setVisibleZones(_walker->getActiveZones());
    _levelTime = 0;
    _screen->getPalette() = _level->getPalette();
    _screen->applyPalette();
    showLevelPage(0);
}

void Generations::loadActors(const ResourceRef &levelRef) {
    // Sprite sets are NNN.gs2 next to GAME.LV2 (0x4C01B0).
    size_t slash = levelRef.name.find_last_of("/\\");
    std::string folder = slash == std::string::npos ? "" : levelRef.name.substr(0, slash + 1);
    bool upper = !levelRef.name.empty() && std::isupper((unsigned char)levelRef.name.back());
    auto loader = [this, levelRef, folder, upper](uint16_t number, SpriteSet &set) {
        ResourceRef ref = {levelRef.archive, folder + text::format(upper ? "%03u.GS2" : "%03u.gs2", number)};
        std::unique_ptr<SeekableReadStream> stream = _resources.open(ref);
        if (!stream || !set.load(*stream)) {
            log::warning("Could not load the sprite set %s", ref.toString().c_str());
            return false;
        }
        return true;
    };
    Walker prototype(*_walker);
    _actors.reset(new Actors(*_level, prototype, loader));
    _walker->setObstacles(&_actors->getObstacles());
    _levelRenderer->setSprites(&_actorSprites);
}

bool Generations::findSoundFile(const std::string &name, const std::string &archivePrefix, ResourceRef &ref) const {
    // Subtitles sit next to their WAVs, so look for the WAV and swap the extension.
    std::string base = text::toLower(name.substr(0, name.rfind('.')));
    std::string extension = name.substr(name.rfind('.'));
    const ResourceRef *fallback = nullptr;
    for (const ResourceRef &sound : _resources.getSounds()) {
        size_t slash = sound.name.find_last_of("/\\");
        std::string file = sound.name.substr(slash == std::string::npos ? 0 : slash + 1);
        if (text::toLower(file.substr(0, file.rfind('.'))) != base)
            continue;
        if (text::toLower(sound.archive).compare(0, archivePrefix.size(), text::toLower(archivePrefix)) == 0) {
            fallback = &sound;
            break;
        }
        if (!fallback)
            fallback = &sound;
    }
    if (!fallback)
        return false;
    ref = *fallback;
    ref.name = ref.name.substr(0, ref.name.rfind('.')) + extension;
    return true;
}

void Generations::startMission(const ResourceRef &levelRef, bool run) {
    // A level's scripts are in GAME.BIN next to its GAME.LV2.
    ResourceRef ref = levelRef;
    size_t dot = ref.name.rfind('.');
    if (dot == std::string::npos)
        return;
    bool upper = dot + 1 < ref.name.size() && std::isupper((unsigned char)ref.name[dot + 1]);
    ref.name = ref.name.substr(0, dot) + (upper ? ".BIN" : ".bin");
    std::unique_ptr<SeekableReadStream> stream = _resources.open(ref);
    if (!stream)
        return;
    _levelScript.reset(new ScriptFile());
    if (!_levelScript->load(*stream)) {
        log::warning("Could not decode %s; the level will run without its scripts", ref.toString().c_str());
        _levelScript.reset();
        return;
    }
    // Sound files: a mission's own in <mission>/sounds, the rest found by
    // name in COMMON.DAT or the MUSIC archives.
    std::string folder = levelRef.name.substr(0, levelRef.name.find_last_of("/\\") + 1);
    auto openSound = [this, levelRef, folder](const std::string &name,
                                              MissionSound::Location where) -> std::unique_ptr<SeekableReadStream> {
        if (where == MissionSound::kLocationMission) {
            ResourceRef ref = {levelRef.archive, folder + "sounds/" + name};
            if (std::unique_ptr<SeekableReadStream> stream = _resources.open(ref))
                return stream;
        }
        ResourceRef ref;
        if (!findSoundFile(name,
                           where == MissionSound::kLocationMission  ? levelRef.archive
                           : where == MissionSound::kLocationCommon ? "COMMON.DAT"
                                                                    : "MUSIC.",
                           ref))
            return nullptr;
        return _resources.open(ref);
    };
    _mission.reset(new Mission(*_level, *_levelScript, *_levelRenderer, *_walker, _actors.get(), _mixer, openSound));
    _mission->setDifficulty(_settings.difficulty);
    _mission->setCarriedArray(_carriedArray);
    if (run)
        _mission->start();
    double yaw;
    if (_mission->takePlayerYaw(yaw))
        _camera.yaw = yaw;
    log::debug(1, "Running %s: %d script objects", ref.toString().c_str(), _mission->getVM().getInstanceCount());
}

void Generations::showLevelPage(int page) {
    if (!_level)
        return;
    int pages = (int)_levelTextures.size() + kLevelPageTextures;
    _levelPage = ((page % pages) + pages) % pages;
    if (_mission)
        _mission->getSound().pauseAll(_levelPage != kLevelPage3D);
    if (_paletteBlended && _levelPage != kLevelPage3D) {
        _screen->getPalette() = _level->getPalette();
        _screen->applyPalette();
        _paletteBlended = false;
    }
    for (bool &held : _walkKeys)
        held = false;

    if (_levelPage == kLevelPageMap) {
        _status = text::format("%s  map: %d sectors, %d walls, %d vertices  ([ ] 3D view, textures)",
                               _levelName.c_str(), (int)_level->getSectors().size(), (int)_level->getWalls().size(),
                               (int)_level->getVertices().size());
    } else if (_levelPage == kLevelPage3D) {
        updateWalkStatus();
    } else {
        LevelFile::Texture texture;
        int index = _levelPage - kLevelPageTextures;
        uint16_t id = _levelTextures[index];
        if (_level->loadTexture(id, texture))
            _status = text::format("%s  texture %d/%d (id %u) %dx%d", _levelName.c_str(), index + 1,
                                   (int)_levelTextures.size(), id, texture.surface.w, texture.surface.h);
        else
            _status = text::format("%s  texture %u (could not decode)", _levelName.c_str(), id);
    }
    _dirty = true;
}

bool Generations::handleWalkKey(const KeyState &state, bool down) {
    // The player's key map first (0x43E8FB), as the Controls panel sets it.
    uint8_t action = _settings.awayKeys.get(KeyMap::virtualKey(state.keycode));
    if (action != KeyMap::kNone && walkAction(action, down))
        return true;

    // Then this engine's own keys, for keys the map leaves free.
    uint32_t key = state.keycode;
    int walkKey;
    switch (key) {
    case 'w':
        walkKey = kWalkForward;
        break;
    case 's':
        walkKey = kWalkBack;
        break;
    case 'a':
        walkKey = kWalkStrafeLeft;
        break;
    case 'd':
        walkKey = kWalkStrafeRight;
        break;
    case reluva::key::kEnd:
        if (down)
            _camera.pitch = 0;
        return true;
    case 'c':
        // Shift+C walks through walls (for testing); C alone crouches.
        if (state.hasFlags(kKbdShift)) {
            if (down) {
                _walker->setCollisionEnabled(!_walker->isCollisionEnabled());
                updateWalkStatus();
            }
            return true;
        }
        walkKey = kWalkCrouch;
        break;
    case 'x':
        return walkAction(KeyMap::kAwayJump, down);
    case 'l':
        if (down) {
            _levelRenderer->setLightingEnabled(!_levelRenderer->isLightingEnabled());
            updateWalkStatus();
        }
        return true;
    case reluva::key::kSpace:
        // Use the open panel's object, as the original's Space does.
        if (down && _mission)
            _mission->getHud().usePanel();
        return true;
    case 'b':
    case 'h':
    case 'o':
        // The left panel: life, map, object (the original's B, H, A; A walks here).
        if (down && _hudScreen)
            _hudScreen->setLeftMode(key == 'b'   ? HudScreen::kLeftLife
                                    : key == 'h' ? HudScreen::kLeftMap
                                                 : HudScreen::kLeftObject);
        return true;
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        // Until there is an inventory panel: use the item in that slot.
        if (down && _mission) {
            _mission->useItem(key - '1');
            updateWalkStatus();
        }
        return true;
    default:
        return false;
    }
    _walkKeys[walkKey] = down;
    return true;
}

bool Generations::walkAction(int action, bool down) {
    int walkKey;
    switch (action) {
    case KeyMap::kAwayForward:
        walkKey = kWalkForward;
        break;
    case KeyMap::kAwayBackward:
        walkKey = kWalkBack;
        break;
    case KeyMap::kAwayLeft:
        walkKey = kWalkTurnLeft;
        break;
    case KeyMap::kAwayRight:
        walkKey = kWalkTurnRight;
        break;
    case KeyMap::kAwaySlideLeft:
        walkKey = kWalkStrafeLeft;
        break;
    case KeyMap::kAwaySlideRight:
        walkKey = kWalkStrafeRight;
        break;
    case KeyMap::kAwayLookUp:
        walkKey = kWalkLookUp;
        break;
    case KeyMap::kAwayLookDown:
        walkKey = kWalkLookDown;
        break;
    case KeyMap::kAwayCrouch:
        walkKey = kWalkCrouch;
        break;
    case KeyMap::kAwayRun:
        walkKey = kWalkRun;
        break;
    case KeyMap::kAwayJump:
        // Once per press.
        if (down && !_walkKeys[kWalkJump])
            _jumpPressed = true;
        walkKey = kWalkJump;
        break;
    case KeyMap::kAwayLookCenter:
        if (down)
            _camera.pitch = 0;
        return true;
    case KeyMap::kAwayAttack:
        // One attack per press (game+0x2E8 latches it until the key is up).
        if (down && !_fireHeld)
            attack();
        _fireHeld = down;
        return true;
    case KeyMap::kAwayMouseLook:
        // game.vt120 (0x440BB4): letting go centres the view with Lookspring.
        if (!down && _mouseLook && _settings.lookspring)
            _camera.pitch = 0;
        _mouseLook = down;
        return true;
    default:
        return false;
    }
    _walkKeys[walkKey] = down;
    return true;
}

void Generations::releaseWalkKeys() {
    for (bool &held : _walkKeys)
        held = false;
    _fireHeld = _mouseLook = false;
    _viewButtons = 0;
}

void Generations::viewButton(int vk, bool down) {
    // The view's buttons go through the key map as virtual keys 1, 2 and 4
    // (0x4420A7, 0x442123); a release counts only after a press in the view.
    int bit = 1 << vk;
    if (down)
        _viewButtons |= bit;
    else if (!(_viewButtons & bit))
        return;
    else
        _viewButtons &= ~bit;
    uint8_t action = _settings.awayKeys.get(vk);
    if (action != KeyMap::kNone)
        walkAction(action, down);
}

void Generations::updateWalk(uint32_t delta) {
    const double kSpeed = 25.0; // world units per second
    double seconds = std::min<uint32_t>(delta, 100) / 1000.0;

    double forward = (_walkKeys[kWalkForward] ? 1 : 0) - (_walkKeys[kWalkBack] ? 1 : 0);
    double strafe = (_walkKeys[kWalkStrafeRight] ? 1 : 0) - (_walkKeys[kWalkStrafeLeft] ? 1 : 0);
    double turn = (_walkKeys[kWalkTurnLeft] ? 1 : 0) - (_walkKeys[kWalkTurnRight] ? 1 : 0);
    double look = (_walkKeys[kWalkLookDown] ? 1 : 0) - (_walkKeys[kWalkLookUp] ? 1 : 0);
    // No moving while a transition holds the screen, nor without the
    // controls the scripts allow (SET_PLAYER_CONTROLS; see Mission::Control).
    // In the air, outside low gravity, only turning and looking (0x43AD04).
    bool lowGravity = _mission && _mission->isLowGravity();
    uint16_t controls = _mission ? _mission->getControls() : 0xFFFF;
    if (_walker->isAirborne() && !lowGravity)
        controls &= Mission::kControlTurn | Mission::kControlLook;
    if (_mission && _mission->isInTransition())
        controls = 0;
    if (!(controls & Mission::kControlWalk))
        forward = strafe = 0;
    if (!(controls & Mission::kControlTurn))
        turn = 0;
    if (!(controls & Mission::kControlLook))
        look = 0;
    _walker->crouch(_walkKeys[kWalkCrouch] && (controls & Mission::kControlCrouch), seconds);
    // A jump from the floor, or in low gravity once a second.
    _jumpWait = _jumpWait > delta ? _jumpWait - delta : 0;
    if (_jumpPressed && (controls & Mission::kControlJump) && _jumpWait == 0 &&
        (!_walker->isAirborne() || lowGravity)) {
        _walker->jump(_mission ? _mission->getMotion().jump : Mission::Motion().jump);
        if (lowGravity)
            _jumpWait = 1000;
    }
    _jumpPressed = false;
    // Low gravity with 0x40: walking goes where the view points.
    bool swim = lowGravity && (controls & Mission::kControlSwim);
    // The scripts' motion settings (SET_PLAYER_MOTION) scale the viewer's
    // speeds, the original's defaults giving these; cutscenes set 0.
    double moveScale = 1;
    double turnScale = 1;
    double lookScale = 1;
    if (_mission) {
        static const Mission::Motion kDefault;
        const Mission::Motion &motion = _mission->getMotion();
        moveScale = motion.speed / kDefault.speed;
        turnScale = (double)motion.turnRate / kDefault.turnRate;
        lookScale = (double)motion.lookRate / kDefault.lookRate;
    }

    // Run doubles walking and turning (game+0x210 and +0x218, 0x43B904).
    if (_walkKeys[kWalkRun]) {
        moveScale *= 2;
        turnScale *= 2;
    }

    _camera.yaw += turn * kTurnSpeed * turnScale * seconds;
    double mouseTurn = 0;
    double mouseLook = 0;
    if (_mouseLook)
        mouseLookRates(mouseTurn, mouseLook);
    if (controls & Mission::kControlTurn)
        _camera.yaw += mouseTurn * seconds;
    if (controls & Mission::kControlLook)
        look += mouseLook / kLookSpeed / lookScale;
    // SET_PLAYER_SPIN: turned regardless, degrees a second (heading up is yaw down).
    if (_mission && _mission->getSpin())
        _camera.yaw -= _mission->getSpin() * kPi / 180 * seconds;
    _camera.pitch = std::max(-1.0, std::min(1.0, _camera.pitch + look * kLookSpeed * lookScale * seconds));
    double step = kSpeed * moveScale * seconds;
    double across = 1;
    double up = 0;
    if (swim) {
        // The pitch is a shear of the view; its angle's cosine and sine.
        double length = std::sqrt(1 + _camera.pitch * _camera.pitch);
        across = 1 / length;
        up = -_camera.pitch / length;
    }
    double dx = (std::cos(_camera.yaw) * forward * across + std::sin(_camera.yaw) * strafe) * step;
    double dy = (std::sin(_camera.yaw) * forward * across - std::cos(_camera.yaw) * strafe) * step;
    if (swim)
        _walker->moveVertically(forward * up * step);

    // Move every frame, so falls carry on without a key held.
    double oldX = _camera.x;
    double oldY = _camera.y;
    double oldZ = _camera.z;
    if (!_mission || !_mission->isInTransition())
        _walker->move(dx, dy, seconds);
    if (_mission) {
        _mission->setPlayerYaw(_camera.yaw);
        _mission->update(delta);
        double yaw;
        if (_mission->takePlayerYaw(yaw))
            _camera.yaw = yaw;
        std::string subtitle;
        uint32_t duration;
        if (_mission->takeSubtitle(subtitle, duration) && (_settings.subtitles || !_frontEnd)) {
            _subtitle = subtitle;
            _missionSubtitle = true;
            _subtitleUntil = _levelTime + duration;
            log::debug(1, "%s", subtitle.c_str());
        }
    } else if (_actors) {
        std::vector<Actors::ScriptEvent> events;
        _actors->update(delta, *_walker, events);
    }
    _camera.x = _walker->getX();
    _camera.y = _walker->getY();
    _camera.z = _walker->getEye();
    _levelRenderer->setVisibleZones(_walker->getActiveZones() | (_mission ? _mission->getExtraZones() : 0));
    if (turn != 0 || look != 0 || mouseTurn != 0 || _camera.x != oldX || _camera.y != oldY || _camera.z != oldZ)
        updateWalkStatus();
}

void Generations::mouseLookRates(double &turn, double &look) const {
    // 0x43AFA3: away from a 64-pixel band across the middle of the view, the
    // mouse sets a heading and pitch atan(offset / distance from the far
    // side to the band) degrees away, and each frame the view turns a
    // fifth of the way there. The keys turn by the turn rate (15 by
    // default, Mission::Motion) and look by the look rate (10) a frame,
    // which here are kTurnSpeed and kLookSpeed; the mouse is scaled alike.
    auto angle = [](int mouse, int size, int offset) {
        int edge = size / 2 - offset;
        mouse = std::max(0, std::min(size - 1, mouse));
        int d = mouse - edge;
        if (d > 0) {
            edge += 64;
            d = mouse - edge;
            if (d < 0)
                return 0.0;
        }
        return -std::atan2((double)d, (double)edge) * 180 / kPi;
    };
    turn = angle(_mouseX, _viewW, 32) / 5 / 15 * kTurnSpeed;
    // Up the view is a positive pitch in the original, a negative one here.
    look = -angle(_mouseY, _viewH, 16) / 5 / 10 * kLookSpeed;
}

void Generations::prepareAim(int w, int h) {
    // The original aims at the cursor while it is over the view, else at
    // the centre (view.vtD8 0x430756).
    _viewW = w;
    _viewH = h;
    bool inView = _mouseX >= 0 && _mouseX < w && _mouseY >= 0 && _mouseY < h;
    _aimX = inView ? _mouseX : w / 2;
    _aimY = inView ? _mouseY : h / 2;

    int kind = 0;
    if (_mission && _mission->getActiveItem() && _mission->isWeapon(_mission->getActiveItem()))
        kind = _mission->getItemKind(_mission->getActiveItem());
    if (kind != _heldItem.getKind())
        _heldItem.load(_resources, _levelRef, kind);

    // Weapons act on what the cursor points at, the punch on the centre.
    if (kind)
        _levelRenderer->setPickPoint(_aimX, _aimY);
    else
        _levelRenderer->setPickPoint(w / 2, h / 2);
    setCursor(kind && inView && HeldItem::getStyle(kind) &&
              HeldItem::place(*HeldItem::getStyle(kind), _aimX, _aimY, w, h).usable);
}

void Generations::drawViewOverlay(Canvas &view) {
    // SHOW_VIEW_OVERLAY: centred at the top of the view, like the punch.
    const Mission::ViewOverlay &overlay = _mission->getViewOverlay();
    if (overlay.image != _viewOverlayImage) {
        _viewOverlayImage = overlay.image;
        _viewOverlayLoaded = false;
        if (!overlay.image.empty()) {
            size_t slash = _levelRef.name.find_last_of("/\\");
            std::string folder = slash == std::string::npos ? "" : _levelRef.name.substr(0, slash + 1);
            std::unique_ptr<SeekableReadStream> stream = _resources.open({_levelRef.archive, folder + overlay.image});
            _viewOverlayLoaded = stream && _viewOverlay.load(*stream);
            if (!_viewOverlayLoaded)
                log::warning("Could not load the view overlay %s", overlay.image.c_str());
        }
    }
    // 0x409DDC: a picture no wider than the view is centred in both modes;
    // only a wider one differs (mode 1 crops round its middle, mode 2
    // copies its rows from the left), which with the view always 640 wide
    // never happens. Anything else draws nothing.
    if (!_viewOverlayLoaded || (overlay.mode != 1 && overlay.mode != 2))
        return;
    // One frame of every row (0x431355 sets no frame height).
    _viewOverlay.drawFrame(view, 0, _viewOverlay.getRowCount(), (view.w - _viewOverlay.getWidth()) / 2, 0);
}

void Generations::drawHands(Canvas &view, uint32_t delta) {
    if (_mission)
        drawViewOverlay(view);
    if (_heldItem.getKind()) {
        _heldItem.draw(view, _aimX, _aimY, _flashLeft > 0);
        _flashLeft = _flashLeft > delta ? _flashLeft - delta : 0;
        return;
    }
    updatePunch(delta, view);
    uint8_t cross = _screen->getPalette().findBestColor(255, 255, 255);
    for (int d = 2; d <= 5; d++) {
        view.setPixel(view.w / 2 - d, view.h / 2, cross);
        view.setPixel(view.w / 2 + d, view.h / 2, cross);
        view.setPixel(view.w / 2, view.h / 2 - d, cross);
        view.setPixel(view.w / 2, view.h / 2 + d, cross);
    }
}

void Generations::attack() {
    if (!_mission || _punching)
        return;
    if (_heldItem.getKind()) {
        // From the gun's muzzle, which follows the cursor; below the gun's
        // line it can't be used.
        double ahead = 1.5;
        double up = -0.5;
        double left = 0;
        if (const HeldItem::Style *style = HeldItem::getStyle(_heldItem.getKind())) {
            if (!HeldItem::place(*style, _aimX, _aimY, _viewW, _viewH).usable)
                return;
            HeldItem::getMuzzle(*style, _aimX, _aimY, _viewW, _viewH, ahead, up, left);
        }
        Mission::FireResult result =
            _mission->fireWeapon(_camera, _levelRenderer->getPick(), _aimX, _aimY, _viewW, _viewH, ahead, up, left);
        if (result == Mission::kFired) {
            const Mission::Item *item = _mission->getItem(_mission->getActiveItem());
            _flashLeft = item && item->setting > 0 ? item->setting : 150;
            updateWalkStatus();
        }
        if (result != Mission::kNoWeapon)
            return;
    }

    // Bare hands: play the punch overlay (SET_PUNCH), which lands on its
    // hit frame. It sits beside GAME.LV2.
    const Mission::Punch &punch = _mission->getPunch();
    if (punch.image != _punchImage) {
        _punchImage = punch.image;
        _punchFrameHeight = 0;
        size_t slash = _levelRef.name.find_last_of("/\\");
        std::string folder = slash == std::string::npos ? "" : _levelRef.name.substr(0, slash + 1);
        std::unique_ptr<SeekableReadStream> stream = _resources.open({_levelRef.archive, folder + punch.image});
        // Frames as tall as the original's 3D view (0x431D25).
        if (stream && _punchOverlay.load(*stream))
            _punchFrameHeight = std::min<int>(Overlay::kViewHeight, _punchOverlay.getRowCount());
        else if (!punch.image.empty())
            log::warning("Could not load the punch overlay %s", punch.image.c_str());
    }
    _mission->swing();
    _punching = true;
    _punchLanded = false;
    _punchFrame = 0;
    _punchTime = 0;
}

void Generations::updatePunch(uint32_t delta, Canvas &view) {
    if (!_punching || !_mission)
        return;
    const Mission::Punch &punch = _mission->getPunch();
    int frames = _punchFrameHeight ? _punchOverlay.getRowCount() / _punchFrameHeight : 0;
    // One frame per tick at most (0x431E57); the blow lands on the first
    // frame showing the hit frame.
    _punchTime += delta;
    _punchFrame = std::min<int>(_punchTime / punch.msPerFrame, _punchFrame + 1);
    if (!_punchLanded && (_punchFrame >= punch.hitFrame || frames == 0)) {
        _punchLanded = true;
        _mission->punch(_levelRenderer->getPick());
    }
    if (_punchFrame >= frames) {
        _punching = false;
        return;
    }
    _punchOverlay.drawFrame(view, _punchFrame, _punchFrameHeight, (view.w - _punchOverlay.getWidth()) / 2,
                            view.h - _punchFrameHeight);
}

void Generations::updateWalkStatus() {
    double degrees = std::fmod(_camera.yaw * 180 / 3.14159265358979323846, 360.0);
    if (degrees < 0)
        degrees += 360;
    // The archive names the level, e.g. ARVADA1.DAT:arvada1/GAME.LV2.
    std::string level = _levelName.substr(0, _levelName.find(':'));
    if (text::hasSuffixIgnoreCase(level, ".dat"))
        level = level.substr(0, level.size() - 4);
    _status = text::format("%s 3D %.0f,%.0f,%.0f %.0fdeg  WASD PgUp/Dn  L: light %s  Shift+C: walls %s", level.c_str(),
                           _camera.x, _camera.y, _camera.z, degrees, _levelRenderer->isLightingEnabled() ? "on" : "off",
                           _walker->isCollisionEnabled() ? "on" : "off");
    if (_mission) {
        _shownHealth = _mission->getHealth();
        _status += text::format("  health %d", _shownHealth);
        if (_mission->getActiveItem())
            _status += text::format("  item %04x", _mission->getActiveItem());
        const Mission::Item *item = _mission->getItem(_mission->getActiveItem());
        if (item && item->maxLevel > 0)
            _status += text::format(" power %d charge %d/%d", item->level, item->charge, item->maxCharge);
    }
}

Error Generations::start() {
    return startFrontEnd();
}

void Generations::keyDown(const KeyState &kbd) {
    if (kbd.keycode == reluva::key::kEscape)
        openMenu(_playingMission);
}

void Generations::click(int /*x*/, int /*y*/) {
}

bool Generations::drawPage(uint32_t /*delta*/) {
    // Nothing is up between the game's screens.
    _screen->clear(kColorBlack);
    return true;
}

void Generations::missionOver() {
    campaignMissionEnded();
}

void Generations::unloadLevel() {
    _hudScreen.reset();
    _heldItem.load(_resources, _levelRef, 0);
    _mission.reset();
    _actors.reset();
    _walker.reset();
    _levelRenderer.reset();
    _level.reset();
}

void Generations::clearView() {
    unloadLevel();
    initPalette();
    _dirty = true;
}

Error Generations::restorePlace(int /*imageIndex*/, int /*soundIndex*/) {
    return Error::fail("saved in the data viewer, with no game in it");
}

bool Generations::playMovie(const std::string &name) {
    avi::Decoder movie;
    std::unique_ptr<SeekableReadStream> file = _resources.open({"", name});
    if (!file || !movie.load(std::move(file)) || !movie.canDecode()) {
        log::warning("Can't play the movie %s", name.c_str());
        return false;
    }

    // The movies are true colour: the screen leaves 8-bit mode while they
    // play, letterboxed in the middle of it.
    _system->initSize(Screen::kWidth, Screen::kHeight, Canvas::kArgb32);
    _system->showMouse(false);

    SoundHandle sound;
    if (_mixer && movie.hasAudio() && movie.audioBits() == 16) {
        std::vector<uint8_t> data = movie.audio();
        uint8_t flags = kRaw16Bits | kRawLittleEndian;
        if (movie.audioChannels() == 2)
            flags |= kRawStereo;
        _mixer->playStream(Mixer::kMusicSoundType, &sound, makeRawStream(std::move(data), movie.audioRate(), flags));
    }

    int x = (Screen::kWidth - std::min(movie.width(), (int)Screen::kWidth)) / 2;
    int y = (Screen::kHeight - std::min(movie.height(), (int)Screen::kHeight)) / 2;
    int w = std::min(movie.width(), (int)Screen::kWidth);
    int h = std::min(movie.height(), (int)Screen::kHeight);
    // The picture as the screen takes it (0xAARRGGBB).
    std::vector<uint32_t> picture(size_t(movie.width()) * movie.height());
    uint32_t start = _system->getMillis();
    bool done = false;
    while (!done && !shouldQuit()) {
        Event event;
        while (_eventMan->pollEvent(event)) {
            if (event.type == kEventLbuttondown ||
                (event.type == kEventKeydown &&
                 (event.kbd.keycode == reluva::key::kEscape || event.kbd.keycode == reluva::key::kSpace ||
                  event.kbd.keycode == reluva::key::kReturn)))
                done = true;
        }

        // Keep to the clock, dropping frames if decoding falls behind.
        int due = (int)((uint64_t)(_system->getMillis() - start) * 1000 / movie.frameMicros());
        bool drawn = false;
        while (!done && movie.currentFrame() < due) {
            if (!movie.decodeNextFrame())
                done = true;
            drawn = true;
        }
        if (drawn && movie.currentFrame() >= 0) {
            const Surface &s = movie.surface();
            for (int py = 0; py < s.h; py++) {
                const uint8_t *src = s.row(py);
                for (int px = 0; px < s.w; px++)
                    picture[size_t(py) * s.w + px] = argb(src[px * 4], src[px * 4 + 1], src[px * 4 + 2]);
            }
            _system->copyRectToScreen(picture.data(), movie.width() * 4, x, y, w, h);
            _system->updateScreen();
        }
        _system->delayMillis(5);
    }

    if (_mixer)
        _mixer->stopHandle(sound);
    _system->initSize(Screen::kWidth, Screen::kHeight);
    _screen->applyPalette();
    _system->showMouse(true);
    return true;
}

std::string Generations::getSaveFileName(int slot) {
    return text::format("stg.s%02d", slot);
}

Error Generations::saveGameState(int slot, const std::string &description) {
    if (_awaitingProceed)
        return Error::fail("the mission hasn't started");
    MemoryWriteStream file;
    MemoryWriteStream *out = &file;

    out->writeUint32BE(kSaveMagic);
    out->writeUint32LE(kSaveVersion);
    out->writeUint16LE((uint16_t)description.size());
    out->writeString(description);
    std::string date = saveDate();
    out->writeUint16LE((uint16_t)date.size());
    out->writeString(date);
    out->writeUint32LE((uint32_t)_imageIndex);
    out->writeUint32LE((uint32_t)_soundIndex);
    // Where the story is.
    out->writeByte(_frontEnd ? 1 : 0);
    if (_frontEnd) {
        Serializer s(nullptr, out);
        s.setVersion(kSaveVersion);
        _campaign.sync(s);
        bool bridge = _bridge != nullptr;
        s.syncAsBool(bridge);
        if (bridge)
            _bridge->sync(s);
    }
    // A mission in progress: where, the view, then everything the mission holds.
    bool inMission = _playingMission && _mission && _level;
    out->writeByte(inMission ? 1 : 0);
    if (inMission) {
        Serializer s(nullptr, out);
        s.setVersion(kSaveVersion);
        std::string name = _missions[_missionChoice].name;
        s.syncString(name);
        s.syncAsDouble(_camera.yaw);
        s.syncAsDouble(_camera.pitch);
        int leftMode = _hudScreen ? (int)_hudScreen->getLeftMode() : 0;
        s.syncAsInt(leftMode);
        _mission->sync(s);
        if (s.err())
            return Error::fail("the mission couldn't be saved");
    }
    if (!_system->saveFile(getSaveFileName(slot), file.data()))
        return Error::fail("couldn't write " + (_system->saveDir() / getSaveFileName(slot)).string());

    log::debug(1, "Saved slot %d to %s", slot, getSaveFileName(slot).c_str());
    return Error::success();
}

Error Generations::loadGameState(int slot) {
    std::unique_ptr<SeekableReadStream> in = _system->openForLoading(getSaveFileName(slot));
    if (!in)
        return Error::fail("no saved game in slot " + std::to_string(slot));

    if (in->readUint32BE() != kSaveMagic)
        return Error::fail("not a saved game");
    uint32_t version = in->readUint32LE();
    if (version > kSaveVersion)
        return Error::fail("saved by a newer version");

    std::string description = in->readFixedString(in->readUint16LE());
    if (version >= kFirstDatedSaveVersion)
        in->readFixedString(in->readUint16LE());
    int32_t imageIndex = in->readSint32LE();
    int32_t soundIndex = in->readSint32LE();
    bool inCampaign = false;
    if (version >= kFirstDatedSaveVersion && in->readByte() != 0) {
        if (!_frontEnd)
            return Error::fail("a saved game, which the data viewer can't load");
        if (version < kFirstBridgeSaveVersion)
            return Error::fail("saved by an older build, before the bridge; start a new game");
        Campaign campaign = _campaign;
        Serializer s(in.get(), nullptr);
        s.setVersion(version);
        _campaign.sync(s);
        bool bridge = false;
        s.syncAsBool(bridge);
        if (bridge && _bridge) {
            _bridge->reset();
            _bridge->sync(s);
        } else if (bridge) {
            s.fail();
        }
        if (s.err()) {
            _campaign = campaign;
            return Error::fail("damaged saved game");
        }
        inCampaign = true;
    }
    bool inMission = version >= 2 && in->readByte() != 0;
    if (in->eos())
        return Error::fail("truncated saved game");

    if (inMission && version < kFirstMissionSaveVersion)
        return Error::fail("the mission was saved by an older build; start it again");
    if (inMission) {
        Serializer s(in.get(), nullptr);
        s.setVersion(version);
        std::string name;
        s.syncString(name);
        int index = -1;
        for (int i = 0; i < (int)_missions.size(); i++) {
            if (_missions[i].name == name)
                index = i;
        }
        if (index < 0)
            return Error::fail("the mission " + std::string(name.c_str()) + " isn't installed");
        double yaw = 0;
        double pitch = 0;
        int leftMode = 0;
        s.syncAsDouble(yaw);
        s.syncAsDouble(pitch);
        s.syncAsInt(leftMode);
        // Set the level up without starting its scripts, then put the
        // saved state over it.
        _missionChoice = index;
        _missionMessage.clear();
        showLevel(_missions[index].level, false);
        if (!_level || !_mission)
            return Error::fail("could not load the mission");
        _mission->sync(s);
        if (s.err()) {
            leaveMission("The saved game could not be read");
            return Error::fail("damaged saved game");
        }
        _camera.yaw = yaw;
        _camera.pitch = pitch;
        _camera.x = _walker->getX();
        _camera.y = _walker->getY();
        _camera.z = _walker->getEye();
        _levelRenderer->setVisibleZones(_walker->getActiveZones());
        if (_hudScreen && leftMode >= HudScreen::kLeftOff && leftMode <= HudScreen::kLeftObject)
            _hudScreen->setLeftMode((HudScreen::LeftMode)leftMode);
        _playingMission = true;
        showLevelPage(kLevelPage3D);
        log::debug(1, "Loaded slot %d: %s (%s)", slot, description.c_str(), name.c_str());
        return Error::success();
    }

    if (inCampaign) {
        // Saved between missions: the story carries on from there, the
        // section starting again from its first line (0x419703).
        if (_playingMission)
            leaveMission("Loaded a saved game");
        if (_bridge)
            _bridge->afterLoad();
        _campaign.restartSection();
        log::debug(1, "Loaded slot %d: %s", slot, description.c_str());
        runCampaign();
        return Error::success();
    }

    if (Error err = restorePlace(imageIndex, soundIndex))
        return err;

    log::debug(1, "Loaded slot %d: %s", slot, description.c_str());
    return Error::success();
}

} // namespace reluva::stgenerations
