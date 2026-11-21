// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/raven/effect_file.h"

#include "common/archive.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::idtech3::raven {

/**
 * Raven's effects system, as the engine runs it for a client game (Jedi
 * Outcast's multiplayer one): effect files registered and played at a
 * place, their primitives made at once or after their delay, then moved,
 * faded and drawn each frame until their life ends; and the primitives the
 * client game adds itself (lines, sprites, polygons, curves, a saber's
 * trail). See docs/engines/idtech3/formats/efx.md.
 *
 * What it draws is handed over each frame as plain shapes, for the client
 * game's host to put in its scene; what it needs of the world (shaders and
 * models registered, traces, sounds, marks) it asks of the host.
 */
// A strip of a bolt of Jedi Outcast's electricity, from `from` (half as
// wide as `fromWidth`) to `to`.
struct BoltStrip {
    std::array<float, 3> from{};
    std::array<float, 3> to{};
    float fromWidth = 0;
    float toWidth = 0;
};

// The strips Raven's renderer draws a bolt from `origin` to `end` as (the
// GPL source's RB_SurfaceElectricity, checked against jk2mp.exe's,
// 0x494c70): 20-unit steps, each step's point pushed off the straight
// line by an offset that builds up (from 10, 10, 10), its random parts
// from `seed`, so the path holds still; each step three strips through
// two kinks made anew each time from `random` (-1 to 1). Tapered, the
// width falls as 1 - how far squared. 1.02a never forks (its fork count is
// never set, 0x494f4f), so branches add nothing. Shorter than a step:
// none.
std::vector<BoltStrip> electricityStrips(const std::array<float, 3> &origin, const std::array<float, 3> &end,
                                         float radius, float chaos, int32_t seed, bool tapered,
                                         const std::function<float()> &random);

class EffectSystem {
public:
    using Vec3 = std::array<float, 3>;
    using Axis = std::array<Vec3, 3>; // forward, left, up

    struct Trace {
        float fraction = 1;
        Vec3 endPos{};
        Vec3 normal{};
        int32_t surfaceFlags = 0;
    };
    // A decal's mark, made by the client game (its CG_IMPACT_MARK).
    struct Decal {
        int32_t shader = 0;
        Vec3 point{};
        Vec3 normal{};
        float rotation = 0;
        Vec3 rgb{};
        float alpha = 0;
        float size = 0;
    };
    // A Ghoul2 model's bolt an effect follows: the instance, its model and
    // bolt, the entity it's on and the model's scale.
    struct Bolt {
        int32_t ghoul2 = 0;
        int32_t model = 0;
        int32_t bolt = -1;
        int32_t entity = -1;
        Vec3 scale{};
    };
    struct Host {
        std::function<int32_t(const std::string &)> registerShader;
        std::function<int32_t(const std::string &)> registerModel;
        std::function<int32_t(const std::string &)> registerSound;
        // Against the world's solid parts; `mins` and `maxs` zero for a point.
        std::function<Trace(const Vec3 &start, const Vec3 &end, const Vec3 &mins, const Vec3 &maxs)> trace;
        std::function<bool(const Vec3 &)> solidAt;
        std::function<void(const Vec3 &, int32_t sound)> playSound;
        std::function<void(const Decal &)> decal;
        // Where an entity is drawn this frame (for effects that follow one).
        std::function<std::optional<Vec3>(int32_t entity)> entityOrigin;
        // Where a bolted effect's bolt is this frame (none: left where it is).
        std::function<std::optional<Vec3>(const Bolt &bolt)> boltPoint;
    };

    // A shape drawn this frame, as the renderer takes the original's
    // effects: a sprite, a quad facing `axis[0]`, a line or electricity from
    // `origin` to `end` `radius` wide, a cylinder from `origin` (`size2`
    // round) to `end` (`radius` round), or a model at `origin` turned (and
    // scaled) by `axis`.
    struct Drawn {
        enum Kind { kSprite, kQuad, kLine, kElectricity, kCylinder, kModel };
        Kind kind = kSprite;
        int32_t media = 0; // a shader, or a model's handle
        Vec3 origin{};
        Vec3 end{};
        Axis axis{};
        float radius = 0;
        float size2 = 0;
        float rotation = 0; // degrees
        std::array<uint8_t, 4> rgba{};
        bool depthHack = false;
        float chaos = 0;    // electricity: how far its steps stray, of their length
        int32_t seed = 0;   // electricity: its shape's
        bool taper = false; // electricity: narrowing to nothing at its end
    };
    struct Light {
        Vec3 origin{};
        float radius = 0;
        Vec3 rgb{};
    };
    struct PolyVertex {
        Vec3 xyz{};
        std::array<float, 2> st{};
        std::array<uint8_t, 4> rgba{};
    };
    struct Poly {
        int32_t shader = 0;
        std::vector<PolyVertex> vertices;
    };
    struct Frame {
        std::vector<Drawn> drawn;
        std::vector<Light> lights;
        std::vector<Poly> polys;
    };

