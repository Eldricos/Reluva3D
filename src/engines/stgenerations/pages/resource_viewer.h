// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/stgenerations/game/generations.h"

#include <string>
#include <vector>

namespace reluva::stgenerations {

/**
 * The data viewer (reluva-viewer): the game's images, overlays, sprites,
 * levels (floor plan, 3D view, textures) and sounds, a page at a time, and
 * its away missions played on their own from a list over a starfield.
 *
 * It is the game without its front end: the levels and missions are the
 * game's own, run by Generations, and this adds the pages and keys around
 * them. Esc leaves it (from a mission, back to the list).
 */
class ResourceViewer : public Generations {
public:
    ResourceViewer(Platform *system, Options options);

protected:
    Error start() override;
    void keyDown(const KeyState &kbd) override;
    void click(int x, int y) override;
    bool drawPage(uint32_t delta) override;
    void missionOver() override;
    void clearView() override;
    Error restorePlace(int imageIndex, int soundIndex) override;

private:
    struct Star {
        int32_t x;
        int32_t y;
        int32_t z;
    };

    // Every page wraps round at either end.
    void showImage(int index);
    void showOverlay(const ResourceRef &ref);
    void showSprites(const ResourceRef &ref);
    // Jump to the first image of the next (direction 1) or previous (-1) archive.
    void jumpArchive(int direction);
    // Show the next (1) or previous (-1) 3D level, GAME.LV2.
    void jumpLevel(int direction);
    void playSound(int index);
    void playBeep(int frequency);
    void drawStarfield(uint32_t delta);
    // The starfield with nothing loaded over it, where the mission list is.
    bool isOnStarfield() const;
    void drawMissionList();

    std::vector<Star> _stars;
    BitmapDecoder _image;
    bool _imageLoaded = false;
    Overlay _overlay;
    bool _overlayLoaded = false;
    int _overlayFrameHeight = 0;
    int _overlayFrame = 0;
    uint32_t _overlayTime = 0; // ms; also animates sprites
    SpriteSet _sprites;
    bool _spritesLoaded = false;
    int _movieChoice = 0;
};

} // namespace reluva::stgenerations
