// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The map's enemies: characters with a CBorgAI, a state machine whose
// states each hold a tree of small AIs (docs/engines/stawayteam/systems/
// maps.md). Their patrols, their sight and their turning on what they see
// are here; the program's own AI code isn't traced yet, so how each piece
// acts is read from the maps' fields.

#include "engines/stawayteam/ui/layout.h"
#include "engines/stawayteam/ui/map_screen.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace reluva::stawayteam {

namespace {

constexpr float kPi = 3.14159265f;
// An enemy that's lost sight of its target this long goes back to its
// patrol (inferred).
constexpr float kForgetAfter = 5.0f;
// Seen only showing more than this much of themselves (0x4960d0).
constexpr float kSeenOpacity = 0.35f;
// Its target this near, as a part of its weapon's range, one that runs
// away does, this long (0x42b0ad, 0x42b0d1).
constexpr float kTooClose = 0.05f;
constexpr float kRunAwayTime = 3.0f;
// Its weapon short of a shot's energy for more than this, in seconds, it
// runs away for that long, this most (0x617400; 0x42b12a).
constexpr float kShortOfShot = 0.25f;
constexpr float kLongestShort = 1.0f;
// A retreat's reach as a part of its weapon's range, and its spot's
// nearest and farthest as parts of that (0x617400, 0x6296f0, 0x614320).
constexpr float kRetreatPart = 0.25f;
constexpr float kRetreatNearest = 0.8333333f;
constexpr float kRetreatFarthest = 1.2f;
// How fast an enemy turns when its AI doesn't say, in degrees a second
// (inferred).
constexpr float kTurnSpeed = 360.0f;
// A walk's speed for a model without one, as the officers'.
constexpr float kDefaultWalk = 36.4f;

float number(const TextObject *o, const char *key, float fallback = 0.0f) {
    return o ? float(o->number(key).value_or(fallback)) : fallback;
}

// The angle, as Facing Angle counts it, of the way from (0, 0) to (dx, dy):
// 0 facing left, turning towards the viewer (the models' directions).
float angleOf(float dx, float dy) {
    return std::atan2(dy, -dx);
}

float wrap(float a) {
    while (a > kPi)
        a -= 2 * kPi;
    while (a < -kPi)
        a += 2 * kPi;
    return a;
}

} // namespace

void MapScreen::setupEnemies() {
    for (size_t i = 0; i < _map.parts.size(); ++i) {
        const MapPart &p = _map.parts[i];
        if (p.model.empty())
            continue;
        for (const TextObject *ai : p.activities)
            if (ai->className == "CBorgAI") {
                // Its categories, its model's Default Entity's where the
                // map leaves them out, Enemy without either (inferred).
                if (p.category.empty()) {
                    const BinaryObject *model = modelObject(p.model);
                    const TextObject *entity = model ? model->object.object("Default Entity") : nullptr;
                    _map.parts[i].category = entity ? readText(entity, "Category") : std::string();
                    if (_map.parts[i].category.empty())
                        _map.parts[i].category = "Enemy";
                }
                if (p.deathType.empty() || p.deathType == "!None") {
                    const BinaryObject *model = modelObject(p.model);
                    const TextObject *entity = model ? model->object.object("Default Entity") : nullptr;
                    if (entity)
                        _map.parts[i].deathType = readText(entity, "Default Death Type");
                }
                Enemy e;
                e.part = i;
                e.settled = _map.parts[i].settled;
                e.ai = ai;
                e.x = p.x;
                e.y = p.y;
                e.angle = p.angle;
                e.state = stateKey(readText(ai, "Current State"));
                const TextObject *targeting = ai->object("Targeting");
                e.validTargets = targeting ? readText(targeting, "Valid Targets") : std::string("Player,Team Member");
                for (const TextObject *other : p.activities)
                    if (other->className == "CShieldAI")
                        e.shield = other;
                _enemyOfPart[i] = _enemies.size();
                _enemies.push_back(std::move(e));
                break;
            }
    }
}

bool MapScreen::seesPoint(const Enemy &e, float x, float y) const {
    // Nearer than its Max Distance (300 unless it says, 0x5dd316) and
    // within half its Vision Cone either side of its facing, a cone of 0
    // or 360 degrees or more seeing all round (0x4fe873); nothing standing
    // full height between (inferred: the program's finding has no such
    // test of its own).
    const float dx = x - e.x;
    const float dy = y - e.y;
    const float reach = number(e.ai, "Max Distance", kSightDefault);
    if (dx * dx + dy * dy >= reach * reach)
        return false;
    const float half = number(e.ai, "Vision Cone", 125.0f) / 2.0f * kPi / 180.0f;
    if (half > 0.0f && half < kPi && std::abs(wrap(angleOf(dx, dy) - e.angle)) > half)
        return false;
    return clearSight(e.x, e.y, x, y);
}

bool MapScreen::sees(const Enemy &e, const Officer &o) const {
    // Nor one faded to 0.35 or less, as a cloaking band fades them, the
    // enemies' sight (0x434e90) asking that through 0x497e10 (0x4960d0);
    // nor the team at all by one whose Valid Targets don't name them: the
    // officers are Player and Team Member (inferred).
    if (o.down() || o.opacity <= kSeenOpacity || !inCategory("Player,Team Member", e.validTargets))
        return false;
    return seesPoint(e, o.x, o.y);
}

bool MapScreen::inView(const Enemy &e, const Officer &o) const {
    if (o.opacity <= kSeenOpacity)
        return false;
    const float half = number(e.ai, "Vision Cone", 125.0f) / 2.0f * kPi / 180.0f;
    if (half > 0.0f && half < kPi && std::abs(wrap(angleOf(o.x - e.x, o.y - e.y) - e.angle)) > half)
        return false;
    return clearSight(e.x, e.y, o.x, o.y);
}

