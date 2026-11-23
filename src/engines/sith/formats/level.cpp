// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/formats/level.h"

#include "common/fs.h"

#include <string>

namespace reluva::sith {

namespace {

class LevelReader {
public:
    LevelReader(const std::vector<uint8_t> &data, Level &out) : _lines(data), _out(out) {}

    Error read() {
        while (!_lines.done()) {
            const std::string_view line = _lines.next();
            if (startsWith(line, "SECTION:")) {
                _section = fs::toLower(after(line, "SECTION:"));
                _sector = nullptr;
                continue;
            }
            if (startsWith(line, "end"))
                continue;
            Error err;
            if (_section == "header")
                err = header(line);
            else if (_section == "georesource" || startsWith(line, "World"))
                err = world(line);
            else if (_section == "sectors")
                err = sector(line);
            else if (_section == "templates")
                err = templ(line);
            // The copyright's picture, and the JK section's comments only.
            if (err)
                return err;
        }
        for (const Level::Surface &s : _out.surfaces)
            if (s.adjoin >= int(_out.adjoins.size()) || s.material >= int(_out.materials.size()))
                return Error::fail("level surface names an adjoin or material it hasn't");
        for (const Level::Sector &s : _out.sectors)
            if (s.firstSurface < 0 || s.firstSurface + s.surfaceCount > int(_out.surfaces.size()))
                return Error::fail("level sector's surfaces run past the level's");
        return Error::success();
    }

private:
    Error bad(const std::string &what) const {
        return Error::fail("level line " + std::to_string(_lines.lineNumber()) + ": can't read " + what);
    }

    static bool floats(const std::vector<std::string_view> &w, size_t at, float *out, size_t n) {
        if (w.size() < at + n)
            return false;
        for (size_t i = 0; i < n; ++i)
            if (!toFloat(w[at + i], out[i]))
                return false;
        return true;
    }

    static bool vec(const std::vector<std::string_view> &w, size_t at, Vec3 &v) {
        float f[3];
        if (!floats(w, at, f, 3))
            return false;
        v = {f[0], f[1], f[2]};
        return true;
    }

    // The value words after a header line's name: it's the last of them.
    Error header(std::string_view line) {
        const std::vector<std::string_view> w = words(line);
        Level::Header &h = _out.header;
        auto last = [&](size_t n, float *out) { return w.size() >= n && floats(w, w.size() - n, out, n); };
        bool ok = true;
        if (startsWith(line, "Version"))
            ok = w.size() > 1 && toInt(w[1], h.version);
        else if (startsWith(line, "World"))
            ok = last(1, &h.gravity);
        else if (startsWith(line, "Ceiling Sky Z"))
            ok = last(1, &h.ceilingSkyZ);
        else if (startsWith(line, "Ceiling Sky Offset"))
            ok = last(2, h.ceilingSkyOffset.data());
        else if (startsWith(line, "Horizon Distance"))
            ok = last(1, &h.horizonDistance);
        else if (startsWith(line, "Horizon Pixels"))
            ok = last(1, &h.horizonPixelsPerRev);
        else if (startsWith(line, "Horizon Sky Offset"))
            ok = last(2, h.horizonSkyOffset.data());
        else if (startsWith(line, "MipMap"))
            ok = last(4, h.mipmapDistances.data());
        else if (startsWith(line, "LOD"))
            ok = last(4, h.lodDistances.data());
        else if (startsWith(line, "Perspective"))
            ok = last(1, &h.perspectiveDistance);
        else if (startsWith(line, "Gouraud"))
            ok = last(1, &h.gouraudDistance);
        else
            return bad("a header line");
        return ok ? Error::success() : bad("a header value");
    }

    // The fields of the next of a list's numbered lines. One line of
    // static.jkl has "5;" for "5:".
    bool item(std::vector<std::string_view> &f) {
        int num = 0;
        const std::string_view line = _lines.next();
        if (numbered(line, num, f))
            return true;
        f = words(line);
        if (f.empty() || f[0].back() != ';' || !toInt(f[0].substr(0, f[0].size() - 1), num))
            return false;
        f.erase(f.begin());
        return true;
    }

