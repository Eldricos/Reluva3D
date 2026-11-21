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
        for (const TextObject *ai : p.activities) {
            const std::string &kind = ai->className;
            if (kind != "CTouchingOvalTriggerAI" && kind != "CTouchingPolygonTriggerAI" &&
                kind != "CRepeatTimerTriggerAI" && kind != "CRelayAI")
                continue;
            Trigger t;
            t.part = i;
            t.ai = ai;
            t.done = flag(ai, "Was Triggered");
            t.touching.assign(_officers.size(), false);
            t.nextRepeat.assign(_officers.size(), 0.0f);
            t.timer = number(ai, "Seconds Till Next Execute", number(ai, "Delay Between Actions"));
            _triggers.push_back(std::move(t));
        }
    }
}

bool MapScreen::shown(size_t part) const {
    const PartState &s = _partStates[part];
    return s.visible && s.active && !s.deleted;
}

std::vector<size_t> MapScreen::partsNamed(const std::string &names) const {
    std::vector<size_t> out;
    size_t at = 0;
    while (at <= names.size()) {
        const size_t comma = std::min(names.find(',', at), names.size());
        const std::string name = lower(trim(names.substr(at, comma - at)));
        at = comma + 1;
        if (name.empty())
            continue;
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
                runAction(t.ai->object("Action"), instigator);
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
                runAction(ai->object("Action"), -1);
            }
            continue;
        }
        if (ai->className == "CRelayAI" || !_teamShown)
            continue;
        // A touching trigger: who's in its oval or polygon, of those it
        // answers to.
        const std::string byName = lower(readText(ai, "Triggered By Name"));
        const bool byPlayers = flag(ai, "Triggered By Players") || flag(ai, "Triggered By Anything");
        const float rx = number(ai, "X Radius");
        const float ry = rx * number(ai, "YX Ratio", 1.0f);
        for (size_t o = 0; o < _officers.size() && !t.done; ++o) {
            const Officer &who = _officers[o];
            const bool answers = byPlayers || (!byName.empty() && lower(_team[who.member].name()) == byName);
            bool in = false;
            if (answers) {
                if (ai->className == "CTouchingOvalTriggerAI") {
                    if (rx > 0.0f && ry > 0.0f) {
                        const float dx = (who.x - p.x) / rx;
                        const float dy = (who.y - p.y) / ry;
                        in = dx * dx + dy * dy <= 1.0f;
                    }
                } else if (p.polygon.size() >= 3) {
                    in = inside(p.polygon, who.x, who.y);
                }
            }
            const int instigator = int(o);
            if (in && !t.touching[o]) {
                t.touching[o] = true;
                t.nextRepeat[o] = 0.0f;
                runAction(ai->object("Enter Action"), instigator);
                if (flag(ai, "Trigger Only Once") && ai->object("Enter Action"))
                    t.done = true;
            }
            if (in && !t.done && ai->object("Repeat Action")) {
                t.nextRepeat[o] -= seconds;
                if (t.nextRepeat[o] <= 0.0f) {
                    t.nextRepeat[o] = number(ai, "Repeat Every");
                    runAction(ai->object("Repeat Action"), instigator);
                    if (flag(ai, "Trigger Only Once"))
                        t.done = true;
                }
            }
            if (!in && t.touching[o]) {
                t.touching[o] = false;
                runAction(ai->object("Exit Action"), instigator);
                if (flag(ai, "Trigger Only Once") && ai->object("Exit Action"))
                    t.done = true;
            }
        }
        // Once done, a trigger that flips its switch turns its part off
        // (Auto-Flip Switch).
        if (t.done && flag(ai, "Auto-Flip Switch"))
            _partStates[t.part].active = false;
    }
}

void MapScreen::runAction(const TextObject *action, int instigator) {
    if (!action || runMissionAction(action, instigator))
        return;
    const std::string &kind = action->className;
    const auto targets = [&](const char *key) {
        const std::string names = readText(action, key);
        std::vector<size_t> parts = partsNamed(names);
        return parts;
    };
    if (kind == "CMultipleActionsAction") {
        if (const TextObject *actions = action->object("Action"))
            for (const TextField *item : actions->items())
                runAction(item->object.get(), instigator);
    } else if (kind == "CRandomAction") {
        // One of its actions, by chance.
        if (const TextObject *actions = action->object("Action")) {
            const auto items = actions->items();
            if (!items.empty()) {
                _random = _random * 1103515245u + 12345u;
                runAction(items[(_random >> 16) % items.size()]->object.get(), instigator);
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
                runAction(items[next++]->object.get(), instigator);
        }
    } else if (kind == "CDelayAction") {
        _delayed.push_back({float(action->number("Delay").value_or(0.0)), action->object("Next Action"), instigator});
    } else if (kind == "CInfoDialogAction") {
        showDialog(readText(action, "Script"), readText(action, "Line"), readText(action, "Who is speaking"));
    } else if (kind == "CTricorderDialogAction") {
        // In the info dialog, for now: the tricorder's own window isn't
        // there yet.
        showDialog(readText(action, "Script"), readText(action, "Line"), "");
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
        // Only a check whether a part's there can be tried yet: the rest
        // count as failing (inferred).
        const TextObject *attempt = action->object("Try Action");
        bool succeeded = false;
        if (attempt && attempt->className == "CCheckExistenceAction")
            for (size_t part : partsNamed(readText(attempt, "Name To Check For")))
                succeeded = succeeded || !_partStates[part].deleted;
        runAction(action->object(succeeded ? "Succeed Action" : "Fail Action"), instigator);
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
