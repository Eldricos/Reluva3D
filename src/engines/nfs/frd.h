// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/nfs/col.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::nfs {

// A track's geometry (.frd), as docs/formats/nfs/frd.md describes.
// Coordinates are as stored: +y up, left-handed like the models (fce.md).
struct Track {
    using Vec3 = std::array<float, 3>;

    // A quad of four vertices (a triangle repeats one), textured by an
    // entry of the track's texture table.
    struct Polygon {
        std::array<uint16_t, 4> vertices{};
        uint16_t texture = 0; // into `textures`
        // Animated (kAnimated): the first frame's texture entry, and the
        // delay in ticks (top 5 bits) and number of frames (low 3).
        uint16_t animationBase = 0;
        uint8_t flags = 0; // kTwoSided, kAnimated (frd.md, Polygons)
        uint8_t animation = 0;
        static constexpr uint8_t kAnimated = 0x04, kTwoSided = 0x10;
        int frameDelay() const { return animation >> 3; }
        int frameCount() const { return animation & 7; }
        // The texture entry shown at a game tick (64 a second; 0x41b52b).
        uint16_t textureAt(uint64_t tick) const;
    };

    // Polygons grouped as an object within a block (`type` 1); type 4
    // entries hold none.
    struct BlockObject {
        uint32_t type = 0;
        std::vector<Polygon> polygons;
    };

    // The part of the track in one piece of the road, and what stands
    // beside it.
    struct Block {
        Vec3 centre{};
        std::array<Vec3, 4> corners{}; // its outline on the ground
        // As stored: all vertices, then the counts of their groups. The
        // vertices are the objects' (objectVertices), then the road's
        // (highResVertices), then more (lane markings').
        uint32_t highResVertices = 0, lowResVertices = 0, mediumResVertices = 0, duplicateVertices = 0,
                 objectVertices = 0;
        std::vector<Vec3> vertices;
        std::vector<uint32_t> colours; // per vertex, 0xAARRGGBB: baked lighting
        std::vector<int16_t> neighbours; // blocks drawn with this one
        // Seven polygon lists: 0, 2 and 4 the road at low, medium and high
        // detail; 1, 3 and 5 roadside polygons at the same levels (on a
        // few tracks); 6 the lane markings.
        std::array<std::vector<Polygon>, 7> polygons;
        std::array<std::vector<BlockObject>, 4> objectLayers;
        // The records after the polygons' counts, as stored (frd.md); the
        // lights and sounds are also decoded below.
        uint32_t startPosition = 0;
        std::vector<uint8_t> positions, polygonRoad, virtualRoad, objectRefs, polygonObjects, sounds, lights;
        // A light: where (16.16 fixed point in the file) and its type,
        // which picks how it looks (lightLook).
        struct Light {
            Vec3 position{};
            uint32_t type = 0;
        };
        // A sound placed on the track: where, the slot of the track's
        // tr3dNN.bnk it plays, and its kind (0 a loop, 1 a one-shot fired
        // at random; frd.md).
        struct PlacedSound {
            Vec3 position{};
            uint16_t slot = 0, kind = 0;
        };
        std::vector<Light> lightList;
        std::vector<PlacedSound> soundList;
    };

    // An object placed anywhere on the track: still (type 4, at
    // `reference`) or moving (type 3, by its keys).
    struct ExtraObject {
        struct Key {
            std::array<int32_t, 3> position{}; // 16.16 fixed point
            std::array<int16_t, 4> rotation{}; // 2.14 fixed point
        };
        uint32_t type = 0;
        Vec3 reference{};
        std::vector<Key> keys; // world positions; the vertices are relative to them
        int16_t keyDelay = 0;  // game ticks (64 a second) per key
        std::vector<Vec3> vertices; // relative to the reference (still) or the key (moving)
        std::vector<uint32_t> colours;
        std::vector<Polygon> polygons;
    };

    // A texture table entry: a picture of the track's .qfs sheet (or of
    // the lane markings') and where each corner of a polygon falls on it.
    struct Texture {
        int width = 0, height = 0;
        std::array<float, 8> corners{}; // u, v of polygon corners 0-3
        bool lane = false;
        uint16_t picture = 0; // the shape's number in the sheet
        uint32_t handle = 0; // room the game fills at load; leftovers
        uint32_t flags = 0;  // low byte: kSeeThrough, 0x04 (frd.md, Textures)
        static constexpr uint32_t kSeeThrough = 0x02;
    };

    std::vector<Block> blocks;
    std::vector<std::vector<ExtraObject>> extraObjects; // 4 groups per block, and one more
    std::vector<Texture> textures;
};

