// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The maps' particle systems, and entities made by actions
// (docs/engines/stawayteam/systems/particles.md).

#include "engines/stawayteam/ui/button.h"
#include "engines/stawayteam/ui/layout.h"
#include "engines/stawayteam/ui/map_screen.h"

#include <algorithm>
#include <cmath>

namespace reluva::stawayteam {

namespace {

std::string lower(std::string s) {
    for (char &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

void MapScreen::addEmitter(size_t part, const TextObject *ai) {
    if (ai->className != "CGenericParticleSystemAI")
        return;
    Emitter e{part, ParticleSystem(*ai, uint32_t(part) * 2654435761u + uint32_t(_emitters.size())), ai};
    // A falling particle bounces by its model's Bounce Factor and Friction.
    for (size_t k = 0; k < e.system.kinds().size(); ++k) {
        ParticleSystem::Kind &kind = e.system.kind(k);
        const ModelInfo &info = modelInfo(kind.model);
        kind.bounceFactor = info.bounceFactor;
        kind.bounceFriction = info.bounceFriction;
        kind.minBounce = info.minBounce;
        kind.minBounceFriction = info.minBounceFriction;
    }
    _emitters.push_back(std::move(e));
}

void MapScreen::turnEmitters(size_t part, bool on) {
    for (Emitter &e : _emitters)
        if (e.part == part)
            e.system.turn(on);
}

void MapScreen::updateEmitters(float seconds) {
    // Each runs while its part's active and there; each burst plays one of
    // its Sounds, by the game's own random numbers or the burst's size
    // (0x524fd2).
    for (Emitter &e : _emitters) {
        const PartState &state = _partStates[e.part];
        if (!state.active || state.deleted)
            continue;
        const auto enemy = _enemyOfPart.find(e.part);
        const float angle = enemy != _enemyOfPart.end() ? _enemies[enemy->second].angle : _map.parts[e.part].angle;
        const int bursts = e.system.update(seconds, angle);
        const std::vector<std::string> &sounds = e.system.sounds();
        if (bursts > 0 && !sounds.empty() && _mixer) {
            _random = _random * 1103515245u + 12345u;
            if (auto sound = gameSound(_data, sounds[size_t(e.system.burstSound(_random >> 16))]))
                _mixer->play(sound, audio::SoundType::kEffects);
        }
    }
}

std::vector<MapScreen::ParticleDrawn> MapScreen::particlesDrawn() {
    // Each particle its model's Default Sequence's frame: played at its
    // rate over and over (Obey Sequence FPS), else once over its life, or
    // its first for one not Animated (0x51f1d0); where it is about the
    // emitter point, the map's way foreshortened, raised by its height
    // (0x524e75). It's an entity of the map (0x524e5e), its height its
    // Rendering Height (0x493a20), so it's sorted among the models as
    // they are, by where it stands.
    std::vector<ParticleDrawn> out;
    for (const Emitter &e : _emitters) {
        if (_partStates[e.part].deleted)
            continue;
        float ox = 0.0f;
        float oy = 0.0f;
        placeOf(e.part, ox, oy);
        ox += e.system.offsetX();
        oy += e.system.offsetY();
        for (const ParticleSystem::Particle &p : e.system.particles()) {
            const ParticleSystem::Kind &kind = e.system.kinds()[p.kind];
            const ModelInfo &info = modelInfo(kind.model);
            const Animation &a = animation(kind.model, info.defaultSequence.empty() ? "Idle" : info.defaultSequence);
            const int frames = a.perDirection();
            if (frames <= 0)
                continue;
            int frame = 0;
            if (kind.animated && kind.obeyFps)
                frame = int(p.age * a.fps) % frames;
            else if (kind.animated && p.life > 0.0f)
                frame = std::min(frames - 1, int(float(frames) * p.age / p.life));
            const float ground = oy + p.v * ParticleSystem::kYToX;
            const float key = (ground + float(info.sortingY) + float(a.sortingY)) * 8.0f;
            out.push_back({key, a.frame(frame, 0), int(std::lround(ox + p.u)), int(std::lround(ground - p.z))});
        }
    }
    return out;
}

size_t MapScreen::spawnEntity(const TextField &entity, float x, float y, int instigator) {
    // A CCreateCustomEntityAction's Entity, at its New Location: a part of
    // the map, its AIs running, its CActionAIs' actions at once (inferred:
    // as a model's entity put down).
    MapPart p = entity.object ? readPart(entity.value, *entity.object) : MapPart{};
    p.x = x;
    p.y = y;
    const size_t part = _map.parts.size();
    _map.parts.push_back(p);
    _partStates.push_back({p.active, p.visible, false, p.collideable});
    if (!p.name.empty())
        _byName[lower(p.name)].push_back(part);
    if (p.visible && !p.model.empty())
        _standing.push_back(part);
    for (const TextObject *ai : p.activities) {
        addTrigger(part, ai);
        addEmitter(part, ai);
        if (ai->className == "CActionAI")
            runAction(ai->object("Action"), instigator, int(part));
    }
    return part;
}

} // namespace reluva::stawayteam
