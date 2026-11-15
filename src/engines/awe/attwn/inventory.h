// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/awe/ui/game_overlay.h"
#include "video/bink.h"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// And Then There Were None's inventory (ac.exe 0x4178a0..0x41b300): a
// panel at the screen's left edge that slides out when its sack is
// clicked (or on a right click, or I): 12 items a page and a tray of four
// below them, the Examine and Combine buttons, and the close-up an item
// is examined in. The items are the shared story state's
// (GameState::items, Orient Express's states); see
// docs/engines/awe/games/attwn/systems/inventory.md.

namespace reluva::awe {
struct Table;
}

namespace reluva::awe::attwn {

/**
 * Text\charlist.txt as the game reads it (0x41a420), not as a table: the
 * first line passed over, and any shorter than 20 characters; the rest
 * cut at tabs and line ends with runs of them as one (strtok), into the
 * item, the character, the condition and the event, a cell's opening
 * quote dropped, and its closing one with it. The install's file reads
 * the same either way.
 */
Table readCharacters(std::string_view text);

/** The inventory panel. */
class Inventory {
public:
    explicit Inventory(OverlayHost &host);

    void update(float seconds);
    void draw(Surface &dst, const GameFonts &fonts);
    bool click(int x, int y);
    /** Opens or closes it (0x417e20). */
    void toggle();
    /** Shut at once, a new item in the bag, as an act changes (0x417f70). */
    void shut();
    /** "examine": the close-up of what's in the tray (0x419bd0). */
    void examine();
    /** Puts the close-up away (0x41a010): `tab` 2 (the journal's Docs) uses the item up. */
    void closeExamine(int tab, bool said);
    bool open() const { return _open; }
    float x() const { return _x; }
    int page() const { return _page; }
    int pages() const { return _pages; }
    /** The slot under the screen's (x, y), -1 for none: a page's 12, then the tray's 144 to 147. */
    int slotAt(int x, int y) const;
    /** The items in the tray (slots 144 to 147), in slot order. */
    std::vector<std::string> tray() const;
    /** The item under the mouse (its description shows), when nothing's in hand. */
    void hover(int x, int y);
    /** The Examine and Combine buttons (0x419790, 0x41a740). */
    void examineButton();
    void combineButton();
    /** The tray emptied onto the pages and `items` put in it, as a player would: false if one isn't in the bag. */
    bool toTray(const std::vector<std::string> &items);
    /**
     * What Examine would do something with: a document or a book not
     * read, an item with a close-up, two prints of one person not
     * compared.
     */
    std::vector<std::vector<std::string>> examinable() const;
    /** A player's next click towards `want` (GameOverlay::hand). */
    OverlayHand hand(OverlayWant want, const std::vector<std::string> &items) const;
    /** Whether a click at (x, y) would be its (click()'s test, without doing it). */
    bool covers(int x, int y) const;
    /** Whether a close-up shows. */
    bool showing() const { return _showing; }

    static constexpr int kTray = 144; ///< the tray's first slot (page 12)

private:
    struct Row {
        std::string description;
        std::string examine; // "docs 65", "books", "0"
    };
    const Row *row(const std::string &item) const;
    void putInTray();
    void updatePages();
    std::string art(const std::string &item, const char *kind) const;

    OverlayHost &_host;
    std::map<std::string, Row> _rows; // itemlist.txt by Item Name
    bool _open = false;
    int _moving = 0; // 1 opening, 2 closing
    float _x = -341;
    float _step = 0;
    int _page = 0;
    int _pages = 2;
    float _blink = 0; // the new item's blink (0x418cfd)
    float _flip = 0.33f;
    bool _blinkOff = false;
    std::string _hover; // the item under the mouse, described
    std::string _seen;  // the item just taken whose blink has started
    // The close-up: the item, its picture or movie, a second print and
    // the paper under them.
    std::string _examined;
    std::string _second;
    std::unique_ptr<bink::Movie> _movie;
    image::Image _frame;
    float _movieTime = 0;
    size_t _next = 0;
    bool _showing = false;
};

} // namespace reluva::awe::attwn
