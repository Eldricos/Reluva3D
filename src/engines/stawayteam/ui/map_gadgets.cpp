// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The gadgets: the cloaking band, what's put down and its AIs, decoys,
// stasis, bodies beamed away (docs/engines/stawayteam/systems/items.md).

#include "engines/stawayteam/ui/button.h"
#include "engines/stawayteam/ui/layout.h"
#include "engines/stawayteam/ui/map_screen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::stawayteam {

namespace {

// A cloaked officer shows a quarter of themselves, fading there and back
// at a whole a second (0x5d3740).
constexpr float kCloakedOpacity = 0.25f;
constexpr float kFadePerSecond = 1.0f;
// Near enough to put something down where it's to go (inferred).
constexpr float kDropReach = 30.0f;

std::string lower(std::string s) {
    for (char &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Which of a figure's 12 directions faces along (dx, dy), 0 facing left.
int directionOf(float dx, float dy) {
    const int d = 6 - int(std::lround(std::atan2(dy, dx) / (3.14159265f / 6.0f)));
    return (d % 12 + 12) % 12;
}

bool flag(const TextObject *o, const char *key) {
    return o && o->integer(key).value_or(0) != 0;
}

float number(const TextObject *o, const char *key, float fallback = 0.0f) {
    return o ? float(o->number(key).value_or(fallback)) : fallback;
}

} // namespace

void MapScreen::toggleCharge() {
    if (_selected < 0)
        return;
    Officer &o = _officers[size_t(_selected)];
    const ItemDefinition *def = itemOf(o);
    if (!def || !def->charged || o.down())
        return;
    // On only with a use's worth of charge in it (0x5d2d30), its Use
    // Sound as it goes on.
    Officer::Held &held = o.items[size_t(o.item)];
    if (held.on) {
        held.on = false;
        return;
    }
    if (held.energy < def->energyUse)
        return;
    held.on = true;
    o.cloaked = true;
    if (_mixer)
        if (auto sound = gameSound(_data, def->useSound))
            _mixer->play(sound, audio::SoundType::kEffects);
}

void MapScreen::updateCharges(float seconds) {
    for (Officer &o : _officers) {
        o.cloaked = false;
        for (Officer::Held &held : o.items) {
            const auto def = _items.find(held.name);
            if (def == _items.end() || !def->second.charged)
                continue;
            if (held.on && (o.down() || !o.alive()))
                held.on = false;
            if (held.on) {
                held.energy -= def->second.energyUse * seconds;
                if (held.energy <= 0.0f) {
                    held.energy = 0.0f;
                    held.on = false;
                }
            } else {
                held.energy = std::min(100.0f, held.energy + def->second.energyRecharge * seconds);
            }
            o.cloaked = o.cloaked || held.on;
        }
        const float target = o.cloaked ? kCloakedOpacity : 1.0f;
        const float step = kFadePerSecond * seconds;
        o.opacity = o.opacity < target ? std::min(target, o.opacity + step) : std::max(target, o.opacity - step);
    }
}

const BinaryObject *MapScreen::modelObject(const std::string &model) {
    auto it = _modelObjects.find(model);
    if (it == _modelObjects.end()) {
        std::unique_ptr<BinaryObject> object;
        std::vector<uint8_t> bytes;
        if (!_data.read("Cache/Models/" + model + ".mdl16", bytes)) {
            auto read = std::make_unique<BinaryObject>();
            if (!readBinaryObject(bytes.data(), bytes.size(), "CModel", *read))
                object = std::move(read);
        }
        it = _modelObjects.emplace(model, std::move(object)).first;
    }
    return it->second.get();
}

size_t MapScreen::spawnPart(const std::string &model, const std::string &name, float x, float y,
                            const std::string &sequence) {
    // As a part of the map from its model's Default Entity: named, where
    // it's put, its sequence playing from its start, its AIs running, its
    // CActionAIs' actions at once (inferred: as the program's entities
    // start).
    const BinaryObject *object = modelObject(model);
    const TextField *entity = object ? object->object.field("Default Entity") : nullptr;
    MapPart p = entity && entity->object ? readPart(entity->value, *entity->object) : MapPart{};
    if (!name.empty())
        p.name = name;
    p.model = model;
    p.x = x;
    p.y = y;
    const ModelInfo &info = modelInfo(model);
    p.sequence = sequence.empty() || sequence == "!None" ? info.defaultSequence : sequence;
    if (p.sequence.empty())
        p.sequence = "Idle";
    const size_t part = _map.parts.size();
    _map.parts.push_back(p);
    _partStates.push_back({p.active, p.visible, false, p.collideable});
    if (!p.name.empty())
        _byName[lower(p.name)].push_back(part);
    _standing.push_back(part);
    Deployed d;
    d.part = part;
    d.sequence = p.sequence;
    d.once = lower(p.sequence) != "idle";
    d.health = float(info.hitPoints);
    d.painSound = object ? readText(&object->object, "Pain Sound") : std::string();
    _deployed.push_back(d);
    for (const TextObject *ai : p.activities) {
        addTrigger(part, ai);
        addEmitter(part, ai);
        if (ai->className == "CActionAI")
            runAction(ai->object("Action"), -1, int(part));
    }
    return part;
}

bool MapScreen::placeNamed(const std::string &name, int instigator, int trigger, float &x, float &y) const {
    const std::string who = lower(name);
    if (who == "$instigator" || who.empty()) {
        const int i = _hasTouched ? _touched : instigator;
        if (i >= 0 && size_t(i) < _officers.size()) {
            x = _officers[size_t(i)].x;
            y = _officers[size_t(i)].y;
            return true;
        }
        if (i <= kEnemyInstigator && size_t(kEnemyInstigator - i) < _enemies.size()) {
            x = _enemies[size_t(kEnemyInstigator - i)].x;
            y = _enemies[size_t(kEnemyInstigator - i)].y;
            return true;
        }
    }
    if ((who == "$trigger" || who == "$instigator" || who.empty()) && trigger >= 0) {
        placeOf(size_t(trigger), x, y);
        return true;
    }
    if ((who == "$trigger" || who == "$instigator" || who.empty()) && _hasHere) {
        x = _hereX;
        y = _hereY;
        return true;
    }
    const std::vector<size_t> parts = partsNamed(name, trigger);
    if (parts.empty())
        return false;
    placeOf(parts.front(), x, y);
    return true;
}

int MapScreen::whoNamed(const std::string &name, int instigator, int trigger) const {
    // An officer (0 up) or an enemy (kEnemyInstigator down) by what an
    // action calls them; kNobody for neither.
    const std::string who = lower(name);
    if (who == "$instigator")
        return _hasTouched ? _touched : instigator;
    // Named no one, it's who's doing it (a stasis field given back).
    if (who.empty() || who == "!none")
        return instigator;
    std::vector<size_t> parts;
    if (who == "$trigger") {
        if (trigger >= 0)
            parts.push_back(size_t(trigger));
    } else {
        parts = partsNamed(name, trigger);
        for (size_t i = 0; i < _officers.size(); ++i)
            if (lower(_team[_officers[i].member].name()) == who)
                return int(i);
    }
    for (size_t part : parts) {
        const auto it = _enemyOfPart.find(part);
        if (it != _enemyOfPart.end())
            return kEnemyInstigator - int(it->second);
    }
    return kNobody;
}

void MapScreen::giveItem(int officer, const std::string &item, int count) {
    if (officer < 0 || size_t(officer) >= _officers.size() || item.empty())
        return;
    Officer &o = _officers[size_t(officer)];
    for (Officer::Held &h : o.items)
        if (h.name == item) {
            if (h.count >= 0)
                h.count += count;
            return;
        }
    const auto def = _items.find(item);
    const bool counted = def != _items.end() && def->second.rounds > 0;
    o.items.push_back({item, counted ? count : -1, def != _items.end() && def->second.stuns});
}

void MapScreen::useUpItem(int officer, const std::string &item) {
    if (officer < 0 || size_t(officer) >= _officers.size())
        return;
    for (Officer::Held &h : _officers[size_t(officer)].items)
        if (h.name == item && h.count > 0) {
            --h.count;
            return;
        }
}

void MapScreen::hold(int who, float seconds) {
    if (who >= 0 && size_t(who) < _officers.size()) {
        Officer &o = _officers[size_t(who)];
        o.held = seconds;
        o.path.clear();
        o.attacking = -1;
        o.task = Officer::Task::kNone;
    } else if (who <= kEnemyInstigator && size_t(kEnemyInstigator - who) < _enemies.size()) {
        Enemy &e = _enemies[size_t(kEnemyInstigator - who)];
        e.held = seconds;
        e.route.clear();
        e.walking = false;
    }
}

bool MapScreen::checkAction(const TextObject *attempt, int instigator, int trigger) {
    // What a CConditionalAction tries: whether a part's there, whether
    // someone carries an item, whether a part's of a category; anything
    // else counts as failing (inferred).
    if (!attempt)
        return false;
    const std::string &kind = attempt->className;
    if (kind == "CCheckExistenceAction") {
        for (size_t part : partsNamed(readText(attempt, "Name To Check For"), trigger))
            if (!_partStates[part].deleted)
                return true;
        return false;
    }
    if (kind == "CCheckForItemAction") {
        const int who = whoNamed(readText(attempt, "Who To Check"), instigator, trigger);
        if (who < 0 || size_t(who) >= _officers.size())
            return false;
        // Having it at all, its uses spent or not: the stasis field's owner
        // has none left once it's set up (inferred).
        const std::string item = readText(attempt, "Inventory Definition");
        for (const Officer::Held &h : _officers[size_t(who)].items)
            if (h.name == item)
                return true;
        return false;
    }
    if (kind == "CIsDiscoveredAction") {
        // Its item found in a mission before.
        const std::string item = readText(attempt, "Item to check");
        return std::find(_progress->discovered.begin(), _progress->discovered.end(), item) !=
               _progress->discovered.end();
    }
    if (kind == "CCheckForClassAction") {
        // An officer of a field: Security, Science, Medical... (116 in the
        // maps, choosing who speaks).
        const int who = whoNamed(readText(attempt, "Who To Check"), instigator, trigger);
        return who >= 0 && size_t(who) < _officers.size() &&
               lower(_team[_officers[size_t(who)].member].field) == lower(readText(attempt, "Class"));
    }
    if (kind == "CCheckCategoryAction") {
        const std::string wanted = readText(attempt, "Category to Check");
        // A thrown thing by its model's (a grenade's Grenades).
        const int who = whoNamed(readText(attempt, "Who to Check"), instigator, trigger);
        if (who <= kThrownInstigator)
            for (const Thrown &t : _thrown)
                if (kThrownInstigator - t.id == who)
                    return inCategory(modelInfo(t.model).category, wanted);
        for (size_t part : partsNamed(readText(attempt, "Who to Check"), trigger))
            if (inCategory(_map.parts[part].category, wanted))
                return true;
        return false;
    }
    return false;
}

bool MapScreen::runGadgetAction(const TextObject *action, int instigator, int trigger) {
    const std::string &kind = action->className;
    if (kind == "CDropItemAtTargetAction" || kind == "CDropItemAction") {
        // Its Model to Create where the item's used, or in front of who
        // used it by Offset In Front Of; one of Inventory Definition to
        // Consume used up.
        float x = 0.0f;
        float y = 0.0f;
        if (kind == "CDropItemAction" && instigator >= 0 && size_t(instigator) < _officers.size()) {
            const Officer &o = _officers[size_t(instigator)];
            const float angle = float(6 - o.direction) * 3.14159265f / 6.0f;
            const float ahead = number(action, "Offset In Front Of");
            x = o.x + std::cos(angle) * ahead;
            y = o.y + std::sin(angle) * ahead;
        } else if (!placeNamed("$trigger", -1, trigger, x, y)) {
            return true;
        }
        spawnPart(readText(action, "Model to Create"), readText(action, "New Name"), x, y,
                  readText(action, "Deploy Sequence"));
        const std::string consume = readText(action, "Inventory Definition to Consume");
        if (!consume.empty() && consume != "!None")
            useUpItem(instigator, consume);
    } else if (kind == "CUseItemOnTargetAction") {
        useUpItem(instigator, readText(action, "Inventory Item Definition"));
    } else if (kind == "CGiveOneInventoryItemAction" || kind == "CGiveInventoryItemDefinitionAction") {
        giveItem(whoNamed(readText(action, "Who to give it to"), instigator, trigger),
                 readText(action, "Inventory Item Definition"), int(action->integer("Count").value_or(1)));
    } else if (kind == "CGiveInventoryItemAction") {
        if (const TextObject *item = action->object("Inventory Item"))
            giveItem(whoNamed(readText(action, "Who to give it to"), instigator, trigger),
                     readText(item, "Definition Index"), 1);
    } else if (kind == "CAssignTemporaryTaskAction" || kind == "CAssignOverrideTemporaryTaskAction") {
        // Held to its Temporary Task AI: a CWaitAI's Time To Wait, a
        // CDoNothingAI's till told it's done (inferred: the other tasks
        // aren't there).
        const TextObject *task = action->object("Temporary Task AI");
        if (task && (task->className == "CWaitAI" || task->className == "CDoNothingAI"))
            hold(whoNamed(readText(action, "Target"), instigator, trigger),
                 task->className == "CWaitAI" ? number(task, "Time To Wait") : kHeldForever);
    } else if (kind == "CSendAIDoneMessage") {
        // Ends a task it's held to.
        const int who = whoNamed(readText(action, "Target"), instigator, trigger);
        if (who >= 0 && size_t(who) < _officers.size() && _officers[size_t(who)].held > 0.0f)
            _officers[size_t(who)].held = 0.0f;
        if (who <= kEnemyInstigator && size_t(kEnemyInstigator - who) < _enemies.size())
            _enemies[size_t(kEnemyInstigator - who)].held = 0.0f;
    } else if (kind == "CAddEffectAction") {
        // Its Warp Sound; one that's Delete When Finished beams away over
        // its Warp Time, fading, and is gone: a part, or an enemy (the
        // alarms' $Instigator, 330 in the maps), held still meanwhile
        // (inferred: its warp effect's dissolving isn't drawn, nor officers
        // beamed; its overlay is).
        if (_mixer)
            if (auto sound = gameSound(_data, readText(action, "Warp Sound")))
                _mixer->play(sound, audio::SoundType::kEffects);
        const TextObject *behavior = action->object("Warp Behavior");
        if (flag(behavior, "Delete When Finished")) {
            const float time = std::max(0.01f, number(behavior, "Warp Time", 1.75f));
            const std::string name = readText(action, "Who To Warp");
            std::vector<size_t> parts;
            const int who = whoNamed(name, instigator, trigger);
            if (who <= kEnemyInstigator && size_t(kEnemyInstigator - who) < _enemies.size())
                parts.push_back(_enemies[size_t(kEnemyInstigator - who)].part);
            else if (lower(name) != "$instigator")
                parts = partsNamed(name, trigger);
            for (size_t part : parts)
                if (std::none_of(_warping.begin(), _warping.end(), [&](const Warp &w) { return w.part == part; })) {
                    const TextObject *overlay = behavior->object("Warp Overlay Effect");
                    _warping.push_back({part, time, time, readText(overlay, "Overlay Effect"),
                                        int(number(overlay, "Overlay YOffset")),
                                        std::max(1, int(number(behavior, "Num Rotations", 4.0f)))});
                }
        }
    } else if (kind == "COpenDoorAction" || kind == "CCloseDoorAction") {
        for (size_t part : partsNamed(readText(action, "Door Name"), trigger))
            moveDoor(part, kind == "COpenDoorAction", instigator);
    } else if (kind == "CLockDoorAction" || kind == "CUnlockDoorAction") {
        for (size_t part : partsNamed(readText(action, "Door Name"), trigger))
            if (const auto door = _doors.find(part); door != _doors.end())
                door->second.locked = kind == "CLockDoorAction";
    } else if (kind == "CSetCurSequenceAction") {
        const std::string sequence = readText(action, "Sequence");
        for (size_t part : partsNamed(readText(action, "Target Name"), trigger)) {
            _map.parts[part].sequence = sequence;
            for (Deployed &d : _deployed)
                if (d.part == part) {
                    d.sequence = sequence;
                    d.time = 0.0f;
                    d.frames = 0;
                    d.once = false;
                }
        }
    } else if (kind == "CSetCategoryAction" || kind == "CClearCategoryAction") {
        const bool set = kind == "CSetCategoryAction";
        const std::string category = readText(action, set ? "Category to Set" : "Category to Clear");
        for (size_t part : partsNamed(readText(action, set ? "Who to Set" : "Who to Clear"), trigger)) {
            std::string &list = _map.parts[part].category;
            if (set && !inCategory(list, category))
                list = list.empty() ? category : list + "," + category;
            if (!set) {
                std::string kept;
                size_t at = 0;
                while (at <= list.size()) {
                    const size_t comma = std::min(list.find(',', at), list.size());
                    const std::string one = list.substr(at, comma - at);
                    if (!one.empty() && !inCategory(one, category))
                        kept += (kept.empty() ? "" : ",") + one;
                    at = comma + 1;
                }
                list = kept;
            }
        }
    } else if (kind == "CAddNoiseAction") {
        float x = 0.0f;
        float y = 0.0f;
        if (placeNamed("$trigger", instigator, trigger, x, y))
            makeNoise(x, y, number(action, "Radius"), number(action, "Age"));
    } else {
        return false;
    }
    return true;
}

void MapScreen::runDeathBehaviors(size_t part, int instigator) {
    // Its Default Death Type's CEntityBehaviorExecuteActions' actions, the
    // part their $trigger: a Klingon's gives its body a process for the
    // Teleportation Brig Controller and makes it Dead Bodies (inferred: the
    // other behaviours, its sequences, are the combat's).
    const std::string &type = _map.parts[part].deathType;
    if (type.empty() || type == "!None")
        return;
    auto it = _deathBehaviors.find(type);
    if (it == _deathBehaviors.end()) {
        std::unique_ptr<TextObject> object;
        std::vector<uint8_t> bytes;
        if (!_data.read("Death Behaviors/" + type + ".txt", bytes)) {
            auto read = std::make_unique<TextObject>();
            if (!readTextObject(std::string(bytes.begin(), bytes.end()), *read))
                object = std::move(read);
        }
        it = _deathBehaviors.emplace(type, std::move(object)).first;
    }
    if (!it->second)
        return;
    for (const TextField &f : it->second->fields)
        if (f.key == "Death Behaviors" && f.object && f.value == "CEntityBehaviorExecuteAction")
            runAction(f.object->object("Action"), instigator, int(part));
}

float MapScreen::warpShown(size_t part) const {
    for (const Warp &w : _warping)
        if (w.part == part)
            return std::clamp(w.left / w.time, 0.0f, 1.0f);
    return 1.0f;
}

bool MapScreen::runItemProcess(size_t officer, float seconds) {
    // Its item's Process Activity: a CDropItemAI walks till the place is in
    // the item's Item Range and sight (or there, without one), a
    // CGotoPointAndDoActionAI to the place; then its Destination Action and
    // Done action, done where the item's used; a CActionAI's Action at once
    // (inferred: as the parts' processes).
    (void)seconds;
    Officer &o = _officers[officer];
    const ItemDefinition *def = itemOf(o);
    const TextObject *list = def && def->process ? def->process->object("Process Activity") : nullptr;
    const auto steps = list ? list->items() : std::vector<const TextField *>{};
    const auto here = [&](bool on) {
        _hasHere = on;
        _hereX = o.taskX;
        _hereY = o.taskY;
    };
    while (o.taskStep < steps.size()) {
        const TextObject *step = steps[o.taskStep]->object.get();
        const std::string kind = step ? step->className : std::string();
        if (kind == "CDropItemAI" || kind == "CGotoPointAndDoActionAI") {
            const bool ranged = kind == "CDropItemAI" && def->range > 0.0f;
            const float reach = ranged ? def->range : kDropReach;
            if (std::hypot(o.taskX - o.x, o.taskY - o.y) > reach ||
                (ranged && !clearSight(o.x, o.y, o.taskX, o.taskY))) {
                if (o.path.empty())
                    o.path = route(o, o.taskX, o.taskY);
                return false;
            }
            o.path.clear();
            o.direction = directionOf(o.taskX - o.x, o.taskY - o.y);
            o.discharge = def->dischargeSequence.empty() ? "Shoot1" : def->dischargeSequence;
            o.shooting = 1e-6f;
            ++o.taskStep;
            here(true);
            runAction(step->object("Destination Action"), int(officer), -1);
            runAction(step->object("Done action") ? step->object("Done action") : step->object("Done Action"),
                      int(officer), -1);
            makeNoise(o.taskX, o.taskY, def->shotNoise, def->shotNoiseTime);
            here(false);
            continue;
        }
        ++o.taskStep;
        if (kind == "CActionAI") {
            here(true);
            runAction(step->object("Action"), int(officer), o.taskPart);
            here(false);
        }
    }
    return true;
}

int MapScreen::closestDecoy(const Enemy &e, float &distance) const {
    // A decoy (a holographic projection, Decoy,Player,Team Member) is
    // found as an officer is, the nearest of its Valid Targets seen
    // (0x5dd690).
    int best = -1;
    for (const Deployed &d : _deployed) {
        const MapPart &p = _map.parts[d.part];
        if (!shown(d.part) || !inCategory(p.category, "Decoy") || !inCategory(p.category, e.validTargets) ||
            !seesPoint(e, p.x, p.y))
            continue;
        const float off = std::hypot(p.x - e.x, p.y - e.y);
        if (best < 0 || off < distance) {
            best = int(d.part);
            distance = off;
        }
    }
    return best;
}

bool MapScreen::fightDecoy(Enemy &e, float seconds) {
    // One calm that sees a decoy nearer than any officer goes after it and
    // shoots it, as an officer; it's had with its hit points (inferred).
    // Unarmed, it stands at it, as at an officer.
    if (e.decoy < 0) {
        if (e.state != "Patrol" && e.state != "Investigate" && e.state != "Recharge" && e.state != "DoSomething")
            return false;
        float distance = 0.0f;
        e.decoy = closestDecoy(e, distance);
        if (e.decoy < 0)
            return false;
        e.target = -1;
        e.route.clear();
        setState(e, "Attack");
    }
    const size_t part = size_t(e.decoy);
    if (!shown(part)) {
        e.decoy = -1;
        setState(e, "Patrol");
        return true;
    }
    if (!e.weapon)
        return true;
    const MapPart &p = _map.parts[part];
    const float distance = std::hypot(p.x - e.x, p.y - e.y);
    if (distance > e.weapon->range) {
        walkTo(e, p.x, p.y, 1.0f, seconds, runs(e, "Pursue"));
        return true;
    }
    e.angle = std::atan2(p.y - e.y, e.x - p.x);
    if (e.reload > 0.0f)
        return true;
    e.reload = e.weapon->reuseDelay;
    shoot(*e.weapon, _map.parts[e.part].model, e.x, e.y, p.x, p.y, kEnemyInstigator - int(&e - _enemies.data()));
    e.shooting = 1e-6f;
    for (Deployed &d : _deployed)
        if (d.part == part) {
            if (_mixer)
                if (auto sound = gameSound(_data, d.painSound))
                    _mixer->play(sound, audio::SoundType::kEffects);
            d.health -= e.weapon->weaponDamage;
            if (d.health <= 0.0f)
                _partStates[part].deleted = true;
        }
    return true;
}

void MapScreen::updateDeployed(float seconds) {
    // What's put down plays its sequence, each frame's Trigger actions as
    // it's reached (the stasis field's last sets its trap); played once,
    // it's then Idle (inferred).
    for (size_t k = 0; k < _deployed.size(); ++k) {
        Deployed &d = _deployed[k];
        d.time += seconds;
        const std::string playing = d.sequence;
        const BinaryObject *object = modelObject(_map.parts[d.part].model);
        const BinaryObject *sequence = nullptr;
        if (object)
            for (const NamedSequence &s : object->sequences)
                if (lower(s.name) == lower(playing) && s.sequence)
                    sequence = s.sequence.get();
        // One its model hasn't is played through at once.
        const int count = sequence && sequence->sequence ? int(sequence->sequence->frames.size()) : 0;
        if (count == 0 && !d.once)
            continue;
        const float fps = sequence ? float(sequence->object.number("Frames Per Second").value_or(15.0)) : 15.0f;
        const int reached = std::min(count, int(d.time * fps) + 1);
        const TextObject *triggers = sequence ? sequence->object.object("Trigger") : nullptr;
        const auto items = triggers ? triggers->items() : std::vector<const TextField *>{};
        const size_t part = d.part;
        for (int f = d.frames; f < reached && _deployed[k].once; ++f) {
            _deployed[k].frames = f + 1;
            if (size_t(f) < items.size() && items[size_t(f)]->object)
                runAction(items[size_t(f)]->object->object("Action"), -1, int(part));
        }
        Deployed &now = _deployed[k];
        if (now.once && now.time * fps >= float(count) && now.sequence == playing) {
            const TextObject *done = now.done;
            const int by = now.doneBy;
            now.once = false;
            now.sequence = now.then;
            now.time = 0.0f;
            now.frames = 0;
            now.done = nullptr;
            _map.parts[part].sequence = now.sequence;
            if (const auto door = _doors.find(part); door != _doors.end() && door->second.open)
                _partStates[part].collideable = false;
            runAction(done, by, int(part));
        }
    }
    for (Warp &w : _warping) {
        w.left -= seconds;
        if (w.left <= 0.0f)
            _partStates[w.part].deleted = true;
    }
    std::erase_if(_warping, [](const Warp &w) { return w.left <= 0.0f; });
    for (Officer &o : _officers)
        if (o.held > 0.0f && o.held < kHeldForever)
            o.held = std::max(0.0f, o.held - seconds);
}

void MapScreen::playSequence(size_t part, const std::string &sequence, const std::string &then, const TextObject *done,
                             int doneBy) {
    auto it = std::find_if(_deployed.begin(), _deployed.end(), [&](const Deployed &d) { return d.part == part; });
    if (it == _deployed.end()) {
        _deployed.push_back({});
        it = _deployed.end() - 1;
        it->part = part;
    }
    it->sequence = sequence;
    it->time = 0.0f;
    it->frames = 0;
    it->once = true;
    it->then = then;
    it->done = done;
    it->doneBy = doneBy;
    _map.parts[part].sequence = sequence;
}

bool MapScreen::hasSequence(size_t part, const std::string &sequence) {
    const BinaryObject *object = modelObject(_map.parts[part].model);
    if (!object)
        return false;
    return std::any_of(object->sequences.begin(), object->sequences.end(),
                       [&](const NamedSequence &s) { return lower(s.name) == lower(sequence) && s.sequence; });
}

void MapScreen::setupDoors() {
    // Each part with a CDoorAI, locked by its Is Locked, open if it shows
    // its Open sequence.
    for (size_t i = 0; i < _map.parts.size(); ++i)
        for (const TextObject *ai : _map.parts[i].activities)
            if (ai->className == "CDoorAI")
                _doors[i] = {flag(ai, "Is Locked"), lower(_map.parts[i].sequence) == "open",
                             flag(ai, "Triggered By Players"), flag(ai, "Triggered By Enemies")};
}

bool MapScreen::moveDoor(size_t part, bool open, int instigator) {
    // Its Before Open (or Closed) action, its model's Activate (or Close)
    // Sound, Opening (or Closing, else Closed) played once and then Open
    // (or Closed), and its After Opened (or Closed) action. It's in the
    // way till it's all the way open (0x48e48d, updateDeployed), and back
    // in it as it starts closing (0x48e35a). A locked one doesn't move.
    const auto it = _doors.find(part);
    if (it == _doors.end())
        return false;
    Door &door = it->second;
    if (door.locked || door.open == open)
        return true;
    door.open = open;
    const TextObject *ai = nullptr;
    for (const TextObject *a : _map.parts[part].activities)
        if (a->className == "CDoorAI")
            ai = a;
    runAction(ai ? ai->object(open ? "Before Open" : "Before Closed") : nullptr, instigator, int(part));
    if (const BinaryObject *object = modelObject(_map.parts[part].model); object && _mixer)
        if (auto sound = gameSound(_data, readText(&object->object, open ? "Activate Sound" : "Close Sound")))
            _mixer->play(sound, audio::SoundType::kEffects);
    if (!open)
        _partStates[part].collideable = _map.parts[part].collideable;
    const std::string moving = open ? "Opening" : hasSequence(part, "Closing") ? "Closing" : "Closed";
    playSequence(part, moving, open ? "Open" : "Closed",
                 ai ? ai->object(open ? "After Opened" : "After Closed") : nullptr, instigator);
    return true;
}

} // namespace reluva::stawayteam
