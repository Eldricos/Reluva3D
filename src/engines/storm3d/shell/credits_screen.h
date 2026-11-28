// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/storm3d/formats/credits_script.h"
#include "engines/storm3d/game_files.h"
#include "engines/storm3d/scene/sod_meshes.h"
#include "engines/storm3d/scene/sprite_font.h"
#include "graphics/gfx3d.h"
#include "image/image.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::storm3d {

/**
 * The credits, as the game draws them in 3D: the stars' background
 * (`mbgstars.sod`) around the camera, and before it the script's pages
 * (`Credits.txt`) one after another, each line's letters the font
 * model's (`font.sod`), fading in, held and fading out; at the top
 * left, "Press any key to exit" in the game's sprite font. See
 * docs/engines/storm3d/games/armada/systems/front-end.md.
 */
class CreditsScreen {
public:
    /** A letter of the font model: its triangles, in the model's units, and where it starts and ends across. */
    struct Glyph {
        std::vector<std::array<float, 2>> corners; // three a triangle
        float left = 0;
        float width = 0;
    };
    /** A letter placed: its glyph's triangles in the original's screen coordinates (pixels, D3D's). */
    struct Placed {
        std::vector<std::array<float, 2>> corners;
    };
    /**
     * Lays a page out as the game does: each line centred across the
     * screen, the letters a gap apart, the lines a pitch apart and the
     * block centred down it; the letters sized by the page's scale, the
     * gap not.
     */
    static std::vector<Placed> layout(const CreditsScript::Page &page, const std::map<char, Glyph> &font);
    /** A page's letters' colour, from the script's: the material lit by the scene's light. */
    static std::array<float, 3> litColour(const std::array<float, 3> &colour);
    /** A page at a moment: how opaque, how near (1 where it holds, less as it goes away), whether it's over. */
    struct Moment {
        float alpha = 0;
        float scale = 1;
        bool over = false;
    };
    /** A page `seconds` after its turn began. */
    static Moment momentAt(const CreditsScript::Page &page, double seconds);

    explicit CreditsScreen(const GameFiles &files) : _files(files), _meshes(files) {}
    ~CreditsScreen() { stop(); }
    CreditsScreen(const CreditsScreen &) = delete;
    CreditsScreen &operator=(const CreditsScreen &) = delete;

    /**
     * Starts the credits at `millis`, drawn through `renderer`: the
     * shell's, `Credits.txt` on its own screen; or a mission's
     * (StartCredits), `Credits2.txt`, its pages over the mission's frame.
     * @return a failure if the script or the font can't be read
     */
    Error start(gfx::Renderer3D *renderer, uint64_t millis, bool inMission = false);
    /** Lets go of the renderer and what it made. */
    void stop();
    /** Draws them as at `millis`, a frame of their own: the stars, the page and the caption. */
    void draw(uint64_t millis);
    /**
     * Draws the page as at `millis` into the frame being drawn, after the
     * last page the first again (0x5936a2).
     */
    void drawPage(uint64_t millis);

private:
    void buildPage(size_t page);

    const GameFiles &_files;
    SodMeshes _meshes;
    gfx::Renderer3D *_renderer = nullptr;
    const SodMeshes::Built *_background = nullptr;
    CreditsScript _script;
    std::map<char, Glyph> _font;
    SpriteFont _caption;
    Surface _overlay;
    gfx::MeshId _pageMesh = 0;
    uint32_t _pageIndices = 0;
    size_t _page = 0;
    size_t _built = SIZE_MAX;
    uint64_t _pageStart = 0;
};

} // namespace reluva::storm3d
