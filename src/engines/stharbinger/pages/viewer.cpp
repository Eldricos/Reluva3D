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

const char* const kHelp = "Scene viewer keys:\n"
                          "  Left/Right       previous/next background of the scene\n"
                          "  Up/Down, PgUp/PgDn  previous/next scene\n"
                          "  H                show hotspot rectangles (hover one to see its name)\n"
                          "  A / Shift+A      next/previous animation of the scene\n"
                          "  S / Shift+S      next/previous sound of the scene\n"
                          "  Space            stop animation and sound\n"
                          "  Esc, Q           quit\n";

} // namespace

SceneViewer::SceneViewer(Harbinger* vm)
    : vm_(vm), system_(vm->getSystem()), resources_(vm->getResources()), scenes_(vm->getScenes()) {}

SceneViewer::~SceneViewer() {
    if (system_->getMixer())
        system_->getMixer()->stopAll();
}

std::string SceneViewer::listName(ListKind kind, int index) const {
    const auto& list = scenes_[sceneIndex_].lists[kind];
    return index >= 0 && index < int(list.size()) ? list[index].name : std::string();
}

int SceneViewer::run(int startScene) {
    system_->setWindowTitle("Reluva - Harbinger scene viewer");
    system_->initGraphics(kScreenWidth, kScreenHeight);
    draw::create(screen_, kScreenWidth, kScreenHeight);
    // A grey ramp until the first palette is loaded.
    for (int i = 0; i < 256; i++)
        palette_.setColor(i, uint8_t(i), uint8_t(i), uint8_t(i));

    std::printf("%s", kHelp);
    enterScene(startScene);

    while (!quit_ && !vm_->shouldQuit()) {
        InputEvent event;
        while (system_->pollEvent(event))
            handleEvent(event);
        updateAnimation();
        if (dirty_) {
            drawScreen();
            dirty_ = false;
        }
        system_->delayMillis(10);
    }
    return 0;
}

void SceneViewer::enterScene(int index) {
    int n = int(scenes_.size());
    sceneIndex_ = ((index % n) + n) % n;
    animIndex_ = -1;
    anim_ = RleAnimation();
    soundIndex_ = -1;
    hoverHotspot_ = -1;
    collectHotspots();

    // Start on the scene's first background, as its script normally does.
    const Scene& scene = scenes_[sceneIndex_];
    int first = 0;
    for (const Directive& d : scene.script) {
        const Argument* a = d.getArg(0);
        if (d.command == kCmdBackground && a && a->type == kArgSymbol) {
            first = int(a->value);
            break;
        }
    }
    showBackground(first);
    log::debug(1, "Scene %s (slot %d): %u directives", scene.name.c_str(), scene.slot, unsigned(scene.script.size()));
}

void SceneViewer::showBackground(int index) {
    const auto& list = scenes_[sceneIndex_].lists[kListBackgrounds];
    dirty_ = true;
    if (list.empty()) {
        backgroundIndex_ = 0;
        backgroundMissing_ = true;
        background_ = Surface();
        updateCaption();
        return;
    }
    int n = int(list.size());
    backgroundIndex_ = ((index % n) + n) % n;

    bool paletteLoaded = false;
    backgroundMissing_ = !resources_.loadBackground(list[backgroundIndex_].name, background_, palette_, paletteLoaded);
    if (backgroundMissing_)
        background_ = Surface();

    // The game keeps the current palette when a background has none of its own.
    // Jumping around in a viewer breaks that chain, so borrow the palette of the
    // nearest background in the list that has one.
    if (!paletteLoaded) {
        for (int dist = 1; dist < n && !paletteLoaded; dist++) {
            for (int cand : {backgroundIndex_ - dist, backgroundIndex_ + dist}) {
                if (cand < 0 || cand >= n)
                    continue;
                auto pal = resources_.openFile("PALETTES/" + text::toUpper(list[cand].name) + ".PAL");
                if (pal && loadPAL(*pal, palette_)) {
                    paletteLoaded = true;
                    break;
                }
            }
        }
    }
    system_->setPalette(palette_.data(), 0, 256);
    updateCaption();
}

void SceneViewer::startAnimation(int index) {
    const auto& list = scenes_[sceneIndex_].lists[kListAnims];
    dirty_ = true;
    if (list.empty())
        return;
    int n = int(list.size());
    animIndex_ = ((index % n) + n) % n;
    animFrame_ = 0;
    nextFrameTime_ = system_->getMillis() + kAnimFrameMillis;
    if (!resources_.loadAnimation(list[animIndex_].name, anim_)) {
        anim_ = RleAnimation();
        log::debug(1, "Animation %s not found", list[animIndex_].name.c_str());
    }
    updateCaption();
}

void SceneViewer::playSound(int index) {
    const auto& list = scenes_[sceneIndex_].lists[kListSounds];
    audio::Mixer* mixer = system_->getMixer();
    if (list.empty() || !mixer)
        return;
    int n = int(list.size());
    soundIndex_ = ((index % n) + n) % n;
    mixer->stop(soundHandle_);
    soundHandle_ = audio::kNoSound;
    std::vector<uint8_t> data;
    if (resources_.loadSound(list[soundIndex_].name, data))
        soundHandle_ = mixer->play(makeRawStream(std::move(data)), audio::SoundType::Effects);
    else
        log::debug(1, "Sound %s not found", list[soundIndex_].name.c_str());
    updateCaption();
}

