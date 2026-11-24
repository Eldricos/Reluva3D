// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/events.h"
#include "common/types.h"
#include "engines/tpw/formats/bf4.h"
#include "engines/tpw/formats/str.h"
#include "engines/tpw/park/attractions.h"
#include "engines/tpw/park/park.h"
#include "engines/tpw/ui/model_view.h"
#include "engines/tpw/ui/ui_model.h"
#include "graphics/soft3d/rasterizer.h"
#include "image/image.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace reluva::tpw {

/**
 * The Buy window: a tab for each kind of attraction, those on sale listed
 * by name with their prices (a tick by those the park has), the one
 * chosen turning in a frame, its ratings below; the tick buys it, to be
 * placed. See docs/engines/tpw/games/simcoaster/systems/building.md#the-buy-window.
 */
class BuyWindow {
public:
    /** Its controls, for clicking them. */
    enum class Control { kNone, kTab, kRow, kBuy, kCancel, kClose, kHelp };

    /** Reads its models, fonts, strings and the bars' picture. */
    Error load(const SearchSet &files);

    /**
     * Opens it on a tab, listing the world's attractions of that kind on
     * sale; both are kept, for the other tabs, while it's open.
     */
    void open(const Attractions &attractions, Attractions::Kind kind, const Park &park);
    void close() { _open = false; }
    bool isOpen() const { return _open; }

    /** What a click did. */
    enum class Result { kNone, kClosed, kBuy };
    /** Takes a mouse event; the window takes them all while it's open. */
    Result handle(const Event &ev);
    /** The attraction chosen in the list; nullptr if the list is empty. */
    const Attractions::Attraction *chosen() const;
    /** The name the game shows for an attraction. */
    std::string shownName(const Attractions::Attraction &a) const;

    /** Moves its clock on, in milliseconds: the chosen one turns. */
    void advance(uint32_t ms) { _clock += ms; }
    /** Draws it over an RGBA32 surface of 640 × 480. */
    void draw(Surface &screen) const;

    /** Where a control is, at 640 × 480: a tab by its kind, a row by its place in the list. */
    static ScreenRect placeOf(Control control, size_t index = 0);

private:
    void choose(size_t row);
    void drawBar(Surface &screen, int x, int y, float level) const;
    void drawPreview(Surface &screen) const;

    const SearchSet *_files = nullptr;
    LoadedModel _window;
    LoadedModel _panel;
    LoadedModel _frame;
    LoadedModel _header;
    LoadedModel _ok;
    LoadedModel _cancel;
    LoadedModel _help;
    LoadedModel _closeButton;
    LoadedModel _tick;
    std::array<LoadedModel, Attractions::kKinds> _tabs;
    image::Image _bar;
    CharacterSet _characters;
    Font _titleFont;
    Font _font;
    StringTable _ui;
    StringTable _names;
    StringTable _oldNames;

    const Attractions *_attractions = nullptr;
    const Park *_park = nullptr;
    bool _open = false;
    Attractions::Kind _kind = Attractions::Kind::kRide;
    std::vector<const Attractions::Attraction *> _list; // by shown name
    std::vector<int> _owned;                            // how many the park has of each
    size_t _row = 0;
    Control _over = Control::kNone;
    size_t _overIndex = 0;
    uint32_t _clock = 0;
    mutable std::map<int, LoadedModel> _models; // the chosen ones', read as they're shown
    mutable soft3d::Rasterizer _rasterizer;
};

} // namespace reluva::tpw
