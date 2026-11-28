// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// A mission saved and loaded: Reluva's own form, not the program's (see
// docs/engines/storm3d/games/armada/systems/saved-games.md). The units
// are made again as they were, in their order, so their handles stay;
// then what they and the players are doing is put back; the scripts are
// started afresh and given their variables back through their own load
// functions, as the program does.

#include "engines/storm3d/play/mission_view.h"

#include "common/text.h"

#include <cstring>
#include <tuple>
#include <type_traits>

namespace reluva::storm3d {

namespace {

// What a save of this form says it is, and its layout's version.
constexpr uint32_t kMagic = 0x53554c52; // "RLUS"
constexpr uint32_t kVersion = 1;

// The same code writes and reads: `reading` picks which.
class Archive {
public:
    Archive(std::vector<uint8_t> &data, bool reading) : _data(data), _reading(reading) {}
    bool reading() const { return _reading; }
    bool failed() const { return _failed; }
    void bytes(void *p, size_t n) {
        if (!_reading) {
            const auto *b = static_cast<const uint8_t *>(p);
            _data.insert(_data.end(), b, b + n);
            return;
        }
        if (_failed || _at + n > _data.size()) {
            _failed = true;
            std::memset(p, 0, n);
            return;
        }
        std::memcpy(p, _data.data() + _at, n);
        _at += n;
    }

private:
    std::vector<uint8_t> &_data;
    bool _reading;
    bool _failed = false;
    size_t _at = 0;
};

} // namespace

struct Saver {
    // Plain values, and structures of them alone, as their bytes.
    template<class T>
        requires std::is_trivially_copyable_v<T>
    static void io(Archive &a, T &value) {
        a.bytes(&value, sizeof value);
    }
    static void io(Archive &a, std::string &s) {
        uint32_t n = uint32_t(s.size());
        io(a, n);
        if (a.reading())
            s.assign(a.failed() ? 0 : n, '\0');
        if (n && !s.empty())
            a.bytes(s.data(), n);
    }
    static void io(Archive &a, std::vector<bool> &v) {
        uint32_t n = uint32_t(v.size());
        io(a, n);
        if (a.reading())
            v.assign(a.failed() ? 0 : n, false);
        for (size_t i = 0; i < v.size(); ++i) {
            bool b = v[i];
            io(a, b);
            v[i] = b;
        }
    }
    template<class T>
        requires(!std::is_trivially_copyable_v<std::vector<T>>)
    static void io(Archive &a, std::vector<T> &v) {
        uint32_t n = uint32_t(v.size());
        io(a, n);
        if (a.reading())
            v.assign(a.failed() ? 0 : n, T{});
        for (T &x : v)
            io(a, x);
    }
    template<class T>
        requires(!std::is_trivially_copyable_v<std::optional<T>>)
    static void io(Archive &a, std::optional<T> &o) {
        bool has = o.has_value();
        io(a, has);
        if (a.reading() && has)
            o.emplace();
        else if (a.reading())
            o.reset();
        if (has)
            io(a, *o);
    }
    template<class K, class V>
    static void io(Archive &a, std::map<K, V> &m) {
        uint32_t n = uint32_t(m.size());
        io(a, n);
        if (!a.reading()) {
            for (auto &[key, value] : m) {
                K k = key;
                io(a, k);
                io(a, value);
            }
            return;
        }
        m.clear();
        for (uint32_t i = 0; i < n && !a.failed(); ++i) {
            K k{};
            V v{};
            io(a, k);
            io(a, v);
            m.emplace(std::move(k), std::move(v));
        }
    }
    template<class T>
    static void io(Archive &a, std::set<T> &s) {
        uint32_t n = uint32_t(s.size());
        io(a, n);
        if (!a.reading()) {
            for (const T &x : s) {
                T copy = x;
                io(a, copy);
            }
            return;
        }
        s.clear();
        for (uint32_t i = 0; i < n && !a.failed(); ++i) {
            T x{};
            io(a, x);
            s.insert(std::move(x));
        }
    }
    template<class A, class B>
        requires(!std::is_trivially_copyable_v<std::pair<A, B>>)
    static void io(Archive &a, std::pair<A, B> &p) {
        io(a, p.first);
        io(a, p.second);
    }
    template<class... T>
        requires(!std::is_trivially_copyable_v<std::tuple<T...>>)
    static void io(Archive &a, std::tuple<T...> &t) {
        std::apply([&a](auto &...x) { (io(a, x), ...); }, t);
    }