void SceneViewer::collectHotspots() {
    // HOTSPOT name, x, y, w, h, label, cursor and SETRECT name, x, y, w, h, taken
    // in script order and tied to the background shown last. Rectangles that come
    // from variables are only known at run time and are left out.
    hotspots_.clear();
    int background = -1;
    for (const Directive& d : scenes_[sceneIndex_].script) {
        const Argument* a0 = d.getArg(0);
        if (d.command == kCmdBackground && a0 && a0->type == kArgSymbol) {
            background = int(a0->value);
            continue;
        }
        if ((d.command != kCmdHotspot && d.command != kCmdSetRect) || !a0)
            continue;
        int v[4];
        bool known = true;
        for (int i = 0; i < 4; i++) {
            const Argument* a = d.getArg(i + 1);
            known = known && a && a->type == kArgConst;
            v[i] = a ? int(a->value) : 0;
        }
        if (known && v[2] > 0 && v[3] > 0)
            hotspots_.push_back({int(a0->value), background, v[0], v[1], v[2], v[3]});
    }
}

void SceneViewer::handleEvent(const InputEvent& event) {
    switch (event.type) {
    case InputEvent::kQuit: quit_ = true; break;
    case InputEvent::kKeyDown: {
        bool shift = (event.mods & keymod::Shift) != 0;
        switch (event.key) {
        case key::Escape:
        case 'q': quit_ = true; break;
        case key::Right: showBackground(backgroundIndex_ + 1); break;
        case key::Left: showBackground(backgroundIndex_ - 1); break;
        case key::Down:
        case key::PageDown: enterScene(sceneIndex_ + 1); break;
        case key::Up:
        case key::PageUp: enterScene(sceneIndex_ - 1); break;
        case 'h':
            showHotspots_ = !showHotspots_;
            dirty_ = true;
            break;
        case 'a': startAnimation(animIndex_ < 0 ? 0 : animIndex_ + (shift ? -1 : 1)); break;
        case 's': playSound(soundIndex_ < 0 ? 0 : soundIndex_ + (shift ? -1 : 1)); break;
        case key::Space:
            animIndex_ = -1;
            anim_ = RleAnimation();
            if (system_->getMixer())
                system_->getMixer()->stopAll();
            dirty_ = true;
            updateCaption();
            break;
        default: break;
        }
        break;
    }
    case InputEvent::kMouseMove: {
        int hover = -1;
        if (showHotspots_) {
            for (size_t i = 0; i < hotspots_.size(); i++) {
                const Hotspot& h = hotspots_[i];
                if ((h.background == backgroundIndex_ || h.background < 0) && event.mouse.x >= h.x &&
                    event.mouse.x < h.x + h.w && event.mouse.y >= h.y && event.mouse.y < h.y + h.h)
                    hover = int(i);
            }
        }
        if (hover != hoverHotspot_) {
            hoverHotspot_ = hover;
            updateCaption();
        }
        break;
    }
    default: break;
    }
}

void SceneViewer::updateAnimation() {
    if (anim_.frameCount() == 0)
        return;
    uint32_t now = system_->getMillis();
    if (int32_t(now - nextFrameTime_) < 0)
        return;
    animFrame_ = (animFrame_ + 1) % anim_.frameCount();
    nextFrameTime_ = now + kAnimFrameMillis;
    dirty_ = true;
}

void SceneViewer::drawScreen() {
    draw::fill(screen_, 0);
    if (!draw::empty(background_))
        draw::blit(screen_, background_, 0, 0);
    if (anim_.frameCount())
        anim_.drawFrame(animFrame_, screen_);
    if (showHotspots_) {
        uint8_t color = uint8_t(palette_.findBestColor(0, 255, 0));
        uint8_t hoverColor = uint8_t(palette_.findBestColor(255, 255, 0));
        for (size_t i = 0; i < hotspots_.size(); i++) {
            const Hotspot& h = hotspots_[i];
            if (h.background == backgroundIndex_ || h.background < 0)
                draw::frameRect(screen_, h.x, h.y, h.w, h.h, int(i) == hoverHotspot_ ? hoverColor : color);
        }
    }
    system_->copyRectToScreen(screen_);
    system_->updateScreen();
}

void SceneViewer::updateCaption() {
    const Scene& scene = scenes_[sceneIndex_];
    int numBackgrounds = int(scene.lists[kListBackgrounds].size());
    std::string caption =
        text::format("%s [%d/%d] | background %s (%d/%d)%s", scene.name.c_str(), sceneIndex_ + 1, int(scenes_.size()),
                     listName(kListBackgrounds, backgroundIndex_).c_str(), numBackgrounds ? backgroundIndex_ + 1 : 0,
                     numBackgrounds, backgroundMissing_ ? " missing" : "");
    if (animIndex_ >= 0)
        caption += text::format(" | anim %s%s", listName(kListAnims, animIndex_).c_str(),
                                anim_.frameCount() ? "" : " missing");
    if (soundIndex_ >= 0)
        caption += " | sound " + listName(kListSounds, soundIndex_);
    if (hoverHotspot_ >= 0)
        caption += " | hotspot " + listName(kListHotspots, hotspots_[hoverHotspot_].index);
    system_->setWindowTitle(caption);
}

} // namespace reluva::stharbinger
