// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/awe/ui/game_overlay.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

// And Then There Were None's journal (ac.exe 0x41b400..0x41d600): a
// panel at the screen's right edge that slides out when its book is
// clicked, its four tabs (General, Guests, Docs, Books) each a contents
// page of sub-headings and, for one chosen, its entries; an entry shows
// once its condition holds. See docs/engines/awe/games/attwn/systems/journal.md.

namespace reluva::awe::attwn {

/** The journal: Text/Journal's four tables, and the panel that shows them. */
class Journal : public GameOverlay {
public:
    /** Reads the four tables from `host`'s files; a table it can't read is left empty. */
    explicit Journal(OverlayHost &host);

    void update(float seconds) override;
    void draw(Surface &dst, const GameFonts &fonts) override;
    bool click(int x, int y) override;
    bool covers(int x, int y) const override;
    /** "journal `<tab>`" (0x41c290): that tab's contents, the book blinking if it's closed. */
    bool verb(const std::vector<std::string> &words) override;

    /** A row of a tab's table: an entry under a sub-heading, shown once its condition holds. */
    struct Entry {
        std::string heading;
        std::string condition;
        std::string text;
        bool shown = false; // its condition held when the journal last looked
    };
    /** A tab's entries (tab 0 to 3), in the table's order. */
    const std::vector<Entry> &entries(int tab) const { return _tabs[size_t(tab)].entries; }
    /** The tab shown, 0 to 3. */
    int tab() const { return _tab; }
    /** The sub-heading whose entries are shown; empty: the tab's contents. */
    const std::string &heading() const { return _tabs[size_t(_tab)].heading; }
    /** Whether the panel is out. */
    bool open() const { return _open; }
    /** The panel's left edge: 754 closed, 420 open. */
    float x() const { return _x; }
    /** The sub-headings the contents page lists, in order (0x41c720). */
    std::vector<std::string> headings() const;
    /** Tests the entries' conditions (0x41c600). */
    void retest();

private:
    struct Tab {
        std::vector<Entry> entries;
        std::string heading;
    };
    struct Row {
        int page = 0;
        int x = 0;
        int y = 0;
        std::string text;
        bool bullet = false;
    };
    // The page's rows as laid out (the contents' sub-headings, or the
    // entries' wrapped rows), each with its page.
    std::vector<Row> layout(const GameFonts *fonts) const;
    int pages() const;
    void openClose();

    OverlayHost &_host;
    std::array<Tab, 4> _tabs;
    int _tab = 0;
    int _page = 0;
    int _contentsPage = 0; // the contents' page a sub-heading was picked on (+0x348)
    bool _open = false;
    int _moving = 0; // 1 opening, 2 closing
    float _x = 754;
    float _step = 0;     // the slide's fraction of a pixel left over
    float _blink = 0;    // seconds the book blinks for (0x41c341)
    float _flip = 0.33f; // till the blink flips
    bool _blinkOff = false;
    const GameFonts *_fonts = nullptr; // the last draw's, for clicks
};

} // namespace reluva::awe::attwn
