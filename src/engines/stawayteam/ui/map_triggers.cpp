// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The map's triggers: its parts' AIs that act when the team comes near,
// on a timer or when another action relays to them, and the actions they
// run (docs/engines/stawayteam/systems/maps.md). The rules are read from
// the maps' own fields; the program's trigger code isn't traced yet.

#include "engines/stawayteam/ui/button.h"
#include "engines/stawayteam/ui/layout.h"
#include "engines/stawayteam/ui/map_screen.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace reluva::stawayteam {

namespace {

// Near enough a part with no footprint to collide with it (inferred).
constexpr float kCollideReach = 20.0f;

std::string lower(std::string s) {
    for (char &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string &s) {
    const size_t from = s.find_first_not_of(' ');
    if (from == std::string::npos)
        return {};
    return s.substr(from, s.find_last_not_of(' ') - from + 1);
}

bool flag(const TextObject *o, const char *key) {
    return o && o->integer(key).value_or(0) != 0;
}

float number(const TextObject *o, const char *key, float fallback = 0.0f) {
    return o ? float(o->number(key).value_or(fallback)) : fallback;
}

bool inside(const std::vector<std::pair<float, float>> &polygon, float x, float y) {
    bool in = false;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const auto [xi, yi] = polygon[i];
        const auto [xj, yj] = polygon[j];
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi)
            in = !in;
    }
    return in;
}

} // namespace

void MapScreen::setupTriggers() {
    _partStates.resize(_map.parts.size());
    for (size_t i = 0; i < _map.parts.size(); ++i) {
        const MapPart &p = _map.parts[i];
        _partStates[i] = {p.active, p.visible, false, p.collideable};
        if (!p.name.empty())
            _byName[lower(p.name)].push_back(i);
        for (const TextObject *ai : p.activities)
            addTrigger(i, ai);
    }
    setupDoors();
}

void MapScreen::addTrigger(size_t part, const TextObject *ai) {
    const std::string &kind = ai->className;
    // A stasis field's CEveryTouchingOvalTriggerAI too (inferred: as a
    // touching trigger for each who comes in); the doors' anything-touching
    // ones, the team's, and those set off by colliding or passing through.
    static const char *const kKinds[] = {"CTouchingOvalTriggerAI",
                                         "CTouchingPolygonTriggerAI",
                                         "CRepeatTimerTriggerAI",
                                         "CRelayAI",
                                         "CEveryTouchingOvalTriggerAI",
                                         "CAnyTouchingOvalTriggerAI",
                                         "CAnyTouchingPolygonTriggerAI",
                                         "CTeamTouchingPolygonTriggerAI",
                                         "CollideTriggerAI",
                                         "CPassThroughTriggerAI"};
    if (std::none_of(std::begin(kKinds), std::end(kKinds), [&](const char *k) { return kind == k; }))
        return;
    Trigger t;
    t.part = part;
    t.ai = ai;
    t.done = flag(ai, "Was Triggered");
    t.touching.assign(_officers.size(), false);
    t.nextRepeat.assign(_officers.size(), 0.0f);
    t.timer = number(ai, "Seconds Till Next Execute", number(ai, "Delay Between Actions"));
    _triggers.push_back(std::move(t));
}

bool MapScreen::shown(size_t part) const {
    const PartState &s = _partStates[part];
    return s.visible && s.active && !s.deleted;
}

std::vector<size_t> MapScreen::partsNamed(const std::string &names, int trigger) const {
    std::vector<size_t> out;
    size_t at = 0;
    while (at <= names.size()) {
        const size_t comma = std::min(names.find(',', at), names.size());
        const std::string name = lower(trim(names.substr(at, comma - at)));
        at = comma + 1;
        if (name.empty())
            continue;
        if (name == "$trigger") {
            if (trigger >= 0)
                out.push_back(size_t(trigger));
            continue;
        }
        const auto it = _byName.find(name);
        if (it != _byName.end())
            out.insert(out.end(), it->second.begin(), it->second.end());
    }
    return out;
}

