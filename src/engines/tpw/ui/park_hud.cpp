// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The places are measured on the recording, at 640 × 480, the models
// matched against its frames at 2:38 and 5:00:
// docs/engines/tpw/games/simcoaster/systems/park.md#the-hud.

#include "engines/tpw/ui/park_hud.h"

#include <utility>

namespace reluva::tpw {

namespace {

// A control's meshes, in the order the models name them
// (docs/engines/tpw/games/simcoaster/formats/ui-models.md#states).
constexpr size_t kPlain = 0;
constexpr size_t kDisabled = 1;
constexpr size_t kLit = 2;
constexpr size_t kLitDown = 3;
constexpr size_t kHeldDown = 4;
constexpr size_t kDown = 5;

constexpr ScreenRect kGauge{14, 5, 36, 96};
// The thermometer's tube: its model is full, its liquid light blue; the
// recording's is black above the level, all of it in a bare park.
constexpr ScreenRect kTube{26, 11, 13, 57};
constexpr ScreenRect kFace{17, 69, 30, 30};
constexpr ScreenRect kCompass{553, 7, 80, 80};
constexpr ScreenRect kDial{569, 23, 48, 48};
constexpr ScreenRect kDateBox{554, 89, 80, 32};
constexpr ScreenRect kDateField{566, 97, 60, 16};
constexpr ScreenRect kToolBar{7, 249, 48, 218};
constexpr ScreenRect kGizmoBar{163, 436, 360, 13};
constexpr ScreenRect kHelp{570, 414, 36, 36};

constexpr int kToolX = 12;
constexpr int kToolSize = 38;
constexpr std::array<int, ParkHud::kTools> kToolY{253, 296, 339, 381, 424};
constexpr int kGizmoY = 425;
constexpr int kGizmoSize = 48;
constexpr std::array<int, ParkHud::kGizmos> kGizmoX{107, 181, 237, 293, 349, 405, 461};
constexpr int kCompassButtonSize = 40;

constexpr std::array<const char *, ParkHud::kTools> kToolModels{
    "ui/tb_pointer.MD2", "ui/tb_path.MD2", "ui/tb_delete.MD2", "ui/tb_camera.MD2", "ui/tb_postcard.MD2"};
constexpr std::array<const char *, ParkHud::kGizmos> kGizmoModels{
    "ui/giz_message.MD2", "ui/giz_attract.MD2", "ui/giz_staff.MD2", "ui/giz_visitors.MD2",
    "ui/giz_finance.MD2", "ui/giz_park.MD2",    "ui/giz_maps.MD2"};
constexpr std::array<const char *, ParkHud::kCompassButtons> kCompassModels{
    "ui/comp_rotatel.MD2", "ui/comp_rotater.MD2", "ui/comp_zoomin.MD2", "ui/comp_zoomout.MD2"};

} // namespace

Error ParkHud::load(const SearchSet &files) {
    std::array<std::pair<LoadedModel *, const char *>, 8> singles{{
        {&_gauge, "ui/happyguage.MD2"},
        {&_faces, "ui/face.MD2"},
        {&_compassPlate, "ui/comp.MD2"},
        {&_compassDial, "ui/compgizn.MD2"},
        {&_dateBox, "ui/datebox.MD2"},
        {&_toolBar, "ui/toolbox.MD2"},
        {&_gizmoBar, "ui/gizmo.MD2"},
        {&_help, "ui/b_help.MD2"},
    }};
    for (auto [model, name] : singles)
        if (Error err = model->load(files, name))
            return err;
    for (size_t i = 0; i < kTools; ++i)
        if (Error err = _tools[i].load(files, kToolModels[i]))
            return err;
    for (size_t i = 0; i < kGizmos; ++i)
        if (Error err = _gizmos[i].load(files, kGizmoModels[i]))
            return err;
    for (size_t i = 0; i < kCompassButtons; ++i)
        if (Error err = _compass[i].load(files, kCompassModels[i]))
            return err;
    return Error::success();
}

ScreenRect ParkHud::toolRect(Tool tool) {
    return {kToolX, kToolY[size_t(tool)], kToolSize, kToolSize};
}

ScreenRect ParkHud::gizmoRect(Gizmo gizmo) {
    return {kGizmoX[size_t(gizmo)], kGizmoY, kGizmoSize, kGizmoSize};
}

ScreenRect ParkHud::compassRect(CompassButton button) {
    // Turning above, zooming below; left, then right.
    const int i = int(button);
    return {kCompass.x + (i % 2) * kCompassButtonSize, kCompass.y + (i / 2) * kCompassButtonSize, kCompassButtonSize,
            kCompassButtonSize};
}

ScreenRect ParkHud::dateField() {
    return kDateField;
}

ParkHud::Spot ParkHud::spotAt(int x, int y) {
    for (size_t i = 0; i < kTools; ++i)
        if (toolRect(Tool(i)).contains(x, y))
            return {Hit::kTool, i};
    for (size_t i = 0; i < kGizmos; ++i)
        if (gizmoRect(Gizmo(i)).contains(x, y))
            return {Hit::kGizmo, i};
    for (size_t i = 0; i < kCompassButtons; ++i)
        if (compassRect(CompassButton(i)).contains(x, y))
            return {Hit::kCompass, i};
    if (kHelp.contains(x, y))
        return {Hit::kHelp, 0};
    return {};
}

bool ParkHud::handle(const Event &ev) {
    switch (ev.type) {
    case EventType::kMouseMove: {
        _over = spotAt(ev.mouseX, ev.mouseY);
        return _over.kind != Hit::kNone;
    }
    case EventType::kMouseDown: {
        if (ev.button != MouseButton::kLeft)
            return false;
        _over = spotAt(ev.mouseX, ev.mouseY);
        _pressed = _over;
        return _pressed.kind != Hit::kNone || kGauge.contains(ev.mouseX, ev.mouseY) ||
               kToolBar.contains(ev.mouseX, ev.mouseY);
    }
    case EventType::kMouseUp: {
        if (ev.button != MouseButton::kLeft || _pressed.kind == Hit::kNone)
            return false;
        // A tool is chosen as the button is let go over it.
        if (_pressed.kind == Hit::kTool && spotAt(ev.mouseX, ev.mouseY) == _pressed)
            _tool = Tool(_pressed.index);
        _pressed = {};
        return true;
    }
    default:
        return false;
    }
}

bool ParkHud::held(CompassButton button) const {
    return _pressed.kind == Hit::kCompass && _pressed.index == size_t(button) && _over == _pressed;
}

size_t ParkHud::stateOf(const Spot &spot, bool chosen, bool disabled) const {
    if (disabled)
        return kDisabled;
    const bool over = _over == spot;
    if (over && _pressed == spot)
        return kHeldDown;
    if (chosen)
        return over ? kLitDown : kDown;
    return over ? kLit : kPlain;
}

void ParkHud::draw(Surface &screen, const View &view) const {
    drawUiMesh(screen, _gauge, 0, kGauge);
    for (int y = kTube.y; y < kTube.y + kTube.h; ++y) {
        uint8_t *row = screen.pixels.data() + size_t(y) * screen.pitch;
        // Its top rounded off.
        const int inset = y < kTube.y + 2 ? 2 : 0;
        for (int x = kTube.x + inset; x < kTube.x + kTube.w - inset; ++x) {
            row[x * 4] = row[x * 4 + 1] = row[x * 4 + 2] = 0;
            row[x * 4 + 3] = 255;
        }
    }
    drawUiMesh(screen, _faces, view.mood < kMoods ? view.mood : kMoods - 1, kFace);

    drawUiMesh(screen, _compassPlate, 0, kCompass);
    for (size_t i = 0; i < kCompassButtons; ++i) {
        const bool disabled = (CompassButton(i) == CompassButton::kZoomIn && !view.canZoomIn) ||
                              (CompassButton(i) == CompassButton::kZoomOut && !view.canZoomOut);
        drawUiMesh(screen, _compass[i], stateOf({Hit::kCompass, i}, false, disabled), compassRect(CompassButton(i)));
    }
    drawUiMesh(screen, _compassDial, 0, kDial);
    drawUiMesh(screen, _dateBox, 0, kDateBox);

    drawUiMesh(screen, _toolBar, 0, kToolBar);
    for (size_t i = 0; i < kTools; ++i)
        drawUiMesh(screen, _tools[i], stateOf({Hit::kTool, i}, size_t(_tool) == i, false), toolRect(Tool(i)));

    drawUiMesh(screen, _gizmoBar, 0, kGizmoBar);
    for (size_t i = 0; i < kGizmos; ++i)
        drawUiMesh(screen, _gizmos[i], stateOf({Hit::kGizmo, i}, false, false), gizmoRect(Gizmo(i)));
    drawUiMesh(screen, _help, _over.kind == Hit::kHelp ? kLit : kPlain, kHelp);
}

} // namespace reluva::tpw
