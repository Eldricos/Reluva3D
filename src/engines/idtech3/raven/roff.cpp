// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/raven/roff.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace reluva::idtech3::raven {

namespace {

constexpr int32_t kVersion1 = 1;
constexpr int32_t kVersion2 = 2;
constexpr int32_t kVersion1FrameMs = 100; // version 1's are sampled at 10 a second
constexpr int32_t kStationary = 0;
constexpr int32_t kLinear = 2;
constexpr float kPi = 3.14159265358979f;

int32_t intAt(const std::vector<uint8_t> &d, size_t at) {
    int32_t v = 0;
    std::memcpy(&v, d.data() + at, 4);
    return v;
}

float floatAt(const std::vector<uint8_t> &d, size_t at) {
    float v = 0;
    std::memcpy(&v, d.data() + at, 4);
    return v;
}

// The exporter can write a turn the long way round.
void fixTurn(std::array<float, 3> &turn) {
    for (float &t : turn) {
        if (t > 180)
            t -= 360;
        else if (t < -180)
            t += 360;
    }
}

} // namespace

bool parseRoff(const std::vector<uint8_t> &d, RoffFile &out) {
    out = {};
    // The original doesn't check the "ROFF" at the start (its test is the
    // wrong way round), only the version and the count.
    if (d.size() < 12)
        return false;
    const int32_t version = intAt(d, 4);
    if (version == kVersion1) {
        const float count = floatAt(d, 8);
        if (!(count > 0) || d.size() < 12 + size_t(count) * 24)
            return false;
        out.frameMs = kVersion1FrameMs;
        for (size_t i = 0; i < size_t(count); ++i) {
            RoffFile::Frame f;
            for (size_t k = 0; k < 3; ++k) {
                f.move[k] = floatAt(d, 12 + i * 24 + 4 * k);
                f.turn[k] = floatAt(d, 24 + i * 24 + 4 * k);
            }
            fixTurn(f.turn);
            out.frames.push_back(std::move(f));
        }
        return true;
    }
    if (version != kVersion2 || d.size() < 20)
        return false;
    const int32_t count = intAt(d, 8);
    const int32_t frameMs = intAt(d, 12); // named a frame rate, used as each frame's ms
    const int32_t noteCount = intAt(d, 16);
    if (count <= 0 || frameMs <= 0 || noteCount < 0 || d.size() < 20 + size_t(count) * 32)
        return false;
    out.frameMs = frameMs;
    std::vector<std::pair<int32_t, int32_t>> noteRanges;
    for (size_t i = 0; i < size_t(count); ++i) {
        const size_t at = 20 + i * 32;
        RoffFile::Frame f;
        for (size_t k = 0; k < 3; ++k) {
            f.move[k] = floatAt(d, at + 4 * k);
            f.turn[k] = floatAt(d, at + 12 + 4 * k);
        }
        fixTurn(f.turn);
        noteRanges.emplace_back(intAt(d, at + 24), intAt(d, at + 28));
        out.frames.push_back(std::move(f));
    }
    // The notes: strings ending in 0 after the frames, which frames name
    // by their first and how many.
    std::vector<std::string> notes;
    size_t at = 20 + size_t(count) * 32;
    for (int32_t n = 0; n < noteCount && at < d.size(); ++n) {
        const auto end = std::find(d.begin() + std::ptrdiff_t(at), d.end(), uint8_t(0));
        notes.emplace_back(d.begin() + std::ptrdiff_t(at), end);
        at = size_t(end - d.begin()) + 1;
    }
    for (size_t i = 0; i < out.frames.size(); ++i) {
        const auto [first, many] = noteRanges[i];
        for (int32_t n = 0; first >= 0 && n < many; ++n)
            if (size_t(first + n) < notes.size())
                out.frames[i].notes.push_back(notes[size_t(first + n)]);
    }
    return true;
}

int32_t RoffSystem::cache(const std::string &file, bool client) {
    int32_t id = 0;
    for (const auto &[n, c] : _roffs)
        if (c.name == file)
            id = n;
    if (!id) {
        std::vector<uint8_t> data;
        Cached c;
        if (_files.read(file, data) || !parseRoff(data, c.file))
            return 0;
        c.name = file;
        id = ++_nextId;
        _roffs[id] = std::move(c);
    }
    (client ? _roffs[id].client : _roffs[id].server) = true;
    return id;
}

bool RoffSystem::play(int32_t entity, int32_t id, bool translate, bool client, int32_t time, const Vec3 &startAngles) {
    Playing p;
    p.entity = entity;
    p.id = id;
    p.nextTime = time;
    p.translate = translate;
    p.client = client;
    p.startAngles = startAngles;
    _playing.push_back(p);
    return true;
}