    EffectSystem(const SearchSet &files, Host host) : _files(files), _host(std::move(host)) {}

    // effects/<name>.efx (the folder and extension may be given); its
    // number, the same for the same name; 0 if it can't be read.
    int32_t registerEffect(std::string_view name);
    // Plays effect `id` at `origin`, facing `axis` (or `forward`, the other
    // axes made up), following `entity` (-1: none) for parts made later.
    void play(int32_t id, const Vec3 &origin, const Axis &axis, int32_t entity = -1);
    void play(int32_t id, const Vec3 &origin, const Vec3 &forward);
    // Plays effect `id` at `origin` facing `forward`, its parts put at
    // `bolt` each frame before they move (FX_PlayBoltedEffectID).
    void playBolted(int32_t id, const Vec3 &origin, const Vec3 &forward, const Bolt &bolt);
    // The effects' clock (ms).
    int32_t time() const { return _time; }
    // The clock (ms) and the eye, each frame before run().
    void setTime(int32_t time, const Vec3 &viewOrigin, const Axis &viewAxis);
    // Makes the parts whose delay is up, moves every live one, drops those
    // whose life is over, and adds what's seen to `out`.
    void run(Frame &out);
    // Everything dropped, effects and their numbers too.
    void clear();

    // The client game's own primitives; times in ms from now, colours and
    // alpha 0 to 1, flags as an effect file's.
    struct Values {
        float start = 1;
        float end = 1;
        float parm = 0;
    };
    void addLine(const Vec3 &start, const Vec3 &end, Values size, Values alpha, const Vec3 &rgbStart,
                 const Vec3 &rgbEnd, float rgbParm, int32_t life, int32_t shader, int32_t flags);
    void addSprite(const Vec3 &origin, const Vec3 &velocity, const Vec3 &acceleration, float scale, Values alpha,
                   float rotation, float bounce, int32_t life, int32_t shader, int32_t flags);
    // Up to 4 corners with their texture coordinates; moving (and turning by
    // `rotationDelta`, pitch and yaw in degrees a 100 ms) after `motionDelay`.
    void addPoly(const std::vector<Vec3> &points, const std::vector<std::array<float, 2>> &st, const Vec3 &velocity,
                 const Vec3 &acceleration, Values alpha, const Vec3 &rgbStart, const Vec3 &rgbEnd, float rgbParm,
                 const Vec3 &rotationDelta, float bounce, int32_t motionDelay, int32_t life, int32_t shader,
                 int32_t flags);
    void addBezier(const Vec3 &start, const Vec3 &end, const Vec3 &control1, const Vec3 &control1Velocity,
                   const Vec3 &control2, const Vec3 &control2Velocity, Values size, Values alpha, const Vec3 &rgbStart,
                   const Vec3 &rgbEnd, float rgbParm, int32_t life, int32_t shader, int32_t flags);
    // A saber's trail: a quad (the new muzzle and tip, the old tip and
    // muzzle), each corner's colour 0 to 255 and texture coordinates moving
    // from their start to their end over its life.
    struct TrailVertex {
        Vec3 origin{};
        Vec3 rgb{};
        float alpha = 0;
        std::array<float, 2> st{};
        std::array<float, 2> destSt{};
    };
    void addTrail(const std::array<TrailVertex, 4> &corners, int32_t shader, int32_t life);

