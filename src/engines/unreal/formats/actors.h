// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// What a level's actors add to its look, read from their saved
// properties: movers (doors, lifts: brushes kept out of the level's BSP,
// drawn from their own polygons) and the sky zone (the room the sky is
// seen from). See docs/engines/unreal/games/ds9fallen/systems/actors.md.

#include "engines/unreal/formats/model.h"
#include "engines/unreal/formats/package_set.h"
#include "engines/unreal/formats/properties.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace reluva::unreal {

/** A brush polygon (a Polys object's), in the brush's own coordinates. */
struct Poly {
    Vec3f base{};
    Vec3f normal{};
    Vec3f textureU{};
    Vec3f textureV{};
    std::vector<Vec3f> vertices;
    uint32_t polyFlags = 0;
    // object references
    int32_t actor = 0;
    int32_t texture = 0;
    int32_t itemName = 0; // name index
    int32_t link = 0;
    int32_t brushPoly = 0;
    int16_t panU = 0;
    int16_t panV = 0;
};

/**
 * Reads a Polys object; `trailing`, if given, is set to the bytes left
 * after the last polygon (none expected).
 */
Error readPolys(const Package &package, int exportIndex, std::vector<Poly> &out, size_t *trailing = nullptr);

/** An export's properties (its state frame skipped). */
Error readActorProperties(const Package &package, int exportIndex, PropertyList &out);

/**
 * Whether an export's class is `className` or a subclass of it, following
 * the classes' super references through the packages that define them.
 */
bool isA(const PackageSet &packages, const Package &package, int exportIndex, std::string_view className);

/** A mover (a door, a lift): a brush placed and moved by its keyframes. */
struct Mover {
    std::string name;
    std::vector<Poly> polys;
    // Placement: a brush point v goes to
    // location + postScale * (rotation * (mainScale * (v - prePivot))).
    Vec3f location{};
    Vec3f prePivot{};
    Rotator rotation;
    Vec3f mainScale{1, 1, 1};
    Vec3f postScale{1, 1, 1};
    // Keyframes, relative to the base (the location and rotation less
    // the current key's); the mover is saved at keyNum.
    int numKeys = 1;
    int keyNum = 0;
    std::array<Vec3f, 8> keyPos{};
    std::array<Rotator, 8> keyRot{};
    float moveTime = 1; // seconds between keys
    /**
     * bDynamicLightMover: lit where it is by the lights reaching its leaf
     * each frame; otherwise it keeps the light it was built with.
     */
    bool dynamicLight = false;
    /** The base its keyframes are from: where it is, less key keyNum's offset. */
    ///@{
    Vec3f basePosition() const;
    Rotator baseRotation() const;
    ///@}
};

/** Every mover in a level package that reads; problems go in `errors`. */
std::vector<Mover> findMovers(const PackageSet &packages, const Package &package, std::vector<std::string> *errors);

/**
 * The sky zone's viewpoint: the first SkyZoneInfo (or subclass) actor's
 * location. nullopt when the level has none.
 */
std::optional<Vec3f> findSkyZone(const PackageSet &packages, const Package &package);

/**
 * What a level's scripts changed when it started (runLevelStart() in
 * vm_level.h), for the actors whose look it changed: by the actor's export
 * index in the level's package. -1: unchanged.
 */
struct ScriptedActor {
    bool removed = false; // destroyed
    int hidden = -1;      // bHidden now: 1 hidden, 0 shown
    int drawType = -1;
    int lightType = -1;
    int lightBrightness = -1;
};
/** What a level's scripts changed, by the actor's export index. */
using ScriptedActors = std::map<int, ScriptedActor>;

/**
 * An actor drawn as a mesh (DrawType DT_Mesh), with what it needs from
 * its properties and its classes' defaults (class_defaults.h).
 */
