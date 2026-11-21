// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/archive.h"
#include "common/events.h"
#include "common/system.h"
#include "engines/stawayteam/formats/text_object.h"
#include "engines/stawayteam/game/map.h"
#include "engines/stawayteam/game/options.h"
#include "engines/stawayteam/ui/animation.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace reluva::stawayteam {

class ConfirmDialog;

/**
 * A map seen through the game's view: its ground, the polygons laid on it
 * and its models, the camera scrolled by the keys and the screen's edges
 * (docs/engines/stawayteam/systems/maps.md).
 */
class MapScreen {
public:
    /** What the player chose that leaves the screen. */
    enum class Choice { kNone, kExit };

    /**
     * `map` shown with `rules`' view; `data`, `rules` and `options` must
     * outlive it.
     */
    MapScreen(const Archive &data, const TextObject &rules, GameMap map, const GameOptions &options, uint64_t now);
    ~MapScreen();

    /** What `ev` does to it at `now` (milliseconds). */
    Choice handleEvent(const Event &ev, audio::Mixer *mixer, uint64_t now);
    /** Moves the camera on to `now`, then draws the view. */
    void draw(Surface &screen, uint64_t now);

    /** The map pixel at the view's middle. */
    float cameraX() const { return _cameraX; }
    float cameraY() const { return _cameraY; }
    /** Puts the view's middle at (x, y), kept on the map. */
    void moveCamera(float x, float y);

    /** The ground's cell at (cx, cy), 64 pixels square, as drawn. */
    const image::Image &groundCell(int cx, int cy);

private:
    // What a ground value draws: a texture, and how much of the next one
    // over it (0-255) (0x538d20, the 16-bit table).
    struct Blend {
        int texture = -1;
        int next = -1;
        int weight = 0;
    };

    void scroll(float seconds);
    void drawPolygon(image::Image &cell, int cellX, int cellY, const MapPart &part);
    const image::Image &texture(const std::string &name);
    const Animation &model(const MapPart &part);

    const Archive &_data;
    const TextObject &_rules;
    GameMap _map;
    const GameOptions &_options;
    int _viewX = 0; // the view's place on the screen and its size (Screen Pos, Screen Size)
    int _viewY = 0;
    int _viewW = 0;
    int _viewH = 0;
    float _cameraX = 0.0f;
    float _cameraY = 0.0f;
    std::array<Blend, 256> _blends{};
    std::vector<image::Image> _groundTextures;
    std::map<std::string, image::Image> _textures; // the polygons', by name
    std::unordered_map<int, image::Image> _cells;
    std::vector<std::vector<size_t>> _cellPolygons; // the ground polygons over each cell
    std::map<std::string, Animation> _models;       // by model and sequence
    std::vector<size_t> _standing;                  // the models, back to front
    // The scroll keys held, and the mouse.
    bool _left = false;
    bool _right = false;
    bool _up = false;
    bool _down = false;
    int _mouseX = -1;
    int _mouseY = -1;
    uint64_t _last = 0;
    std::unique_ptr<ConfirmDialog> _confirm;
};

} // namespace reluva::stawayteam