int MapScreen::closestSeen(const Enemy &e) const {
    // Of those it sees, the nearest (CEntityBehaviorStateSetClosestTarget,
    // 0x4fe710).
    int best = -1;
    float bestDistance = 0.0f;
    for (size_t i = 0; i < _officers.size(); ++i) {
        const Officer &o = _officers[i];
        const float distance = std::hypot(o.x - e.x, o.y - e.y);
        if (sees(e, o) && (best < 0 || distance < bestDistance)) {
            best = int(i);
            bestDistance = distance;
        }
    }
    return best;
}

bool MapScreen::turnTo(Enemy &e, float angle, float degreesPerSecond, float seconds) {
    const float diff = wrap(angle - e.angle);
    const float step = degreesPerSecond * kPi / 180.0f * seconds;
    if (std::abs(diff) <= step) {
        e.angle = angle;
        return true;
    }
    e.angle += diff > 0 ? step : -step;
    return false;
}

bool MapScreen::walkTo(Enemy &e, float x, float y, float ratio, float seconds, bool running) {
    const float dx = x - e.x;
    const float dy = y - e.y;
    const float distance = std::hypot(dx, dy);
    if (distance < 1.0f)
        return true;
    e.angle = angleOf(dx, dy);
    const Animation &walking = animation(_map.parts[e.part].model, running ? "Run" : "Walk");
    const int direction = walking.direction(e.angle);
    // As far as its frames go in the time, shown at their rate times its
    // play ratio (the entity's play speed, 0x449fb9).
    const float play = ratio > 0.0f ? ratio : 1.0f;
    float used = seconds;
    float step = walking.walk(direction, e.travelled, seconds, walking.fps * play, distance, used);
    if (step < 0.0f) {
        step = std::min(distance, kDefaultWalk * play * seconds);
        e.travelled = 0.0f;
    }
    // Through anyone in its way, as the officers go (0x497c60); one that
    // Tries To Collide is put back where it was if its step meets
    // something that collides, a closed door too (0x497c0f, 0x4f0089).
    // The way its direction's frames go (0x5512e0); the last step onto
    // the place itself (inferred).
    float wx = dx / distance;
    float wy = dy / distance;
    if (step < distance)
        walking.cycleWay(direction, wx, wy);
    const float nx = e.x + wx * step;
    const float ny = e.y + wy * step;
    if (_map.parts[e.part].triesToCollide && blockedAt(e.part, nx, ny)) {
        e.walking = false;
        return false;
    }
    e.x = nx;
    e.y = ny;
    e.walking = true;
    e.running = running;
    return step >= distance;
}

void MapScreen::forget(Enemy &e, const TextObject *ai) {
    if (!ai)
        return;
    e.progress.erase(ai);
    for (const TextField &f : ai->fields)
        if (f.object)
            forget(e, f.object.get());
}

bool MapScreen::runAI(Enemy &e, const TextObject *ai, float seconds) {
    if (!ai)
        return true;
    const std::string &kind = ai->className;
    Enemy::Progress &p = e.progress[ai];
    if (kind == "CSeriesAI") {
        // Its AIs one after another, from the first again after the last.
        const TextObject *list = ai->object("AIs");
        const auto items = list ? list->items() : std::vector<const TextField *>{};
        if (items.empty())
            return true;
        if (p.index >= items.size())
            p.index = 0;
        const TextObject *child = items[p.index]->object.get();
        if (runAI(e, child, seconds)) {
            forget(e, child);
            e.progress[ai].index++;
        }
        return false;
    }
    if (kind == "CLimitedTimeAI") {
        // Its What To Do, for Max Time.
        p.time += seconds;
        const float limit = number(ai, "Max Time");
        runAI(e, ai->object("What To Do"), seconds);
        return e.progress[ai].time >= limit;
    }
    if (kind == "CIdleAI") {
        p.time += seconds;
        return p.time >= number(ai->object("Idle Behavior"), "Num Seconds", 1.0f);
    }
    if (kind == "CScanAreaAI") {
        // From Starting Angle to Ending Angle, in degrees about the way it
        // faced as it began, at Turning Speed degrees a second (inferred).
        if (!p.begun) {
            p.begun = true;
            const float start = number(ai, "Starting Facing Angle", -999.0f);
            const float base = start <= -999.0f ? e.angle : start;
            p.from = base + number(ai, "Starting Angle") * kPi / 180.0f;
            p.to = base + number(ai, "Ending Angle") * kPi / 180.0f;
            p.index = 0;
        }
        const float speed = number(ai, "Turning Speed", 10.0f);
        if (p.index == 0) {
            if (turnTo(e, p.from, speed, seconds))
                e.progress[ai].index = 1;
            return false;
        }
        return turnTo(e, p.to, speed, seconds);
    }
    if (kind == "CFollowPathAI") {
        // Along the corners of the polygon named its Path Poly, as its
        // Order says, at its Moving's Play Ratio (inferred: straight from
        // corner to corner).
        if (!p.begun) {
            p.begun = true;
            for (size_t i : partsNamed(readText(ai, "Path Poly")))
                if (!_map.parts[i].polygon.empty()) {
                    p.points = _map.parts[i].polygon;
                    break;
                }
            p.index = 0;
            p.step = 1;
        }
        if (p.points.empty())
            return true;
        const std::string order = readText(ai, "Order");
        const float ratio = number(ai->object("Moving"), "Play Ratio", 1.0f);
        const auto [x, y] = p.points[p.index];
        if (!walkTo(e, x, y, ratio, seconds))
            return false;
        Enemy::Progress &q = e.progress[ai];
        const int last = int(q.points.size()) - 1;
        const int next = int(q.index) + q.step;
        if (next >= 0 && next <= last) {
            q.index = size_t(next);
            return false;
        }
        if (order == "Loop") {
            q.index = 0;
            return false;
        }
        if (order == "Back and forth" || order == "Forth and back") {
            q.step = -q.step;
            q.index = size_t(int(q.index) + q.step);
            return false;
        }
        return true; // Go To End
    }
    if (kind == "CRechargeHealthAI") {
        // Its Hit Points back a second, the damage taken going no lower
        // than none (0x43c610, 0x4951f0); it's never done of itself.
        e.health = std::min(e.maxHealth, e.health + number(ai, "Hit Points") * seconds);
        return e.health >= e.maxHealth;
    }
    if (kind == "CGoForwardAI") {
        // Its Dist to Move the way it faces, then its Done action.
        if (!p.begun) {
            p.begun = true;
            const float d = number(ai, "Dist to Move");
            p.points = {{e.x - std::cos(e.angle) * d, e.y + std::sin(e.angle) * d}};
        }
        if (!walkTo(e, p.points[0].first, p.points[0].second, 1.0f, seconds))
            return false;
        runAction(ai->object("Done action"), -1, int(e.part));
        return true;
    }
    if (kind == "CActionAI") {
        runAction(ai->object("Action"), -1, int(e.part));
        return true;
    }
    // The rest (going home, waiting at random...) aren't there yet: done
    // at once.
    return true;
}