Error parseTrack(const std::vector<uint8_t>& data, Track& out);

// Which of a polygon's four vertices the game draws as corner k (0-3):
// the corner that takes Texture::corners[k] and the k-th in its winding.
// A block's lists keep the file's order (0x41b300); the placed objects'
// loop (0x41b640) queues vertices 1, 0, 3, 2, and the renderer (0x433840)
// and the back-face test go by the queue. So a placed object's picture is
// flipped left to right and its winding reversed against the file's
// (frd.md, Polygons).
inline int polygonSlot(int k, bool placedObject) { return placedObject ? k ^ 1 : k; }

// How a light of a type looks at a game tick (64 a second), from
// nfs3.exe's table at 0x55e3dc (frd.md, Lights): drawn with glw0 in its
// colour (ARGB), size units from its centre to its edges, unless it's
// blinking off.
struct LightLook {
    uint32_t color = 0;
    float size = 0;
    bool shown = true;
};
LightLook lightLook(uint32_t type, uint64_t tick);
// A light's ARGB colour dimmed by the effects intensity (the Direct3D
// renderer's SFX_INTENSITY option, 0-1; 1 leaves it as it is), each byte
// alpha included, as 0x491223 does.
uint32_t dimLight(uint32_t color, float intensity);
// The two quads 0x491190 draws for a light (frd.md, Lights), both turned
// as the cars' glows are, and only while the light's centre is on the
// screen at a depth z from 1 to 256: glw0 at a fixed size on the screen,
// screenHalf of the projection's x scale (320 in a 640-wide race) from
// its centre, 0.025 x size, in the colour times (256 - z) / 256 (each
// byte, alpha too); and glw3 (0x4918ca) in perspective, worldHalf units,
// 1.05 x 0.25 x size, in the full colour.
struct LightQuads {
    bool shown = false;
    uint32_t glowColour = 0; // glw0
    float screenHalf = 0;
    uint32_t flareColour = 0; // glw3
    float worldHalf = 0;
};
LightQuads lightQuads(const LightLook& look, float depth);

// Which road detail the game draws a block at (0 low, 1 medium, 2 high),
// or -1 for not at all, by the distance on the ground from the camera to
// the block's centre (0x41cdc0, 0x41aa60). The distances are set by
// 0x41df10 and 0x41d620 for the main view of a one-player race: 500 to
// draw, 150 for medium and 110 for high, times 1, 0.55 or 0.4 by day and
// 0.75, 0.41 or 0.3 by night for view levels 0-1, 2 and 3 (0x6fbc28; the
// highest, 1, is what a fresh config.dat gets on a fast machine).
int blockDetailAt(float distance, bool night, int viewLevel = 1);

// The game drops a polygon whose first queued corner (polygonSlot(0))
// is more than the view's +0x30 ahead along the view (0x41b3bf, and the
// same in the placed objects' loop and the headlight pass). That's the
// view's first distance (0x5dd104), 500 for the main view of a one-player
// race (0x41df10; 400 split screen), which 0x41d620 doesn't scale: by
// night and at every view level it stays 500, while the blocks' reach
// (+0x34) shrinks.
constexpr float kPolygonReach = 500;
inline bool polygonInReach(float depthAhead) { return depthAhead <= kPolygonReach; }

// A car's headlights (frd.md, the headlights), as nfs3.exe sets them up
// (0x42d160) and projects them (0x41a550, 0x41a3e0): a light frame whose
// forward is the car's tilted down, and the picture glw2 of sfx.fsh
// projected through a 90-degree square frustum along it, coloured by how
// far off the axis and how far along it a point is. Positions and vectors
// in the track's coordinates.
struct Headlight {
    std::array<float, 3> at{}, right{}, up{}, forward{};
    float nearPlane = 1, fadeFrom = 10, reach = 40; // fading from fadeFrom to nothing at reach
    uint32_t colour = 0;                             // ARGB, alpha too
    // The colour behind it (the light record's +0x74): 0 for a car's
    // headlights, which light nothing behind; a police car's roof light
    // shines both ways (sirenLight).
    uint32_t backColour = 0;
};
// The intensity the pass gives a car's light (0x41c670, 0x41a800): 1 less
// three quarters of the share of the race's cars (all of them, 0x5efd9c)
// it has already lit for this block, so the first car lit gets 1 and each
// further one less, never below a quarter.
inline float headlightIntensity(int litBefore, int carCount) {
    return carCount > 0 ? 1.0f - static_cast<float>(litBefore) / static_cast<float>(carCount) * 0.75f : 1.0f;
}
// beams: the car's light state (0 off, 1 low, 2 high; the headlight key
// steps through them, 0x40de50). projected: the projtex option is on with
// the z-buffer (0x6fbc3c, 0x7a3a60). Low beams fade from 10 to 40 in
// 0xB0B0B0B0, tilted down 1/16 of a turn; high from 20 to 60 in
// 0xE0E0E0E0, 1/32 of a turn; without projection 20 or 40 to 40 or 60, in
// white. The car's heading in radians (atan2(x, z) of its forward) and
// intensity (1 for the first car the pass lights, 0x41a800).
Headlight carHeadlight(int beams, bool projected, const std::array<float, 3>& at, float heading,
                       float intensity = 1);
