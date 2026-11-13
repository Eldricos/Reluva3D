// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Scenes page's mouse: the game's pick of the object under it
// (0x435eb0), the cursor it shows over it (0x415650, the 16 pictures of
// 0x413f00), and the hotspots that keys press (0x436110). See
// docs/engines/awe/games/motoe/formats/ac2.md.

#include "engines/awe/pages/viewers.h"

#include "common/fs.h"
#include "engines/awe/game/picking.h"
#include "engines/awe/render/sprite.h"
#include "engines/awe/formats/tables.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace reluva::awe {

namespace {

// The cursor pictures, by number (0x413f78).
constexpr int kCursorCount = 16;
constexpr const char *kCursors[kCursorCount] = {
    "interface/cursor01.tga",      "interface/cursor02.tga",     "interface/cursorlook.tga",
    "interface/cursortake.tga",    "interface/cursoropen.tga",   "interface/cursorwalk.tga",
    "interface/cursortalk.tga",    "interface/cursorlisten.tga", "interface/cursorstairs.tga",
    "interface/cursorkeyhole.tga", "interface/cursorback.tga",   "interface/cursoroperate.tga",
    "interface/cursorshadow.tga",  "interface/cursorup.tga",     "interface/cursordown.tga",
    "interface/cursorempty.tga"};
constexpr int kCursorOperate = 11;
constexpr int kCursorEmpty = 15;

bool isCharacter(const Ac2Record &o) {
    return o.tag == "NPCX" || o.tag == "PCXX";
}

// An outline (listD) makes a region the game tests the point against
// (CreatePolygonRgn, WINDING, 0x40ef10).
bool hasOutline(const Ac2Record &o) {
    const Ac2Value *outline = o.field("listD");
    return outline && outline->numbers.size() >= 3;
}

} // namespace

bool ScenePage::objectShown(const Ac2Record &o) const {
    // Loaded and shown (+0xb00, +0xb04): as its condition was when the
    // scene was entered or "update" last ran, or as "hide" or "show" left
    // it since. One not yet tested (a scene being read) by its condition.
    if (const auto it = _shownNow.find(&o); it != _shownNow.end())
        return it->second;
    return testCondition(conditionOf(o), _state);
}

bool ScenePage::clickable(const Ac2Record &o) const {
    // 0x40e570: a script, or a conditional one whose condition holds.
    if (!o.text("script").empty())
        return true;
    if (const Ac2Value *alternatives = o.field("listB"))
        for (const auto &[condition, run] : alternatives->pairs)
            if (testCondition(condition, _state))
                return true;
    return false;
}

int ScenePage::cursorOf(const Ac2Record &o) const {
    // 0x40e890: the first conditional cursor whose condition holds, else
    // its own (the classes' defaults where the file has none: hotspots 5,
    // items 3, characters 6).
    if (const Ac2Value *conditional = o.field("conditionalCursors"))
        for (const auto &[condition, cursor] : conditional->pairs)
            if (testCondition(condition, _state))
                return std::atoi(cursor.c_str());
    if (o.field("cursor"))
        return o.i("cursor");
    return o.tag == "SPOT" ? 5 : o.tag == "ITEM" ? 3 : isCharacter(o) ? 6 : 0;
}

bool ScenePage::objectRect(const Ac2Record &o, int &left, int &top, int &right, int &bottom) const {
    left = o.i("left"), top = o.i("top"), right = o.i("right"), bottom = o.i("bottom");
    if (right <= left || bottom <= top)
        return false; // IsRectEmpty
    if (!isCharacter(o))
        return true;
    // A model's rectangle follows it on the screen: where it was put
    // against where the file has it (0x40ea70 at load, 0x40eb60 as it
    // moves).
    const Ac2Record *camr = _scene.camera();
    if (!camr)
        return true;
    for (const Character &c : _characters) {
        if (c.record != &o)
            continue;
        const SceneCamera cam = SceneCamera::fromRecord(*camr);
        const float was[3] = {o.f("x"), o.f("y"), o.f("z")};
        const float now[3] = {c.x, c.y, c.z};
        float x0;
        float y0;
        float x1;
        float y1;
        if (cam.project(was, kScreenW, kScreenH, x0, y0) && cam.project(now, kScreenW, kScreenH, x1, y1)) {
            const int dx = int(std::lround(x1 - x0));
            const int dy = int(std::lround(y1 - y0));
            left += dx, right += dx, top += dy, bottom += dy;
        }
        break;
    }
    return true;
}

