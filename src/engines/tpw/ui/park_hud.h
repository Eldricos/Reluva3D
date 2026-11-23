// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/events.h"
#include "common/types.h"
#include "engines/tpw/ui/model_view.h"
#include "engines/tpw/ui/ui_model.h"

#include <array>
#include <cstddef>

namespace reluva::tpw {

/**
 * The park's screen furniture, from the interface's models: the
 * thermometer of the park's mood (empty: its fill isn't drawn yet) and
 * its face, the compass with its turn
 * and zoom buttons, the date's box, the tools down the left and the
 * buttons along the bottom. It follows the mouse over its buttons, and
 * says which are held or chosen; the money and the date are written over
 * it by the park's screen. See
 * docs/engines/tpw/games/simcoaster/systems/park.md#the-hud.
 */
class ParkHud {
public:
    /** The tools down the left, top to bottom. */
    enum class Tool { kPointer, kPath, kBulldozer, kCamera, kPostcard };
    static constexpr size_t kTools = 5;

    /** The buttons along the bottom, left to right. */
    enum class Gizmo { kMessages, kAttractions, kStaff, kVisitors, kFinance, kPark, kMaps };
    static constexpr size_t kGizmos = 7;

    /** The compass's buttons. */
    enum class CompassButton { kTurnLeft, kTurnRight, kZoomIn, kZoomOut };
    static constexpr size_t kCompassButtons = 4;

    /** The six faces of the park's mood, unhappiest first; the last is a bare park's, a red no-entry sign. */
    static constexpr size_t kMoods = 6;

    /** What the HUD shows besides its buttons' states. */
    struct View {
        size_t mood = kMoods - 1;
        bool canZoomIn = true;
        bool canZoomOut = true;
    };

    /** Reads the models. */
    Error load(const SearchSet &files);

    /**
     * Follows the mouse over the buttons, presses and releases them; a
     * click on a tool chooses it.
     * @return True if the event is the HUD's: the mouse over or on one of
     *     its pieces, which the park beneath shouldn't take.
     */
    bool handle(const Event &ev);

    /** The tool chosen: the pointer at first. */
    Tool tool() const { return _tool; }
    /** Whether a compass button is held down, as the camera turns and zooms while it is. */
    bool held(CompassButton button) const;

    /** Where a piece is at 640 × 480. */
    ///@{
    static ScreenRect toolRect(Tool tool);
    static ScreenRect gizmoRect(Gizmo gizmo);
    static ScreenRect compassRect(CompassButton button);
    ///@}
    /** The black field of the date's box, where the date is written. */
    static ScreenRect dateField();

    /** Draws it over an RGBA32 surface of 640 × 480. */
    void draw(Surface &screen, const View &view) const;

private:
    // A button: where it is in the list of all of them, to follow the mouse.
    enum class Hit { kNone, kTool, kGizmo, kCompass, kHelp };
    struct Spot {
        Hit kind = Hit::kNone;
        size_t index = 0;
        bool operator==(const Spot &) const = default;
    };
    static Spot spotAt(int x, int y);
    size_t stateOf(const Spot &spot, bool chosen, bool disabled) const;

    LoadedModel _gauge;
    LoadedModel _faces;
    LoadedModel _compassPlate;
    LoadedModel _compassDial;
    std::array<LoadedModel, kCompassButtons> _compass;
    LoadedModel _dateBox;
    LoadedModel _toolBar;
    std::array<LoadedModel, kTools> _tools;
    LoadedModel _gizmoBar;
    std::array<LoadedModel, kGizmos> _gizmos;
    LoadedModel _help;

    Tool _tool = Tool::kPointer;
    Spot _over;    // under the mouse
    Spot _pressed; // the left button held down on it
};

} // namespace reluva::tpw
