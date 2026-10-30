// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/gfx3d.h"
#include "common/types.h"
#include "engines/age/render_cache.h"
#include "engines/age/vecmath.h"

#include <string>
#include <string_view>
#include <vector>

namespace reluva::age {

// tune/<city>.mmHudMap: where the HUD's map goes and how near it looks.
// See docs/engines/age-driving.md.
struct HudMapSetup {
    float width = 0.21f, height = 0.25f; // Size: shares of the screen
    float left = 0.78f, top = 0.75f;     // Pos: its top left corner, likewise
    bool zoomIn = false;                 // starting near
    float approachRate = 1.2f;           // a second, towards a new zoom
    // Heights it's seen from (metres), near and far, and its icons' lengths
    // (metres) there; the FS ones full screen.
    float zoomInDist = 577, zoomOutDist = 1195, iconScaleMin = 34, iconScaleMax = 52.4f;
    float zoomInDistFS = 786, zoomOutDistFS = 1581, iconScaleMinFS = 15, iconScaleMaxFS = 18;
    Vec3 ocean{0.084f, 0.7f, 0.94f}; // Ocean Color
};
Error parseHudMapSetup(std::string_view text, HudMapSetup& out);

// Something shown on the map: a car (geometry/hudmap_tri, its paint job
// `color` 0-9, pointing along `forward`) or a dot (hudmap_square, paint
// job 0-8: RED_DOT, BLUE_DOT, GREEN_DOT, GREY_DOT, YELLOW_DOT, FINISH_DOT,
// GOLD_DOT, BANK_DOT, HIDEOUT_DOT).
struct HudMapIcon {
    Vec3 position{}, forward{0, 0, -1};
    int color = 0;
    bool dot = false;
};

namespace hudcolor {
// hudmap_tri's paint jobs as Reluva uses them.
constexpr int kPlayer = 5;   // yellow
constexpr int kOpponent = 4; // red
constexpr int kPolice = 8;   // blue
// hudmap_square's.
constexpr int kCheckpoint = 0; // RED_DOT
constexpr int kNext = 2;       // GREEN_DOT
constexpr int kFinish = 5;     // FINISH_DOT
} // namespace hudcolor

// The HUD's map: the city's geometry/hudmap_<city> (its map pictures laid
// flat on the city's ground plan) on the ocean's colour, seen from above
// the player, turned so the way they face is up.
class HudMap {
public:
    HudMap() = default;
    HudMap(const HudMap&) = delete;
    HudMap& operator=(const HudMap&) = delete;
    ~HudMap() { unload(); }

    // False if the city has none.
    bool load(gfx::Renderer3D& renderer, const SearchSet& files, ModelCache& models, const std::string& map);
    void unload();
    bool loaded() const { return ok_; }
    const HudMapSetup& setup() const { return setup_; }

    void toggleZoom() { zoomIn_ = !zoomIn_; }
    void toggleFullScreen() { full_ = !full_; }
    bool fullScreen() const { return full_; }
    // The corner map on or off (the game's Map Toggle); full screen shows
    // either way.
    void toggleShown() { shown_ = !shown_; }
    bool shown() const { return shown_ || full_; }
    // Eases the height towards the zoom's.
    void update(float dt);
    // The height it's seen from now.
    float distance() const { return distance_; }

    // Over the frame drawn so far (frameW x frameH pixels), about `center`
    // facing `forward`; leaves the whole frame as the viewport after.
    void draw(gfx::Renderer3D& renderer, ModelCache& models, const Vec3& center, const Vec3& forward,
              const std::vector<HudMapIcon>& icons, int frameW, int frameH) const;

private:
    float targetDistance() const;
    float iconLength() const;

    gfx::Renderer3D* renderer_ = nullptr;
    HudMapSetup setup_;
    std::string model_;
    gfx::MeshId quad_ = 0; // the ocean
    bool ok_ = false, zoomIn_ = false, full_ = false, shown_ = true;
    float distance_ = 0;
};

} // namespace reluva::age
