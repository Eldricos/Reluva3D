// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The game's help (MM2HELP.HLP in its folder, a Windows Help file), shown
// when the menus' navigation bar's Help is pressed: its topics listed,
// and a topic's text to read, with its pictures and its links to other
// topics. See docs/engines/age/games/mm2/systems/menus.md.

#include "common/archive.h"
#include "common/events.h"
#include "common/types.h"
#include "common/winhelp.h"

#include <map>
#include <string>
#include <vector>

namespace reluva::age {

/** The help, drawn over the menus while open; it has the keys and mouse then. */
class HelpViewer {
public:
    /** Reads the help; false (and why) if it isn't there or can't be read. */
    Error load(const SearchSet &files);
    /** Whether the help read has topics to list. */
    bool loaded() const { return !_list.empty(); }

    /** Opens on the list of topics, if the help loaded. */
    void open();
    /** Whether it's open: over the menus, taking their keys. */
    bool isOpen() const { return _open; }
    /** A key, click or wheel turn while open; closes itself on Esc (or a right click) from the list. */
    void handle(const Event &ev);
    /** Draws the list or the topic over the 640 x 480 overlay, while open. */
    void draw(Surface &overlay) const;

    /** For tests: the topic being read (-1: the list). */
    int reading() const { return _reading; }
    /** For tests: the topic's links, each the topic it goes to. */
    const std::vector<int> &links() const { return _links; }
    /** For tests: the link chosen (an index into links(), -1: none). */
    int chosenLink() const { return _link; }

private:
    // A line of the topic as it's laid out: its characters, each one's link
    // (an index into _links, or -1); or a picture's rows.
    struct Line {
        std::string text;
        std::vector<int> link;
        int picture = -1; // the picture starting on this line
    };
    void openTopic(int index, bool remember = true);
    void layOut();
    void scroll(int lines);
    void follow(int link);
    void showLink(int link); // scrolled into view
    int visibleRows() const;
    const winhelp::Picture *picture(int number) const;
    int pictureLines(const winhelp::Picture &p) const;

    std::vector<uint8_t> _file;                        // the help, for its pictures
    std::vector<winhelp::Topic> _topics;               // every topic (links go to any)
    std::vector<int> _list;                            // those listed: with a title and text
    mutable std::map<int, winhelp::Picture> _pictures; // read when first shown
    bool _open = false;
    // the list's chosen topic, and its first shown
    int _row = 0;
    int _top = 0;
    int _reading = -1;      // the topic open, or -1 for the list
    std::vector<int> _back; // the topics read before, for Backspace
    std::vector<Line> _lines;
    std::vector<int> _links; // the topic's links' targets
    int _link = -1;          // the link chosen (Tab)
    int _line = 0;           // the first line shown
};

} // namespace reluva::age