    Error world(std::string_view line) {
        if (!startsWith(line, "World"))
            return bad("a line outside a list");
        const std::vector<std::string_view> w = words(line);
        int n = 0;
        if (w.size() < 3 || !toInt(w.back(), n) || n < 0)
            return bad("a list's count");
        std::string what;
        for (size_t i = 1; i + 1 < w.size(); ++i)
            what += (i > 1 ? " " : "") + fs::toLower(w[i]);
        std::vector<std::string_view> f;
        if (what == "sectors")
            return Error::success(); // its sectors follow, each from its SECTOR line
        if (what == "sounds") {
            // The only list not numbered.
            for (int i = 0; i < n && !startsWith(_lines.peek(), "end"); ++i)
                _out.sounds.emplace_back(words(_lines.next()).at(0));
            return Error::success();
        }
        std::vector<std::string> *names = what == "aiclasses"      ? &_out.aiClasses
                                          : what == "models"       ? &_out.models
                                          : what == "sprites"      ? &_out.sprites
                                          : what == "keyframes"    ? &_out.keyframes
                                          : what == "puppets"      ? &_out.puppets
                                          : what == "soundclasses" ? &_out.soundClasses
                                          : what == "scripts"      ? &_out.scripts
                                          : what == "colormaps"    ? &_out.colormaps
                                                                   : nullptr;
        // The count is room for that many: a list may end sooner, at "end".
        for (int i = 0; i < n && !startsWith(_lines.peek(), "end"); ++i) {
            if (names) {
                if (!item(f) || f.empty())
                    return bad(what);
                names->emplace_back(f[0]);
            } else if (what == "materials") {
                Level::MaterialRef m;
                if (!item(f) || f.size() < 3 || !toFloat(f[1], m.xTile) || !toFloat(f[2], m.yTile))
                    return bad("a material");
                m.name = f[0];
                _out.materials.push_back(m);
            } else if (what == "vertices") {
                Vec3 v;
                if (!item(f) || !vec(f, 0, v))
                    return bad("a vertex");
                _out.vertices.push_back(v);
            } else if (what == "texture vertices") {
                std::array<float, 2> uv{};
                if (!item(f) || !floats(f, 0, uv.data(), 2))
                    return bad("a texture vertex");
                _out.texVertices.push_back(uv);
            } else if (what == "adjoins") {
                Level::Adjoin a;
                if (!item(f) || f.size() < 3 || !toFlags(f[0], a.flags) || !toInt(f[1], a.mirror) ||
                    !toFloat(f[2], a.distance))
                    return bad("an adjoin");
                _out.adjoins.push_back(a);
            } else if (what == "surfaces") {
                if (Error err = surface())
                    return err;
            } else if (what == "cogs") {
                if (!item(f) || f.empty())
                    return bad("a cog");
                Level::Cog cog;
                cog.script = f[0];
                cog.values.assign(f.begin() + 1, f.end());
                _out.cogs.push_back(std::move(cog));
            } else if (what == "templates") {
                if (Error err = templ(_lines.next()))
                    return err;
            } else if (what == "things") {
                if (Error err = thing())
                    return err;
            } else
                return bad("an unknown list, " + what);
        }
        if (what == "surfaces") {
            // A normal for each surface follows, without a heading.
            for (Level::Surface &s : _out.surfaces)
                if (!item(f) || !vec(f, 0, s.normal))
                    return bad("a surface normal");
        }
        return Error::success();
    }

    Error surface() {
        std::vector<std::string_view> f;
        Level::Surface s;
        int corners = 0;
        if (!item(f) || f.size() < 9 || !toInt(f[0], s.material) || !toFlags(f[1], s.surfaceFlags) ||
            !toFlags(f[2], s.faceFlags) || !toInt(f[3], s.geometryMode) || !toInt(f[4], s.lightingMode) ||
            !toInt(f[5], s.textureMode) || !toInt(f[6], s.adjoin) || !toFloat(f[7], s.extraLight) ||
            !toInt(f[8], corners) || corners < 0 || f.size() < 9 + size_t(corners) * 2)
            return bad("a surface");
        // Each corner is "vertex,texture vertex", then each corner's light.
        for (int c = 0; c < corners; ++c) {
            const std::string_view pair = f[9 + size_t(c)];
            const size_t comma = pair.find(',');
            int v = 0;
            int t = 0;
            if (comma == std::string_view::npos || !toInt(pair.substr(0, comma), v) ||
                !toInt(pair.substr(comma + 1), t) || v < 0 || v >= int(_out.vertices.size()) ||
                t >= int(_out.texVertices.size()))
                return bad("a surface's corner");
            s.vertices.push_back(v);
            s.texVertices.push_back(t);
            float light = 0;
            if (!toFloat(f[9 + size_t(corners + c)], light))
                return bad("a surface's light");
            s.intensities.push_back(light);
        }
        _out.surfaces.push_back(std::move(s));
        return Error::success();
    }

