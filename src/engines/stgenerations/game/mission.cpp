// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/game/mission.h"

#include <algorithm>
#include <cmath>

#include "engines/stgenerations/debug.h"
#include "common/serializer.h"
#include "engines/stgenerations/formats/opcodes.h"

namespace reluva::stgenerations {

namespace {

const char *const kDebugChannel = "scripts";
const double kPi = 3.14159265358979323846;
const double kFixed = 65536.0;
// Size of the second word array (F2/F3); the original's bound isn't known.
const int kArraySize = 4096;
// Most timer firings handled for one timer in one update, after a long pause.
const int kMaxTimerCatchUp = 4;

double fromFixed(int32_t v) {
    return v / kFixed;
}

int32_t toFixed(double v) {
    return (int32_t)std::lround(v * kFixed);
}

// Whether a record whose id field is recordId belongs to script object id.
bool isObject(uint16_t recordId, uint16_t typeBits, uint16_t id) {
    return recordId != 0 && (uint16_t)(recordId | typeBits) == id;
}

} // namespace

Mission::Mission(LevelFile &level, const ScriptFile &script, LevelRenderer &renderer, Walker &walker, Actors *actors,
                 Mixer *mixer, const MissionSound::FileOpener &soundFiles)
    : _level(level), _script(script), _renderer(renderer), _walker(walker), _random(0x53544721), _actors(actors),
      _hud([this](uint16_t object, uint16_t event, const std::vector<int16_t> &args) { send(object, event, args); }) {
    // Sounds that follow a billboard (mode 2) or an actor (mode 3).
    auto locate = [this](int mode, uint16_t object, double &x, double &y, double &z) {
        Position position;
        uint16_t id = (uint16_t)(object | (mode == MissionSound::kPositionActor ? kTypeActor : kTypeBillboard));
        if (!getPosition(id, position))
            return false;
        x = position.x;
        y = position.y;
        z = position.z;
        return true;
    };
    _sound.reset(new MissionSound(script, mixer, soundFiles, locate));
    _projectiles.reset(new Projectiles(level, script.getSection(4)));
    _movers.reset(new Movers(level));
    _vm.reset(new ScriptVM(script, this));
    _array.assign(kArraySize, 0);
    for (const LevelFile::Wall &wall : level.getWalls())
        _wallTextures.push_back(wall.texture);
    for (const LevelFile::Sector &flat : level.getSectors())
        _flatTextures.push_back(flat.texture);
    for (LevelFile::Billboard &billboard : level.getBillboards()) {
        _billboardTextures.push_back(billboard.texture);
        if (billboard.flags & LevelFile::kBillboardItem)
            dropBillboard(billboard);
    }
    // Until SET_TRANSITION_COLOUR, the transitions use the palette's 0x30.
    level.getPalette().getColor(0x30, _transitionColour[0], _transitionColour[1], _transitionColour[2]);
}

Mission::~Mission() {
}

std::vector<int> Mission::getStartOrder() const {
    // The original keeps its objects in an MFC CMap with 17 buckets and the
    // default hash (key >> 4), inserting at the head of each bucket, and
    // visits buckets in order (0x4356FF).
    std::vector<int> order;
    for (int i = 0; i < _vm->getInstanceCount(); i++)
        order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [this](int a, int b) {
        uint16_t ida = _vm->getInstance(a).object->id, idb = _vm->getInstance(b).object->id;
        int ba = (ida >> 4) % 17, bb = (idb >> 4) % 17;
        return ba != bb ? ba < bb : a > b;
    });
    return order;
}

Mission::TransitionView Mission::getTransitionView() const {
    TransitionView view;
    if (!_transition.active)
        return view;
    view.active = true;
    view.kind = _transition.ending     ? TransitionView::kEnding
                : _transition.teleport ? TransitionView::kTeleport
                                       : TransitionView::kBriefing;
    view.style = _transitionStyle;
    view.r = _transitionColour[0];
    view.g = _transitionColour[1];
    view.b = _transitionColour[2];
    // 0x43E13A: each phase ends at the sum of the times before it.
    const uint32_t *t = _transition.phase;
    uint32_t e = _transition.elapsed, end1 = t[0], end2 = end1 + t[1], end3 = end2 + t[2];
    if (e < end1) {
        view.phase = 1;
        view.amount = (int)((uint64_t)e * 256 / t[0]);
    } else if (e < end2) {
        view.phase = 2;
        view.amount = (int)((uint64_t)(e - end1) * 1024 / t[1]);
    } else if (e < end3) {
        view.phase = 3;
        view.amount = 256 - (int)((uint64_t)(e - end2) * 256 / t[2]);
    }
    return view;
}

void Mission::setTextureScroll(uint16_t id, int16_t u, int16_t v) {
    for (LevelFile::Wall &wall : _level.getWalls()) {
        if (isObject(wall.objectId, kTypeWall, id)) {
            wall.uScroll = u;
            wall.vScroll = v;
        }
    }
    for (LevelFile::Sector &flat : _level.getSectors()) {
        if (isObject(flat.objectId, kTypeFlat, id)) {
            flat.uScroll = u;
            flat.vScroll = v;
        }
    }
}

void Mission::refreshGeometry(const std::vector<int> &walls) {
    if (walls.empty())
        return;
    _walker.refreshWalls(walls);
    _projectiles->refreshWalls(walls);
}

void Mission::moveLift(uint16_t id, int level) {
    // 0x4583EA: every flat of the group jumps to the level's height. The
    // player isn't carried: the car's own panel moves them with
    // TELEPORT_PLAYER_RELATIVE straight after, and the next step finds
    // their footing again.
    auto it = _lifts.find(id);
    if (it == _lifts.end() || level < 1 || level > it->second.count)
        return;
    const Lift &lift = it->second;
    int32_t height = lift.heights[level - 1];
    std::vector<LevelFile::Sector> &flats = _level.getSectors();
    for (LevelFile::Sector &flat : flats) {
        if (isObject(flat.objectId, kTypeFlat, lift.flat) && !flat.isCeiling())
            flat.height = height;
    }
}

void Mission::runPush(uint32_t ms) {
    _push.elapsed += ms;
    uint32_t t = _push.elapsed;
    if (t >= _push.end) {
        _push.active = false;
        return;
    }
    double envelope = t < _push.ramp   ? (double)t / std::max<uint32_t>(1, _push.ramp)
                      : t < _push.hold ? 1.0
                                       : (double)(_push.end - t) / std::max<uint32_t>(1, _push.end - _push.hold);
    double dx = _push.x - _walker.getX(), dy = _push.y - _walker.getY();
    double length = std::sqrt(dx * dx + dy * dy);
    if (length < 1e-6)
        return;
    double step = _push.strength * envelope * ms / 1000.0;
    _walker.move(dx / length * step, dy / length * step, 0);
}

void Mission::sync(Serializer &s) {
    _vm->sync(s);
    _level.syncState(s);
    {
        std::vector<int> moved;
        _movers->sync(s, _walker, moved);
        if (s.isLoading())
            refreshGeometry(moved);
    }
    _walker.sync(s);
    bool hasActors = _actors != nullptr;
    s.syncAsBool(hasActors);
    if (hasActors != (_actors != nullptr)) {
        s.fail();
        return;
    }
    if (_actors)
        _actors->sync(s);
    if (s.err())
        return;

    s.syncAsUint32(_time);
    uint32_t count = (uint32_t)_timers.size();
    s.syncAsUint32(count);
    if (s.isLoading())
        _timers.clear();
    auto timer = _timers.begin();
    for (uint32_t i = 0; i < count && !s.err(); i++) {
        uint16_t id = s.isSaving() ? timer->first : 0;
        Timer t = s.isSaving() ? timer->second : Timer();
        s.syncAsUint16(id);
        s.syncAsUint32(t.period);
        s.syncAsUint32(t.due);
        if (s.isLoading())
            _timers[id] = t;
        else
            ++timer;
    }
    s.syncVector(_listeners, [](Serializer &ser, uint16_t &v) { ser.syncAsUint16(v); });
    s.syncVector(_array, [](Serializer &ser, int16_t &v) { ser.syncAsSint16(v); });

    count = (uint32_t)_items.size();
    s.syncAsUint32(count);
    if (s.isLoading())
        _items.clear();
    auto item = _items.begin();
    for (uint32_t i = 0; i < count && !s.err(); i++) {
        uint16_t id = s.isSaving() ? item->first : 0;
        Item it = s.isSaving() ? item->second : Item();
        s.syncAsUint16(id);
        s.syncAsSint16(it.kind);
        s.syncAsSint16(it.b);
        s.syncAsSint16(it.c);
        s.syncAsSint32(it.charge);
        s.syncAsSint32(it.maxCharge);
        s.syncAsSint32(it.level);
        s.syncAsSint32(it.maxLevel);
        s.syncAsSint32(it.setting);
        if (s.isLoading())
            _items[id] = it;
        else
            ++item;
    }
    // Shot damages follow the weapons' power levels.
    s.syncCount((uint32_t)_projectiles->getTypeCount());
    for (int type = 0; type < _projectiles->getTypeCount() && !s.err(); type++) {
        int16_t damage = _projectiles->getType(type).damage;
        s.syncAsSint16(damage);
        if (s.isLoading())
            _projectiles->setDamage(type, damage);
    }

    _hud.sync(s);
    s.syncAsInt(_leftPanelRequest);
    s.syncAsUint16(_activeItem);
    s.syncAsBool(_finished);
    s.syncAsUint32(_objectives);
    s.syncAsUint32(_objectivesDone);
    s.syncAsBool(_accomplished);
    s.syncAsBool(_transition.active);
    s.syncAsBool(_transition.ending);
    for (uint32_t &phase : _transition.phase)
        s.syncAsUint32(phase);
    s.syncAsUint32(_transition.elapsed);
    s.syncAsInt(_transitionSound);
    s.syncAsInt(_transitionVolume);
    s.syncAsInt(_transitionStyle);
    s.syncAsDouble(_motion.speed);
    s.syncAsInt(_motion.turnRate);
    s.syncAsInt(_motion.lookRate);
    s.syncAsDouble(_motion.accel);
    s.syncAsInt(_motion.turnAccel);
    s.syncAsInt(_motion.lookAccel);
    s.syncAsDouble(_motion.jump);
    s.syncAsUint16(_controls);
    s.syncAsDouble(_fallMultiplier);
    s.syncAsDouble(_fallDivisor);
    if (s.version() == 0 || s.version() >= kLowGravitySaveVersion)
        s.syncAsBool(_lowGravity);
    else if (s.isLoading())
        _lowGravity = _walker.getGravity() == 0;
    if (s.version() == 0 || s.version() >= kLowGravitySaveVersion) {
        uint32_t count = (uint32_t)_safeFlats.size();
        s.syncAsUint32(count);
        std::vector<uint16_t> ids(_safeFlats.begin(), _safeFlats.end());
        ids.resize(count);
        for (uint16_t &id : ids)
            s.syncAsUint16(id);
        if (s.isLoading())
            _safeFlats = std::set<uint16_t>(ids.begin(), ids.end());
        uint32_t pools = (uint32_t)_pools.size();
        s.syncAsUint32(pools);
        _pools.resize(pools);
        for (Pool &pool : _pools) {
            s.syncAsUint16(pool.below);
            s.syncAsUint16(pool.above);
            s.syncAsUint16(pool.bottom);
        }
        s.syncAsInt(_waterState);
        s.syncAsInt(_passingFlat);
        if (s.isLoading()) {
            _flatMark.clear();
            _poolSurface.clear();
            for (const Pool &pool : _pools)
                setPool(pool.below, pool.above, pool.bottom);
        }
    }
    s.syncAsBool(_push.active);
    s.syncAsDouble(_push.x);
    s.syncAsDouble(_push.y);
    s.syncAsDouble(_push.strength);
    s.syncAsUint32(_push.ramp);
    s.syncAsUint32(_push.hold);
    s.syncAsUint32(_push.end);
    s.syncAsUint32(_push.elapsed);
    s.syncAsBool(_transition.teleport);
    s.syncAsBool(_transition.moved);
    s.syncAsDouble(_transition.x);
    s.syncAsDouble(_transition.y);
    s.syncAsDouble(_transition.z);
    s.syncAsSint16(_transition.heading);
    for (uint8_t &c : _transitionColour)
        s.syncAsByte(c);
    s.syncAsUint32(_environmentPeriod);
    s.syncAsUint32(_environmentElapsed);
    s.syncAsUint16(_environmentSource);
    s.syncAsUint16(_wornItem);
    s.syncAsUint16(_gaugeItem);
    s.syncAsInt(_spin);
    s.syncString(_viewOverlay.image);
    s.syncAsInt(_viewOverlay.mode);
    s.syncVector(_outcomes, [](Serializer &ser, int32_t &v) { ser.syncAsSint32(v); });
    s.syncAsUint16(_engaged);
    s.syncAsDouble(_engagedX);
    s.syncAsDouble(_engagedY);
    uint32_t lifts = (uint32_t)_lifts.size();
    s.syncAsUint32(lifts);
    if (s.isLoading())
        _lifts.clear();
    auto lift = _lifts.begin();
    for (uint32_t i = 0; i < lifts && !s.err(); i++) {
        uint16_t id = s.isSaving() ? lift->first : 0;
        Lift l = s.isSaving() ? lift->second : Lift();
        s.syncAsUint16(id);
        s.syncAsUint16(l.flat);
        s.syncAsInt(l.count);
        for (int32_t &h : l.heights)
            s.syncAsSint32(h);
        if (s.isLoading())
            _lifts[id] = l;
        else
            ++lift;
    }
    _renderer.syncLights(s);
    s.syncString(_punch.image);
    s.syncAsUint32(_punch.msPerFrame);
    s.syncAsInt(_punch.hitFrame);
    s.syncAsSint16(_punch.damage);
    s.syncAsSint16(_punch.power);
    s.syncAsInt(_punch.swingSound);
    s.syncAsInt(_punch.swingVolume);
    s.syncAsInt(_punch.hitSound);
    s.syncAsInt(_punch.hitVolume);
    s.syncAsSint16(_health);
    s.syncAsSint16(_maxHealth);
    s.syncAsInt(_touchedWall);
    s.syncAsInt(_standingFlat);
    s.syncAsInt(_billboard);
    s.syncAsUint16(_touchedActor);
    s.syncAsDouble(_currentYaw);
    uint32_t seed = _random.getSeed();
    s.syncAsUint32(seed);

    if (s.isLoading()) {
        _random.setSeed(seed ^ _time);
        _projectiles->clear();
        _actorEvents.clear();
        _soundsFinished.clear();
        _sound->stopAll();
        _hasYaw = true;
        _yaw = _currentYaw;
    }
}

