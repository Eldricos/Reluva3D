// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace reluva::stawayteam {

/**
 * A map's ground (`CPlasmaTileMap`): textures blended by a grid of values,
 * one at each corner of its 64-pixel cells
 * (docs/engines/stawayteam/systems/maps.md).
 */
struct GroundMap {
    float blending = 0.25f;            // how much of each texture's band blends into the next
    std::vector<std::string> textures; // of Cache/Textures, lowest values' first
    int width = 0;                     // corners across
    int height = 0;                    // corners down
    std::vector<uint8_t> values;       // width x height, top row first: which textures, 0-255
    std::vector<uint8_t> seeds;        // width x height: the blending's randomness
};

/** One of a map's parts (`Tree List`): a model placed on it, or a polygon. */
struct MapPart {
    std::string className; // "CEntityBase", "CRenderablePolygon"...
    std::string model;     // of Cache/Models; empty for a polygon
    std::string sequence;  // the model's sequence shown ("Idle")
    float x = 0.0f;        // where it stands, in the map's pixels
    float y = 0.0f;
    int renderingHeight = 0;
    bool visible = true;
    std::vector<std::pair<float, float>> polygon; // its corners, the last the first again
    std::string renderType;                       // a polygon's: "Textured", "Black", "Shadow", "Warped"...
    std::string texture;                          // a textured polygon's, of Cache/Textures
    bool onGround = false;
};

/** A map (`Levels/Mission NN/<name>.zax`, a `CLayerSaveData`), as far as drawing it goes. */
struct GameMap {
    int width = 0; // in pixels
    int height = 0;
    float viewX = 0.0f; // where the camera starts
    float viewY = 0.0f;
    std::string minimap; // of Cache/Levels
    std::string song;    // of Music
    GroundMap ground;
    std::vector<MapPart> parts;
};

/** Reads the map at `path` in `data` ("Levels/Mission 01/01 Klingon Outpost.zax"). */
Error loadMap(const Archive &data, const std::string &path, GameMap &out);

} // namespace reluva::stawayteam
