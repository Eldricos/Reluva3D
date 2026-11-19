// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/pages/scene_browser.h"

#include "engines/awe/game/camera.h"
#include "graphics/font.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace reluva::awe {

void SceneBrowser::sceneLoading() {
    _selected = -1;
    _hover = {};
}

void SceneBrowser::drawOverlays(const std::vector<const Ac2Record *> &objects) {
    if (_showGrid)
        drawGrid();
    if (_hotspots)
        for (const Ac2Record *o : objects) {
            const int l = o->i("left");
            const int t = o->i("top");
            const int r = o->i("right");
            const int b = o->i("bottom");
            if (o->tag != "SPOT" || r <= l || b <= t || !objectShown(*o))
                continue;
            // Its outline where it has one (listD), else its rectangle.
            const Ac2Value *outline = o->field("listD");
            if (outline && outline->numbers.size() >= 3) {
                const auto &pts = outline->numbers;
                for (size_t i = 0; i < pts.size(); i++)
                    line(int(pts[i].first), int(pts[i].second), int(pts[(i + 1) % pts.size()].first),
                         int(pts[(i + 1) % pts.size()].second), {255, 220, 60});
            } else {
                frame(l, t, r - l, b - t, {255, 220, 60});
            }
            const Ac2Record *hot = o->part("HOTS");
            std::string name = o->text("name");
            if (name.empty() && hot)
                name = hot->text("name");
            label(l + 2, t + 2, name.empty() ? shorten(o->text("script"), 60) : name, {255, 220, 60});
        }
    if (_selected >= 0 && size_t(_selected) < objects.size()) {
        const Ac2Record *o = objects[size_t(_selected)];
        // Its picture as it's drawn: an animation's frame, else its file.
        const auto loop = _loops.find(o);
        const image::Image &img = loop != _loops.end() ? loop->second.frame : picture(pictureFile(*o));
        if (img.w > 0)
            frame(int(o->f("x")), int(o->f("y")), img.w, img.h, {90, 255, 120});
        // Its fields, as the dump shows them.
        int y = frameH() - 2 * font::kLineHeight - 6;
        std::vector<std::string> lines;
        std::string text;
        for (const auto &[name, v] : o->fields) {
            const std::string s = v.str();
            if (s == "0" || s == "\"\"" || s == "[]")
                continue;
            const std::string item = name + "=" + shorten(s, 60) + "  ";
            if (font::length(text + item) * font::kAdvance > size_t(frameW() - 8))
                lines.push_back(text), text.clear();
            text += item;
        }
        for (const Ac2Record &p : o->parts) {
            text += p.tag + "{";
            for (const auto &[name, v] : p.fields)
                if (v.str() != "0" && v.str() != "\"\"" && v.str() != "[]")
                    text += name + "=" + shorten(v.str(), 40) + " ";
            text += "} ";
        }
        lines.push_back(text);
        y -= int(lines.size()) * font::kLineHeight;
        for (const std::string &l : lines) {
            label(4, y, l, {150, 255, 170});
            y += font::kLineHeight;
        }
    }
    if (_info) {
        std::vector<std::string> lines;
        lines.push_back((_names.empty() ? std::string("No scenes") : _names[size_t(_index)]) + "  (" +
                        std::to_string(_index + 1) + "/" + std::to_string(_names.size()) + ")" +
                        (_base.empty() ? "" : "  over " + _base));
        if (!_error.empty())
            lines.push_back(_error);
        std::map<std::string, int> tags;
        for (const Ac2Record *o : objects)
            tags[o->tag]++;
        std::string counts = std::to_string(objects.size()) + " objects:";
        for (const auto &[t, n] : tags)
            counts += " " + std::to_string(n) + " " + t;
        lines.push_back(counts);
        if (!_grid.empty()) {
            std::string g = "walk grid: cells of " + std::to_string(_grid.cellSize());
            if (_hover.valid())
                g += ", under the mouse (" + std::to_string(_hover.a) + ", " + std::to_string(_hover.b) +
                     ") = " + std::to_string(_grid.at(_hover.a, _hover.b));
            if (!_path.empty())
                g += ", path of " + std::to_string(_path.size()) + " points";
            lines.push_back(g);
        }
        if (!_scriptLog.empty()) {
            std::string tags;
            for (const auto &[t, v] : _state.tags)
                tags += " " + t + "=" + std::to_string(v);
            std::string items;
            for (const GameState::Item &i : _state.items)
                items += " " + i.name;
            lines.push_back("script: tags" + (tags.empty() ? std::string(" none") : tags) + "; items" +
                            (items.empty() ? std::string(" none") : items));
            for (const std::string &l : _scriptLog)
                lines.push_back(l);
        }
        lines.push_back(std::string("train ") + (_state.values["moving"] ? "moving" : "stopped") + ", weather " +
                        (_state.values["weather"] ? "on" : "off") + (_state.values["time"] ? ", night" : "") +
                        (_state.act.empty() ? "" : ", act " + _state.act));
        if (const Ac2Record *cam = _scene.camera()) {
            char buf[200];
            std::snprintf(buf, sizeof buf,
                          "camera about (%g, %g, %g), turned (%g, %g, %g), back (%g, %g, %g), fov %.1f deg",
                          cam->f("x"), cam->f("y"), cam->f("z"), cam->f("pitch"), cam->f("yaw"), cam->f("roll"),
                          cam->f("offsetX"), cam->f("offsetY"), cam->f("offsetZ"), cam->f("fov") * 57.29578f);
            lines.push_back(buf);
        }
        for (const Ac2Record &r : _scene.records)
            if (r.tag == "MUSC" || r.tag == "EVNT")
                if (const Ac2Value *v = r.fields.empty() ? nullptr : &r.fields[0].second; v && !v->pairs.empty())
                    lines.push_back(r.tag + " " + shorten(v->str(), 150));
        for (const Ac2Record *o : objects)
            if (o->tag == "NPCX" || o->tag == "PCXX") {
                char buf[200];
                std::string anim;
                // Where it is now, else where the scene puts it.
                float x = o->f("x");
                float y = o->f("y");
                float z = o->f("z");
                float rotY = o->f("rotY");
                for (const Character &c : _characters)
                    if (c.record == o && c.actor) {
                        x = c.x, y = c.y, z = c.z, rotY = c.rotY;
                        for (const Animator::Slot &sl : c.actor->animator().slots())
                            if (sl.state != Animator::kStopped)
                                anim += (anim.empty() ? ", playing " : " ") + sl.name;
                    }
                std::snprintf(buf, sizeof buf, "%s %s at (%g, %g, %g), facing %g%s", o->tag.c_str(),
                              o->text("file").c_str(), x, y, z, rotY, anim.c_str());
                lines.push_back(buf);
            }
        int y = 2;
        for (const std::string &l : lines) {
            label(4, y, shorten(l, 168), l == _error ? Color{255, 120, 120} : Color{230, 230, 230});
            y += font::kLineHeight;
        }
    }
}

