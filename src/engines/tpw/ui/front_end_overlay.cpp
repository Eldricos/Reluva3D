// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The places and times are measured on the recording:
// docs/engines/tpw/games/simcoaster/systems/front-end.md#the-portrait-and-the-button.

#include "engines/tpw/ui/front_end_overlay.h"

#include <array>
#include <utility>

namespace reluva::tpw {

namespace {

constexpr size_t kAdvisorMesh = 0; // mugshot1.tga
constexpr size_t kPlainMesh = 0;
constexpr size_t kLitMesh = 2;

constexpr ScreenRect kFrame{503, 8, 129, 129};
// The picture inside the frame's rim.
constexpr ScreenRect kPortrait{509, 14, 117, 117};
constexpr ScreenRect kButton{570, 410, 64, 64};

// While the advisor speaks, it seems *(inferred)*: three spells, in
// milliseconds from the start of the camera's path.
constexpr std::array<std::pair<uint32_t, uint32_t>, 3> kAdvisorSpells{{
    {300, 25200},
    {27300, 32100},
    {34100, 52200},
}};

} // namespace

Error FrontEndOverlay::load(const SearchSet &files) {
    for (auto [model, name] : {std::pair{&_frame, "ui/!frame.MD2"}, std::pair{&_portraits, "ui/mugshot.MD2"},
                               std::pair{&_skip, "ui/b_skip.MD2"}, std::pair{&_play, "ui/b_play.MD2"}})
        if (Error err = model->load(files, name))
            return err;
    return Error::success();
}

bool FrontEndOverlay::advisorShown(uint32_t ms) {
    for (const auto &[from, to] : kAdvisorSpells)
        if (ms >= from && ms < to)
            return true;
    return false;
}

FrontEndOverlay::Button FrontEndOverlay::buttonAt(uint32_t ms) {
    return ms < kGlobeMillis ? Button::kSkip : Button::kPlay;
}

ScreenRect FrontEndOverlay::buttonRect() {
    return kButton;
}

bool FrontEndOverlay::handle(const Event &ev) {
    switch (ev.type) {
    case EventType::kMouseMove:
        _lit = kButton.contains(ev.mouseX, ev.mouseY);
        return false;
    case EventType::kMouseDown:
        return ev.button == MouseButton::kLeft && kButton.contains(ev.mouseX, ev.mouseY);
    case EventType::kKeyDown:
        return !ev.repeat;
    default:
        return false;
    }
}

void FrontEndOverlay::draw(Surface &screen, uint32_t ms) const {
    if (advisorShown(ms)) {
        drawUiMesh(screen, _frame, 0, kFrame);
        drawUiMesh(screen, _portraits, kAdvisorMesh, kPortrait);
    }
    const LoadedModel &button = buttonAt(ms) == Button::kSkip ? _skip : _play;
    drawUiMesh(screen, button, _lit ? kLitMesh : kPlainMesh, kButton);
}

} // namespace reluva::tpw
