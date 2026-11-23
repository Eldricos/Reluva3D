// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The viewer's page of the .spr sprites and .par particle clouds.

#include "engines/sith/formats/sprite.h"
#include "engines/sith/pages/pages.h"
#include "engines/sith/render/draw3d.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>

namespace reluva::sith {

namespace {

constexpr Color kBackground = {40, 40, 48};
constexpr float kTurn = 3.14159265f / 12;

/**
 * The sprites, then the particle clouds, drawn through a colormap (C steps
 * through them). `.` shows a material's next cel; A/D and W/S turn about a
 * cloud.
 */
class SpritePage : public Page {
public:
    explicit SpritePage(const ViewerData &data) : _data(data), _materials(data.files) {
        _files = filesEnding(data.files, ".spr");
        const std::vector<FileRef> par = filesEnding(data.files, ".par");
        _files.insert(_files.end(), par.begin(), par.end());
        _colormap = std::max(0, data.findColormap("dflt.cmp"));
    }

    const char *name() const override { return "Sprites"; }
    size_t count() const override { return _data.colormaps.empty() ? 0 : _files.size(); }
    const char *keys() const override { return "Left/Right, .: next cel, A/D W/S: turn, C: colormap"; }

    bool handleKey(uint32_t pressed) override {
        if (stepIndex(pressed, _files.size(), _index))
            _cel = 0;
        else if (pressed == '.')
            ++_cel;
        else if (pressed == 'a')
            _yaw -= kTurn;
        else if (pressed == 'd')
            _yaw += kTurn;
        else if (pressed == 'w')
            _pitch = std::min(1.5f, _pitch + kTurn);
        else if (pressed == 's')
            _pitch = std::max(-1.5f, _pitch - kTurn);
        else if (pressed == 'c')
            _colormap = (_colormap + 1) % int(_data.colormaps.size());
        else
            return false;
        return true;
    }

    void draw(Surface &screen) override {
        fillRect(screen, 0, 0, screen.w, screen.h, kBackground);
        const FileRef &file = _files[_index];
        std::string line = std::to_string(_index + 1) + "/" + std::to_string(_files.size()) + "  " +
                           file.first->describe() + ": " + file.second;
        std::vector<uint8_t> bytes;
        if (Error err = file.first->read(file.second, bytes)) {
            drawStatus(screen, line + "  " + err.message);
            return;
        }
        const auto &[cmpName, cmp] = _data.colormaps[size_t(_colormap)];
        const bool cloud = file.second.size() > 4 && file.second.substr(file.second.size() - 4) == ".par";
        Paint paint;
        char info[160];
        if (!cloud) {
            Sprite sprite;
            if (Error err = parseSprite(bytes, sprite)) {
                drawStatus(screen, line + "  " + err.message);
                return;
            }
            paint.material = _materials.find(sprite.material);
            paint.cel = _cel;
            const float size = std::max(sprite.width, sprite.height);
            const soft3d::OrbitCamera camera({0, 0, 0}, 0, 0, size * 2.2f, kViewerW, kViewerH - 16);
            Draw3D draw(screen, camera, cmp);
            draw.billboard({}, sprite.width, sprite.height, paint, std::min(1.0f, 1 + sprite.extraLight));
            std::snprintf(info, sizeof(info), "  %s, %.3f x %.3f, geometry mode %d, offset %.2f %.2f %.2f",
                          sprite.material.c_str(), double(sprite.width), double(sprite.height), sprite.geometryMode,
                          double(sprite.offset.x), double(sprite.offset.y), double(sprite.offset.z));
        } else {
            Particles par;
            if (Error err = parseParticles(bytes, par)) {
                drawStatus(screen, line + "  " + err.message);
                return;
            }
            paint.material = _materials.find(par.material);
            paint.cel = _cel;
            const soft3d::OrbitCamera camera({0, 0, 0}, _yaw, _pitch, std::max(par.radius, 0.01f) * 3, kViewerW,
                                             kViewerH - 16);
            Draw3D draw(screen, camera, cmp);
            for (const Vec3 &p : par.points)
                draw.billboard(p, par.size, par.size, paint);
            std::snprintf(info, sizeof(info), "  %s, %zu points of size %.3f, radius %.3f", par.material.c_str(),
                          par.points.size(), double(par.size), double(par.radius));
        }
        line += info;
        if (paint.material)
            line += ", cel " + std::to_string(_cel % std::max<size_t>(1, paint.material->cels.size()) + 1) + "/" +
                    std::to_string(paint.material->cels.size());
        else
            line += ", its material isn't in the archives";
        drawStatus(screen, line + ", " + cmpName);
    }

private:
    const ViewerData &_data;
    MaterialCache _materials;
    std::vector<FileRef> _files;
    size_t _index = 0;
    int _cel = 0;
    int _colormap = 0;
    float _yaw = 0.6f;
    float _pitch = 0.35f;
};

} // namespace

std::unique_ptr<Page> makeSpritePage(const ViewerData &data) {
    return std::make_unique<SpritePage>(data);
}

} // namespace reluva::sith