// The same for a car whose rows (right, up, forward; track coordinates)
// are given, on a sloping or banked road.
Headlight carHeadlight(int beams, bool projected, const std::array<float, 3>& at, const std::array<float, 3>& right,
                       const std::array<float, 3>& up, const std::array<float, 3>& forward, float intensity = 1);
// The light state a car starts a race with (0x42bcb0): off, but low beams
// in the weather (0x6fd4cc), and by night (0x6fd4c8) high beams for a
// human's car (+0x200 bit 4) and low beams for the others (computer
// racers, traffic, the police). The computer flashes its lights a step up
// for 64 steps when it is held up (0x40de50, undone by 0x40deb0).
inline int startingBeams(bool night, bool weather, bool human) {
    return night ? (human ? 2 : 1) : weather ? 1 : 0;
}
// A police car's roof light with its siren on (+0x734, set by 0x42d020
// each frame): a unit above the car along its up row, its frame the
// car's turned about the up by (step mod 32) / 32 of a turn, so it sweeps
// round twice a second; from 3.5 it fades from 15 to nothing at 30, in
// 0xFF8080FF (blue) ahead and 0xFFFF8080 (red) behind (+0x7A4, +0x7A8).
// Its pass (0x41ab40) only raises vertex colours, of every polygon of the
// lists the headlights take but the lane markings, whatever its texture.
Headlight sirenLight(const std::array<float, 3>& carAt, const std::array<float, 3>& right,
                     const std::array<float, 3>& up, const std::array<float, 3>& forward, int step);
// The screen the game projects with (0x56009c-0x5600a8 and the clip
// window at 0x7d34f8-0x7d350c; as an nfs-host race at 640 x 480 has them):
// x = cx + sx x / z, y = cy + sy y / z, inside left-right and top-bottom,
// and 1 / z below inverseDepth (z beyond 1).
struct ViewScreen {
    float sx = 320, sy = -330.332f, cx = 320, cy = 240;
    float left = 0, right = 640, top = 0, bottom = 480, inverseDepth = 1;
};
// Whether the game leaves a block out of a light's pass (0x41bf30; for a
// roof light 0x41bc90): its outline's four corners (Track::Block::corners)
// turned into the light's frame, each wider than its depth (|x| times
// `zoom`, the camera's +0x54 held to 1 at most, beyond z; for a roof
// light, beyond |z|) and all outside one side of the screen's window
// when projected with the light as the camera (or all behind it).
// The screen the game sets up for a view (0x4dbce0 -> 0x4bf260): from
// the view angle in degrees (viewangle, 0x6fbc40: 90, or 75 when it is 1;
// 0x4dbcc0) the half angle across a = angle / 2 and the half angle up
// a x 13 / 16, both whole degrees (45 and 36; 37 and 30), and
// sx = width / 2 / tan a, sy = -(height / 2 / tan(a x 13 / 16)) (in the
// x87's precision, degrees x 0.0174532793), cx, cy the window's middle;
// sx negated on the mirrored track. So at 640 x 480 and 90 degrees the
// view is 90 degrees across and 72 up, its pixels 330.33 / 320 tall.
ViewScreen viewScreen(int left, int top, int width, int height, int viewAngle = 90, bool mirrored = false);
bool lightMissesBlock(const Headlight& light, const std::array<std::array<float, 3>, 4>& corners,
                      float zoom = 1, bool bothWays = false, const ViewScreen& screen = {});
// Where nfs3.exe puts a car's light (0x42d160): the car's position
// (+0x98) plus its up row (+0xCC) times +0x10C (its up . the road's
// normal, 1 on level road), plus a quarter of its half length (+0x11C)
// along +0x67C. That is the forward row of the light's own frame
// (+0x664), the car's rows turned down by the tilt, which the same
// function rewrites only after taking the place: the frame of the step
// before, the light's forward when the car turns slowly. Here the car
// is upright: its up the track's.
std::array<float, 3> headlightPlace(const Headlight& light, const std::array<float, 3>& carAt, float halfLength,
                                    float upright = 1);
