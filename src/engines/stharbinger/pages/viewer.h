// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "engines/stharbinger/formats/graphics.h"
#include "engines/stharbinger/formats/resources.h"
#include "engines/stharbinger/platform.h"

#include <string>
#include <vector>

class Platform;

namespace reluva::stharbinger {

class Harbinger;

// A developer aid (start_mode=viewer, "Developer: start with" in the launcher): steps through each scene's backgrounds,
// animations and sounds and shows its hotspot rectangles, without running the
// scripts.
class SceneViewer {
public:
    explicit SceneViewer(Harbinger *vm);
    ~SceneViewer();

    int run(int startScene);

private:
    struct Hotspot {
        int index;      // in the scene's CH list
        int background; // CB index current when it was defined, or -1
        int x, y, w, h;
    };

    void enterScene(int index);
    void showBackground(int index);
    void startAnimation(int index);
    void playSound(int index);
    void collectHotspots();

    void handleEvent(const InputEvent &event);
    void updateAnimation();
    void drawScreen();
    void updateCaption();

    std::string listName(ListKind kind, int index) const;

    Harbinger *_vm;
    Platform *_system;
    Resources &_resources;
    const std::vector<Scene> &_scenes;
    bool _quit = false;

    int _sceneIndex = 0;
    int _backgroundIndex = 0;
    bool _backgroundMissing = false;
    Surface _background;
    Surface _screen;
    Palette _palette;

    std::vector<Hotspot> _hotspots;
    bool _showHotspots = false;
    int _hoverHotspot = -1;

    RleAnimation _anim;
    int _animIndex = -1;
    int _animFrame = 0;
    uint32_t _nextFrameTime = 0;

    int _soundIndex = -1;
    audio::SoundHandle _soundHandle = audio::kNoSound;

    bool _dirty = true;
};

} // namespace reluva::stharbinger