bool ScenePage::hitObject(const Ac2Record &o, int sx, int sy) const {
    // 0x40ef70: in its outline if it has one, else in its rectangle.
    if (hasOutline(o))
        return inPolygon(o.field("listD")->numbers, sx, sy);
    int l;
    int t;
    int r;
    int b;
    return objectRect(o, l, t, r, b) && sx >= l && sx < r && sy >= t && sy < b;
}

bool ScenePage::hitBones(const Ac2Record &o, int sx, int sy) const {
    // 0x428c80: the ray through the point (GrannyGetPickingRay) against
    // the boxes of the bones named Bip01 something, as they're posed now.
    const Ac2Record *camr = _scene.camera();
    if (!camr)
        return false;
    for (const Character &c : _characters) {
        if (c.record != &o || !c.model || !c.actor)
            continue;
        if (c.pickFrame != _frame) {
            // Posed once for all the points picked till the page moves on.
            c.pickBoxes.clear();
            auto add = [&](const Model &model, const Pose &pose) {
                for (const ModelMesh &mesh : model.meshes)
                    for (const ModelMesh::BoneBox &box : mesh.boxes)
                        if (size_t(box.bone) < model.bones.size() && size_t(box.bone) < pose.world.size() &&
                            fs::toLower(model.bones[size_t(box.bone)].name).find("bip01") != std::string::npos)
                            c.pickBoxes.push_back(
                                {gfx::multiply(c.placement, pose.world[size_t(box.bone)]), box.min, box.max});
            };
            Pose pose;
            c.actor->animator().pose(pose);
            add(c.model->model(), pose);
            for (const auto &[name, prop] : c.props) {
                Pose propPose;
                c.actor->animator().poseFor(prop->model(), propPose);
                add(prop->model(), propPose);
            }
            c.pickFrame = _frame;
        }
        gfx::Vec3 from;
        gfx::Vec3 dir;
        SceneCamera::fromRecord(*camr).ray(float(sx), float(sy), kScreenW, kScreenH, from, dir);
        for (const Character::PickBox &b : c.pickBoxes)
            if (rayHitsBox(b.m, b.lo, b.hi, from, dir))
                return true;
        return false;
    }
    return false;
}

const Ac2Record *ScenePage::pickAt(int sx, int sy) const {
    // 0x435eb0 as the mouse and clicks use it, over the objects shown.
    std::vector<const Ac2Record *> list;
    std::vector<PickCandidate> candidates;
    for (const Ac2Record *o : _scene.objects()) {
        if (!objectShown(*o))
            continue;
        PickCandidate c;
        c.depth = o->f("depth") != 0 ? o->f("depth") : o->f("z");
        c.character = isCharacter(*o);
        if (c.character)
            for (const Character &ch : _characters)
                if (ch.record == o && o->f("depth") == 0)
                    c.depth = ch.z;
        c.pickFirst = o->i("pickFirst") != 0;
        c.outline = hasOutline(*o);
        int l;
        int t;
        int r;
        int b;
        c.rect = objectRect(*o, l, t, r, b);
        c.clickable = clickable(*o);
        c.hit = hitObject(*o, sx, sy);
        c.boneHit = c.character && !c.rect && !c.outline && hitBones(*o, sx, sy);
        list.push_back(o);
        candidates.push_back(c);
    }
    const int i = pick(candidates, !itemInHand().empty());
    return i < 0 ? nullptr : list[size_t(i)];
}

const std::string &ScenePage::itemInHand() const {
    // The item the next click uses, taken from the inventory.
    return _state.held;
}