void MapScreen::relay(const std::string &names, int instigator) {
    // A relay that's active runs its action; once, if it says so
    // (CRelayAI's Trigger Only Once).
    for (size_t part : partsNamed(names)) {
        if (!_partStates[part].active || _partStates[part].deleted)
            continue;
        for (Trigger &t : _triggers)
            if (t.part == part && t.ai->className == "CRelayAI" && !t.done) {
                if (flag(t.ai, "Trigger Only Once"))
                    t.done = true;
                runAction(t.ai->object("Action"), instigator, int(part));
            }
    }
}

void MapScreen::updateTriggers(float seconds) {
    for (size_t k = 0; k < _triggers.size(); ++k) {
        Trigger &t = _triggers[k];
        const PartState &state = _partStates[t.part];
        if (t.done || !state.active || state.deleted)
            continue;
        const MapPart &p = _map.parts[t.part];
        const TextObject *ai = t.ai;
        if (ai->className == "CRepeatTimerTriggerAI") {
            // Every Delay Between Actions, give or take Plus or Minus.
            t.timer -= seconds;
            if (t.timer <= 0.0f) {
                _random = _random * 1103515245u + 12345u;
                const float spread = number(ai, "Plus or Minus");
                const float r = float((_random >> 16) & 0x7fff) / 32767.0f * 2.0f - 1.0f;
                t.timer = std::max(0.05f, number(ai, "Delay Between Actions") + spread * r);
                runAction(ai->object("Action"), -1, int(t.part));
            }
            continue;
        }
        if (ai->className == "CRelayAI" || !_teamShown)
            continue;
        if (updateOtherTrigger(t, seconds))
            continue;
        // A touching trigger: who's in its oval or polygon, of those it
        // answers to.
        const std::string byName = lower(readText(ai, "Triggered By Name"));
        // A Triggered by list (a stasis field's "Enemy,Player,Team Member")
        // names its kinds.
        const std::string byList = readText(ai, "Triggered by");
        const bool byPlayers = flag(ai, "Triggered By Players") || flag(ai, "Triggered By Anything") ||
                               inCategory(byList, "Player,Team Member");
        const bool oval = ai->className.find("Oval") != std::string::npos;
        const float rx = number(ai, "X Radius");
        const float ry = rx * number(ai, "YX Ratio", 1.0f);
        for (size_t o = 0; o < _officers.size() && !t.done; ++o) {
            const Officer &who = _officers[o];
            const bool answers =
                who.present && (byPlayers || (!byName.empty() && lower(_team[who.member].name()) == byName));
            bool in = false;
            if (answers) {
                in = touches(t, who.x, who.y);
            }
            const int instigator = int(o);
            if (in && !t.touching[o]) {
                t.touching[o] = true;
                t.nextRepeat[o] = 0.0f;
                runAction(ai->object("Enter Action"), instigator, int(t.part));
                if (flag(ai, "Trigger Only Once") && ai->object("Enter Action"))
                    t.done = true;
            }
            if (in && !t.done && ai->object("Repeat Action")) {
                t.nextRepeat[o] -= seconds;
                if (t.nextRepeat[o] <= 0.0f) {
                    t.nextRepeat[o] = number(ai, "Repeat Every");
                    runAction(ai->object("Repeat Action"), instigator, int(t.part));
                    if (flag(ai, "Trigger Only Once"))
                        t.done = true;
                }
            }
            if (!in && t.touching[o]) {
                t.touching[o] = false;
                runAction(ai->object("Exit Action"), instigator, int(t.part));
                if (flag(ai, "Trigger Only Once") && ai->object("Exit Action"))
                    t.done = true;
            }
        }
        // The enemies too, for one Triggered By Enemies (66 in the maps),
        // By Anything, or whose list names them; each as an officer, their
        // instigator (inferred).
        const bool byEnemies =
            flag(ai, "Triggered By Enemies") || flag(ai, "Triggered By Anything") || inCategory(byList, "Enemy");
        if (byEnemies) {
            t.touchingEnemies.resize(_enemies.size(), false);
            t.enemyRepeat.resize(_enemies.size(), 0.0f);
            for (size_t i = 0; i < _enemies.size() && !t.done; ++i) {
                const Enemy &e = _enemies[i];
                const bool in = e.alive() && shown(e.part) && touches(t, e.x, e.y);
                const int instigator = kEnemyInstigator - int(i);
                if (in && !t.touchingEnemies[i]) {
                    t.touchingEnemies[i] = true;
                    t.enemyRepeat[i] = 0.0f;
                    runAction(ai->object("Enter Action"), instigator, int(t.part));
                    if (flag(ai, "Trigger Only Once") && ai->object("Enter Action"))
                        t.done = true;
                }
                if (in && !t.done && ai->object("Repeat Action")) {
                    t.enemyRepeat[i] -= seconds;
                    if (t.enemyRepeat[i] <= 0.0f) {
                        t.enemyRepeat[i] = number(ai, "Repeat Every");
                        runAction(ai->object("Repeat Action"), instigator, int(t.part));
                    }
                }
                if (!in && t.touchingEnemies[i]) {
                    t.touchingEnemies[i] = false;
                    runAction(ai->object("Exit Action"), instigator, int(t.part));
                    if (flag(ai, "Trigger Only Once") && ai->object("Exit Action"))
                        t.done = true;
                }
            }
        }
        // Once done, a trigger that flips its switch turns its part off
        // (Auto-Flip Switch).
        if (t.done && flag(ai, "Auto-Flip Switch"))
            _partStates[t.part].active = false;
    }
}

