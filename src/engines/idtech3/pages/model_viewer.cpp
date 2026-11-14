// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's Models page: every .md3, .mdr and Ghoul2 .glm in the game
// drawn on the GPU with its shaders, players put together from their three
// parts, Ghoul2 models with their skeleton's animations.

#include "engines/idtech3/pages/model_viewer.h"

#include "graphics/font.h"
#include "common/fs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <set>

namespace reluva::idtech3 {

namespace {

constexpr int kFrameW = 960;
constexpr int kFrameH = 720;
constexpr float kPi = 3.14159265358979f;
constexpr float kAnimationFps = 15;

bool endsWith(const std::string &s, const char *suffix) {
    const size_t n = std::char_traits<char>::length(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

std::string folderOf(const std::string &path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

} // namespace

ModelViewer::ModelViewer(PageContext &ctx) : Page(ctx) {
    std::set<std::string> names;
    for (const std::string &n : ctx.files.list())
        names.insert(normalizeArchiveName(n));
    std::set<std::string> players;
    // A player part in either format (Elite Force's bodies are .mdr).
    auto has = [&](const std::string &dir, const char *part) {
        return names.count(dir + "/" + part + ".md3") || names.count(dir + "/" + part + ".mdr");
    };
    for (const std::string &n : names) {
        if (endsWith(n, ".glm")) {
            _entries.push_back({n, false});
            continue;
        }
        if (!endsWith(n, ".md3") && !endsWith(n, ".mdr"))
            continue;
        // Lower-detail copies of a model.
        if (endsWith(n, "_1.md3") || endsWith(n, "_2.md3"))
            continue;
        const std::string dir = folderOf(n);
        bool part = false;
        for (const char *p : {"lower", "upper", "head"})
            part = part || endsWith(n, (std::string("/") + p + ".md3").c_str()) ||
                   endsWith(n, (std::string("/") + p + ".mdr").c_str());
        if (part && has(dir, "lower") && has(dir, "upper") && has(dir, "head")) {
            if (players.insert(dir).second)
                _entries.push_back({dir, true});
            continue;
        }
        _entries.push_back({n, false});
    }
}

ModelViewer::~ModelViewer() {
    deactivate();
}

void ModelViewer::deactivate() {
    _models.reset();
    _shaders.reset();
    _renderer = nullptr;
}

void ModelViewer::activate() {
    if (Error err = _ctx.sys.init3D(kFrameW, kFrameH, _renderer)) {
        _renderer = nullptr;
        _ctx.sys.initGraphics(_ctx.screen.w, _ctx.screen.h, PixelFormat::kRGBA32);
        clear(16, 18, 24);
        text(4, 4, "The Models page needs 3D rendering, which isn't available:", {255, 120, 120});
        text(4, 4 + font::kLineHeight, err.message);
        setTitle("3D rendering not available");
        return;
    }
    _overlay.create(kFrameW, kFrameH, PixelFormat::kRGBA32);
    _shaders = std::make_unique<ShaderCache>(*_renderer, _ctx.files, _ctx.shaders);
    _models = std::make_unique<ModelCache>(*_renderer, *_shaders, _ctx.files);
    applyVideoSettings(videoSettings(_ctx.cvars), *_shaders, nullptr, _models.get());
    show(_index);
}

void ModelViewer::show(int index) {
    _index = index;
    const Entry &e = _entries[size_t(index)];
    setTitle(e.name + " (" + std::to_string(index + 1) + "/" + std::to_string(_entries.size()) + ")");
    _models->clear();
    _shaders->clear();
    _info.clear();
    _error.clear();
    _model = _lower = _upper = _head = -1;
    _skins[0] = _skins[1] = _skins[2] = nullptr;
    _frame = 0;
    _startMs = _ctx.sys.getMillis();
    _anims.clear();
    _animTime = 0;

    std::vector<const Md3Model *> parts;
    if (e.player) {
        // Each part as .mdr if there is one (Elite Force's), else .md3.
        auto part = [&](const char *name) {
            const std::string mdr = e.name + "/" + name + ".mdr";
            return _models->load(_ctx.files.has(mdr) ? mdr : e.name + "/" + name + ".md3", &_error);
        };
        _lower = part("lower");
        _upper = part("upper");
        _head = part("head");
        if (_lower < 0 || _upper < 0 || _head < 0) {
            _info.push_back(e.name);
            return;
        }
        // Its animations: played in pairs, legs and torso, as the game does.
        std::vector<uint8_t> cfg;
        if (Error err = _ctx.files.read(e.name + "/animation.cfg", cfg); !err) {
            _anims = parseAnimationConfig(std::string(cfg.begin(), cfg.end()));
            _legsAnim = _torsoAnim = -1;
            for (size_t i = 0; i < _anims.size(); ++i) {
                if (_anims[i].name == "LEGS_IDLE" || (_legsAnim < 0 && _anims[i].part == PlayerAnimation::Part::kLegs))
                    _legsAnim = int(i);
                if (_anims[i].name == "TORSO_STAND" ||
                    (_torsoAnim < 0 && _anims[i].part == PlayerAnimation::Part::kTorso))
                    _torsoAnim = int(i);
            }
            if (_legsAnim < 0 || _torsoAnim < 0)
                _anims.clear();
        }
        _skins[0] = &_models->skin(e.name + "/lower_default.skin");
        _skins[1] = &_models->skin(e.name + "/upper_default.skin");
        _skins[2] = &_models->skin(e.name + "/head_default.skin");
        parts = {&_models->model(_lower), &_models->model(_upper), &_models->model(_head)};
    } else {
        _model = _models->load(e.name, &_error);
        if (_model < 0) {
            _info.push_back(e.name);
            return;
        }
        parts = {&_models->model(_model)};
        // A Ghoul2 model: its folder's default skin, and the animations
        // beside its skeleton, each played on the whole of it.
        if (const GlmModel *glm = _models->ghoul2(_model)) {
            const std::string skin = folderOf(e.name) + "/model_default.skin";
            if (_ctx.files.has(skin))
                _skins[0] = &_models->skin(skin);
            std::vector<uint8_t> cfg;
            if (!_ctx.files.read(folderOf(glm->animName) + "/animation.cfg", cfg))
                _anims = parseGhoul2AnimationConfig(std::string(cfg.begin(), cfg.end()));
            _legsAnim = _torsoAnim = 0;
            for (size_t i = 0; i < _anims.size(); ++i)
                if (_anims[i].name == "BOTH_STAND1")
                    _legsAnim = _torsoAnim = int(i);
        }
    }

    // Frame the first frame's bounds (a player stands about 60 units
    // above its legs' origin).
    const Md3Frame &f = parts[0]->frames.front();
    for (int i = 0; i < 3; ++i)
        _center[size_t(i)] = (f.mins[size_t(i)] + f.maxs[size_t(i)]) / 2;
    _radius = std::max(8.0f, f.radius);
    if (e.player) {
        _center[2] += 16;
        _radius = std::max(_radius, 40.0f);
    }
    resetView();

    _info.push_back(e.name);
    for (size_t pi = 0; pi < parts.size(); ++pi) {
        const Md3Model *m = parts[pi];
        size_t verts = 0;
        size_t tris = 0;
        std::set<std::string> shaderNames;
        for (const Md3Surface &s : m->surfaces) {
            verts += size_t(s.vertexCount);
            tris += s.indices.size() / 3;
            for (const std::string &sh : s.shaders)
                shaderNames.insert(sh);
        }
        std::string tags;
        for (int t = 0; t < m->tagCount; ++t)
            tags += (t ? " " : "") + m->tags[size_t(t)].name;
        char line[256];
        std::snprintf(line, sizeof line, "%s: %zu frames, %zu surfaces, %zu vertices, %zu triangles%s%s",
                      m->name.empty() ? "model" : m->name.c_str(), m->frames.size(), m->surfaces.size(), verts, tris,
                      tags.empty() ? "" : ", tags: ", tags.c_str());
        _info.push_back(line);
        // Each surface's shader (from the skin if it names one) and whether
        // it, or the image it needs, is there.
        for (const Md3Surface &s : m->surfaces) {
            std::string shader = s.shaders.empty() ? std::string() : s.shaders.front();
            if (const Skin *skin = _skins[pi])
                if (auto it = skin->find(fs::toLower(s.name)); it != skin->end())
                    shader = it->second;
            bool found = !shader.empty();
            if (const ShaderDef *def = _ctx.shaders.find(shader)) {
                for (const ShaderStage &st : def->stages)
                    for (const std::string &img : st.maps)
                        if (!img.empty() && img[0] != '$' && findImage(_ctx.files, img).empty())
                            found = false;
            } else if (found) {
                found = !findImage(_ctx.files, shader).empty();
            }
            _info.push_back("  " + s.name + ": " + (shader.empty() ? std::string("(no shader)") : shader) +
                            (found ? "" : "  [MISSING]"));
        }
    }
}

void ModelViewer::resetView() {
    _yaw = kPi * 0.85f;
    _pitch = 0.25f;
    _distance = _radius * 2.6f;
}

void ModelViewer::handleEvent(const Event &ev) {
    if (!_renderer || _entries.empty())
        return;
    if (ev.type == EventType::kKeyDown) {
        const int n = static_cast<int>(_entries.size());
        int i = _index;
        if (ev.key == key::kLeft)
            i = (i + n - 1) % n;
        else if (ev.key == key::kRight)
            i = (i + 1) % n;
        else if (ev.key == key::kPageUp)
            i = std::max(0, i - 10);
        else if (ev.key == key::kPageDown)
            i = std::min(n - 1, i + 10);
        else if (ev.key == key::kHome)
            i = 0;
        else if (ev.key == key::kEnd)
            i = n - 1;
        else if (ev.key == 'a')
            _animate = !_animate;
        else if (ev.key == 't')
            _torsoOnTag = !_torsoOnTag;
        else if ((ev.key == '[' || ev.key == ']') && !_anims.empty())
            nextAnimation(ev.key == ']' ? 1 : -1);
        else if (ev.key == ',')
            --_frame;
        else if (ev.key == '.')
            ++_frame;
        else if (ev.key == 'r')
            resetView();
        else if (ev.key == 'i')
            _showInfo = !_showInfo;
        else if (ev.key == '+' || ev.key == '=')
            _distance = std::max(_radius * 0.3f, _distance * 0.9f);
        else if (ev.key == '-')
            _distance /= 0.9f;
        if (i != _index)
            show(i);
    } else if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft) {
        _dragging = true;
        _lastX = ev.mouseX;
        _lastY = ev.mouseY;
    } else if (ev.type == EventType::kMouseUp && ev.button == MouseButton::kLeft) {
        _dragging = false;
    } else if (ev.type == EventType::kMouseMove && _dragging) {
        _yaw -= float(ev.mouseX - _lastX) * 0.01f;
        _pitch = std::clamp(_pitch + float(ev.mouseY - _lastY) * 0.01f, -1.5f, 1.5f);
        _lastX = ev.mouseX;
        _lastY = ev.mouseY;
    } else if (ev.type == EventType::kMouseWheel) {
        _distance = std::max(_radius * 0.3f, _distance * std::pow(0.9f, float(ev.wheelY)));
    }
}

void ModelViewer::nextAnimation(int step) {
    // A Ghoul2 model's: each in turn on the whole skeleton.
    if (_model >= 0) {
        const int n = int(_anims.size());
        _legsAnim = _torsoAnim = ((_torsoAnim + step) % n + n) % n;
        _animTime = 0;
        _animate = true;
        return;
    }
    // Through every animation: one for the whole body plays on both
    // halves; a torso or legs one on its half, the other going back to
    // standing or idling if it was playing one for the whole body.
    const int n = int(_anims.size());
    const int current = step > 0 ? std::max(_legsAnim, _torsoAnim) : std::min(_legsAnim, _torsoAnim);
    const int i = ((current + step) % n + n) % n;
    auto find = [&](const char *name, PlayerAnimation::Part part) {
        for (size_t k = 0; k < _anims.size(); ++k)
            if (_anims[k].name == name)
                return int(k);
        for (size_t k = 0; k < _anims.size(); ++k)
            if (_anims[k].part == part)
                return int(k);
        return 0;
    };
    switch (_anims[size_t(i)].part) {
    case PlayerAnimation::Part::kBoth:
        _legsAnim = _torsoAnim = i;
        break;
    case PlayerAnimation::Part::kTorso:
        _torsoAnim = i;
        if (_anims[size_t(_legsAnim)].part != PlayerAnimation::Part::kLegs)
            _legsAnim = find("LEGS_IDLE", PlayerAnimation::Part::kLegs);
        break;
    case PlayerAnimation::Part::kLegs:
        _legsAnim = i;
        if (_anims[size_t(_torsoAnim)].part != PlayerAnimation::Part::kTorso)
            _torsoAnim = find("TORSO_STAND", PlayerAnimation::Part::kTorso);
        break;
    }
    _animTime = 0;
    _animate = true;
}

void ModelViewer::update(uint64_t nowMs) {
    if (!_renderer)
        return;
    const float time = float(nowMs - _startMs) / 1000.0f;
    if (_animate && _lastMs != 0)
        _animTime += std::min(float(nowMs - _lastMs), 100.0f) / 1000.0f;
    _lastMs = nowMs;
    const int frame = _animate ? int(time * kAnimationFps) : _frame;

    const gfx::Vec3 offset{std::cos(_pitch) * std::cos(_yaw), std::cos(_pitch) * std::sin(_yaw), std::sin(_pitch)};
    const gfx::Vec3 eye{_center[0] + offset[0] * _distance, _center[1] + offset[1] * _distance,
                        _center[2] + offset[2] * _distance};
    const float aspect = float(kFrameW) / float(kFrameH);
    const gfx::Mat4 viewProj = gfx::multiply(gfx::perspective(0.9f, aspect, 1, 8192),
                                             gfx::lookAlong(eye, {-offset[0], -offset[1], -offset[2]}, {0, 0, 1}));
    _renderer->setGamma(brightness(_ctx));
    _renderer->beginFrame(viewProj, eye, {70, 74, 82});

    // No map, so no light grid: a fixed light, as the original uses then.
    const PointLight light = LightGrid{}.sample({0, 0, 0});
    std::string frames;
    if (_model >= 0) {
        const int n = int(_models->model(_model).frames.size());
        int shown = ((frame % n) + n) % n;
        int next = shown;
        float lerp = 0;
        if (!_anims.empty()) {
            const PlayerAnimation &a = _anims[size_t(_torsoAnim)];
            a.frameAt(_animate ? _animTime : float(_frame) / a.fps, shown, next, lerp);
            shown = std::clamp(shown, 0, n - 1);
            next = std::clamp(next, 0, n - 1);
        }
        _models->draw(_model, next, gfx::identity(), light, time, _skins[0], 0, 1e9f, {}, nullptr, shown, 1 - lerp);
        frames = "frame " + std::to_string(shown + 1) + "/" + std::to_string(n) +
                 (_anims.empty() ? std::string() : "; " + _anims[size_t(_torsoAnim)].name + " ([ ]: animation)");
    } else if (_lower >= 0) {
        // Legs at the origin, the torso on the legs' tag_torso, the head
        // on the torso's tag_head.
        const Md3Model &legs = _models->model(_lower);
        const Md3Model &torso = _models->model(_upper);
        const int nl = int(legs.frames.size());
        const int nt = int(torso.frames.size());
        // Each half's frame, the next and how far towards it: from its
        // animation (animation.cfg) if the player has them, else frame by
        // frame through the whole model.
        int fl = ((frame % nl) + nl) % nl;
        int ft = ((frame % nt) + nt) % nt;
        int nextL = fl;
        int nextT = ft;
        float lerpL = 0;
        float lerpT = 0;
        if (!_anims.empty()) {
            _anims[size_t(_legsAnim)].frameAt(_animTime, fl, nextL, lerpL);
            _anims[size_t(_torsoAnim)].frameAt(_animTime, ft, nextT, lerpT);
            fl = std::clamp(fl, 0, nl - 1), nextL = std::clamp(nextL, 0, nl - 1);
            ft = std::clamp(ft, 0, nt - 1), nextT = std::clamp(nextT, 0, nt - 1);
        }
        // A tag between two frames.
        auto tagBetween = [](const Md3Model &m, int a, int b, float f, const char *name) -> std::optional<Md3Tag> {
            const Md3Tag *ta = m.tag(a, name);
            const Md3Tag *tb = m.tag(b, name);
            if (!ta || !tb)
                return std::nullopt;
            Md3Tag t = *ta;
            for (size_t k = 0; k < 3; ++k) {
                t.origin[k] += (tb->origin[k] - ta->origin[k]) * f;
                for (size_t j = 0; j < 3; ++j)
                    t.axis[k][j] += (tb->axis[k][j] - ta->axis[k][j]) * f;
            }
            return t;
        };
        const gfx::Mat4 base = gfx::identity();
        _models->draw(_lower, nextL, base, light, time, _skins[0], 0, 1e9f, {}, nullptr, fl, 1 - lerpL);
        // (With T, the torso where its own file has it instead, to compare:
        // skeletal models' halves may already fit together.)
        gfx::Mat4 torsoAt = base;
        if (const auto t = tagBetween(legs, fl, nextL, lerpL, "tag_torso"); t && _torsoOnTag)
            torsoAt = gfx::multiply(base, tagPlacement(*t));
        _models->draw(_upper, nextT, torsoAt, light, time, _skins[1], 0, 1e9f, {}, nullptr, ft, 1 - lerpT);
        gfx::Mat4 headAt = torsoAt;
        if (const auto t = tagBetween(torso, ft, nextT, lerpT, "tag_head"))
            headAt = gfx::multiply(torsoAt, tagPlacement(*t));
        _models->draw(_head, 0, headAt, light, time, _skins[2]);
        frames = "legs frame " + std::to_string(fl + 1) + "/" + std::to_string(nl) + ", torso " +
                 std::to_string(ft + 1) + "/" + std::to_string(nt) +
                 (_anims.empty() ? std::string()
                                 : "; " + _anims[size_t(_legsAnim)].name + " with " + _anims[size_t(_torsoAnim)].name +
                                       " ([ ]: animation)") +
                 (_torsoOnTag ? "; torso on the legs' tag_torso (T: as in its file)"
                              : "; torso as in its file (T: on the legs' tag_torso)");
    }

    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), 0);
    if (_showInfo || !_error.empty()) {
        std::vector<std::string> lines = _info;
        if (!_error.empty())
            lines.push_back(_error);
        if (!frames.empty())
            lines.push_back(frames + (_animate ? " (A: stop)" : " (A: animate, ,/.: step)") + ", " +
                            std::to_string(_shaders->missingImages()) + " images missing");
        const int lh = font::kLineHeight;
        size_t widest = 0;
        for (const std::string &l : lines)
            widest = std::max(widest, font::length(l));
        const int boxW = std::min(kFrameW, int(widest) * font::kAdvance + 8);
        const int boxH = int(lines.size()) * lh + 6;
        for (int y = 0; y < boxH; ++y)
            for (int x = 0; x < boxW; ++x)
                _overlay.row(y)[x * 4 + 3] = 150;
        for (size_t i = 0; i < lines.size(); ++i)
            font::drawText(_overlay, 4, 3 + int(i) * lh, lines[i],
                           !_error.empty() && lines[i] == _error ? Color{255, 120, 120} : Color{230, 230, 230});
    }
    _renderer->endFrame(&_overlay);
}

} // namespace reluva::idtech3
