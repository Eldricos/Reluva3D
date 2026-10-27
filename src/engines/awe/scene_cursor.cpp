// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Scenes page's mouse: the game's pick of the object under it
// (0x435eb0), the cursor it shows over it (0x415650, the 16 pictures of
// 0x413f00), and the hotspots that keys press (0x436110). See
// docs/formats/awe/ac2.md.

#include "engines/awe/viewers.h"

#include "common/fs.h"
#include "engines/awe/sprite.h"
#include "engines/awe/tables.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace reluva::awe {

namespace {

// The cursor pictures, by number (0x413f78).
constexpr int kCursorCount = 16;
constexpr const char* kCursors[kCursorCount] = {
    "interface/cursor01.tga",     "interface/cursor02.tga",    "interface/cursorlook.tga",
    "interface/cursortake.tga",   "interface/cursoropen.tga",  "interface/cursorwalk.tga",
    "interface/cursortalk.tga",   "interface/cursorlisten.tga", "interface/cursorstairs.tga",
    "interface/cursorkeyhole.tga", "interface/cursorback.tga", "interface/cursoroperate.tga",
    "interface/cursorshadow.tga", "interface/cursorup.tga",    "interface/cursordown.tga",
    "interface/cursorempty.tga"};
constexpr int kCursorOperate = 11, kCursorEmpty = 15;

bool isCharacter(const Ac2Record& o) { return o.tag == "NPCX" || o.tag == "PCXX"; }

// An outline (listD) makes a region the game tests the point against
// (CreatePolygonRgn, WINDING, 0x40ef10).
bool hasOutline(const Ac2Record& o) {
    const Ac2Value* outline = o.field("listD");
    return outline && outline->numbers.size() >= 3;
}

// PtInRegion over the outline's polygon, filled by winding.
bool inRegion(const Ac2Record& o, int sx, int sy) {
    const auto& pts = o.field("listD")->numbers;
    const float px = float(sx) + 0.5f, py = float(sy) + 0.5f;
    int winding = 0;
    for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
        const float x0 = float(int32_t(pts[j].first)), y0 = float(int32_t(pts[j].second));
        const float x1 = float(int32_t(pts[i].first)), y1 = float(int32_t(pts[i].second));
        const float side = (x1 - x0) * (py - y0) - (px - x0) * (y1 - y0);
        if (y0 <= py) {
            if (y1 > py && side > 0)
                winding++;
        } else if (y1 <= py && side < 0) {
            winding--;
        }
    }
    return winding != 0;
}

// Whether a ray (from, dir) meets a box (lo..hi) in the axes the affine
// `m` puts in the world (GrannyRayIntersectsBox).
bool rayHitsBox(const gfx::Mat4& m, const float lo[3], const float hi[3], const gfx::Vec3& from,
                const gfx::Vec3& dir) {
    // The ray in the box's axes: through the inverse of m's 3x3 part, by
    // its cofactors.
    auto a = [&](int r, int c) { return m[size_t(c * 4 + r)]; };
    float inv[3][3];
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++) {
            const int r0 = (c + 1) % 3, r1 = (c + 2) % 3, c0 = (r + 1) % 3, c1 = (r + 2) % 3;
            inv[r][c] = a(r0, c0) * a(r1, c1) - a(r0, c1) * a(r1, c0);
        }
    const float det = a(0, 0) * inv[0][0] + a(0, 1) * inv[1][0] + a(0, 2) * inv[2][0];
    if (std::abs(det) < 1e-20f)
        return false;
    float o[3], d[3];
    const float rel[3] = {from[0] - a(0, 3), from[1] - a(1, 3), from[2] - a(2, 3)};
    for (int r = 0; r < 3; r++) {
        o[r] = (inv[r][0] * rel[0] + inv[r][1] * rel[1] + inv[r][2] * rel[2]) / det;
        d[r] = (inv[r][0] * dir[0] + inv[r][1] * dir[1] + inv[r][2] * dir[2]) / det;
    }
    float t0 = 0, t1 = 1e30f;
    for (size_t k = 0; k < 3; k++) {
        if (std::abs(d[k]) < 1e-12f) {
            if (o[k] < lo[k] || o[k] > hi[k])
                return false;
            continue;
        }
        float a = (lo[k] - o[k]) / d[k], b = (hi[k] - o[k]) / d[k];
        if (a > b)
            std::swap(a, b);
        t0 = std::max(t0, a), t1 = std::min(t1, b);
        if (t0 > t1)
            return false;
    }
    return true;
}

} // namespace

bool ScenePage::objectShown(const Ac2Record& o) const {
    // The pick's list: objects loaded and shown (+0xb00, +0xb04).
    return !hidden(o) && (testCondition(o.text("condition"), state_) || shown(o));
}

bool ScenePage::clickable(const Ac2Record& o) const {
    // 0x40e570: a script, or a conditional one whose condition holds.
    if (!o.text("script").empty())
        return true;
    if (const Ac2Value* alternatives = o.field("listB"))
        for (const auto& [condition, run] : alternatives->pairs)
            if (testCondition(condition, state_))
                return true;
    return false;
}

