// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The data viewer's page drawing the SI files' 3D models and playing
// their animations (docs/engines/legoisland/formats/obj.md).

#include "common/fs.h"
#include "engines/legoisland/formats/dta.h"
#include "engines/legoisland/pages/viewers.h"
#include "graphics/font.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace reluva::legoisland {

namespace {

constexpr int kRowH = font::kLineHeight + 1;
constexpr Color kText = {230, 230, 230};
constexpr Color kTitle = {150, 200, 255};
constexpr Color kError = {255, 120, 120};

std::string withoutDigits(std::string s) {
    while (!s.empty() && s.back() >= '0' && s.back() <= '9')
        s.pop_back();
    return s;
}

// A name without the `*` copies have in front.
std::string base(const std::string &name) {
    return fs::toLower(name.starts_with('*') ? name.substr(1) : name);
}

} // namespace

SiScenePage::SiScenePage(System &sys, Surface &screen, const WdbFile &wdb, std::vector<std::filesystem::path> files)
    : _sys(sys), _screen(screen), _lib(wdb), _files(std::move(files)) {
}

SiScenePage::~SiScenePage() {
    deactivate();
}

void SiScenePage::activate() {
    if (Error e = _sys.init3D(kViewerW, kViewerH, _renderer)) {
        _renderer = nullptr;
        for (int y = 0; y < _screen.h; y++)
            std::fill_n(_screen.row(y), size_t(_screen.w) * 4, uint8_t(40));
        font::drawText(_screen, 4, 4, "The SI files' models need 3D rendering, which isn't available:", kError);
        font::drawText(_screen, 4, 4 + kRowH, e.message, kText);
        return;
    }
    _scene = std::make_unique<RoiScene>(*_renderer);
    _overlay.create(kViewerW, kViewerH, PixelFormat::kRGBA32);
    if (_objects.empty() && _fileError.empty())
        openFile(_file);
    else
        show(_current);
}

void SiScenePage::deactivate() {
    _scene.reset();
    _bindings.clear();
    _renderer = nullptr;
}

void SiScenePage::openFile(size_t index) {
    _file = index;
    _objects.clear();
    _fileError.clear();
    if (Error e = _si.open(_files[index])) {
        _fileError = e.message;
        _si = SiFile();
    }
    for (const SiObject *o : _si.all()) {
        const std::string kind = objectKind(*o);
        if (kind == "ani" || kind == "mod")
            _objects.push_back(o);
    }
    show(0);
}

void SiScenePage::show(size_t index) {
    _current = index;
    _time = 0;
    _cameraView = false;
    build();
    // Frame all the pieces cover as it plays, a few dozen moments of it:
    // characters walk well outside the circle its header gives.
    gfx::Vec3 lo{1e30f, 1e30f, 1e30f};
    gfx::Vec3 hi{-1e30f, -1e30f, -1e30f};
    bool any = false;
    NodePlaces places;
    const uint32_t stepMs = std::max<uint32_t>(100, _animation.duration / 40);
    for (uint32_t t = 0; t <= _animation.duration; t += stepMs) {
        placeNodes(_animation.root, t, places);
        for (const Binding &b : _bindings)
            if (places.isShown(b.node.empty() ? b.parent : b.node))
                any |= _scene->grow(b.piece, placeOf(b, places), lo, hi);
    }
    _orbit.frame(lo, hi, any);
}

bool SiScenePage::findModel(const std::string &name, WdbModel &out) const {
    const std::string n = base(name);
    if (_lib.model(n, out) || _lib.model(withoutDigits(n), out))
        return true;
    // The SI file's own models, by their root's name.
    for (const SiObject *o : _objects) {
        if (objectKind(*o) != "mod")
            continue;
        std::vector<SiPiece> pieces;
        if (_si.readPieces(o->id, pieces) || parseModel(objectData(pieces), out))
            continue;
        if (fs::toLower(out.roi.name) == n || fs::toLower(out.roi.name) == withoutDigits(n))
            return true;
    }
    return false;
}