void Mission::start() {
    _listeners.clear();
    for (int index : getStartOrder())
        _vm->setState(_vm->getInstance(index), 0);
    // The original broadcasts event 6 from its mission object's method
    // 0x43E742, which also marks the mission as begun. verid1 places the
    // player at the start when it arrives, so it is sent once here.
    broadcast(kEventStart);
    checkPlayerContacts();
}

void Mission::updateTransition(uint32_t ms) {
    // 0x43E13A: at its end the briefing gives way to the mission (event 7);
    // the ending one ends it.
    _transition.elapsed += ms;
    // The player moves as the crossfade starts (the original moves at once
    // but shows the old view until then).
    if (_transition.teleport && !_transition.moved && _transition.elapsed >= _transition.phase[0]) {
        _transition.moved = true;
        _walker.place(_transition.x, _transition.y, _transition.z);
        setPlayerHeading(_transition.heading);
    }
    if (_transition.elapsed >= _transition.phase[0] + _transition.phase[1] + _transition.phase[2]) {
        _transition.active = false;
        if (_transition.ending)
            _finished = true;
        else
            broadcast(kEventBriefingDone);
    }
}

void Mission::update(uint32_t ms) {
    // The original clamps its frame time the same way (0x4309C5).
    ms = std::max<uint32_t>(8, std::min<uint32_t>(ms, 1000));
    // 0x43A7BD: while a transition runs, nothing else does.
    if (_transition.active) {
        updateTransition(ms);
        return;
    }
    _time += ms;
    runTimers();

    // Doors (0x4B82E0), and what they tell their scripts.
    {
        std::vector<int> moved;
        std::vector<Movers::Notice> notices;
        _movers->update(ms, _walker, moved, notices);
        refreshGeometry(moved);
        for (const Movers::Notice &notice : notices)
            send(notice.object, notice.event);
    }
    // Landing hard, or a jump hitting the ceiling, hurts (0x4357E5,
    // 0x43A662: kind 1), unless the flat is marked safe (SET_FLAT_SAFE).
    {
        const std::vector<LevelFile::Sector> &flats = _level.getSectors();
        auto hurt = [&](double speed, int flat) {
            uint16_t id = flat >= 0 && flats[flat].objectId ? (uint16_t)(flats[flat].objectId | kTypeFlat) : 0;
            if (speed <= 0 || _fallDivisor <= 0 || (id && _safeFlats.count(id)))
                return;
            int damage = (int)(speed * _fallMultiplier / _fallDivisor);
            if (damage > 0)
                damagePlayer(id, (int16_t)damage, 1);
        };
        double landing = _walker.takeLanding();
        hurt(landing, _walker.getLandingFloor());
        int ceiling;
        double bump = _walker.takeCeilingHit(ceiling);
        hurt(bump, ceiling);
    }
    if (_push.active)
        runPush(ms);
    if (_engaged) {
        double dx = _walker.getX() - _engagedX, dy = _walker.getY() - _engagedY;
        if (dx * dx + dy * dy > 3.0 * 3.0) {
            uint16_t engaged = _engaged;
            _engaged = 0;
            send(engaged, Hud::kEventWalkedAway);
        }
    }

    // 0x43AD04: once every objective is done, event 8, once.
    if (_objectivesDone == _objectives && !_accomplished) {
        _accomplished = true;
        broadcast(kEventObjectivesDone);
    }
    // SET_ENVIRONMENT_DAMAGE (0x43B310): past the shield, no lower than 1.
    if (_environmentPeriod) {
        _environmentElapsed += ms;
        while (_environmentElapsed > _environmentPeriod) {
            _environmentElapsed -= _environmentPeriod;
            _health = (int16_t)std::max(1, _health - 1);
            broadcast(kEventPlayerDamaged, {(int16_t)_environmentSource, 1, 1, 13});
        }
    }

    if (_actors) {
        _actorEvents.clear();
        _actors->update(ms, _walker, _actorEvents);
        for (const Actors::ScriptEvent &event : _actorEvents)
            send(event.object, event.event);
        std::vector<Actors::Shot> shots;
        _actors->takeShots(shots);
        for (const Actors::Shot &shot : shots)
            fire(shot.weapon, shot.x, shot.y, shot.z, shot.yaw, shot.pitch, shot.shooter);
    }
    runProjectiles(ms);

    _hud.update(ms, _walker.getX(), _walker.getY());

    updateWater();
    checkPlayerContacts();

    MissionSound::Listener listener = {_walker.getX(), _walker.getY(), _walker.getEye(), _currentYaw};
    _soundsFinished.clear();
    _sound->update(ms, listener, _soundsFinished);
    for (const MissionSound::Finished &done : _soundsFinished)
        send(done.object, MissionSound::kEventFinished);
}

void Mission::fire(int type, double x, double y, double z, double yaw, double pitch, int shooter) {
    if (type < 0 || type >= _projectiles->getTypeCount())
        return;
    // The spawn wrapper (0x440334) sounds the shot where it starts.
    playSoundAt(_projectiles->getType(type).fireSound, x, y, z);
    _projectiles->fire(type, x, y, z, yaw, pitch, shooter);
}

void Mission::playSoundAt(int sound, double x, double y, double z) {
    if (sound < 0 || sound >= _sound->getSoundCount())
        return;
    _sound->setPosition((uint32_t)sound, x, y, z);
    _sound->play(sound);
}

void Mission::runProjectiles(uint32_t ms) {
    std::vector<Projectiles::Target> targets;
    if (_actors)
        _actors->getTargets(targets);
    std::vector<Projectiles::Report> reports;
    _projectiles->update(ms, _walker.getActiveZones(), targets, _walker.getX(), _walker.getY(), _walker.getEye(),
                         _walker.getRadius(), reports);
    for (const Projectiles::Report &report : reports) {
        const Projectiles::Hit &hit = report.hit;
        const Projectiles::Type &type = _projectiles->getType(hit.type);
        if (report.what == Projectiles::Report::kFired) {
            playSoundAt(type.fireSound, hit.x, hit.y, hit.z);
            continue;
        }
        // 0x43696F: the impact sounds where it hit; the player takes the
        // damage, anything else is told (16) and everyone hears of it (59).
        playSoundAt(type.impactSound, hit.x, hit.y, hit.z);
        uint16_t id = 0;
        switch (hit.target) {
        case Projectiles::Hit::kPlayer:
            damagePlayer(0, type.damage, type.team);
            continue;
        case Projectiles::Hit::kWall:
            id = _level.getWalls()[hit.index].objectId ? _level.getWalls()[hit.index].objectId | kTypeWall : 0;
            break;
        case Projectiles::Hit::kBillboard:
            id = _level.getBillboards()[hit.index].objectId
                     ? _level.getBillboards()[hit.index].objectId | kTypeBillboard
                     : 0;
            break;
        case Projectiles::Hit::kActor:
            id = (uint16_t)hit.index;
            break;
        default:
            break;
        }
        if (!id)
            continue;
        broadcast(kEventHitReport, {(int16_t)id, type.damage, type.damage, type.team});
        send(id, kEventHit, {type.damage, type.damage, type.team});
    }
}

void Mission::swing() {
    if (_punch.swingSound < 0)
        return;
    if (_punch.swingVolume >= 0)
        _sound->playSound(_punch.swingSound, _punch.swingVolume);
    else
        _sound->play(_punch.swingSound);
}

void Mission::punch(const LevelRenderer::Pick &pick) {
    // 0x4364B8: within reach (the player's radius and half a unit, plus
    // the radius of a billboard or actor), what was hit gets event 16 and
    // everyone hears of it (59); out of reach, everyone hears of a miss.
    const int16_t kTeam = 2;
    double reach = _walker.getRadius() + 0.5;
    uint16_t id = 0;
    switch (pick.kind) {
    case LevelRenderer::Pick::kNone:
        return;
    case LevelRenderer::Pick::kWall:
        id = _level.getWalls()[pick.index].objectId | kTypeWall;
        break;
    case LevelRenderer::Pick::kFlat:
        id = _level.getSectors()[pick.index].objectId | kTypeFlat;
        break;
    case LevelRenderer::Pick::kBillboard:
        id = _level.getBillboards()[pick.index].objectId | kTypeBillboard;
        reach += fromFixed(_level.getBillboards()[pick.index].radius);
        break;
    case LevelRenderer::Pick::kSprite:
        if (!pick.object || !_actors)
            return;
        id = pick.object;
        reach += _actors->getRadius(id);
        break;
    }
    bool inReach = pick.depth <= reach;
    broadcast(kEventHitReport, {(int16_t)(inReach ? id : 0), _punch.damage, _punch.power, kTeam});
    if (!inReach)
        return;
    send(id, kEventHit, {_punch.damage, _punch.power, kTeam});
    if (_punch.hitSound >= 0) {
        if (_punch.hitVolume >= 0)
            _sound->playSound(_punch.hitSound, _punch.hitVolume);
        else
            _sound->play(_punch.hitSound);
    }
}