bool MapScreen::runs(const Enemy &e, const char *state) const {
    const TextObject *s = e.ai->object(state);
    return s && readText(s, "Move Mode") == "Run";
}

bool MapScreen::followRoute(Enemy &e, float seconds, bool running) {
    while (!e.route.empty()) {
        const auto [x, y] = e.route.front();
        if (!walkTo(e, x, y, 1.0f, seconds, running))
            return false;
        e.route.erase(e.route.begin());
    }
    return true;
}

void MapScreen::placeOf(size_t part, float &x, float &y) const {
    const auto enemy = _enemyOfPart.find(part);
    x = enemy != _enemyOfPart.end() ? _enemies[enemy->second].x : _map.parts[part].x;
    y = enemy != _enemyOfPart.end() ? _enemies[enemy->second].y : _map.parts[part].y;
}

void MapScreen::gotATarget(Enemy &e) {
    // Its Acquired a Target, mostly Attack, with a target alive, in its
    // cone and in sight, and its reaction time over; else its Default
    // (0x42e1d0).
    const TextObject *got = e.ai->object("GotATarget");
    const bool acquired = e.target >= 0 && size_t(e.target) < _officers.size() &&
                          sees(e, _officers[size_t(e.target)]) && e.reaction <= 0.0f;
    std::string next = stateKey(got ? readText(got, acquired ? "Acquired a Target" : "Default") : std::string());
    if (next.empty())
        next = acquired ? "Attack" : "Patrol";
    if (next == "Attack" && acquired) {
        attackOfficer(e, e.target);
        return;
    }
    e.target = -1;
    if (next == "Investigate")
        investigate(e, e.seenX, e.seenY);
    else
        setState(e, next == "Alert" || next == "Attack" ? "Patrol" : next);
}

void MapScreen::attackOfficer(Enemy &e, int officer) {
    const Officer &o = _officers[size_t(officer)];
    e.target = officer;
    e.unseen = 0.0f;
    e.seenX = o.x;
    e.seenY = o.y;
    e.route.clear();
    setState(e, "Attack");
}

void MapScreen::maybeRetreat(Enemy &e) {
    // Hit, its health's whole percent at most its Retreat At Percent, and
    // not retreating already, it retreats (0x434477): one whose Retreat AI
    // goes somewhere (a CGoToAI's Destination: 50 in all the maps, to an
    // alarm or a security panel) runs there; one with none (the rest)
    // stands, doing nothing (0x43014f).
    const int percent = int(e.ai->integer("Retreat At Percent").value_or(0));
    if (percent <= 0 || e.state == "Retreat" || e.maxHealth <= 0.0f || int(e.health / e.maxHealth * 100.0f) > percent)
        return;
    const TextObject *retreat = e.ai->object("Retreat");
    const TextObject *goTo = retreat ? retreat->object("Retreat AI") : nullptr;
    e.route.clear();
    e.walking = false;
    e.retreated = false;
    if (goTo && goTo->className == "CGoToAI") {
        const std::vector<size_t> places = partsNamed(readText(goTo, "Destination"), int(e.part));
        if (!places.empty()) {
            float x = 0.0f;
            float y = 0.0f;
            placeOf(places.front(), x, y);
            e.route = route(e, x, y);
        }
    }
    setState(e, "Retreat");
}

std::string MapScreen::stateKey(const std::string &name) {
    // The states' names as the transitions and choices write them ("Run
    // Away", "Investigate Dead Body") and as CBorgAI's fields are named
    // ("RunAway"); "Got a Target" and "Lost a Target" are GotATarget's
    // and LostATarget's (0x6379c8).
    std::string key;
    bool up = true;
    for (const char c : name) {
        if (c == ' ') {
            up = true;
            continue;
        }
        key += up ? char(std::toupper(static_cast<unsigned char>(c))) : c;
        up = false;
    }
    return key;
}

