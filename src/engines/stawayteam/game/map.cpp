// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/game/map.h"

#include "engines/stawayteam/formats/text_object.h"

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

Error loadMap(const Archive &data, const std::string &path, GameMap &out) {
    std::vector<uint8_t> bytes;
    if (Error err = data.read(path, bytes))
        return err;
    TextObject map;
    if (Error err = readTextObject(std::string(bytes.begin(), bytes.end()), map))
        return Error::fail(path + ": " + err.message);
    out = GameMap{};
    out.width = int(map.integer("Width").value_or(0));
    out.height = int(map.integer("Height").value_or(0));
    const std::string view = text(map, "View Position");
    out.viewX = std::strtof(view.c_str(), nullptr);
    const size_t comma = view.find(',');
    out.viewY = comma == std::string::npos ? 0.0f : std::strtof(view.c_str() + comma + 1, nullptr);
    if (const TextObject *custom = map.object("Custom Map Properties")) {
        out.minimap = text(*custom, "Minimap Image");
        out.song = text(*custom, "Map Song");
    }
    if (const TextObject *plasma = map.object("Plasma Ground"))
        readGround(*plasma, out.ground);
    if (const TextObject *tree = map.object("Tree List"))
        for (const TextField &field : tree->fields) {
            if (field.key != "Level Part" || !field.object)
                continue;
            const TextObject &o = *field.object;
            MapPart part;
            part.className = field.value;
            part.model = text(o, "Model");
            part.sequence = text(o, "Cur Sequence");
            part.x = float(o.number("Position X").value_or(0.0));
            part.y = float(o.number("Position Y").value_or(0.0));
            part.renderingHeight = int(o.integer("Rendering Height").value_or(0));
            part.visible = o.integer("Visible").value_or(1) != 0;
            part.polygon = readPolygon(text(o, "Polygon"));
            part.renderType = text(o, "Render Type");
            part.texture = text(o, "Static Texture");
            part.onGround = o.integer("On Ground").value_or(0) != 0;
            out.parts.push_back(std::move(part));
        }
    return Error::success();
}

} // namespace reluva::stawayteam
