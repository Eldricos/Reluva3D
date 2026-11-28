// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/map_screen.h"

#include "engines/stawayteam/ui/layout.h"

#include <cctype>
#include <cstring>

namespace reluva::stawayteam {

namespace {

std::string lower(std::string s) {
    for (char &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// vector<bool>'s items aren't references, so StateIo's lists can't take it.
void bits(StateIo &s, std::vector<bool> &v) {
    std::vector<uint8_t> b(v.begin(), v.end());
    s.io(b);
    v.assign(b.begin(), b.end());
}

void colour(StateIo &s, std::array<uint8_t, 3> &rgb) {
    for (uint8_t &c : rgb)
        s.io(c);
}

void stateOf(StateIo &s, Objective &o) {
    s.io(o.name);
    s.io(o.className);
    s.io(o.status);
    s.io(o.visible);
    s.io(o.required);
    s.io(o.points);
    s.io(o.failAction);
    s.io(o.successAction);
    s.io(o.script);
    s.io(o.line);
    s.io(o.needed);
    s.io(o.count);
    s.io(o.who);
    s.io(o.officer);
    s.io(o.item);
}

void stateOf(StateIo &s, MapPart &p) {
    s.io(p.className);
    s.io(p.name);
    s.io(p.model);
    s.io(p.sequence);
    s.io(p.x);
    s.io(p.y);
    s.io(p.renderingHeight);
    s.io(p.visible);
    s.io(p.active);
    s.io(p.activities);
    s.io(p.collideable);
    s.io(p.triesToCollide);
    s.io(p.fullHeight);
    s.io(p.settled);
    s.io(p.inventory);
    s.io(p.stuns);
    s.io(p.destroyedAction);
    s.io(p.category);
    s.io(p.teamMemberInfo);
    s.io(p.deathType);
    s.io(p.polygon);
    s.io(p.renderType);
    s.io(p.texture);
    s.io(p.warpEffect);
    s.io(p.onGround);
    s.io(p.ySort);
    s.io(p.facing);
    s.io(p.setsFacing);
    s.io(p.angle);
    s.io(p.overlay);
}

} // namespace

void stateOf(StateIo &s, MissionProgress &progress) {
    s.list(progress.objectives, [&](Objective &o) { stateOf(s, o); });
    s.io(progress.won);
    s.io(progress.over);
    s.io(progress.completed);
    s.io(progress.nextMission);
    s.io(progress.overText);
    s.io(progress.time);
    s.io(progress.discover);
    s.io(progress.discovered);
}

void stateOf(StateIo &s, TeamMember &m) {
    for (std::string *each : {&m.id, &m.firstName, &m.lastName, &m.rank, &m.field, &m.portrait})
        s.io(*each);
    s.io(m.available);
    s.io(m.characterScript);
    s.io(m.characterText);
    s.list(m.attributes, [&](TeamMember::Attribute &a) {
        s.io(a.name);
        s.io(a.value);
    });
    s.list(m.inventory, [&](TeamMember::Item &i) {
        s.io(i.name);
        s.io(i.count);
    });
    for (std::string *each : {&m.race, &m.gender, &m.headIcon, &m.bodyModel, &m.headModel})
        s.io(*each);
}

void MapScreen::stateRoots(const std::string &prefix, StateIo &s) {
    s.addRoot(prefix + "map", _map.object.get());
    for (const auto &[model, object] : _modelObjects)
        if (object)
            s.addRoot(prefix + "model/" + model, &object->object);
    for (const auto &[type, object] : _deathBehaviors)
        s.addRoot(prefix + "death/" + type, object.get());
    for (const auto &[name, item] : _items) {
        s.addRoot(prefix + "item/" + name + "/process", item.process.get());
        s.addRoot(prefix + "item/" + name + "/discharge", item.discharge.get());
        s.addRoot(prefix + "item/" + name + "/info", item.infoRenderer.get());
    }
}

const TextObject *MapScreen::stateRoot(const std::string &name) {
    if (name == "map")
        return _map.object.get();
    if (name.starts_with("model/")) {
        const BinaryObject *object = modelObject(name.substr(6));
        return object ? &object->object : nullptr;
    }
    if (name.starts_with("death/"))
        return deathBehaviors(name.substr(6));
    if (name.starts_with("item/")) {
        const size_t slash = name.rfind('/');
        const auto it = _items.find(name.substr(5, slash - 5));
        if (slash <= 5 || it == _items.end())
            return nullptr;
        const std::string which = name.substr(slash + 1);
        return which == "process"     ? it->second.process.get()
               : which == "discharge" ? it->second.discharge.get()
               : which == "info"      ? it->second.infoRenderer.get()
                                      : nullptr;
    }
    return nullptr;
}

void MapScreen::state(StateIo &s) {
    // An item by its name among the map's.
    const auto item = [&](const ItemDefinition *&p) {
        std::string name;
        for (const auto &[key, each] : _items)
            if (&each == p)
                name = key;
        s.io(name);
        if (s.reading()) {
            const auto it = _items.find(name);
            p = it == _items.end() ? nullptr : &it->second;
        }
    };

    s.list(_map.parts, [&](MapPart &p) { stateOf(s, p); });
    s.list(_partStates, [&](PartState &p) {
        s.io(p.active);
        s.io(p.visible);
        s.io(p.deleted);
        s.io(p.collideable);
    });
    s.io(_standing);
    s.io(_cameraX);
    s.io(_cameraY);
    s.io(_scrollVX);
    s.io(_scrollVY);
    s.io(_scrollTime);
    s.io(_zoom);
    s.io(_waterTime);

    s.list(_team, [&](TeamMember &m) { stateOf(s, m); });
    s.list(_officers, [&](Officer &o) {
        s.io(o.member);
        s.io(o.spawn);
        s.io(o.standIn);
        s.io(o.addedAIs);
        s.io(o.x);
        s.io(o.y);
        s.io(o.settled);
        s.io(o.direction);
        s.io(o.path);
        s.io(o.travelled);
        s.io(o.move);
        s.io(o.chosen);
        s.io(o.health);
        s.io(o.maxHealth);
        item(o.weapon);
        s.io(o.attacking);
        for (float *f : {&o.reload, &o.shooting, &o.dead, &o.stunned, &o.stunTotal, &o.stunLength, &o.enhanced})
            s.io(*f);
        s.io(o.discharge);
        s.io(o.scanText);
        s.io(o.scanTyped);
        s.list(o.items, [&](Officer::Held &h) {
            s.io(h.name);
            s.io(h.count);
            s.io(h.stun);
            s.io(h.energy);
            s.io(h.on);
            s.io(h.overload);
            s.io(h.beep);
        });
        s.io(o.item);
        s.io(o.task);
        s.io(o.taskX);
        s.io(o.taskY);
        s.io(o.taskOfficer);
        s.io(o.taskPart);
        s.io(o.taskProcess);
        s.io(o.taskStep);
        s.io(o.shown);
        s.io(o.present);
        s.io(o.opacity);
        s.io(o.cloaked);
        s.io(o.held);
    });
    s.io(_selected);

    s.list(_scripts, [&](CameraScript &c) {
        s.io(c.where);
        s.io(c.timeToPan);
        s.io(c.timeToWait);
        s.io(c.panToPosition);
        s.io(c.action);
        s.io(c.holdsActors);
    });
    s.io(_scriptBegun);
    s.io(_scriptWaiting);
    for (float *f : {&_panStartX, &_panStartY, &_panDirX, &_panDirY, &_panDistance, &_panTime, &_waitLeft})
        s.io(*f);

    s.list(_enemies, [&](Enemy &e) {
        s.io(e.part);
        s.io(e.ai);
        s.io(e.x);
        s.io(e.y);
        s.io(e.settled);
        s.io(e.angle);
        s.io(e.state);
        s.io(e.target);
        s.io(e.foe);
        s.io(e.stateTime);
        s.io(e.unseen);
        s.io(e.walking);
        s.io(e.running);
        s.io(e.travelled);
        s.io(e.heardX);
        s.io(e.heardY);
        s.io(e.validTargets);
        s.io(e.bodiesSeen);
        s.io(e.runAway);
        s.io(e.runRetry);
        s.io(e.onceSequence);
        s.io(e.onceTime);
        s.io(e.beforeInactive);
        s.io(e.doTimer);
        s.io(e.recharge);
        s.io(e.patrolAI);
        s.io(e.seenX);
        s.io(e.seenY);
        s.io(e.retreated);
        s.io(e.route);
        s.io(e.looked);
        s.io(e.health);
        s.io(e.maxHealth);
        item(e.weapon);
        s.io(e.stuns);
        for (float *f : {&e.reload, &e.shooting, &e.dead, &e.stunned, &e.stunTotal, &e.stunLength, &e.held})
            s.io(*f);
        s.io(e.stunKept);
        s.io(e.stunLaid);
        s.io(e.decoy);
        s.io(e.reaction);
        s.io(e.energy);
        s.io(e.shield);
        s.map(e.adapted, [&](std::string &, float &level) { s.io(level); });
        s.io(e.lastDamage);
        s.map(e.progress, [&](const TextObject *&ai, Enemy::Progress &p) {
            s.io(ai);
            s.io(p.time);
            s.io(p.index);
            s.io(p.points);
            s.io(p.step);
            s.io(p.from);
            s.io(p.to);
            s.io(p.begun);
        });
        s.map(e.transitions, [&](const TextObject *&t, std::pair<bool, int> &done) {
            s.io(t);
            s.io(done);
        });
    });
    s.list(_triggers, [&](Trigger &t) {
        s.io(t.part);
        s.io(t.ai);
        s.io(t.done);
        s.io(t.occupied);
        s.io(t.occupant);
        s.io(t.repeatIn);
        bits(s, t.touchingEnemies);
        s.io(t.anything);
        s.io(t.pending);
        s.io(t.countdown);
        s.io(t.instigator);
        s.io(t.thrownIn);
        s.io(t.inside);
        s.io(t.timer);
    });
    s.map(_series, [&](const TextObject *&series, size_t &next) {
        s.io(series);
        s.io(next);
    });
    s.io(_random);
    s.list(_delayed, [&](Delayed &d) {
        s.io(d.left);
        s.io(d.action);
        s.io(d.instigator);
        s.io(d.trigger);
    });
    s.io(_teamShown);
    s.io(_warpLeft);
    s.io(_warpTime);
    s.io(_warpEffect);
    s.io(_warpFadeIn);
    s.io(_dialogText);
    s.io(_dialogSpeaker);
    s.io(_dialogLeft);

    s.io(_thrownIds);
    s.io(_beamIds);
    s.list(_flashes, [&](Flash &f) {
        s.io(f.model);
        s.io(f.x);
        s.io(f.y);
        s.io(f.time);
        s.io(f.sequence);
        s.io(f.follow);
    });
    s.list(_beams, [&](Beam &b) {
        for (float *f :
             {&b.x0, &b.y0, &b.x1, &b.y1, &b.sx, &b.sy, &b.ux, &b.uy, &b.lift, &b.tail, &b.head, &b.reach, &b.left})
            s.io(*f);
        s.io(b.id);
        s.io(b.shotFlash);
        s.io(b.hitFlash);
        s.io(b.hit);
        s.io(b.time);
        colour(s, b.outside);
        colour(s, b.middle);
    });
    s.list(_bolts, [&](Bolt &b) {
        item(b.weapon);
        s.io(b.self);
        s.io(b.attacker);
        s.io(b.stun);
        for (float *f : {&b.x, &b.y, &b.ux, &b.uy, &b.aimX, &b.aimY, &b.head, &b.range})
            s.io(*f);
        s.io(b.crossed);
        s.io(b.beam);
    });
    s.list(_thrown, [&](Thrown &t) {
        item(t.item);
        s.io(t.model);
        for (float *f : {&t.x, &t.y, &t.toX, &t.toY, &t.left})
            s.io(*f);
        s.io(t.thrower);
        for (float *f : {&t.z, &t.vx, &t.vy, &t.vz})
            s.io(*f);
        s.io(t.resting);
        s.io(t.id);
        s.io(t.gravity);
    });
    s.list(_mines, [&](Mine &m) {
        item(m.item);
        s.io(m.model);
        s.io(m.x);
        s.io(m.y);
        s.io(m.placed);
        s.io(m.setOff);
        s.io(m.fuse);
        s.io(m.placedBy);
    });
    s.list(_deployed, [&](Deployed &d) {
        s.io(d.part);
        s.io(d.sequence);
        s.io(d.time);
        s.io(d.frames);
        s.io(d.once);
        s.io(d.health);
        s.io(d.painSound);
        s.io(d.then);
        s.io(d.done);
        s.io(d.doneBy);
    });
    s.map(_doors, [&](size_t &part, Door &d) {
        s.io(part);
        s.io(d.locked);
        s.io(d.open);
        s.io(d.byPlayers);
        s.io(d.byEnemies);
    });
    s.map(_stoppedAt, [&](size_t &part, int &frame) {
        s.io(part);
        s.io(frame);
    });
    s.map(_gridPlaces, [&](int &who, GridPlace &g) {
        s.io(who);
        for (int &c : g.cells)
            s.io(c);
        s.io(g.since);
    });
    s.io(_gridCount);
    s.list(_warping, [&](Warp &w) {
        s.io(w.part);
        s.io(w.left);
        s.io(w.time);
        s.io(w.overlay);
        s.io(w.overlayY);
        s.io(w.rotations);
        s.io(w.effect);
        s.io(w.fadeIn);
    });
    s.list(_noises, [&](Noise &n) {
        for (float *f : {&n.x, &n.y, &n.radius, &n.age, &n.left})
            s.io(*f);
    });
    s.io(_itemTitle);
    s.io(_active);
    s.io(_listening);
    s.io(_paused);
    s.io(_vision);
    s.io(_visionEnemy);
    s.io(_melded);
    s.io(_melder);
    s.io(_meldChosen);
    s.io(_meldCategory);

    // The particle systems, made again from their AIs and then set going
    // as they were.
    size_t emitters = _emitters.size();
    s.io(emitters);
    if (s.reading())
        _emitters.clear();
    for (size_t i = 0; i < emitters && !s.failed(); ++i) {
        size_t part = s.reading() ? 0 : _emitters[i].part;
        const TextObject *ai = s.reading() ? nullptr : _emitters[i].ai;
        s.io(part);
        s.io(ai);
        if (s.reading()) {
            if (!ai || part >= _map.parts.size())
                break;
            addEmitter(part, ai);
        }
        _emitters.back().system.state(s);
    }
    if (!s.reading())
        return;

    // What's kept of all that, made again from it.
    _enemyOfPart.clear();
    for (size_t i = 0; i < _enemies.size(); ++i)
        _enemyOfPart[_enemies[i].part] = i;
    _byName.clear();
    for (size_t i = 0; i < _map.parts.size(); ++i)
        if (!_map.parts[i].name.empty())
            _byName[lower(_map.parts[i].name)].push_back(i);
    _hud = std::make_unique<GameHud>(_data, _rules, _team, _map.minimap, _map.width, _map.height);
    _dialogIcon =
        _dialogSpeaker.empty() ? image::Image{} : cachePicture(_data, "Character/Character Icons/" + _dialogSpeaker);
    _dialogVoice = audio::kNoSound;
    _drawn.clear();
    _hovered = -1;
    _left = _right = _up = _down = _zoomingIn = _zoomingOut = false;
    _pressed = _dragging = false;
    _relocations.clear();
    _switchTo = -1;
    _escape.reset();
    _objectivesWindow.reset();
    _endGame.reset();
}

image::Image MapScreen::viewPicture() {
    // Drawn again without the windows that might be over it, the time
    // standing still.
    auto escape = std::move(_escape);
    auto objectives = std::move(_objectivesWindow);
    Surface screen;
    screen.create(_viewX + _viewW, _viewY + _viewH, PixelFormat::kRGBA32);
    draw(screen, _last);
    _escape = std::move(escape);
    _objectivesWindow = std::move(objectives);
    image::Image out;
    out.w = _viewW;
    out.h = _viewH;
    out.rgba.resize(size_t(_viewW) * size_t(_viewH) * 4);
    for (int y = 0; y < _viewH; ++y)
        std::memcpy(out.pixel(0, y), screen.row(_viewY + y) + _viewX * 4, size_t(_viewW) * 4);
    return out;
}

} // namespace reluva::stawayteam
