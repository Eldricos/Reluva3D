// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/formats/sprite.h"

#include <string>

namespace reluva::sith {

Error parseSprite(const std::vector<uint8_t> &data, Sprite &out) {
    out = Sprite{};
    TextLines lines(data);
    const std::vector<std::string_view> w = words(lines.next());
    if (w.size() < 11 || !toInt(w[1], out.type) || !toFloat(w[2], out.width) || !toFloat(w[3], out.height) ||
        !toInt(w[4], out.geometryMode) || !toInt(w[5], out.lightingMode) || !toInt(w[6], out.textureMode) ||
        !toFloat(w[7], out.extraLight) || !toFloat(w[8], out.offset.x) || !toFloat(w[9], out.offset.y) ||
        !toFloat(w[10], out.offset.z))
        return Error::fail("sprite without its eleven fields");
    out.material = w[0];
    return Error::success();
}

Error parseParticles(const std::vector<uint8_t> &data, Particles &out) {
    out = Particles{};
    TextLines lines(data);
    auto bad = [&](const std::string &what) {
        return Error::fail("particles line " + std::to_string(lines.lineNumber()) + ": can't read " + what);
    };
    while (!lines.done()) {
        const std::string_view line = lines.next();
        const std::vector<std::string_view> w = words(line);
        bool ok = true;
        if (startsWith(line, "SECTION:") || startsWith(line, "PAR"))
            continue;
        if (startsWith(line, "SIZE"))
            ok = w.size() > 1 && toFloat(w[1], out.size);
        else if (startsWith(line, "MATERIAL")) {
            ok = w.size() > 1;
            if (ok)
                out.material = w[1];
        } else if (startsWith(line, "LIGHTINGMODE"))
            ok = w.size() > 1 && toInt(w[1], out.lightingMode);
        else if (startsWith(line, "RADIUS"))
            ok = w.size() > 1 && toFloat(w[1], out.radius);
        else if (startsWith(line, "INSERT"))
            ok = w.size() > 4 && toFloat(w[2], out.insertOffset.x) && toFloat(w[3], out.insertOffset.y) &&
                 toFloat(w[4], out.insertOffset.z);
        else if (startsWith(line, "VERTICES")) {
            int n = 0;
            if (w.size() < 2 || !toInt(w[1], n) || n < 0)
                return bad("the point count");
            for (int i = 0; i < n; ++i) {
                int num = 0;
                std::vector<std::string_view> f;
                Vec3 p;
                float light = 0;
                if (!numbered(lines.next(), num, f) || f.size() < 4 || !toFloat(f[0], p.x) || !toFloat(f[1], p.y) ||
                    !toFloat(f[2], p.z) || !toFloat(f[3], light))
                    return bad("a point");
                out.points.push_back(p);
                out.pointLight.push_back(light);
            }
        } else
            return bad("an unknown line");
        if (!ok)
            return bad("a value");
    }
    if (out.material.empty() || out.points.empty())
        return Error::fail("particles without a material or points");
    return Error::success();
}

} // namespace reluva::sith
