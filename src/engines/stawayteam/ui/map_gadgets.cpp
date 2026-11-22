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
    if (kind == "CCheckCategoryAction") {
        const std::string wanted = readText(attempt, "Category to Check");
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
        // Its Warp Sound; one that's Delete When Finished beams its part
        // away over its Warp Time (inferred: the effect's pictures aren't
        // drawn).
        if (_mixer)
            if (auto sound = gameSound(_data, readText(action, "Warp Sound")))
                _mixer->play(sound, audio::SoundType::kEffects);
        const TextObject *behavior = action->object("Warp Behavior");
        if (flag(behavior, "Delete When Finished"))
            for (size_t part : partsNamed(readText(action, "Who To Warp"), trigger))
                _warping.emplace_back(part, number(behavior, "Warp Time", 1.75f));
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
                    o.path = _wayPoints.points.empty() ? std::vector<std::pair<float, float>>{{o.taskX, o.taskY}}
                                                       : _wayPoints.path(o.x, o.y, o.taskX, o.taskY);
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

bool MapScreen::fightDecoy(Enemy &e, float seconds) {
    // One on patrol or looking about that sees a decoy (a part of category
    // Decoy: a holographic projection) goes after it and shoots it, as an
    // officer; it's had with its hit points (inferred: the program's
    // targeting isn't traced).
    if (!e.weapon)
        return false;
    if (e.decoy < 0) {
        if (e.state != "Patrol" && e.state != "Investigate")
            return false;
        for (const Deployed &d : _deployed) {
            const MapPart &p = _map.parts[d.part];
            if (!shown(d.part) || !inCategory(p.category, "Decoy"))
                continue;
            const float dx = p.x - e.x;
            const float dy = p.y - e.y;
            const float off = std::abs(std::remainder(std::atan2(dy, dx) - e.angle, 2.0f * 3.14159265f));
            if (std::hypot(dx, dy) <= number(e.ai, "Max Distance", 375.0f) &&
                off * 180.0f / 3.14159265f <= number(e.ai, "Vision Cone", 125.0f) / 2.0f &&
                clearSight(e.x, e.y, p.x, p.y)) {
                e.decoy = int(d.part);
                e.state = "Attack";
                e.target = -1;
                e.stateTime = 0.0f;
                e.route.clear();
                break;
            }
        }
        if (e.decoy < 0)
            return false;
    }
    const size_t part = size_t(e.decoy);
    if (!shown(part)) {
        e.decoy = -1;
        e.state = "Patrol";
        e.stateTime = 0.0f;
        return true;
    }
    const MapPart &p = _map.parts[part];
    const float distance = std::hypot(p.x - e.x, p.y - e.y);
    if (distance > e.weapon->range) {
        walkTo(e, p.x, p.y, 1.0f, seconds, runs(e, "Pursue"));
        return true;
    }
    e.angle = std::atan2(p.y - e.y, p.x - e.x);
    if (e.reload > 0.0f)
        return true;
    e.reload = e.weapon->reuseDelay;
    shoot(*e.weapon, _map.parts[e.part].model, e.x, e.y, p.x, p.y);
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
        if (!sequence || !sequence->sequence)
            continue;
        const int count = int(sequence->sequence->frames.size());
        const float fps = float(sequence->object.number("Frames Per Second").value_or(15.0));
        const int reached = std::min(count, int(d.time * fps) + 1);
        const TextObject *triggers = sequence->object.object("Trigger");
        const auto items = triggers ? triggers->items() : std::vector<const TextField *>{};
        const size_t part = d.part;
        for (int f = d.frames; f < reached && _deployed[k].once; ++f) {
            _deployed[k].frames = f + 1;
            if (size_t(f) < items.size() && items[size_t(f)]->object)
                runAction(items[size_t(f)]->object->object("Action"), -1, int(part));
        }
        Deployed &now = _deployed[k];
        if (now.once && now.time * fps >= float(count) && now.sequence == playing) {
            now.once = false;
            now.sequence = "Idle";
            now.time = 0.0f;
            now.frames = 0;
            _map.parts[part].sequence = "Idle";
        }
    }
    for (auto &[part, left] : _warping) {
        left -= seconds;
        if (left <= 0.0f)
            _partStates[part].deleted = true;
    }
    std::erase_if(_warping, [](const auto &w) { return w.second <= 0.0f; });
    for (Officer &o : _officers)
        if (o.held > 0.0f && o.held < kHeldForever)
            o.held = std::max(0.0f, o.held - seconds);
}

} // namespace reluva::stawayteam
