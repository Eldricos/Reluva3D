// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <vector>

#include "common/text.h"
#include "common/stream.h"
#include "common/types.h"

namespace reluva::stgenerations {

// The space combat's files in COMBAT.DAT, which come from a separate 3D
// library ("DRE") and its own tools, unlike the rest of the game.

// A model's layout, DRE object format v1.10 (loader 0x4E4C30):
//
//   u32 size (of the rest), u16 version 110, u16 max verts, u16 max faces, char name[20]
//   then a block whose offsets count from its start:
//     +04 i32 parts, +08 u32 offset of the 44-byte part records,
//     +10 f32 extent[3], +1C f32 centre[3]
//   part: i32 verts (negative: a billboard quad), i32 faces, i32 2,
//     u32 offsets of verts (f32[3]), uvs (16.16 pairs, or 0), 0, 0,
//     faces (u16 material, a, b, c), vertex normals, face normals (f32[3]),
//     plane distances (f32)

/** A model (.3DV, .3DR, .PCn): parts of textured triangles, x across, y up, z forward. */
struct Model3D {
    /** A corner of a triangle. */
    struct Vertex {
        float x;
        float y;
        float z;
        // Texture coordinates, 0..1 across the texture.
        float u;
        float v;
    };
    /** A face: its material, its corners (indices into the part's vertices) and its normal. */
    struct Triangle {
        uint16_t material;
        uint16_t a;
        uint16_t b;
        uint16_t c;
        float nx;
        float ny;
        float nz;
    };
    /** A part of the model; a part without texture coordinates isn't textured. */
    struct Part {
        std::vector<Vertex> vertices;
        std::vector<Triangle> triangles;
        bool textured = false;
    };
    std::vector<Part> parts;
    float extent[3] = {};
    float centre[3] = {};
    // Half the largest extent.
    float radius = 0;

    /** Reads a v1.10 model; false if it's another version or damaged. */
    bool load(SeekableReadStream &stream);
};

/** One of a model's materials (.MTL, .MTR, .DMG; loader 0x4E5520), 36 bytes each. */
struct CombatMaterial {
    // RGB555, for untextured faces.
    uint32_t colour = 0;
    uint32_t param = 0;
    // 1 textured, 2 an effect (black see-through), 3 flat, 0 flat see-through.
    uint32_t flags = 0;
    std::string texture;

    /** Reads a material file, version 110; false if it's another or damaged. */
    static bool loadList(SeekableReadStream &stream, std::vector<CombatMaterial> &list);
    /** The colour as 0xAARRGGBB. */
    uint32_t rgb() const;
};

/** A true-colour picture: textures (.IMG = GIF, .LBM) and the console's .RAS. */
struct CombatImage {
    int w = 0;
    int h = 0;
    // 0xAARRGGBB; alpha 0 where see-through.
    std::vector<uint32_t> pixels;

    /** Reads a GIF87a/89a (the .IMG textures); false if it isn't one or is damaged. */
    bool loadGif(SeekableReadStream &stream);
    /** Reads an IFF PBM (.LBM: BOOM, PHOTON, PHASER1, SHIELD); false if it isn't one or is damaged. */
    bool loadLbm(SeekableReadStream &stream);
    /**
     * Reads a Sun rasterfile (.RAS, loader 0x49C23E), 8-bit with its own
     * colour map in three planes; colour (255,0,163) is see-through. False if
     * it isn't one or isn't 8-bit.
     */
    bool loadRas(SeekableReadStream &stream);
    /** A pixel; (x, y) must be inside the picture. */
    uint32_t get(int x, int y) const { return pixels[(size_t)y * w + x]; }
    // A .RAS picture's colour indices, for the ships' system maps (S???_STR).
    std::vector<uint8_t> indices;
};

/**
 * Reads the palette of the animations and the starfield (STANDARD.PAL, 6-bit
 * VGA) as 0xAARRGGBB; false if the file is short.
 */
bool loadStandardPalette(SeekableReadStream &stream, uint32_t palette[256]);

/**
 * An animation (.SPR, anim.cpp 0x4AF7B0): explosions, shield hits,
 * torpedoes and phaser beams, in STANDARD.PAL. Chunks of 'SPRT', 'LIST',
 * 'TIME' (ms a frame) and 'COMP' (a frame, packed one of four ways).
 */
struct SprAnimation {
    int w = 0;
    int h = 0;
    uint32_t frameMillis = 100;
    // Palette indices; 0 is see-through.
    std::vector<std::vector<uint8_t>> frames;

    /**
     * Reads the frames, all of one size; false if there are none or it's
     * damaged. frameMillis is at most 90, however slow the file's.
     */
    bool load(SeekableReadStream &stream);
};

/** A ship class from SYSTEMS.DAT (parser 0x49C770). */
struct ShipClass {
    /** A ship's systems, indexing `systems`. */
    enum System {
        kShip = 0,
        kShield1 = 1, // .. kShield4 = 4
        kWeapons = 5,
        kLifeSupport = 6,
        kEngines = 7,
        kPower = 8,
        kComputer = 9,
        kSensors = 10,
        kDamageControl = 11,
        kCloak = 12,
        kPhaser0 = 13, // .. 22
        kPhoton0 = 23, // .. 26
        kSystemCount = 27
    };
    /** One system's figures, as SYSTEMS.DAT gives them. */
    struct SystemInfo {
        // -1: the ship doesn't have it. Photon tubes: torpedoes a salvo.
        int max = -1;
        int charge = 0;
        int discharge = 0;
        int repair = 0;
        // Systems 1..12: cumulative threshold for random hits, and % for an aimed hit.
        int threshold = 0;
        int aimed = 0;
        // Weapons: the octants (0..7) the bank or tube fires into.
        int arcs = 0;
    };
    std::string token;
    int maxSpeed = 500;
    int agility = 50;
    int hitPoints = 100;
    int closeRadius = 5000;
    int destruct = 50;
    int phasers = 0;
    int photons = 0;
    int shields = 0;
    int torpedoes = 0;
    SystemInfo systems[kSystemCount];
    // % of a hit leaking past each shield, by shield energy 120, 110 .. 0.
    int bleed[5][13] = {};
    int phaserRange = 20000;
    int photonRange = 30000;
    int photonDamage = 30;
    // Spread, in degrees, of a salvo of 1..10 torpedoes.
    int cone[10] = {};

    /** Reads every class in SYSTEMS.DAT's text; false if there are none or one is damaged. */
    static bool loadAll(const std::string &text, std::vector<ShipClass> &classes);
};

/** A ship line of SCn.MIS (parser 0x4A36A4). */
struct MissionShip {
    std::string type;
    int x = 0;
    int y = 0;
    int z = 0;
    int yaw = 0;
    int pitch = 0;
    std::string ai;
    int wave = 1; // set by the ":wave n" line before it
};

/** Reads the ships of a mission's text (SCn.MIS); false if it has none. */
bool loadCombatMission(const std::string &text, std::vector<MissionShip> &ships);

/** A font (.MET): the cells of ASCII 32..127 in a .RAS strip, big endian. */
struct MetFont {
    /** Where a character's picture is in the strip, in pixels. */
    struct Cell {
        int x;
        int y;
        int w;
        int h;
    };
    Cell cells[96] = {};

    /** Reads the cells; false if the file is short. */
    bool load(SeekableReadStream &stream);
};

} // namespace reluva::stgenerations
