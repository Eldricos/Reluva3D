// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/unreal/vm/vm_level.h"

#include "engines/unreal/formats/game_save.h"

#include "engines/unreal/formats/mesh.h"

#include "common/fs.h"

#include <algorithm>
#include <set>
#include <chrono>
#include <fstream>
#include <iterator>
#include <cmath>
#include <map>

namespace reluva::unreal {

namespace {
std::string lowerText(const std::string &s) {
    return fs::toLower(s);
}
} // namespace

Error readLevelActors(const Package &map, std::vector<int32_t> &out) {
    out.clear();
    const std::vector<int> levels = map.exportsOfClass("Level");
    if (levels.empty())
        return Error::fail(map.name() + ": no Level object");
    const int index = levels.front();
    std::vector<uint8_t> data;
    if (Error err = map.readExport(index, data))
        return err;
    Reader r(data, size_t(map.exports()[size_t(index)].serialOffset));
    PropertyList props;
    if (Error err = beginObject(map, index, r, props))
        return err;
    // A transaction array: how many, how many allocated, the references.
    const int32_t count = r.i32();
    r.i32();
    if (!r.ok() || count < 0 || size_t(count) > r.remaining())
        return Error::fail(map.name() + ": " + std::to_string(count) + " actors don't fit");
    out.reserve(size_t(count));
    for (int32_t i = 0; i < count && r.ok(); ++i)
        out.push_back(r.index());
    return r.ok() ? Error::success() : Error::fail(map.name() + ": the actor list runs past the end");
}

ScriptLevel::ScriptLevel(Vm &vm, const Package &map) : _vm(vm), _map(map) {
    _vm.level = this;
    registerEngineNatives(_vm);
    reseedEffectRandom();
    reseedEffectPhysicsRandom();
    reseedCoreRandom();
}

ScriptLevel::~ScriptLevel() {
    if (_vm.level == this)
        _vm.level = nullptr;
}

Error ScriptLevel::load() {
    _hasModel = findLevelModel(_map, &_model) >= 0 && !_model.nodes.empty();
    if (_hasModel)
        _collision = std::make_unique<LevelCollision>(_model);
    // Movers' brushes' boxes, for colliding with them where they've moved.
    for (const Mover &m : findMovers(_vm.packages(), _map, nullptr)) {
        BrushBox b;
        b.min = {1e30f, 1e30f, 1e30f};
        b.max = {-1e30f, -1e30f, -1e30f};
        for (const Poly &p : m.polys)
            for (const Vec3f &v : p.vertices)
                for (size_t k = 0; k < 3; ++k) {
                    b.min[k] = std::min(b.min[k], v[k]);
                    b.max[k] = std::max(b.max[k], v[k]);
                }
        b.prePivot = m.prePivot;
        b.mainScale = m.mainScale;
        b.postScale = m.postScale;
        float scale = 1;
        for (size_t k = 0; k < 3; ++k)
            scale = std::max(
                scale, std::fabs(m.mainScale[k]) *
                           std::max({std::fabs(m.postScale[0]), std::fabs(m.postScale[1]), std::fabs(m.postScale[2])}));
        for (const Poly &p : m.polys)
            for (const Vec3f &v : p.vertices) {
                const float dx = v[0] - m.prePivot[0];
                const float dy = v[1] - m.prePivot[1];
                const float dz = v[2] - m.prePivot[2];
                b.radius = std::max(b.radius, std::sqrt(dx * dx + dy * dy + dz * dz) * scale);
            }
        for (size_t i = 0; i < _map.exports().size(); ++i)
            if (_map.path(int32_t(i) + 1) == m.name) {
                _brushes[int(i)] = b;
                break;
            }
    }
    std::vector<int32_t> refs;
    if (Error err = readLevelActors(_map, refs))
        return err;
    // A saved game's ghosts (actors out of the level for now) are made too.
    std::set<int32_t> ghostRefs;
    if (!readLevelExtras(_map, _saved))
        for (int32_t g : _saved.ghosts)
            if (g > 0 && ghostRefs.insert(g).second)
                refs.push_back(g);
    // First every actor, so they can refer to each other; then their
    // properties.
    std::vector<std::pair<VmObject *, int>> made;
    std::vector<VmObject *> level; // their Outer: the level, set once it's made
    for (int32_t ref : refs) {
        if (ref <= 0)
            continue;
        const int index = ref - 1;
        const Export &e = _map.exports()[size_t(index)];
        const ObjectRef cref = _vm.packages().resolve(_map, e.classRef);
        VmClass *cls = cref ? _vm.classAt(*cref.package, cref.exportIndex) : nullptr;
        if (!cls) {
            _vm.reportError("the level's actor " + _map.path(ref) + " has no class that loads");
            continue;
        }
        // UnrealEd keeps its viewports' cameras in the level as Camera
        // actors, which are PlayerPawns: left out, or the player logging
        // in would take one over (GameInfo.Login reuses a PlayerPawn
        // without a player).
        bool camera = false;
        for (const VmClass *c = cls; c && !camera; c = c->parent())
            camera = lowerText(c->name) == "camera";
        if (camera) {
            ++_editorCameras;
            continue;
        }
        VmObject *a = _vm.create(cls, _vm.name(_map.objectName(ref)));
        _vm.adopt(_map, index, a);
        level.push_back(a);
        made.push_back({a, index});
        (ghostRefs.count(ref) ? _ghosts : _actors).push_back(a);
    }
    for (const auto &[a, index] : made) {
        std::vector<uint8_t> data;
        PropertyList props;
        if (Error err = _map.readExport(index, data)) {
            _vm.reportError(err.message);
            continue;
        }
        Reader r(data, size_t(_map.exports()[size_t(index)].serialOffset));
        StateFrame frame;
        if (Error err = beginObject(_map, index, r, props, &frame)) {
            _vm.reportError(err.message);
            continue;
        }
        if (frame.has)
            _savedFrames[a] = frame;
        _vm.applyProperties(a, _map, props);
        _localizedSet += _vm.applyLocalizedActor(a, _map.name());
        if (!_levelInfo && _vm.isA(a, "LevelInfo"))
            _levelInfo = a;
        // A mover's brush's own BSP (UnrealEd builds one for each), to
        // collide with it where it's moved to.
        if (_vm.isA(a, "Mover"))
            _movers.push_back(a);
        if (auto it = _brushes.find(index); it != _brushes.end())
            if (Value *brush = a->find(_vm, "Brush"); brush && brush->asObject() && brush->o->package) {
                auto model = std::make_shared<Model>();
                if (!readModel(*brush->o->package, brush->o->exportIndex, *model, false) && !model->nodes.empty()) {
                    it->second.collision = std::make_shared<LevelCollision>(*model);
                    it->second.model = std::move(model);
                }
            }
    }
    if (!_levelInfo)
        return Error::fail(_map.name() + ": no LevelInfo among its actors");
    const std::vector<int> levels = _map.exportsOfClass("Level");
    _levelObject = levels.empty() ? nullptr : _vm.objectAt(_map, levels.front() + 1);
    for (VmObject *a : level)
        if (Value *v = a->find(_vm, "Outer"))
            v->o = _levelObject;
    for (VmObject *a : _actors)
        init(a);
    loadPaths();
    return Error::success();
}

void ScriptLevel::init(VmObject *a) {
    if (Value *v = a->find(_vm, "Level"))
        v->o = _levelInfo;
    if (Value *v = a->find(_vm, "XLevel"))
        v->o = _levelObject;
}

// The level's start, as UGameEngine::LoadMap: the game type spawned (while
// the level hasn't begun play, so it hears none of its events: the engine
// passes an actor's events to its script only once LevelInfo.bBegunPlay),
// the ghosts set aside, then bBegunPlay and bStartup, InitGame, and six
// passes over every actor (the GameInfo and LevelInfo too), each reaching
// those spawned on the way: SetInitialState, PreBeginPlay, BeginPlay, its
// zone (ZoneChange if it's in a ZoneInfo's), PostBeginPlay, its base.
void ScriptLevel::beginPlay(const std::string &defaultGame) {
    _vm.holdEvents = true;
    if (Value *v = _levelInfo->find(_vm, "bHighDetailMode"))
        *v = Value::boolValue(true);
    // The game type: the level's own, else the default.
    VmClass *gameClass = nullptr;
    if (Value *v = _levelInfo->find(_vm, "DefaultGameType"); v && v->asObject() && v->o->asClass)
        gameClass = v->o->asClass;
    if (!gameClass)
        gameClass = _vm.findClass(defaultGame);
    if (gameClass) {
        _game = spawn(gameClass, nullptr, 0, nullptr, nullptr);
        if (Value *v = _levelInfo->find(_vm, "Game"))
            v->o = _game;
    } else {
        _vm.reportError("no game type " + defaultGame);
    }
    for (const char *field : {"TimeSeconds", "NoPauseTimeSeconds"})
        if (Value *v = _levelInfo->find(_vm, field))
            v->f = 0;
    // Those placed as ghosts (The Fallen's bIsGhost: a guard who arrives
    // later, an effect for a scene) set aside before anything starts, until
    // an event brings them back (GhostActors, MakeGhost), when they start
    // as if spawned.
    {
        const std::vector<VmObject *> placed = _actors;
        for (VmObject *a : placed)
            if (const Value *ghost = a != _game ? prop(a, "bIsGhost") : nullptr; ghost && ghost->asBool()) {
                makeGhost(a, true);
                ++_placedGhosts;
            }
    }
    for (VmObject *a : _actors)
        if (Value *v = prop(a, "Touching"))
            for (size_t k = 0; k < 4; ++k)
                v[k] = Value::objectValue(nullptr);
    if (Value *v = _levelInfo->find(_vm, "bBegunPlay"))
        *v = Value::boolValue(true);
    if (Value *v = _levelInfo->find(_vm, "bStartup"))
        *v = Value::boolValue(true);
    _vm.holdEvents = false;
    if (_game) {
        Value error = Value::stringValue("");
        if (VmFunction *f = _vm.findFunction(_game, _vm.name("InitGame")))
            _vm.invoke(f, _game, {Value::stringValue(_options), error}, {nullptr, &error});
    }
    auto pass = [&](auto &&each) {
        for (size_t i = 0; i < _actors.size(); ++i)
            if (VmObject *a = _actors[i]; !a->deleted)
                each(a);
    };
    pass([&](VmObject *a) { _vm.callEvent(a, "SetInitialState"); });
    pass([&](VmObject *a) { _vm.callEvent(a, "PreBeginPlay"); });
    pass([&](VmObject *a) {
        _vm.callEvent(a, "BeginPlay");
        if (Value *v = prop(a, "bHasBegunPlay"))
            *v = Value::boolValue(true);
    });
    pass([&](VmObject *a) { startZone(a); });
    pass([&](VmObject *a) { _vm.callEvent(a, "PostBeginPlay"); });
    pass([&](VmObject *a) { startBase(a); });
    if (Value *v = _levelInfo->find(_vm, "bStartup"))
        *v = Value::boolValue(false);
}

// Its zone at the start: the level's, then the one it's in; in a ZoneInfo's,
// ZoneChange (and nothing to the zones). Its body's parts too, quietly.
void ScriptLevel::startZone(VmObject *a) {
    Value *region = prop(a, "Region");
    const Value *location = prop(a, "Location");
    if (!region || !location || !_hasModel)
        return;
    _vm.assign(*region, regionAt(Vm::vectorOf(*location)));
    VmObject *zone = !region->fields.empty() ? region->fields[0].asObject() : nullptr;
    if (zone && zone != _levelInfo)
        _vm.callEvent(a, "ZoneChange", {Value::objectValue(zone)});
    if (!a->deleted)
        updateBodyRegions(a, false);
}

// Its base at the start: with an AttachTag, the actor it names (the last of
// them); else, for a decoration, item or pawn colliding with the world,
// with no base and not moving (Physics none or rotating), whatever it
// stands on (8 units down), the world being none.
void ScriptLevel::startBase(VmObject *a) {
    if (const Value *t = prop(a, "AttachTag"); t && t->i != 0) {
        VmObject *onto = nullptr;
        for (VmObject *b : _actors)
            if (const Value *tag = b != a && !b->deleted ? prop(b, "Tag") : nullptr; tag && tag->i == t->i)
                onto = b;
        if (onto)
            setBase(a, onto);
        return;
    }
    const Value *base = prop(a, "Base");
    const Value *world = prop(a, "bCollideWorld");
    const Value *physics = prop(a, "Physics");
    if (!base || base->asObject() || !world || !world->asBool() || !physics ||
        (physics->asInt() != 0 && physics->asInt() != 5))
        return;
    if (!_vm.isA(a, "Decoration") && !_vm.isA(a, "Inventory") && !_vm.isA(a, "Pawn"))
        return;
    const MoveHit h = testMove(a, {0, 0, -8});
    if (h.hit() && h.actor && h.actor != _levelInfo)
        setBase(a, h.actor);
}

VmObject *ScriptLevel::login(const std::string &playerClass, const std::string &playerName, std::string &error,
                             const std::string &portal) {
    error.clear();
    if (!_game) {
        error = "no game type to log in to";
        return nullptr;
    }
    // Which character, as SpawnPlayActor picks it: the Player of the
    // level's CTVE_PlayerStarts that name one (enabled, or tagged as the
    // portal arrived by). With no portal, the first; with one, the one
    // tagged so, else the last. Else the URL's class, else Sisko.
    VmClass *cls = nullptr;
    {
        VmObject *chosen = nullptr;
        bool matched = false;
        const NameId portalName = portal.empty() ? 0 : _vm.name(portal);
        for (VmObject *a : _actors) {
            if (a->deleted || !_vm.isA(a, "CTVE_PlayerStart"))
                continue;
            const Value *p = prop(a, "Player");
            if (!p || !p->asObject() || !p->o->asClass)
                continue;
            const Value *tag = prop(a, "Tag");
            const bool tagged = portalName && tag && tag->i == portalName;
            const Value *enabled = prop(a, "bEnabled");
            if (!(enabled && enabled->asBool()) && !tagged)
                continue;
            if (!portalName) {
                if (!chosen)
                    chosen = a;
            } else if (!matched) {
                chosen = a;
                matched = tagged;
            }
        }
        if (chosen)
            cls = prop(chosen, "Player")->o->asClass;
    }
    if (!cls && !playerClass.empty())
        cls = _vm.findClass(playerClass);
    if (!cls)
        cls = _vm.findClass("PLYR.DS9_Sisko");
    if (!cls) {
        error = "no player class";
        return nullptr;
    }
    VmFunction *login = _vm.findFunction(_game, _vm.name("Login"));
    if (!login) {
        error = _game->cls->name + " has no Login";
        return nullptr;
    }
    const std::string options = "?Name=" + playerName + "?Class=" + cls->package->name() + "." + cls->name + _options;
    Value message = Value::stringValue("");
    const Value pawn =
        _vm.invoke(login, _game,
                   {Value::stringValue(portal), Value::stringValue(options), message, Value::objectValue(cls->object)},
                   {nullptr, nullptr, &message, nullptr});
    _player = pawn.asObject();
    if (!_player) {
        error = "Login refused: " + (message.s.empty() ? std::string("no reason given") : message.s);
        return nullptr;
    }
    // Then as SpawnPlayActor goes on: the viewport (Possess), an
    // administrator (playing alone), TravelPreAccept; what travelled with
    // the player put back (its own properties, its things spawned), each
    // of those told TravelPreAccept, last first (their GiveTo rebuilds
    // the inventory in its old order); the game's AcceptInventory; each
    // TravelPostAccept, last first; the player's TravelPostAccept;
    // PostLogin.
    attachViewport();
    _vm.callEvent(_player, "Possess");
    if (Value *v = prop(_player, "bAdmin"))
        *v = Value::boolValue(true);
    _vm.callEvent(_player, "TravelPreAccept");
    const std::vector<VmObject *> travelled = acceptTravel(_incomingTravel);
    for (auto it = travelled.rbegin(); it != travelled.rend(); ++it)
        if (!(*it)->deleted)
            _vm.callEvent(*it, "TravelPreAccept");
    _vm.callEvent(_game, "AcceptInventory", {Value::objectValue(_player)});
    for (auto it = travelled.rbegin(); it != travelled.rend(); ++it)
        if (!(*it)->deleted)
            _vm.callEvent(*it, "TravelPostAccept");
    _vm.callEvent(_player, "TravelPostAccept");
    _vm.callEvent(_game, "PostLogin", {Value::objectValue(_player)});
    if (_carriedPlayedTime >= 0)
        if (Value *v = prop(_player, "GamePlayedTime"))
            *v = Value::floatValue(_carriedPlayedTime);
    return _player;
}

// The engine's side of the player: a viewport (a class with no script,
// made up as a kind of Player) with the .ini's console.
void ScriptLevel::attachViewport() {
    VmClass *playerBase = _vm.findClass("Engine.Player");
    VmClass *viewportClass = _vm.defineIntrinsic("Viewport", playerBase);
    VmObject *viewport = _vm.create(viewportClass, _vm.name("Viewport0"));
    if (Value *v = viewport->find(_vm, "Actor"))
        v->o = _player;
    if (VmClass *consoleClass = _vm.findClass(iniSetting(_vm.packages(), "Engine.Engine", "Console"))) {
        VmObject *console = _vm.create(consoleClass, 0, viewport);
        if (Value *v = viewport->find(_vm, "Console"))
            v->o = console;
        if (Value *v = console->find(_vm, "Viewport"))
            v->o = viewport;
    }
    if (Value *v = _player->find(_vm, "Player"))
        v->o = viewport;
}

Value ScriptLevel::regionAt(const std::array<float, 3> &p) {
    int zone = 0;
    int leaf = -1;
    if (_hasModel) {
        int32_t n = 0;
        for (int guard = 0; guard < 100000 && n >= 0 && size_t(n) < _model.nodes.size(); ++guard) {
            const BspNode &node = _model.nodes[size_t(n)];
            const bool front = node.plane[0] * p[0] + node.plane[1] * p[1] + node.plane[2] * p[2] - node.plane[3] >= 0;
            zone = node.zone[front ? 1 : 0];
            leaf = node.leaf[front ? 1 : 0];
            n = front ? node.front : node.back;
            if (n <= 0)
                break;
        }
    }
    Value v;
    Value *saved = _levelInfo ? _levelInfo->find(_vm, "Region") : nullptr;
    if (saved && saved->type == VType::kStruct) {
        v = *saved;
        if (const VmStruct *st = v.st) {
            VmObject *zoneActor =
                size_t(zone) < _model.zones.size() ? _vm.objectAt(_map, _model.zones[size_t(zone)].actor) : nullptr;
            // A zone with no ZoneInfo of its own (or outside them all) is
            // the level's: LevelInfo is the default zone, as in UE1.
            // Scripts read Region.Zone everywhere (the player's ViewFlash,
            // UnderWaterSounds, HandleWalking took None 150,000 times).
            if (!zoneActor)
                zoneActor = _levelInfo;
            if (VmProperty *f = st->findProperty(_vm.name("Zone")))
                v.fields[size_t(f->slot)] = Value::objectValue(zoneActor);
            if (VmProperty *f = st->findProperty(_vm.name("iLeaf")))
                v.fields[size_t(f->slot)] = Value::intValue(leaf);
            if (VmProperty *f = st->findProperty(_vm.name("ZoneNumber")))
                v.fields[size_t(f->slot)] = Value::byteValue(zone);
        }
    }
    return v;
}

void ScriptLevel::litActors(std::vector<LitActor> &out) {
    out.clear();
    auto byte = [&](VmObject *a, const char *name, uint8_t otherwise) {
        const Value *v = prop(a, name);
        return v ? uint8_t(v->asInt()) : otherwise;
    };
    for (VmObject *a : _actors) {
        if (a->deleted)
            continue;
        const Value *type = prop(a, "LightType");
        if (!type || type->asInt() == 0)
            continue;
        LitActor l;
        l.brightness = byte(a, "LightBrightness", 0);
        const Value *loc = prop(a, "Location");
        if (!l.brightness || !loc)
            continue;
        l.actor = a;
        l.location = Vm::vectorOf(*loc);
        l.type = uint8_t(type->asInt());
        l.hue = byte(a, "LightHue", 0);
        l.saturation = byte(a, "LightSaturation", 255);
        l.radius = byte(a, "LightRadius", 0);
        l.period = byte(a, "LightPeriod", 32);
        l.phase = byte(a, "LightPhase", 0);
        l.effect = byte(a, "LightEffect", 0);
        const Value *isStatic = prop(a, "bStatic");
        l.isStatic = isStatic && isStatic->asBool();
        out.push_back(l);
    }
}

// A noise an actor makes (MakeNoise; a sound played without bNoHearNoise):
// each pawn but the player and whoever made it hears it if it's loud
// enough where the pawn is: Loudness x 800 over the distance, halved with
// the world between, against its HearingThreshold (with the usual 1, a
// shot of loudness 1 carries 800 units in the open). Its HearNoise
// (Loudness, the maker): The Fallen's AI turns to search, or attacks if
// it can see the maker. One noise a pawn every fifth of a second. Reluva's
// reading: UE1's rule isn't in the scripts.
void ScriptLevel::makeNoise(VmObject *maker, float loudness) {
    if (!maker || maker->deleted || loudness <= 0)
        return;
    const Value *at = prop(maker, "Location");
    if (!at)
        return;
    const auto from = Vm::vectorOf(*at);
    const Value *ownerValue = prop(maker, "Owner");
    VmObject *owner = ownerValue ? ownerValue->asObject() : nullptr;
    const Value *instigatorValue = prop(maker, "Instigator");
    VmObject *instigator = instigatorValue ? instigatorValue->asObject() : nullptr;
    // Only a pawn's noise is heard (one's own, its weapon's or shot's, or
    // what it set off: an Instigator), as UE1's MakeNoise asks; the
    // level's ambient sounds aren't.
    auto isPawn = [&](VmObject *a) {
        if (!a || a->deleted)
            return false;
        auto it = _pawnClasses.find(a->cls);
        if (it == _pawnClasses.end())
            it = _pawnClasses.emplace(a->cls, _vm.isA(a, "Pawn")).first;
        return it->second;
    };
    if (!isPawn(maker) && !isPawn(instigator) && !isPawn(owner))
        return;
    ++noise.made;
    const std::vector<VmObject *> now = _actors;
    for (VmObject *p : now) {
        if (p == maker || p == owner || p == instigator || p == _player || p->deleted)
            continue;
        if (!isPawn(p))
            continue;
        const Value *health = prop(p, "Health");
        if (health && health->asInt() <= 0)
            continue;
        float &quiet = _noiseQuiet[p];
        if (quiet > _time)
            continue;
        const Value *loc = prop(p, "Location");
        if (!loc)
            continue;
        const auto to = Vm::vectorOf(*loc);
        const float dx = to[0] - from[0];
        const float dy = to[1] - from[1];
        const float dz = to[2] - from[2];
        const float dist = std::max(1.0f, std::sqrt(dx * dx + dy * dy + dz * dz));
        float heard = loudness * 800 / dist;
        const Value *threshold = prop(p, "HearingThreshold");
        const float need = threshold ? std::max(0.01f, threshold->asFloat()) : 1;
        if (heard < need)
            continue;
        if (!lineOfSight(p, from, to))
            heard *= 0.5f;
        if (heard < need)
            continue;
        quiet = _time + 0.2f;
        ++noise.heard;
        _vm.callEvent(p, "HearNoise", {Value::floatValue(heard), Value::objectValue(maker)});
    }
}

void ScriptLevel::updateRegion(VmObject *a) {
    if (!a || !_hasModel)
        return;
    Value *location = prop(a, "Location");
    Value *region = prop(a, "Region");
    if (location && region)
        _vm.assign(*region, regionAt(Vm::vectorOf(*location)));
    updateBodyRegions(a, false);
}

// A pawn's feet, chest (The Fallen's: ChestHeight up its height) and head
// (its eyes) are each in a zone too, which the engine keeps up to date
// (FootRegion, ChestRegion, HeadRegion: wading, swimming, its breath
// underwater), telling its scripts as each changes zone
// (FootZoneChange, ChestZoneChange, HeadZoneChange) when `events`.
void ScriptLevel::updateBodyRegions(VmObject *a, bool events) {
    Value *foot = prop(a, "FootRegion");
    Value *head = prop(a, "HeadRegion");
    const Value *location = prop(a, "Location");
    if (!foot || !head || !location || !_hasModel)
        return;
    const auto at = Vm::vectorOf(*location);
    const Value *h = prop(a, "CollisionHeight");
    const float height = h ? h->asFloat() : 0;
    const Value *eye = prop(a, "EyeHeight");
    const Value *chestHeight = prop(a, "ChestHeight");
    struct Part {
        Value *region;
        float z;
        const char *event;
    };
    // The engine's: the feet at the bottom; the player's chest
    // (CTVE_PlayerPawn's) ChestHeight up its standing height (its height
    // when swimming); the head at its eyes. (Engine.dll was read as giving
    // the player no head region, but its scripts read one throughout.)
    const bool player = _vm.isA(a, "CTVE_PlayerPawn");
    float chestZ = 0;
    if (player) {
        const Value *stand = prop(a, "StandCollisionHeight");
        const Value *physics = prop(a, "Physics");
        const float tall =
            physics && physics->asInt() == 3 /* PHYS_Swimming */ ? height : (stand ? stand->asFloat() : height);
        chestZ = at[2] + (chestHeight ? chestHeight->asFloat() : 0.65f) * tall;
    }
    Part parts[3] = {{foot, at[2] - height, "FootZoneChange"},
                     {player ? prop(a, "ChestRegion") : nullptr, chestZ, "ChestZoneChange"},
                     {head, at[2] + (eye ? eye->asFloat() : height * 0.5f), "HeadZoneChange"}};
    for (Part &p : parts) {
        if (!p.region)
            continue;
        const VmObject *before = !p.region->fields.empty() ? p.region->fields[0].asObject() : nullptr;
        _vm.assign(*p.region, regionAt({at[0], at[1], p.z}));
        VmObject *now = !p.region->fields.empty() ? p.region->fields[0].asObject() : nullptr;
        if (events && now && before && now != before && !a->deleted)
            _vm.callEvent(a, p.event, {Value::objectValue(now)});
    }
}

bool ScriptLevel::paused() const {
    const Value *v = _levelInfo ? _levelInfo->find(_vm, "Pauser") : nullptr;
    return v && !v->s.empty();
}

// A level tick, as ULevel::Tick: the time scaled by TimeDilation and
// counted (unless paused), then kept within 0.005 to 0.4 of a second for
// the actors (DeltaTime). Each actor that isn't static, in the level's
// order (those spawned on the way too), as AActor::Tick: its animation;
// the player its input and PlayerTick, any other its Tick; its state
// code; its timer; its life span (Expired: gone, no physics); its
// physics; a player's UpdateEyeHeight. Paused, the player only reads its
// input (its axes 0) and only bAlwaysTick actors tick.
void ScriptLevel::tick(float seconds) {
    const bool stopped = paused();
    if (const Value *d = _levelInfo->find(_vm, "TimeDilation"); d && d->asFloat() > 0)
        seconds *= d->asFloat();
    if (!stopped) {
        _time += seconds;
        if (Value *v = _levelInfo->find(_vm, "TimeSeconds"))
            v->f += seconds;
    }
    if (Value *v = _levelInfo->find(_vm, "NoPauseTimeSeconds"))
        v->f += seconds;
    seconds = std::clamp(seconds, 0.005f, 0.4f);
    if (Value *v = _levelInfo->find(_vm, "DeltaTime"))
        v->f = seconds;
    // As ULevel::Tick: each actor in the level's order, those spawned on
    // the way too; one whose owner hasn't ticked yet waits for it, ticked
    // in a pass after (and another, while those passes get anywhere).
    std::vector<VmObject *> later;
    bool progress = false;
    for (size_t i = 0; i < _actors.size(); ++i) {
        VmObject *a = _actors[i];
        if (a->deleted || a == _levelInfo)
            continue;
        progress |= tickActor(a, seconds, stopped, later);
    }
    while (!later.empty() && progress) {
        std::vector<VmObject *> pass;
        pass.swap(later);
        progress = false;
        for (VmObject *a : pass)
            if (!a->deleted && _ticked[a] != _tickedFlag)
                progress |= tickActor(a, seconds, stopped, later);
    }
    _tickedFlag = !_tickedFlag;
    // What's attached (C3DAttach) goes where its holder now is.
    placeAttached();
    // The console ticks as the client does, paused or not (its UWindow
    // menus).
    if (VmObject *con = console())
        _vm.callEvent(con, "Tick", {Value::floatValue(seconds)});
    // The dialogue, paused or not.
    tickDialog(seconds);
    // The audio driver's update: the mouths that follow the sounds.
    lipSyncUpdate();
    tickFlis(seconds);
    ++_ticks;
    for (VmObject *a : _actors)
        if (a->deleted)
            _ticked.erase(a);
    _actors.erase(std::remove_if(_actors.begin(), _actors.end(), [](VmObject *a) { return a->deleted; }),
                  _actors.end());
}

void ScriptLevel::zonesRendered(uint64_t zones, int cameraZone) {
    const float now = _levelInfo ? pf(_levelInfo, "TimeSeconds") : 0;
    for (size_t z = 0; z < _zoneRenderTime.size(); ++z)
        if ((zones >> z) & 1)
            _zoneRenderTime[z] = now;
    if (cameraZone >= 0)
        _cameraZone = cameraZone;
    _renderedTick = _ticks;
}

// The Fallen's sleep (the engine's ShutDown), at the top of AActor::Tick:
// an actor not seen for 5 seconds is asked to sleep, and asleep has no
// tick at all (no animation, Tick, state code, timer or physics) until
// it's seen again; one that won't is asked again in 2 seconds. Never one
// with bMustNotShutDown, a life span, or in zone 0. "Seen", its own
// LastRenderTime (which drawing sets), or for one that shows nothing of
// its own (hidden, bWakeWhenSeeZone, drawn as nothing, a mesh or sprite
// without one) when its zone was last drawn; with the view in zone 0,
// now. Asleep (bForceStasis), it wakes once seen in the last 5 seconds
// (DisableShutdownForSomeTime puts LastRenderTime forward), else after 6
// to 7 is asked again: to sleep on, 5 seconds back; not, now.
bool ScriptLevel::sleeping(VmObject *a) {
    const auto objectProp = [&](VmObject *o, const char *name) {
        const Value *v = prop(o, name);
        return v ? v->asObject() : nullptr;
    };
    // A MultiMesh whose list has nothing in it (CTVE_MultiMesh, a
    // CTVE_DllObject: no Next), whatever it's drawn as.
    const auto emptyMultiMesh = [&] {
        VmObject *m = objectProp(a, "MultiMesh");
        const Value *next = m ? m->find(_vm, "Next") : nullptr;
        return m && (!next || !next->asObject());
    };
    // No view (all seen), or a cut scene being skipped (the engine's
    // bFastForward, FastForward's).
    if (!_levelInfo || _renderedTick < _ticks - 2 || fastForward())
        return false;
    if (pb(a, "bMustNotShutDown") || pf(a, "LifeSpan") != 0)
        return false;
    const Value *region = prop(a, "Region");
    const int zone = region && region->fields.size() > 2 ? region->fields[2].asInt() : 0;
    if (zone <= 0 || zone >= int(_zoneRenderTime.size()))
        return false;
    Value *drawn = prop(a, "LastRenderTime");
    Value *stasis = prop(a, "bForceStasis");
    if (!drawn || !stasis)
        return false;
    const float now = pf(_levelInfo, "TimeSeconds");
    const int drawType = int(pf(a, "DrawType"));
    const bool byZone = pb(a, "bHidden") || pb(a, "bWakeWhenSeeZone") || (drawType == 0 && !_vm.isA(a, "Decal")) ||
                        (drawType == 2 && !objectProp(a, "Mesh")) ||
                        ((drawType == 1 || drawType == 7) && !objectProp(a, "Texture")) || emptyMultiMesh();
    if (byZone)
        *drawn = Value::floatValue(_cameraZone <= 0 ? now : _zoneRenderTime[size_t(zone)]);
    const float elapsed = now - drawn->asFloat();
    if (!stasis->asBool()) {
        if (elapsed < 5)
            return false;
        if (!mayShutDown(a, now, zone)) {
            *drawn = Value::floatValue(now - 3);
            return false;
        }
        *stasis = Value::boolValue(true);
        if (pb(a, "bDestroyOnShutDown"))
            destroy(a);
        return true;
    }
    if (elapsed <= 5) {
        *stasis = Value::boolValue(false);
        return false;
    }
    if (elapsed <= 6 + std::uniform_real_distribution<float>(0, 1)(_sleepRandom))
        return true;
    if (mayShutDown(a, now, zone)) {
        *drawn = Value::floatValue(now - 5);
        return true;
    }
    *drawn = Value::floatValue(now);
    *stasis = Value::boolValue(false);
    return false;
}

// ShutDown's say: with bAskMeBeforeShuttingDown, the script's
// CanIShutYouDown first; with bWakeWhenSeePlayer (and not by its zone),
// not while the player can see it (its zone drawn in the last 5 seconds
// and nothing of the world between).
bool ScriptLevel::mayShutDown(VmObject *a, float now, int zone) {
    if (pb(a, "bAskMeBeforeShuttingDown") && !_vm.callEvent(a, "CanIShutYouDown").asBool())
        return false;
    if (a->deleted)
        return false;
    if (pb(a, "bWakeWhenSeePlayer") && !pb(a, "bWakeWhenSeeZone")) {
        if (now - _zoneRenderTime[size_t(zone)] >= 5)
            return true;
        if (!_player)
            return true;
        return !lineOfSight(a, pv(_player, "Location"), pv(a, "Location"));
    }
    return true;
}

// One actor's tick, as AActor::Tick: static ones not at all; then The
// Fallen's sleep; one whose Owner hasn't ticked this time put off
// (`later`); then its animation, its input and PlayerTick or its Tick,
// state code, timer, life span (Expired: gone, no physics), physics, a
// player's UpdateEyeHeight, and a pawn's PainTimer counting down.
bool ScriptLevel::tickActor(VmObject *a, float seconds, bool stopped, std::vector<VmObject *> &later) {
    if (const Value *st = prop(a, "bStatic"); st && st->asBool())
        return false;
    if (stopped && a != _player) {
        const Value *always = prop(a, "bAlwaysTick");
        if (!always || !always->asBool())
            return false;
    }
    if (a == _player) {
        // The input axes, as the engine sets them each frame: those
        // the keys are bound to (DefUser.ini's MoveForward is "Axis
        // aBaseY Speed=+300", TurnRight "Axis aBaseX Speed=+150"...),
        // the rest 0; PlayerInput works out the others from them. A
        // held key adds its speed times the frame's time, and the
        // engine then scales the axes by 20 over the frame's time: 20
        // times the speed in all (the scripts' climbing speed,
        // aForward x 0.02, is 120 a second). Paused, all 0.
        constexpr float kHeld = 20;
        const float on = stopped ? 0.0f : 1.0f;
        const std::pair<const char *, float> axes[] = {
            {"aBaseY", on * controls.forward * 300 * kHeld},
            {"aStrafe", on * controls.strafe * 300 * kHeld},
            {"aBaseX", on * controls.turn * 150 * kHeld},
            {"aLookUp", on * controls.lookUp * 300 * kHeld},
            {"aMouseX", on * controls.mouseX},
            {"aMouseY", on * controls.mouseY},
            {"aUp", on * ((controls.jump ? 300.0f : 0.0f) - (controls.duck ? 300.0f : 0.0f)) * kHeld},
            {"aForward", 0},
            {"aTurn", 0},
            {"aBaseZ", 0},
        };
        for (const auto &[axis, value] : axes)
            if (Value *v = a->find(_vm, axis))
                v->f = value;
        controls.mouseX = controls.mouseY = 0; // the mouse's movement goes into one tick
        for (const auto &[button, held] : {std::pair{"bExtra1", controls.jump}, std::pair{"bDuck", controls.duck},
                                           std::pair{"bFire", controls.fire}})
            if (Value *v = a->find(_vm, button))
                v->i = held && !stopped ? 1 : 0;
        if (stopped) {
            _vm.callEvent(a, "PlayerInput", {Value::floatValue(seconds)});
            return false;
        }
    }
    if (sleeping(a))
        return false;
    if (const Value *o = prop(a, "Owner"))
        if (VmObject *owner = o->asObject(); owner && !owner->deleted && _ticked[owner] != _tickedFlag) {
            later.push_back(a);
            return false;
        }
    _ticked[a] = _tickedFlag;
    animate(a, seconds);
    if (a->deleted)
        return true;
    if (a == _player) {
        _vm.callEvent(a, "PlayerInput", {Value::floatValue(seconds)});
        if (Value *old = prop(a, "OldLocation"))
            if (const Value *at = prop(a, "Location"))
                _vm.assign(*old, *at);
        _vm.callEvent(a, "PlayerTick", {Value::floatValue(seconds)});
    }
    _vm.tick(a, seconds, a != _player);
    // A life span running out: Expired, then gone. Any but 0 counts down
    // (one below 0 at once).
    if (Value *life = a->deleted ? nullptr : prop(a, "LifeSpan"); life && life->asFloat() != 0) {
        life->f -= seconds;
        if (life->f <= 0.0001f) {
            _vm.callEvent(a, "Expired");
            destroy(a);
            return true;
        }
    }
    if (!a->deleted)
        physics(a, seconds);
    // Once it has moved, a player's UpdateEyeHeight: the view's height,
    // and in The Fallen the arms' IK, which puts the weapon in hand.
    if (!a->deleted)
        if (const Value *p = prop(a, "bIsPlayer"); p && p->asBool())
            _vm.callEvent(a, "UpdateEyeHeight", {Value::floatValue(seconds)});
    if (!a->deleted)
        moveMultiMeshes(a, seconds);
    // The engine only counts PainTime down (a full tick, a pawn): its
    // scripts set it (in water, UnderWaterTime; in a pain zone, 0.01) and
    // PainTimer does the drowning or the zone's harm, then sets it again.
    if (!a->deleted && !stopped) {
        auto it = _pawnClasses.find(a->cls);
        if (it == _pawnClasses.end())
            it = _pawnClasses.emplace(a->cls, _vm.isA(a, "Pawn")).first;
        if (Value *pain = it->second ? prop(a, "PainTime") : nullptr; pain && pain->asFloat() > 0) {
            *pain = Value::floatValue(pain->asFloat() - seconds);
            if (pain->asFloat() < 0.001f) {
                *pain = Value::floatValue(0);
                _vm.callEvent(a, "PainTimer");
            }
        }
    }
    return true;
}

VmObject *ScriptLevel::spawn(VmClass *cls, VmObject *owner, NameId tag, const Value *location, const Value *rotation) {
    if (!cls || (cls->classFlags & 0x1)) { // abstract
        _vm.reportError("Spawn of " + std::string(cls ? "abstract class " + cls->name : "no class"));
        return nullptr;
    }
    VmObject *a = _vm.create(cls, 0, _levelObject);
    if (!a->isActor) {
        a->deleted = true;
        _vm.reportError("Spawn of " + cls->name + ", not an actor");
        return nullptr;
    }
    _actors.push_back(a);
    _ticked[a] = !_tickedFlag; // not ticked yet this time
    init(a);
    if (Value *v = a->find(_vm, "Owner"))
        v->o = owner;
    if (Value *v = a->find(_vm, "Tag"))
        v->i = tag ? tag : cls->nameId;
    // Where it fits, if it collides with the world (or bCollideWhenPlacing);
    // as SpawnActor does, if it fits nowhere near it isn't spawned.
    if (location && !place(a, Vm::vectorOf(*location), true)) {
        a->deleted = true;
        _actors.erase(std::remove(_actors.begin(), _actors.end(), a), _actors.end());
        ++movement.spawnsRefused;
        if (_vm.logger)
            _vm.logger("Log: SpawnActor failed because of collision at the spawn location [" + cls->name + "]");
        return nullptr;
    }
    if (rotation)
        if (Value *v = a->find(_vm, "Rotation"))
            _vm.assign(*v, *rotation);
    updateRegion(a);
    if (owner && !owner->deleted)
        _vm.callEvent(owner, "GainedChild", {Value::objectValue(a)});
    startAsSpawned(a);
    return a->deleted ? nullptr : a;
}

// SpawnActor's events: SetInitialState, Spawned, PreBeginPlay, BeginPlay,
// its zone, PostBeginPlay.
void ScriptLevel::startAsSpawned(VmObject *a) {
    for (const char *event : {"SetInitialState", "Spawned", "PreBeginPlay", "BeginPlay"}) {
        if (a->deleted)
            return;
        _vm.callEvent(a, event);
    }
    if (a->deleted)
        return;
    updateRegion(a);
    if (!a->deleted)
        _vm.callEvent(a, "PostBeginPlay");
    if (Value *v = prop(a, "bHasBegunPlay"))
        *v = Value::boolValue(true);
}

void ScriptLevel::destroy(VmObject *a) {
    if (!a || a->deleted)
        return;
    _vm.callEvent(a, "Destroyed");
    if (Value *owner = a->find(_vm, "Owner"); owner && owner->asObject())
        _vm.callEvent(owner->o, "LostChild", {Value::objectValue(a)});
    if (Value *v = a->find(_vm, "bDeleteMe"))
        v->i = 1;
    a->deleted = true;
    _bends.erase(a);
    _ghosts.erase(std::remove(_ghosts.begin(), _ghosts.end(), a), _ghosts.end());
    _decals.erase(a);
    _sparkFlash.erase(a);
    _clambers.erase(a);
    _noiseQuiet.erase(a);
}

void ScriptLevel::makeGhost(VmObject *a, bool ghost) {
    if (!a || a->deleted)
        return;
    const bool now = std::find(_ghosts.begin(), _ghosts.end(), a) != _ghosts.end();
    // (bIsGhost may say so already: placed as one.)
    if (ghost == now)
        return;
    if (ghost) {
        // As the engine destroys an actor, without destroying it (no
        // Destroyed, not bDeleteMe): EndState; no base, and nothing based
        // on it; its own actors without an owner; done touching (UnTouch
        // both ways); its owner's LostChild.
        _vm.callEvent(a, "EndState");
        setBase(a, nullptr);
        const std::vector<VmObject *> others = _actors;
        for (VmObject *o : others) {
            if (o == a || o->deleted)
                continue;
            if (const Value *b = prop(o, "Base"); b && b->asObject() == a)
                setBase(o, nullptr);
            if (Value *w = prop(o, "Owner"); w && w->asObject() == a)
                w->o = nullptr;
        }
        const std::vector<VmObject *> touched = _touching[a];
        for (VmObject *o : touched) {
            auto &theirs = _touching[o];
            theirs.erase(std::remove(theirs.begin(), theirs.end(), a), theirs.end());
            _vm.callEvent(a, "UnTouch", {Value::objectValue(o)});
            _vm.callEvent(o, "UnTouch", {Value::objectValue(a)});
        }
        _touching.erase(a);
        if (Value *t = prop(a, "Touching"))
            for (size_t k = 0; k < 4; ++k)
                t[k] = Value::objectValue(nullptr);
        if (const Value *w = prop(a, "Owner"); w && w->asObject() && !w->o->deleted)
            _vm.callEvent(w->o, "LostChild", {Value::objectValue(a)});
        _actors.erase(std::remove(_actors.begin(), _actors.end(), a), _actors.end());
        _ghosts.push_back(a);
    } else {
        _ghosts.erase(std::remove(_ghosts.begin(), _ghosts.end(), a), _ghosts.end());
        _actors.push_back(a);
    }
    if (Value *v = prop(a, "bIsGhost"))
        *v = Value::boolValue(ghost);
    _collidersTick = -1;
    // Back, as SpawnActor takes it again: one placed as a ghost starts as
    // if spawned; one that began play already gets only its zone
    // (ZoneChange if a ZoneInfo's) and base again.
    if (!ghost) {
        if (const Value *begun = prop(a, "bHasBegunPlay"); begun && !begun->asBool())
            startAsSpawned(a);
        else
            startZone(a);
        if (!a->deleted)
            startBase(a);
    }
}

void ScriptLevel::tickActor(VmObject *a, float seconds) {
    if (!a || a->deleted)
        return;
    animate(a, seconds);
    if (!a->deleted)
        _vm.tick(a, seconds);
}

namespace {

std::string iniSettingIn(const std::filesystem::path &file, std::string_view section, std::string_view key) {
    std::ifstream in(file, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::string wantSection = "[" + lowerText(std::string(section)) + "]";
    const std::string wantKey = lowerText(std::string(key)) + "=";
    bool inSection = false;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (!line.empty() && line.front() == '[') {
            inSection = lowerText(line) == wantSection;
            continue;
        }
        if (inSection && lowerText(line).rfind(wantKey, 0) == 0)
            return line.substr(wantKey.size());
    }
    return "";
}

} // namespace

std::string iniSetting(const PackageSet &packages, std::string_view section, std::string_view key) {
    return iniSettingIn(packages.iniFile(), section, key);
}

std::string iniSetting(const PackageSet &packages, std::string_view file, std::string_view section,
                       std::string_view key) {
    return iniSettingIn(packages.iniFile().parent_path() / std::string(file), section, key);
}

std::string defaultGameType(const PackageSet &packages) {
    const std::string game = iniSetting(packages, "Engine.Engine", "DefaultGame");
    return game.empty() ? "Engine.GameInfo" : game;
}

Error runLevelStart(const PackageSet &packages, const Package &map, const std::string &defaultGame, float seconds,
                    LevelStart &out) {
    out = {};
    const auto t0 = std::chrono::steady_clock::now();
    Vm vm(packages);
    ScriptLevel level(vm, map);
    if (Error err = level.load())
        return err;
    struct Saved {
        VmObject *actor;
        int drawType;
        int hidden;
        int lightType;
        int lightBrightness;
    };
    auto byteOf = [&](VmObject *a, const char *name) {
        const Value *v = a->find(vm, name);
        return v ? v->asInt() : -1;
    };
    std::vector<std::pair<int, Saved>> before;
    for (VmObject *a : level.actors())
        if (a->package == &map)
            before.push_back({a->exportIndex,
                              {a, byteOf(a, "DrawType"), byteOf(a, "bHidden"), byteOf(a, "LightType"),
                               byteOf(a, "LightBrightness")}});
    std::string loginError;
    VmObject *p = nullptr;
    if (level.savedByGame()) { // a saved game of the original's
        p = level.resumeSavedGame(iniSetting(packages, "URL", "Name"), loginError);
    } else {
        level.beginPlay(defaultGame);
        p = level.login(iniSetting(packages, "URL", "Class"), iniSetting(packages, "URL", "Name"), loginError);
    }
    if (p)
        out.player = p->cls->name;
    out.loginError = loginError;
    for (int i = 0; i < int(seconds * 30); ++i)
        level.tick(1.0f / 30);
    for (const auto &[index, was] : before) {
        VmObject *a = was.actor;
        ScriptedActor c;
        bool any = false;
        if (a->deleted) {
            c.removed = any = true;
            ++out.removed;
        } else {
            if (byteOf(a, "bHidden") != was.hidden) {
                c.hidden = byteOf(a, "bHidden");
                any = true;
                if (c.hidden == 1)
                    ++out.hidden;
                else
                    ++out.shown;
            }
            if (byteOf(a, "DrawType") != was.drawType) {
                c.drawType = byteOf(a, "DrawType");
                any = true;
                ++out.drawTypes;
            }
            const int type = byteOf(a, "LightType");
            const int brightness = byteOf(a, "LightBrightness");
            if (type != was.lightType || brightness != was.lightBrightness) {
                c.lightType = type;
                c.lightBrightness = brightness;
                any = true;
                ++out.lights;
            }
        }
        if (any)
            out.changed[index] = c;
    }
    out.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    out.errors = vm.stats.errors.size();
    out.missingNatives = vm.stats.missingNatives.size();
    out.accessedNone = vm.stats.accessedNone;
    return Error::success();
}

// --- Engine natives -------------------------------------------------------------------------

namespace {

using Vec = std::array<float, 3>;

Vec location(Vm &vm, VmObject *a) {
    Value *v = a ? a->find(vm, "Location") : nullptr;
    return v ? Vm::vectorOf(*v) : Vec{0, 0, 0};
}

float distance(const Vec &a, const Vec &b) {
    const float dx = a[0] - b[0];
    const float dy = a[1] - b[1];
    const float dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// The level's live actors of a class (the class argument a class value).
std::vector<Value> actorsOf(Vm &vm, const Value &classArg) {
    std::vector<Value> out;
    if (!vm.level)
        return out;
    VmObject *c = classArg.asObject();
    const VmClass *cls = c && c->asClass ? c->asClass : nullptr;
    for (VmObject *a : vm.level->actors())
        if (!a->deleted && (!cls || (a->cls && a->cls->isA(cls))))
            out.push_back(Value::objectValue(a));
    return out;
}

} // namespace

void registerEngineNatives(Vm &vm) {
    if (vm.hasNative("Actor.Spawn"))
        return;
    registerMovementNatives(vm);
    registerSplineNatives(vm);
    registerAnimationNatives(vm);
    registerAttachNatives(vm);
    registerPathNatives(vm);
    registerDialogNatives(vm);
    registerDocumentNatives(vm);
    registerCanvasNatives(vm);
    registerConsoleNatives(vm);
    registerSaveNatives(vm);
    registerSoundNatives(vm);
    registerEffectNatives(vm);
    auto reg = [&](const char *name, NativeFn fn) { vm.registerNative(name, std::move(fn)); };
    auto B = [](bool v) { return Value::boolValue(v); };

    reg("Actor.Spawn", [](NativeCall &c) {
        VmObject *cls = c.o(0);
        if (!c.vm.level || !cls || !cls->asClass) {
            c.result = Value::objectValue(nullptr);
            return;
        }
        const bool hasLoc = c.given.size() > 3 && c.given[3];
        const bool hasRot = c.given.size() > 4 && c.given[4];
        // Where it's spawned: given, or the spawner's place.
        Value here = c.self && c.self->find(c.vm, "Location") ? *c.self->find(c.vm, "Location") : Value();
        Value turn = c.self && c.self->find(c.vm, "Rotation") ? *c.self->find(c.vm, "Rotation") : Value();
        VmObject *owner = c.given.size() > 1 && c.given[1] ? c.o(1) : c.self;
        c.result =
            Value::objectValue(c.vm.level->spawn(cls->asClass, owner, c.given.size() > 2 && c.given[2] ? c.i(2) : 0,
                                                 hasLoc ? &c.args[3] : &here, hasRot ? &c.args[4] : &turn));
    });
    reg("Actor.Destroy", [=](NativeCall &c) {
        VmObject *a = c.self;
        Value *noDelete = a ? a->find(c.vm, "bNoDelete") : nullptr;
        Value *isStatic = a ? a->find(c.vm, "bStatic") : nullptr;
        if (!a || !c.vm.level || (noDelete && noDelete->asBool()) || (isStatic && isStatic->asBool())) {
            c.result = B(false);
            return;
        }
        c.vm.level->destroy(a);
        c.result = B(true);
    });
    reg("Actor.SetTimer", [](NativeCall &c) {
        if (!c.self)
            return;
        c.self->timerRate = std::max(0.0f, c.f(0));
        c.self->timerCount = 0;
        c.self->timerLoop = c.b(1);
    });
    reg("Actor.Sleep", [](NativeCall &c) { c.latentSeconds = std::max(0.0f, c.f(0)); });
    // Latent functions that wait for what isn't simulated yet: done at once.
    for (const char *done : {"Actor.FinishInterpolation"})
        reg(done, [](NativeCall &c) { c.latentSeconds = 0; });
    reg("Actor.SetRotation", [=](NativeCall &c) {
        if (Value *v = c.self ? c.self->find(c.vm, "Rotation") : nullptr)
            c.vm.assign(*v, c.args[0]);
        c.result = B(true);
    });
    reg("Actor.SetCollision", [](NativeCall &c) {
        if (!c.self)
            return;
        const char *names[] = {"bCollideActors", "bBlockActors", "bBlockPlayers"};
        for (size_t k = 0; k < 3; ++k)
            if (c.given.size() > k && c.given[k])
                if (Value *v = c.self->find(c.vm, names[k]))
                    v->i = c.b(k);
        if (c.vm.level)
            c.vm.level->collisionChanged();
    });
    reg("Actor.SetCollisionSize", [=](NativeCall &c) {
        if (c.self) {
            if (Value *v = c.self->find(c.vm, "CollisionRadius"))
                v->f = c.f(0);
            if (Value *v = c.self->find(c.vm, "CollisionHeight"))
                v->f = c.f(1);
        }
        c.result = B(true);
    });
    reg("Actor.SetOwner", [](NativeCall &c) {
        if (Value *v = c.self ? c.self->find(c.vm, "Owner") : nullptr)
            v->o = c.o(0);
    });
    reg("Actor.SetBase", [](NativeCall &c) {
        if (Value *v = c.self ? c.self->find(c.vm, "Base") : nullptr)
            v->o = c.o(0);
    });
    // StopWaiting(): a Sleep its state code is in ends now (it goes on at
    // the next tick), as UE1's does.
    reg("Pawn.StopWaiting", [](NativeCall &c) {
        if (c.self && c.self->latent && c.self->sleeping > 0)
            c.self->sleeping = 0;
    });
    // MakeNoise(float Loudness): the AI hears it (ScriptLevel::makeNoise).
    reg("Actor.MakeNoise", [](NativeCall &c) {
        if (c.vm.level && c.self)
            c.vm.level->makeNoise(c.self, c.f(0));
    });
    // Not heard: a demo's sounds. Sounds themselves are vm_sound.cpp's.
    reg("Actor.DemoPlaySound", [](NativeCall &) {});
    reg("Actor.GetRegion", [](NativeCall &c) { // GetRegion(Vector Point, out PointRegion Region)
        if (c.vm.level)
            c.args[1] = c.vm.level->regionAt(Vm::vectorOf(c.args[0]));
    });
    reg("Actor.GetMapName",
        [](NativeCall &c) { c.result = Value::stringValue(c.vm.level ? c.vm.level->map().name() : ""); });
    reg("Actor.GetURLMap",
        [](NativeCall &c) { c.result = Value::stringValue(c.vm.level ? c.vm.level->map().name() : ""); });
    reg("Actor.PlayerCanSeeMe", [=](NativeCall &c) { c.result = B(false); });
    reg("Actor.Error", [](NativeCall &c) {
        c.vm.reportError("Error() from " + (c.self && c.self->cls ? c.self->cls->name : std::string("?")) + ": " +
                         c.s(0));
        if (c.vm.level && c.self)
            c.vm.level->destroy(c.self);
    });

    // Iterators: their results, put in the out parameter in turn.
    reg("Actor.AllActors", [](NativeCall &c) {
        const bool byTag = c.given.size() > 2 && c.given[2] && c.i(2) != 0;
        for (Value &v : actorsOf(c.vm, c.args[0])) {
            Value *tag = byTag ? v.o->find(c.vm, "Tag") : nullptr;
            if (!byTag || (tag && tag->i == c.i(2)))
                c.items.push_back(v);
        }
    });
    // ListActors(class<Actor> BaseClass, out Actor Actor, out ActorListEntry
    // ListEntry, Actor inActor): the actors in inActor's ActorList (its
    // scripts fill it with what it's seen: ScanForActors) of the class, each
    // with its entry; empty entries left out.
    reg("Actor.ListActors", [](NativeCall &c) {
        VmObject *of = c.o(3);
        const VmObject *cls = c.o(0);
        const VmProperty *p = of && of->cls ? of->cls->findProperty(c.vm.name("ActorList")) : nullptr;
        if (!p || !p->structType)
            return;
        const VmProperty *item = p->structType->findProperty(c.vm.name("Item"));
        if (!item)
            return;
        for (int k = 0; k < p->arrayDim && size_t(p->slot + k) < of->values.size(); ++k) {
            const Value &entry = of->values[size_t(p->slot + k)];
            if (size_t(item->slot) >= entry.fields.size())
                continue;
            VmObject *a = entry.fields[size_t(item->slot)].asObject();
            if (!a || a->deleted || (cls && cls->asClass && !(a->cls && a->cls->isA(cls->asClass))))
                continue;
            c.items.push_back(Value::objectValue(a));
            c.items2.push_back(entry);
        }
    });
    // GhostActors(class<Actor> BaseClass, out Actor Actor, optional name
    // MatchTag, optional bool bDontUnghostAutomatically): the ghosts of the
    // class with that tag, each an actor again unless asked not to.
    reg("Actor.GhostActors", [](NativeCall &c) {
        if (!c.vm.level)
            return;
        const VmObject *cls = c.o(0);
        const bool byTag = c.given.size() > 2 && c.given[2] && c.i(2) != 0;
        // Reviving all of a class's ghosts at once the engine refuses.
        if (!byTag && !c.b(3)) {
            c.vm.reportError("GhostActors: iterating over ghost actors without specifying a Tag");
            return;
        }
        const std::vector<VmObject *> ghosts = c.vm.level->ghosts();
        for (VmObject *a : ghosts) {
            if (a->deleted || (cls && cls->asClass && !(a->cls && a->cls->isA(cls->asClass))))
                continue;
            if (const Value *tag = a->find(c.vm, "Tag"); byTag && (!tag || tag->i != c.i(2)))
                continue;
            if (!c.b(3))
                c.vm.level->makeGhost(a, false);
            c.items.push_back(Value::objectValue(a));
        }
    });
    // MakeGhost(bool bGhost): out of the level, or back.
    reg("Actor.MakeGhost", [](NativeCall &c) {
        if (c.vm.level)
            c.vm.level->makeGhost(c.self, c.b(0));
    });
    // ScriptTick(float DeltaTime): the actor ticked by its owner, the HUD
    // its portrait, so it moves while the game's paused; when it isn't,
    // the level ticks it already, and once a tick is enough.
    reg("Actor.ScriptTick", [](NativeCall &c) {
        if (c.vm.level && c.vm.level->paused())
            c.vm.level->tickActor(c.self, c.f(0));
    });
    // GetRendererFlags(): what the renderer does itself. Bit 2: it draws
    // characters' own shadows (the Maps page does, LevelView::drawShadow),
    // so The Fallen's blob shadows (CTV_Shadow) hide.
    reg("Actor.GetRendererFlags", [](NativeCall &c) { c.result = Value::intValue(2); });
    // UpdateMultiSkins(): its MultiSkins changed. The page reads them each
    // frame, so there's nothing to bring up to date.
    reg("Actor.UpdateMultiSkins", [](NativeCall &) {});
    reg("Actor.ChildActors", [](NativeCall &c) {
        for (Value &v : actorsOf(c.vm, c.args[0]))
            if (Value *owner = v.o->find(c.vm, "Owner"); owner && owner->o == c.self)
                c.items.push_back(v);
    });
    reg("Actor.BasedActors", [](NativeCall &c) {
        for (Value &v : actorsOf(c.vm, c.args[0]))
            if (Value *base = v.o->find(c.vm, "Base"); base && base->o == c.self)
                c.items.push_back(v);
    });
    // Within a radius (no line of sight yet, so the visible ones too).
    for (const char *radius : {"Actor.RadiusActors", "Actor.VisibleActors", "Actor.VisibleCollidingActors"})
        reg(radius, [](NativeCall &c) {
            const Vec at = c.given.size() > 3 && c.given[3] ? Vm::vectorOf(c.args[3]) : location(c.vm, c.self);
            for (Value &v : actorsOf(c.vm, c.args[0]))
                if (distance(location(c.vm, v.o), at) <= c.f(2))
                    c.items.push_back(v);
        });
    // RadiusPawns(class<Actor> BaseClass, out Actor Actor, float Radius,
    // optional Vector Loc): the pawns among those (a pawn's head looks for
    // someone to look at with it).
    reg("Actor.RadiusPawns", [](NativeCall &c) {
        const Vec at = c.given.size() > 3 && c.given[3] ? Vm::vectorOf(c.args[3]) : location(c.vm, c.self);
        for (Value &v : actorsOf(c.vm, c.args[0]))
            if (c.vm.isA(v.o, "Pawn") && distance(location(c.vm, v.o), at) <= c.f(2))
                c.items.push_back(v);
    });
    reg("Actor.TouchingActors", [](NativeCall &) {}); // no collision yet
    reg("Actor.AllPawns", [](NativeCall &c) {
        for (Value &v : actorsOf(c.vm, c.args[0]))
            if (c.vm.isA(v.o, "Pawn"))
                c.items.push_back(v);
    });

    // Pawns keep a list in the LevelInfo.
    reg("Pawn.AddPawn", [](NativeCall &c) {
        if (!c.vm.level || !c.self)
            return;
        Value *list = c.vm.level->levelInfo()->find(c.vm, "PawnList");
        Value *next = c.self->find(c.vm, "nextPawn");
        if (list && next) {
            next->o = list->asObject();
            list->o = c.self;
        }
    });
    reg("Pawn.RemovePawn", [](NativeCall &c) {
        if (!c.vm.level || !c.self)
            return;
        Value *link = c.vm.level->levelInfo()->find(c.vm, "PawnList");
        for (int guard = 0; link && link->asObject() && guard < 100000; ++guard) {
            if (link->o == c.self) {
                Value *next = c.self->find(c.vm, "nextPawn");
                link->o = next ? next->asObject() : nullptr;
                return;
            }
            link = link->o->find(c.vm, "nextPawn");
        }
    });
    reg("LevelInfo.GetLocalURL",
        [](NativeCall &c) { c.result = Value::stringValue(c.vm.level ? c.vm.level->map().name() : ""); });
    reg("LevelInfo.GetAddressURL", [](NativeCall &c) { c.result = Value::stringValue("127.0.0.1"); });
}

} // namespace reluva::unreal
