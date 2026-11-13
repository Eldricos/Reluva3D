// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/game/actors.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "engines/stgenerations/debug.h"
#include "common/serializer.h"

namespace reluva::stgenerations {

namespace {

const double kFixed = 65536.0;
const double kPi = 3.14159265358979323846;
const uint16_t kQuarterTurn = 0x4000;
// Actor flags (record offset 0x6C).
const uint32_t kFlagFullbright = 0x10;
const uint32_t kFlagAlwaysActive = 0x400000;
// Flyers keep their height rather than standing on the floor (0x4C3AC0).
const uint32_t kFlagFlyer = 0x1000000;
// Sprite sequence for each animation entry (table at 0x552338).
const int kSequenceForAnimation[LevelFile::kAnimationCount] = {0, 2, 0, 3, 4, 5, 1};

double fromFixed(int32_t v) {
    return v / kFixed;
}

// The original's quick distances: 2D max + 3/4 min (0x4C57F0)...
double distance2D(double dx, double dy) {
    dx = std::fabs(dx);
    dy = std::fabs(dy);
    return std::max(dx, dy) + std::min(dx, dy) * 3 / 4;
}

// ...and 3D max + a quarter of the other two (0x4B4630).
double distance3D(double dx, double dy, double dz) {
    dx = std::fabs(dx);
    dy = std::fabs(dy);
    dz = std::fabs(dz);
    double m = std::max(dx, std::max(dy, dz));
    return m + (dx + dy + dz - m) / 4;
}

int animationIndex(int mode) {
    return std::min(mode, LevelFile::kAnimationCount - 1);
}

} // namespace

struct Actors::Actor {
    explicit Actor(const Walker &prototype) : walker(prototype) {}