// The same with the car's up row (track coordinates).
std::array<float, 3> headlightPlace(const Headlight& light, const std::array<float, 3>& carAt,
                                    const std::array<float, 3>& carUp, float halfLength, float upright = 1);
// A point in the light: its place on the picture, its colour (0 outside
// the reach) and the game's outcodes: 1 left, 2 right, 4 below, 8 above
// the frustum, 0x10 before the near plane or behind, 0x20 past the reach.
// A light with a back colour (a roof light) takes a point behind it as
// one ahead with x, y and z negated, in that colour (0x41a550).
struct HeadlightPoint {
    float u = 0, v = 0;
    uint32_t colour = 0;
    uint8_t outside = 0;
};
HeadlightPoint headlightAt(const Headlight& light, const std::array<float, 3>& point);
// Whether the game draws a triangle of the projected picture (0x41a360,
// 0x41a120): not all its corners outside the same side, none past the
// reach, and one at least with alpha 8 or more.
bool headlightTriangle(const HeadlightPoint& a, const HeadlightPoint& b, const HeadlightPoint& c);
// Each byte of an ARGB colour times k / 256 (k 0-255), as the game scales
// light colours (0x41a3e0, 0x491223).
uint32_t scaleArgb(uint32_t colour, uint32_t k);

// Another car's lights on a car (0x4b97b0, by night, before it is
// drawn): for each car in play but this one whose lights are on, this
// car's position in each light's frame. A headlight within its near
// plane and reach, no wider or higher than deep, adds its colour (+0x6D0,
// unscaled) times (1 - |x| / z) (1 - z / reach); a police car's roof
// light (z taken as |z|, its back colour behind) times 1 - z / reach,
// and then that car's headlights may add theirs too (only then). The
// sum of each channel over the lights that add is divided by their
// number (truncated) and added to the car's colour, at most 255.
struct CarLights {
    Headlight headlight;
    bool headlightOn = false;
    Headlight siren;
    bool sirenOn = false;
};
uint32_t carLitBy(uint32_t colour, const std::array<float, 3>& carAt, const std::vector<CarLights>& others);

// The computer's speed table (speedsF.bin, or speedsR.bin for the track
// run backward; physics.md): after a 16-byte header, one byte per two
// road slices, the speed wanted in m/s (0x40e080). 64 without the file.
float aiSlicesSpeed(const std::vector<uint8_t>& speeds, int slice);

// A placed sound's volume (0-127) at a distance in track units, as
// 0x41d210 has it: 2^16 over (distance in 1/16 units)^2 / 2^10, at most 127.
int placedSoundVolume(float distance);

// When the game fires a one-shot placed sound (kind 1; 0x41d27d): on a
// frame whose (count & 63) >> 3 is the number picked, 0 to 7, which is
// then picked afresh. One pick shared by all, from rand().
struct OneShotTimer {
    int pick = 0;
    template <typename Random>
    bool fires(uint32_t frame, Random& random) {
        if (static_cast<int>((frame & 63) >> 3) != pick)
            return false;
        pick = random.next() & 7;
        return true;
    }
};

// Whether a point is under cover (0x420bf0): the road point's side code
// (col.md) on the point's side of it is 2; the right side's is the low
// nibble, unless that is 0, then the left's.
bool pointCovered(const TrackCollision::RoadPoint& p, const std::array<float, 3>& at);
// How far from a covered car's slice the cover ends (0x4ba350): the first
// n from 1 at which slice + n or slice - n is not covered (at the car's
// place), 8 when both still are at 7; 0 when the car isn't covered.
int coverDepth(const TrackCollision& col, int slice, const std::array<float, 3>& at);
// A road polygon's surface (the block's 8-byte record for it, byte 1
// & 0x3F, as 0x4207e0 reads it), list 4's polygon `index`; 0 without one.
int roadSurface(const Track::Block& b, size_t index);
// Whether the rain's flares (0x491bc0) are drawn on a surface: in the
// weather, the table at 0x55308c (by surface and the weather's kind, rain
// or snow, which agree for the 16 surfaces) says 10: surfaces 1, 4, 6, 7,
// 10, 11 and 12.
bool wetSurface(int surface);
// The road's frame on list 4's polygon `index` as a track position on it
// has it (trackpos_road_frame 0x420230 with +0x64 = 2, which 0x41e990 sets
// on finding the polygon): up and forward the block's virtual road vector
// (12 bytes, six s16 / 32768) that the polygon's 8-byte record's byte 0
// picks, right up x forward, and the point the polygon's vertex 1
// (+0x24, from 0x41e6a0's +0x54). False without the records.
struct RoadFrame {
    std::array<float, 3> right{1, 0, 0}, up{0, 1, 0}, forward{0, 0, 1}, point{};
};
bool polygonRoadFrame(const Track::Block& b, size_t index, RoadFrame& out);
// The height 0x4968a0 (and its fixed-point twin 0x4969c0) gives a point:
// the point taken into the frame, its part along the up dropped, and back:
// point.y + ((p - point) . right) right.y + ((p - point) . forward) forward.y.
float roadFrameHeight(const RoadFrame& f, const std::array<float, 3>& p);

