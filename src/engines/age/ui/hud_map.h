// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "graphics/gfx3d.h"
#include "common/types.h"
#include "engines/age/render/render_cache.h"
#include "engines/age/vecmath.h"

#include <string>
#include <string_view>
#include <vector>

namespace reluva::age {

/**
 * tune/<city>.mmHudMap: where the HUD's map goes and how near it looks.
 * See docs/engines/age/games/mm2/systems/hud.md.
 */
struct HudMapSetup {
    // Size: shares of the screen
    float width = 0.21f;
    float height = 0.25f;
    // Pos: its top left corner, likewise
    float left = 0.78f;
    float top = 0.75f;
    bool zoomIn = false;       // starting near
    float approachRate = 1.2f; // a second, towards a new zoom
    // Heights it's seen from (metres), near and far, and its icons' lengths
    // (metres) there; the FS ones full screen.
    float zoomInDist = 577;
    float zoomOutDist = 1195;
    float iconScaleMin = 34;
    float iconScaleMax = 52.4f;
    float zoomInDistFS = 786;
    float zoomOutDistFS = 1581;
    float iconScaleMinFS = 15;
    float iconScaleMaxFS = 18;
    Vec3 ocean{0.084f, 0.7f, 0.94f}; // Ocean Color
};
Error parseHudMapSetup(std::string_view text, HudMapSetup &out);

/**
 * Something shown on the map: a car (geometry/hudmap_tri, its paint job
 * `color` 0-9, pointing along `forward`) or a dot (hudmap_square, paint
 * job 0-8: RED_DOT, BLUE_DOT, GREEN_DOT, GREY_DOT, YELLOW_DOT, FINISH_DOT,
 * GOLD_DOT, BANK_DOT, HIDEOUT_DOT).
 */
struct HudMapIcon {
    Vec3 position{};
    Vec3 forward{0, 0, -1};
    int color = 0;
    bool dot = false;
};

namespace hudcolor {
/** hudmap_tri's paint jobs as Reluva uses them. */
constexpr int kPlayer = 5;   // yellow
constexpr int kOpponent = 4; // red
constexpr int kPolice = 8;   // blue
/** hudmap_square's. */
constexpr int kCheckpoint = 0; // RED_DOT
constexpr int kNext = 2;       // GREEN_DOT
constexpr int kFinish = 5;     // FINISH_DOT
} // namespace hudcolor

/**
 * The HUD's map: the city's geometry/hudmap_<city> (its map pictures laid
 * flat on the city's ground plan) on the ocean's colour, seen from above
 * the player, turned so the way they face is up.
 */
class HudMap {
public:
    HudMap() = default;
    HudMap(const HudMap &) = delete;
    HudMap &operator=(const HudMap &) = delete;
    ~HudMap() { unload(); }

    /** False if the city has none. */
    bool load(gfx::Renderer3D &renderer, const SearchSet &files, ModelCache &models, const std::string &map);
    void unload();
    bool loaded() const { return _ok; }
    const HudMapSetup &setup() const { return _setup; }

    void toggleZoom() { _zoomIn = !_zoomIn; }
    void toggleFullScreen() { _full = !_full; }
    bool fullScreen() const { return _full; }
    /**
     * The corner map's sizes, then off (the game's Map Toggle: "toggle
     * between two different sizes of city map, or turn the map on and
     * off"); full screen shows either way. The larger is Reluva's: 1.6
     * times, its bottom right corner kept.
     */
    void toggleShown() { _size = (_size + 1) % 3; }
    bool shown() const { return _size < 2 || _full; }
    /** Turned with the player (the Rotating Map), else north up. */
    void toggleRotating() { _rotating = !_rotating; }
    bool rotating() const { return _rotating; }
    /** Eases the height towards the zoom's. */
    void update(float dt);
    /** The height it's seen from now. */
    float distance() const { return _distance; }

    /**
     * Over the frame drawn so far (frameW x frameH pixels), about `center`
     * facing `forward`; leaves the whole frame as the viewport after.
     */
    void draw(gfx::Renderer3D &renderer, ModelCache &models, const Vec3 &center, const Vec3 &forward,
              const std::vector<HudMapIcon> &icons, int frameW, int frameH) const;

private:
    float targetDistance() const;
    float iconLength() const;

    gfx::Renderer3D *_renderer = nullptr;
    HudMapSetup _setup;
    std::string _model;
    gfx::MeshId _quad = 0; // the ocean
    bool _ok = false;
    bool _zoomIn = false;
    bool _full = false;
    bool _rotating = true;
    int _size = 0; // 0 the corner's, 1 larger, 2 off
    float _distance = 0;
};

} // namespace reluva::age