void SiScenePage::build() {
    _scene->clear();
    _bindings.clear();
    _actorModels.clear();
    _drawnAs.clear();
    _error.clear();
    _animation = WdbAnimation();
    _isAnimation = false;
    if (_current >= _objects.size())
        return;
    const SiObject &o = *_objects[_current];
    std::vector<SiPiece> pieces;
    if (Error e = _si.readPieces(o.id, pieces)) {
        _error = e.message;
        return;
    }
    const std::vector<uint8_t> data = objectData(pieces);
    if (objectKind(o) == "mod") {
        if (Error e = parseModel(data, _model)) {
            _error = e.message;
            return;
        }
        // A model stands as its own animation places it at its start.
        _animation.root = _model.animation;
        bindModel(_model, "");
        return;
    }
    if (Error e = parseAnimation(data, _animation)) {
        _error = e.message;
        return;
    }
    _isAnimation = true;
    for (const WdbActor &actor : _animation.actors) {
        const WdbAnimNode *node = findNode(_animation.root, actor.name);
        if (!node) {
            _drawnAs.push_back(actor.name + ": no node");
            continue;
        }
        if (actor.kind == uint32_t(DtaModelKind::kCharacter)) {
            const size_t before = _bindings.size();
            for (const WdbAnimNode &child : node->children)
                bindCharacter(child);
            _drawnAs.push_back(actor.name + ": a character, " + std::to_string(_bindings.size() - before) + " pieces");
            continue;
        }
        auto model = std::make_unique<WdbModel>();
        if (findModel(actor.name, *model)) {
            _drawnAs.push_back(actor.name + ": model " + model->roi.name);
            bindModel(*model, node->name);
            _actorModels.push_back(std::move(model));
            continue;
        }
        // A part's, or the shared part's, by its name without digits.
        const std::string n = withoutDigits(base(actor.name));
        const WdbPart *part = _lib.part(base(actor.name));
        if (!part)
            part = _lib.part(n);
        const WdbRoi *roi = part && !part->rois.empty() ? &part->rois[0] : _lib.sharedRoi(n);
        if (!roi) {
            _drawnAs.push_back(actor.name + ": not found");
            _scene->addMissing(actor.name);
            continue;
        }
        TextureLists lists;
        if (part)
            lists.push_back(&part->textures);
        lists.push_back(&_lib.file().sharedPart().textures);
        lists.push_back(&_lib.file().sharedTextures());
        const size_t piece = _scene->add(roi->lods, _lod, lists);
        if (piece != RoiScene::kNone)
            _bindings.push_back({piece, node->name, "", gfx::identity()});
        _drawnAs.push_back(actor.name + ": part " + roi->name);
    }
}

// A model's ROIs: each where the animation's node of its name puts it, or
// else where the model's own animation puts it, from the actor's node.
void SiScenePage::bindModel(const WdbModel &model, const std::string &actorNode) {
    NodePlaces own;
    placeNodes(model.animation, 0, own);
    std::vector<std::pair<size_t, const WdbRoi *>> pieces;
    addModelRois(*_scene, _lib, model, _lod, {}, pieces);
    for (const auto &[piece, roi] : pieces) {
        if (findNode(_animation.root, roi->name) || actorNode.empty()) {
            _bindings.push_back({piece, roi->name, "", gfx::identity()});
        } else {
            const gfx::Mat4 *at = own.find(roi->name);
            _bindings.push_back({piece, "", actorNode, at ? *at : gfx::identity()});
        }
    }
}

// A character's nodes are named after the shared part's pieces: BODY,
// HEAD, ARM-RT; the legs (LEG-RT, LEG-LFT) are both its "leg", and the
// right claw its "claw-lft" (inferred).
void SiScenePage::bindCharacter(const WdbAnimNode &node) {
    // Another actor it carries is drawn as that actor.
    for (const WdbActor &actor : _animation.actors)
        if (fs::toLower(actor.name) == fs::toLower(node.name))
            return;
    std::string n = fs::toLower(node.name);
    // A node named with a "-" first only groups others.
    if (n.starts_with('-')) {
        for (const WdbAnimNode &child : node.children)
            bindCharacter(child);
        return;
    }
    const WdbRoi *roi = _lib.sharedRoi(n);
    if (!roi && n.ends_with("-rt"))
        roi = _lib.sharedRoi(n.substr(0, n.size() - 3) + "-lft");
    if (!roi && n.find('-') != std::string::npos)
        roi = _lib.sharedRoi(n.substr(0, n.find('-')));
    if (roi) {
        const TextureLists lists = {&_lib.file().sharedPart().textures, &_lib.file().sharedTextures()};
        const size_t piece = _scene->add(roi->lods, _lod, lists);
        if (piece != RoiScene::kNone)
            _bindings.push_back({piece, node.name, "", gfx::identity()});
    } else {
        _scene->addMissing("character piece " + node.name);
    }
    for (const WdbAnimNode &child : node.children)
        bindCharacter(child);
}

gfx::Mat4 SiScenePage::placeOf(const Binding &b, const NodePlaces &places) const {
    if (!b.node.empty())
        if (const gfx::Mat4 *at = places.find(b.node))
            return *at;
    if (!b.parent.empty())
        if (const gfx::Mat4 *at = places.find(b.parent))
            return gfx::multiply(*at, b.offset);
    return b.offset;
}