void MapScreen::setState(Enemy &e, const std::string &to) {
    // The state left's End Action, the transitions from it to the new one,
    // then the new one's Start Action (0x4348d9): the enemy their $Trigger,
    // and the states' actions' instigator too (0x42b8da, 0x426978); the
    // transitions', its target, or itself without one (0x426561). A
    // transition fires Every Time, or once: First Time for good, Once Per
    // New Target till the target changes (0x426550, 0x4352d9).
    const std::string from = e.state;
    const std::string next = stateKey(to);
    if (from == next)
        return;
    const int self = int(e.part);
    const int itself = kEnemyInstigator - int(&e - _enemies.data());
    if (const TextObject *old = e.ai->object(from))
        runAction(old->object("End Action"), itself, self);
    e.state = next;
    e.stateTime = 0.0f;
    const TextObject *holder = e.ai->object("Transitions");
    const TextObject *list = holder ? holder->object("Transition") : nullptr;
    for (const TextField *f : list ? list->items() : std::vector<const TextField *>{}) {
        const TextObject *t = f->object.get();
        if (!t)
            continue;
        const std::string tFrom = stateKey(readText(t, "From State"));
        const std::string tTo = stateKey(readText(t, "To State"));
        if ((tFrom != "Any" && tFrom != from) || (tTo != "Any" && tTo != next))
            continue;
        const std::string when = readText(t, "Trigger");
        auto &fired = e.transitions[t];
        if (when == "First Time" && fired.first)
            continue;
        if (when == "Once Per New Target" && fired.first && fired.second == e.target)
            continue;
        fired = {true, e.target};
        runAction(t->object("Action"), e.target >= 0 ? e.target : itself, self);
        if (e.state != next)
            return;
    }
    if (const TextObject *now = e.ai->object(next))
        runAction(now->object("Start Action"), itself, self);
}

bool MapScreen::runEnemyAction(const TextObject *action, int instigator, int trigger) {
    const std::string &kind = action->className;
    const auto enemiesNamed = [&](const char *key) {
        std::vector<Enemy *> out;
        const int who = whoNamed(readText(action, key), instigator, trigger);
        if (who <= kEnemyInstigator && size_t(kEnemyInstigator - who) < _enemies.size()) {
            Enemy &e = _enemies[size_t(kEnemyInstigator - who)];
            if (e.alive())
                out.push_back(&e);
            return out;
        }
        for (size_t part : partsNamed(readText(action, key), trigger)) {
            const auto it = _enemyOfPart.find(part);
            if (it != _enemyOfPart.end() && _enemies[it->second].alive())
                out.push_back(&_enemies[it->second]);
        }
        return out;
    };
    if (kind == "CSendDeactivateAIMessage" || kind == "CSendActivateAIMessage") {
        // Deactivated, one with an Inactive state shuts down: its
        // PlaySequenceBehavior's Sequence once, then still; activated, it
        // plays it again and goes back to the state it was in (0x42c4b0,
        // 0x42c447: its Last State).
        for (Enemy *e : enemiesNamed("Target")) {
            const TextObject *inactive = e->ai->object("Inactive");
            if (!inactive)
                continue;
            const TextObject *play = inactive->object("PlaySequenceBehavior");
            const std::string sequence = play ? readText(play, "Sequence") : std::string();
            e->onceSequence = sequence.empty() || sequence == "Idle" ? std::string() : sequence;
            e->onceTime = 0.0f;
            if (kind == "CSendDeactivateAIMessage" && e->state != "Inactive") {
                e->beforeInactive = e->state;
                e->target = -1;
                e->foe = -1;
                e->route.clear();
                e->walking = false;
                setState(*e, "Inactive");
            } else if (kind == "CSendActivateAIMessage" && e->state == "Inactive") {
                e->onceSequence.clear();
                setState(*e, e->beforeInactive.empty() ? "Patrol" : e->beforeInactive);
            }
        }
        return true;
    }
    if (kind == "CForceStateChangeAction") {
        // Its Target put in its State: Investigate looking about where it
        // is (inferred).
        const std::string state = stateKey(readText(action, "State"));
        for (Enemy *e : enemiesNamed("Target")) {
            e->target = -1;
            e->foe = -1;
            e->route.clear();
            if (state == "Investigate")
                investigate(*e, e->x, e->y);
            else if (!state.empty())
                setState(*e, state);
        }
        return true;
    }
    if (kind == "CSendInvestigateHereMessageAction") {
        // Its Target goes to look where the instigator is (0x578060): the
        // officer who set it off, or else the part whose action it is; one
        // already after the team carries on (inferred).
        float x = 0.0f;
        float y = 0.0f;
        if (instigator >= 0) {
            x = _officers[size_t(instigator)].x;
            y = _officers[size_t(instigator)].y;
        } else if (_hasHere) {
            // Where a radius's action is about (a decoy's).
            x = _hereX;
            y = _hereY;
        } else if (trigger >= 0) {
            placeOf(size_t(trigger), x, y);
        } else {
            return true;
        }
        for (Enemy *e : enemiesNamed("Target"))
            if (!e->down() && (e->state == "Patrol" || e->state == "Investigate"))
                investigate(*e, x, y);
    } else if (kind == "CSendInvestigateTargetMessageAction") {
        // Its Target is given the instigator as its target (0x578b70), and
        // goes after them (inferred).
        if (instigator >= 0 && _officers[size_t(instigator)].alive())
            for (Enemy *e : enemiesNamed("Target"))
                if (!e->down() && e->state != "Retreat")
                    attackOfficer(*e, instigator);
    } else if (kind == "CSetPatrolAIAction") {
        // Its Target patrols by its Patrol AI from now.
        for (Enemy *e : enemiesNamed("Target")) {
            e->patrolAI = action->object("Patrol AI");
            forget(*e, e->patrolAI);
        }
    } else if (kind == "CBorgSetTargetTypeAction" || kind == "CSetTargetTypeAction") {
        // Whom its Entity Name will target from now (an empty list: no
        // one).
        for (Enemy *e : enemiesNamed("Entity Name"))
            e->validTargets = readText(action, "Valid Targets");
    } else if (kind == "CDoOnAllWithinRadiusAction") {
        // Its Touching Action for each active one of Who To Affect's
        // categories (0x5758f2), the dead too, within an oval Radius across
        // and Radius times Y To X Ratio up and down about its Target Entity
        // (0x576750, 0x5767d0), named its Name Filter in full, any case
        // (0x575800), that isn't Who To Ignore; each its $Instigator.
        const std::string centre = readText(action, "Target Entity");
        float cx = 0.0f;
        float cy = 0.0f;
        std::string lowered = centre;
        for (char &c : lowered)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        if (lowered == "$instigator" && instigator >= 0) {
            cx = _officers[size_t(instigator)].x;
            cy = _officers[size_t(instigator)].y;
        } else if (!placeNamed(lowered == "$instigator" ? "$trigger" : centre, -1, trigger, cx, cy)) {
            return true;
        }
        const float rx = number(action, "Radius");
        const float ry = rx * number(action, "Y To X Ratio", 1.0f);
        std::string filter = readText(action, "Name Filter");
        for (char &c : filter)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        const std::vector<size_t> ignored = partsNamed(readText(action, "Who To Ignore"), trigger);
        // Who To Affect, if it says, by their categories (an EM pulse's
        // Electronic Equipment); its $instigator the one it's on, and the
        // place it's about where it's done (inferred).
        const std::string affects = readText(action, "Who To Affect");
        const bool hadHere = _hasHere;
        const float hereX = _hereX;
        const float hereY = _hereY;
        runAction(action->object("Start Action"), instigator, trigger);
        const auto touch = [&](int who, int part) {
            _hasTouched = true;
            _touched = who;
            _hasHere = true;
            _hereX = cx;
            _hereY = cy;
            runAction(action->object("Touching Action"), instigator, part);
            _hasTouched = false;
            _hasHere = hadHere;
            _hereX = hereX;
            _hereY = hereY;
        };
        if (inCategory(affects, "Player,Team Member"))
            for (size_t i = 0; i < _officers.size(); ++i) {
                const Officer &o = _officers[i];
                const float dx = (o.x - cx) / rx;
                const float dy = (o.y - cy) / ry;
                if (o.present && o.alive() && rx > 0.0f && ry > 0.0f && dx * dx + dy * dy <= 1.0f)
                    touch(int(i), trigger);
            }
        for (size_t k = 0; k < _enemies.size(); ++k) {
            const Enemy &e = _enemies[k];
            if (!_partStates[e.part].active || _partStates[e.part].deleted || rx <= 0.0f || ry <= 0.0f)
                continue;
            if (!affects.empty() && !inCategory(_map.parts[e.part].category, affects))
                continue;
            std::string name = _map.parts[e.part].name;
            for (char &c : name)
                c = char(std::tolower(static_cast<unsigned char>(c)));
            if ((!filter.empty() && name != filter) ||
                std::find(ignored.begin(), ignored.end(), e.part) != ignored.end())
                continue;
            const float dx = (e.x - cx) / rx;
            const float dy = (e.y - cy) / ry;
            if (dx * dx + dy * dy <= 1.0f)
                touch(kEnemyInstigator - int(k), int(e.part));
        }
        // And every other part of those categories (0x5758f2): the EM
        // pulse's Electronic Equipment that isn't an enemy, each its own
        // $Instigator.
        if (!affects.empty() && rx > 0.0f && ry > 0.0f)
            for (size_t part = 0; part < _map.parts.size(); ++part) {
                const MapPart &p = _map.parts[part];
                std::string partName = p.name;
                for (char &c : partName)
                    c = char(std::tolower(static_cast<unsigned char>(c)));
                if (_enemyOfPart.count(part) || !_partStates[part].active || _partStates[part].deleted ||
                    p.category.empty() || !inCategory(p.category, affects) ||
                    std::find(ignored.begin(), ignored.end(), part) != ignored.end() ||
                    (!filter.empty() && partName != filter))
                    continue;
                float px = 0.0f;
                float py = 0.0f;
                placeOf(part, px, py);
                const float dx = (px - cx) / rx;
                const float dy = (py - cy) / ry;
                if (dx * dx + dy * dy <= 1.0f)
                    touch(kNobody, int(part));
            }
        runAction(action->object("Done Action"), instigator, trigger);
    } else {
        return false;
    }
    return true;
}