int ScenePage::cursorOf(const Ac2Record& o) const {
    // 0x40e890: the first conditional cursor whose condition holds, else
    // its own (the classes' defaults where the file has none: hotspots 5,
    // items 3, characters 6).
    if (const Ac2Value* conditional = o.field("conditionalCursors"))
        for (const auto& [condition, cursor] : conditional->pairs)
            if (testCondition(condition, state_))
                return std::atoi(cursor.c_str());
    if (o.field("cursor"))
        return o.i("cursor");
    return o.tag == "SPOT" ? 5 : o.tag == "ITEM" ? 3 : isCharacter(o) ? 6 : 0;
}

bool ScenePage::objectRect(const Ac2Record& o, int& left, int& top, int& right, int& bottom) const {
    left = o.i("left"), top = o.i("top"), right = o.i("right"), bottom = o.i("bottom");
    if (right <= left || bottom <= top)
        return false; // IsRectEmpty
    if (!isCharacter(o))
        return true;
    // A model's rectangle follows it on the screen: where it was put
    // against where the file has it (0x40ea70 at load, 0x40eb60 as it
    // moves).
    const Ac2Record* camr = scene_.camera();
    if (!camr)
        return true;
    for (const Character& c : characters_) {
        if (c.record != &o)
            continue;
        const SceneCamera cam = SceneCamera::fromRecord(*camr);
        const float was[3] = {o.f("x"), o.f("y"), o.f("z")}, now[3] = {c.x, c.y, c.z};
        float x0, y0, x1, y1;
        if (cam.project(was, kScreenW, kScreenH, x0, y0) && cam.project(now, kScreenW, kScreenH, x1, y1)) {
            const int dx = int(std::lround(x1 - x0)), dy = int(std::lround(y1 - y0));
            left += dx, right += dx, top += dy, bottom += dy;
        }
        break;
    }
    return true;
}

bool ScenePage::hitObject(const Ac2Record& o, int sx, int sy) const {
    // 0x40ef70: in its outline if it has one, else in its rectangle.
    if (hasOutline(o))
        return inRegion(o, sx, sy);
    int l, t, r, b;
    return objectRect(o, l, t, r, b) && sx >= l && sx < r && sy >= t && sy < b;
}

bool ScenePage::hitBones(const Ac2Record& o, int sx, int sy) const {
    // 0x428c80: the ray through the point (GrannyGetPickingRay) against
    // the boxes of the bones named Bip01 something, as they're posed now.
    const Ac2Record* camr = scene_.camera();
    if (!camr)
        return false;
    for (const Character& c : characters_) {
        if (c.record != &o || !c.model || !c.actor)
            continue;
        gfx::Vec3 from, dir;
        SceneCamera::fromRecord(*camr).ray(float(sx), float(sy), kScreenW, kScreenH, from, dir);
        auto hits = [&](const Model& model, const Pose& pose) {
            for (const ModelMesh& mesh : model.meshes)
                for (const ModelMesh::BoneBox& box : mesh.boxes) {
                    if (size_t(box.bone) >= model.bones.size() || size_t(box.bone) >= pose.world.size() ||
                        fs::toLower(model.bones[size_t(box.bone)].name).find("bip01") == std::string::npos)
                        continue;
                    if (rayHitsBox(gfx::multiply(c.placement, pose.world[size_t(box.bone)]), box.min, box.max, from,
                                   dir))
                        return true;
                }
            return false;
        };
        Pose pose;
        c.actor->animator().pose(pose);
        if (hits(c.model->model(), pose))
            return true;
        for (const auto& [name, prop] : c.props) {
            Pose propPose;
            c.actor->animator().poseFor(prop->model(), propPose);
            if (hits(prop->model(), propPose))
                return true;
        }
        return false;
    }
    return false;
}

const Ac2Record* ScenePage::pickAt(int sx, int sy) const {
    // 0x435eb0 as the mouse and clicks use it: the objects shown, ordered
    // by depth (d0, else z: a character's is where it stands) and tried
    // front to back, in three passes. A pass's first hit is the answer.
    std::vector<const Ac2Record*> list;
    for (const Ac2Record* o : scene_.objects())
        if (objectShown(*o))
            list.push_back(o);
    auto depth = [&](const Ac2Record* o) {
        if (o->f("depth") != 0)
            return o->f("depth");
        for (const Character& c : characters_)
            if (c.record == o)
                return c.z;
        return o->f("z");
    };
    std::stable_sort(list.begin(), list.end(),
                     [&](const Ac2Record* a, const Ac2Record* b) { return depth(a) < depth(b); });
    const bool inHand = !itemInHand().empty();
    auto hasRect = [&](const Ac2Record& o) {
        int l, t, r, b;
        return objectRect(o, l, t, r, b);
    };
    // 1: pickFirst objects with an outline (or no rectangle).
    for (auto it = list.rbegin(); it != list.rend(); ++it)
        if ((!hasRect(**it) || hasOutline(**it)) && (*it)->i("pickFirst") && hitObject(**it, sx, sy))
            return *it;
    // 2: the characters, those with only a rectangle by their bones' boxes
    // if they can be clicked (or an item is in hand).
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
        const Ac2Record& o = **it;
        if (!isCharacter(o))
            continue;
        const bool byBones = hasRect(o) && !hasOutline(o);
        if (byBones && !inHand && !clickable(o))
            continue;
        if (byBones ? hitBones(o, sx, sy) : hitObject(o, sx, sy))
            return &o;
    }
    // 3: everything, by outline or rectangle.
    for (auto it = list.rbegin(); it != list.rend(); ++it)
        if (hitObject(**it, sx, sy))
            return *it;
    return nullptr;
}