bool ScenePage::canUseOn(const std::string &item, const Ac2Record &o) {
    // 0x419e90: one of its conditional scripts asks for an item and holds
    // with this one in hand (0x40e7e0 in use mode), or, for a character,
    // uselist.txt has a row for the item and it whose condition holds
    // (0x41c4b0).
    const std::string name = fs::toLower(item);
    const std::string was = _state.held;
    _state.held = name;
    bool can = false;
    if (const Ac2Value *alternatives = o.field("listB"))
        for (const auto &[condition, run] : alternatives->pairs)
            if (fs::toLower(condition).find("use") != std::string::npos && testCondition(condition, _state)) {
                can = true;
                break;
            }
    _state.held = was;
    if (can || !isCharacter(o)) // NPCX or PCXX (0x419ec7)
        return can;
    auto unquoted = [](std::string s) {
        return s.size() >= 2 && s.front() == '"' && s.back() == '"' ? s.substr(1, s.size() - 2) : s;
    };
    const Table &t = itemTable("uselist.txt");
    const std::string model = fs::toLower(o.text("file"));
    for (size_t r = 0; r < t.rows.size(); r++)
        if (fs::toLower(unquoted(t.cell(r, 0))) == name &&
            model.find(fs::toLower(unquoted(t.cell(r, 1))) + "_model") != std::string::npos &&
            testCondition(unquoted(t.cell(r, 2)), _state))
            return true;
    return false;
}

bool ScenePage::interfaceShown() const {
    // 0x4143c0: the scene shows the interface (SHOW), and "interface off"
    // hasn't hidden it.
    for (const Ac2Record &r : _scene.records)
        if (r.tag == "SHOW")
            return r.i("value") != 0 && !_interfaceOff;
    return !_interfaceOff;
}

bool ScenePage::cursorShown() const {
    // 0x4140b0: none while the interface is off, a movie or the loading
    // screen shows; else where the interface shows, in the menus, and in
    // conversations; but not while the letterbox is up, or in a
    // conversation while the wheel has chosen and the mouse not moved.
    if (!_sceneShown || _movie || _load.screen || _interfaceOff)
        return false;
    if (!interfaceShown() && _location.rfind("menu.", 0) != 0 && !_dialogue->active())
        return false;
    return _letterbox.mode == 0 && !(_dialogue->active() && _dialogue->wheelChoosing());
}

bool ScenePage::pickAllowed(int sy) const {
    // 0x414400, 0x415fa0: not while the screen fades, a locked script
    // runs, a conversation goes on or the letterbox is up; not over the
    // interface bar (the top 60 pixels while it's down, else 5: 0x417580).
    if (!_sceneShown || _movie || _load.screen || _fade != _fadeTarget || _runner->locked() || _dialogue->active() ||
        _letterbox.mode != 0)
        return false;
    return sy >= (!_ui.barShown && _ui.barAlpha == 0 ? 5 : 60);
}

int ScenePage::cursorUnder(int sx, int sy, const Ac2Record **under) const {
    // 0x4154f5: the cursor of the object under the mouse if it can be
    // clicked, else 0 (the arrow).
    const Ac2Record *o = pickAllowed(sy) ? pickAt(sx, sy) : nullptr;
    if (under)
        *under = o;
    return o && clickable(*o) ? cursorOf(*o) : 0;
}

void ScenePage::drawCursor() {
    // 0x415650: the arrow at the mouse (cursor 1: the bright one), then
    // the cursor's picture over it, both by their top-left corners; the
    // picture kept 62 pixels in from the right and the bottom. With an
    // item in hand: the empty ring with the item's own cursor picture 26
    // pixels in, or the cog where it can be used.
    if (!cursorShown())
        return;
    const Ac2Record *under = nullptr;
    const int cursor = cursorUnder(_lastMouseX, _lastMouseY, &under);
    int x = _lastMouseX;
    int y = _lastMouseY;
    auto put = [&](const std::string &file, int px, int py) {
        SpriteTransform t;
        t.x = float(px), t.y = float(py);
        drawSprite(_ctx.screen, picture(file), t, true);
    };
    put(kCursors[cursor == 1 ? 1 : 0], x, y);
    if (x + 62 > kScreenW)
        x = kScreenW - 62;
    if (y + 62 > kScreenH)
        y = kScreenH - 62;
    const std::string &item = itemInHand();
    if (!item.empty()) {
        if (under && canUseOn(item, *under)) {
            put(kCursors[kCursorOperate], x, y);
            return;
        }
        put(kCursors[kCursorEmpty], x, y);
        put("inventory/items/" + item + "/cur" + item + ".png", x + 26, y + 26);
        return;
    }
    if (cursor > 1 && cursor < kCursorCount)
        put(kCursors[cursor], x, y);
}