bool MapScreen::touches(const Trigger &t, float x, float y) const {
    // In its oval (X Radius across, YX Ratio of that up and down) or its
    // part's polygon.
    // About where its part is now: a character's, as it walks (a
    // scientist's to be reached).
    const MapPart &p = _map.parts[t.part];
    if (t.ai->className.find("Oval") != std::string::npos) {
        const float rx = number(t.ai, "X Radius");
        const float ry = rx * number(t.ai, "YX Ratio", 1.0f);
        if (rx <= 0.0f || ry <= 0.0f)
            return false;
        float px = 0.0f;
        float py = 0.0f;
        placeOf(t.part, px, py);
        const float dx = (x - px) / rx;
        const float dy = (y - py) / ry;
        return dx * dx + dy * dy <= 1.0f;
    }
    return p.polygon.size() >= 3 && inside(p.polygon, x, y);
}

bool MapScreen::updateOtherTrigger(Trigger &t, float seconds) {
    const TextObject *ai = t.ai;
    const std::string &kind = ai->className;
    const MapPart &p = _map.parts[t.part];
    // Who's about, as instigators: the officers here, then the enemies
    // standing.
    struct Who {
        int instigator;
        float x;
        float y;
        bool enemy;
    };
    std::vector<Who> about;
    for (size_t o = 0; o < _officers.size(); ++o)
        if (_officers[o].present && _officers[o].alive())
            about.push_back({int(o), _officers[o].x, _officers[o].y, false});
    for (size_t i = 0; i < _enemies.size(); ++i)
        if (_enemies[i].alive() && shown(_enemies[i].part))
            about.push_back({kEnemyInstigator - int(i), _enemies[i].x, _enemies[i].y, true});
    if (kind == "CAnyTouchingOvalTriggerAI" || kind == "CAnyTouchingPolygonTriggerAI") {
        // Anything of its Triggered by list in it: its Enter Action as the
        // first comes in, after its Enter Action Delay; its Exit Action as
        // the last leaves, after its Exit Action Delay, unless someone's
        // back by then (the doors': open, close; inferred).
        const std::string by = readText(ai, "Triggered by");
        int first = kNobody;
        bool any = false;
        for (const Who &w : about)
            if (inCategory(by, w.enemy ? "Enemy" : "Player,Team Member") && touches(t, w.x, w.y)) {
                if (!any)
                    first = w.instigator;
                any = true;
            }
        if (any && !t.anything) {
            t.anything = true;
            t.pending = ai->object("Enter Action");
            t.countdown = number(ai, "Enter Action Delay");
            t.instigator = first;
        } else if (!any && t.anything) {
            t.anything = false;
            t.pending = ai->object("Exit Action");
            t.countdown = number(ai, "Exit Action Delay");
        }
        if (t.pending) {
            t.countdown -= seconds;
            if (t.countdown <= 0.0f) {
                const TextObject *action = t.pending;
                t.pending = nullptr;
                runAction(action, t.instigator, int(t.part));
            }
        }
        return true;
    }
    if (kind == "CTeamTouchingPolygonTriggerAI") {
        // The whole team here in its polygon: its Action (inferred).
        bool all = false;
        for (const Officer &o : _officers) {
            if (!o.present || !o.alive())
                continue;
            all = true;
            if (!touches(t, o.x, o.y))
                return true;
        }
        if (all) {
            if (flag(ai, "Trigger Only Once"))
                t.done = true;
            runAction(ai->object("Action"), -1, int(t.part));
        }
        return true;
    }
    if (kind == "CollideTriggerAI" || kind == "CPassThroughTriggerAI") {
        // One it answers to walking into the part (its footprint, or its
        // polygon), or, passing through, crossing it: its Action, once per
        // coming in, not again within Disallow Retrigger seconds (inferred:
        // projectiles don't set them off).
        t.countdown = std::max(0.0f, t.countdown - seconds);
        const bool players = flag(ai, "Triggered By Players") || flag(ai, "Triggered By Anything");
        const bool enemies = flag(ai, "Triggered By Enemies") || flag(ai, "Triggered By Anything");
        const std::vector<std::pair<float, float>> &foot = modelInfo(p.model).footprint;
        t.touchingEnemies.resize(about.size(), false);
        for (size_t k = 0; k < about.size() && !t.done; ++k) {
            const Who &w = about[k];
            if (w.enemy ? !enemies : !players)
                continue;
            bool in = false;
            if (p.polygon.size() >= 3) {
                in = inside(p.polygon, w.x, w.y);
            } else if (foot.size() >= 3) {
                std::vector<std::pair<float, float>> placed = foot;
                for (auto &[fx, fy] : placed) {
                    fx += p.x;
                    fy += p.y;
                }
                in = inside(placed, w.x, w.y);
            } else {
                in = std::hypot(w.x - p.x, w.y - p.y) <= kCollideReach;
            }
            const bool was = t.touchingEnemies[k];
            t.touchingEnemies[k] = in;
            if (!in || was || t.countdown > 0.0f)
                continue;
            t.countdown = number(ai, "Disallow Retrigger");
            if (flag(ai, "Trigger Only Once"))
                t.done = true;
            runAction(ai->object("Action"), w.instigator, int(t.part));
        }
        return true;
    }
    return false;
}

