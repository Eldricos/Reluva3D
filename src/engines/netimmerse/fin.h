// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace reluva::netimmerse {

// A LEGO Alpha Team model (.FIN): a NetImmerse stream of objects, each
// written as its class name, its address when saved (its id, which links
// between objects use) and its fields. See docs/engines/netimmerse/formats/fin.md.
//
// The objects are found by their headers, then each class that's
// understood is read; a class that isn't is skipped without losing the
// rest. Every object records whether its fields
// were read to exactly its end, which `reluva-tool fin-check` counts.

// NiAVObject: anything placed in the scene.
struct FinAvObject {
    std::string name;
    // Not drawn until the game shows it: effects such as sparks,
    // explosions and a stunned agent's stars.
    bool hidden = false;
    std::array<float, 3> translation{};
    std::array<float, 9> rotation{1, 0, 0, 0, 1, 0, 0, 0, 1}; // row by row
    float scale = 1;
    std::vector<uint32_t> properties; // ids
};

// NiNode and its subclasses (NiBillboardNode, NiLODNode, the 3ds animation
// nodes and bones, the game's DDUnit and DDEnv).
struct FinNode : FinAvObject {
    std::vector<uint32_t> children; // ids
    // NiLODNode: one child per level of detail, the first the nearest.
    bool levelsOfDetail = false;
};

// NiTriShape (and NiEnvMappedTriShape): a triangle mesh with its data
// inline.
struct FinShape : FinAvObject {
    uint16_t vertexCount = 0;
    std::vector<float> vertices;      // x, y, z per vertex
    std::vector<float> normals;       // empty, or x, y, z per vertex
    std::array<float, 4> bound{};     // centre and radius
    uint16_t uvSets = 0;
    std::vector<float> uvw;           // uvSets sets of u, v, w per vertex
    std::vector<float> colors;        // empty, or r, g, b, a (0..1) per vertex
    std::vector<uint16_t> triangles;  // three indices each
};

struct FinMaterial {
    std::array<float, 3> ambient{}, diffuse{}, specular{}, emissive{};
    float glossiness = 0, alpha = 1;
};

// Ni3dsSkin: a shape whose vertices follow bones. Each vertex has its
// influences: a bone, a weight and the vertex's position in the bone's
// space. (In the retail models every vertex has one, of weight 1.) The
// shape's own vertices are where they were when modelled.
struct FinSkin : FinShape {
    struct Influence {
        uint32_t bone = 0; // id of an Ni3dsBone
        float weight = 1;
        std::array<float, 3> offset{};
    };
    std::vector<uint32_t> firstInfluence; // per vertex, into influences; one more at the end
    std::vector<Influence> influences;
};

// Ni3dsMorphShape: a shape whose vertices move by weights keyed over time.
// The first target is the shape as modelled; the others are offsets from
// it, each added times its weight.
struct FinMorph : FinShape {
    struct Key {
        float time = 0;              // ticks
        std::vector<float> weights;  // one per target
    };
    std::vector<Key> keys;
    std::vector<std::vector<float>> targets; // each x, y, z per vertex
};

// Animation times are 3ds Max ticks, 4800 a second.
constexpr float kTicksPerSecond = 4800;

// Ni3dsAnimationCore's settings (NiAnimation's AnimCoreLoadBinary): a
// time t is t × frequency + phase, then looped, played back and forth or
// clamped between the begin and end key times (`start`, `stop`).
struct FinController {
    uint32_t cycle = 0; // 0 loop, 1 reverse (back and forth), 2 clamp
    float frequency = 1, phase = 0, start = 0, stop = 0;
    uint32_t target = 0; // id of what it animates
};

// NiFlipTextures (an NiAction): cycles its target NiTextureProperty's
// images. At a time t (ticks), the image is images × (t − start) ÷
// duration, past the ends as `outOfBounds` says. The rate is already in
// the duration (images × 160 ÷ rate in every file: 160 ticks, a 30th of
// a second, an image at rate 1).
struct FinFlip {
    enum OutOfBounds : uint32_t { Loop = 0, Reverse = 1, Clamp = 2 }; // reverse: back and forth
    uint32_t outOfBounds = Loop;
    float rate = 1, startTime = 0, duration = 0;
    uint32_t target = 0; // id of its NiTextureProperty
    // Its image at time t of n.
    size_t image(double t, size_t images) const;
};

// Ni3dsColorAnimator and Ni3dsAlphaAnimator: a material's colour or alpha
// over time, keyed.
struct FinMaterialAnimator : FinController {
    uint32_t channel = 0; // colour: 0 ambient, 1 diffuse, 3 emissive
    bool alpha = false;
    struct Key {
        float time = 0; // ticks
        std::array<float, 3> value{}; // alpha: the first only
    };
    std::vector<Key> keys;
};

// NiLight, as NiMain's LoadBinary reads it and NiDirect3d lights vertices
// with it (fin.md).
struct FinLight : FinAvObject {
    enum Type : uint32_t { Ambient = 0, Directional = 1, Point = 2, Spot = 3 };
    uint32_t type = Point;
    bool on = true; // the light switch
    std::array<float, 3> location{}, direction{};
    std::array<float, 3> ambient{}, diffuse{}, specular{};
    float spotAngle = 0, spotExponent = 0; // degrees
    float dimmer = 1; // each colour is times this
    // When `attenuated`: falling off with distance, to 1/64 at
    // `attenuationDistance`, more steeply the higher the curve.
    bool attenuated = false;
    float attenuationDistance = 0, attenuationCurve = 0;
    std::vector<uint32_t> illuminated; // ids: the nodes it lights (none in the retail files)
};