// The computer's driving (physics.md: the drivers, the speed wanted, the
// lane votes), as nfs3.exe's AI racer does it each step (64 a second,
// car_driver_ai 0x409c00 and car_ai_model 0x408600), for cars kept on
// the virtual road. Lanes are 0-13, 7 and up right of the road's point
// along its right vector, whichever way the car runs.
struct AiCarData {
    std::array<float, 112> accel{}; // AI ACC0-7 (P +0x2EC), m/s/s at each m/s
    float maxBrake = 10;            // maximum braking deceleration (P +0x1D8)
    float topSpeedCap = 70;         // P +0x1CC
    float maxVelocity = 70;         // P +0x1C8, the cops' top speed (0x401370)
};
// The fields of a car's carp.txt the computer's driving reads; false when
// one is missing.
bool parseCarpAi(const std::string& text, AiCarData& out);

// The drivers' table (0x55226c): skill 0-2, whether they surge (+0xC 0)
// and the personality 0-5.
struct AiDriver {
    int skill = 1;
    bool surges = false;
    int personality = 2;
};
// The driver's factor on the speed wanted (0x408dc0): skill 0 x 0.94, 1
// x 0.97; surging x 1.05 when (step >> 7) & 3 is 1, x 0.95 when 3;
// personality 0 x 0.95, 1 x 0.97.
float aiDriverFactor(const AiDriver& d, int step);
// The single race's per-track factor with ai Normal (0x40e410, 0x40ee80):
// 0.82 on tracks 0, 3, 5 and 7, 0.84 on the others.
float aiTrackFactor(int track);