    // The structures with words or lists in them, field by field.
    static void io(Archive &a, Movement::Ship &s) {
        io(a, s.at), io(a, s.heading), io(a, s.speed), io(a, s.goal), io(a, s.route), io(a, s.waiting);
        io(a, s.avoiding);
    }
    static void io(Archive &a, Combat::Weapon &w) {
        io(a, w.range), io(a, w.delay), io(a, w.damage), io(a, w.variance), io(a, w.speed), io(a, w.sprite);
        io(a, w.hitChance), io(a, w.shooterModifier), io(a, w.targetModifier), io(a, w.shieldCrew);
        io(a, w.hullCrew), io(a, w.threshold), io(a, w.ready);
    }
    static void io(Archive &a, Combat::Special &s) {
        io(a, s.weapon), io(a, s.kind), io(a, s.ordnance), io(a, s.button), io(a, s.research), io(a, s.model);
        io(a, s.sprite), io(a, s.range), io(a, s.delay), io(a, s.energy), io(a, s.damage), io(a, s.variance);
        io(a, s.speed), io(a, s.accel), io(a, s.detect), io(a, s.life), io(a, s.needTarget), io(a, s.radius);
        io(a, s.timer), io(a, s.modifier), io(a, s.maxTargets), io(a, s.crew), io(a, s.ready), io(a, s.values);
    }
    static void io(Archive &a, Combat::Shot &s) {
        io(a, s.target), io(a, s.at), io(a, s.speed), io(a, s.damage), io(a, s.sprite), io(a, s.from);
        io(a, s.slot), io(a, s.crew), io(a, s.miss), io(a, s.shooter);
    }
    static void io(Archive &a, MissionView::Order &o) {
        io(a, o.item), io(a, o.time), io(a, o.elapsed), io(a, o.dilithium), io(a, o.crew), io(a, o.officers);
    }
    static void io(Archive &a, MissionView::Site &s) {
        io(a, s.builder), io(a, s.at), io(a, s.order), io(a, s.started);
    }
    static void io(Archive &a, MissionView::Mine &m) {
        io(a, m.at), io(a, m.slot), io(a, m.special), io(a, m.left), io(a, m.speed);
    }
    static void io(Archive &a, MissionView::Hold &h) {
        io(a, h.unit), io(a, h.target), io(a, h.until), io(a, h.cost), io(a, h.range), io(a, h.kind);
        io(a, h.rate), io(a, h.offset), io(a, h.pull), io(a, h.boardAt), io(a, h.boardEvery), io(a, h.boardSize);
    }
    static void io(Archive &a, MissionView::Field &f) {
        io(a, f.kind), io(a, f.at), io(a, f.slot), io(a, f.began), io(a, f.until), io(a, f.radius);
        io(a, f.grow), io(a, f.damage), io(a, f.hit), io(a, f.size), io(a, f.shape), io(a, f.variance);
        io(a, f.crew);
    }
    static void io(Archive &a, MissionView::Player &p) {
        io(a, p.slot), io(a, p.race), io(a, p.colour), io(a, p.human), io(a, p.stances), io(a, p.team);
        io(a, p.name), io(a, p.side);
    }
    static void io(Archive &a, MissionView::OverlayText &t) {
        io(a, t.area), io(a, t.text), io(a, t.colour), io(a, t.title), io(a, t.fade), io(a, t.shown);
    }
    static void io(Archive &a, MissionView::SpecialFlag &f) {
        io(a, f.name), io(a, f.holder), io(a, f.restrictions), io(a, f.names), io(a, f.handles);
    }
    static void io(Archive &a, MissionView::Wormhole &w) {
        io(a, w.unit), io(a, w.object), io(a, w.partner), io(a, w.state), io(a, w.scale), io(a, w.closedScale);
        io(a, w.openScale), io(a, w.rate), io(a, w.speedRange), io(a, w.maxSpeed), io(a, w.openSound);
        io(a, w.closeSound), io(a, w.team), io(a, w.gone);
    }
    static void io(Archive &a, ComputerPlayer::Element &e) { io(a, e.type), io(a, e.count), io(a, e.base); }
    static void io(Archive &a, MissionView::Computer &c) {
        io(a, c.slot), io(a, c.race), io(a, c.stances), io(a, c.stance), io(a, c.list), io(a, c.bank);
        io(a, c.crewCarry), io(a, c.think), io(a, c.plan), io(a, c.personality), io(a, c.explored);
        io(a, c.bases), io(a, c.baseMoons), io(a, c.lastBase), io(a, c.offLimits);
    }

