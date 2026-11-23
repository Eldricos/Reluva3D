// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// What the script asks for is the game's; how a particle flies and looks
// isn't worked out from the library yet, and is sized to the recording:
// docs/engines/tpw/games/simcoaster/systems/front-end.md#the-effects.

#include "engines/tpw/ui/front_end_effects.h"

#include "engines/tpw/ui/model_view.h"

#include <algorithm>
#include <cmath>

namespace reluva::tpw {

namespace {

constexpr const char *kScript = "levels/wIntro/obstacles/Camera/camera.RSE";
constexpr const char *kLibrary = "Particle/Tp2.plb";
constexpr const char *kTrigger = "VAR_OBSTACLETRIG";

// The script's own waits put its first sparkles 9.8 s after it's set
// going; the recording has them 13.2 s after the path starts.
constexpr uint32_t kTriggerMs = 3400;

constexpr uint16_t kEventOpcode = 13;
constexpr uint16_t kWait4AnimOpcode = 46;
constexpr int kParticleEvent = 2;
constexpr int kSoundEvent = 5;
constexpr const char *kSoundFolder = "global/sound";

// The emitters the script names: 11 to 14 the four Fx helpers round the
// star, which it always sets off together; 1 the well's middle.
constexpr int kFirstFx = 11;
constexpr int kWellEmitter = 1;

// A particle's flight and look *(guessed)*: so many units a second at
// most, falling at so many a second each second, its star's arms so
// long, as the recording's sparkles spread and look.
constexpr float kSpeed = 6;
constexpr float kFall = 3;
constexpr float kArm = 1.2f;
constexpr int kParticlesPerBurst = 20;
// Turns of the script a call: it gives way at each ENDSLICE and wait.
constexpr int kMostTurns = 64;

// Adds a colour onto a pixel, `weight` 0 to 1.
void add(uint8_t *p, uint32_t argb, float weight) {
    const float a = float(argb >> 24) / 255 * weight;
    for (int i = 0; i < 3; ++i) {
        const float c = float((argb >> (16 - 8 * i)) & 0xff);
        p[i] = uint8_t(std::min(255.0f, float(p[i]) + c * a));
    }
}

} // namespace

float Particles::random() {
    _seed = _seed * 1103515245u + 12345u;
    return float((_seed >> 8) & 0xffff) / 65535.0f;
}

void Particles::burst(const ParticleLibrary::Effect &effect, const gfx::Vec3 &at, uint32_t now) {
    for (int i = 0; i < kParticlesPerBurst; ++i) {
        // A direction on the sphere, a speed up to kSpeed.
        const float z = 2 * random() - 1;
        const float turn = 6.2831853f * random();
        const float r = std::sqrt(std::max(0.0f, 1 - z * z));
        const float speed = kSpeed * random();
        _particles.push_back({at,
                              {r * std::cos(turn) * speed, z * speed, r * std::sin(turn) * speed},
                              now,
                              std::max<uint32_t>(1, effect.lifeMs),
                              effect.colours});
    }
}

void Particles::forget(uint32_t now) {
    std::erase_if(_particles, [now](const Particle &p) { return now >= p.born + p.life; });
}

void Particles::draw(Surface &screen, const soft3d::OrbitCamera &camera, uint32_t now) const {
    for (const Particle &p : _particles) {
        if (now < p.born || now >= p.born + p.life)
            continue;
        const float t = float(now - p.born) / 1000;
        const gfx::Vec3 at{p.at[0] + p.velocity[0] * t, p.at[1] + p.velocity[1] * t - kFall * t * t / 2,
                           p.at[2] + p.velocity[2] * t};
        const soft3d::ViewVertex v = camera.toView(toViewerWorld(at));
        if (v.z < camera.nearZ)
            continue;
        const soft3d::ScreenVertex s = camera.project(v);
        // Its colour along the ramp by its age.
        const float age = float(now - p.born) / float(p.life) * float(p.colours.size() - 1);
        const uint32_t colour = p.colours[std::min(p.colours.size() - 1, size_t(age))];
        const int arm = std::max(1, int(kArm * camera.focal / v.z));
        const int cx = int(s.x);
        const int cy = int(s.y);
        for (int d = -arm; d <= arm; ++d) {
            const float weight = 1 - float(std::abs(d)) / float(arm + 1);
            for (const auto &[x, y] : {std::pair{cx + d, cy}, std::pair{cx, cy + d}})
                if (x >= 0 && y >= 0 && x < screen.w && y < screen.h && (d != 0 || x == cx))
                    add(screen.row(y) + x * 4, colour, weight);
        }
    }
}

FrontEndEffects::FrontEndEffects() = default;
FrontEndEffects::~FrontEndEffects() = default;

Error FrontEndEffects::load(const SearchSet &files, const FrontEndScene &scene) {
    _scene = &scene;
    std::vector<uint8_t> data;
    if (Error err = files.read(kScript, data))
        return err;
    if (Error err = _script.parse(data))
        return err;
    if (Error err = files.read(kLibrary, data))
        return err;
    if (Error err = _library.parse(data))
        return err;
    _sounds = SoundEffects();
    if (_sounds.load(files, kSoundFolder, SoundEffects::globalCategories()))
        _sounds = SoundEffects();
    _soundsStarted.clear();
    ScriptHost &host = *this;
    _machine = std::make_unique<ScriptMachine>(_script, host);
    _ms = 0;
    _triggered = false;
    return Error::success();
}

uint32_t FrontEndEffects::now() const {
    return _ms;
}

int FrontEndEffects::random(int below) {
    _random = _random * 1103515245u + 12345u;
    return below > 0 ? int((_random >> 8) % uint32_t(below)) : 0;
}

Error FrontEndEffects::loadScript(const ScriptMachine &, const std::string &name, Script &) {
    return Error::fail("the front end's script starts no others: " + name);
}

int FrontEndEffects::emitterNode(int emitter) const {
    const std::string name = emitter == kWellEmitter ? "Particle Pivot"
                             : emitter >= kFirstFx && emitter < kFirstFx + 4
                                 ? "Fx" + std::to_string(emitter - kFirstFx + 1)
                                 : std::string();
    const auto &nodes = _scene->model().model.nodes();
    for (size_t i = 0; i < nodes.size() && !name.empty(); ++i)
        if (nodes[i].name == name)
            return int(i);
    return -1;
}

HostResult FrontEndEffects::perform(ScriptMachine &, const Script::Instruction &instruction,
                                    const std::vector<int> &values) {
    if (instruction.opcode == kWait4AnimOpcode) {
        const uint32_t end = uint32_t(_scene->frames() * 1000 / Animation::kFramesPerSecond);
        return {0, end > _ms ? end - _ms : 0};
    }
    if (instruction.opcode != kEventOpcode || values.size() < 3)
        return {};
    // Its second value is the emitter the sound plays from (-1 none),
    // which Reluva doesn't place sounds by yet.
    if (values[0] == kSoundEvent && values[2] > 0) {
        _soundsStarted.push_back({_ms, uint32_t(values[2])});
        if (_mixer)
            _sounds.play(*_mixer, uint32_t(values[2]));
        return {};
    }
    if (values[0] != kParticleEvent)
        return {};
    const int node = emitterNode(values[1]);
    const auto &effects = _library.effects();
    if (node < 0 || values[2] <= 0 || size_t(values[2]) >= effects.size() || size_t(node) >= _nodes.size())
        return {};
    const gfx::Mat4 &m = _nodes[size_t(node)];
    _particles.burst(effects[size_t(values[2])], {m[12], m[13], m[14]}, _ms);
    return {};
}

void FrontEndEffects::advance(uint32_t ms, float frame) {
    if (!_machine)
        return;
    _ms = ms;
    _nodes = _scene->nodeTransforms(frame);
    if (!_triggered && ms >= kTriggerMs) {
        const auto &names = _script.variables();
        for (size_t i = 0; i < names.size(); ++i)
            if (names[i] == kTrigger)
                _machine->setVariable(i, 1);
        _triggered = true;
    }
    for (int turn = 0; turn < kMostTurns && _machine->state() != ScriptMachine::State::kEnded &&
                       _machine->state() != ScriptMachine::State::kFailed;
         ++turn) {
        if (_machine->state() == ScriptMachine::State::kWaiting && _machine->wakeTime() > _ms)
            break;
        _machine->tick();
    }
    _particles.forget(ms);
}

void FrontEndEffects::draw(Surface &screen, const soft3d::OrbitCamera &camera, uint32_t ms) const {
    _particles.draw(screen, camera, ms);
}

} // namespace reluva::tpw
