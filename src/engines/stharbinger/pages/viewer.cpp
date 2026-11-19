// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stharbinger/pages/viewer.h"

#include "common/log.h"
#include "common/text.h"
#include "engines/stharbinger/formats/audio.h"
#include "engines/stharbinger/harbinger.h"
#include "engines/stharbinger/platform.h"

#include <cstdio>

namespace reluva::stharbinger {

namespace {

const uint32_t kAnimFrameMillis = 66; // ~15 frames per second

const char *const kHelp = "Scene viewer keys:\n"
                          "  Left/Right       previous/next background of the scene\n"
                          "  Up/Down, PgUp/PgDn  previous/next scene\n"
                          "  H                show hotspot rectangles (hover one to see its name)\n"
                          "  A / Shift+A      next/previous animation of the scene\n"
                          "  S / Shift+S      next/previous sound of the scene\n"
                          "  Space            stop animation and sound\n"
                          "  Esc, Q           leave the viewer\n";

} // namespace

SceneViewer::SceneViewer(Harbinger *vm)
    : _vm(vm), _system(vm->getSystem()), _resources(vm->getResources()), _scenes(vm->getScenes()) {
}

SceneViewer::~SceneViewer() {
    if (_system->getMixer())
        _system->getMixer()->stopAll();
}

std::string SceneViewer::listName(ListKind kind, int index) const {
    const auto &list = _scenes[_sceneIndex].lists[kind];
    return index >= 0 && index < int(list.size()) ? list[index].name : std::string();
}

int SceneViewer::run(int startScene) {
    _system->setWindowTitle("Reluva - Harbinger scene viewer");
    _system->initGraphics(kScreenWidth, kScreenHeight);
    draw::create(_screen, kScreenWidth, kScreenHeight);
    // A grey ramp until the first palette is loaded.
    for (int i = 0; i < 256; i++)
        _palette.setColor(i, uint8_t(i), uint8_t(i), uint8_t(i));

    std::printf("%s", kHelp);
    enterScene(startScene);

    while (!_quit && !_vm->shouldQuit()) {
        InputEvent event;
        while (_system->pollEvent(event))
            handleEvent(event);
        updateAnimation();
        if (_dirty) {
            drawScreen();
            _dirty = false;
        }
        _system->delayMillis(10);
    }
    return 0;
}

void SceneViewer::enterScene(int index) {
    int n = int(_scenes.size());
    _sceneIndex = ((index % n) + n) % n;
    _animIndex = -1;
    _anim = RleAnimation();
    _soundIndex = -1;
    _hoverHotspot = -1;
    collectHotspots();

    // Start on the scene's first background, as its script normally does.
    const Scene &scene = _scenes[_sceneIndex];
    int first = 0;
    for (const Directive &d : scene.script) {
        const Argument *a = d.getArg(0);
        if (d.command == kCmdBackground && a && a->type == kArgSymbol) {
            first = int(a->value);
            break;
        }
    }
    showBackground(first);
    log::debug(1, "Scene %s (slot %d): %u directives", scene.name.c_str(), scene.slot, unsigned(scene.script.size()));
}

void SceneViewer::showBackground(int index) {
    const auto &list = _scenes[_sceneIndex].lists[kListBackgrounds];
    _dirty = true;
    if (list.empty()) {
        _backgroundIndex = 0;
        _backgroundMissing = true;
        _background = Surface();
        updateCaption();
        return;
    }
    int n = int(list.size());
    _backgroundIndex = ((index % n) + n) % n;

    bool paletteLoaded = false;
    _backgroundMissing = !_resources.loadBackground(list[_backgroundIndex].name, _background, _palette, paletteLoaded);
    if (_backgroundMissing)
        _background = Surface();

    // The game keeps the current palette when a background has none of its own.
    // Jumping around in a viewer breaks that chain, so borrow the palette of the
    // nearest background in the list that has one.
    if (!paletteLoaded) {
        for (int dist = 1; dist < n && !paletteLoaded; dist++) {
            for (int cand : {_backgroundIndex - dist, _backgroundIndex + dist}) {
                if (cand < 0 || cand >= n)
                    continue;
                auto pal = _resources.openFile("PALETTES/" + text::toUpper(list[cand].name) + ".PAL");
                if (pal && loadPAL(*pal, _palette)) {
                    paletteLoaded = true;
                    break;
                }
            }
        }
    }
    _system->setPalette(_palette.data(), 0, 256);
    updateCaption();
}

void SceneViewer::startAnimation(int index) {
    const auto &list = _scenes[_sceneIndex].lists[kListAnims];
    _dirty = true;
    if (list.empty())
        return;
    int n = int(list.size());
    _animIndex = ((index % n) + n) % n;
    _animFrame = 0;
    _nextFrameTime = _system->getMillis() + kAnimFrameMillis;
    if (!_resources.loadAnimation(list[_animIndex].name, _anim)) {
        _anim = RleAnimation();
        log::debug(1, "Animation %s not found", list[_animIndex].name.c_str());
    }
    updateCaption();
}

void SceneViewer::playSound(int index) {
    const auto &list = _scenes[_sceneIndex].lists[kListSounds];
    audio::Mixer *mixer = _system->getMixer();
    if (list.empty() || !mixer)
        return;
    int n = int(list.size());
    _soundIndex = ((index % n) + n) % n;
    mixer->stop(_soundHandle);
    _soundHandle = audio::kNoSound;
    std::vector<uint8_t> data;
    if (_resources.loadSound(list[_soundIndex].name, data))
        _soundHandle = mixer->play(makeRawStream(std::move(data)), audio::SoundType::kEffects);
    else
        log::debug(1, "Sound %s not found", list[_soundIndex].name.c_str());
    updateCaption();
}

void SceneViewer::collectHotspots() {
    // HOTSPOT name, x, y, w, h, label, cursor and SETRECT name, x, y, w, h, taken
    // in script order and tied to the background shown last. Rectangles that come
    // from variables are only known at run time and are left out.
    _hotspots.clear();
    int background = -1;
    for (const Directive &d : _scenes[_sceneIndex].script) {
        const Argument *a0 = d.getArg(0);
        if (d.command == kCmdBackground && a0 && a0->type == kArgSymbol) {
            background = int(a0->value);
            continue;
        }
        if ((d.command != kCmdHotspot && d.command != kCmdSetRect) || !a0)
            continue;
        int v[4];
        bool known = true;
        for (int i = 0; i < 4; i++) {
            const Argument *a = d.getArg(i + 1);
            known = known && a && a->type == kArgConst;
            v[i] = a ? int(a->value) : 0;
        }
        if (known && v[2] > 0 && v[3] > 0)
            _hotspots.push_back({int(a0->value), background, v[0], v[1], v[2], v[3]});
    }
}

void SceneViewer::handleEvent(const InputEvent &event) {
    switch (event.type) {
    case InputEvent::kQuit:
        _quit = true;
        break;
    case InputEvent::kKeyDown: {
        bool shift = (event.mods & keymod::kShift) != 0;
        switch (event.key) {
        case key::kEscape:
        case 'q':
            _quit = true;
            break;
        case key::kRight:
            showBackground(_backgroundIndex + 1);
            break;
        case key::kLeft:
            showBackground(_backgroundIndex - 1);
            break;
        case key::kDown:
        case key::kPageDown:
            enterScene(_sceneIndex + 1);
            break;
        case key::kUp:
        case key::kPageUp:
            enterScene(_sceneIndex - 1);
            break;
        case 'h':
            _showHotspots = !_showHotspots;
            _dirty = true;
            break;
        case 'a':
            startAnimation(_animIndex < 0 ? 0 : _animIndex + (shift ? -1 : 1));
            break;
        case 's':
            playSound(_soundIndex < 0 ? 0 : _soundIndex + (shift ? -1 : 1));
            break;
        case key::kSpace:
            _animIndex = -1;
            _anim = RleAnimation();
            if (_system->getMixer())
                _system->getMixer()->stopAll();
            _dirty = true;
            updateCaption();
            break;
        default:
            break;
        }
        break;
    }
    case InputEvent::kMouseMove: {
        int hover = -1;
        if (_showHotspots) {
            for (size_t i = 0; i < _hotspots.size(); i++) {
                const Hotspot &h = _hotspots[i];
                if ((h.background == _backgroundIndex || h.background < 0) && event.mouse.x >= h.x &&
                    event.mouse.x < h.x + h.w && event.mouse.y >= h.y && event.mouse.y < h.y + h.h)
                    hover = int(i);
            }
        }
        if (hover != _hoverHotspot) {
            _hoverHotspot = hover;
            updateCaption();
        }
        break;
    }
    default:
        break;
    }
}

void SceneViewer::updateAnimation() {
    if (_anim.frameCount() == 0)
        return;
    uint32_t now = _system->getMillis();
    if (int32_t(now - _nextFrameTime) < 0)
        return;
    _animFrame = (_animFrame + 1) % _anim.frameCount();
    _nextFrameTime = now + kAnimFrameMillis;
    _dirty = true;
}

void SceneViewer::drawScreen() {
    draw::fill(_screen, 0);
    if (!draw::empty(_background))
        draw::blit(_screen, _background, 0, 0);
    if (_anim.frameCount())
        _anim.drawFrame(_animFrame, _screen);
    if (_showHotspots) {
        uint8_t color = uint8_t(_palette.findBestColor(0, 255, 0));
        uint8_t hoverColor = uint8_t(_palette.findBestColor(255, 255, 0));
        for (size_t i = 0; i < _hotspots.size(); i++) {
            const Hotspot &h = _hotspots[i];
            if (h.background == _backgroundIndex || h.background < 0)
                draw::frameRect(_screen, h.x, h.y, h.w, h.h, int(i) == _hoverHotspot ? hoverColor : color);
        }
    }
    _system->copyRectToScreen(_screen);
    _system->updateScreen();
}

void SceneViewer::updateCaption() {
    const Scene &scene = _scenes[_sceneIndex];
    int numBackgrounds = int(scene.lists[kListBackgrounds].size());
    std::string caption =
        text::format("%s [%d/%d] | background %s (%d/%d)%s", scene.name.c_str(), _sceneIndex + 1, int(_scenes.size()),
                     listName(kListBackgrounds, _backgroundIndex).c_str(), numBackgrounds ? _backgroundIndex + 1 : 0,
                     numBackgrounds, _backgroundMissing ? " missing" : "");
    if (_animIndex >= 0)
        caption += text::format(" | anim %s%s", listName(kListAnims, _animIndex).c_str(),
                                _anim.frameCount() ? "" : " missing");
    if (_soundIndex >= 0)
        caption += " | sound " + listName(kListSounds, _soundIndex);
    if (_hoverHotspot >= 0)
        caption += " | hotspot " + listName(kListHotspots, _hotspots[_hoverHotspot].index);
    _system->setWindowTitle(caption);
}

} // namespace reluva::stharbinger