    // The units as they were made: what, whose, where.
    struct Made {
        std::string odf;
        int slot = 0;
        std::optional<gfx::Vec3> colour;
        gfx::Vec3 at{};
        float heading = 0;
    };
    static void io(Archive &a, Made &m) { io(a, m.odf), io(a, m.slot), io(a, m.colour), io(a, m.at), io(a, m.heading); }

    // What changes in play, in one order for writing and reading. Left
    // out: what's only drawn (shots' glows, explosions, debris), and the
    // interface's state but the camera and the selection.
    static void state(Archive &a, MissionView &v) {
        io(a, v._last), io(a, v._overview), io(a, v._resources), io(a, v._crewCarry), io(a, v._selection);
        io(a, v._queues), io(a, v._footprints), io(a, v._freighters), io(a, v._sites);
        io(a, v._physics), io(a, v._ships), io(a, v._vitals), io(a, v._weapons), io(a, v._targets);
        io(a, v._specials), io(a, v._researched), io(a, v._podsAt), io(a, v._mines), io(a, v._cloaked);
        io(a, v._cloakedWith), io(a, v._revealedUntil), io(a, v._selfDestructAt), io(a, v._effects);
        io(a, v._fields), io(a, v._running), io(a, v._expiresAt), io(a, v._takenOver), io(a, v._spies);
        io(a, v._transportTo), io(a, v._nextParty), io(a, v._parties), io(a, v._repairingAt);
        io(a, v._recrewing), io(a, v._poolCarry), io(a, v._holds), io(a, v._chasing), io(a, v._dead);
        io(a, v._shots), io(a, v._players), io(a, v._tallies), io(a, v._surrendered), io(a, v._settledAt);
        io(a, v._scripted), io(a, v._picked), io(a, v._alerts), io(a, v._avoiding), io(a, v._markers);
        io(a, v._flags), io(a, v._nextFlag), io(a, v._relations), io(a, v._allyAttacks), io(a, v._perceived);
        io(a, v._hitAt), io(a, v._explodedAt), io(a, v._patrols), io(a, v._teamsOff), io(a, v._scriptPasses);
        io(a, v._paused), io(a, v._cannotDie), io(a, v._missionEnd), io(a, v._overlayTexts);
        io(a, v._nextOverlayText), io(a, v._objectives), io(a, v._objectivesShown), io(a, v._gridShown);
        io(a, v._warpSpeeds), io(a, v._roll), io(a, v._conduits), io(a, v._jumps), io(a, v._wormholes);
        io(a, v._waitingWormhole), io(a, v._boundFor), io(a, v._passages), io(a, v._decommissioning);
        io(a, v._billboards), io(a, v._stanceClock), io(a, v._computers), io(a, v._mapObjects);
        io(a, v._nextVoice), io(a, v._jukebox), io(a, v._lastTrack), io(a, v._trackRoll);
    }
};

Error MissionView::save(std::vector<uint8_t> &data) {
    if (!canSave())
        return Error::fail("a mission can be saved only in a campaign, out of a cut scene");
    data.clear();
    Archive a(data, false);
    uint32_t magic = kMagic;
    uint32_t version = kVersion;
    Saver::io(a, magic);
    Saver::io(a, version);
    Saver::io(a, _map);
    Saver::io(a, _slot);
    std::vector<Saver::Made> made;
    for (const MapScene::Unit &u : _scene.units())
        made.push_back({u.odf, u.slot, u.colour, u.at, u.heading});
    Saver::io(a, made);
    Saver::state(a, *this);
    std::vector<uint8_t> values;
    if (Error e = _missionScripts->saveValues(values))
        return e;
    Saver::io(a, values);
    float clock = _missionScripts->clock();
    Saver::io(a, clock);
    return Error::success();
}

Error MissionView::load(gfx::Renderer3D *renderer, const std::vector<uint8_t> &data, uint64_t millis) {
    std::vector<uint8_t> copy = data;
    Archive a(copy, true);
    uint32_t magic = 0;
    uint32_t version = 0;
    Saver::io(a, magic);
    Saver::io(a, version);
    if (a.failed() || magic != kMagic || version != kVersion)
        return Error::fail("not a saved game of Reluva's, or one of another version");
    std::string map;
    int slot = 1;
    std::vector<Saver::Made> made;
    Saver::io(a, map);
    Saver::io(a, slot);
    Saver::io(a, made);
    if (a.failed())
        return Error::fail("the saved game is cut short");
    // The mission started afresh, its scripts with it; then the units made
    // since, in their order, so each keeps its handle.
    if (Error e = start(renderer, map, slot, -1, millis))
        return e;
    if (_scene.units().size() > made.size())
        return Error::fail("the saved game has fewer units than its map");
    for (size_t i = _scene.units().size(); i < made.size(); ++i) {
        MapScene::Unit owner;
        owner.slot = made[i].slot;
        owner.colour = made[i].colour;
        auto unit = addShip(made[i].odf, made[i].at, made[i].heading, owner);
        if (!unit || *unit != i)
            return Error::fail("the saved game's " + made[i].odf + " can't be made again");
    }
    Saver::state(a, *this);
    std::vector<uint8_t> values;
    float clock = 0;
    Saver::io(a, values);
    Saver::io(a, clock);
    if (a.failed())
        return Error::fail("the saved game is cut short");
    // The scene as they are: whose, where, gone, cloaked, a gate's size;
    // the map's objects where they were moved to.
    for (size_t i = 0; i < made.size(); ++i) {
        if (_scene.units()[i].slot != made[i].slot || _scene.units()[i].colour != made[i].colour)
            _scene.setOwner(i, made[i].slot, made[i].colour);
        if (_dead[i]) {
            _scene.hideUnit(i);
            continue;
        }
        const gfx::Vec3 at = i < _ships.size() ? _ships[i].at : made[i].at;
        _scene.moveUnit(i, at, i < _ships.size() ? _ships[i].heading : made[i].heading);
        if (_cloaked[i])
            _scene.setCloaked(i, !_revealedUntil.count(i));
    }
    for (const Wormhole &w : _wormholes)
        if (w.unit && !w.gone)
            _scene.scaleUnit(*w.unit, w.scale);
    for (size_t k = 0; k < _mapObjects.size() && k < _scene.map().objects.size(); ++k)
        if (_scene.map().objects[k].position != _mapObjects[k].second)
            _scene.moveObject(k, _mapObjects[k].second);
    _mapIcons = mapIconsOf(_scene.map(), odfLoader());
    if (Error e = _missionScripts->loadValues(values, clock))
        return e;
    if (_jukebox)
        playMusic();
    // The game's clock carried on from where it was saved.
    _millisOffset = int64_t(_last) - int64_t(millis);
    return Error::success();
}

} // namespace reluva::storm3d
