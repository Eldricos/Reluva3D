// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/sith/formats/text.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace reluva::sith {

/**
 * A .jkl level: its geometry (sectors, their surfaces, and the adjoins
 * between them), the resources it uses, and the things placed in it
 * (docs/engines/sith/games/jk/formats/jkl.md).
 */
struct Level {
    /** The HEADER section's settings. */
    struct Header {
        int version = 0;
        float gravity = 0;
        float ceilingSkyZ = 0;
        float horizonDistance = 0;
        float horizonPixelsPerRev = 0;
        std::array<float, 2> horizonSkyOffset{};
        std::array<float, 2> ceilingSkyOffset{};
        std::array<float, 4> mipmapDistances{};
        std::array<float, 4> lodDistances{};
        float perspectiveDistance = 0;
        float gouraudDistance = 0;
    };

    /** A material the level uses, and how often it repeats across a texel. */
    struct MaterialRef {
        std::string name;
        float xTile = 1;
        float yTile = 1;
    };

    /** The link through a surface into the sector beyond. */
    struct Adjoin {
        uint32_t flags = 0;
        /** The adjoin on the other side, back the other way. */
        int mirror = -1;
        float distance = 0;
    };

    /** A polygon of a sector: a wall, a floor, or an adjoin into the next sector. */
    struct Surface {
        /** Into the level's materials; -1 for none (an open adjoin). */
        int material = -1;
        uint32_t surfaceFlags = 0;
        uint32_t faceFlags = 0;
        int geometryMode = 0;
        int lightingMode = 0;
        int textureMode = 0;
        /** Into the level's adjoins; -1 for a solid surface. */
        int adjoin = -1;
        float extraLight = 0;
        /** Its corners: into the level's vertices and texture vertices (-1 for none). */
        ///@{
        std::vector<int> vertices;
        std::vector<int> texVertices;
        ///@}
        /** Each corner's light. */
        std::vector<float> intensities;
        /**
         * Each corner's red, green and blue light, in Mysteries of the
         * Sith's levels; empty in Jedi Knight's.
         */
        std::vector<Vec3> colours;
        Vec3 normal;
    };

    /** A convex room: its surfaces are a run of the level's. */
    struct Sector {
        uint32_t flags = 0;
        float ambientLight = 0;
        float extraLight = 0;
        /** Into the level's colormaps. */
        int colormap = 0;
        Vec3 tint;
        std::array<Vec3, 2> bounds{};
        std::optional<std::array<Vec3, 2>> collideBox;
        Vec3 center;
        float radius = 0;
        /** Its ambient sound and that sound's volume, if it has one. */
        std::string sound;
        float soundVolume = 0;
        std::vector<int> vertices;
        int firstSurface = 0;
        int surfaceCount = 0;
    };

    /** A cog: a script and the values it starts with. */
    struct Cog {
        std::string script;
        std::vector<std::string> values;
    };

    /** A thing's template: its own settings over its base template's. */
    struct Template {
        std::string name;
        /** The template it's based on; "none" for none. */
        std::string base;
        /** Its settings as written: "key=value" words. */
        std::vector<std::string> params;
    };

    /** A thing placed in the level. */
    struct Thing {
        std::string templateName;
        std::string name;
        Vec3 position;
        /** Pitch, yaw and roll, in degrees. */
        Vec3 rotation;
        int sector = -1;
        /** Settings over its template's, as written. */
        std::vector<std::string> params;
    };

    Header header;
    std::vector<std::string> sounds;
    std::vector<MaterialRef> materials;
    std::vector<std::string> colormaps;
    std::vector<Vec3> vertices;
    std::vector<std::array<float, 2>> texVertices;
    std::vector<Adjoin> adjoins;
    std::vector<Surface> surfaces;
    std::vector<Sector> sectors;
    std::vector<std::string> aiClasses;
    std::vector<std::string> models;
    std::vector<std::string> sprites;
    std::vector<std::string> keyframes;
    std::vector<std::string> puppets;
    std::vector<std::string> soundClasses;
    std::vector<std::string> scripts;
    std::vector<Cog> cogs;
    std::vector<Template> templates;
    std::vector<Thing> things;

    /** The template of this name, matched ignoring case, or nullptr. */
    const Template *findTemplate(const std::string &name) const;
    /**
     * A thing's setting, from its own params, else its template's, else
     * the template's bases'; empty if none has it.
     */
    std::string setting(const Thing &thing, const std::string &key) const;
};

/** Reads a .jkl file; fails, saying where, if a line can't be read. */
Error parseLevel(const std::vector<uint8_t> &data, Level &out);

} // namespace reluva::sith