void ScenePage::updateHotspots() {
    // 0x413040, each hotspot every frame: the mouse come over it (by its
    // own outline or rectangle, if it can be clicked) shows the picture
    // its name names ("exitover") and plays its mouse-over sound; gone,
    // the picture is hidden and the sound stopped. Not while the screen
    // fades.
    if (_fade != _fadeTarget || !_drawing) // a tool not drawing has no mouse
        return;
    const std::string scene = _scene.path + "#" + std::to_string(_index);
    if (scene != _overScene)
        _overScene = scene, _over.clear();
    for (const Ac2Record *o : _scene.objects()) {
        const Ac2Record *hot = o->tag == "SPOT" ? o->part("HOTS") : nullptr;
        if (!hot)
            continue;
        const bool in = objectShown(*o) && hitObject(*o, _lastMouseX, _lastMouseY) && clickable(*o);
        if (in == (_over.count(o) != 0))
            continue;
        const std::string name = fs::toLower(hot->text("name"));
        std::string sound = fs::toLower(hot->text("hoverSound"));
        sound = sound.substr(0, sound.rfind('.'));
        if (in) {
            _over.insert(o);
            if (!name.empty())
                showObject(name, true);
            if (!sound.empty())
                startSound(sound, hot->i("hoverSoundLoops") != 0);
        } else {
            _over.erase(o);
            if (!name.empty())
                showObject(name, false);
            stopSound(sound);
        }
        _cursorMoved = true;
    }
}

void ScenePage::stopSound(const std::string &name) {
    // 0x4251c0: the first sound playing with that name (its file's).
    std::string key = fs::toLower(name);
    if (const size_t slash = key.find_last_of("\\/"); slash != std::string::npos)
        key = key.substr(slash + 1);
    if (key.empty() || !_ctx.mixer)
        return;
    for (auto it = _sounds.begin(); it != _sounds.end(); ++it)
        if (it->first.find(key) != std::string::npos && _ctx.mixer->isPlaying(it->second)) {
            _ctx.mixer->stop(it->second);
            _sounds.erase(it);
            return;
        }
}

void ScenePage::hotspotPressed(const Ac2Record &o) {
    // 0x4131a0: a hotspot's mouse-over sound stopped, its click's played.
    const Ac2Record *hot = o.tag == "SPOT" ? o.part("HOTS") : nullptr;
    if (!hot)
        return;
    auto name = [](std::string s) {
        s = fs::toLower(s);
        return s.substr(0, s.rfind('.'));
    };
    stopSound(name(hot->text("hoverSound")));
    if (!hot->text("clickSound").empty())
        playSound(name(hot->text("clickSound")));
}

bool ScenePage::hotspotKey(uint32_t key) {
    // 0x436110: Escape and Enter press the first hotspot whose key is
    // VK_ESCAPE or VK_RETURN (shown or not), its script run.
    const char *name = key == key::kEscape ? "vk_escape" : key == key::kReturn ? "vk_return" : nullptr;
    if (!name)
        return false;
    for (const Ac2Record *o : _scene.objects()) {
        const Ac2Record *hot = o->tag == "SPOT" ? o->part("HOTS") : nullptr;
        if (!hot || fs::toLower(hot->text("key")) != name)
            continue;
        hotspotPressed(*o);
        runScript(*o);
        return true;
    }
    return false;
}

} // namespace reluva::awe
