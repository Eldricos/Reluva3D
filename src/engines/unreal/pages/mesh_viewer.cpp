// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's Meshes page: one Mesh or LodMesh at a time, turning
// in front of the camera, with its animations.

#include "engines/unreal/pages/viewers.h"

#include "engines/unreal/formats/texture.h"

#include "graphics/font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace reluva::unreal {

namespace {

constexpr int kFrameW = 960;
constexpr int kFrameH = 720;
constexpr float kPi = 3.14159265358979f;

} // namespace

MeshViewer::MeshViewer(ViewContext &ctx)
    : Page(ctx), _browser(
                     ctx.packages,
                     [&] {
                         // Code packages first: that's where meshes usually are.
                         std::vector<std::string> names = ctx.packages.names(".u");
                         for (std::string &n : ctx.packages.mapNames())
                             names.push_back(std::move(n));
                         for (std::string &n : ctx.packages.names(".utx"))
                             names.push_back(std::move(n));
                         return names;
                     }(),
                     isMeshClass) {
}

MeshViewer::~MeshViewer() {
    deactivate();
}

void MeshViewer::deactivate() {
    unload();
    _renderer = nullptr;
}

void MeshViewer::unload() {
    if (!_renderer)
        return;
    if (_gpu.mesh)
        _renderer->destroyMesh(_gpu.mesh);
    _gpu = GpuMesh{};
    for (gfx::TextureId t : _textures)
        if (t)
            _renderer->destroyTexture(t);
    _textures.clear();
}