    size_t live() const { return _live.size() + _born.size(); }
    size_t scheduled() const { return _scheduled.size(); }
    // Effects registered, by number (name, primitives).
    size_t registered() const { return _effects.size(); }

private:
    using Primitive = EffectPrimitive;
    // A primitive as registered: its media and the effects it runs as numbers.
    struct Template {
        Primitive p;
        std::vector<int32_t> media;
        std::vector<int32_t> impactFx;
        std::vector<int32_t> deathFx;
        std::vector<int32_t> emitFx;
        std::vector<int32_t> playFx;
    };
    struct Effect {
        std::string name;
        std::vector<Template> templates;
    };
    struct Scheduled {
        int32_t start = 0;
        int32_t effect = 0; // its template's effect and place in it
        size_t index = 0;
        Vec3 origin{};
        Axis axis{};
        int32_t entity = -1;
        int32_t group = -1; // a bolted effect's (_bolted), -1 none
    };
    // A value fading from start to end (start * perc + end * (1 - perc)),
    // as `flags` (one channel's) say; `parm` is a time, or a wave's speed.
    struct Fade {
        float start = 0;
        float end = 0;
        float parm = 0;
    };
    // A live primitive.
    struct Live {
        enum Kind { kParticle, kQuad, kLine, kTail, kElectricity, kCylinder, kEmitter, kLight, kPoly, kBezier, kTrail };
        Kind kind = kParticle;
        int32_t group = -1; // a bolted effect's (_bolted), -1 none
        uint32_t flags = 0;
        int32_t timeStart = 0;
        int32_t timeEnd = 0;
        int32_t media = 0;
        Vec3 origin{};
        Vec3 origin2{};
        Vec3 velocity{};
        Vec3 acceleration{};
        Vec3 mins{};
        Vec3 maxs{};
        float elasticity = 0;
        Fade size;
        Fade size2;
        Fade length;
        Fade alpha;
        Vec3 rgbStart{};
        Vec3 rgbEnd{};
        float rgbParm = 0;
        float rotation = 0;
        float rotationDelta = 0;
        int32_t deathFx = 0;
        int32_t impactFx = 0;
        // Worked out each frame.
        float radius = 0;
        float radius2 = 0;
        float len = 0;
        Vec3 rgb{};
        std::array<uint8_t, 4> rgba{};
        // A quad's or a cylinder's facing; an emitter's turn and model axes.
        Vec3 normal{};
        Vec3 angles{};
        Vec3 angleDelta{};
        Axis axis{};
        // An emitter's effect, how far apart it's left, and where it was.
        int32_t emitFx = 0;
        float density = 0;
        float variance = 0;
        Vec3 oldOrigin{};
        Vec3 oldVelocity{};
        int32_t oldTime = 0;
        // Electricity.
        float chaos = 0;
        int32_t seed = 0;
        // A polygon's corners about its middle, its turn, and when it moves.
        std::vector<Vec3> corners;
        std::vector<std::array<float, 2>> st;
        Vec3 turn{};
        int32_t motionTime = 0;
        // A curve's control points.
        Vec3 control1{};
        Vec3 control2{};
        Vec3 control1Velocity{};
        Vec3 control2Velocity{};
        std::array<TrailVertex, 4> trail{};
    };

    float random() { return std::uniform_real_distribution<float>(0, 1)(_random); }
    float crandom() { return 2 * (random() - 0.5f); }
    float pick(const EffectRange &r) { return r.min == r.max ? r.min : r.min + (r.max - r.min) * random(); }
    template<typename T>
    T pickFrom(const std::vector<T> &v) {
        return v.empty() ? T{} : v[size_t(random() * float(v.size())) % v.size()];
    }
    std::vector<int32_t> registerEffects(const std::vector<std::string> &names);
    // `late`: ms since it was due.
    void create(const Template &t, const Vec3 &origin, const Axis &axis, int32_t late);
    // A fade's parm as given (hundredths of the life, or a wave's speed in
    // half turns a second) made into what update() uses.
    Fade fade(float start, float end, float parm, int channelFlags, int32_t life) const;
    Live &spawn(Live::Kind kind, int32_t life, uint32_t flags);
    float fadeValue(const Live &l, const Fade &f, int channelFlags, bool randomOnValue);
    void updateColour(Live &l);
    bool updateOrigin(Live &l);
    bool update(Live &l);
    void die(const Live &l);
    void draw(Live &l, Frame &out);
    bool behindOrNear(const Vec3 &p, float near) const;

    const SearchSet &_files;
    Host _host;
    std::vector<Effect> _effects;           // by number - 1
    std::map<std::string, int32_t> _byName; // lower case, without the folder and extension
    std::vector<Scheduled> _scheduled;
    // The bolted effects playing, by group, and the next group's number.
    std::map<int32_t, Bolt> _bolted;
    int32_t _nextGroup = 0;
    void playIn(int32_t id, const Vec3 &origin, const Axis &axis, int32_t entity, int32_t group);
    // create(), its parts put in `group`.
    void createIn(const Template &t, const Vec3 &origin, const Axis &axis, int32_t late, int32_t group);
    std::vector<Live> _live;
    std::vector<Live> _born; // made since the last run(), joining _live there
    int32_t _time = 0;
    int32_t _frameTime = 0;
    Vec3 _viewOrigin{};
    Axis _viewAxis{};
    std::minstd_rand _random{1};
};

} // namespace reluva::idtech3::raven