Mission::FireResult Mission::fireWeapon(const Camera &camera, const LevelRenderer::Pick &pick, int x, int y, int w,
                                        int h, double ahead, double up, double left) {
    auto it = _items.find(_activeItem);
    if (it == _items.end())
        return kNoWeapon;
    Item &item = it->second;
    if (!isWeapon(_activeItem) || item.c < 0 || item.c >= _projectiles->getTypeCount())
        return kNoWeapon;
    // Each shot costs its damage in charge (vt38, 0x449F20); without
    // enough, the trigger does nothing.
    int16_t cost = std::max<int16_t>(1, _projectiles->getType(item.c).damage);
    if (item.charge < cost)
        return kEmpty;
    item.charge -= cost;
    broadcast(kEventItemUsed, {cost});

    // 0x4B4E10: aim at what shows under the cursor, or at a point far
    // ahead if that is nothing or too close.
    double depth = pick.kind != LevelRenderer::Pick::kNone ? pick.depth : 0;
    if (depth <= _walker.getRadius())
        depth = w / 2.0;
    double tx, ty, tz;
    LevelRenderer::unproject(camera, w, h, x, y, depth, tx, ty, tz);
    double c = std::cos(camera.yaw), s = std::sin(camera.yaw);
    double ox = camera.x + c * ahead - s * left, oy = camera.y + s * ahead + c * left, oz = camera.z + up;
    double dx = tx - ox, dy = ty - oy;
    fire(item.c, ox, oy, oz, std::atan2(dy, dx), std::atan2(tz - oz, std::sqrt(dx * dx + dy * dy)),
         Projectiles::kShooterPlayer);
    return kFired;
}

void Mission::setItemLevel(int level) {
    // 0x44A03A: the level is the cost of a shot and the damage it does, for
    // every item firing the same kind of shot.
    auto it = _items.find(_activeItem);
    if (it == _items.end() || it->second.maxLevel <= 0)
        return;
    Item &item = it->second;
    item.level = std::max(1, std::min<int>(level, item.maxLevel));
    if (item.c >= 0 && item.c < _projectiles->getTypeCount())
        _projectiles->setDamage(item.c, (int16_t)item.level);
}

void Mission::useItem(int slot) {
    // 0x44789E: an item the player can hold becomes the one in use; then
    // the item hears it was used.
    if (slot < 0 || slot >= Hud::kSlots || !_hud.getSlot(slot))
        return;
    uint16_t id = _hud.getSlot(slot);
    if (isWeapon(id))
        _activeItem = id;
    send(id, kEventActivate);
}

void Mission::dropItem(int slot) {
    // 0x44763A: out of the inventory; the item's script puts it somewhere.
    if (slot < 0 || slot >= Hud::kSlots || !_hud.getSlot(slot))
        return;
    uint16_t id = _hud.getSlot(slot);
    _hud.removeItem(id);
    if (_activeItem == id)
        _activeItem = 0;
    send(id, Hud::kEventDropped);
}

void Mission::describeItem(int slot) {
    if (slot >= 0 && slot < Hud::kSlots && _hud.getSlot(slot))
        send(_hud.getSlot(slot), Hud::kEventDescribe);
}

bool Mission::isWeapon(uint16_t id) const {
    auto it = _items.find(id);
    if (it == _items.end())
        return false;
    int16_t kind = it->second.kind;
    return (kind >= 2 && kind <= 6) || (kind >= 11 && kind <= 13);
}

int Mission::getItemIcon(uint16_t id) const {
    auto it = _items.find(id);
    return it == _items.end() ? -1 : it->second.b;
}

void Mission::damagePlayer(int16_t source, int16_t damage, int16_t kind) {
    // 0x43A42B: health never drops below 1 here; scripts decide when the
    // player has died (verid1 at health 1).
    if (damage == 0)
        return;
    // A worn shield (SET_WORN_ITEM, kind 9) with charge soaks the whole
    // hit (0x44B182), except damage kind 12; empty, it hears event 48.
    auto worn = _items.find(_wornItem);
    if (_wornItem && kind != 12 && worn != _items.end() && worn->second.charge > 0) {
        worn->second.charge = std::max<int32_t>(0, worn->second.charge - damage);
        if (worn->second.charge == 0)
            send(_wornItem, kEventShieldEmpty);
        return;
    }
    _health = (int16_t)std::max(1, std::min<int>(_maxHealth, _health - damage));
    broadcast(kEventPlayerDamaged, {source, damage, damage, kind});
}

bool Mission::takePlayerYaw(double &yaw) {
    if (!_hasYaw)
        return false;
    yaw = _yaw;
    _hasYaw = false;
    return true;
}

int16_t Mission::send(uint16_t id, uint16_t event, const std::vector<int16_t> &args) {
    int index = _vm->findInstance(id);
    if (index < 0)
        return 0;
    ScriptInstance &target = _vm->getInstance(index);
    debugC(2, kDebugChannel, "event %u (%d) to %04X in state %d%s", event, args.empty() ? 0 : args[0], id, target.state,
           target.enabled ? "" : ", disabled");
    return _vm->sendEvent(target, event, args);
}

void Mission::broadcast(uint16_t event, const std::vector<int16_t> &args) {
    // Copied: handlers may subscribe or unsubscribe.
    std::vector<uint16_t> listeners = _listeners;
    for (uint16_t id : listeners)
        send(id, event, args);
}

void Mission::setTimer(uint16_t id, uint32_t ms) {
    Timer timer;
    timer.period = std::max<uint32_t>(ms, 1);
    timer.due = _time + timer.period;
    _timers[id] = timer;
}

void Mission::runTimers() {
    for (int round = 0; round < kMaxTimerCatchUp; round++) {
        std::vector<std::pair<uint32_t, uint16_t>> due;
        for (const auto &timer : _timers) {
            if (timer.second.due <= _time)
                due.push_back({timer.second.due, timer.first});
        }
        if (due.empty())
            return;
        std::sort(due.begin(), due.end());
        for (const auto &entry : due) {
            auto it = _timers.find(entry.second);
            if (it == _timers.end() || it->second.due > _time)
                continue;
            // Periodic until killed, as with the original's Windows timers.
            it->second.due += it->second.period;
            if (_vm->findInstance(entry.second) < 0) {
                _timers.erase(it);
                continue;
            }
            send(entry.second, kEventTimer);
        }
    }
    // Still behind (a long pause): drop the missed firings.
    for (auto &timer : _timers) {
        if (timer.second.due <= _time)
            timer.second.due = _time + timer.second.period;
    }
}

void Mission::checkPlayerContacts() {
    // The engine reports only changes (0x4B46A0, 0x435CD8): the old thing
    // gets "untouch" and the new one "touch".
    int wall = _walker.getContactWall();
    if (wall != _touchedWall) {
        if (_touchedWall >= 0)
            send((uint16_t)(_level.getWalls()[_touchedWall].objectId | kTypeWall), kEventUntouch);
        _touchedWall = wall;
        if (wall >= 0) {
            send((uint16_t)(_level.getWalls()[wall].objectId | kTypeWall), kEventTouch);
            // Walking into a wall may start a door (0x4B94D0).
            std::vector<Movers::Notice> notices;
            _movers->bump(wall, notices);
            for (const Movers::Notice &notice : notices)
                send(notice.object, notice.event);
        }
    }

    uint16_t actor = _actors ? _actors->getObstacleObject(_walker.getContactObstacle()) : 0;
    if (actor != _touchedActor) {
        if (_touchedActor)
            send(_touchedActor, kEventUntouch);
        _touchedActor = actor;
        if (actor)
            send(actor, kEventTouch);
    }

    int flat = _walker.getFloor();
    if (flat != _standingFlat) {
        if (_standingFlat >= 0) {
            uint16_t id = (uint16_t)(_level.getSectors()[_standingFlat].objectId | kTypeFlat);
            send(id, kEventUntouch);
            send(id, kEventFloorLeft);
        }
        _standingFlat = flat;
        if (flat >= 0) {
            uint16_t id = (uint16_t)(_level.getSectors()[flat].objectId | kTypeFlat);
            send(id, kEventFloorEntered);
            send(id, kEventTouch);
        }
    }
    // A pass-through floor at the player's feet still hears it, as the
    // original finds it first and steps through it (0x4B4904).
    int passing = findPassThroughFloor();
    if (passing != _passingFlat) {
        if (_passingFlat >= 0) {
            uint16_t id = (uint16_t)(_level.getSectors()[_passingFlat].objectId | kTypeFlat);
            send(id, kEventUntouch);
            send(id, kEventFloorLeft);
        }
        _passingFlat = passing;
        if (passing >= 0) {
            uint16_t id = (uint16_t)(_level.getSectors()[passing].objectId | kTypeFlat);
            send(id, kEventFloorEntered);
            send(id, kEventTouch);
        }
    }

    // Billboards fire when the player enters the box around them (0x4B9940).
    int inside = -1;
    const std::vector<LevelFile::Billboard> &billboards = _level.getBillboards();
    double feet = _walker.getFeet(), head = _walker.getEye() + Walker::kHeadroom;
    for (size_t i = 0; i < billboards.size() && inside < 0; i++) {
        const LevelFile::Billboard &b = billboards[i];
        if (b.radius <= 0 || b.hidden || !(b.zones & _walker.getActiveZones()))
            continue;
        double r = fromFixed(b.radius), z = fromFixed(b.z);
        if (std::fabs(_walker.getX() - fromFixed(b.x)) <= r && std::fabs(_walker.getY() - fromFixed(b.y)) <= r &&
            z <= head && z + fromFixed(b.boxHeight) >= feet + Walker::kStepHeight)
            inside = (int)i;
    }
    if (inside != _billboard) {
        if (_billboard >= 0)
            send((uint16_t)(billboards[_billboard].objectId | kTypeBillboard), kEventUntouch);
        _billboard = inside;
        if (inside >= 0)
            send((uint16_t)(billboards[inside].objectId | kTypeBillboard), kEventTouch);
    }
}

double Mission::pop32(ScriptVM &vm) {
    uint16_t high = (uint16_t)vm.pop();
    uint16_t low = (uint16_t)vm.pop();
    return fromFixed((int32_t)((uint32_t)high << 16 | low));
}

uint32_t Mission::popRaw32(ScriptVM &vm) {
    uint16_t high = (uint16_t)vm.pop();
    uint16_t low = (uint16_t)vm.pop();
    return (uint32_t)high << 16 | low;
}

void Mission::push32(ScriptVM &vm, double value) {
    uint32_t v = (uint32_t)toFixed(value);
    vm.push((int16_t)(v & 0xFFFF));
    vm.push((int16_t)(v >> 16));
}

Mission::Position Mission::popPosition(ScriptVM &vm) {
    Position position;
    position.x = pop32(vm);
    position.y = pop32(vm);
    position.z = pop32(vm);
    position.heading = vm.pop();
    return position;
}

void Mission::pushPosition(ScriptVM &vm, const Position &position) {
    vm.push(position.heading);
    push32(vm, position.z);
    push32(vm, position.y);
    push32(vm, position.x);
}

bool Mission::getPosition(uint16_t id, Position &position) const {
    if (id == 0) {
        position.x = _walker.getX();
        position.y = _walker.getY();
        position.z = _walker.getFeet();
        double degrees = std::fmod(90 - _currentYaw * 180 / kPi, 360.0);
        position.heading = (int16_t)std::lround(degrees < 0 ? degrees + 360 : degrees);
        return true;
    }
    uint16_t heading;
    if (_actors && _actors->getPosition(id, position.x, position.y, position.z, heading)) {
        position.heading = (int16_t)(heading * 360 / 65536);
        return true;
    }
    for (const LevelFile::Billboard &b : _level.getBillboards()) {
        if (isObject(b.objectId, kTypeBillboard, id)) {
            position = {fromFixed(b.x), fromFixed(b.y), fromFixed(b.z), 0};
            return true;
        }
    }
    return false;
}