void MapScreen::updateEnemies(float seconds) {
    // The dead bodies on the map now, of category Dead Bodies.
    std::vector<size_t> bodies;
    for (size_t part = 0; part < _map.parts.size(); ++part)
        if (!_map.parts[part].category.empty() && inCategory(_map.parts[part].category, "Dead Bodies"))
            bodies.push_back(part);
    for (Enemy &e : _enemies) {
        e.walking = false;
        e.running = false;
        // One taken over has no mind of its own (updateMeld).
        if (int(&e - _enemies.data()) == _melded)
            continue;
        const PartState &state = _partStates[e.part];
        if (!state.active || state.deleted || e.down())
            continue;
        // Beaming away, it does nothing.
        if (std::any_of(_warping.begin(), _warping.end(), [&](const Warp &w) { return w.part == e.part; }))
            continue;
        // Held to a task (a stasis field's, an EM pulse's wait), it does
        // nothing else.
        if (e.held > 0.0f) {
            if (e.held < kHeldForever)
                e.held = std::max(0.0f, e.held - seconds);
            continue;
        }
        e.stateTime += seconds;
        e.reaction = std::max(0.0f, e.reaction - seconds); // 0x434806
        const TextObject *states = e.ai;
        const int self = int(e.part);
        // Seeing an officer, a patrolling enemy is alerted: its Alert
        // state's Start Action, turning to them for Alert Time; then it
        // attacks (inferred: CBorgStateGotATarget's transitions aren't
        // traced).
        const bool calm =
            e.state == "Patrol" || e.state == "Investigate" || e.state == "Recharge" || e.state == "DoSomething";
        if (calm && _teamShown) {
            const int seen = closestSeen(e);
            float decoy = 0.0f;
            if (seen >= 0 && (closestDecoy(e, decoy) < 0 ||
                              decoy >= std::hypot(_officers[size_t(seen)].x - e.x, _officers[size_t(seen)].y - e.y))) {
                e.target = seen;
                e.unseen = 0.0f;
                e.route.clear();
                // On patrol, alerted (0x4297b5); looking about or doing
                // something, straight to Got a Target (0x42ed60, 0x4310ae).
                if (e.state == "Patrol" || e.state == "Recharge") {
                    setState(e, "Alert");
                } else {
                    e.seenX = _officers[size_t(seen)].x;
                    e.seenY = _officers[size_t(seen)].y;
                    gotATarget(e);
                }
            }
        }
        // Hearing the team, one on patrol goes to look, or looks again
        // where it hears them next; after looking about, it's back on
        // patrol where it left off (inferred).
        float heardX = 0.0f;
        float heardY = 0.0f;
        if ((e.state == "Patrol" || e.state == "Investigate") && hears(e, heardX, heardY) &&
            (e.state == "Patrol" || heardX != e.heardX || heardY != e.heardY))
            investigate(e, heardX, heardY);
        if (e.target < 0 && e.decoy < 0 && fightFoe(size_t(&e - _enemies.data()), seconds))
            continue;
        if ((e.decoy >= 0 || e.target < 0) && fightDecoy(e, seconds))
            continue;
        // Patrolling or alerted, a dead body seen (a part of Dead Bodies: a
        // Klingon's body, by its death behaviours), within its sight, not
        // in its list of those seen: its place taken as the one to go to,
        // the body put in the list (0x4295db, and Alert's 0x42a550), and
        // its GotATarget's Dead Body, mostly Investigate Dead Body, going
        // to look at it, its Start Action raising the alarm (0x42e318).
        if (e.state == "Patrol" || e.state == "Alert")
            for (const size_t part : bodies) {
                if (!shown(part) || part == e.part ||
                    std::find(e.bodiesSeen.begin(), e.bodiesSeen.end(), part) != e.bodiesSeen.end())
                    continue;
                float bx = 0.0f;
                float by = 0.0f;
                placeOf(part, bx, by);
                if (!seesPoint(e, bx, by))
                    continue;
                e.bodiesSeen.push_back(part);
                const TextObject *got = states->object("GotATarget");
                if (stateKey(got ? readText(got, "Dead Body") : std::string()) != "InvestigateDeadBody")
                    break;
                e.target = -1;
                e.heardX = bx;
                e.heardY = by;
                e.route = route(e, bx, by);
                e.looked = -1.0f;
                if (const TextObject *s = states->object("InvestigateDeadBody"))
                    forget(e, s->object("Investigate Dead Body AI"));
                setState(e, "InvestigateDeadBody");
                break;
            }
        if (e.state == "InvestigateDeadBody") {
            if (runInvestigate(e, seconds, "InvestigateDeadBody", "Investigate Dead Body AI"))
                setState(e, "Patrol");
            continue;
        }
        // Recharging (80 enemies start so): its Recharge AI, for its Time
        // To Recharge if it has one (-1: till it's disturbed), then End
        // Recharge (0x427e89); its AI, then its patrol (inferred).
        if (e.state == "Recharge") {
            const TextObject *recharge = states->object("Recharge");
            e.recharge += seconds;
            runAI(e, recharge ? recharge->object("Recharge AI") : nullptr, seconds);
            const float time = number(recharge, "Time To Recharge", -1.0f);
            if (time >= 0.0f && e.recharge >= time) {
                e.recharge = 0.0f;
                if (const TextObject *end = states->object("EndRecharge"))
                    forget(e, end->object("End Recharge AI"));
                setState(e, "EndRecharge");
            }
            continue;
        }
        if (e.state == "EndRecharge") {
            const TextObject *end = states->object("EndRecharge");
            if (runAI(e, end ? end->object("End Recharge AI") : nullptr, seconds))
                setState(e, "Patrol");
            continue;
        }
        // Running away from one too close, for its time, then back at them
        // (0x42b084).
        if (e.state == "RunAway") {
            e.runAway -= seconds;
            if (e.runAway <= 0.0f || e.route.empty() || followRoute(e, seconds, true))
                setState(e, e.target >= 0 ? "Attack" : "Patrol");
            continue;
        }
        if (e.state == "Investigate") {
            if (runInvestigate(e, seconds))
                setState(e, "Patrol");
            continue;
        }
        // Shut down (deactivated), it plays its sequence once and is
        // still, seeing nothing, till it's activated (0x42c410).
        if (e.state == "Inactive") {
            e.onceTime += seconds;
            continue;
        }
        // Doing something, its DoSomething AI: a repeat timer's Action
        // every Delay Between Actions, give or take Plus or Minus; it
        // still sees and hears as on patrol (inferred).
        if (e.state == "DoSomething") {
            const TextObject *doing = states->object("DoSomething");
            const TextObject *ai = doing ? doing->object("DoSomething AI") : nullptr;
            if (ai && ai->className == "CRepeatTimerTriggerAI") {
                if (e.doTimer < 0.0f)
                    e.doTimer = number(ai, "Delay Between Actions");
                e.doTimer -= seconds;
                if (e.doTimer <= 0.0f) {
                    _random = _random * 1103515245u + 12345u;
                    const float r = float((_random >> 16) & 0x7fff) / 32767.0f * 2.0f - 1.0f;
                    e.doTimer = std::max(0.05f, number(ai, "Delay Between Actions") + number(ai, "Plus or Minus") * r);
                    runAction(ai->object("Action"), -1, self);
                }
            } else {
                runAI(e, ai, seconds);
            }
            continue;
        }
        if (e.state == "Patrol") {
            const TextObject *patrol = states->object("Patrol");
            runAI(e, e.patrolAI ? e.patrolAI : patrol ? patrol->object("Patrol AI") : nullptr, seconds);
            continue;
        }
        if (e.state == "Retreat") {
            // Its Retreat AI while its health's at most its Retreat At
            // Percent, and nothing else: there, its GoTo's Done action, and it
            // waits; healed past it, it goes to look where its target was
            // (0x4300e0).
            const float percent = float(e.ai->integer("Retreat At Percent").value_or(0));
            if (int(e.health / std::max(e.maxHealth, 1.0f) * 100.0f) > int(percent)) {
                e.target = -1;
                e.retreated = false;
                investigate(e, e.seenX, e.seenY);
                continue;
            }
            if (!e.retreated && followRoute(e, seconds, runs(e, "Retreat"))) {
                const TextObject *retreat = states->object("Retreat");
                const TextObject *goTo = retreat ? retreat->object("Retreat AI") : nullptr;
                e.retreated = true;
                runAction(goTo ? goTo->object("Done action") : nullptr, e.target, self);
            }
            continue;
        }
        if ((e.state != "Alert" && e.state != "Attack" && e.state != "Pursue") || e.target < 0)
            continue;
        const Officer &o = _officers[size_t(e.target)];
        // Attacking and pursuing, the target's seen in its cone and in sight
        // at any distance (0x434e20, 0x434e90), as long as it's there and
        // not dead (0x4350c0).
        const bool valid = o.alive() && o.present && inCategory("Player,Team Member", e.validTargets);
        const bool seen = valid && inView(e, o);
        if (seen) {
            e.seenX = o.x;
            e.seenY = o.y;
            e.route.clear();
        }
        e.unseen = seen ? 0.0f : e.unseen + seconds;
        if (e.state == "Attack" || e.state == "Pursue") {
            // Not seen, or its reaction time not over: Lost a Target, whose
            // Had a Target (its place, where they were seen last) is mostly
            // Investigate (0x42afcc, 0x42b9a0, 0x42d970).
            if (!seen || e.reaction > 0.0f) {
                const TextObject *lost = states->object("LostATarget");
                const std::string next = stateKey(lost ? readText(lost, "Had a Target") : std::string());
                e.target = -1;
                e.route.clear();
                if (next == "Investigate")
                    investigate(e, e.seenX, e.seenY);
                else
                    setState(e, next.empty() || next == "Attack" || next == "Pursue" ? "Patrol" : next);
                continue;
            }
            // Further than its Max Distance it pursues them, at its Pursue's
            // pace, till they're within it again, then attacks (0x42b069,
            // 0x42ba78); one holding a retreat's place stays (inferred).
            const float sight = number(e.ai, "Max Distance", kSightDefault);
            const float distance = std::hypot(o.x - e.x, o.y - e.y);
            if (e.state == "Attack" && distance > sight && !e.retreated) {
                setState(e, "Pursue");
                continue;
            }
            if (e.state == "Pursue") {
                if (distance <= sight)
                    setState(e, "Attack");
                else
                    walkTo(e, o.x, o.y, 1.0f, seconds, runs(e, "Pursue"));
                continue;
            }
        }
        turnTo(e, angleOf(o.x - e.x, o.y - e.y), kTurnSpeed, seconds);
        // Alert Time over, its Next State (0x42a189): mostly Got a Target,
        // which goes to its Acquired a Target, mostly Attack, with a target
        // alive, in its cone and in sight, and its reaction time over; else
        // to its Default (0x42e1d0).
        const TextObject *alert = states->object("Alert");
        if (e.state == "Alert" && e.stateTime >= number(alert, "Alert Time", 2.0f)) {
            const std::string next = stateKey(alert ? readText(alert, "Next State") : std::string());
            if (next.empty() || next == "GotATarget") {
                gotATarget(e);
                if (e.state != "Attack")
                    continue;
            } else if (next == "Attack") {
                attackOfficer(e, e.target);
            } else if (next == "Investigate") {
                e.target = -1;
                investigate(e, e.seenX, e.seenY);
                continue;
            } else if (next != "Alert") {
                e.target = -1;
                setState(e, next);
                continue;
            }
        }
        // Attacking, it shoots what it sees in its weapon's range, aimed
        // within its Max Shoot Angle, each Reuse Delay; one it sees further
        // off it goes after, at its Pursue state's pace (inferred).
        if (e.state != "Attack" || !e.weapon || !seen)
            continue;
        const float toward = angleOf(o.x - e.x, o.y - e.y);
        const float distance = std::hypot(o.x - e.x, o.y - e.y);
        if (distance > e.weapon->range) {
            if (!e.retreated)
                walkTo(e, o.x, o.y, 1.0f, seconds, runs(e, "Pursue"));
            continue;
        }
        // Its target within a twentieth of its weapon's range, one that
        // Runs Away If Too Close does, three seconds (0x42b084). Its
        // CRunAwayRetreatAI's spot is a quarter of its weapon's range from
        // the target (0x430702), 0.83 to 1.2 times that, within 45 degrees
        // of straight away from them, on the ground unforeshortened; with
        // no way there, anywhere round them (0x5530f0, 0x5531a0).
        // Else, its weapon short of a shot's energy for more than a quarter
        // of a second, it runs away that long, a second at most (0x42b0e2,
        // 0x42aee0).
        float runFor = 0.0f;
        if (readText(e.ai, "Run Away If Too Close") == "1") {
            if (distance <= e.weapon->range * kTooClose)
                runFor = kRunAwayTime;
            else if (!hasShot(*e.weapon, e.energy) && e.weapon->energyRecharge > 0.0f &&
                     (e.weapon->energyUse - e.energy) / e.weapon->energyRecharge > kShortOfShot)
                runFor = std::min(kLongestShort, (e.weapon->energyUse - e.energy) / e.weapon->energyRecharge);
        }
        if (runFor > 0.0f) {
            const auto randomUnit = [&] {
                _random = _random * 1103515245u + 12345u;
                return float((_random >> 8) & 0xffffff) / float(0xffffff);
            };
            const float away = std::atan2((e.y - o.y) / ParticleSystem::kYToX, e.x - o.x);
            const float reach = e.weapon->range * kRetreatPart;
            for (const float spread : {kPi / 4.0f, kPi}) {
                const float a = away + (randomUnit() * 2.0f - 1.0f) * spread;
                const float r = reach * (kRetreatNearest + randomUnit() * (kRetreatFarthest - kRetreatNearest));
                e.route = route(e, o.x + std::cos(a) * r, o.y + std::sin(a) * r * ParticleSystem::kYToX);
                if (!e.route.empty())
                    break;
            }
            e.runAway = runFor;
            setState(e, "RunAway");
            continue;
        }
        const float off = std::abs(wrap(toward - e.angle)) * 180.0f / kPi;
        if (off <= e.weapon->maxShootAngle && e.reload <= 0.0f && hasShot(*e.weapon, e.energy)) {
            spendShot(*e.weapon, e.energy);
            e.reload = e.weapon->reuseDelay;
            shoot(*e.weapon, _map.parts[e.part].model, e.x, e.y, o.x, o.y,
                  kEnemyInstigator - int(&e - _enemies.data()));
            e.shooting = 1e-6f;
            const int self = kEnemyInstigator - int(&e - _enemies.data());
            if (e.weapon->dischargeAction == "CStrikeTargetAction")
                hurtOfficer(size_t(e.target), *e.weapon, e.stuns);
            else
                fireBolt(*e.weapon, e.x, e.y, o.x, o.y, self, self, e.stuns);
        }
    }
}

