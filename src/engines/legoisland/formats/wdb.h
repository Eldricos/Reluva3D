// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace reluva::legoisland {

/** One picture of a texture: 8-bit indices into its own palette. */
struct WdbImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> palette; // red, green, blue for each colour
    std::vector<uint8_t> pixels;  // width * height, top row first; each below the palette's count

    /** The picture in colour, every pixel opaque. */
    void toRGBA(std::vector<uint8_t> &out) const;
};

/**
 * A texture, by the name meshes give it ("dogface.gif"). A name starting
 * with `^` has a second picture after the first (docs/engines/legoisland/formats/wdb.md).
 */
struct WdbTexture {
    std::string name;
    std::vector<WdbImage> images; // one, or two for a `^` name
};

/** A corner of a mesh's triangles: which of its LOD's vertices, normals and texture coordinates it takes. */
struct WdbCorner {
    uint16_t vertex = 0;
    uint16_t normal = 0;
    uint32_t texCoord = kNone; // kNone in an untextured mesh

    static constexpr uint32_t kNone = 0xFFFFFFFF;
};

/** Part of a LOD drawn in one colour or texture. */
struct WdbMesh {
    std::vector<WdbCorner> corners;
    std::vector<uint16_t> triangles;  // 3 indices into `corners` for each
    std::array<uint8_t, 3> color{};   // red, green, blue
    float transparency = 0;           // 0 for an opaque mesh; 0.6 in the windows (inferred)
    uint8_t shading = 0;              // 1 smooth, 0 flat: the materials named "... F" are 0 (inferred)
    std::array<uint8_t, 3> unknown{}; // 0 or 10, 0 or 1, 0 or 1 (formats/wdb.md)
    std::string texture;              // empty for none
    std::string material;             // "LEGO RED", "INH LEGO RED F"...
};

/** One level of detail of an ROI: its points, and the meshes drawn with them. */
struct WdbLod {
    uint32_t flags = 0; // 8 on the first LOD of some of the characters' parts, else 0
    std::vector<std::array<float, 3>> vertices;
    std::vector<std::array<float, 3>> normals;
    std::vector<std::array<float, 2>> texCoords;
    std::vector<WdbMesh> meshes;
};

/**
 * An ROI ("real-time object instance"): a thing drawn, with its levels of
 * detail, least detailed first. A model's ROIs nest; a part's don't, and
 * have no bounds.
 */
struct WdbRoi {
    std::string name;
    std::array<float, 3> sphereCenter{};
    float sphereRadius = 0;
    std::array<float, 3> boxMin{}; // an empty box (888888 to -888888) for an ROI with nothing of its own
    std::array<float, 3> boxMax{};
    std::string indirectName; // "indir-f-<name>" in some of a model's ROIs, else empty
    bool usesPart = false;    // a model's ROI whose LODs are a part's (partName())
    std::vector<WdbLod> lods;
    std::vector<WdbRoi> children;

    /** The part whose LODs a usesPart ROI takes: its name, without any digits at its end (inferred). */
    std::string partName() const;
};

/** One key of an animation: when, and the value from then on. */
struct WdbKey {
    uint32_t time = 0;            // in milliseconds (inferred)
    uint8_t flags = 0;            // 1 in most, also 0, 4 and 5
    std::array<float, 4> value{}; // x, y, z; a rotation's quaternion w, x, y, z; a morph's visibility in [0]
};

/** A node of a model's animation, named after the ROI it moves. */
struct WdbAnimNode {
    std::string name;
    std::vector<WdbKey> translation;
    std::vector<WdbKey> rotation;
    std::vector<WdbKey> scale;
    std::vector<WdbKey> morph;
    std::vector<WdbAnimNode> children;
};

/** A world's model: its data's place in the file, and where the world puts it. */
struct WdbModelRef {
    std::string name;
    uint32_t size = 0;
    uint32_t offset = 0;
    std::string presenter; // "LegoEntityPresenter", or empty
    std::array<float, 3> location{};
    std::array<float, 3> direction{};
    std::array<float, 3> up{};
    uint8_t flag = 0; // 1 for the island's buildings, terrain and race tracks, 0 for the rest
};

/** A world's part: its data's place in the file. */
struct WdbPartRef {
    std::string name;
    uint32_t size = 0;
    uint32_t offset = 0;
};

/** A world (a part of the game, as "ACT1" or "HOSP"), and the parts and models it loads. */
struct WdbWorld {
    std::string name;
    std::vector<WdbPartRef> parts;
    std::vector<WdbModelRef> models;
};

/** A part: ROIs with no children, shared by the models that name them, and their textures. */
struct WdbPart {
    std::vector<WdbRoi> rois;
    std::vector<WdbTexture> textures;
};

/** A model: its animation, its tree of ROIs, and its textures. */
struct WdbModel {
    uint32_t version = 0;                 // 19 in every model
    std::array<uint32_t, 3> animHeader{}; // 1, 0, 0 in every model
    WdbAnimNode animation;
    WdbRoi roi;
    std::vector<WdbTexture> textures;
};

/**
 * `WORLD.WDB`, the world database: the worlds, the textures and part
 * every world shares, and each world's parts and models, read from the
 * file when asked for (docs/engines/legoisland/formats/wdb.md).
 */
class WdbFile {
public:
    /** Reads the file, and its worlds, shared textures and shared part. */
    Error open(const std::filesystem::path &path);
    /** The same from the file's bytes, kept. */
    Error load(std::vector<uint8_t> data);

    /** The worlds, in the file's order. */
    const std::vector<WdbWorld> &worlds() const { return _worlds; }
    /** The textures every world shares: the characters' faces and chests. */
    const std::vector<WdbTexture> &sharedTextures() const { return _sharedTextures; }
    /** The part every world shares: the characters' bodies, hats, and the plants. */
    const WdbPart &sharedPart() const { return _sharedPart; }

    /** Each part and model once, though several worlds may load it, in the file's order. */
    ///@{
    std::vector<WdbPartRef> allParts() const;
    std::vector<WdbModelRef> allModels() const;
    ///@}

    /** A part's or a model's data. */
    ///@{
    Error readPart(const WdbPartRef &ref, WdbPart &out) const;
    Error readModel(const WdbModelRef &ref, WdbModel &out) const;
    ///@}

private:
    std::vector<uint8_t> _data;
    std::vector<WdbWorld> _worlds;
    std::vector<WdbTexture> _sharedTextures;
    WdbPart _sharedPart;
};

} // namespace reluva::legoisland