bool RoffSystem::step(Playing &p, const RoffFile &roff, int32_t time, const Side &side) {
    if (time < p.nextTime)
        return true;
    Vec3 origin{};
    Vec3 angles{};
    if (!side.current || !side.current(p.entity, origin, angles))
        return false;
    const Vec3 none{};
    if (p.frame >= roff.frames.size()) {
        // The end: still where it is.
        if (side.setTrajectories)
            side.setTrajectories(p.entity, kStationary, time, origin, none, angles, none);
        if (side.finished)
            side.finished(p.entity);
        return false;
    }
    const RoffFile::Frame &f = roff.frames[p.frame];
    // Translated: the move along the entity's own axes as it faced when it
    // started (forward, left, up), the turn as it is.
    Vec3 move = f.move;
    if (p.translate) {
        const float y = p.startAngles[1] * kPi / 180;
        const float pi = p.startAngles[0] * kPi / 180;
        const float r = p.startAngles[2] * kPi / 180;
        const float sy = std::sin(y);
        const float cy = std::cos(y);
        const float sp = std::sin(pi);
        const float cp = std::cos(pi);
        const float sr = std::sin(r);
        const float cr = std::cos(r);
        const Vec3 forward{cp * cy, cp * sy, -sp};
        const Vec3 right{-sr * sp * cy + cr * sy, -sr * sp * sy - cr * cy, -sr * cp};
        const Vec3 up{cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp};
        for (size_t k = 0; k < 3; ++k)
            move[k] = forward[k] * f.move[0] - right[k] * f.move[1] + up[k] * f.move[2];
    }
    // Moving at a frame's worth each frame time: so much a second.
    const float perSecond = 1000.0f / float(roff.frameMs);
    Vec3 moveRate{};
    Vec3 turnRate{};
    for (size_t k = 0; k < 3; ++k) {
        moveRate[k] = move[k] * perSecond;
        turnRate[k] = f.turn[k] * perSecond;
    }
    if (side.setTrajectories)
        side.setTrajectories(p.entity, kLinear, time, origin, moveRate, angles, turnRate);
    // A note is cut at control characters, each piece given on its own.
    for (const std::string &note : f.notes) {
        std::string piece;
        for (size_t i = 0; i <= note.size(); ++i) {
            if (i < note.size() && uint8_t(note[i]) >= ' ') {
                piece += note[i];
                continue;
            }
            if (!piece.empty() && side.note)
                side.note(p.entity, piece);
            piece.clear();
        }
    }
    ++p.frame;
    p.nextTime = time + roff.frameMs;
    return true;
}

void RoffSystem::update(bool client, int32_t time, const Side &side) {
    std::vector<Playing> kept;
    // Stepping may run the game's code (a note), which may play or purge:
    // the list is taken, and what it added kept.
    std::vector<Playing> list = std::move(_playing);
    _playing.clear();
    for (Playing &p : list) {
        if (p.client != client) {
            kept.push_back(p);
            continue;
        }
        const auto it = _roffs.find(p.id);
        if (it != _roffs.end() && step(p, it->second.file, time, side)) {
            kept.push_back(p);
        } else if (it == _roffs.end()) {
            // An unknown ROFF: stopped where it is.
            Vec3 origin{};
            Vec3 angles{};
            if (side.current && side.setTrajectories && side.current(p.entity, origin, angles))
                side.setTrajectories(p.entity, kStationary, time, origin, {}, angles, {});
        }
    }
    kept.insert(kept.end(), _playing.begin(), _playing.end());
    _playing = std::move(kept);
}

bool RoffSystem::purge(int32_t entity, bool client, int32_t time, const Side &side) {
    const auto it = std::find_if(_playing.begin(), _playing.end(),
                                 [&](const Playing &p) { return p.client == client && p.entity == entity; });
    if (it == _playing.end())
        return false;
    Vec3 origin{};
    Vec3 angles{};
    if (side.current && side.setTrajectories && side.current(entity, origin, angles))
        side.setTrajectories(entity, kStationary, time, origin, {}, angles, {});
    _playing.erase(it);
    return true;
}

void RoffSystem::clean(bool client) {
    for (auto it = _roffs.begin(); it != _roffs.end();) {
        (client ? it->second.client : it->second.server) = false;
        if (!it->second.client && !it->second.server)
            it = _roffs.erase(it);
        else
            ++it;
    }
    std::erase_if(_playing, [&](const Playing &p) { return p.client == client; });
}

} // namespace reluva::idtech3::raven
