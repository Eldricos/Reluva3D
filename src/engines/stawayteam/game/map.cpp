// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/game/map.h"

#include "engines/stawayteam/formats/binary_object.h"
#include "engines/stawayteam/formats/text_object.h"

#include <cctype>
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

#include <cstdlib>

namespace reluva::stawayteam {

namespace {

std::string text(const TextObject &object, const char *key) {
    const std::string *value = object.value(key);
    return value ? *value : std::string();
}

// A row of the ground's grid: two hex digits a corner.
void readRow(const std::string &hex, int width, std::vector<uint8_t> &out) {
    for (int i = 0; i < width; ++i) {
        unsigned value = 0;
        if (size_t(i) * 2 + 2 <= hex.size())
            value = unsigned(std::strtoul(hex.substr(size_t(i) * 2, 2).c_str(), nullptr, 16));
        out.push_back(uint8_t(value));
    }
}

void readGround(const TextObject &plasma, GroundMap &out) {
    out.blending = float(plasma.number("Blending").value_or(0.25));
    const long count = plasma.integer("Num Textures").value_or(0);
    for (long i = 0; i < count; ++i)
        out.textures.push_back(text(plasma, ("Texture " + std::to_string(i)).c_str()));
    // The grid's size by its rows: as many as there are, as wide as the
    // first.
    const std::string *first = plasma.value("Elevations Row 0");
    out.width = first ? int(first->size() / 2) : 0;
    for (int row = 0;; ++row) {
        const std::string *values = plasma.value("Elevations Row " + std::to_string(row));
        if (!values)
            break;
        readRow(*values, out.width, out.values);
        const std::string *seeds = plasma.value("Seeds Row " + std::to_string(row));
        readRow(seeds ? *seeds : std::string(), out.width, out.seeds);
        out.height = row + 1;
    }
}

std::vector<std::pair<float, float>> readPolygon(const std::string &points) {
    std::vector<std::pair<float, float>> out;
    const char *p = points.c_str();
    for (;;) {
        char *end = nullptr;
        const float x = std::strtof(p, &end);
        if (end == p)
            break;
        p = end;
        while (*p == ',' || *p == ' ')
            ++p;
        const float y = std::strtof(p, &end);
        if (end == p)
            break;
        p = end;
        while (*p == ',' || *p == ' ')
            ++p;
        out.emplace_back(x, y);
    }
    return out;
}

} // namespace

std::vector<CameraScript> readCameraScripts(const TextObject &manager) {
    std::vector<CameraScript> out;
    const TextObject *array = manager.object("Camera Script Array");
    if (!array)
        return out;
    for (const TextField *item : array->items()) {
        if (!item->object)
            continue;
        const TextObject &o = *item->object;
        CameraScript s;
        s.where = text(o, "Where To Pan");
        s.timeToPan = float(o.number("Time To Pan").value_or(0.0));
        s.timeToWait = float(o.number("Time To Wait").value_or(0.0));
        s.panToPosition = o.integer("Pan To Position").value_or(0) != 0;
        s.action = o.object("Destination Action");
        out.push_back(s);
    }
    return out;
}

namespace {

std::string lowered(std::string s) {
    for (char &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

bool inCategory(const std::string &categories, const std::string &wanted) {
    size_t at = 0;
    const std::string have = "," + lowered(categories) + ",";
    const std::string want = lowered(wanted);
    while (at <= want.size()) {
        const size_t comma = std::min(want.find(',', at), want.size());
        std::string one = want.substr(at, comma - at);
        while (!one.empty() && one.front() == ' ')
            one.erase(one.begin());
        while (!one.empty() && one.back() == ' ')
            one.pop_back();
        if (!one.empty() && have.find("," + one + ",") != std::string::npos)
            return true;
        at = comma + 1;
    }
    return false;
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

MapPart readPart(const std::string &className, const TextObject &o) {
    MapPart part;
    part.className = className;
    part.name = text(o, "Name");
    // A spawn point's facing, in its AI (CSpawnPointAI).
    if (const TextObject *activity = o.object("Activity"))
        for (const TextField *ai : activity->items()) {
            if (!ai->object)
                continue;
            part.activities.push_back(ai->object.get());
            if (ai->value == "CSpawnPointAI" && ai->object->number("Facing Angle"))
                part.facing = float(*ai->object->number("Facing Angle"));
        }
    part.model = text(o, "Model");
    part.sequence = text(o, "Cur Sequence");
    part.x = float(o.number("Position X").value_or(0.0));
    part.y = float(o.number("Position Y").value_or(0.0));
    part.renderingHeight = int(o.integer("Rendering Height").value_or(0));
    part.visible = o.integer("Visible").value_or(1) != 0;
    part.active = o.integer("Active").value_or(1) != 0;
    part.collideable = o.integer("Collideable").value_or(1) != 0;
    part.fullHeight = o.integer("Full Height").value_or(1) != 0;
    if (const TextObject *inventory = o.object("Inventory"))
        if (const TextObject *list = inventory->object("Inventory List"))
            for (const TextField *item : list->items())
                if (item->object) {
                    part.inventory.push_back(text(*item->object, "Definition Index"));
                    part.stuns = part.stuns || text(*item->object, "Weapon Setting") == "Stun";
                }
    part.polygon = readPolygon(text(o, "Polygon"));
    part.angle = float(o.number("Facing Angle").value_or(0.0));
    part.overlay = text(o, "Overlay Model");
    if (part.overlay == "!Unknown Model")
        part.overlay.clear();
    part.renderType = text(o, "Render Type");
    part.destroyedAction = o.object("Destroyed Action");
    part.category = text(o, "Category");
    part.deathType = text(o, "Default Death Type");
    part.texture = text(o, "Static Texture");
    if (part.renderType == "Warped")
        part.warpEffect = text(o, "Warp Effect");
    part.onGround = o.integer("On Ground").value_or(0) != 0;

    return part;
}

Error loadMap(const Archive &data, const std::string &path, GameMap &out) {
    std::vector<uint8_t> bytes;
    if (Error err = data.read(path, bytes))
        return err;
    auto object = std::make_shared<TextObject>();
    if (Error err = readTextObject(std::string(bytes.begin(), bytes.end()), *object))
        return Error::fail(path + ": " + err.message);
    const TextObject &map = *object;
    out = GameMap{};
    out.object = object;
    out.width = int(map.integer("Width").value_or(0));
    out.height = int(map.integer("Height").value_or(0));
    const std::string view = text(map, "View Position");
    out.viewX = std::strtof(view.c_str(), nullptr);
    const size_t comma = view.find(',');
    out.viewY = comma == std::string::npos ? 0.0f : std::strtof(view.c_str() + comma + 1, nullptr);
    if (const TextObject *custom = map.object("Custom Map Properties")) {
        out.minimap = text(*custom, "Minimap Image");
        out.song = text(*custom, "Map Song");
        if (const TextObject *manager = custom->object("Camera Script Manager"))
            out.cameraScripts = readCameraScripts(*manager);
    }
    if (const TextObject *plasma = map.object("Plasma Ground"))
        readGround(*plasma, out.ground);
    if (const TextObject *tree = map.object("Tree List"))
        for (const TextField &field : tree->fields) {
            if (field.key != "Level Part" || !field.object)
                continue;
            out.parts.push_back(readPart(field.value, *field.object));
        }
    return Error::success();
}

namespace {

// The first object of class `className` in `o`, however deep.
const TextObject *find(const TextObject &o, const std::string &className) {
    if (o.className == className)
        return &o;
    for (const TextField &f : o.fields)
        if (f.object)
            if (const TextObject *found = find(*f.object, className))
                return found;
    return nullptr;
}

} // namespace

Error loadModelInfo(const Archive &data, const std::string &model, ModelInfo &out) {
    out = ModelInfo{};
    std::vector<uint8_t> bytes;
    const std::string path = "Cache/Models/" + model + ".mdl16";
    if (Error err = data.read(path, bytes))
        return err;
    BinaryObject object;
    if (Error err = readBinaryObject(bytes.data(), bytes.size(), "CModel", object))
        return Error::fail(path + ": " + err.message);
    out.footprint = readPolygon(text(object.object, "Bounds Poly"));
    out.hitPoints = int(object.object.number("Hit Points").value_or(0.0));
    out.sortingY = int(object.object.integer("Sorting Y").value_or(0));
    if (const TextObject *entity = object.object.object("Default Entity"))
        if (const std::string *category = entity->value("Category"))
            out.category = *category;
    out.explosion = text(object.object, "Explosion");
    if (out.explosion == "!Unknown Model")
        out.explosion.clear();
    out.deathSound = text(object.object, "Death Sound");
    out.explosionRadius = float(object.object.number("Explosion Radial Damage").value_or(0.0));
    out.explosionNeedsSight = object.object.integer("Require LOS For Damage").value_or(0) != 0;
    out.maxVelocity = float(object.object.number("Max Velocity").value_or(0.0));
    out.bounceFactor = float(object.object.number("Bounce Factor").value_or(0.0));
    out.bounceFriction = float(object.object.number("Bounce Friction").value_or(0.0));
    out.minBounce = float(object.object.number("Min Bounce").value_or(0.0));
    out.minBounceFriction = float(object.object.number("Min Bounce Friction").value_or(0.0));
    out.descriptionScript = text(object.object, "Description Script");
    out.descriptionLine = text(object.object, "Description Line");
    out.defaultSequence = text(object.object, "Default Sequence");
    out.activateSound = text(object.object, "Activate Sound");
    if (const TextObject *entity = object.object.object("Default Entity"))
        if (const TextObject *ai = find(*entity, "CActionAI"))
            if (const TextObject *arm = ai->object("Action"); arm && arm->className == "CDelayAction") {
                out.armTime = float(arm->number("Delay").value_or(0.0));
                if (const TextObject *trigger = find(*arm, "CTouchingOvalTriggerAI")) {
                    out.triggerRadius = float(trigger->number("X Radius").value_or(0.0));
                    out.triggerRatio = float(trigger->number("YX Ratio").value_or(1.0));
                    if (const TextObject *enter = trigger->object("Enter Action")) {
                        if (const TextObject *destroy = find(*enter, "CDelayAction"))
                            out.fuseAfterTrigger = float(destroy->number("Delay").value_or(0.0));
                        if (const TextObject *noise = find(*enter, "CAddNoiseAction")) {
                            out.triggerNoise = float(noise->number("Radius").value_or(0.0));
                            out.triggerNoiseAge = float(noise->number("Age").value_or(0.0));
                        }
                    }
                }
            }
    if (const TextObject *entity = object.object.object("Default Entity")) {
        out.fuse = float(entity->number("Shot Duration").value_or(0.0));
        if (const TextObject *noise = entity->object("Destroyed Action"))
            if (noise->className == "CAddNoiseAction") {
                out.noiseRadius = float(noise->number("Radius").value_or(0.0));
                out.noiseAge = float(noise->number("Age").value_or(0.0));
            }
    }
    if (const TextObject *entity = object.object.object("Default Entity"))
        if (const TextObject *inventory = entity->object("Inventory"))
            if (const TextObject *list = inventory->object("Inventory List"))
                for (const TextField *item : list->items())
                    if (item->object)
                        out.inventory.push_back(text(*item->object, "Definition Index"));
    return Error::success();
}

Error loadWayPoints(const Archive &data, const std::string &path, WayPoints &out) {
    std::vector<uint8_t> bytes;
    if (Error err = data.read(path, bytes))
        return err;
    BinaryObject object;
    if (Error err = readBinaryObject(bytes.data(), bytes.size(), "CWayPointMap", object))
        return Error::fail(path + ": " + err.message);
    out.points.clear();
    for (const WayPoint &p : object.wayPoints)
        out.points.push_back({float(p.x), float(p.y), {}});
    for (const PathLine &line : object.lines) {
        const int n = int(out.points.size());
        if (line.a < 0 || line.b < 0 || line.a >= n || line.b >= n)
            continue;
        out.points[size_t(line.a)].neighbours.push_back(line.b);
        out.points[size_t(line.b)].neighbours.push_back(line.a);
    }
    return Error::success();
}

int WayPoints::nearest(float x, float y) const {
    int best = -1;
    float bestDistance = std::numeric_limits<float>::max();
    for (size_t i = 0; i < points.size(); ++i) {
        const float dx = points[i].x - x;
        const float dy = points[i].y - y;
        if (dx * dx + dy * dy < bestDistance) {
            bestDistance = dx * dx + dy * dy;
            best = int(i);
        }
    }
    return best;
}

std::vector<std::pair<float, float>> WayPoints::path(float x0, float y0, float x1, float y1) const {
    // A* over the lines, by their lengths (inferred: the program's own
    // search isn't traced).
    const int from = nearest(x0, y0);
    const int to = nearest(x1, y1);
    if (from < 0 || to < 0)
        return {};
    const auto distance = [&](int a, int b) {
        return std::hypot(points[size_t(a)].x - points[size_t(b)].x, points[size_t(a)].y - points[size_t(b)].y);
    };
    std::vector<float> cost(points.size(), std::numeric_limits<float>::max());
    std::vector<int> came(points.size(), -1);
    using Entry = std::pair<float, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    cost[size_t(from)] = 0.0f;
    open.push({distance(from, to), from});
    while (!open.empty()) {
        const int at = open.top().second;
        open.pop();
        if (at == to)
            break;
        for (int next : points[size_t(at)].neighbours) {
            const float c = cost[size_t(at)] + distance(at, next);
            if (c < cost[size_t(next)]) {
                cost[size_t(next)] = c;
                came[size_t(next)] = at;
                open.push({c + distance(next, to), next});
            }
        }
    }
    if (from != to && came[size_t(to)] < 0)
        return {};
    std::vector<std::pair<float, float>> out = {{x1, y1}};
    for (int at = to; at >= 0; at = came[size_t(at)])
        out.emplace_back(points[size_t(at)].x, points[size_t(at)].y);
    std::reverse(out.begin(), out.end());
    return out;
}

} // namespace reluva::stawayteam