const std::string& ScenePage::itemInHand() const {
    // The item the next click uses (U for now: the inventory screen hands
    // one over here).
    return inHand_;
}

bool ScenePage::canUseOn(const std::string& item, const Ac2Record& o) {
    // 0x419e90: one of its conditional scripts asks for an item and holds
    // with this one in hand (0x40e7e0 in use mode), or, for a character,
    // uselist.txt has a row for the item and it whose condition holds
    // (0x41c4b0).
    const std::string name = fs::toLower(item), was = state_.held;
    state_.held = name;
    bool can = false;
    if (const Ac2Value* alternatives = o.field("listB"))
        for (const auto& [condition, run] : alternatives->pairs)
            if (fs::toLower(condition).find("use") != std::string::npos && testCondition(condition, state_)) {
                can = true;
                break;
            }
    state_.held = was;
    if (can || o.tag != "NPCX")
        return can;
    auto unquoted = [](std::string s) {
        return s.size() >= 2 && s.front() == '"' && s.back() == '"' ? s.substr(1, s.size() - 2) : s;
    };
    const Table& t = itemTable("uselist.txt");
    const std::string model = fs::toLower(o.text("file"));
    for (size_t r = 0; r < t.rows.size(); r++)
        if (fs::toLower(unquoted(t.cell(r, 0))) == name &&
            model.find(fs::toLower(unquoted(t.cell(r, 1))) + "_model") != std::string::npos &&
            testCondition(unquoted(t.cell(r, 2)), state_))
            return true;
    return false;
}

bool ScenePage::interfaceShown() const {
    // 0x4143c0: the scene shows the interface (SHOW), and "interface off"
    // hasn't hidden it.
    for (const Ac2Record& r : scene_.records)
        if (r.tag == "SHOW")
            return r.i("value") != 0 && !interfaceOff_;
    return !interfaceOff_;
}

bool ScenePage::cursorShown() const {
    // 0x4140b0: none while the interface is off, a movie or the loading
    // screen shows; else where the interface shows, in the menus, and in
    // conversations.
    if (!sceneShown_ || movie_ || load_.screen || interfaceOff_)
        return false;
    return interfaceShown() || location_.rfind("menu.", 0) == 0 || dialogue_->active();
}

bool ScenePage::pickAllowed(int sy) const {
    // 0x414400, 0x415fa0: not while the screen fades, a locked script
    // runs or a conversation goes on; not over the interface bar (the top
    // 60 pixels while it's down, else 5: 0x417580).
    if (!sceneShown_ || movie_ || load_.screen || fade_ != fadeTarget_ || runner_->locked() || dialogue_->active())
        return false;
    return sy >= 5;
}

int ScenePage::cursorUnder(int sx, int sy, const Ac2Record** under) const {
    // 0x4154f5: the cursor of the object under the mouse if it can be
    // clicked, else 0 (the arrow).
    const Ac2Record* o = pickAllowed(sy) ? pickAt(sx, sy) : nullptr;
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
    const Ac2Record* under = nullptr;
    const int cursor = cursorUnder(lastMouseX_, lastMouseY_, &under);
    int x = lastMouseX_, y = lastMouseY_;
    auto put = [&](const std::string& file, int px, int py) {
        SpriteTransform t;
        t.x = float(px), t.y = float(py);
        drawSprite(ctx_.screen, picture(file), t, true);
    };
    put(kCursors[cursor == 1 ? 1 : 0], x, y);
    if (x + 62 > kScreenW)
        x = kScreenW - 62;
    if (y + 62 > kScreenH)
        y = kScreenH - 62;
    const std::string& item = itemInHand();
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

bool ScenePage::hotspotKey(uint32_t key) {
    // 0x436110: Escape and Enter press the first hotspot whose key is
    // VK_ESCAPE or VK_RETURN (shown or not), its script run.
    const char* name = key == key::Escape ? "vk_escape" : key == key::Return ? "vk_return" : nullptr;
    if (!name)
        return false;
    for (const Ac2Record* o : scene_.objects()) {
        const Ac2Record* hot = o->tag == "SPOT" ? o->part("HOTS") : nullptr;
        if (!hot || fs::toLower(hot->text("key")) != name)
            continue;
        runScript(*o);
        return true;
    }
    return false;
}

} // namespace reluva::awe