void Mission::setPosition(uint16_t id, const Position &position) {
    if (_actors && _actors->isActor(id))
        _actors->setPosition(id, position.x, position.y, position.z, (uint16_t)(position.heading * 65536 / 360));
    // Billboards ignore the position (0x4586D0): items just settle onto the
    // floor again.
    for (LevelFile::Billboard &b : _level.getBillboards()) {
        if (isObject(b.objectId, kTypeBillboard, id) && (b.flags & LevelFile::kBillboardItem))
            dropBillboard(b);
    }
}

void Mission::dropAt(uint16_t id, const Position &position) {
    if (_actors && _actors->isActor(id))
        _actors->setPosition(id, position.x, position.y, position.z, (uint16_t)(position.heading * 65536 / 360));
    for (LevelFile::Billboard &b : _level.getBillboards()) {
        if (isObject(b.objectId, kTypeBillboard, id)) {
            b.x = toFixed(position.x);
            b.y = toFixed(position.y);
            b.z = toFixed(position.z);
            dropBillboard(b);
        }
    }
}

int Mission::findPassThroughFloor() const {
    // The highest one over the player, between the feet and a step above.
    const std::vector<LevelFile::Sector> &flats = _level.getSectors();
    double feet = _walker.getFeet(), top = feet + _walker.getStepHeight();
    int best = -1;
    for (size_t i = 0; i < flats.size(); i++) {
        const LevelFile::Sector &flat = flats[i];
        if (flat.isCeiling() || !(flat.flags & LevelFile::kFlagPassThrough) || !flat.objectId)
            continue;
        double h = fromFixed(flat.height);
        if (h < feet - 0.01 || h > top || (best >= 0 && h <= fromFixed(flats[best].height)))
            continue;
        if (_walker.flatContains((int)i, _walker.getX(), _walker.getY()))
            best = (int)i;
    }
    return best;
}

void Mission::setPool(uint16_t surfaceBelow, uint16_t surfaceAbove, uint16_t bottom) {
    // SET_FLAT_GROUPS (0x440D3D): the water's surface seen from below
    // (mark 2), from above (mark 1; a pass-through floor) and the pool's
    // bottom (mark 3, knowing its surface).
    std::vector<LevelFile::Sector> &flats = _level.getSectors();
    _flatMark.resize(flats.size(), 0);
    _poolSurface.resize(flats.size(), 0);
    for (size_t i = 0; i < flats.size(); i++) {
        // The flats' own ids (+0x14), as 0x4B7600 looks them up.
        uint16_t id = flats[i].objectId & 0x1FFF;
        if (!id)
            continue;
        if (id == (surfaceAbove & 0x1FFF)) {
            _flatMark[i] = 1;
        } else if (id == (surfaceBelow & 0x1FFF)) {
            _flatMark[i] = 2;
        } else if (id == (bottom & 0x1FFF)) {
            _flatMark[i] = 3;
            _poolSurface[i] = surfaceAbove & 0x1FFF;
        }
    }
}

void Mission::updateWater() {
    // 0x43D0C9: on a pool's bottom, the eye's depth under the surface says
    // whether the player is out of the water (0), wading or afloat (1) or
    // under it (2). Each change is broadcast (58, 57, 56), and gravity
    // follows: with control 0x40, buoyancy at the surface and none under
    // it; without, half gravity under it.
    if (_flatMark.empty())
        return;
    const std::vector<LevelFile::Sector> &flats = _level.getSectors();
    int floor = _walker.getFloor();
    int mark = floor >= 0 && floor < (int)_flatMark.size() ? _flatMark[floor] : 0;
    int state = 0;
    double depth = 0;
    _waterZones = 0;
    if (mark == 1) {
        state = 1;
    } else if (mark == 3) {
        double eye = _walker.getEye();
        depth = -(eye - _walker.getFeet());
        for (size_t i = 0; i < flats.size(); i++) {
            if (_flatMark[i] == 1 && (flats[i].objectId & 0x1FFF) == _poolSurface[floor] &&
                _walker.flatContains((int)i, _walker.getX(), _walker.getY())) {
                depth = fromFixed(flats[i].height) - eye;
                _waterZones = flats[i].getActiveZones();
                break;
            }
        }
        if (depth >= kWaterBand)
            state = 2;
        else if (depth < -kWaterBand)
            state = 1;
        else
            state = _walker.getFallSpeed() > 0 ? 2 : 1;
    }
    if (state != 1)
        _waterZones = 0;
    if (state == 0 && _waterState == 0)
        return;

    double gravity = Walker::kGravity, maxFall = _walker.getMaxFallSpeed();
    if (!(_controls & kControlSwim)) {
        if (state == 2) {
            gravity *= 0.5;
            maxFall = 100;
        }
    } else if (state == 1) {
        // Afloat: the eye rides about a unit over the surface.
        double above = depth < -kWaterBand ? -depth : kWaterBand;
        if (above - 1 < Walker::kEyeHeight)
            gravity = (above - 1) * Walker::kGravity / Walker::kEyeHeight;
    } else if (state == 2) {
        gravity = 0;
    }
    _walker.setGravity(gravity, maxFall);

    if (state != _waterState) {
        _waterState = state;
        static const uint16_t kEvents[3] = {kEventOutOfWater, kEventInWater, kEventUnderWater};
        broadcast(kEvents[state]);
    }
}

void Mission::dropBillboard(LevelFile::Billboard &b) {
    // 0x4B7AA0: the highest floor no more than 2 units above the
    // billboard's base, passing through pass-through floors.
    double z = fromFixed(b.z);
    int floor =
        _walker.findFloor(fromFixed(b.x), fromFixed(b.y), z + 2, Walker::kAllZones, LevelFile::kFlagPassThrough);
    if (floor < 0)
        return;
    const LevelFile::Sector &flat = _level.getSectors()[floor];
    b.zones = flat.zones;
    b.light = flat.light;
    if (!(b.flags & LevelFile::kBillboardFlyer))
        b.z = flat.height;
}

void Mission::setVisible(uint16_t id, bool visible) {
    for (LevelFile::Wall &wall : _level.getWalls()) {
        if (isObject(wall.objectId, kTypeWall, id))
            wall.flags = visible ? wall.flags | LevelFile::kFlagVisible : wall.flags & ~LevelFile::kFlagVisible;
    }
    for (LevelFile::Sector &flat : _level.getSectors()) {
        if (isObject(flat.objectId, kTypeFlat, id))
            flat.flags = visible ? flat.flags | LevelFile::kFlagVisible : flat.flags & ~LevelFile::kFlagVisible;
    }
    for (LevelFile::Billboard &b : _level.getBillboards()) {
        if (isObject(b.objectId, kTypeBillboard, id))
            b.hidden = !visible;
    }
    // Showing an actor makes it idle (0x456BBC); hiding takes it out (mode 8).
    if (_actors && _actors->isActor(id))
        _actors->setMode(id, visible ? Actors::kModeIdle : Actors::kModeHidden);
}

void Mission::setActorMode(ScriptVM &vm, int mode, bool popTarget) {
    uint16_t id = (uint16_t)vm.pop();
    uint16_t target = popTarget ? (uint16_t)vm.pop() : 0;
    if (_actors)
        _actors->setMode(id, mode, target);
}

void Mission::setTexture(uint16_t id, uint16_t texture, uint16_t frames, uint16_t speed) {
    for (LevelFile::Wall &wall : _level.getWalls()) {
        if (isObject(wall.objectId, kTypeWall, id)) {
            wall.texture = texture;
            wall.frames = frames;
            wall.speed = speed;
        }
    }
    for (LevelFile::Sector &flat : _level.getSectors()) {
        if (isObject(flat.objectId, kTypeFlat, id)) {
            flat.texture = texture;
            flat.frames = frames;
            flat.speed = speed;
        }
    }
    for (LevelFile::Billboard &b : _level.getBillboards()) {
        if (isObject(b.objectId, kTypeBillboard, id)) {
            b.texture = texture;
            b.frames = frames;
            b.speed = speed;
        }
    }
}

void Mission::restoreTexture(uint16_t id) {
    std::vector<LevelFile::Wall> &walls = _level.getWalls();
    for (size_t i = 0; i < walls.size(); i++) {
        if (isObject(walls[i].objectId, kTypeWall, id))
            walls[i].texture = _wallTextures[i];
    }
    std::vector<LevelFile::Sector> &flats = _level.getSectors();
    for (size_t i = 0; i < flats.size(); i++) {
        if (isObject(flats[i].objectId, kTypeFlat, id))
            flats[i].texture = _flatTextures[i];
    }
    std::vector<LevelFile::Billboard> &billboards = _level.getBillboards();
    for (size_t i = 0; i < billboards.size(); i++) {
        if (isObject(billboards[i].objectId, kTypeBillboard, id))
            billboards[i].texture = _billboardTextures[i];
    }
}

void Mission::setFullbright(uint16_t id, bool on) {
    for (LevelFile::Wall &wall : _level.getWalls()) {
        if (isObject(wall.objectId, kTypeWall, id))
            wall.fullbright = on;
    }
    for (LevelFile::Sector &flat : _level.getSectors()) {
        if (isObject(flat.objectId, kTypeFlat, id))
            flat.fullbright = on;
    }
    for (LevelFile::Billboard &b : _level.getBillboards()) {
        if (isObject(b.objectId, kTypeBillboard, id))
            b.fullbright = on;
    }
}

void Mission::setPlayerHeading(int16_t degrees) {
    // The original looks along (sin h, cos h) on the floor plan; the
    // renderer's yaw is measured from +x.
    _yaw = kPi / 2 - degrees * kPi / 180;
    _hasYaw = true;
}

void Mission::skipNative(ScriptVM &vm, uint16_t op) {
    const OpcodeInfo *info = getOpcodeInfo(op);
    if (info) {
        for (int i = 0; i < info->pops; i++)
            vm.pop();
        for (int i = 0; i < info->pushes; i++)
            vm.push(0);
    }
    if (_missing[op]++ == 0)
        debugC(1, kDebugChannel, "native %02X (%s) is not implemented", op, info ? info->name : "?");
}

void Mission::unhandledEvent(ScriptVM & /*vm*/, ScriptInstance &self, uint16_t event,
                             const std::vector<int16_t> &args) {
    // An actor hit (damage, power, team) without a handler of its own takes
    // it, unless the attacker is on its team (0x457B29).
    if (event == kEventHit && _actors && args.size() >= 3 && _actors->isActor(self.object->id) &&
        _actors->getTeam(self.object->id) != args[2]) {
        uint16_t outcome = _actors->takeHit(self.object->id, args[0], args[1]);
        if (outcome)
            send(self.object->id, outcome);
    }
}

