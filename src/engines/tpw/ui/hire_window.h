// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/events.h"
#include "common/types.h"
#include "engines/tpw/formats/bf4.h"
#include "engines/tpw/formats/str.h"
#include "engines/tpw/park/park.h"
#include "engines/tpw/ui/model_view.h"
#include "engines/tpw/ui/sprites.h"
#include "engines/tpw/ui/ui_model.h"
#include "image/image.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace reluva::tpw {

/**
 * The Hire Staff window: a tab for each kind of staff, the agencies'
 * candidates of the kind shown listed with their experience, monthly
 * wage and agency, the kind's sprite turning in a frame; the tick hires
 * the one chosen. See docs/engines/tpw/games/simcoaster/systems/staff.md.
 */
class HireWindow {
public:
    /** Its controls, for clicking them. */
    enum class Control { kNone, kTab, kRow, kHire, kCancel, kClose, kHelp };

    /**
     * Reads its models, fonts, strings, the staff's names and sprites
     * (the entertainers' of the world in `world`, as `levels/science`).
     */
    Error load(const SearchSet &files, const std::string &world);

    /** Opens it on a tab, over the park whose candidates it lists. */
    void open(const Park &park, Park::StaffKind kind);
    void close() { _open = false; }
    bool isOpen() const { return _open; }

    /** What a click did. */
    enum class Result { kNone, kClosed, kHire };
    /** Takes a mouse event; the window takes them all while it's open. */
    Result handle(const Event &ev);
    /** The kind shown, and the candidate chosen in its list. */
    Park::StaffKind kind() const { return _kind; }
    size_t chosen() const { return _row; }

    /** A candidate's name, as their kind's names table has it. */
    std::string nameOf(Park::StaffKind kind, int name) const;
    /** A kind's sprite, for the park's staff; nullptr if it couldn't be read. */
    const LoadedSprite *spriteOf(Park::StaffKind kind) const;

    /** Moves its clock on, in milliseconds: the sprite turns. */
    void advance(uint32_t ms) { _clock += ms; }
    /** Draws it over an RGBA32 surface of 640 × 480. */
    void draw(Surface &screen) const;

    /** Where a control is, at 640 × 480: a tab by its kind, a row by its place in the list. */
    static ScreenRect placeOf(Control control, size_t index = 0);
    /** The tooltip of the control at a point, UIHELPTEXT's, if any. */
    static std::optional<uint32_t> tipAt(int x, int y);

private:
    const std::vector<Park::Candidate> &list() const;
    bool needsLab() const;

    LoadedModel _window;
    LoadedModel _panel;
    LoadedModel _frame;
    LoadedModel _header;
    LoadedModel _ok;
    LoadedModel _cancel;
    LoadedModel _help;
    LoadedModel _closeButton;
    std::array<LoadedModel, Park::kStaffKinds> _tabs;
    std::array<std::optional<LoadedSprite>, Park::kStaffKinds> _sprites;
    std::array<StringTable, Park::kStaffKinds> _names;
    image::Image _bar;
    CharacterSet _characters;
    Font _titleFont;
    Font _font;
    StringTable _ui;

    const Park *_park = nullptr;
    bool _open = false;
    Park::StaffKind _kind = Park::StaffKind::kJanitor;
    size_t _row = 0;
    Control _over = Control::kNone;
    size_t _overIndex = 0;
    uint32_t _clock = 0;
};

} // namespace reluva::tpw