void SceneBrowser::drawFooter() {
    help(std::string("Left/Right: scene  Up/Down: object  H: hotspots  I (F9 in a game): info  M: moving  W: weather  "
                     "T: night  G: grid  N: new game  click: walk") +
         (_renderer ? "  C: characters" : "  (no 3D: no characters)"));
}

std::string SceneBrowser::windowTitle() {
    return "Scenes: " + (_names.empty() ? std::string() : _names[size_t(_index)]);
}

bool SceneBrowser::viewerEvent(const Event &ev) {
    if (ev.type == EventType::kMouseMove) {
        // The grid's cell under the mouse, while the grid shows.
        if (!_showGrid)
            return false;
        float x;
        float z;
        WalkGrid::Cell cell;
        if (const Ac2Record *cam = _scene.camera();
            cam && SceneCamera::fromRecord(*cam).floorAt(float(ev.mouseX), float(ev.mouseY), frameW(), frameH(), x, z))
            cell = _grid.cellAt(x, z);
        if (!(cell == _hover)) {
            _hover = cell;
            draw();
        }
        return true;
    }
    if (ev.type != EventType::kKeyDown)
        return false;
    if (const int d = step(ev.key, _index, int(_names.size()))) {
        _index += d;
        load();
        return true;
    }
    const int n = int(_scene.objects().size());
    if (ev.key == key::kUp || ev.key == key::kDown) {
        if (n > 0)
            _selected = ev.key == key::kDown ? (_selected + 1 >= n ? -1 : _selected + 1)
                                             : (_selected < 0 ? n - 1 : _selected - 1);
    } else if (ev.key == 'h') {
        _hotspots = !_hotspots;
    } else if (ev.key == 'n') {
        newGame();
        return true;
    } else if (ev.key == 'g') {
        _showGrid = !_showGrid;
    } else if (ev.key == 'm' || ev.key == 'w' || ev.key == 't') {
        int &v = _state.values[ev.key == 'm' ? "moving" : ev.key == 'w' ? "weather" : "time"];
        v = !v;
        retestObjects(); // the viewer's own: shown at once
    } else if (ev.key == 'i' || ev.key == key::kF1 + 8) {
        // (In a game I is the inventory's: F9.)
        _info = !_info;
    } else if (ev.key == 'c' && _renderer) {
        _showCharacters = !_showCharacters;
        if (_showCharacters && _characters.empty())
            loadCharacters();
    } else {
        return false;
    }
    draw();
    return true;
}