struct AiCar {
    int slice = 0;      // the road point behind it (+0x1C)
    int way = 1;        // +1 running the slices' way, -1 against (+0x620 bit 0x1000)
    float offset = 0;   // the aim now (+0x7C8)
    // Its side offset as driven (+0x7B8), which its lanes are taken at
    // (0x40e350): the car steered towards the aim (aiMove), lagging and
    // overshooting it a little as the game's controllers do; with
    // `sideKept` false taken as the aim.
    // `side` is +0x7B8 as the game measures it (0x4a2bc0: the car's place
    // less its slice's road point, along the right), `lateral` the car's
    // own place across the road from the line between the slice's point
    // and the next, `along` how far it is from the one to the other (0-1,
    // set by whoever moves it): on a bend the measured offset changes
    // across a slice while the car goes straight on, and the controller
    // steers by it.
    float side = 0, sideSpeed = 0, lateral = 0, along = 0;
    bool sideKept = false;
    float target = 0;   // the offset wanted (+0x7B4)
    float speed = 0;    // m/s along its way (+0x7C4)
    float wanted = 0;   // the speed wanted (+0x7B0), as a size
    float scale = 1;    // the speed scale (+0x7BC): aiDriverFactor x aiTrackFactor, at most 1
    float halfWidth = 1.183f;
    AiDriver driver;
    bool cop = false;   // +0x200 bit 0x20: weighs more in the votes of the cars behind
    int lane = 7, laneLeft = 7, laneRight = 7; // +0x638, +0x63C, +0x640
    int beams = 0;      // +0x660
    bool flashing = false; // +0x620 bit 8, until flashUntil (+0x644)
    int flashUntil = 0;
    bool braking = false; // +0x509 not 0: the brake held (ai_brake's 255)
    int pick = 7;       // the lane chosen (0x56ef74)
    // A computer cop's driver (car_driver_cop 0x4032e0, physics.md), for
    // a cop: its state +0x70C (1 patrol, 2 driving to its order, 3 to the
    // roadside, 4 stopped there, 5 the chase, 6 the stop, 7 a run with the
    // siren on, 8-11 to a roadblock and across the road), its target (+0x624, `chasing`, an index into the cars, -1 none),
    // its phase (car +0, the gate 0x401240 letting it act when (step / 2
    // + phase) mod 4 is 0) and +0x711 bit 1 (x 1.1 on its top speed).
    int copState = 0;
    int chasing = -1;
    int phase = 0;
    bool copFast = false;
    bool siren = false; // +0x710 bit 0x10 (+0x734)
    bool racer = true;  // in the race's racers (0x5efd94's list): not a cop or traffic
    // A cop's orders (+0x7AC, cop_ai_order 0x403830): the kind (0 to the
    // roadside, 1 drive to, 2 patrol, 3 a run with the siren, 4 a
    // roadblock; -1 none), taken once (+1), the slice range (from, length:
    // 0x40f000) and the way to face for a patrol (+6: 1 against the
    // slices); the racer it watches (+0x730).
    int orderKind = -1;
    bool orderTaken = false;
    int orderFrom = 0, orderLength = 0;
    bool orderAgainst = false;
    int watching = -1;
    bool stripWanted = false; // +0x711 bit 8: lays a strip at the roadside
    bool blockWatch = false;  // +0x711 bit 4: a roadblock, watching its racer by
    bool across = false;      // turned across the road (0x403180, state 11)
    bool headed = false;      // +0x710 bit 0x20 (state 2)
    int index = 0;            // +0x714, the cop's own index: its strip's lane
    // Its strip (0x7dd850 + index x 0x50, spike_lay 0x4c9dd0): out, the
    // slice and lane, the racer near radioed (+0x711 bit 0x20).
    bool stripOn = false, stripNear = false;
    int stripSlice = 0, stripLane = 7;
    bool reversing = false;   // +0x7D4 (ai_reverse_set)
    bool holding = false;     // +0x710 bit 0x40: the stop test held since
    int holdUntil = 0;        // +0x720
    int waitFrom = 0, waitNear = 0; // +0x72C, +0x728: waiting for the watched racer
    // A racer's chase record (+0x628: +0x1C 2 pulled over, +0x28 the step
    // the stop or the ticket ends, +0x50 the cops stopping it), its state
    // (+0x6E0: 1 racing, 5 pulling over, 6 stopped), +0x6EC, its tickets
    // (+0x2C8) and the step it is free of chases to (+0x634).
    bool pulledOver = false;
    bool spiked = false; // +0x621 bit 0x40: a strip caught it (spike_hit_racer), until the ticket
    int recordStep = 0, stopping = 0;
    int pursuit = 1, pullUntil = 0;
    int tickets = 0, chaseFree = 0;
    // The rest of the chase record the cops' calls read (cop_set_target
    // 0x4018b0 opens it, +0x1C 1): +0x14 the chases lost (cop_lose_target
    // 0x401a80), +0x20 the first chase's step, +0x24 the step of the last
    // call for help (every 320 steps from 640 after the first chase),
    // +0x54 what has been called (1 backup, 2 a strip asked for, 4 a
    // roadblock), +0x58 the cop sent to lay a strip, +0x5C the cop that
    // asked last (indices into the cars, -1 none).
    bool recordOpen = false;
    int chasesLost = 0, chaseFrom = 0, callAt = 0, calls = 0, stripCop = -1, caller = -1;
    // +0x6F0: the most it has been over the track section's speed while a
    // computer cop looked at it (racer_overspeed_update 0x4014b0: for each
    // racer within 16 slices of a free cop as it looks for a target, and
    // for its target each call while chasing), 0 after a ticket. Over 2
    // m/s it makes the racer a target (0x403f80) and the strip's cop take
    // it (0x401650).
    float overspeed = 0;
    // Its race distance less the leading human's (its +8 less 0x56ebb8's
    // +8), set by whoever runs the race: the stop's hold (0x402110).
    int raceDistance = 0;
    // Its wheels where they meet the road (x right, z forward of its
    // place; the model's parts 1-4): a strip catches them (0x4ca160).
    std::array<std::array<float, 2>, 4> wheels{{{-0.8f, 1.3f}, {0.8f, 1.3f}, {-0.8f, -1.3f}, {0.8f, -1.3f}}};
    // The last radio call, by its routine's address (aiRadioCall), and the
    // step it was made at; the game queues the lines as speech (physics.md,
    // The radio), the stand-in only notes it.
    uint32_t radio = 0;
    int radioStep = -1;
};
// What a radio call (its routine's address, physics.md's table) says, in
// a few words; empty for an unknown one.
const char* aiRadioCall(uint32_t call);
// Where a strip lies (spike_lay 0x4c9dd0 with 0x4c9bc0): across the
// lane's middle (aiLaneOffset's, its half's lane width from bytes 0x17 or
// 0x1B), 1.5 lane widths long: offsets `from` to `to` along the road's
// right at the slice's point.
void aiStripSpan(const TrackCollision::RoadPoint& p, int lane, float& from, float& to);
// The strip's quad (spike_lay 0x4c9dd0): its two ends on the road at the
// slice's point (aiStripSpan's), and the same two 0.5 higher (0x540360).
std::array<std::array<float, 3>, 4> aiStripQuad(const TrackCollision::RoadPoint& p, int lane);
// Which wheels are on a strip (spike_hit_test 0x4ca160), a bit each: the
// quad's corners taken into the car's frame from each wheel (`right`,
// `up`, `forward` its rows, the wheels as AiCar::wheels from `at` on the
// road), each given an out code against a box of ±0.5 round the wheel
// (0x540388, 0x540390); a wheel whose four codes share no side is on it.
unsigned aiStripWheels(const std::array<std::array<float, 3>, 4>& quad, const std::array<float, 3>& at,
                       const std::array<float, 3>& right, const std::array<float, 3>& up,
                       const std::array<float, 3>& forward, const std::array<std::array<float, 2>, 4>& wheels);
