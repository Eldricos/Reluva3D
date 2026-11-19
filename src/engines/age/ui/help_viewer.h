// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The game's help (MM2HELP.HLP in its folder, a Windows Help file), shown
// when the menus' navigation bar's Help is pressed: its topics listed,
// and a topic's text to read, with its pictures and its links to other
// topics. See docs/engines/age-menus.md.

#include "common/archive.h"
#include "common/events.h"
#include "common/types.h"
#include "common/winhelp.h"

#include <map>
#include <string>
#include <vector>

namespace reluva::age {

class HelpViewer {
public:
    // Reads the help; false (and why) if it isn't there or can't be read.
    Error load(const SearchSet& files);
    bool loaded() const { return !_list.empty(); }

    void open();
    bool isOpen() const { return _open; }
    void handle(const Event& ev); // closes itself on Esc from the list
    void draw(Surface& overlay) const;

    // For tests: the topic being read (-1: the list), its links (each the
    // topic it goes to), the one chosen.
    int reading() const { return _reading; }
    const std::vector<int>& links() const { return _links; }
    int chosenLink() const { return _link; }

private:
    // A line of the topic as it's laid out: its characters, each one's link
    // (an index into links_, or -1); or a picture's rows.
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
    const winhelp::Picture* picture(int number) const;
    int pictureLines(const winhelp::Picture& p) const;

    std::vector<uint8_t> _file;                        // the help, for its pictures
    std::vector<winhelp::Topic> _topics;               // every topic (links go to any)
    std::vector<int> _list;                            // those listed: with a title and text
    mutable std::map<int, winhelp::Picture> _pictures; // read when first shown
    bool _open = false;
    int _row = 0, _top = 0; // the list's chosen topic, and its first shown
    int _reading = -1;      // the topic open, or -1 for the list
    std::vector<int> _back; // the topics read before, for Backspace
    std::vector<Line> _lines;
    std::vector<int> _links; // the topic's links' targets
    int _link = -1;          // the link chosen (Tab)
    int _line = 0;           // the first line shown
};

} // namespace reluva::age
