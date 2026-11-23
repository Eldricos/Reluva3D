// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/tpw/formats/plb.h"
#include "engines/tpw/formats/rse.h"
#include "engines/tpw/script/machine.h"
#include "engines/tpw/ui/front_end.h"
#include "graphics/soft3d/view3d.h"

#include <array>
#include <memory>
#include <vector>

namespace reluva::tpw {

/**
 * Particles in flight: each from a burst of an effect, flying off at
 * random, its colour the effect's at its age, drawn as a small star
 * added onto the screen. See
 * docs/engines/tpw/games/simcoaster/systems/front-end.md#the-effects.
 */
class Particles {
public:
    /** A burst of `effect`'s particles at a point of the game's space, at a time in milliseconds. */
    void burst(const ParticleLibrary::Effect &effect, const gfx::Vec3 &at, uint32_t now);
    /** Draws the particles alive at `now` onto an RGBA32 surface, over what's there. */
    void draw(Surface &screen, const soft3d::OrbitCamera &camera, uint32_t now) const;
    /** Lets go of the particles dead by `now`. */
    void forget(uint32_t now);
    size_t size() const { return _particles.size(); }

private:
    struct Particle {
        gfx::Vec3 at;
        gfx::Vec3 velocity; // units a second
        uint32_t born;
        uint32_t life;
        std::array<uint32_t, 16> colours;
    };
    float random();

    std::vector<Particle> _particles;
    uint32_t _seed = 1;
};

/**
 * The front end's effects: the camera path obstacle's script
 * (`camera.RSE`) run beside the path on the script machine, the
 * particle effects it starts (`EVENT 2 <emitter> <effect>`) burst at the
 * model's emitters and drawn over the scene.
 */
class FrontEndEffects : private ScriptHost {
public:
    FrontEndEffects();
    ~FrontEndEffects() override;

    /** Reads the script and the particle library; `scene` must outlive it. Fails on either missing. */
    Error load(const SearchSet &files, const FrontEndScene &scene);

    /** Runs the script on to `ms` since the path started, the path at `frame`. */
    void advance(uint32_t ms, float frame);
    /** Draws the particles alive at `ms` over the scene. */
    void draw(Surface &screen, const soft3d::OrbitCamera &camera, uint32_t ms) const;
    /** How many particles are in flight. */
    size_t particles() const { return _particles.size(); }

private:
    uint32_t now() const override;
    int random(int below) override;
    Error loadScript(const ScriptMachine &parent, const std::string &name, Script &out) override;
    HostResult perform(ScriptMachine &machine, const Script::Instruction &instruction,
                       const std::vector<int> &values) override;
    int emitterNode(int emitter) const;

    const FrontEndScene *_scene = nullptr;
    ParticleLibrary _library;
    Script _script;
    std::unique_ptr<ScriptMachine> _machine;
    Particles _particles;
    std::vector<gfx::Mat4> _nodes; // the path's nodes at the frame advance() was given
    uint32_t _ms = 0;
    bool _triggered = false;
    uint32_t _random = 1;
};

} // namespace reluva::tpw