    Error sector(std::string_view line) {
        const std::vector<std::string_view> w = words(line);
        if (startsWith(line, "SECTOR")) {
            _sector = &_out.sectors.emplace_back();
            return Error::success();
        }
        if (!_sector)
            return bad("a line outside a sector");
        Level::Sector &s = *_sector;
        bool ok = true;
        float f[6];
        if (startsWith(line, "FLAGS"))
            ok = w.size() > 1 && toFlags(w[1], s.flags);
        else if (startsWith(line, "AMBIENT"))
            ok = w.size() > 2 && toFloat(w[2], s.ambientLight);
        else if (startsWith(line, "EXTRA"))
            ok = w.size() > 2 && toFloat(w[2], s.extraLight);
        else if (startsWith(line, "COLORMAP"))
            ok = w.size() > 1 && toInt(w[1], s.colormap);
        else if (startsWith(line, "TINT"))
            ok = vec(w, 1, s.tint);
        else if (startsWith(line, "BOUNDBOX"))
            ok = vec(w, 1, s.bounds[0]) && vec(w, 4, s.bounds[1]);
        else if (startsWith(line, "COLLIDEBOX")) {
            ok = floats(w, 1, f, 6);
            if (ok)
                s.collideBox = std::array<Vec3, 2>{Vec3{f[0], f[1], f[2]}, Vec3{f[3], f[4], f[5]}};
        } else if (startsWith(line, "CENTER"))
            ok = vec(w, 1, s.center);
        else if (startsWith(line, "RADIUS"))
            ok = w.size() > 1 && toFloat(w[1], s.radius);
        else if (startsWith(line, "SOUND")) {
            ok = w.size() > 2 && toFloat(w[2], s.soundVolume);
            if (ok)
                s.sound = w[1];
        } else if (startsWith(line, "VERTICES")) {
            int n = 0;
            if (w.size() < 2 || !toInt(w[1], n) || n < 0)
                return bad("a sector's vertex count");
            for (int i = 0; i < n; ++i) {
                std::vector<std::string_view> v;
                int index = 0;
                if (!item(v) || v.empty() || !toInt(v[0], index) || index < 0 || index >= int(_out.vertices.size()))
                    return bad("a sector's vertex");
                s.vertices.push_back(index);
            }
        } else if (startsWith(line, "SURFACES"))
            ok = w.size() > 2 && toInt(w[1], s.firstSurface) && toInt(w[2], s.surfaceCount);
        else if (startsWith(line, "World"))
            return Error::success();
        else
            return bad("a sector line");
        return ok ? Error::success() : bad("a sector value");
    }

    // A template: its name, its base, then its settings; the header line
    // and the list's count come before them.
    Error templ(std::string_view line) {
        if (startsWith(line, "World"))
            return world(line);
        const std::vector<std::string_view> w = words(line);
        if (w.size() < 2)
            return bad("a template");
        Level::Template t;
        t.name = w[0];
        t.base = w[1];
        t.params.assign(w.begin() + 2, w.end());
        _out.templates.push_back(std::move(t));
        return Error::success();
    }

    Error thing() {
        std::vector<std::string_view> f;
        Level::Thing t;
        if (!item(f) || f.size() < 9 || !vec(f, 2, t.position) || !vec(f, 5, t.rotation) || !toInt(f[8], t.sector))
            return bad("a thing");
        t.templateName = f[0];
        t.name = f[1];
        t.params.assign(f.begin() + 9, f.end());
        _out.things.push_back(std::move(t));
        return Error::success();
    }

    TextLines _lines;
    Level &_out;
    std::string _section;
    Level::Sector *_sector = nullptr;
};

// The value of `key` among "key=value" words, or nothing.
std::optional<std::string> param(const std::vector<std::string> &params, const std::string &key) {
    for (const std::string &p : params) {
        const size_t eq = p.find('=');
        if (eq != std::string::npos && fs::toLower(p.substr(0, eq)) == key)
            return p.substr(eq + 1);
    }
    return std::nullopt;
}

} // namespace

const Level::Template *Level::findTemplate(const std::string &name) const {
    const std::string want = fs::toLower(name);
    for (const Template &t : templates)
        if (fs::toLower(t.name) == want)
            return &t;
    return nullptr;
}

std::string Level::setting(const Thing &thing, const std::string &key) const {
    const std::string want = fs::toLower(key);
    if (auto v = param(thing.params, want))
        return *v;
    const Template *t = findTemplate(thing.templateName);
    // A template's bases go back a few steps: none goes round in a circle,
    // but the count keeps a bad file from doing so.
    for (int depth = 0; t && depth < 32; ++depth) {
        if (auto v = param(t->params, want))
            return *v;
        t = fs::toLower(t->base) == "none" ? nullptr : findTemplate(t->base);
    }
    return "";
}

Error parseLevel(const std::vector<uint8_t> &data, Level &out) {
    out = Level{};
    return LevelReader(data, out).read();
}

} // namespace reluva::sith
