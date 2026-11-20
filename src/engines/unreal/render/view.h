// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// What the game's screens and the data viewer's pages share: the game's
// packages and the system they draw with (ViewContext), and a screen of
// either (Page).

#include <unordered_set>

#include "engines/unreal/formats/actors.h"
#include "engines/unreal/games.h"
#include "engines/unreal/audio/level_audio.h"
#include "engines/unreal/game/level_play.h"
#include "engines/unreal/render/lighting.h"
#include "engines/unreal/render/mesh_render.h"
#include "engines/unreal/formats/model.h"
#include "engines/unreal/formats/package_set.h"
#include "engines/unreal/render/procedural.h"
#include "engines/unreal/formats/texture.h"
#include "engines/unreal/game/zones.h"

#include "audio/mixer.h"
#include "common/events.h"
#include "graphics/gfx3d.h"
#include "common/system.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace reluva::unreal {

/**
 * What every page shares: the system, the screen, the game's packages and
 * settings. The references are the viewer's or the game's, alive while the
 * pages are.
 */
struct ViewContext {
    System &sys;
    Surface &screen; // RGBA32
    const PackageSet &packages;
    const GameInfo &game;
    const std::vector<std::string> &warnings;
    audio::Mixer *mixer = nullptr; // none if the system has no audio
    /** How imported-model meshes are turned, on both 3D pages (M changes it). */
    MeshTurn meshTurn = MeshTurn::kReverse;
    int meshYawQuarters = kImportYawQuarters; // and about Z (Y changes it)
    /**
     * Where the game's saved games go (GameTarget::savePath's "Saves");
     * empty: none can be saved.
     */
    std::filesystem::path saveFolder{};
    /** The game's own folder (its movies are found from it). */
    std::filesystem::path gameFolder{};
    /** The Fallen's shared animation sets, read the first time a page needs them. */
    std::shared_ptr<const std::vector<AnimationSet>> animationSets{};
    /** animationSets, read now if they haven't been. */
    const std::vector<AnimationSet> &sets() {
        if (!animationSets)
            animationSets = sharedAnimationSets(packages);
        return *animationSets;
    }
};

/**
 * A screen of the data viewer, or the game's: one page is showing at a
 * time, and Tab moves on to the next with something to show.
 */
class Page {
public:
    /** `ctx` must outlive the page. */
    explicit Page(ViewContext &ctx) : _ctx(ctx) {}
    virtual ~Page() = default;

    /** Its name, shown in the window's title. */
    virtual const char *name() const = 0;
    /** How many things it has to show; pages with none are skipped. */
    virtual size_t count() const = 0;
    /** Redraws and retitles the window, after switching to it. */
    virtual void activate() = 0;
    /** Called when switching away from it. */
    virtual void deactivate() {}
    /** An event the viewer doesn't take itself (Tab, Escape). */
    virtual void handleEvent(const Event &ev) = 0;
    /** Once a frame, with the system's clock in milliseconds. */
    virtual void update(uint64_t /*nowMs*/) {}
    /**
     * Whether it draws in 3D: the viewer sets up 2D graphics again on
     * leaving it, and doesn't wait between its frames (presenting paces them).
     */
    virtual bool uses3D() const { return false; }
    /**
     * A key the page takes before the viewer's own use of it (Escape
     * leaving): a game's while it plays.
     */
    virtual bool ownsKey(uint32_t /*key*/) const { return false; }

protected:
    /** Fills the screen, or a rectangle of it (clipped to it), with a colour. */
    ///@{
    void clear(uint8_t r, uint8_t g, uint8_t b);
    void fill(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b);
    ///@}
    /** Draws text in the built-in font, its top left at (x, y). */
    void text(int x, int y, const std::string &s, Color c = {230, 230, 230});
    /** Titles the window "Reluva - `game` - `page`: `detail`". */
    void setTitle(const std::string &detail);

    ViewContext &_ctx;
};

} // namespace reluva::unreal