void MeshViewer::activate() {
    if (Error err = _ctx.sys.init3D(kFrameW, kFrameH, _renderer)) {
        _renderer = nullptr;
        _ctx.sys.initGraphics(_ctx.screen.w, _ctx.screen.h, PixelFormat::kRGBA32);
        clear(16, 18, 24);
        text(4, 4, "The Meshes page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        setTitle("3D rendering not available");
        return;
    }
    _overlay.create(kFrameW, kFrameH, PixelFormat::kRGBA32);
    if (!_started) {
        _started = true;
        _browser.stepPackage(0);
    }
    show();
}

void MeshViewer::resetView() {
    _yaw = 0;
    _pitch = 0.3f;
    _distance = _gpu.radius * 2.5f;
}

void MeshViewer::show() {
    unload();
    _error.clear();
    _info.clear();
    _anim = -1;
    _time = 0;
    const Package *pkg = _browser.package();
    if (!pkg) {
        _error = "No meshes found";
        return;
    }
    const int index = _browser.exportIndex();
    const std::string name = pkg->path(index + 1);
    setTitle(name + " (" + _browser.where() + ")");
    _info.push_back(pkg->className(index + 1) + " " + name + "  " + _browser.where());
    _choices.clear();
    _choice = -1;
    Error err = readMesh(*pkg, index, _mesh);
    if (err) {
        // The Fallen's shared animations are Meshes without vertices.
        AnimationSet set;
        if (!readAnimationSet(*pkg, index, set)) {
            _info.push_back("A shared animation set: a skeleton and " + std::to_string(set.frames()) +
                            " frames of clips, no vertices of its own.");
            _info.push_back("Characters borrow from it: pick one built on bones, and [/] steps through these too.");
            return;
        }
    }
    if (!err)
        err = uploadMesh(*_renderer, _mesh, _gpu, _ctx.meshTurn, _ctx.meshYawQuarters);
    if (err) {
        _error = err.message;
        return;
    }
    for (const MeshAnimSeq &a : _mesh.anims)
        _choices.push_back(a.name);
    _ownAnims = _choices.size();
    _ownAnimCount = _mesh.anims.size();
    _ownFrames = _mesh.animFrames;
    if (_mesh.bones)
        for (const MeshAnimSeq &a : _mesh.otherAnims)
            if (std::find(_choices.begin(), _choices.end(), a.name) == _choices.end())
                _choices.push_back(a.name);
    // Its own textures.
    for (int32_t ref : _mesh.textures) {
        gfx::TextureId id = 0;
        const ObjectRef tex = _ctx.packages.resolve(*pkg, ref);
        image::Image img;
        if (tex && !decodeTexture(_ctx.packages, *tex.package, tex.exportIndex, img))
            if (_renderer->createTexture(img.w, img.h, img.rgba.data(), {}, id))
                id = 0;
        _textures.push_back(id);
    }
    if (!_mesh.anims.empty()) {
        _anim = 0;
        _choice = 0;
    }
    char line[200];
    std::snprintf(line, sizeof line, "%d vertices x %d frames (%d-byte), %zu triangles, %zu textures, %zu animations",
                  _mesh.frameVerts, _mesh.animFrames, _mesh.vertexBytes, _mesh.tris.size(), _mesh.textures.size(),
                  _mesh.anims.size());
    _info.push_back(line);
    std::snprintf(line, sizeof line, "scale %g %g %g, origin %g %g %g, rotation origin %d %d %d",
                  double(_mesh.scale[0]), double(_mesh.scale[1]), double(_mesh.scale[2]), double(_mesh.origin[0]),
                  double(_mesh.origin[1]), double(_mesh.origin[2]), _mesh.rotOrigin.pitch, _mesh.rotOrigin.yaw,
                  _mesh.rotOrigin.roll);
    _info.push_back(line);
    if (_mesh.importAxes) {
        const auto &a = *_mesh.importAxes;
        std::snprintf(line, sizeof line, "%s (rows %g %g %g / %g %g %g / %g %g %g); drawn %s (M), yaw %d (Y)",
                      _mesh.importAxesGuessed ? "no float copy, usual turn" : "float copy turned", double(a[0][0]),
                      double(a[0][1]), double(a[0][2]), double(a[1][0]), double(a[1][1]), double(a[1][2]),
                      double(a[2][0]), double(a[2][1]), double(a[2][2]), meshTurnName(_ctx.meshTurn),
                      _ctx.meshYawQuarters * 90);
        _info.push_back(line);
    }
    if (_mesh.bones)
        _info.push_back("posed from " + std::to_string(_mesh.bones) + " bones: " +
                        (_mesh.bonesAnimated ? "every frame from their animation keys"
                                             : "the pose they're bound in (their keys don't match the frames)"));
    if (_mesh.keyFrames)
        _info.push_back(std::to_string(_mesh.keyFrames) + " of its " + std::to_string(_mesh.animFrames) +
                        " frames of vertices found (keyframes not worked out yet): the first is shown");
    if (_mesh.trailingBytes)
        _info.push_back(std::to_string(_mesh.trailingBytes) + " bytes after the last field read");
    resetView();
}

// Picks one of the animations to step through, borrowing it from the
// shared sets (and uploading the mesh again with its frames) if need be.
void MeshViewer::pick(int choice) {
    _choice = choice;
    const std::string &name = _choices[size_t(choice)];
    if (!_mesh.findAnim(name)) {
        // Only the mesh's own frames and the one borrowed now are kept.
        _mesh.anims.resize(_ownAnimCount);
        _mesh.animFrames = _ownFrames;
        _mesh.verts.resize(size_t(_ownFrames) * size_t(_mesh.frameVerts));
        const Package *pkg = _browser.package();
        if (!pkg || !borrowNamedAnimation(*pkg, _browser.exportIndex(), _ctx.sets(), name, _mesh)) {
            _anim = -1;
            return;
        }
        if (_gpu.mesh)
            _renderer->destroyMesh(_gpu.mesh);
        const float radius = _gpu.radius;
        if (Error err = uploadMesh(*_renderer, _mesh, _gpu, _ctx.meshTurn, _ctx.meshYawQuarters)) {
            _error = err.message;
            _anim = -1;
            return;
        }
        _gpu.radius = radius; // keep the view where it was
    }
    _anim = int(_mesh.findAnim(name) - _mesh.anims.data());
}

void MeshViewer::handleEvent(const Event &ev) {
    if (!_renderer)
        return;
    if (ev.type == EventType::kKeyDown && !ev.repeat) {
        const int anims = int(_choices.size());
        if (ev.key == key::kLeft || ev.key == key::kRight) {
            _browser.step(ev.key == key::kLeft ? -1 : 1);
            show();
        } else if (ev.key == key::kPageUp || ev.key == key::kPageDown) {
            _browser.stepPackage(ev.key == key::kPageUp ? -1 : 1);
            show();
        } else if (ev.key == 'a') {
            _playing = !_playing;
        } else if ((ev.key == '[' || ev.key == ']') && anims > 0) {
            pick((std::max(_choice, 0) + (ev.key == '[' ? anims - 1 : 1)) % anims);
            _time = 0;
        } else if (ev.key == 'r') {
            resetView();
        } else if (ev.key == 'i') {
            _showInfo = !_showInfo;
        } else if (ev.key == 'm' || ev.key == 'y') {
            if (ev.key == 'm')
                _ctx.meshTurn = MeshTurn((int(_ctx.meshTurn) + 1) % 3);
            else
                _ctx.meshYawQuarters = (_ctx.meshYawQuarters + 1) % 4;
            const float yaw = _yaw;
            const float pitch = _pitch;
            const float distance = _distance;
            show();
            _yaw = yaw;
            _pitch = pitch;
            _distance = distance;
        }
    } else if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft) {
        _dragging = true;
        _lastX = ev.mouseX;
        _lastY = ev.mouseY;
    } else if (ev.type == EventType::kMouseUp && ev.button == MouseButton::kLeft) {
        _dragging = false;
    } else if (ev.type == EventType::kMouseMove && _dragging) {
        _yaw += float(ev.mouseX - _lastX) * 0.01f;
        _pitch = std::clamp(_pitch + float(ev.mouseY - _lastY) * 0.01f, -1.5f, 1.5f);
        _lastX = ev.mouseX;
        _lastY = ev.mouseY;
    } else if (ev.type == EventType::kMouseWheel) {
        _distance = std::clamp(_distance * (ev.wheelY > 0 ? 0.9f : 1.1f), 4.0f, 20000.0f);
    }
}