// How long a cop holds a slow target before it is pulled over
// (cop_ai_stop_check 0x402110, 0x40237a): 192 steps for a human; for a
// computer racer 160 when it is more than 60 slices ahead of the leading
// human, 224 when more than 60 behind, else 192.
int aiStopHold(bool humanTarget, int raceDistance);
// The orders the page's dispatcher key gives the police car, as the
// chasing cops' calls give a free cop (physics.md): a strip (0x4039c0: an
// order of kind 0 from its slice - 6, 3 slices, +0x711 bit 8), a roadblock
// (0x403b20: kind 4 at a slice, the siren on, +0x711 bit 4) or a patrol
// (kind 2), each watching a racer.
void aiOrderStrip(AiCar& cop, int racer, int sliceCount);
void aiOrderRoadblock(AiCar& cop, int racer, int slice);
void aiOrderPatrol(AiCar& cop);
// The lane at a side offset (0x40e2b0): 7 + the offset over the right
// lanes' width rounded to the nearest (6 + over the left's, for one on
// the left), 0-13.
int aiLaneAt(const TrackCollision::RoadPoint& p, float offset);
// Whether a lane has road at a point (0x40e200, 0x40e270): bit 14 - lane
// of the point's strip mask.
bool aiLaneOpen(const TrackCollision::RoadPoint& p, int lane);
// The side offset the computer aims at for a lane (0x40daf0): a lane's
// middle, (L - 7 + 1/2) widths right or (6 - L + 1/2) left; for the lane
// just past the road's lanes, a half width of the car past the road's
// edge; within the walls less the half width; then moved `nudge` half
// widths right (0x56ef78, from the lanes' strips).
float aiLaneOffset(const TrackCollision::RoadPoint& p, int lane, float halfWidth, int nudge = 0);
// The speed table's best line lane for a slice (0x40e0c0): the high
// nibble of the byte after the speeds; 7 without the table.
int aiLineLane(const std::vector<uint8_t>& speeds, int slice, int sliceCount);
// One step (of 64 a second) of the computer's driving for cars[self], as
// an AI racer (+0x200 bit 8) past the race's start. Its driver
// (car_driver_ai 0x409c00) acts only when aiDriverActs lets it, every 8
// steps: the lanes it is on (at its side offset as measured), the speed
// wanted (the speed table for its slice x its scale, held under the top
// speed cap x the track factor), flashing its lights when held up behind
// a car in its lane (0x404840), keeping behind it (0x4094d0), the lane
// votes and the lane chosen (lane_votes_start, ai_lane_vote_line,
// lane_votes_ahead with the strips' 0x40d720, lane_choose); pulled over
// (its record's +0x1C = 2) it pulls over (5) and stops (6) until the
// ticket. The model (car_ai_physics, 32 Hz: the even steps) moves its aim
// 0.2 towards the offset wanted (0x408870) and its speed over 1/32 s
// towards the speed wanted by the acceleration table or the brakes
// (0x4064f0, 0x4063e0), the brake held only when more than 5 m/s too
// fast; its side offset follows the aim every step. speeds: the table
// for its way.
void aiStep(std::vector<AiCar>& cars, size_t self, const TrackCollision& col, const std::vector<uint8_t>& speeds,
            const AiCarData& data, int step, int track, bool weather);
