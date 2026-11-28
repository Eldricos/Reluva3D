// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/formats/keyframe.h"

#include <string>

namespace reluva::sith {

bool Keyframes::at(const Node &node, float frame, Vec3 &position, Vec3 &rotation) const {
    if (node.entries.empty())
        return false;
    const Entry *e = &node.entries.front();
    for (const Entry &k : node.entries)
        if (float(k.frame) <= frame)
            e = &k;
    const float t = frame - float(e->frame);
    position = {e->position.x + e->positionDelta.x * t, e->position.y + e->positionDelta.y * t,
                e->position.z + e->positionDelta.z * t};
    rotation = {e->rotation.x + e->rotationDelta.x * t, e->rotation.y + e->rotationDelta.y * t,
                e->rotation.z + e->rotationDelta.z * t};
    return true;
}

Error parseKeyframes(const std::vector<uint8_t> &data, Keyframes &out) {
    out = Keyframes{};
    TextLines lines(data);
    auto bad = [&](const std::string &what) {
        return Error::fail("keyframe line " + std::to_string(lines.lineNumber()) + ": can't read " + what);
    };
    auto vec = [](const std::vector<std::string_view> &w, size_t at, Vec3 &v) {
        return w.size() >= at + 3 && toFloat(w[at], v.x) && toFloat(w[at + 1], v.y) && toFloat(w[at + 2], v.z);
    };
    Keyframes::Node *node = nullptr;
    // The game reads as many nodes as NODES gives and stops (JK.EXE
    // 0x44b1a3): Mysteries of the Sith's s1shred.key has a stray line after.
    int nodeCount = -1;
    while (!lines.done()) {
        const std::string_view line = lines.next();
        const std::vector<std::string_view> w = words(line);
        bool ok = true;
        if (startsWith(line, "SECTION:"))
            continue;
        if (startsWith(line, "NODES")) {
            if (w.size() < 2 || !toInt(w[1], nodeCount))
                return bad("the node count");
            continue;
        }
        if (startsWith(line, "FLAGS"))
            ok = w.size() > 1 && toFlags(w[1], out.flags);
        else if (startsWith(line, "TYPE"))
            ok = w.size() > 1 && toFlags(w[1], out.type);
        else if (startsWith(line, "FRAMES"))
            ok = w.size() > 1 && toInt(w[1], out.frames);
        else if (startsWith(line, "FPS"))
            ok = w.size() > 1 && toFloat(w[1], out.fps);
        else if (startsWith(line, "JOINTS"))
            ok = w.size() > 1 && toInt(w[1], out.joints);
        else if (startsWith(line, "MARKERS")) {
            int n = 0;
            if (w.size() < 2 || !toInt(w[1], n) || n < 0)
                return bad("the marker count");
            for (int i = 0; i < n; ++i) {
                const std::vector<std::string_view> m = words(lines.next());
                Keyframes::Marker marker;
                if (m.size() < 2 || !toFloat(m[0], marker.frame) || !toInt(m[1], marker.type))
                    return bad("a marker");
                out.markers.push_back(marker);
            }
        } else if (startsWith(line, "NODE")) {
            node = &out.nodes.emplace_back();
            ok = w.size() > 1 && toInt(w[1], node->number);
        } else if (startsWith(line, "MESH")) {
            if (!node || w.size() < 2 || !startsWith(after(line, "MESH"), "NAME"))
                return bad("a node's mesh name");
            node->meshName = after(after(line, "MESH"), "NAME");
        } else if (startsWith(line, "ENTRIES")) {
            int n = 0;
            if (!node || w.size() < 2 || !toInt(w[1], n) || n < 0)
                return bad("the entry count");
            // Each entry is two lines: its frame, flags and values, then
            // their changes each frame.
            for (int i = 0; i < n; ++i) {
                int num = 0;
                std::vector<std::string_view> f;
                Keyframes::Entry e;
                if (!numbered(lines.next(), num, f) || f.size() < 8 || !toInt(f[0], e.frame) ||
                    !toFlags(f[1], e.flags) || !vec(f, 2, e.position) || !vec(f, 5, e.rotation))
                    return bad("an entry");
                const std::vector<std::string_view> d = words(lines.next());
                if (!vec(d, 0, e.positionDelta) || !vec(d, 3, e.rotationDelta))
                    return bad("an entry's changes");
                node->entries.push_back(e);
            }
            if (nodeCount >= 0 && out.nodes.size() == size_t(nodeCount))
                break;
        } else
            return bad("an unknown line");
        if (!ok)
            return bad("a header value");
    }
    if (out.frames <= 0 || out.fps <= 0)
        return Error::fail("keyframe file without its frames or speed");
    return Error::success();
}

} // namespace reluva::sith
