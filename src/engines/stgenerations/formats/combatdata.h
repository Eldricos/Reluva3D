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

// A model (.3DV, .3DR, .PCn), DRE object format v1.10 (loader 0x4E4C30):
// parts of textured triangles, x across, y up, z forward.
//
//   u32 size (of the rest), u16 version 110, u16 max verts, u16 max faces, char name[20]
//   then a block whose offsets count from its start:
//     +04 i32 parts, +08 u32 offset of the 44-byte part records,
//     +10 f32 extent[3], +1C f32 centre[3]
//   part: i32 verts (negative: a billboard quad), i32 faces, i32 2,
//     u32 offsets of verts (f32[3]), uvs (16.16 pairs, or 0), 0, 0,
//     faces (u16 material, a, b, c), vertex normals, face normals (f32[3]),
//     plane distances (f32)
struct Model3D {
    struct Vertex {
        float x, y, z;
        // Texture coordinates, 0..1 across the texture.
        float u, v;
    };
    struct Triangle {
        uint16_t material, a, b, c;
        float nx, ny, nz;
    };
    struct Part {
        std::vector<Vertex> vertices;
        std::vector<Triangle> triangles;
        bool textured = false;
    };
    std::vector<Part> parts;
    float extent[3] = {}, centre[3] = {};
    // Half the largest extent.
    float radius = 0;

    bool load(SeekableReadStream &stream);
};

// A model's materials (.MTL, .MTR, .DMG; loader 0x4E5520), 36 bytes each.
struct CombatMaterial {
    // RGB555, for untextured faces.
    uint32_t colour = 0;
    uint32_t param = 0;
    // 1 textured, 2 an effect (black see-through), 3 flat, 0 flat see-through.
    uint32_t flags = 0;
    std::string texture;

    static bool loadList(SeekableReadStream &stream, std::vector<CombatMaterial> &list);
    // The colour as 0xAARRGGBB.
    uint32_t rgb() const;
};

// A true-colour picture: textures (.IMG = GIF, .LBM) and the console's .RAS.
struct CombatImage {
    int w = 0, h = 0;
    // 0xAARRGGBB; alpha 0 where see-through.
    std::vector<uint32_t> pixels;

    // A GIF87a/89a (the .IMG textures).
    bool loadGif(SeekableReadStream &stream);
    // An IFF PBM (.LBM: BOOM, PHOTON, PHASER1, SHIELD).
    bool loadLbm(SeekableReadStream &stream);
    // A Sun rasterfile (.RAS, loader 0x49C23E), 8-bit with its own colour
    // map in three planes; colour (255,0,163) is see-through.
    bool loadRas(SeekableReadStream &stream);
    uint32_t get(int x, int y) const { return pixels[(size_t)y * w + x]; }
    // A .RAS picture's colour indices, for the ships' system maps (S???_STR).
    std::vector<uint8_t> indices;
};

// The palette of the animations and the starfield (STANDARD.PAL, 6-bit VGA).
bool loadStandardPalette(SeekableReadStream &stream, uint32_t palette[256]);

// An animation (.SPR, anim.cpp 0x4AF7B0): explosions, shield hits,
// torpedoes and phaser beams, in STANDARD.PAL. Chunks of 'SPRT', 'LIST',
// 'TIME' (ms a frame) and 'COMP' (a frame, packed one of four ways).
struct SprAnimation {
    int w = 0, h = 0;
    uint32_t frameMillis = 100;
    // Palette indices; 0 is see-through.
    std::vector<std::vector<uint8_t>> frames;

    bool load(SeekableReadStream &stream);
};

// A ship class from SYSTEMS.DAT (parser 0x49C770).
struct ShipClass {
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
    struct SystemInfo {
        // -1: the ship doesn't have it. Photon tubes: torpedoes a salvo.
        int max = -1;
        int charge = 0, discharge = 0, repair = 0;
        // Systems 1..12: cumulative threshold for random hits, and % for an aimed hit.
        int threshold = 0, aimed = 0;
        // Weapons: the octants (0..7) the bank or tube fires into.
        int arcs = 0;
    };
    std::string token;
    int maxSpeed = 500, agility = 50, hitPoints = 100, closeRadius = 5000, destruct = 50;
    int phasers = 0, photons = 0, shields = 0, torpedoes = 0;
    SystemInfo systems[kSystemCount];
    // % of a hit leaking past each shield, by shield energy 120, 110 .. 0.
    int bleed[5][13] = {};
    int phaserRange = 20000, photonRange = 30000, photonDamage = 30;
    // Spread, in degrees, of a salvo of 1..10 torpedoes.
    int cone[10] = {};

    static bool loadAll(const std::string &text, std::vector<ShipClass> &classes);
};

// A ship line of SCn.MIS (parser 0x4A36A4).
struct MissionShip {
    std::string type;
    int x = 0, y = 0, z = 0, yaw = 0, pitch = 0;
    std::string ai;
    int wave = 1;
};

bool loadCombatMission(const std::string &text, std::vector<MissionShip> &ships);

// A font (.MET): the cells of ASCII 32..127 in a .RAS strip, big endian.
struct MetFont {
    struct Cell {
        int x, y, w, h;
    };
    Cell cells[96] = {};

    bool load(SeekableReadStream &stream);
};

} // namespace reluva::stgenerations