    // The record from the level, with fields the scripts change.
    LevelFile::Actor data;
    Walker walker;
    double x = 0, y = 0, z = 0;
    double homeX = 0, homeY = 0;
    uint16_t heading = 0;
    int mode = kModeIdle;
    // Object to face, attack or walk to; 0 is the player.
    uint16_t target = 0;
    // Animation position in 1/256 frames.
    uint32_t timer = 0;
    // Set when a mode starts: attackers fire at once.
    bool fresh = true;
    // Set when a mode starts and cleared by the "now facing" report (+0x10F).
    bool announce = true;
    // Wander and walk-to steering.
    bool turning = false;
    uint16_t goal = 0;
    bool returning = false;
    double walked = 0, nextTurn = 0;
    double centreDistance = 0;
    // SET_ACTOR_COMBAT_PARAMS: the id of what it guards (+0x70), 0 none.
    uint16_t guardTarget = 0;
    // Proximity.
    bool near = false, inRange = false;
    // Projectile type fired in attack mode, or -1.
    int weapon = -1;
    bool firing = false;
    // ACTOR_GO_TO: mode 7 heads for this point instead of an object.
    bool toPoint = false;
    double pointX = 0, pointY = 0, pointZ = 0;
    // SET_ACTOR_CLIMB_ANGLE: the share of a flyer's speed spent going across (+0x80).
    double across = 1.0;
};

Actors::Actors(const LevelFile &level, const Walker &prototype, const SpriteLoader &loader) : _level(level) {
    for (const LevelFile::Actor &data : level.getActors()) {
        std::unique_ptr<Actor> actor(new Actor(prototype));
        actor->data = data;
        actor->heading = data.heading;
        actor->mode = data.mode;
        actor->homeX = actor->x = fromFixed(data.x);
        actor->homeY = actor->y = fromFixed(data.y);
        // The file's z includes the draw offset (0x4BE361).
        actor->z = fromFixed(data.z) - fromFixed(data.drawOffset);
        Walker &walker = actor->walker;
        walker.setRadius(fromFixed(data.radius));
        walker.setStepHeight(fromFixed(data.stepHeight));
        walker.setEyeHeight(fromFixed(data.bodyHeight));
        walker.setHeadroom(0);
        walker.setFalls(false);
        walker.setIgnoredFloors(LevelFile::kFlagPassThrough);
        walker.setObstacles(&_obstacles, (int)_actors.size());
        walker.place(actor->x, actor->y, actor->z);
        if (!(data.flags & kFlagFlyer) && walker.getFloor() >= 0)
            actor->z = walker.getFeet();
        _actors.push_back(std::move(actor));

        if (loader && !_sprites.count(data.spriteSet)) {
            std::unique_ptr<SpriteSet> set(new SpriteSet());
            if (loader(data.spriteSet, *set))
                _sprites[data.spriteSet] = std::move(set);
            else
                _sprites[data.spriteSet] = nullptr;
        }
    }
    _obstacles.resize(_actors.size());
    for (size_t i = 0; i < _actors.size(); i++) {
        const Actor &a = *_actors[i];
        _obstacles[i] = {a.x, a.y, a.mode == kModeHidden ? 0 : fromFixed(a.data.radius), a.z,
                         a.z + fromFixed(a.data.bodyHeight)};
    }
}

Actors::~Actors() {
}

void Actors::sync(Serializer &s) {
    s.syncCount((uint32_t)_actors.size());
    for (std::unique_ptr<Actor> &pointer : _actors) {
        if (s.err())
            return;
        Actor &a = *pointer;
        LevelFile::Actor &d = a.data;
        for (int32_t *v : {&d.reach, &d.scale, &d.height, &d.radius, &d.bodyHeight, &d.outerRadius, &d.innerRadius,
                           &d.homeRadius, &d.minWalk, &d.maxWalk, &d.stepHeight})
            s.syncAsSint32(*v);
        s.syncAsUint32(d.flags);
        s.syncAsUint16(d.minTurn);
        s.syncAsUint16(d.maxTurn);
        s.syncAsUint16(d.turnSpeed);
        s.syncAsByte(d.guardMode);
        for (LevelFile::Animation &animation : d.animations)
            s.syncAsUint16(animation.speed);
        d.light.sync(s);
        a.walker.sync(s);
        s.syncAsDouble(a.x);
        s.syncAsDouble(a.y);
        s.syncAsDouble(a.z);
        s.syncAsDouble(a.homeX);
        s.syncAsDouble(a.homeY);
        s.syncAsUint16(a.heading);
        s.syncAsInt(a.mode);
        s.syncAsUint16(a.target);
        s.syncAsUint32(a.timer);
        s.syncAsBool(a.fresh);
        s.syncAsBool(a.announce);
        s.syncAsBool(a.turning);
        s.syncAsUint16(a.goal);
        s.syncAsBool(a.returning);
        s.syncAsDouble(a.walked);
        s.syncAsDouble(a.nextTurn);
        s.syncAsDouble(a.centreDistance);
        if (s.version() == 0 || s.version() >= kGuardSaveVersion)
            s.syncAsUint16(a.guardTarget);
        s.syncAsBool(a.near);
        s.syncAsBool(a.inRange);
        s.syncAsInt(a.weapon);
        s.syncAsBool(a.firing);
        s.syncAsBool(a.toPoint);
        s.syncAsDouble(a.pointX);
        s.syncAsDouble(a.pointY);
        s.syncAsDouble(a.pointZ);
        s.syncAsDouble(a.across);
    }

    uint32_t count = (uint32_t)_proximity.size();
    s.syncAsUint32(count);
    if (s.isLoading())
        _proximity.clear();
    auto proximity = _proximity.begin();
    for (uint32_t i = 0; i < count && !s.err(); i++) {
        uint16_t key = s.isSaving() ? proximity->first : 0;
        std::vector<double> radii = s.isSaving() ? proximity->second : std::vector<double>();
        s.syncAsUint16(key);
        s.syncVector(radii, [](Serializer &ser, double &v) { ser.syncAsDouble(v); });
        if (s.isLoading())
            _proximity[key] = radii;
        else
            ++proximity;
    }

    count = (uint32_t)_combat.size();
    s.syncAsUint32(count);
    if (s.isLoading())
        _combat.clear();
    auto combat = _combat.begin();
    for (uint32_t i = 0; i < count && !s.err(); i++) {
        uint16_t id = s.isSaving() ? combat->first : 0;
        CombatStats stats = s.isSaving() ? combat->second : CombatStats();
        s.syncAsUint16(id);
        for (int32_t *v : {&stats.fullThreshold, &stats.stunThreshold, &stats.noticeThreshold, &stats.stunMax,
                           &stats.stun, &stats.hitPointsMax, &stats.hitPoints})
            s.syncAsSint32(*v);
        if (s.isLoading())
            _combat[id] = stats;
        else
            ++combat;
    }

    count = (uint32_t)_teams.size();
    s.syncAsUint32(count);
    if (s.isLoading())
        _teams.clear();
    auto team = _teams.begin();
    for (uint32_t i = 0; i < count && !s.err(); i++) {
        uint16_t id = s.isSaving() ? team->first : 0;
        int16_t value = s.isSaving() ? team->second : 0;
        s.syncAsUint16(id);
        s.syncAsSint16(value);
        if (s.isLoading())
            _teams[id] = value;
        else
            ++team;
    }
    s.syncAsUint32(_seed);

    if (s.isLoading()) {
        _shots.clear();
        for (size_t i = 0; i < _actors.size(); i++) {
            const Actor &a = *_actors[i];
            _obstacles[i] = {a.x, a.y, a.mode == kModeHidden ? 0 : fromFixed(a.data.radius), a.z,
                             a.z + fromFixed(a.data.bodyHeight)};
        }
    }
}

uint32_t Actors::random() {
    // The original's generator (0x57AA68).
    _seed = _seed * 0x41C64E6D + 0x3039;
    return _seed >> 16;
}

uint16_t Actors::headingTowards(double dx, double dy) {
    return (uint16_t)(int32_t)std::lround(std::atan2(-dx, dy) * 32768 / kPi);
}

bool Actors::isActor(uint16_t id) const {
    return !findAll(id).empty();
}

std::vector<int> Actors::findAll(uint16_t id) const {
    if ((id & 0xE000) == 0)
        id |= kObjectType;
    std::vector<int> found;
    for (size_t i = 0; i < _actors.size(); i++) {
        if ((uint16_t)(_actors[i]->data.objectId | kObjectType) == id)
            found.push_back((int)i);
    }
    return found;
}

uint16_t Actors::getObstacleObject(int obstacle) const {
    if (obstacle < 0 || obstacle >= (int)_actors.size())
        return 0;
    return (uint16_t)(_actors[obstacle]->data.objectId | kObjectType);
}

void Actors::setMode(uint16_t id, int mode, uint16_t target) {
    for (int index : findAll(id)) {
        Actor &actor = *_actors[index];
        // Every change restarts the animation (0x4B5090).
        actor.mode = mode;
        actor.target = target;
        actor.timer = 0;
        actor.fresh = true;
        actor.announce = true;
        actor.turning = false;
        actor.returning = false;
        actor.toPoint = false;
    }
}

void Actors::goTo(uint16_t id, double x, double y, double z) {
    // 0x456F96: mode 7 towards a point; arriving is event 37 as before.
    setMode(id, kModeWalkTo);
    for (int index : findAll(id)) {
        Actor &actor = *_actors[index];
        actor.toPoint = true;
        actor.pointX = x;
        actor.pointY = y;
        actor.pointZ = z;
    }
}

void Actors::setFlag(uint16_t id, uint32_t flag, bool on) {
    for (int index : findAll(id)) {
        uint32_t &flags = _actors[index]->data.flags;
        flags = on ? flags | flag : flags & ~flag;
    }
}

int Actors::getAlertedCount() const {
    int count = 0;
    for (const std::unique_ptr<Actor> &actor : _actors)
        count += actor->near && actor->mode != kModeHidden;
    return count;
}

void Actors::setTarget(uint16_t id, uint16_t target) {
    for (int index : findAll(id)) {
        _actors[index]->target = target;
        _actors[index]->toPoint = false;
    }
}

void Actors::setAnimationSpeed(uint16_t id, int animation, uint16_t speed) {
    if (animation < 0 || animation >= LevelFile::kAnimationCount)
        return;
    for (int index : findAll(id))
        _actors[index]->data.animations[animation].speed = speed;
}

void Actors::setClimbAngle(uint16_t id, int degrees) {
    // 0x456E9C: +0x80 = cos(|degrees|) in 16.16 (the runtime's cos at
    // 0x502F3A): the share of the speed going across.
    for (int index : findAll(id))
        _actors[index]->across = std::cos(std::abs(degrees) * kPi / 180);
}

bool Actors::getPosition(uint16_t id, double &x, double &y, double &z, uint16_t &heading) const {
    std::vector<int> found = findAll(id);
    if (found.empty())
        return false;
    const Actor &actor = *_actors[found[0]];
    x = actor.x;
    y = actor.y;
    z = actor.z;
    heading = actor.heading;
    return true;
}

void Actors::setPosition(uint16_t id, double x, double y, double z, uint16_t heading) {
    // As 0x4B64C0: put the actor on the floor at the point.
    for (int index : findAll(id)) {
        Actor &actor = *_actors[index];
        actor.walker.place(x, y, z);
        actor.x = x;
        actor.y = y;
        bool grounded = !(actor.data.flags & kFlagFlyer) && actor.walker.getFloor() >= 0;
        actor.z = grounded ? actor.walker.getFeet() : z;
        actor.heading = heading;
    }
}

void Actors::setHeading(uint16_t id, uint16_t heading) {
    for (int index : findAll(id))
        _actors[index]->heading = heading;
}

void Actors::setStepHeight(uint16_t id, double height) {
    for (int index : findAll(id))
        _actors[index]->walker.setStepHeight(height);
}

void Actors::setRadius(uint16_t id, double radius) {
    for (int index : findAll(id)) {
        _actors[index]->data.radius = (int32_t)std::lround(radius * kFixed);
        _actors[index]->walker.setRadius(radius);
    }
}

void Actors::setBodyHeight(uint16_t id, double height) {
    for (int index : findAll(id)) {
        _actors[index]->data.bodyHeight = (int32_t)std::lround(height * kFixed);
        _actors[index]->walker.setEyeHeight(height);
    }
}

void Actors::setReach(uint16_t id, double reach) {
    for (int index : findAll(id))
        _actors[index]->data.reach = (int32_t)std::lround(reach * kFixed);
}

void Actors::setCombatStats(uint16_t id, const int32_t values[7]) {
    std::vector<int> parts = findAll(id);
    if (parts.empty())
        return;
    CombatStats &stats = _combat[(uint16_t)(_actors[parts[0]]->data.objectId | kObjectType)];
    int32_t *fields[7] = {&stats.fullThreshold, &stats.stunThreshold, &stats.noticeThreshold, &stats.stunMax,
                          &stats.stun,          &stats.hitPointsMax,  &stats.hitPoints};
    for (int i = 0; i < 7; i++) {
        if (values[i] != kKeep)
            *fields[i] = values[i];
    }
}

Actors::CombatStats Actors::getCombatStats(uint16_t id) const {
    std::vector<int> parts = findAll(id);
    if (parts.empty())
        return CombatStats();
    auto it = _combat.find((uint16_t)(_actors[parts[0]]->data.objectId | kObjectType));
    return it == _combat.end() ? CombatStats() : it->second;
}

void Actors::setTeam(uint16_t id, int16_t team) {
    std::vector<int> parts = findAll(id);
    if (!parts.empty())
        _teams[(uint16_t)(_actors[parts[0]]->data.objectId | kObjectType)] = team;
}

int16_t Actors::getTeam(uint16_t id) const {
    std::vector<int> parts = findAll(id);
    if (parts.empty())
        return 0;
    auto it = _teams.find((uint16_t)(_actors[parts[0]]->data.objectId | kObjectType));
    return it == _teams.end() ? 0 : it->second;
}

uint16_t Actors::takeHit(uint16_t id, int damage, int power) {
    std::vector<int> parts = findAll(id);
    if (parts.empty())
        return 0;
    CombatStats &stats = _combat[(uint16_t)(_actors[parts[0]]->data.objectId | kObjectType)];
    if (power > stats.fullThreshold) {
        stats.hitPoints -= damage;
        stats.stun -= damage;
        if (stats.hitPoints <= 0) {
            stats.hitPoints = stats.stun = 0;
            return kEventKilled;
        }
        if (stats.stun <= 0) {
            stats.stun = 0;
            return kEventKnockedOut;
        }
        return kEventHurt;
    }
    if (power > stats.stunThreshold) {
        stats.stun -= damage;
        if (stats.stun <= 0) {
            stats.stun = 0;
            return kEventKnockedOut;
        }
        return kEventStunned;
    }
    if (power > stats.noticeThreshold)
        return kEventGrazed;
    return 0;
}

double Actors::getRadius(uint16_t id) const {
    std::vector<int> parts = findAll(id);
    return parts.empty() ? 0 : fromFixed(_actors[parts[0]]->data.radius);
}

void Actors::setWeapon(uint16_t id, int weapon) {
    for (int index : findAll(id))
        _actors[index]->weapon = weapon;
}

void Actors::setAnimationRate(uint16_t id, int mode, uint8_t rate) {
    if (mode < 0 || mode >= LevelFile::kAnimationCount)
        return;
    for (int index : findAll(id))
        _actors[index]->data.animations[mode].rate = rate;
}

void Actors::setWander(uint16_t id, const Wander &wander) {
    auto set16 = [](uint16_t &field, int value) {
        if (value != kKeep)
            field = (uint16_t)value;
    };
    auto set32 = [](int32_t &field, uint32_t value) {
        if (value != kKeep32)
            field = (int32_t)value;
    };
    for (int index : findAll(id)) {
        Actor &actor = *_actors[index];
        LevelFile::Actor &data = actor.data;
        if (wander.guardMode != kKeep)
            data.guardMode = (uint8_t)wander.guardMode;
        set16(actor.guardTarget, wander.guardTarget);
        // Stored as they come, as the original does: scripts give degrees
        // but the engine reads 1/65536 turns.
        set16(data.minTurn, wander.minTurn);
        set16(data.maxTurn, wander.maxTurn);
        set32(data.innerRadius, wander.inner);
        set32(data.homeRadius, wander.home);
        set32(data.outerRadius, wander.outer);
        set32(data.minWalk, wander.minWalk);
        set32(data.maxWalk, wander.maxWalk);
    }
}

void Actors::setProximity(int actorClass, int targetClass, const double radii[5]) {
    _proximity[(uint16_t)((actorClass & 0xFF) << 8 | (targetClass & 0xFF))] = std::vector<double>(radii, radii + 5);
}

bool Actors::getGuardPosition(int mode, uint16_t target, double &x, double &y) const {
    // Found by its number, whatever type bits come with it.
    uint16_t number = target & 0x1FFF;
    if (mode == 2) {
        for (const LevelFile::Billboard &billboard : _level.getBillboards()) {
            if ((billboard.objectId & 0x1FFF) == number) {
                x = fromFixed(billboard.x);
                y = fromFixed(billboard.y);
                return true;
            }
        }
        return false;
    }
    for (const std::unique_ptr<Actor> &other : _actors) {
        if ((other->data.objectId & 0x1FFF) == number) {
            x = other->x;
            y = other->y;
            return true;
        }
    }
    return false;
}

bool Actors::getTarget(const Actor &actor, const Walker &player, double &x, double &y, double &z) const {
    if (actor.toPoint) {
        x = actor.pointX;
        y = actor.pointY;
        z = actor.pointZ;
        return true;
    }
    if (actor.target != 0) {
        uint16_t heading;
        if (getPosition(actor.target, x, y, z, heading))
            return true;
    }
    x = player.getX();
    y = player.getY();
    z = player.getFeet();
    return true;
}

Actors::Shot Actors::aim(const Actor &actor, const Walker &player) const {
    // 0x4C4460: from the muzzle, turned with the actor, at the target: the
    // player's eyes, or halfway up another actor.
    double angle = actor.heading * 2 * kPi / 65536;
    double s = std::sin(angle);
    double c = std::cos(angle);
    double ox = fromFixed(actor.data.muzzleX);
    double oy = fromFixed(actor.data.muzzleY);
    Shot shot;
    shot.shooter = (uint16_t)(actor.data.objectId | kObjectType);
    shot.weapon = actor.weapon;
    shot.x = actor.x + ox * c - oy * s;
    shot.y = actor.y + oy * c + ox * s;
    shot.z = actor.z + fromFixed(actor.data.drawOffset) + fromFixed(actor.data.muzzleZ);
    double tx = player.getX();
    double ty = player.getY();
    double tz = player.getEye();
    if (actor.target != 0) {
        uint16_t heading;
        std::vector<int> parts = findAll(actor.target);
        if (getPosition(actor.target, tx, ty, tz, heading) && !parts.empty())
            tz += fromFixed(_actors[parts[0]]->data.bodyHeight) / 2;
    }
    double dx = tx - shot.x;
    double dy = ty - shot.y;
    shot.yaw = std::atan2(dy, dx);
    shot.pitch = std::atan2(tz - shot.z, std::sqrt(dx * dx + dy * dy));
    return shot;
}

void Actors::takeShots(std::vector<Shot> &shots) {
    shots.insert(shots.end(), _shots.begin(), _shots.end());
    _shots.clear();
}

void Actors::getTargets(std::vector<Projectiles::Target> &targets) const {
    targets.clear();
    for (size_t i = 0; i < _actors.size(); i++) {
        const Actor &actor = *_actors[i];
        if (actor.mode == kModeHidden)
            continue;
        targets.push_back({(uint16_t)(actor.data.objectId | kObjectType), actor.x, actor.y, _obstacles[i].bottom,
                           _obstacles[i].top, _obstacles[i].radius});
    }
}

bool Actors::turnTowards(Actor &actor, uint16_t goal, uint32_t ms) {
    int32_t limit = (int32_t)((uint32_t)actor.data.turnSpeed * ms / 1024);
    int16_t diff = (int16_t)(goal - actor.heading);
    if (std::abs((int)diff) <= limit) {
        actor.heading = goal;
        return true;
    }
    if (limit > 0)
        actor.heading = (uint16_t)(actor.heading + (diff > 0 ? limit : -limit));
    return false;
}

void Actors::moveForward(Actor &actor, double distance, uint32_t ms) {
    if (distance <= 0)
        return;
    double angle = actor.heading * 2 * kPi / 65536;
    double oldX = actor.x;
    double oldY = actor.y;
    actor.walker.move(-std::sin(angle) * distance, std::cos(angle) * distance, ms / 1000.0);
    actor.x = actor.walker.getX();
    actor.y = actor.walker.getY();
    if (!(actor.data.flags & kFlagFlyer) && actor.walker.getFloor() >= 0)
        actor.z = actor.walker.getFeet();
    actor.walked += std::sqrt((actor.x - oldX) * (actor.x - oldX) + (actor.y - oldY) * (actor.y - oldY));
}

double Actors::runAnimation(Actor &actor, uint32_t ms, std::vector<ScriptEvent> &events) {
    // 0x4C4230: about `rate` frames a second. The first pass plays every
    // frame, then it loops from loopStart; loopStart = frames - 1 plays
    // once and holds the last frame.
    const LevelFile::Animation &anim = actor.data.animations[animationIndex(actor.mode)];
    uint32_t span = (uint32_t)(anim.frames - anim.loopStart) * 256;
    uint32_t delta = std::min<uint32_t>((uint32_t)anim.rate * 256 * ms >> 10, span - 1);
    actor.timer += delta;
    uint16_t object = (uint16_t)(actor.data.objectId | kObjectType);
    bool fire = actor.mode == kModeAttack && actor.fresh;
    if ((actor.timer >> 8) >= anim.frames) {
        actor.timer -= span;
        if (anim.frames > 1)
            events.push_back({object, kEventCycle});
        fire = actor.mode == kModeAttack;
    }
    // Attackers fire at the start of each cycle, which is reported like an
    // arrival (engine code 18).
    if (fire) {
        events.push_back({object, kEventArrived});
        actor.firing = true;
    }

    // Walking keeps pace with the animation: speed (8.8 units) per frame.
    return (double)anim.speed * delta / (256.0 * 256.0);
}

void Actors::runWander(Actor &actor, int /*index*/, double step, const Walker &player, uint32_t ms) {
    // 0x4C3C10, mode 6. The centre (0x4C4015) is home, or else what it
    // guards: an actor (1) or a billboard (2), the player if none.
    const LevelFile::Actor &data = actor.data;
    double cx = actor.homeX;
    double cy = actor.homeY;
    if (data.guardMode) {
        cx = player.getX();
        cy = player.getY();
        if (actor.guardTarget)
            getGuardPosition(data.guardMode, actor.guardTarget, cx, cy);
    }
    double distance = distance2D(cx - actor.x, cy - actor.y);
    uint16_t towardsCentre = headingTowards(cx - actor.x, cy - actor.y);
    double inner = fromFixed(data.innerRadius);
    double home = fromFixed(data.homeRadius);
    double outer = fromFixed(data.outerRadius);

    if (actor.returning) {
        // Head home until back inside the outer band.
        actor.goal = towardsCentre;
        actor.turning = true;
        if (distance < outer)
            actor.returning = false;
    } else if (home > 0 && distance > home) {
        actor.returning = true;
    } else if (outer > 0 && distance > outer && !actor.turning) {
        // Turn a quarter towards the centre.
        int16_t side = (int16_t)(towardsCentre - actor.heading);
        actor.goal = (uint16_t)(actor.heading + (side >= 0 ? kQuarterTurn : -kQuarterTurn));
        actor.turning = true;
    } else if (inner > 0 && actor.centreDistance > inner && distance <= inner && !actor.turning) {
        actor.goal = (uint16_t)(actor.heading + kQuarterTurn);
        actor.turning = true;
    }
    actor.centreDistance = distance;

    if (!actor.turning && actor.walked >= actor.nextTurn) {
        // A random turn after a random distance, always the same way round.
        uint32_t range = data.maxTurn > data.minTurn ? data.maxTurn - data.minTurn : 0;
        actor.goal = (uint16_t)(actor.heading + data.minTurn + (range ? random() % (range + 1) : 0));
        actor.turning = true;
        actor.walked = 0;
        double minWalk = fromFixed(data.minWalk);
        double maxWalk = fromFixed(data.maxWalk);
        actor.nextTurn = minWalk + (maxWalk > minWalk ? (maxWalk - minWalk) * (random() % 1024) / 1023.0 : 0);
    }
    if (actor.turning && turnTowards(actor, actor.goal, ms))
        actor.turning = actor.returning;

    moveForward(actor, step, ms);
    if (actor.walker.wasBlocked()) {
        uint16_t turn = kQuarterTurn + (actor.returning ? (uint16_t)(random() % 0x2000) : 0);
        actor.goal = (uint16_t)(actor.heading + turn);
        actor.turning = true;
    }
}

void Actors::runWalkTo(Actor &actor, double step, const Walker &player, uint32_t ms, std::vector<ScriptEvent> &events) {
    // Mode 7: straight at the target. Arrival is reported every tick while
    // within reach; the actor keeps going until a script changes its mode.
    double tx;
    double ty;
    double tz;
    getTarget(actor, player, tx, ty, tz);
    if (distance2D(tx - actor.x, ty - actor.y) < fromFixed(actor.data.reach))
        events.push_back({(uint16_t)(actor.data.objectId | kObjectType), kEventArrived});

    uint16_t aim = headingTowards(tx - actor.x, ty - actor.y);
    if (actor.turning) {
        // Sidestepping an obstacle first; there is no path finding.
        if (turnTowards(actor, actor.goal, ms))
            actor.turning = false;
    } else {
        turnTowards(actor, aim, ms);
    }
    if (actor.toPoint && (actor.data.flags & kFlagFlyer)) {
        // Flyers climb or sink towards the point with what isn't spent going across.
        double rise = step * std::sqrt(std::max(0.0, 1.0 - actor.across * actor.across));
        double dz = tz - actor.z;
        actor.z += std::max(-rise, std::min(rise, dz));
        step *= actor.across;
    }
    moveForward(actor, step, ms);
    if (actor.walker.wasBlocked() && !actor.turning) {
        int16_t side = (int16_t)(aim - actor.heading);
        actor.goal = (uint16_t)(actor.heading + (side >= 0 ? kQuarterTurn : -kQuarterTurn));
        actor.turning = true;
    }
}

void Actors::runProximity(Actor &actor, const Walker &player, bool frozen, std::vector<ScriptEvent> &events) {
    auto it = _proximity.find((uint16_t)((actor.data.spriteSet & 0xFF) << 8));
    if (it == _proximity.end())
        return;
    const std::vector<double> &r = it->second;
    uint16_t object = (uint16_t)(actor.data.objectId | kObjectType);
    if (frozen) {
        if (actor.near)
            events.push_back({object, kEventPlayerLost});
        actor.near = false;
        return;
    }
    // From the player's eye to the actor's feet; seeing the player also
    // takes a line of sight (0x4BB900) from halfway between the actor's
    // muzzle and the top of its sprite to the player's eye, past walls and
    // floors in the zones of both.
    double d = distance3D(player.getX() - actor.x, player.getY() - actor.y, player.getEye() - actor.z);
    auto sees = [&]() {
        double base = actor.z + fromFixed(actor.data.drawOffset);
        double muzzle = fromFixed(actor.data.muzzleZ);
        double eye = base + muzzle + (fromFixed(actor.data.height) - muzzle) / 2;
        int floor = actor.walker.getFloor();
        uint64_t zones = player.getActiveZones();
        if (floor >= 0)
            zones |= _level.getSectors()[floor].getActiveZones();
        return player.lineOfSight(actor.x, actor.y, eye, player.getX(), player.getY(), player.getEye(), zones);
    };
    if (!actor.near && r[1] > 0 && d < r[1] && sees()) {
        actor.near = true;
        events.push_back({object, kEventPlayerNear});
    } else if (actor.near && (d > r[2] || !sees())) {
        actor.near = false;
        events.push_back({object, kEventPlayerLost});
    }
    if (!actor.inRange && r[3] > 0 && d < r[3]) {
        actor.inRange = true;
        events.push_back({object, kEventPlayerInRange});
    } else if (actor.inRange && d > r[4]) {
        actor.inRange = false;
        events.push_back({object, kEventPlayerOutOfRange});
    }
}

void Actors::update(uint32_t ms, const Walker &player, std::vector<ScriptEvent> &events) {
    ms = std::max<uint32_t>(8, std::min<uint32_t>(ms, 1000));
    const std::vector<LevelFile::Sector> &flats = _level.getSectors();
    for (size_t i = 0; i < _actors.size(); i++) {
        Actor &actor = *_actors[i];
        if (actor.mode == kModeHidden) {
            _obstacles[i].radius = 0;
            continue;
        }

        // Actors whose floor is in a zone the player can't see are frozen.
        int floor = actor.walker.getFloor();
        bool frozen =
            floor >= 0 && !(flats[floor].zones & player.getActiveZones()) && !(actor.data.flags & kFlagAlwaysActive);
        runProximity(actor, player, frozen, events);
        if (frozen)
            continue;

        double step = runAnimation(actor, ms, events);
        uint16_t object = (uint16_t)(actor.data.objectId | kObjectType);
        if (actor.firing) {
            actor.firing = false;
            if (actor.weapon >= 0)
                _shots.push_back(aim(actor, player));
        }
        switch (actor.mode) {
        case kModeFace:
        case kModeAttack: {
            double tx;
            double ty;
            double tz;
            getTarget(actor, player, tx, ty, tz);
            if (turnTowards(actor, headingTowards(tx - actor.x, ty - actor.y), ms) && actor.announce &&
                actor.mode == kModeFace) {
                events.push_back({object, kEventArrived});
                actor.announce = false;
            }
            break;
        }
        case kModeWander:
            runWander(actor, (int)i, step, player, ms);
            break;
        case kModeWalkTo:
            runWalkTo(actor, step, player, ms, events);
            break;
        default:
            break;
        }
        actor.fresh = false;

        Walker::Obstacle &o = _obstacles[i];
        o.x = actor.x;
        o.y = actor.y;
        o.radius = fromFixed(actor.data.radius);
        o.bottom = actor.z;
        o.top = actor.z + fromFixed(actor.data.bodyHeight);
    }
}

void Actors::getSprites(double cameraX, double cameraY, uint64_t visibleZones, std::vector<SpriteDraw> &sprites) const {
    sprites.clear();
    const std::vector<LevelFile::Sector> &flats = _level.getSectors();
    for (const std::unique_ptr<Actor> &pointer : _actors) {
        const Actor &actor = *pointer;
        if (actor.mode == kModeHidden || (actor.data.flags & LevelFile::kFlagNotDrawn))
            continue;
        int floor = actor.walker.getFloor();
        if (floor >= 0 && !(flats[floor].zones & visibleZones))
            continue;
        auto set = _sprites.find(actor.data.spriteSet);
        if (set == _sprites.end() || !set->second)
            continue;

        // 0x4C3C10: the sequence for the mode, then the view direction from
        // the angle between the camera and the actor's heading.
        int animation = animationIndex(actor.mode);
        const LevelFile::Animation &anim = actor.data.animations[animation];
        const std::vector<std::vector<uint8_t>> &sequences = set->second->getSequences();
        int sequence = kSequenceForAnimation[animation];
        if (sequence >= (int)sequences.size() || sequences[sequence].empty())
            continue;
        const std::vector<uint8_t> &frames = sequences[sequence];

        uint16_t seen = (uint16_t)(int32_t)std::lround(std::atan2(actor.x - cameraX, actor.y - cameraY) * 32768 / kPi);
        int octant = (uint16_t)(-seen - actor.heading + 0x1000) >> 13;
        int direction = octant * anim.directions >> 3;
        bool mirrored = false;
        if (anim.mirror && direction > 4) {
            direction = 8 - direction;
            mirrored = true;
        }
        int index = (uint8_t)((actor.timer >> 8) + direction * anim.frames);
        if (index >= (int)frames.size())
            index = 0;
        int frame = frames[index];
        if (frame >= set->second->getFrameCount())
            continue;

        const SpriteSet::Frame &f = set->second->getFrame(frame);
        SpriteDraw sprite;
        sprite.x = actor.x;
        sprite.y = actor.y;
        sprite.bottom = actor.z + fromFixed(actor.data.drawOffset);
        sprite.halfWidth = f.halfWidth * fromFixed(actor.data.scale);
        sprite.height = fromFixed(actor.data.height);
        sprite.surface = &f.surface;
        sprite.mirrored = mirrored;
        // Lit like the floor underfoot (its light is copied each frame).
        sprite.light = floor >= 0 ? flats[floor].light : actor.data.light;
        sprite.fullbright = (actor.data.flags & kFlagFullbright) != 0;
        const uint32_t seeThrough = LevelFile::kFlagTranslucent | LevelFile::kFlagColumnTests;
        sprite.translucent = (actor.data.flags & seeThrough) == seeThrough;
        sprite.object = (uint16_t)(actor.data.objectId | kObjectType);
        sprites.push_back(sprite);
    }
}

} // namespace reluva::stgenerations