void SiScenePage::handleEvent(const Event &ev) {
    if (!_renderer || _orbit.handle(ev) || ev.type != EventType::kKeyDown)
        return;
    const size_t n = _objects.size();
    switch (ev.key) {
    case key::kLeft:
    case key::kRight:
        if (!_files.empty())
            openFile((_file + (ev.key == key::kRight ? 1 : _files.size() - 1)) % _files.size());
        return;
    case key::kUp:
        if (_current > 0)
            show(_current - 1);
        return;
    case key::kDown:
        if (_current + 1 < n)
            show(_current + 1);
        return;
    case key::kPageUp:
        show(_current > 20 ? _current - 20 : 0);
        return;
    case key::kPageDown:
        if (n)
            show(std::min(_current + 20, n - 1));
        return;
    case key::kHome:
        show(0);
        return;
    case key::kEnd:
        if (n)
            show(n - 1);
        return;
    case ' ':
        _paused = !_paused;
        return;
    case 'c':
        _cameraView = !_cameraView && _animation.hasCamera;
        return;
    case 'l':
        _lod = (_lod + 1) % std::max(1, _scene->lodLevels());
        build();
        return;
    case 'r':
        show(_current);
        return;
    case 'i':
        _showInfo = !_showInfo;
        return;
    default:
        return;
    }
}

void SiScenePage::drawInfo() {
    std::fill(_overlay.pixels.begin(), _overlay.pixels.end(), 0);
    int y = 4;
    auto line = [&](const std::string &s, Color c) {
        if (y > kViewerH - kRowH)
            return;
        font::drawText(_overlay, 4, y, s, c);
        y += kRowH;
    };
    std::string title = std::to_string(_file + 1) + "/" + std::to_string(_files.size()) + "  " +
                        _files[_file].filename().string() + ": ";
    if (_objects.empty())
        title += "no 3D models or animations (Left/Right: another SI file)";
    else
        title += _objects[_current]->name + "  (" + std::to_string(_current + 1) + "/" +
                 std::to_string(_objects.size()) + ", Tab: next page)";
    line(title, kTitle);
    for (const std::string &e : {_fileError, _error})
        if (!e.empty())
            line(e, kError);
    if (!_showInfo || _objects.empty())
        return;
    line(_objects[_current]->fileName, kText);
    char buf[200];
    if (_isAnimation) {
        std::snprintf(buf, sizeof buf, "%.2f of %.2f s%s%s; LOD %d (L), %zu triangles; I hides this", _time / 1000.0,
                      _animation.duration / 1000.0, _paused ? ", paused (Space)" : "",
                      _animation.hasCamera ? (_cameraView ? ", through its camera (C)" : ", has a camera (C)") : "",
                      _lod + 1, _scene->triangles());
        line(buf, kText);
        std::snprintf(buf, sizeof buf, "looking at (%.2f, %.2f, %.2f) from %.2f away; R resets", _orbit.center[0],
                      _orbit.center[1], -_orbit.center[2], _orbit.distance);
        line(buf, kText);
        for (const std::string &a : _drawnAs)
            line("  " + a, kText);
    } else {
        std::snprintf(buf, sizeof buf, "model %s, %zu pieces; LOD %d (L), %zu triangles; I hides this",
                      _model.roi.name.c_str(), _bindings.size(), _lod + 1, _scene->triangles());
        line(buf, kText);
    }
    for (const std::string &m : _scene->missing())
        line("not found: " + m, kError);
}

void SiScenePage::update(uint64_t nowMs) {
    if (!_renderer)
        return;
    if (!_paused && _lastMs && _animation.duration)
        _time = uint32_t((_time + (nowMs - _lastMs)) % _animation.duration);
    _lastMs = nowMs;

    gfx::Vec3 eye{};
    gfx::Mat4 viewProj = _orbit.viewProj(kViewerW, kViewerH, eye);
    if (_cameraView && !_animation.cameraEye.empty() && !_animation.cameraTarget.empty()) {
        // The camera's keys are in the game's space: mirrored, as the models are.
        const std::array<float, 4> e = keyValue(_animation.cameraEye, _time);
        const std::array<float, 4> t = keyValue(_animation.cameraTarget, _time);
        eye = {e[0], e[1], -e[2]};
        const gfx::Vec3 forward{t[0] - e[0], t[1] - e[1], -(t[2] - e[2])};
        viewProj = gfx::multiply(gfx::perspective(0.8f, float(kViewerW) / float(kViewerH), 0.05f, 500.0f),
                                 gfx::lookAlong(eye, forward, {0, 1, 0}));
    }
    _renderer->beginFrame(viewProj, eye, {70, 74, 82});
    NodePlaces places;
    placeNodes(_animation.root, _time, places);
    for (const Binding &b : _bindings)
        if (places.isShown(b.node.empty() ? b.parent : b.node))
            _scene->put(b.piece, placeOf(b, places));
    _scene->flush();
    drawInfo();
    _renderer->endFrame(&_overlay);
}

} // namespace reluva::legoisland