void Mission::callNative(ScriptVM &vm, ScriptInstance &self, uint16_t op) {
    switch (op) {
    case OP_SEND_SIGNAL: {
        uint16_t id = (uint16_t)vm.pop();
        int16_t value = vm.pop();
        send(id, kEventSignal, {value});
        break;
    }
    case 0x2B: { // RANDOM
        int16_t n = vm.pop();
        vm.push(n > 0 ? (int16_t)_random.getRandomNumber((uint32_t)n - 1) : 0);
        break;
    }
    case 0x2C: { // SEND_SET_VALUE
        uint16_t id = (uint16_t)vm.pop();
        int16_t value = vm.pop();
        send(id, kEventSetValue, {value});
        break;
    }
    case 0x2D: // RESET_DEFAULT
        restoreTexture((uint16_t)vm.pop());
        break;
    case 0x2E:   // DISABLE_OBJECT
    case 0x2F: { // ENABLE_OBJECT
        int index = _vm->findInstance((uint16_t)vm.pop());
        if (index >= 0)
            _vm->getInstance(index).enabled = op == 0x2F;
        break;
    }
    case 0x30: { // UNSUBSCRIBE
        uint16_t id = (uint16_t)vm.pop();
        _listeners.erase(std::remove(_listeners.begin(), _listeners.end(), id), _listeners.end());
        break;
    }
    case 0x31: { // SUBSCRIBE
        uint16_t id = (uint16_t)vm.pop();
        if (std::find(_listeners.begin(), _listeners.end(), id) == _listeners.end())
            _listeners.insert(_listeners.begin(), id);
        break;
    }
    case 0x32: { // SET_TIMER_MS
        uint16_t id = (uint16_t)vm.pop();
        setTimer(id, (uint16_t)vm.pop());
        break;
    }
    case 0x33: { // SET_TIMER_SECONDS
        uint16_t id = (uint16_t)vm.pop();
        setTimer(id, (uint32_t)(uint16_t)vm.pop() * 1000);
        break;
    }
    case 0x34: { // SET_TIMER_RANDOM
        uint16_t id = (uint16_t)vm.pop();
        int min = vm.pop(), max = vm.pop();
        int span = std::max(1, max - min + 1);
        setTimer(id, (uint32_t)std::max(0, min + (int)_random.getRandomNumber((uint32_t)span - 1)) * 100);
        break;
    }
    case 0x35: // KILL_TIMER
        _timers.erase((uint16_t)vm.pop());
        break;
    case 0x36: { // SET_SURFACE_TEXTURE
        uint16_t id = (uint16_t)vm.pop();
        uint16_t texture = (uint16_t)vm.pop();
        uint16_t frames = (uint16_t)vm.pop();
        int16_t period = vm.pop();
        setTexture(id, texture, frames, period > 0 ? (uint16_t)(0x4000 / period) : 0);
        break;
    }
    case 0x46: // HIDE
        setVisible((uint16_t)vm.pop(), false);
        break;
    case 0x47: // SHOW
        setVisible((uint16_t)vm.pop(), true);
        break;
    case 0x43: { // SET_PROXIMITY_RANGES
        int actorClass = vm.pop();
        int targetClass = vm.pop();
        double radii[5];
        for (int i = 0; i < 5; i++)
            radii[i] = pop32(vm);
        if (_actors)
            _actors->setProximity(actorClass, targetClass, radii);
        break;
    }
    case 0x44: { // SET_ACTOR_STEP_HEIGHT
        uint16_t id = (uint16_t)vm.pop();
        double height = pop32(vm);
        if (_actors)
            _actors->setStepHeight(id, height);
        break;
    }
    case 0x48: // ACTOR_WANDER
        setActorMode(vm, Actors::kModeWander, false);
        break;
    case 0x49: // ACTOR_APPROACH_PLAYER
        setActorMode(vm, Actors::kModeWalkTo, false);
        break;
    case 0x4E: { // SET_ACTOR_COMBAT_PARAMS: the wander settings
        // 0x457703; 0x8000 and 0x80000000 keep a value.
        uint16_t id = (uint16_t)vm.pop();
        Actors::Wander wander;
        wander.guardMode = (uint16_t)vm.pop();
        wander.guardTarget = (uint16_t)vm.pop();
        wander.minTurn = (uint16_t)vm.pop();
        wander.maxTurn = (uint16_t)vm.pop();
        wander.inner = popRaw32(vm);
        wander.home = popRaw32(vm);
        wander.outer = popRaw32(vm);
        wander.minWalk = popRaw32(vm);
        wander.maxWalk = popRaw32(vm);
        if (_actors)
            _actors->setWander(id ? id : self.object->id, wander);
        break;
    }
    case 0x4F: { // SET_ACTOR_WIDTH
        uint16_t id = (uint16_t)vm.pop();
        double width = pop32(vm);
        if (_actors)
            _actors->setRadius(id, width / 2);
        break;
    }
    case 0x50: { // SET_ACTOR_HEIGHT
        uint16_t id = (uint16_t)vm.pop();
        double height = pop32(vm);
        if (_actors)
            _actors->setBodyHeight(id, height);
        break;
    }
    case 0x53: { // SET_ACTOR_ANIM_RATE
        int mode = vm.pop();
        int rate = vm.pop();
        if (_actors)
            _actors->setAnimationRate(self.object->id, mode, (uint8_t)rate);
        break;
    }
    case 0x54: // ACTOR_STATE_1
        setActorMode(vm, Actors::kModePain, false);
        break;
    case 0x55: // ACTOR_ATTACK
        setActorMode(vm, Actors::kModeAttack, true);
        break;
    case 0x56: // ACTOR_FACE
        setActorMode(vm, Actors::kModeFace, true);
        break;
    case 0x57: // ACTOR_STATE_4
        setActorMode(vm, Actors::kModeDeathA, false);
        break;
    case 0x58: // ACTOR_STATE_5
        setActorMode(vm, Actors::kModeDeathB, false);
        break;
    case 0x5A: { // SET_ACTOR_REACH
        uint16_t id = (uint16_t)vm.pop();
        double reach = pop32(vm);
        if (_actors)
            _actors->setReach(id, reach);
        break;
    }
    case 0x41: { // SET_ACTOR_COMBAT_STATS
        int32_t values[7];
        for (int i = 0; i < 7; i++)
            values[i] = vm.pop();
        if (_actors)
            _actors->setCombatStats(self.object->id, values);
        break;
    }
    case 0x42: { // GET_ACTOR_COMBAT_STATS
        uint16_t id = (uint16_t)vm.pop();
        Actors::CombatStats stats;
        if (_actors)
            stats = _actors->getCombatStats(id ? id : self.object->id);
        vm.push((int16_t)stats.hitPoints);
        vm.push((int16_t)stats.stun);
        break;
    }
    case 0x45: { // SET_ACTOR_WEAPON
        uint16_t id = (uint16_t)vm.pop();
        int16_t weapon = vm.pop();
        if (_actors)
            _actors->setWeapon(id, weapon);
        break;
    }
    case 0xF1: { // SET_ACTOR_TEAM
        uint16_t id = (uint16_t)vm.pop();
        int16_t team = vm.pop();
        if (_actors)
            _actors->setTeam(id, team);
        break;
    }
    case 0x82: { // ATTACH_ITEM_BEHAVIOUR
        uint16_t id = (uint16_t)vm.pop();
        Item &item = _items[id];
        item.kind = vm.pop();
        item.b = vm.pop();
        item.c = vm.pop();
        // 0x449918: the items with a charge start full, at a power level.
        struct Charge {
            int16_t kind;
            int32_t charge, maxLevel, level;
        };
        // Kinds 9 (a worn shield) and 14 (a script-driven gauge) hold 30.
        static const Charge kCharges[] = {{2, 320, 16, 4}, {3, 200, 12, 4}, {4, 400, 16, 8},   {5, 1200, 18, 18},
                                          {6, 240, 12, 4}, {9, 30, 0, 0},   {11, 100, 10, 10}, {12, 100, 10, 10},
                                          {13, 360, 9, 3}, {14, 30, 0, 0}};
        item.charge = item.maxCharge = item.level = item.maxLevel = 0;
        for (const Charge &charge : kCharges) {
            if (charge.kind == item.kind) {
                item.charge = item.maxCharge = charge.charge;
                item.maxLevel = charge.maxLevel;
                item.level = charge.level;
            }
        }
        break;
    }
    case 0x84: { // INVENTORY_ADD
        uint16_t id = (uint16_t)vm.pop();
        int16_t slot = vm.pop();
        // The first thing the player can hold that is picked up is in use.
        if (_hud.addItem(id, slot) >= 0 && !_activeItem && isWeapon(id))
            _activeItem = id;
        break;
    }
    case 0x85: { // INVENTORY_REMOVE
        uint16_t id = (uint16_t)vm.pop();
        _hud.removeItem(id);
        if (_activeItem == id)
            _activeItem = 0;
        break;
    }
    case 0x8C: // SET_ACTIVE_ITEM
        if (vm.pop())
            _activeItem = self.object->id;
        else if (_activeItem == self.object->id)
            _activeItem = 0;
        break;
    case 0xB4: { // ADD_ITEM_CHARGE
        uint16_t id = (uint16_t)vm.pop();
        int16_t delta = vm.pop();
        // 0x44E952: within 0 and the most it holds.
        auto it = _items.find(id ? id : self.object->id);
        if (it != _items.end())
            it->second.charge = std::max<int32_t>(0, std::min(it->second.charge + delta, it->second.maxCharge));
        break;
    }
    case 0xE9: { // SET_ITEM_SETTING
        uint16_t id = (uint16_t)vm.pop();
        int16_t value = vm.pop();
        // 0x449E2C: how long the muzzle flashes when it is used (view.vt134).
        auto it = _items.find(id ? id : self.object->id);
        if (it != _items.end())
            it->second.setting = value;
        break;
    }
    case 0xC7: { // GET_ITEM_CHARGE
        uint16_t id = (uint16_t)vm.pop();
        auto it = _items.find(id ? id : self.object->id);
        vm.push(it == _items.end() ? 0 : (int16_t)it->second.charge);
        break;
    }
    case 0xC4: { // SET_PUNCH
        int16_t image = vm.pop();
        const std::vector<std::string> &names = _script.getResourceNames();
        _punch.image = image >= 0 && image < (int16_t)names.size() ? names[image] : std::string();
        _punch.msPerFrame = (uint32_t)std::max<int16_t>(1, vm.pop());
        _punch.hitFrame = vm.pop();
        _punch.damage = vm.pop();
        _punch.power = vm.pop();
        _punch.swingSound = vm.pop();
        _punch.swingVolume = vm.pop();
        _punch.hitSound = vm.pop();
        _punch.hitVolume = vm.pop();
        break;
    }
    case 0x4D: { // SET_POSITION
        uint16_t id = (uint16_t)vm.pop();
        setPosition(id, popPosition(vm));
        break;
    }
    case 0x59: { // SET_HEADING
        uint16_t id = (uint16_t)vm.pop();
        int16_t heading = vm.pop();
        if (_actors)
            _actors->setHeading(id, (uint16_t)(heading * 65536 / 360));
        break;
    }
    case 0x70: { // HIDE_ACTOR
        uint16_t id = (uint16_t)vm.pop();
        vm.pop();
        setVisible(id, false);
        break;
    }
    case 0x71: { // DROP_AT
        uint16_t id = (uint16_t)vm.pop();
        uint16_t where = (uint16_t)vm.pop();
        Position position;
        setVisible(id, true);
        if (getPosition(where, position))
            dropAt(id, position);
        break;
    }
    case 0x72: // TRIGGER
        send((uint16_t)vm.pop(), kEventActivate);
        break;
    case 0x75: { // TELEPORT_PLAYER
        Position position = popPosition(vm);
        debugC(1, kDebugChannel, "player to (%.1f, %.1f, %.1f) heading %d", position.x, position.y, position.z,
               position.heading);
        _walker.place(position.x, position.y, position.z);
        setPlayerHeading(position.heading);
        break;
    }
    case 0x87: { // DAMAGE_PLAYER
        int16_t amount = vm.pop();
        int16_t type = vm.pop();
        damagePlayer((int16_t)self.object->id, amount, type);
        break;
    }
    case 0x89: { // ADD_HEALTH
        int16_t amount = vm.pop();
        _health = (int16_t)std::max(1, std::min<int>(_maxHealth, _health + amount));
        broadcast(kEventPlayerHealed, {_health});
        break;
    }
    case 0xA2: { // SET_STEP_HEADROOM
        _walker.setHeadroom(pop32(vm));
        _walker.setStepHeight(pop32(vm));
        break;
    }
    case 0xB1: { // SET_PARENT
        int index = _vm->findInstance((uint16_t)vm.pop());
        self.parent = index >= 0 ? &_vm->getInstance(index) : nullptr;
        break;
    }
    case 0xB3: // SET_EYE_HEIGHT
        _walker.setEyeHeight(pop32(vm));
        break;
    case 0xB7: // SET_SKY_ENABLED
        _renderer.setSkyEnabled(vm.pop() != 0);
        break;
    case 0xBA:   // SCREEN_FLASH_NOTIFY
    case 0xBB: { // SCREEN_FLASH
        uint16_t notify = op == 0xBA ? (uint16_t)vm.pop() : 0;
        uint8_t r = (uint8_t)vm.pop(), g = (uint8_t)vm.pop(), b = (uint8_t)vm.pop();
        int percent = vm.pop();
        uint32_t fadeIn = popRaw32(vm), hold = popRaw32(vm), fadeOut = popRaw32(vm);
        _hud.setEffect(op == 0xBA ? 0 : 1, r, g, b, percent, fadeIn, hold, fadeOut, notify);
        break;
    }
    case 0xBC: { // SCREEN_TINT
        uint8_t r = (uint8_t)vm.pop(), g = (uint8_t)vm.pop(), b = (uint8_t)vm.pop();
        _hud.setEffect(3, r, g, b, vm.pop(), 0, 0, 0, 0);
        break;
    }
    case 0xBD: { // fade in and out over the time given
        uint16_t notify = (uint16_t)vm.pop();
        uint8_t r = (uint8_t)vm.pop(), g = (uint8_t)vm.pop(), b = (uint8_t)vm.pop();
        int percent = vm.pop();
        uint32_t total = (uint16_t)vm.pop();
        _hud.setEffect(2, r, g, b, percent, total / 2, 0, total - total / 2, notify);
        break;
    }
    case 0x8E: // VIEW_SHAKE
        _hud.setShake(vm.pop());
        break;
    case 0xB0: // VIEW_OVERLAY: the underwater ripple
        _hud.setRipple(vm.pop() != 0);
        break;
    case 0x6C: { // PANEL_SHOW_IMAGE
        uint16_t id = (uint16_t)vm.pop();
        int16_t image = vm.pop();
        int16_t frame = vm.pop();
        _hud.showPanel(id ? id : self.object->id, image, frame, _walker.getX(), _walker.getY());
        break;
    }
    case 0x6D: { // animate a panel's image
        uint16_t id = (uint16_t)vm.pop();
        int16_t first = vm.pop(), last = vm.pop(), loops = vm.pop();
        uint16_t ms = (uint16_t)vm.pop();
        _hud.animatePanel(id ? id : self.object->id, first, last, loops, ms);
        break;
    }
    case 0x6E: { // PANEL_REMOVE
        uint16_t id = (uint16_t)vm.pop();
        _hud.removePanel(id ? id : self.object->id);
        break;
    }
    case 0x6F: // the first panel's object
        vm.push((int16_t)_hud.getFirstPanelOwner());
        break;
    case 0x9D: { // PANEL_ADD_BUTTON
        int16_t button = vm.pop(), image = vm.pop(), x = vm.pop(), y = vm.pop(), w = vm.pop(), h = vm.pop();
        _hud.addButton(self.object->id, button, image, Rect(x, y, x + w, y + h), false);
        break;
    }
    case 0x8F: { // add a hotspot
        int16_t button = vm.pop(), x = vm.pop(), y = vm.pop(), w = vm.pop(), h = vm.pop();
        _hud.addButton(self.object->id, button, -1, Rect(x, y, x + w, y + h), true);
        break;
    }
    case 0x90: // remove a hotspot
    case 0xA1: // remove a button
        _hud.removeButton(self.object->id, vm.pop());
        break;
    case 0x9E: { // SET_PANEL_BUTTON
        int16_t button = vm.pop(), image = vm.pop(), frame = vm.pop();
        _hud.setButton(self.object->id, button, image, frame);
        break;
    }
    case 0x9F: { // PANEL_ANIMATE_BUTTON
        int16_t button = vm.pop(), first = vm.pop(), last = vm.pop(), loops = vm.pop();
        uint16_t ms = (uint16_t)vm.pop();
        _hud.animateButton(self.object->id, button, first, last, loops, ms);
        break;
    }
    case 0xA0: { // colour-keyed button
        int16_t button = vm.pop();
        _hud.setButtonKeyed(self.object->id, button, vm.pop() != 0);
        break;
    }
    case 0xC3: { // SET_BODY_IMAGE
        int16_t image = vm.pop();
        _hud.setBodyImage(image, vm.pop());
        break;
    }
    case 0x7B: { // MAP_MARKER
        uint16_t id = (uint16_t)vm.pop();
        int16_t style = vm.pop();
        if ((id & kTypeMask) == kTypeBillboard || (id & kTypeMask) == kTypeActor)
            _hud.setMarker(id, Hud::kMarkerLife, style);
        break;
    }
    case 0x7C: // MAP_REMOVE
        _hud.removeMarker((uint16_t)vm.pop(), Hud::kMarkerLife);
        break;
    case 0x7D: // energy marker
        _hud.setMarker((uint16_t)vm.pop(), Hud::kMarkerEnergy, 0);
        break;
    case 0x7E:
        _hud.removeMarker((uint16_t)vm.pop(), Hud::kMarkerEnergy);
        break;
    case 0x7F: // trilithium marker
        _hud.setMarker((uint16_t)vm.pop(), Hud::kMarkerTrilithium, 0);
        break;
    case 0x80:
        _hud.removeMarker((uint16_t)vm.pop(), Hud::kMarkerTrilithium);
        break;
    case 0x86: { // SET_ZONE_ON_MAP: walls with flag 0x800 are drawn on the map
        int16_t zone = vm.pop();
        bool on = vm.pop() != 0;
        uint64_t mask = zone > 0 && zone < 64 ? (uint64_t)1 << zone : ~(uint64_t)0;
        for (LevelFile::Wall &wall : _level.getWalls()) {
            if (zone == 0 || (wall.zones & mask) || wall.zones == 0)
                wall.flags = on ? wall.flags | LevelFile::kWallOnMap : wall.flags & ~LevelFile::kWallOnMap;
        }
        break;
    }
    case 0xCA: { // MAP_SET_LEVELS: three height bands, low and high
        double levels[6];
        for (int i = 0; i < 6; i++)
            levels[i] = pop32(vm);
        _hud.setMapLevels(levels);
        break;
    }
    case 0xA5: { // GAME_MODE
        int16_t command = vm.pop();
        if (command == 2 || command == 3)
            _leftPanelRequest = command;
        break;
    }
    case 0x68: { // PLAY_SOUND
        int sound = vm.pop();
        int volume = vm.pop();
        vm.push((int16_t)_sound->playSound(sound, volume));
        break;
    }
    case 0x69: { // PLAY_SOUND_DELAYED
        int sound = vm.pop();
        vm.pop();
        uint32_t delay = popRaw32(vm);
        vm.push((int16_t)_sound->playDelayed(sound, delay));
        break;
    }
    case 0x6A: // PRELOAD_SOUND: files are read when played
        vm.pop();
        break;
    case 0x6B: // STOP_SOUND: takes a 16-bit value, which the original only matches against voices
        _sound->stopOne((uint32_t)(uint16_t)vm.pop() << 16);
        break;
    case 0x8D: { // PLAY_SOUND_CUE
        uint16_t id = (uint16_t)vm.pop();
        int cue = vm.pop();
        _sound->playCue(cue, id);
        break;
    }
    case 0xCF: // STOP_ONE_SOUND
        _sound->stopOne(popRaw32(vm));
        break;
    case 0xD0: { // SET_SOUND_VOLUME
        uint32_t reference = popRaw32(vm);
        _sound->setVolume(reference, vm.pop());
        break;
    }
    case 0xD1: { // SET_SOUND_LOOP
        int sound = vm.pop();
        int32_t repeats = (int32_t)popRaw32(vm);
        uint32_t minGap = (uint16_t)vm.pop();
        uint32_t maxGap = (uint16_t)vm.pop();
        _sound->setLoop(sound, repeats, minGap, maxGap);
        break;
    }
    case 0xD2: { // SET_SOUND_PRIORITY
        uint32_t reference = (uint16_t)vm.pop();
        _sound->setPriority(reference, vm.pop());
        break;
    }
    case 0xD3: { // SET_SOUND_POSITION
        uint32_t reference = popRaw32(vm);
        double x = pop32(vm), y = pop32(vm), z = pop32(vm);
        _sound->setPosition(reference, x, y, z);
        break;
    }
    case 0xDC: { // PLAY_SOUND_FADE_IN
        uint32_t reference = popRaw32(vm);
        uint32_t ms = (uint16_t)vm.pop();
        vm.push((int16_t)_sound->playFadeIn(reference, ms));
        break;
    }
    case 0xDD: { // FADE_OUT_SOUND
        uint32_t reference = popRaw32(vm);
        vm.push(_sound->fadeOut(reference, (uint16_t)vm.pop()) ? 0 : -1);
        break;
    }
    case 0xDE: { // MUTE_SOUND
        uint32_t reference = popRaw32(vm);
        _sound->setMuted(reference, vm.pop() != 0);
        break;
    }
    case 0xDF:
    case 0xE0: { // SET_SOUND_OBJECT
        uint32_t reference = popRaw32(vm);
        _sound->setObject(reference, (uint16_t)vm.pop());
        break;
    }
    case 0xE1: { // SET_SOUND_MAX_DISTANCE
        uint32_t reference = popRaw32(vm);
        _sound->setRadius(reference, pop32(vm));
        break;
    }
    case 0xBE: { // DISTANCE_TO_PLAYER
        Position position;
        int16_t distance = 0;
        if (getPosition((uint16_t)vm.pop(), position)) {
            double dx = position.x - _walker.getX(), dy = position.y - _walker.getY();
            double dz = position.z - _walker.getFeet();
            distance = (int16_t)std::min<long>(32767, std::lround(std::sqrt(dx * dx + dy * dy + dz * dz)));
        }
        vm.push(distance);
        break;
    }
    case 0x27: // DO_LOG
    case 0x29: // DO_LOG_LOCAL
    case 0x2A: // DO_LOG_GLOBAL
        debugC(3, kDebugChannel, "%04X: log %02X %d", self.object->id, op, vm.pop());
        break;
    case 0x28: // DO_LOG_STACK
        // Logs the top n words without popping them.
        vm.pop();
        break;
    case 0x37: // RESET_TEXTURE
        restoreTexture((uint16_t)vm.pop());
        break;
    case 0x40: { // SET_ACTOR_MAP_MARKER
        // 0x457163: an actor's marker; values 1 and 2 are style 0, k is k - 2.
        vm.pop();
        int16_t value = vm.pop();
        if ((self.object->id & kTypeMask) == kTypeActor && value >= 1 && value <= 9)
            _hud.setMarker(self.object->id, Hud::kMarkerLife, std::max(0, value - 2));
        break;
    }
    case 0x4A: { // ACTOR_GO_TO_ACTOR
        uint16_t id = (uint16_t)vm.pop();
        uint16_t target = (uint16_t)vm.pop();
        if (_actors)
            _actors->setMode(id ? id : self.object->id, Actors::kModeWalkTo, (uint16_t)(target | kTypeActor));
        break;
    }
    case 0x4B: { // ACTOR_GO_TO_BILLBOARD
        // 0x456CC9: mode 7 towards a billboard (target | 0x2000).
        uint16_t id = (uint16_t)vm.pop();
        uint16_t target = (uint16_t)vm.pop();
        Position to;
        if (_actors && getPosition((uint16_t)(target | kTypeBillboard), to))
            _actors->goTo(id ? id : self.object->id, to.x, to.y, to.z);
        break;
    }
    case 0x74: { // DISENGAGE
        // 0x4381B2: forget the engaged object, if it is this one, quietly.
        uint16_t id = (uint16_t)vm.pop();
        if (_engaged == id)
            _engaged = 0;
        break;
    }
    case 0x95: // SET_TRANSITION_STYLE
        // game+0x330 (1 by default), the transitions' speckles (0x40BC05):
        // 1 single pixels, 2 streaks a sixteenth of the width.
        _transitionStyle = vm.pop();
        break;
    case 0xED: { // SET_FLAT_SAFE
        // The flat object's +0x6C (0x45BA40): landing on it, or bumping a
        // jump into it, does no harm (0x4357E5).
        uint16_t id = (uint16_t)vm.pop();
        bool safe = vm.pop() != 0;
        id = id ? id : self.object->id;
        if (safe)
            _safeFlats.insert(id);
        else
            _safeFlats.erase(id);
        break;
    }
    case 0x5B: { // SET_ACTOR_TARGET
        uint16_t id = (uint16_t)vm.pop();
        uint16_t target = (uint16_t)vm.pop();
        if (_actors)
            _actors->setTarget(id ? id : self.object->id, (uint16_t)(target | kTypeActor));
        break;
    }
    case 0x5C: { // SET_ACTOR_ANIM_SPEED
        uint16_t id = (uint16_t)vm.pop();
        int anim = vm.pop();
        uint32_t speed = popRaw32(vm);
        if (_actors)
            _actors->setAnimationSpeed(id ? id : self.object->id, anim, (uint16_t)(speed >> 8));
        break;
    }
    case 0x5E: { // LIFT_SETUP
        // 0x4581AD: the flat group a lift moves and up to 8 heights.
        Lift lift;
        lift.flat = (uint16_t)vm.pop();
        lift.count = std::max(0, std::min<int>(8, vm.pop()));
        for (int i = 0; i < 8; i++)
            lift.heights[i] = (int32_t)popRaw32(vm);
        _lifts[self.object->id] = lift;
        break;
    }
    case 0x64: { // LIFT_GO_TO
        uint16_t id = (uint16_t)vm.pop();
        int level = vm.pop();
        moveLift(id ? id : self.object->id, level);
        break;
    }
    case 0x66: { // SET_LIGHT_LEVEL
        uint16_t id = (uint16_t)vm.pop();
        int percent = vm.pop();
        _renderer.setGroupBrightness((int16_t)((id ? id : self.object->id) & 0x1FFF), percent);
        break;
    }
    case 0x67: { // RESET_LIGHT_LEVEL
        uint16_t id = (uint16_t)vm.pop();
        _renderer.resetGroupBrightness((int16_t)((id ? id : self.object->id) & 0x1FFF));
        break;
    }
    case 0x73: { // ENGAGE
        // 0x4380FE: event 35 once the player is 3 units from here; another
        // object engaged gets its 35 at once.
        uint16_t id = (uint16_t)vm.pop();
        if (_engaged && _engaged != id)
            send(_engaged, Hud::kEventWalkedAway);
        _engaged = id;
        _engagedX = _walker.getX();
        _engagedY = _walker.getY();
        break;
    }
    case 0x76: { // TELEPORT_PLAYER_RELATIVE
        // 0x452703: by the difference between two places, turning by theirs.
        Position from = popPosition(vm), to = popPosition(vm);
        _walker.place(_walker.getX() + to.x - from.x, _walker.getY() + to.y - from.y,
                      _walker.getFeet() + to.z - from.z);
        int degrees = (int)std::lround((kPi / 2 - _currentYaw) * 180 / kPi);
        setPlayerHeading((int16_t)(degrees + to.heading - from.heading));
        break;
    }
    case 0x15: // ALLOC_GLOBALS
        // 0x44F10A grows the globals; there are always 32768 here.
        vm.pop();
        break;
    case 0x88: { // SET_ENVIRONMENT_DAMAGE
        // 0x43A566: 1 health every period ms (0 stops), as damage kind 13.
        _environmentPeriod = (uint16_t)vm.pop();
        _environmentSource = self.object->id;
        if (!_environmentPeriod)
            _environmentElapsed = 0;
        break;
    }
    case 0x8A: // SET_HEALTH
        _health = (int16_t)std::min<int>(vm.pop(), _maxHealth);
        break;
    case 0x8B: { // SET_WORN_ITEM
        uint16_t id = (uint16_t)vm.pop();
        _wornItem = id;
        break;
    }
    case 0xE7: // SET_WORN_GAUGE
        _gaugeItem = (uint16_t)vm.pop();
        break;
    case 0xCB: // SET_ITEM_CHARGE_MAX
    case 0xE6: {
        // 0x44EACC: the most it holds, then the charge, each kept for 0x8000.
        uint16_t id = (uint16_t)vm.pop();
        uint16_t charge = (uint16_t)vm.pop(), most = (uint16_t)vm.pop();
        auto it = _items.find(id ? id : self.object->id);
        if (it == _items.end())
            break;
        Item &item = it->second;
        if (most != 0x8000)
            item.maxCharge = op == 0xE6 ? most : (int16_t)most;
        if (charge != 0x8000)
            item.charge = op == 0xE6 ? charge : (int16_t)charge;
        item.charge = std::max<int32_t>(0, std::min(item.charge, item.maxCharge));
        break;
    }
    case 0xCC: { // GET_ITEM_VALUE
        // Worn items' charge; for weapons their team (0x45A470 and kin).
        uint16_t id = (uint16_t)vm.pop();
        auto it = _items.find(id ? id : self.object->id);
        int16_t value = 0;
        if (it != _items.end()) {
            const Item &item = it->second;
            switch (item.kind) {
            case 9:
            case 14:
                value = (int16_t)item.charge;
                break;
            case 2:
                value = 3;
                break;
            case 3:
            case 4:
            case 5:
                value = 4;
                break;
            case 6:
                value = 5;
                break;
            case 11:
            case 12:
                value = 7;
                break;
            case 13:
                value = 8;
                break;
            default:
                break;
            }
        }
        vm.push(value);
        break;
    }
    case 0x93: { // TRANSITION_TELEPORT
        // 0x43DE6F: out to the colour, the player moved, back in; event 7.
        Position to = popPosition(vm);
        uint32_t t1 = popRaw32(vm), t2 = popRaw32(vm), t3 = popRaw32(vm);
        if (_transition.active)
            break;
        _transition = Transition();
        _transition.active = true;
        _transition.teleport = true;
        _transition.phase[0] = t1;
        _transition.phase[1] = t2;
        _transition.phase[2] = t3;
        _transition.x = to.x;
        _transition.y = to.y;
        _transition.z = to.z;
        _transition.heading = to.heading;
        if (_transitionSound >= 0)
            _sound->playSound(_transitionSound, _transitionVolume);
        break;
    }
    case 0x94: // SET_TRANSITION_COLOUR
        // 0x43E084: the colour transitions blend through.
        _transitionColour[0] = (uint8_t)vm.pop();
        _transitionColour[1] = (uint8_t)vm.pop();
        _transitionColour[2] = (uint8_t)vm.pop();
        break;
    case 0x97: { // SET_MISSION_OUTCOME
        // 0x461251: up to five codes for the campaign, read when the mission ends.
        int32_t code = (int32_t)popRaw32(vm);
        if (_outcomes.size() < 5)
            _outcomes.push_back(code);
        break;
    }
    case 0xA8: // LOAD_ACTOR_CLASS
        // Loads a class's sprites (0x4B6560); every set is loaded here.
        vm.pop();
        break;
    case 0xAB: { // SET_TEXTURE_SCROLL
        uint16_t id = (uint16_t)vm.pop();
        int16_t u = vm.pop(), v = vm.pop();
        setTextureScroll(id ? id : self.object->id, u, v);
        break;
    }
    case 0xAD: // SET_FLAT_FLAG_68
        // The flat object's +0x68 = 1 (0x45BA00). Its getter (slot 0x23C)
        // is only reached by opcode 0x5B, which scripts use on actors, so
        // nothing reads it.
        vm.pop();
        break;
    case 0xAF: // SET_PLAYER_SPIN
        // 0x440B90: the player turns this many degrees a second.
        _spin = vm.pop();
        break;
    case 0xB6: // SET_SKY
        // 0x431729: sky n, and the sky on. Missions only use sky 0.
        vm.pop();
        _renderer.setSkyEnabled(true);
        break;
    case 0xB8: { // SHOW_VIEW_OVERLAY
        // 0x4415A7: a script resource's .OVR over the whole view, or -1 to remove it.
        int16_t image = vm.pop();
        int16_t mode = vm.pop();
        const std::vector<std::string> &names = _script.getResourceNames();
        _viewOverlay.image = image >= 0 && image < (int16_t)names.size() ? names[image] : std::string();
        _viewOverlay.mode = mode;
        break;
    }
    case 0xC0: { // SET_FLAT_ZONES
        uint16_t id = (uint16_t)vm.pop();
        uint64_t low = popRaw32(vm), high = popRaw32(vm);
        for (LevelFile::Sector &flat : _level.getSectors()) {
            if (isObject(flat.objectId, kTypeFlat, id ? id : self.object->id))
                flat.activeZones = high << 32 | low;
        }
        break;
    }
    case 0xC1: { // ADD_FLAT_ZONE_BAND
        // 0x4590D9: zones in play by height, the flat's own below them all.
        uint16_t id = (uint16_t)vm.pop();
        int32_t height = (int32_t)popRaw32(vm);
        uint64_t low = popRaw32(vm), high = popRaw32(vm);
        for (LevelFile::Sector &flat : _level.getSectors()) {
            if (!isObject(flat.objectId, kTypeFlat, id ? id : self.object->id))
                continue;
            auto &bands = flat.zoneBands;
            if (bands.empty())
                bands.push_back({INT32_MIN, flat.activeZones});
            if (bands.size() >= 8)
                continue;
            bands.push_back({height, high << 32 | low});
            std::stable_sort(bands.begin(), bands.end(),
                             [](const std::pair<int32_t, uint64_t> &a, const std::pair<int32_t, uint64_t> &b) {
                                 return a.first > b.first;
                             });
        }
        break;
    }
    case 0xC9: { // MAP_HEIGHT_BAND
        // 0x4442DF: the heights the map shows, below and above the player.
        uint32_t below = popRaw32(vm), above = popRaw32(vm);
        double levels[6];
        for (int i = 0; i < 6; i++)
            levels[i] = _hud.getMapLevels()[i];
        auto pick = [](uint32_t raw, double current, double fallback) {
            return raw == 0x80000000 ? current : raw == 0x80000001 ? fallback : fromFixed((int32_t)raw);
        };
        levels[2] = pick(below, levels[2], -6.0);
        levels[3] = pick(above, levels[3], 6.0);
        _hud.setMapLevels(levels);
        break;
    }
    case 0xE3: { // FIRE_PROJECTILE_AT_OBJECT
        int type = vm.pop();
        double fromX = pop32(vm), fromY = pop32(vm), fromZ = pop32(vm);
        uint16_t target = (uint16_t)vm.pop();
        Position to;
        if (getPosition(target, to) && type >= 0 && type < _projectiles->getTypeCount()) {
            double dx = to.x - fromX, dy = to.y - fromY;
            fire(type, fromX, fromY, fromZ, std::atan2(dy, dx), std::atan2(to.z - fromZ, std::sqrt(dx * dx + dy * dy)),
                 Projectiles::kShooterNone);
        }
        break;
    }
    case 0xE5: // SET_PIXEL_PICK
        // Flag 0x200: pick only opaque texels, which picking here does anyway.
        vm.pop();
        vm.pop();
        break;
    case 0xEF: // GET_ALERTED_COUNT
        vm.push((int16_t)(_actors ? _actors->getAlertedCount() : 0));
        break;
    case 0x39:   // MOVER_OPEN
    case 0x3A:   // MOVER_CLOSE
    case 0x3E:   // MOVER_SET_OPEN
    case 0x3F: { // MOVER_SET_CLOSED
        // 0x45643E: straight to the state, locked or not.
        uint16_t id = (uint16_t)vm.pop();
        static const std::map<uint16_t, int> kStates = {
            {0x39, Movers::kOpening}, {0x3A, Movers::kClosing}, {0x3E, Movers::kOpen}, {0x3F, Movers::kClosed}};
        std::vector<int> moved;
        std::vector<Movers::Notice> notices;
        _movers->setState(id ? id : self.object->id, kStates.at(op), _walker, moved, notices);
        refreshGeometry(moved);
        for (const Movers::Notice &notice : notices)
            send(notice.object, notice.event);
        break;
    }
    case 0x3B: { // MOVER_TOGGLE
        uint16_t id = (uint16_t)vm.pop();
        std::vector<int> moved;
        std::vector<Movers::Notice> notices;
        _movers->toggle(id ? id : self.object->id, _walker, moved, notices);
        refreshGeometry(moved);
        for (const Movers::Notice &notice : notices)
            send(notice.object, notice.event);
        break;
    }
    case 0x3C:   // MOVER_LOCK
    case 0x3D: { // MOVER_UNLOCK
        uint16_t id = (uint16_t)vm.pop();
        _movers->setLocked(id ? id : self.object->id, op == 0x3C);
        break;
    }
    case 0x4C: { // ACTOR_GO_TO
        uint16_t id = (uint16_t)vm.pop();
        double x = pop32(vm), y = pop32(vm), z = pop32(vm);
        if (_actors)
            _actors->goTo(id ? id : self.object->id, x, y, z);
        break;
    }
    case 0x5D:   // SET_ACTOR_ALWAYS_ACTIVE
    case 0xEA: { // SET_ACTOR_FLYER
        uint16_t id = (uint16_t)vm.pop();
        bool on = vm.pop() != 0;
        if (_actors)
            _actors->setFlag(id ? id : self.object->id, op == 0x5D ? 0x400000 : 0x1000000, on);
        break;
    }
    case 0xE8: { // SET_ACTOR_CLIMB_ANGLE
        uint16_t id = (uint16_t)vm.pop();
        int degrees = vm.pop();
        if (_actors)
            _actors->setClimbAngle(id ? id : self.object->id, degrees);
        break;
    }
    case 0xA9: // RELEASE_ACTOR_CLASS
        // Frees the class's sprites once unused (0x4B65C0); they are kept here.
        vm.pop();
        break;
    case 0x77: { // PUSH_PLAYER
        // 0x43B447: towards (or, for a negative strength, away from) a point,
        // ramping up to tRamp ms, holding to tHold and easing off to tEnd.
        Position position = popPosition(vm);
        _push.x = position.x;
        _push.y = position.y;
        _push.strength = pop32(vm);
        _push.ramp = (uint16_t)vm.pop();
        _push.hold = (uint16_t)vm.pop();
        _push.end = (uint16_t)vm.pop();
        _push.elapsed = 0;
        _push.active = true;
        break;
    }
    case 0x78: // STOP_PUSHING_PLAYER
        _push.active = false;
        break;
    case 0x83: { // SET_ITEM_ICON
        uint16_t id = (uint16_t)vm.pop();
        int16_t icon = vm.pop();
        auto it = _items.find(id ? id : self.object->id);
        if (it != _items.end())
            it->second.b = icon;
        break;
    }
    case 0xA3: { // SET_GRAVITY
        // 0x441257: gravity, the fastest fall and the step down; 0x80000000
        // keeps a value, 0x80000001 restores its default. The step down is
        // set only when the fastest fall is, as the original does.
        uint32_t gravity = popRaw32(vm), maxFall = popRaw32(vm), stepDown = popRaw32(vm);
        auto pick = [](uint32_t raw, double current, double fallback) {
            return raw == 0x80000000 ? current : raw == 0x80000001 ? fallback : fromFixed((int32_t)raw);
        };
        _walker.setGravity(pick(gravity, _walker.getGravity(), Walker::kGravity),
                           pick(maxFall, _walker.getMaxFallSpeed(), Walker::kMaxFallSpeed));
        if (maxFall != 0x80000000)
            _walker.setStepDown(maxFall == 0x80000001 ? Walker::kStepDown : fromFixed((int32_t)stepDown));
        break;
    }
    case 0xB5: // LOW_GRAVITY
        // 0x440C72: no gravity, a slow fall and no step down, or back to
        // the defaults.
        _lowGravity = vm.pop() != 0;
        if (_lowGravity) {
            _walker.setGravity(0, 10.0);
            _walker.setStepDown(0);
        } else {
            _walker.setGravity(Walker::kGravity, Walker::kMaxFallSpeed);
            _walker.setStepDown(Walker::kStepDown);
        }
        break;
    case 0xEE: { // SET_FALL_DAMAGE
        // 0x436F35: a landing at fall speed v hurts v * multiplier / divisor
        // (kind 1, 0x43A662). 0x80000000 keeps a value, 0x80000001 restores
        // its default: 1 and 32.
        uint32_t multiplier = popRaw32(vm), divisor = popRaw32(vm);
        if (multiplier != 0x80000000)
            _fallMultiplier = multiplier == 0x80000001 ? 1.0 : (double)(int32_t)multiplier;
        if (divisor != 0x80000000)
            _fallDivisor = divisor == 0x80000001 ? 32.0 : fromFixed((int32_t)divisor);
        break;
    }
    case 0xAE: // SET_PLAYER_CONTROLS
        // game+0x1E8, 0x5F by default: the bits of Mission::Control, what the
        // player may do (0x43AD04).
        _controls = (uint16_t)vm.pop();
        break;
    case 0xB2: { // SET_PLAYER_MOTION
        // 0x440F23: 0x80000000 / 0x8000 keep a value, 0x80000001 / 0x8001
        // restore its default (0x545334).
        static const Motion kDefault;
        auto pick32 = [](uint32_t raw, double current, double fallback) {
            return raw == 0x80000000 ? current : raw == 0x80000001 ? fallback : fromFixed((int32_t)raw);
        };
        auto pick16 = [](uint16_t raw, int current, int fallback) {
            return raw == 0x8000 ? current : raw == 0x8001 ? fallback : (int)(int16_t)raw;
        };
        _motion.speed = pick32(popRaw32(vm), _motion.speed, kDefault.speed);
        _motion.turnRate = pick16((uint16_t)vm.pop(), _motion.turnRate, kDefault.turnRate);
        _motion.lookRate = pick16((uint16_t)vm.pop(), _motion.lookRate, kDefault.lookRate);
        _motion.accel = pick32(popRaw32(vm), _motion.accel, kDefault.accel);
        _motion.turnAccel = pick16((uint16_t)vm.pop(), _motion.turnAccel, kDefault.turnAccel);
        _motion.lookAccel = pick16((uint16_t)vm.pop(), _motion.lookAccel, kDefault.lookAccel);
        _motion.jump = pick32(popRaw32(vm), _motion.jump, kDefault.jump);
        break;
    }
    case 0xC8: { // MAP_ZOOM_LEVELS
        // 0x44555F: per level a scale and a shift: pixels = units * 65536 * scale >> shift.
        for (int level = 0; level < 3; level++) {
            uint16_t scale = (uint16_t)vm.pop(), shift = (uint16_t)vm.pop();
            if (scale == 0x8000 || shift == 0x8000)
                continue;
            static const double kDefault[3] = {0.5, 1.0, 2.0};
            _hud.setMapZoom(level, scale == 0x8001 ? kDefault[level]
                                                   : scale * 65536.0 / (double)(1u << std::min<uint16_t>(shift, 31)));
        }
        break;
    }
    case 0xCD: { // SET_FLAT_GROUPS
        uint16_t below = (uint16_t)vm.pop(), above = (uint16_t)vm.pop(), bottom = (uint16_t)vm.pop();
        setPool(below, above, bottom);
        _pools.push_back({below, above, bottom});
        break;
    }
    case 0xD5: // GET_DIFFICULTY
        // The configuration's +0x340, 1 unless changed.
        vm.push((int16_t)_difficulty);
        break;
    case 0xD6: // SET_INVENTORY_ENABLED
        _hud.setInventoryEnabled(vm.pop() != 0);
        break;
    case 0xD7: { // SET_ITEM_ENABLED
        uint16_t id = (uint16_t)vm.pop();
        bool on = vm.pop() != 0;
        _hud.setItemEnabled(id ? id : self.object->id, on);
        break;
    }
    case 0xE2: { // FIRE_PROJECTILE_AT
        // 0x4403DE: a shot of the given type from one point at another, from no one.
        int type = vm.pop();
        double fromX = pop32(vm), fromY = pop32(vm), fromZ = pop32(vm);
        double toX = pop32(vm), toY = pop32(vm), toZ = pop32(vm);
        double dx = toX - fromX, dy = toY - fromY;
        if (type >= 0 && type < _projectiles->getTypeCount())
            fire(type, fromX, fromY, fromZ, std::atan2(dy, dx), std::atan2(toZ - fromZ, std::sqrt(dx * dx + dy * dy)),
                 Projectiles::kShooterNone);
        break;
    }
    case 0xF4: { // SET_BILLBOARD_UNPICKABLE
        uint16_t id = (uint16_t)vm.pop();
        bool on = vm.pop() != 0;
        for (LevelFile::Billboard &billboard : _level.getBillboards()) {
            if (isObject(billboard.objectId, kTypeBillboard, id ? id : self.object->id))
                billboard.flags = on ? billboard.flags | LevelFile::kBillboardUnpickable
                                     : billboard.flags & ~LevelFile::kBillboardUnpickable;
        }
        break;
    }
    case 0x91:   // BRIEFING_TRANSITION
    case 0x92: { // END_TRANSITION
        // 0x43D8FF / 0x43DC37: three phases, t1 then t2 then t3 ms.
        uint32_t t1 = popRaw32(vm), t2 = popRaw32(vm), t3 = popRaw32(vm);
        bool ending = op == 0x92;
        // The ending one waits for nothing; it is dropped while another runs.
        if (ending && _transition.active)
            break;
        _transition = Transition();
        _transition.active = true;
        _transition.ending = ending;
        _transition.phase[0] = t1;
        _transition.phase[1] = t2;
        _transition.phase[2] = t3;
        _transition.elapsed = 0;
        if (_transitionSound >= 0)
            _sound->playSound(_transitionSound, _transitionVolume);
        break;
    }
    case 0x96: // SET_TRANSITION_SOUND
        _transitionSound = vm.pop();
        _transitionVolume = vm.pop();
        break;
    case 0x99: // SET_OBJECTIVES
        // game+0x94: the bits that must be done; -1 (none) until a script sets it.
        _objectives = popRaw32(vm);
        break;
    case 0x9A: // OBJECTIVE_DONE
        _objectivesDone |= popRaw32(vm);
        break;
    case 0x9B: // OBJECTIVE_UNDONE
        _objectivesDone &= ~popRaw32(vm);
        break;
    case 0xBF: // END_MISSION
        // game.vt114: the location's controller is done (0x443384 sets its
        // +0x50); whether it went well is only whether event 8 came.
        _finished = true;
        break;
    case 0xC6: // GET_HEALTH
        vm.push(_health);
        break;
    case 0xD9: // SET_PLAYER_WIDTH
        _walker.setRadius(pop32(vm) / 2);
        break;
    case 0xDB: { // GET_POSITION
        Position position = {0, 0, 0, 0};
        getPosition((uint16_t)vm.pop(), position);
        pushPosition(vm, position);
        break;
    }
    case 0xEC: { // SET_FULLBRIGHT
        uint16_t id = (uint16_t)vm.pop();
        setFullbright(id, vm.pop() != 0);
        break;
    }
    case 0xF2: { // GET_ARRAY
        int16_t index = vm.pop();
        vm.push(index >= 0 && index < kArraySize ? _array[index] : 0);
        break;
    }
    case 0xF3: { // SET_ARRAY
        int16_t index = vm.pop();
        int16_t value = vm.pop();
        if (index >= 0 && index < kArraySize)
            _array[index] = value;
        break;
    }
    default:
        skipNative(vm, op);
        break;
    }
}

} // namespace reluva::stgenerations