// Whether a computer car's driver acts on a step (0x40df10 -> 0x401240,
// for racers and cops alike): the drivers are a 32 Hz task, on the even
// steps, let through when (step / 2 + its phase, car +0) mod 4 is 0.
bool aiDriverActs(const AiCar& car, int step);
// How far a computer car sits inside a bend as it steers at the point
// ahead (0x406e50 with 0x406d40, 0x406980: about 1.7 s on, 4-20 slices,
// short of a 45-degree turn) rather than along the road: a share of the
// point's offset across the car's slice, along the slice's right.
float aiBendCut(const TrackCollision& col, const AiCar& car);

// The track section's speed (0x40ed40): by track and slice, hard-coded
// ranges, 8.94 to 31.29 m/s.
float trackSectionSpeed(int track, int slice);

// The catch-up factor on a racer's speed scale (0x4090a0), by the human
// furthest on (0x56ebb8) and how many computer racers are ahead of him
// (0x404c80, 0x56ebb4: 0 none, 1 some, 2 all). Race distances in slices.
// raceType 0x6fd3b8 (1, 2 Tournament and Knockout; 3 Hot Pursuit),
// aggressive the ai option (0x6fd4d8 1), expert the game level, network
// 0x6fd3ac's link kind.
struct AiRace {
    bool human = true;     // a human in the race (0x56ebb8 not 0)
    int humanDistance = 0; // his race distance (+8)
    int ahead = 0;         // 0x56ebb4
    int raceType = 0, track = 0, link = 0;
    bool aggressive = false, expert = false;
};
int aiRacersAhead(int humanDistance, const std::vector<int>& racerDistances);
float aiCatchUpFactor(const AiRace& race, int distance, int place);

// One step of a computer cop's driver (car_driver_cop 0x4032e0) for
// cars[self] (copState set), then its aim and speed as aiStep moves them.
// The driver acts only when the gate lets it (every 8 steps): its orders
// (cop_ai_order_start 0x403030: to the roadside and a strip, patrol, a
// run with the siren, a roadblock), patrol (1, cop_ai_drive 0x403270
// with 1.19) and the run with the siren (7, with 2): the lanes and the
// cars round it, the section's speed x k under its top speed (0x401430,
// 0x401370), kept behind the car ahead in its lane or x 0.4 for one coming
// at it (0x40bff0), lane_votes_ahead and the lane chosen; the roadside (3,
// 6.5 m/s, 0x402d90), stopped there (4) laying its strip (0x403d30); a
// roadblock (8, 9 at 6 m/s, 10 in place, 11 across the road, 0x403180);
// the target (0x402e10): a racer within 16 slices over the section's
// speed by more than 6.71 m/s (0x403f80) starts the chase (5,
// cop_ai_chase 0x4025c0 with 0x402420: closing, reversing when past it),
// given up past t_ticket_times' slices; the stop test (0x402110) and the
// stop (6, cop_stop_begin 0x401e40) to the ticket (0x401ff0); every 320
// steps of a chase its calls for help (a strip by the heat, 0x4039c0;
// backup, 0x4043a0) to the free cops in `cars`. Its strip's hits are
// tested each step (0x4297d0, 0x4ca160: the racers' wheels on its quad).
void aiCopStep(std::vector<AiCar>& cars, size_t self, const TrackCollision& col, const AiCarData& data, int step,
               int track, bool weather);

// A car record of a dump of nfs3.exe's cars (0x5e1120, 0x9AC bytes a
// car; physics.md): in play +0x89, flags +0x200 (4 a human, 0x20 a cop,
// 0x40 hidden), the slice +0x1C, the position +0x98 and rows +0xC0 (right,
// up, forward), the brake +0x509, the lights +0x660, the roof light
// +0x734 (on with a cop's siren), the fog beams' scroll +0x944, the speed
// +0x7C4 and side offset +0x7B8, the paint +0x8A8-+0x8B0.
struct DumpedCar {
    bool inPlay = false, human = false, cop = false, hidden = false, siren = false, braking = false;
    int slice = 0, beams = 0, beamScroll = 0;
    float speed = 0, offset = 0;
    std::array<int, 3> paint{}; // the paint in HSV (+0x8A8, +0x8AC, +0x8B0)
    int setup = -1; // its set-up record (+0x220 points at 0x6fd52c + 0x6C each), -1 elsewhere
    std::array<float, 3> at{}, right{}, up{}, forward{};
};
std::vector<DumpedCar> parseCarDump(const std::vector<uint8_t>& bytes);

} // namespace reluva::nfs
