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
                Enemy e;
                e.part = i;
                e.ai = ai;
                e.x = p.x;
                e.y = p.y;
                e.angle = p.angle;
                e.state = readText(ai, "Current State");
                _enemyOfPart[i] = _enemies.size();
                _enemies.push_back(std::move(e));
                break;
            }
    }
}

bool MapScreen::sees(const Enemy &e, const Officer &o) const {
    // Within its Max Distance and half its Vision Cone either side of its
    // facing, nothing standing full height between (CBorgSetClosestTarget,
    // inferred).
    if (o.down())
        return false;
    const float dx = o.x - e.x;
    const float dy = o.y - e.y;
    const float distance = std::hypot(dx, dy);
    if (distance > number(e.ai, "Max Distance", 375.0f))
        return false;
    const float off = std::abs(wrap(angleOf(dx, dy) - e.angle));
    if (off * 180.0f / kPi > number(e.ai, "Vision Cone", 125.0f) / 2.0f)
        return false;
    return clearSight(e.x, e.y, o.x, o.y);
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
    const float cycle = walking.cycleDistance(direction);
    const int per = walking.perDirection();
    float speed = cycle > 0.0f && per > 0 && walking.fps > 0.0f ? cycle * walking.fps / float(per) : kDefaultWalk;
    speed *= ratio > 0.0f ? ratio : 1.0f;
    const float step = std::min(distance, speed * seconds);
    e.x += dx / distance * step;
    e.y += dy / distance * step;
    e.walking = true;
    e.running = running;
    e.travelled = cycle > 0.0f ? std::fmod(e.travelled + step / (ratio > 0.0f ? ratio : 1.0f), cycle) : 0.0f;
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
    // The rest (going forward, home, waiting at random...) aren't there
    // yet: done at once.
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

void MapScreen::attackOfficer(Enemy &e, int officer) {
    const Officer &o = _officers[size_t(officer)];
    e.target = officer;
    e.unseen = 0.0f;
    e.seenX = o.x;
    e.seenY = o.y;
    e.route.clear();
    if (e.state == "Attack")
        return;
    e.state = "Attack";
    e.stateTime = 0.0f;
    if (const TextObject *attack = e.ai->object("Attack"))
        runAction(attack->object("Start Action"), officer, int(e.part));
}

void MapScreen::maybeRetreat(Enemy &e) {
    // Hurt to its Retreat At Percent of its health, one whose Retreat AI
    // goes somewhere (a CGoToAI's Destination: 48 in all the maps, to an
    // alarm or a security panel) runs there, once (inferred).
    const int percent = int(e.ai->integer("Retreat At Percent").value_or(0));
    const TextObject *retreat = e.ai->object("Retreat");
    const TextObject *goTo = retreat ? retreat->object("Retreat AI") : nullptr;
    if (percent <= 0 || e.retreated || e.state == "Retreat" || !goTo || goTo->className != "CGoToAI" ||
        e.health > e.maxHealth * float(percent) / 100.0f)
        return;
    const std::vector<size_t> places = partsNamed(readText(goTo, "Destination"), int(e.part));
    if (places.empty())
        return;
    float x = 0.0f;
    float y = 0.0f;
    placeOf(places.front(), x, y);
    e.route =
        _wayPoints.points.empty() ? std::vector<std::pair<float, float>>{{x, y}} : _wayPoints.path(e.x, e.y, x, y);
    e.state = "Retreat";
    e.stateTime = 0.0f;
    runAction(retreat->object("Start Action"), e.target, int(e.part));
}

bool MapScreen::runEnemyAction(const TextObject *action, int instigator, int trigger) {
    const std::string &kind = action->className;
    const auto enemiesNamed = [&](const char *key) {
        std::vector<Enemy *> out;
        for (size_t part : partsNamed(readText(action, key), trigger)) {
            const auto it = _enemyOfPart.find(part);
            if (it != _enemyOfPart.end() && _enemies[it->second].alive())
                out.push_back(&_enemies[it->second]);
        }
        return out;
    };
    if (kind == "CSendInvestigateHereMessageAction") {
        // Its Target goes to look where the instigator is (0x578060): the
        // officer who set it off, or else the part whose action it is; one
        // already after the team carries on (inferred).
        float x = 0.0f;
        float y = 0.0f;
        if (instigator >= 0) {
            x = _officers[size_t(instigator)].x;
            y = _officers[size_t(instigator)].y;
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
    } else if (kind == "CDoOnAllWithinRadiusAction") {
        // Its Touching Action for each enemy in an oval Radius across and
        // Radius times Y To X Ratio up and down about its Target Entity,
        // whose name has its Name Filter in it, that isn't Who To Ignore;
        // each the $Trigger of it (inferred: how it chooses isn't traced).
        const std::string centre = readText(action, "Target Entity");
        float cx = 0.0f;
        float cy = 0.0f;
        std::string lowered = centre;
        for (char &c : lowered)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        if (lowered == "$instigator" && instigator >= 0) {
            cx = _officers[size_t(instigator)].x;
            cy = _officers[size_t(instigator)].y;
        } else {
            const std::vector<size_t> at = partsNamed(lowered == "$instigator" ? "$trigger" : centre, trigger);
            if (at.empty())
                return true;
            placeOf(at.front(), cx, cy);
        }
        const float rx = number(action, "Radius");
        const float ry = rx * number(action, "Y To X Ratio", 1.0f);
        std::string filter = readText(action, "Name Filter");
        for (char &c : filter)
            c = char(std::tolower(static_cast<unsigned char>(c)));
        const std::vector<size_t> ignored = partsNamed(readText(action, "Who To Ignore"), trigger);
        runAction(action->object("Start Action"), instigator, trigger);
        for (const Enemy &e : _enemies) {
            if (!e.alive() || !_partStates[e.part].active || _partStates[e.part].deleted || rx <= 0.0f || ry <= 0.0f)
                continue;
            std::string name = _map.parts[e.part].name;
            for (char &c : name)
                c = char(std::tolower(static_cast<unsigned char>(c)));
            if ((!filter.empty() && name.find(filter) == std::string::npos) ||
                std::find(ignored.begin(), ignored.end(), e.part) != ignored.end())
                continue;
            const float dx = (e.x - cx) / rx;
            const float dy = (e.y - cy) / ry;
            if (dx * dx + dy * dy <= 1.0f)
                runAction(action->object("Touching Action"), instigator, int(e.part));
        }
        runAction(action->object("Done Action"), instigator, trigger);
    } else {
        return false;
    }
    return true;
}

void MapScreen::updateEnemies(float seconds) {
    for (Enemy &e : _enemies) {
        e.walking = false;
        e.running = false;
        const PartState &state = _partStates[e.part];
        if (!state.active || state.deleted || e.down())
            continue;
        e.stateTime += seconds;
        const TextObject *states = e.ai;
        const int self = int(e.part);
        // Seeing an officer, a patrolling enemy is alerted: its Alert
        // state's Start Action, turning to them for Alert Time; then it
        // attacks (inferred: CBorgStateGotATarget's transitions aren't
        // traced).
        if ((e.state == "Patrol" || e.state == "Investigate") && _teamShown) {
            for (size_t i = 0; i < _officers.size(); ++i)
                if (sees(e, _officers[i])) {
                    e.target = int(i);
                    e.state = "Alert";
                    e.stateTime = 0.0f;
                    e.unseen = 0.0f;
                    e.route.clear();
                    if (const TextObject *alert = states->object("Alert"))
                        runAction(alert->object("Start Action"), int(i), self);
                    break;
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
        if (e.state == "Investigate") {
            if (runInvestigate(e, seconds)) {
                if (const TextObject *s = states->object("Investigate"))
                    runAction(s->object("End Action"), -1, self);
                e.state = "Patrol";
                e.stateTime = 0.0f;
            }
            continue;
        }
        if (e.state == "Patrol") {
            if (const TextObject *patrol = states->object("Patrol"))
                runAI(e, patrol->object("Patrol AI"), seconds);
            continue;
        }
        if (e.state == "Retreat") {
            // There, its GoTo's Done action, and it holds the place, still
            // fighting (inferred).
            if (followRoute(e, seconds, runs(e, "Retreat"))) {
                const TextObject *retreat = states->object("Retreat");
                const TextObject *goTo = retreat ? retreat->object("Retreat AI") : nullptr;
                e.retreated = true;
                e.state = "Attack";
                e.stateTime = 0.0f;
                runAction(goTo ? goTo->object("Done action") : nullptr, e.target, self);
            }
            continue;
        }
        if ((e.state != "Alert" && e.state != "Attack") || e.target < 0)
            continue;
        const Officer &o = _officers[size_t(e.target)];
        const bool seen = sees(e, o);
        if (seen) {
            e.seenX = o.x;
            e.seenY = o.y;
            e.route.clear();
        }
        e.unseen = seen ? 0.0f : e.unseen + seconds;
        // Out of sight, an attacker whose Pursue AI chases goes after them
        // by the waypoints to where it saw them last; it gives up there, or
        // with nowhere to go, once it's not seen them for a while
        // (inferred).
        const TextObject *pursue = states->object("Pursue");
        const TextObject *chase = pursue ? pursue->object("Pursue AI") : nullptr;
        const bool chases = chase && chase->className == "CChasePursueAI" && !e.retreated;
        bool arrived = true;
        if (!seen && e.state == "Attack" && chases) {
            if (e.route.empty() && std::hypot(e.seenX - e.x, e.seenY - e.y) > 1.0f)
                e.route = _wayPoints.points.empty() ? std::vector<std::pair<float, float>>{{e.seenX, e.seenY}}
                                                    : _wayPoints.path(e.x, e.y, e.seenX, e.seenY);
            arrived = followRoute(e, seconds, runs(e, "Pursue"));
            if (arrived) {
                e.seenX = e.x;
                e.seenY = e.y;
            }
        } else {
            turnTo(e, angleOf(o.x - e.x, o.y - e.y), kTurnSpeed, seconds);
        }
        if ((e.unseen > kForgetAfter && arrived) || o.down()) {
            // Back to its patrol, where it left off.
            e.state = "Patrol";
            e.target = -1;
            e.stateTime = 0.0f;
            e.route.clear();
            continue;
        }
        const TextObject *alert = states->object("Alert");
        if (e.state == "Alert" && e.stateTime >= number(alert, "Alert Time", 2.0f)) {
            attackOfficer(e, e.target);
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
        const float off = std::abs(wrap(toward - e.angle)) * 180.0f / kPi;
        if (off <= e.weapon->maxShootAngle && e.reload <= 0.0f) {
            e.reload = e.weapon->reuseDelay;
            shoot(*e.weapon, _map.parts[e.part].model, e.x, e.y, o.x, o.y);
            e.shooting = 1e-6f;
            hurtOfficer(size_t(e.target), *e.weapon, e.stuns);
        }
    }
}

} // namespace reluva::stawayteam