void MapScreen::runAction(const TextObject *action, int instigator, int trigger) {
    if (!action || runMissionAction(action, instigator) || runEnemyAction(action, instigator, trigger) ||
        runGadgetAction(action, instigator, trigger) || runTeamAction(action, instigator, trigger))
        return;
    const std::string &kind = action->className;
    const auto targets = [&](const char *key) {
        const std::string names = readText(action, key);
        std::vector<size_t> parts = partsNamed(names, trigger);
        return parts;
    };
    if (kind == "CMultipleActionsAction") {
        if (const TextObject *actions = action->object("Action"))
            for (const TextField *item : actions->items())
                runAction(item->object.get(), instigator, trigger);
    } else if (kind == "CRandomAction") {
        // One of its actions, by chance.
        if (const TextObject *actions = action->object("Action")) {
            const auto items = actions->items();
            if (!items.empty()) {
                _random = _random * 1103515245u + 12345u;
                runAction(items[(_random >> 16) % items.size()]->object.get(), instigator, trigger);
            }
        }
    } else if (kind == "CSeriesAction" || kind == "CShuffledSeriesAction") {
        // The next of its actions each time, from the first again after the
        // last (When Done's Loop; others stop).
        if (const TextObject *actions = action->object("Action")) {
            const auto items = actions->items();
            size_t &next = _series[action];
            if (next >= items.size() && lower(readText(action, "When Done")) == "loop")
                next = 0;
            if (next < items.size())
                runAction(items[next++]->object.get(), instigator, trigger);
        }
    } else if (kind == "CDelayAction") {
        _delayed.push_back(
            {float(action->number("Delay").value_or(0.0)), action->object("Next Action"), instigator, trigger});
    } else if (kind == "CInfoDialogAction") {
        showDialog(readText(action, "Script"), readText(action, "Line"), readText(action, "Who is speaking"));
    } else if (kind == "CTricorderDialogAction") {
        // On the tricorder's panel, typed out, as a scan's description
        // (inferred: as CTricorderModelDescriptionAction's).
        _scanText = describe(readText(action, "Script"), readText(action, "Line"));
        _scanTyped = 0.0f;
    } else if (kind == "CTriggerRelayAction") {
        relay(readText(action, "Relay Name"), instigator);
    } else if (kind == "CActivateAction") {
        for (size_t part : targets("Target Name"))
            _partStates[part].active = true;
    } else if (kind == "CDeactivateAction") {
        for (size_t part : targets("Target Name"))
            _partStates[part].active = false;
    } else if (kind == "CMakeVisibleAction") {
        const bool visible = flag(action, "Make Visible");
        const std::string who = lower(readText(action, "Who to modify"));
        if (who == "$instigator") {
            if (instigator < 0)
                _teamShown = visible;
        } else {
            for (size_t part : targets("Who to modify"))
                _partStates[part].visible = visible;
        }
    } else if (kind == "CAddAIAction") {
        // Its AI given to its Target Name: a process to go through, a
        // trigger, a point of interest on the minimap.
        if (const TextObject *ai = action->object("AI"))
            for (size_t part : targets("Target Name")) {
                _map.parts[part].activities.push_back(ai);
                addTrigger(part, ai);
            }
    } else if (kind == "CRemoveAIAction") {
        // Its AI, a class's name, taken from its Target Name: a trigger
        // stops (inferred: all of that class).
        const std::string ai = readText(action, "AI");
        for (size_t part : targets("Target Name")) {
            std::erase_if(_map.parts[part].activities, [&](const TextObject *a) { return a->className == ai; });
            for (Trigger &tr : _triggers)
                if (tr.part == part && tr.ai->className == ai)
                    tr.done = true;
        }
    } else if (kind == "CSetCollidableAction") {
        for (size_t part : targets("Target Name"))
            _partStates[part].collideable = flag(action, "Collidable");
    } else if (kind == "CDeleteAction") {
        for (size_t part : targets("Target Name"))
            _partStates[part].deleted = true;
    } else if (kind == "CDestroyAction") {
        for (size_t part : targets("Target"))
            _partStates[part].deleted = true;
    } else if (kind == "CPlaySoundAction") {
        if (_mixer)
            if (auto sound = gameSound(_data, readText(action, "Sound")))
                _mixer->play(sound, audio::SoundType::kEffects);
    } else if (kind == "CConditionalAction") {
        const bool succeeded = checkAction(action->object("Try Action"), instigator, trigger);
        runAction(action->object(succeeded ? "Succeed Action" : "Fail Action"), instigator, trigger);
    } else if (kind == "CAddEffectToTeam") {
        // The team beams in: the warp's sound, and them fading in under the
        // transporter's sparkle over its Warp Time (inferred for how it's
        // drawn).
        const TextObject *behavior = action->object("Warp Behavior");
        _warpTime = behavior ? float(behavior->number("Warp Time").value_or(1.75)) : 1.75f;
        _warpLeft = _warpTime;
        _teamShown = true;
        if (_mixer)
            if (auto sound = gameSound(_data, readText(action, "Warp Sound")))
                _mixer->play(sound, audio::SoundType::kEffects);
    } else if (kind == "CAddCameraScriptAction") {
        if (const TextObject *manager = action->object("Script")) {
            const auto more = readCameraScripts(*manager);
            _scripts.insert(_scripts.end(), more.begin(), more.end());
        }
    }
    // The rest (the AIs' messages and tasks, objectives, items) aren't there
    // yet.
}

} // namespace reluva::stawayteam