// DDActorSharedData: the game's own data for a model. Its clips (named
// stretches of the one animation timeline), and the keys for each 3ds node
// (Ni3dsAnimationNode, Ni3dsBone), in the order a depth-first walk of the
// scene graph meets them.
struct FinActor {
    struct Clip {
        std::string name;
        float start = 0, end = 0; // seconds
        std::string sound;        // played with it (a .DDS in the archives), or ""
        // The sound's settings, as LoadComp gives them to its NiA3dSource:
        // the node it's placed at (none: the model's own), whether it
        // loops, is heard unplaced (A3D's native render mode) and is muted
        // beyond its maximum distance; a delay before it starts (0: at
        // once); volume, the distance model's scale and the distances.
        std::string soundNode;
        bool loop = false, unplaced = false, muteBeyondMax = false;
        float delay = 0;
        float volume = 1, falloff = 1, maxDistance = 0, minDistance = 0;
        // What the clip animates, the things with keys in its time: tracks
        // (by number), flip-books and morph shapes (each numbered in the
        // order the file holds them).
        std::vector<uint16_t> tracks, flips, morphs;
    };
    template <typename T>
    struct Key {
        float time = 0; // ticks
        T value{};
    };
    // Keys of types 2 and 3 curve to the next key; type 1 runs straight.
    // A position's curve from this key to the next, u running 0 to 1 over
    // it, is value + u (out + u (a + u b)).
    struct PositionKey {
        float time = 0; // ticks
        std::array<float, 3> value{}, out{}, a{}, b{};
        bool curve = false;
    };
    // Rotations curve by squad, through each key's control quaternions.
    struct RotationKey {
        float time = 0;
        std::array<float, 4> value{}, in{}, out{}; // quaternions: w, x, y, z
        bool curve = false;
    };
    // A number's curve is Hermite's, with the tangents into and out of each
    // key (already scaled to the intervals either side).
    struct FloatKey {
        float time = 0;
        float value = 0, in = 0, out = 0;
        bool curve = false;
    };
    struct Track {
        std::vector<PositionKey> positions;
        std::vector<RotationKey> rotations;
        // Instead of rotations: an angle (radians) about each of X, Y and Z.
        std::array<std::vector<FloatKey>, 3> euler;
        std::vector<FloatKey> scales;
        std::vector<Key<bool>> visible;
    };
    std::string id, name, textureSet;
    std::vector<Clip> clips;
    std::vector<Track> tracks;
    std::vector<std::array<float, 3>> footprint; // a polygon on the floor
};

// NiAlphaProperty. Blend factors as NetImmerse numbers them (6 source
// alpha, 7 one minus source alpha...).
struct FinAlpha {
    bool blend = false;
    uint32_t source = 6, destination = 7;
};

struct FinObjectInfo {
    std::string type;
    uint32_t id = 0;
    uint32_t offset = 0, size = 0; // the fields, after the class name and id
    bool read = false;             // a class the reader understands
    bool exact = false;            // its fields read to exactly its end
};

class FinFile {
public:
    Error parse(const std::vector<uint8_t>& data);

    const std::vector<FinObjectInfo>& objects() const { return objects_; }
    // Objects saved as top-level ones, in order: the model's roots.
    const std::vector<uint32_t>& roots() const { return roots_; }

    // By id.
    std::map<uint32_t, FinNode> nodes;
    std::map<uint32_t, FinShape> shapes;
    std::map<uint32_t, FinShape> coronas; // DDCorona: glows, with vertices but no triangles
    std::map<uint32_t, FinSkin> skins;
    std::map<uint32_t, FinMorph> morphs;
    std::map<uint32_t, FinLight> lights;
    std::map<uint32_t, FinFlip> flips;
    std::map<uint32_t, FinMaterialAnimator> materialAnimators;
    std::map<uint32_t, FinActor> actors;
    std::map<uint32_t, FinMaterial> materials;
    std::map<uint32_t, FinAlpha> alphas;
    std::map<uint32_t, std::vector<uint32_t>> textures;  // NiTextureProperty: its images' ids (frames)
    std::map<uint32_t, uint32_t> multiTextures;          // NiMultiTextureProperty: its image's id (a lightmap)
    std::map<uint32_t, std::string> images;              // NiImage: the file name
    std::map<uint32_t, uint32_t> vertexColorModes;       // NiVertexColorProperty
    std::map<uint32_t, std::array<uint8_t, 2>> zBuffers; // NiZBufferProperty: test, write

    // What kind of object an id is, or "" if none.
    std::string typeOf(uint32_t id) const;
    // The 3ds nodes in the order an actor's tracks are: depth first from
    // the roots, each node before its children, children in order.
    std::vector<uint32_t> animatedNodes() const;

private:
    std::vector<FinObjectInfo> objects_;
    std::vector<uint32_t> roots_;
    std::map<uint32_t, size_t> byId_;
};

} // namespace reluva::netimmerse
