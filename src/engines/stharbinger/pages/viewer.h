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

    Harbinger *vm_;
    Platform *system_;
    Resources &resources_;
    const std::vector<Scene> &scenes_;
    bool quit_ = false;

    int sceneIndex_ = 0;
    int backgroundIndex_ = 0;
    bool backgroundMissing_ = false;
    Surface background_;
    Surface screen_;
    Palette palette_;

    std::vector<Hotspot> hotspots_;
    bool showHotspots_ = false;
    int hoverHotspot_ = -1;

    RleAnimation anim_;
    int animIndex_ = -1;
    int animFrame_ = 0;
    uint32_t nextFrameTime_ = 0;

    int soundIndex_ = -1;
    audio::SoundHandle soundHandle_ = audio::kNoSound;

    bool dirty_ = true;
};

} // namespace reluva::stharbinger
