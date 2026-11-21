// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/render/world_effects.h"

#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace reluva::idtech3 {

namespace {

using V3 = std::array<float, 3>;

float dot(const V3 &a, const V3 &b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

V3 sub(const V3 &a, const V3 &b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}

V3 scaled(const V3 &a, float s) {
    return {a[0] * s, a[1] * s, a[2] * s};
}

// As the engines' VectorNormalize: a zero vector stays as it is.
V3 normalized(const V3 &a) {
    const float length = std::sqrt(dot(a, a));
    return length > 0 ? scaled(a, 1 / length) : a;
}

V3 cross(const V3 &a, const V3 &b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

float random(std::mt19937 &rng, float from, float to) {
    return from + (to - from) * std::uniform_real_distribution<float>(0, 1)(rng);
}

// From `from` to `to`, both included (jk2mp.exe's irand, 0x434190).
int irandom(std::mt19937 &rng, int from, int to) {
    return std::uniform_int_distribution<int>(from, to)(rng);
}

// The command's words as COM_ParseExt reads them on one line: runs of
// anything but spaces, or a quoted string.
class Words {
public:
    explicit Words(const std::string &text) : _text(text) {}
    std::string next() {
        while (_at < _text.size() && uint8_t(_text[_at]) <= ' ')
            ++_at;
        std::string word;
        if (_at < _text.size() && _text[_at] == '"') {
            ++_at;
            while (_at < _text.size() && _text[_at] != '"')
                word += _text[_at++];
            if (_at < _text.size())
                ++_at;
            return word;
        }
        while (_at < _text.size() && uint8_t(_text[_at]) > ' ')
            word += _text[_at++];
        return word;
    }
    // What's left after the words taken.
    std::string rest() const { return _text.substr(std::min(_at, _text.size())); }

private:
    std::string _text;
    size_t _at = 0;
};

bool is(const std::string &word, const char *name) {
    return fs::toLower(word) == name;
}

// atof's and atol's way: what the word starts with, 0 if nothing.
float number(const std::string &word) {
    return float(std::atof(word.c_str()));
}

// As the shaders' ParseVector (0x48c0e0): "( a b c )", each value stored
// as it's read; false (and a warning, not kept here) if a parenthesis or
// a value is missing.
bool vectorIn(Words &words, int count, float *out) {
    if (words.next() != "(")
        return false;
    for (int i = 0; i < count; ++i) {
        const std::string word = words.next();
        if (word.empty())
            return false;
        out[i] = number(word);
    }
    return words.next() == ")";
}

gfx::Vertex vertexAt(const V3 &at, const std::array<uint8_t, 4> &rgba, float s = 0, float t = 0) {
    gfx::Vertex v{};
    for (size_t i = 0; i < 3; ++i)
        v.pos[i] = at[i];
    v.uv[0] = s;
    v.uv[1] = t;
    for (size_t i = 0; i < 4; ++i)
        v.color[i] = rgba[i];
    v.normal[2] = 1;
    return v;
}

uint8_t byteOf(float v) {
    return uint8_t(std::clamp(v, 0.0f, 1.0f) * 255 + 0.5f);
}

// A quad as two triangles.
void addQuad(std::vector<gfx::Vertex> &out, const gfx::Vertex &a, const gfx::Vertex &b, const gfx::Vertex &c,
             const gfx::Vertex &d) {
    out.insert(out.end(), {a, b, c, a, c, d});
}

WorldEffects::Batch &batchFor(std::vector<WorldEffects::Batch> &out, WorldEffects::Look look) {
    if (out.empty() || out.back().look != look)
        out.push_back({look, {}});
    return out.back();
}

// Drops start again this high above the eye (0x48e975).
constexpr float kRespawnHeight = 40;
// The drop's width, of the view's left axis (0x4cf510).
constexpr float kDropWidth = 0.2f;
// How fast the rain fades in or out, a second (0x48e891).
constexpr float kFadeSpeed = 0.5f;
// The rain's alpha: jk2sp.exe's (0x48e26d) and jk2mp.exe's (0x49bba0).
constexpr float kSpRainAlpha = 0.15f;
constexpr float kMpRainAlpha = 0.1f;
// The snow's room for its contents: cells across, along and up.
constexpr int kCellsX = 16;
constexpr int kCellsY = 16;
constexpr int kCellsZ = 8;
// A wind blows on nothing while the eye is farther than this from it (0x49a365).
constexpr float kWindReach = 300;
// The misty fog's grid, a side, and its picture's sizes (0x499400).
constexpr int kFogSide = 30;
constexpr float kFogMinSize = 0.15f;
constexpr float kFogMaxSize = 0.3f;
constexpr float kFogSize = 0.1f;

// Frames until the wind turns again: jk2sp.exe's 200 and up to 249 more
// (0x48e802), jk2mp.exe's 200 to 450 (0x49c0b2).
int framesToTurn(std::mt19937 &rng, WorldEffects::Engine engine) {
    return engine == WorldEffects::Engine::kSinglePlayer ? 200 + int(rng() % 250) : irandom(rng, 200, 450);
}

// A direction across, of length 1 (or none).
V3 randomAcross(std::mt19937 &rng) {
    const float x = random(rng, -1, 1);
    const float y = random(rng, -1, 1);
    return normalized({x, y, 0});
}

// What an effect is handed each frame.
struct Frame {
    const WorldEffects::ViewParms &view;
    float seconds = 0;
    int32_t eyeContents = 0;
    std::mt19937 &rng;
};

// A snowflake.
struct Flake {
    V3 pos{};
    V3 velocity{};
    bool drawn = false; // in the open, so drawn
};

} // namespace

// One of a system's list of effects (CWorldEffect): its winds and misty
// fogs, each handed the system's commands, its wind and its frames.
struct WorldEffects::Effect {
    Effect() = default;
    Effect(const Effect &) = delete;
    Effect &operator=(const Effect &) = delete;
    virtual ~Effect() = default;
    virtual bool command(const std::string & /*rest*/) { return false; }
    virtual void setWind(const V3 & /*wind*/) {}
    virtual void setFlakeCount(size_t /*count*/) {}
    virtual void update(const Frame & /*frame*/, std::vector<Flake> * /*flakes*/) {}
    virtual void draw(const WorldEffects::ViewParms & /*view*/, bool /*showWind*/, std::vector<Batch> & /*out*/) const {
    }
};

// A wind (CWind) blowing on the snowflakes: everywhere (the snow's gusts),
// or in a box along its direction (one from "snow wind").
struct WorldEffects::Wind final : WorldEffects::Effect {
    explicit Wind(bool everywhere) : global(everywhere) {}

    // As its UpdateParms (0x49a100): the box's three planes, the first
    // along the wind, the second towards the point, the third across both.
    void set(const V3 &at, const V3 &blowing, const V3 &extent, int frames) {
        point = at;
        size = {extent[0], extent[1] * 2, extent[2] * 2};
        velocity = blowing;
        const V3 along = normalized(blowing);
        const float alongDistance = dot(point, along);
        const V3 towards = normalized(sub(point, scaled(along, alongDistance)));
        const V3 third = normalized(cross(along, towards));
        const std::array<V3, 3> normals{along, towards, third};
        for (size_t i = 0; i < 3; ++i) {
            planes[i] = {normals[i][0], normals[i][1], normals[i][2], dot(point, normals[i]) - size[i] / 2};
            reach[i] = size[i];
        }
        duration = frames;
    }

    void setFlakeCount(size_t count) override { counts.assign(count, 0); }

    // As its Update (0x49a320): within reach of the eye, each flake in its
    // box (any, for the gusts) is pushed by it; one it misses stops the
    // pushing, as the original's loop then tries the same flake again
    // till its count runs out.
    void update(const Frame &frame, std::vector<Flake> *flakes) override {
        if (!enabled || !flakes)
            return;
        const V3 off = sub(frame.view.eye, point);
        if (std::sqrt(dot(off, off)) > kWindReach)
            return;
        // A box from "snow wind" has no list of counts in the original,
        // which would read one that isn't there; here it's made.
        if (counts.size() != flakes->size())
            counts.assign(flakes->size(), 0);
        std::array<float, 3> distances{};
        size_t item = 0;
        for (size_t left = flakes->size(); left; --left) {
            int &count = counts[item];
            if (count) {
                --count;
                ++item;
                continue;
            }
            Flake &flake = (*flakes)[item];
            if (!global) {
                size_t j = 0;
                for (; j < 3; ++j) {
                    const float d = dot(flake.pos, {planes[j][0], planes[j][1], planes[j][2]}) - planes[j][3];
                    if (d < 0.01f || d > reach[j])
                        break;
                    distances[j] = d;
                }
                if (j != 3)
                    continue;
            }
            count = int(float(duration) * (1 - distances[0] / reach[0]));
            for (size_t i = 0; i < 3; ++i)
                flake.velocity[i] += frame.seconds * velocity[i];
            ++item;
        }
    }

    // With "debug wind", its box's three near sides, red, green and blue
    // and half see-through (0x49a500).
    void draw(const WorldEffects::ViewParms & /*view*/, bool showWind, std::vector<Batch> &out) const override {
        if (!enabled || !showWind)
            return;
        auto corner = [&](float a, float b, float c) {
            V3 p = point;
            const std::array<float, 3> by{a, b, c};
            for (size_t j = 0; j < 3; ++j)
                for (size_t i = 0; i < 3; ++i)
                    p[i] += by[j] * size[j] / 2 * planes[j][i];
            return p;
        };
        std::vector<gfx::Vertex> &v = batchFor(out, Look::kPlain).vertices;
        auto side = [&](const std::array<uint8_t, 4> &rgba, const std::array<V3, 4> &at) {
            addQuad(v, vertexAt(at[0], rgba), vertexAt(at[1], rgba), vertexAt(at[2], rgba), vertexAt(at[3], rgba));
        };
        side({255, 0, 0, 128}, {corner(-1, -1, -1), corner(-1, 1, -1), corner(-1, 1, 1), corner(-1, -1, 1)});
        side({0, 255, 0, 128}, {corner(-1, -1, -1), corner(1, -1, -1), corner(1, -1, 1), corner(-1, -1, 1)});
        side({0, 0, 255, 128}, {corner(-1, -1, -1), corner(1, -1, -1), corner(1, 1, -1), corner(-1, 1, -1)});
    }

    bool global = false;
    bool enabled = true;
    V3 point{};
    V3 size{};
    V3 velocity{};
    std::array<std::array<float, 4>, 3> planes{}; // normal, distance
    std::array<float, 3> reach{};
    int duration = 0;
    std::vector<int> counts; // frames each flake is left alone
};

// A window onto the fog's picture (CMistyFog), drifting with the wind;
// one of a pair: as it shrinks or grows too far it fades out and its
// partner fades in somewhere else.
struct WorldEffects::Fog {
    // As its CreateTextureCoords (0x4997f0): smaller if the wind blows
    // away from the eye, larger if towards it.
    void place(const WorldEffects::ViewParms &view, std::mt19937 &rng) {
        speed = random(rng, 200, 700);
        const float forward = dot(wind, view.forward);
        if (forward > 0.5f)
            size = random(rng, kFogMinSize, kFogMinSize * 1.01f);
        else if (forward < -0.5f)
            size = random(rng, kFogMaxSize - kFogMinSize, kFogMaxSize);
        else
            size = random(rng, kFogMinSize * 1.5f, kFogMinSize * 1.5f + kFogSize);
        size /= 2;
        const float x = (1 - size - 0.4f) * random(rng, 0, 1) + 0.2f;
        const float y = (1 - size - 0.4f) * random(rng, 0, 1) + 0.2f;
        coords = {x - size, y - size, x + size, y + size};
    }

    // Its partner fading out: in again from nothing (0x499790 on).
    void restart(const WorldEffects::ViewParms &view, std::mt19937 &rng) {
        alpha = 0;
        direction = 0.025f;
        fading = true;
        place(view, rng);
        drawing = true;
    }

    // As its Update (0x4995a0), each frame whatever its length.
    void update(const WorldEffects::ViewParms &view, std::mt19937 &rng) {
        if (!drawing)
            return;
        const float forward = dot(wind, view.forward);
        const float across = dot(wind, view.left);
        coords[0] += across / speed;
        coords[2] += across / speed;
        const float zoom = forward / speed / 4;
        coords[0] -= zoom;
        coords[1] -= zoom;
        coords[2] += zoom;
        coords[3] += zoom;
        const float w = std::abs(coords[0] - coords[2]);
        const float h = std::abs(coords[1] - coords[3]);
        if ((w < kFogMinSize || h < kFogMinSize || w > kFogMaxSize || h > kFogMaxSize) && !fading) {
            fading = true;
            direction = -0.025f;
            if (partner)
                partner->restart(view, rng);
        }
        if (fading) {
            alpha += direction * 0.4f;
            if (alpha < 0) {
                drawing = false;
                alpha = 0;
            } else if (alpha >= 1) {
                fading = false;
                alpha = 1;
            }
        }
    }

    std::array<float, 4> coords{}; // s, t of one corner, then the other's
    float alpha = 1;
    bool fading = false;
    bool drawing = false;
    float direction = 0;
    float speed = 0;
    float size = 0;
    V3 wind{};
    Fog *partner = nullptr;
};

// The misty fog (CMistyFog2): a grid over the view whose corners' alphas
// are read from the picture through two pairs of drifting windows, added
// over the scene while the eye is in the open.
struct WorldEffects::FogLayer final : WorldEffects::Effect {
    FogLayer(std::shared_ptr<const WeatherPicture> image, const WorldEffects::ViewParms &view, std::mt19937 &rng)
        : picture(std::move(image)) {
        for (size_t i = 0; i < 4; i += 2) {
            fogs[i].partner = &fogs[i + 1];
            fogs[i + 1].partner = &fogs[i];
            fogs[i].drawing = true;
        }
        for (Fog &fog : fogs)
            fog.place(view, rng);
        // The grid, 20 across at 10 in front of the eye, its points a
        // little off their places (0x4999cd on).
        const float step = 20.0f / float(kFogSide - 1);
        for (int y = 0; y < kFogSide; ++y)
            for (int x = 0; x < kFogSide; ++x) {
                const float jx = random(rng, -step / 16, step / 16);
                const float jy = random(rng, -step / 16, step / 16);
                points[size_t(y * kFogSide + x)] = {-10 + float(x) * step + jx, 10 - float(y) * step + jy, -10};
            }
    }

    // "fog density <alpha>" (0x499af0).
    bool command(const std::string &rest) override {
        Words words(rest);
        if (!is(words.next(), "fog") || !is(words.next(), "density"))
            return false;
        density = number(words.next());
        return true;
    }

    void setWind(const V3 &wind) override {
        for (Fog &fog : fogs)
            fog.wind = wind;
    }

    // As its Update (0x499b70): fading in over two seconds in the open
    // and out of water, out as long outside them; the windows moved on,
    // then each corner's alpha read through those showing.
    void update(const Frame &frame, std::vector<Flake> * /*flakes*/) override {
        if ((frame.eyeContents & kContentsOutside) && !(frame.eyeContents & kContentsWater)) {
            if (fade < 1)
                fade += frame.seconds / 2;
            fade = std::min(fade, 1.0f);
        } else {
            if (fade > 0)
                fade -= frame.seconds / 2;
            if (fade <= 0)
                return;
        }
        alphas.fill(0);
        // In the original's order: each pair's second, then its first.
        for (const size_t i : {3, 1, 0, 2})
            fogs[i].update(frame.view, frame.rng);
        for (const size_t i : {2, 3, 0, 1})
            sample(fogs[i]);
    }

    // As its UpdateTexture (0x499c70): the picture read a byte a point, as
    // the original reads its RGBA bytes, wrapping.
    void sample(const Fog &fog) {
        if (!fog.drawing || !picture || picture->width <= 0 || picture->height <= 0)
            return;
        const float alpha = fog.alpha * density * (1.0f / 255.0f) * fade;
        const float xStep = (fog.coords[2] - fog.coords[0]) / float(kFogSide);
        const float yStep = (fog.coords[3] - fog.coords[1]) / float(kFogSide);
        const int w = picture->width;
        const int h = picture->height;
        float yPos = fog.coords[1];
        for (int y = 0; y < kFogSide; ++y, yPos += yStep) {
            const int ty = int(yPos * float(h)) & (h - 1);
            float xPos = fog.coords[0];
            for (int x = 0; x < kFogSide; ++x, xPos += xStep) {
                const int tx = int(xPos * float(w)) & (w - 1);
                const size_t at = size_t(ty) * size_t(w) + size_t(tx);
                if (at < picture->rgba.size())
                    alphas[size_t(y * kFogSide + x)] += float(picture->rgba[at]) * alpha;
            }
        }
    }

    // As its Render (0x499e20): the grid's quads, white, added by their
    // corners' alphas, in its own view.
    void draw(const WorldEffects::ViewParms & /*view*/, bool /*showWind*/, std::vector<Batch> &out) const override {
        if (fade <= 0)
            return;
        std::vector<gfx::Vertex> &v = batchFor(out, Look::kFog).vertices;
        auto corner = [&](int x, int y) {
            const size_t i = size_t(y * kFogSide + x);
            return vertexAt(points[i], {255, 255, 255, byteOf(alphas[i])});
        };
        for (int y = 0; y + 1 < kFogSide; ++y)
            for (int x = 0; x + 1 < kFogSide; ++x)
                addQuad(v, corner(x, y), corner(x + 1, y), corner(x + 1, y + 1), corner(x, y + 1));
    }

    std::shared_ptr<const WeatherPicture> picture;
    std::array<Fog, 4> fogs; // two pairs, each a window and its partner
    std::array<V3, size_t(kFogSide *kFogSide)> points{};
    std::array<float, size_t(kFogSide *kFogSide)> alphas{};
    float density = 0.3f;
    float fade = 0;
};

// What rain and snow share (CWorldEffectsSystem): their list of effects.
struct WorldEffects::Effects {
    bool command(const std::string &rest) {
        for (const auto &e : list)
            if (e->command(rest))
                return true;
        return false;
    }
    void setWind(const V3 &wind) {
        for (const auto &e : list)
            e->setWind(wind);
    }
    void update(const Frame &frame, std::vector<Flake> *flakes) {
        for (const auto &e : list)
            e->update(frame, flakes);
    }
    void draw(const WorldEffects::ViewParms &view, bool showWind, std::vector<Batch> &out) const {
        for (const auto &e : list)
            e->draw(view, showWind, out);
    }
    std::vector<std::unique_ptr<Effect>> list;
};

// Rain (CRainSystem): drops falling round the eye, leaning in a wind that
// turns now and then, while the eye is in the open.
struct WorldEffects::Rain {
    Rain(int count, WorldEffects::Engine from, std::mt19937 &rng)
        : engine(from), alpha(from == Engine::kSinglePlayer ? kSpRainAlpha : kMpRainAlpha) {
        drops.resize(size_t(std::max(count, 0)));
        for (auto &drop : drops)
            respawn(drop, true, rng);
    }

    void respawn(std::array<float, 6> &drop, bool start, std::mt19937 &rng) const {
        // Anywhere round the eye; at the start anywhere in height, after
        // at the top (0x48e420, 0x48e958).
        drop[0] = random(rng, 0, spread[0]);
        drop[1] = random(rng, 0, spread[1]);
        drop[2] = start ? random(rng, -spread[2], kRespawnHeight) : kRespawnHeight;
        for (size_t i = 0; i < 3; ++i)
            drop[3 + i] = random(rng, minVelocity[i], maxVelocity[i]);
    }

    // Its own commands (0x48e530, 0x49be30), after its effects'.
    bool command(const std::string &rest, WorldEffects &owner) {
        if (effects.command(rest))
            return true;
        Words words(rest);
        const std::string verb = words.next();
        if (is(verb, "fog")) {
            if (!owner.addFog(effects))
                return true;
            gustFrames = 0;
        } else if (is(verb, "fall")) {
            std::array<float, 2> fall{};
            if (vectorIn(words, 2, fall.data())) {
                minVelocity[2] = fall[0];
                maxVelocity[2] = fall[1];
            }
        } else if (is(verb, "spread")) {
            vectorIn(words, 2, &spread[1]);
        } else if (is(verb, "alpha")) {
            alpha = number(words.next());
        } else if (is(verb, "height")) {
            height = number(words.next());
        } else if (is(verb, "angle")) {
            windAngle = number(words.next());
        } else {
            return false;
        }
        return true;
    }

    // As its Update (0x48e740, 0x49c040).
    void update(const Frame &frame) {
        const float seconds = std::max(frame.seconds, 0.0f);
        // A gust's direction now and then, across; the wind turns to it,
        // and the fog is told the wind as it was.
        if (--gustFrames < 0) {
            windTarget = randomAcross(frame.rng);
            gustFrames = framesToTurn(frame.rng, engine);
            effects.setWind(wind);
        }
        for (size_t i = 0; i < 3; ++i)
            wind[i] += (windTarget[i] - wind[i]) * seconds;
        effects.update(frame, nullptr);
        // In the open and out of water it fades in, else out.
        fallingHere = (frame.eyeContents & kContentsOutside) && !(frame.eyeContents & kContentsWater);
        if (fallingHere)
            fade = std::min(1.0f, fade + seconds * kFadeSpeed);
        else
            fade = std::max(0.0f, fade - seconds * kFadeSpeed);
        if (fade <= 0)
            return;
        for (auto &drop : drops) {
            for (size_t i = 0; i < 3; ++i)
                drop[i] += drop[3 + i] * seconds;
            if (drop[2] < -spread[2])
                respawn(drop, false, frame.rng);
        }
    }

    // As its Render (0x48e9d0, 0x49c270): its effects, then each drop a
    // triangle from where it is (leant with the wind the lower it is),
    // across by a fifth of the left axis, and down by its height against
    // the wind. Below the eye a drop is fainter the nearer it is, as the
    // alpha times its distance over its depth.
    void draw(const WorldEffects::ViewParms &view, bool showWind, std::vector<Batch> &out) const {
        effects.draw(view, showWind, out);
        if (drops.empty() || fade <= 0)
            return;
        const V3 side = scaled(view.left, kDropWidth);
        const V3 fall{-height * windAngle * wind[0], -height * windAngle * wind[1], -height};
        std::vector<gfx::Vertex> &v = batchFor(out, Look::kRain).vertices;
        v.reserve(v.size() + drops.size() * 3);
        for (const auto &drop : drops) {
            float a = alpha;
            if (drop[2] < 0)
                a = std::min(-drop[1] / drop[2] * alpha, alpha);
            const std::array<uint8_t, 4> rgba{255, 255, 255, byteOf(a * fade)};
            const V3 at{view.eye[0] + std::sin(drop[0]) * drop[1] + drop[2] * windAngle * wind[0],
                        view.eye[1] + std::cos(drop[0]) * drop[1] + drop[2] * windAngle * wind[1],
                        view.eye[2] + drop[2]};
            const V3 across{at[0] + side[0], at[1] + side[1], at[2] + side[2]};
            const V3 below{across[0] + fall[0], across[1] + fall[1], across[2] + fall[2]};
            v.push_back(vertexAt(at, rgba, 1, 0));
            v.push_back(vertexAt(across, rgba, 0, 0));
            v.push_back(vertexAt(below, rgba, 0, 1));
        }
    }

    WorldEffects::Engine engine;
    // Each drop: the angle round the eye, its distance, its height; then
    // how fast each changes a second.
    std::vector<std::array<float, 6>> drops;
    float height = 5;
    std::array<float, 3> spread{6.28318531f, 20, 20}; // angle, distance, height below the eye
    float alpha;
    float windAngle = 1;
    std::array<float, 3> minVelocity{0.1f, 0.1f, -60};
    std::array<float, 3> maxVelocity{-0.1f, -0.1f, -50};
    V3 wind{};       // blowing now
    V3 windTarget{}; // blowing towards
    int gustFrames = 0;
    float fade = 0;
    bool fallingHere = false;
    Effects effects;
};

// Snow (CSnowSystem): flakes in a box round the eye, drifting, blown by
// gusts, drawn where the level is open to the sky.
struct WorldEffects::Snow {
    Snow(int count, WorldEffects::Engine from) : engine(from) {
        // As its Init (0x49aeb0): every flake far away, so the first frame
        // places them all.
        flakes.resize(size_t(std::max(count, 0)));
        for (Flake &f : flakes)
            f.pos = {99999, 99999, 99999};
        mins = maxs = {99999, 99999, 99999};
        // The cells are sized by the spread it starts with, and stay so.
        cell = {(maxSpread[0] - minSpread[0]) / kCellsX, (maxSpread[1] - minSpread[1]) / kCellsY,
                (maxSpread[2] - minSpread[2]) / kCellsZ};
        auto g = std::make_unique<Wind>(true);
        g->enabled = false;
        gust = g.get();
        effects.list.push_back(std::move(g));
        for (const auto &e : effects.list)
            e->setFlakeCount(flakes.size());
    }

    // Its own commands (0x49af60), after its effects'.
    bool command(const std::string &rest, WorldEffects &owner) {
        if (effects.command(rest))
            return true;
        Words words(rest);
        const std::string verb = words.next();
        if (is(verb, "wind")) {
            std::array<V3, 3> values{};
            for (V3 &value : values)
                vectorIn(words, 3, value.data());
            auto w = std::make_unique<Wind>(false);
            w->set(values[0], values[1], values[2], 0);
            effects.list.push_back(std::move(w));
        } else if (is(verb, "fog")) {
            if (!owner.addFog(effects))
                return true;
            windChange = 0;
        } else if (is(verb, "alpha")) {
            alpha = number(words.next());
        } else if (is(verb, "spread") || is(verb, "velocity")) {
            // "velocity" sets the spread too, as the original's does.
            vectorIn(words, 3, minSpread.data());
            vectorIn(words, 3, maxSpread.data());
        } else if (is(verb, "blowing")) {
            const std::string what = words.next();
            if (is(what, "duration")) {
                windDuration = float(std::atol(words.next().c_str()));
            } else if (is(what, "low")) {
                windLow = float(std::atol(words.next().c_str()));
            } else if (is(what, "velocity")) {
                std::array<float, 2> range{};
                vectorIn(words, 2, range.data());
                windMin = range[0];
                windMax = range[1];
            } else if (is(what, "size")) {
                vectorIn(words, 3, windSize.data());
            } else {
                return false;
            }
        } else {
            return false;
        }
        return true;
    }

    // As its Update (0x49b2f0).
    void update(const Frame &frame, const std::function<int32_t(const V3 &)> &contentsAt) {
        std::mt19937 &rng = frame.rng;
        if (--windChange < 0) {
            windDirection = randomAcross(rng);
            windChange = framesToTurn(rng, engine);
            effects.setWind(windDirection);
        }
        if (overall & kContentsOutside)
            effects.update(frame, &flakes);
        const V3 &origin = frame.view.eye;
        // A gust now and then, round the eye, each way its own speed
        // (0x49b450 on).
        nextGust -= frame.seconds;
        if (nextGust < 0)
            gust->enabled = false;
        if (nextGust < lowSize) {
            V3 blowing = randomAcross(rng);
            for (float &b : blowing)
                b *= random(rng, windMin, windMax);
            gust->enabled = true;
            gust->set(origin, blowing, windSize, 0);
            nextGust = random(rng, windDuration, windDuration * 2);
            lowSize = -random(rng, windLow, windLow * 3);
        }
        // The box round the eye; moved more than 25 any way, the level's
        // contents are read again in its cells, and the flakes it's left
        // behind brought into the part it's moved into.
        V3 newMins{};
        V3 newMaxs{};
        V3 difference{};
        V3 start{};
        for (size_t i = 0; i < 3; ++i) {
            newMins[i] = minSpread[i] + origin[i];
            newMaxs[i] = maxSpread[i] + origin[i];
            difference[i] = newMaxs[i] - maxs[i];
            if (difference[i] >= 0) {
                difference[i] = std::min(difference[i], newMaxs[i] - newMins[i]);
                start[i] = newMaxs[i] - difference[i];
            } else {
                difference[i] = std::max(difference[i], newMins[i] - newMaxs[i]);
                start[i] = newMins[i] - difference[i];
            }
        }
        if (std::abs(difference[0]) > 25 || std::abs(difference[1]) > 25 || std::abs(difference[2]) > 25) {
            for (size_t i = 0; i < 3; ++i)
                gridStart[i] = float(int((origin[i] + minSpread[i]) / cell[i])) * cell[i];
            overall = 0;
            size_t n = 0;
            V3 pos{};
            pos[2] = gridStart[2];
            for (int z = 0; z < kCellsZ; ++z, pos[2] += cell[2]) {
                pos[1] = gridStart[1];
                for (int y = 0; y < kCellsY; ++y, pos[1] += cell[1]) {
                    pos[0] = gridStart[0];
                    for (int x = 0; x < kCellsX; ++x, pos[0] += cell[0]) {
                        const int32_t c = contentsAt ? contentsAt(pos) : 0;
                        overall |= c;
                        grid[n++] = c;
                    }
                }
            }
            for (Flake &f : flakes) {
                bool reset = false;
                for (size_t i = 0; i < 3; ++i)
                    if (f.pos[i] < newMins[i] || f.pos[i] > newMaxs[i]) {
                        f.pos[i] = random(rng, 0, difference[i]) + start[i];
                        reset = true;
                    }
                if (reset)
                    f.velocity = {0, 0, random(rng, maxVelocity[2], minVelocity[2])};
            }
            mins = newMins;
            maxs = newMaxs;
        }
        if (!(overall & kContentsOutside)) {
            snowing = false;
            return;
        }
        snowing = true;
        // Each flake drifts across within the velocities, falls no slower
        // than the least, and is drawn only in an open cell; one that falls
        // out of the box starts again at its top.
        const float stabilize = 18 * frame.seconds; // 0x49ad49
        for (Flake &f : flakes) {
            for (size_t i = 0; i < 2; ++i) {
                if (f.velocity[i] < minVelocity[i])
                    f.velocity[i] += stabilize;
                else if (f.velocity[i] > maxVelocity[i])
                    f.velocity[i] -= stabilize;
                else
                    f.velocity[i] += random(rng, -1.4f, 1.4f);
            }
            if (f.velocity[2] > minVelocity[2])
                f.velocity[2] -= 18 * 2;
            for (size_t i = 0; i < 3; ++i)
                f.pos[i] += frame.seconds * f.velocity[i];
            bool reset = f.pos[2] < newMins[2];
            size_t at = 0;
            if (!reset) {
                const int x = int((f.pos[0] - gridStart[0]) / cell[0]);
                const int y = int((f.pos[1] - gridStart[1]) / cell[1]);
                const int z = int((f.pos[2] - gridStart[2]) / cell[2]);
                reset = x < 0 || x >= kCellsX || y < 0 || y >= kCellsY || z < 0 || z >= kCellsZ;
                at = size_t((z * kCellsY + y) * kCellsX + x);
            }
            if (reset) {
                f.pos[2] = newMaxs[2] - (newMins[2] - f.pos[2]);
                if (f.pos[2] < newMins[2] || f.pos[2] > newMaxs[2])
                    f.pos[2] = random(rng, newMins[2], newMaxs[2]);
                f.pos[0] = random(rng, newMins[0], newMaxs[0]);
                f.pos[1] = random(rng, newMins[1], newMaxs[1]);
                f.velocity = {0, 0, random(rng, maxVelocity[2], minVelocity[2])};
                f.drawn = false;
            } else {
                f.drawn = (grid[at] & kContentsOutside) != 0;
            }
        }
    }

    // As its Render (0x49ba70): its effects, then each flake drawn a grey
    // point, as large as the original's points at their distance (10
    // pixels over the square root of 1 + 0.0004 x the distance squared,
    // 1 to 4: 0x49babf on), here a square facing the eye.
    void draw(const WorldEffects::ViewParms &view, bool showWind, std::vector<Batch> &out) const {
        if (!(overall & kContentsOutside))
            return;
        effects.draw(view, showWind, out);
        const std::array<uint8_t, 4> rgba{204, 204, 204, byteOf(alpha)};
        std::vector<gfx::Vertex> &v = batchFor(out, Look::kPlain).vertices;
        for (const Flake &f : flakes) {
            if (!f.drawn)
                continue;
            const V3 off = sub(f.pos, view.eye);
            const float d = std::sqrt(dot(off, off));
            const float pixels = std::clamp(10 / std::sqrt(1 + 0.0004f * d * d), 1.0f, 4.0f);
            const float half = pixels / 2 * d * view.pixel;
            auto at = [&](float l, float u) {
                V3 p = f.pos;
                for (size_t i = 0; i < 3; ++i)
                    p[i] += (view.left[i] * l + view.up[i] * u) * half;
                return vertexAt(p, rgba);
            };
            addQuad(v, at(1, 1), at(-1, 1), at(-1, -1), at(1, -1));
        }
    }

    WorldEffects::Engine engine;
    float alpha = 0.09f;
    V3 minSpread{-600, -600, -200};
    V3 maxSpread{600, 600, 250};
    V3 minVelocity{-15, -15, -20};
    V3 maxVelocity{15, 15, -70};
    float windDuration = 2;
    float windLow = 3;
    float windMin = 30;
    float windMax = 70;
    V3 windSize{1000, 300, 300};
    V3 mins{};
    V3 maxs{};
    float nextGust = 0;
    float lowSize = 0;
    Wind *gust = nullptr; // in effects
    V3 windDirection{};
    int windChange = 0;
    std::vector<Flake> flakes;
    std::array<int32_t, size_t(kCellsX *kCellsY *kCellsZ)> grid{};
    V3 cell{};
    V3 gridStart{};
    int32_t overall = 0;
    bool snowing = false;
    Effects effects;
};

WorldEffects::WorldEffects(Engine engine) : _engine(engine) {
}

WorldEffects::~WorldEffects() = default;
WorldEffects::WorldEffects(WorldEffects &&) noexcept = default;
WorldEffects &WorldEffects::operator=(WorldEffects &&) noexcept = default;

void WorldEffects::clear() {
    _rain.reset();
    _snow.reset();
}

bool WorldEffects::addFog(Effects &effects) {
    // A misty fog of picture 2 (0x499910): an error ends the level if it
    // isn't there (0x4994cc), as it isn't in any release's paks.
    const std::string name = "gfx/world/fog2";
    if (!_fogPicture) {
        auto picture = std::make_shared<WeatherPicture>();
        if (!loadPicture || !loadPicture(name, *picture)) {
            _error = "Could not load " + name;
            return false;
        }
        _fogPicture = std::move(picture);
    }
    effects.list.push_back(std::make_unique<FogLayer>(_fogPicture, _view, _random));
    return true;
}

WorldEffects::Outcome WorldEffects::command(const std::string &line) {
    // As R_WorldEffectCommand (0x48ed50, 0x49c570): a system's "init
    // <count>" starts it afresh, "remove" stops it, the rest go to it;
    // "debug wind" shows the winds' boxes, "debug blah" starts a test
    // snow; multiplayer's "exec" runs the rest as a console command.
    Outcome out;
    Words words(line);
    const std::string first = words.next();
    if (is(first, "snow") || is(first, "rain"))
        return systemCommand(fs::toLower(first), words.rest());
    if (is(first, "debug")) {
        const std::string what = words.next();
        if (is(what, "wind")) {
            _showWind = !_showWind;
            out.understood = true;
        } else if (is(what, "blah")) {
            for (const char *test : {"snow init 1000", "snow alpha 1", "snow fog"}) {
                Outcome o = command(test);
                if (!o.error.empty())
                    return o;
            }
            out.understood = true;
        }
        return out;
    }
    if (is(first, "exec") && _engine == Engine::kMultiplayer) {
        out.execute = words.rest();
        out.understood = true;
    }
    return out;
}

WorldEffects::Outcome WorldEffects::systemCommand(const std::string &system, const std::string &rest) {
    Outcome out;
    Words words(rest);
    const std::string verb = words.next();
    const bool snow = system == "snow";
    if (is(verb, "init")) {
        const int count = std::atoi(words.next().c_str());
        if (snow)
            _snow = std::make_unique<Snow>(count, _engine);
        else
            _rain = std::make_unique<Rain>(count, _engine, _random);
        out.understood = true;
        return out;
    }
    if (is(verb, "remove")) {
        if (snow)
            _snow.reset();
        else
            _rain.reset();
        out.understood = true;
        return out;
    }
    _error.clear();
    if (snow && _snow)
        out.understood = _snow->command(rest, *this);
    else if (!snow && _rain)
        out.understood = _rain->command(rest, *this);
    out.error = std::exchange(_error, std::string());
    return out;
}

void WorldEffects::update(float seconds, const ViewParms &view) {
    // As the world effects pass (0x48eca0, 0x49c4d0): the contents at the
    // eye, then the rain, then the snow.
    _view = view;
    const int32_t eyeContents = contents ? contents(view.eye) : 0;
    const Frame frame{view, seconds, eyeContents, _random};
    if (_rain)
        _rain->update(frame);
    if (_snow)
        _snow->update(frame, contents);
}

void WorldEffects::draw(const ViewParms &view, std::vector<Batch> &out) const {
    if (_rain)
        _rain->draw(view, _showWind, out);
    if (_snow)
        _snow->draw(view, _showWind, out);
}

gfx::Mat4 WorldEffects::fogView() {
    return gfx::perspective(80 * 3.14159265f / 180, 1, 4, 2048);
}

bool WorldEffects::fallingHere() const {
    return (_rain && _rain->fallingHere) || (_snow && _snow->snowing);
}

bool WorldEffects::windSpeed(float &speed) const {
    // The rain's GetFloatVariable (0x49bdf0): its lean x 75.
    if (!_rain)
        return false;
    speed = _rain->windAngle * 75;
    return true;
}

bool WorldEffects::windVector(std::array<float, 3> &wind) const {
    // R_GetWindVector (0x49c8b0): the rain's wind as it blows now, else the
    // snow's direction.
    if (_rain)
        wind = _rain->wind;
    else if (_snow)
        wind = _snow->windDirection;
    else
        return false;
    return true;
}

float WorldEffects::rainFade() const {
    return _rain ? _rain->fade : 0;
}

} // namespace reluva::idtech3
