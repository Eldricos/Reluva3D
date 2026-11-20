// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The Fallen's own saved games: a level written out as a package (.dsa),
// as the engine saves it. What's read here is what a map doesn't have:
// the Level object's ghosts, dialogue table counts and the sounds playing,
// and the C3D mesh states (each actor's animation groups and face
// channels). See docs/formats/unreal/save.md.

#include "common/types.h"
#include "engines/unreal/formats/package.h"

#include <array>
#include <string>
#include <vector>

namespace reluva::unreal {

/**
 * The Level object past its actor list: its URL, the ghosts (actors out of
 * the level for now), the dialogue table as said so far (each line's key
 * and how often it's been said) and the sounds playing when it was saved.
 */
struct LevelExtras {
    std::vector<int32_t> actors; // object references (0: none)
    std::string urlMap;
    std::string urlPortal;
    std::vector<int32_t> ghosts;
    struct Line {
        std::string key; // TYPE SPEAKER LISTENER ID MISSION
        int said = 0;
    };
    std::vector<Line> lines;
    struct Sound {
        int32_t actor = 0; // object reference
        int32_t id = 0;    // the audio driver's: actor x 16 + slot x 2 + no-override; below 0 slot none
        std::string path;  // Package.Group.Name
        /** start: in the level's seconds */
        float volume = 1;
        float radius = 0;
        float pitch = 1;
        float start = 0;
    };
    std::vector<Sound> sounds;
    /**
     * The dialogue's lines under way: being said, or waiting for their
     * speaker to be free (`queued`). `line` and `back` are places in the
     * saved lines (`back`, the RETURNLINE its thread goes back to, 0xFFFF
     * none); `end` when it's done, in level seconds (a HUD line's on the
     * clock that runs while paused); `speaker` an object reference.
     */
    struct Talk {
        int32_t thread = 0;
        int line = 0;
        int back = 0xFFFF;
        float end = 0;
        int32_t speaker = 0;
        bool queued = false;
    };
    std::vector<Talk> talks;
    int32_t nextThread = 0; // the dialogue engine's next thread number
    int cameras = 0;
};
Error readLevelExtras(const Package &package, LevelExtras &out);

/**
 * The engine's probe event at a bit of a state frame's mask (Core's names
 * from 300 on, The Fallen's in the place of some of the probes), "" past 63.
 */
const char *probeEvent(int bit);

/**
 * A C3DMESHSTATE: an actor's animation groups (its mesh's own, FullBody,
 * LowerTorso, UpperTorso, then its face channels sk3...) as the engine
 * keeps them.
 */
struct MeshStateGroup {
    std::string group;
    std::string sequence;
    int32_t source = 0;
    float frame = 0;  // seconds into the sequence (a face channel's), or the body's 0-1
    int32_t type = 0; // a body group's Boned (1 true, 2 false); 4 a face channel
    /**
     * The group's mode (Engine.dll's): 0 held still, 1 playing once, 2
     * fading in to play once, 3 looping, 4 fading in to loop, 5 tweening
     * (C3DTweenAnim), 6 fading out. A face channel's: 0 held, 5 tweening.
     */
    int32_t state = 0;
    float rate = 1;
    float weight = 1;
    float fromFrame = 0;
    float fromWeight = 0;
    float elapsed = 0;
    float duration = 1;
    std::string pendingSequence;
    float pendingFrame = 0;
    float pendingWeight = 0;
    float pendingTween = 0;
    std::string bone;               // its InheritBone: what its clears turn
    std::array<int32_t, 3> clear{}; // ClearYaw, ClearPitch, ClearRoll (fairly sure)
};
/**
 * An actor held by the mesh state's own (C3DAttach): its name, its point
 * and the holder's point.
 */
struct MeshStateAttachment {
    std::string child;
    std::string childPoint;
    std::string parentPoint;
};
struct MeshState {
    std::string mesh;
    std::vector<MeshStateGroup> groups;
    std::vector<MeshStateAttachment> attachments;
};
Error readMeshState(const Package &package, int exportIndex, MeshState &out);

/**
 * Where a C3D mesh state's animation groups are in its export's data
 * (from the start of its count to the end of the last).
 */
bool meshStateGroupSpan(const std::vector<uint8_t> &data, size_t &begin, size_t &end);
/**
 * Where a C3D mesh state's attachments are in its export's data (from the
 * start of their count to the end of the last).
 */
bool meshStateAttachmentSpan(const std::vector<uint8_t> &data, size_t &begin, size_t &end);
/**
 * Where, in a Level object's data, the model reference starts and the
 * reach list ends (copied as they are into a saved game's).
 */
bool levelModelSpan(const std::vector<uint8_t> &data, int version, size_t &begin, size_t &end);

/** Whether a file is a package (a saved game of the original's, not Reluva's own). */
bool isPackageFile(const std::string &path);

} // namespace reluva::unreal