void SceneBrowser::drawGrid() {
    const Ac2Record *camr = _scene.camera();
    if (!camr || _grid.empty())
        return;
    const SceneCamera cam = SceneCamera::fromRecord(*camr);
    const float size = float(_grid.cellSize());
    auto corner = [&](float x, float z, std::pair<float, float> &out) {
        const float p[3] = {x, 0, z};
        return cam.project(p, frameW(), frameH(), out.first, out.second);
    };
    // A cell's square on the floor, on the screen.
    auto square = [&](WalkGrid::Cell cell, std::vector<std::pair<float, float>> &pts) {
        const float x0 = float(cell.a - 32) * size;
        const float z1 = float(32 - cell.b) * size;
        pts.assign(4, {});
        return corner(x0, z1 - size, pts[0]) && corner(x0 + size, z1 - size, pts[1]) && corner(x0 + size, z1, pts[2]) &&
               corner(x0, z1, pts[3]);
    };
    std::vector<std::pair<float, float>> pts;
    for (int a = 0; a < WalkGrid::kSize; a++)
        for (int b = 0; b < WalkGrid::kSize; b++) {
            const uint8_t v = _grid.at(a, b);
            if (v == 0 || !square({a, b}, pts))
                continue;
            // 1 blocked, 2 blocked unless allowed, 3 blocked at first.
            const Color c = v == 1 ? Color{230, 50, 50} : v == 2 ? Color{240, 150, 40} : Color{240, 230, 60};
            fillPolygon(pts, c, 90);
        }
    if (_hover.valid() && square(_hover, pts))
        for (size_t i = 0; i < 4; i++)
            line(int(pts[i].first), int(pts[i].second), int(pts[(i + 1) % 4].first), int(pts[(i + 1) % 4].second),
                 _grid.blocked(_hover, true) ? Color{255, 80, 80} : Color{80, 255, 255});
    for (size_t i = 0; i + 1 < _path.size(); i++) {
        float x0;
        float z0;
        float x1;
        float z1;
        _grid.centre(_path[i], x0, z0);
        _grid.centre(_path[i + 1], x1, z1);
        std::pair<float, float> p0;
        std::pair<float, float> p1;
        if (corner(x0, z0, p0) && corner(x1, z1, p1))
            line(int(p0.first), int(p0.second), int(p1.first), int(p1.second), {90, 255, 120});
    }
}

} // namespace reluva::awe