void MeshViewer::update(uint64_t nowMs) {
    if (!_renderer)
        return;
    const float dt = _lastMs ? std::min(0.1f, float(nowMs - _lastMs) / 1000.0f) : 0.0f;
    _lastMs = nowMs;
    if (_playing)
        _time += dt;
    // Orbiting the mesh's middle, in the renderer's axes (Y negated).
    const gfx::Vec3 target{_gpu.center[0], -_gpu.center[1], _gpu.center[2]};
    const gfx::Vec3 eye{target[0] + _distance * std::cos(_pitch) * std::cos(_yaw),
                        target[1] + _distance * std::cos(_pitch) * std::sin(_yaw),
                        target[2] + _distance * std::sin(_pitch)};
    const gfx::Vec3 forward{target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    const float aspect = float(kFrameW) / float(kFrameH);
    const gfx::Mat4 viewProj =
        gfx::multiply(gfx::perspective(kPi / 3, aspect, 1, 65536), gfx::lookAlong(eye, forward, {0, 0, 1}));
    _renderer->beginFrame(viewProj, eye, {40, 44, 56});
    if (_gpu.mesh) {
        MeshLight light;
        light.ambient = {0.45f, 0.45f, 0.45f};
        light.directed = {0.6f, 0.6f, 0.6f};
        light.direction = {0.4f, -0.3f, 0.87f};
        const MeshAnimSeq *anim = _anim >= 0 ? &_mesh.anims[size_t(_anim)] : nullptr;
        drawMesh(*_renderer, _gpu, meshPlacement({}, {}, 1, {}), poseAt(_mesh, anim, _time), _textures, light, 1);
    }

    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), 0);
    std::vector<std::string> lines;
    if (_showInfo) {
        lines = _info;
        if (_anim >= 0) {
            const MeshAnimSeq &a = _mesh.anims[size_t(_anim)];
            char line[200];
            std::snprintf(line, sizeof line, "animation %d/%zu: %s (%s), frames %d-%d at %g/s%s%s", _choice + 1,
                          _choices.size(), a.name.c_str(), a.group.c_str(), a.startFrame,
                          a.startFrame + a.numFrames - 1, double(a.rate),
                          size_t(_choice) >= _ownAnims ? ", from a shared set" : "", _playing ? "" : ", stopped");
            lines.push_back(line);
        } else if (_choice >= 0 && size_t(_choice) < _choices.size()) {
            lines.push_back("animation " + std::to_string(_choice + 1) + "/" + std::to_string(_choices.size()) + ": " +
                            _choices[size_t(_choice)] + " isn't in any shared animation set");
        }
    }
    if (!_error.empty())
        lines.push_back(_error);
    for (size_t i = 0; i < lines.size(); ++i)
        font::drawText(_overlay, 4, 3 + int(i) * font::kLineHeight, lines[i],
                       !_error.empty() && i + 1 == lines.size() ? Color{255, 120, 120} : Color{230, 230, 230});
    _renderer->endFrame(&_overlay);
}

} // namespace reluva::unreal