struct MeshActor {
    std::string name;
    ObjectRef mesh;
    Vec3f location{};
    Vec3f prePivot{};
    Rotator rotation;
    float drawScale = 1;
    std::string animSequence;
    float animFrame = 0; // 0-1 through the sequence
    ObjectRef skin;
    std::array<ObjectRef, 8> multiSkins{};
};

/** How many of a level's actors findMeshActors found, and left out. */
struct MeshActorStats {
    int meshes = 0;       // actors drawn as meshes
    int fromDefaults = 0; // of those, whose mesh came from their class's defaults
    int hidden = 0;       // drawn as meshes but hidden (bHidden)
    int unreadable = 0;   // actors whose properties didn't read
};

class ClassDefaults;
/**
 * The actors drawn as meshes, with a mesh that reads.
 * @param stats Set, if given, to how many there were and why some were left out.
 * @param scripted What the level's scripts changed, if given (actors they
 *        removed, hid or turned to another draw type are left out).
 */
std::vector<MeshActor> findMeshActors(const PackageSet &packages, const Package &package, ClassDefaults &defaults,
                                      MeshActorStats *stats = nullptr, const ScriptedActors *scripted = nullptr);

/** Engine.Actor's ERenderStyle: how an actor's sprite or mesh blends. */
///@{
constexpr uint8_t kStyleNormal = 1;
constexpr uint8_t kStyleMasked = 2;
constexpr uint8_t kStyleTranslucent = 3;
constexpr uint8_t kStyleModulated = 4;
///@}
/**
 * The Fallen's two more: alpha-blended by the actor's glow, and multiplying
 * what's behind (Direct3D's ZERO, SRCCOLOR).
 */
///@{
constexpr uint8_t kStyleBlended = 5;
constexpr uint8_t kStyleSubtractive = 6;
///@}

/**
 * An actor drawn as a sprite (DrawType DT_Sprite, DT_VerticalSprite or
 * DT_SpriteAnimOnce): its texture facing the camera, DrawScale world
 * units per texel, centred on where it stands.
 */
struct SpriteActor {
    std::string name;
    ObjectRef texture;
    Vec3f location{};
    Vec3f prePivot{};
    float drawScale = 1;
    float scaleGlow = 1;        // its brightness
    Vec3f colorFilter{1, 1, 1}; // ColorFilter: its tint (Render.dll's DrawActorSprite)
    uint8_t style = kStyleNormal;
    bool vertical = false; // turns about the vertical only (DT_VerticalSprite)
    bool unlit = false;    // drawn at full brightness, not lit where it stands
};

/** How many of a level's actors findSpriteActors found, and left out. */
struct SpriteActorStats {
    int sprites = 0;
    int editorOnly = 0; // icons only the editor shows (S_Light, S_Camera...)
    int hidden = 0;     // bHidden, or a DrawScale of 0
};

/**
 * The actors drawn as sprites. Those whose sprite is an editor icon are
 * left out: the game hides them, by bHidden or by their scripts when it
 * starts (which `scripted` covers only when they were run), and the icon
 * gives them away (actors.md).
 * `stats` and `scripted` as for findMeshActors.
 */
std::vector<SpriteActor> findSpriteActors(const PackageSet &packages, const Package &package, ClassDefaults &defaults,
                                          SpriteActorStats *stats = nullptr, const ScriptedActors *scripted = nullptr);

/** A rotator's axes in Unreal's space: forward, right, up. */
std::array<Vec3f, 3> rotationAxes(const Rotator &r);
/**
 * The rotation whose axes (X forward, Y right, Z up) those are, as near as
 * they're square to each other.
 */
Rotator rotatorFromAxes(const std::array<Vec3f, 3> &axes);
/**
 * A fraction t of the way from one rotation to another, the shorter way
 * round, turning evenly (a quaternion slerp, as The Fallen's engine eases
 * rotations: C3DInterpolateRotationX, used by Actor.InterpolateRotation
 * and the splines).
 */
Rotator slerpRotators(const Rotator &from, const Rotator &to, float t);

} // namespace reluva::unreal