bool MapScreen::fightFoe(size_t self, float seconds) {
    // As one after an officer, by the same rules: the nearest other enemy
    // whose category its Valid Targets name seen from patrol (Mission 01's
    // freed Klingons, Electronic Equipment themselves, after the
    // Romulans), alerted for its Alert Time, then shot at in its weapon's
    // range, gone after out of it, and given up out of sight a while; one
    // shot back whose Valid Targets name the shooter (inferred: the
    // program's target finder looks among all of a Valid Targets', 0x4fe710).
    Enemy &e = _enemies[self];
    if (e.foe < 0) {
        const bool calm =
            e.state == "Patrol" || e.state == "Investigate" || e.state == "Recharge" || e.state == "DoSomething";
        if (!calm || !_teamShown)
            return false;
        int best = -1;
        float bestDistance = 0.0f;
        for (size_t j = 0; j < _enemies.size(); ++j) {
            const Enemy &f = _enemies[j];
            if (j == self || !f.alive() || !shown(f.part) || !inCategory(_map.parts[f.part].category, e.validTargets))
                continue;
            const float distance = std::hypot(f.x - e.x, f.y - e.y);
            if (seesPoint(e, f.x, f.y) && (best < 0 || distance < bestDistance)) {
                best = int(j);
                bestDistance = distance;
            }
        }
        if (best < 0)
            return false;
        e.foe = best;
        e.unseen = 0.0f;
        e.route.clear();
        setState(e, "Alert");
        return true;
    }
    Enemy &f = _enemies[size_t(e.foe)];
    const bool seen = f.alive() && shown(f.part) && seesPoint(e, f.x, f.y);
    e.unseen = seen ? 0.0f : e.unseen + seconds;
    if (!f.alive() || !shown(f.part) || e.unseen > kForgetAfter) {
        e.foe = -1;
        setState(e, "Patrol");
        return true;
    }
    const float toward = angleOf(f.x - e.x, f.y - e.y);
    turnTo(e, toward, kTurnSpeed, seconds);
    const TextObject *alert = e.ai->object("Alert");
    if (e.state == "Alert" && e.stateTime >= number(alert, "Alert Time", 2.0f))
        setState(e, "Attack");
    if (e.state != "Attack" || !e.weapon || !seen)
        return true;
    const float distance = std::hypot(f.x - e.x, f.y - e.y);
    if (distance > e.weapon->range) {
        walkTo(e, f.x, f.y, 1.0f, seconds, runs(e, "Pursue"));
        return true;
    }
    e.walking = false;
    const float off = std::abs(wrap(toward - e.angle)) * 180.0f / kPi;
    if (off > e.weapon->maxShootAngle || e.reload > 0.0f || !hasShot(*e.weapon, e.energy))
        return true;
    spendShot(*e.weapon, e.energy);
    e.reload = e.weapon->reuseDelay;
    shoot(*e.weapon, _map.parts[e.part].model, e.x, e.y, f.x, f.y, kEnemyInstigator - int(&e - _enemies.data()));
    e.shooting = 1e-6f;
    const size_t victim = size_t(e.foe);
    if (e.weapon->dischargeAction == "CStrikeTargetAction")
        hurtEnemy(victim, *e.weapon, -1, e.stuns);
    else
        fireBolt(*e.weapon, e.x, e.y, f.x, f.y, kEnemyInstigator - int(&e - _enemies.data()), -1, e.stuns);
    Enemy &v = _enemies[victim];
    if (int(victim) != _melded && v.alive() && v.foe < 0 && v.target < 0 &&
        inCategory(_map.parts[e.part].category, v.validTargets)) {
        v.foe = int(self);
        v.unseen = 0.0f;
        setState(v, "Attack");
    }
    return true;
}

} // namespace reluva::stawayteam
