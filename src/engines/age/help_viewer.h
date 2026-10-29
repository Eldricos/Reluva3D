// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The game's help (MM2HELP.HLP in its folder, a Windows Help file), shown
// when the menus' navigation bar's Help is pressed: its topics listed,
// and a topic's text to read. See docs/engines/age-menus.md.

#include "common/archive.h"
#include "common/events.h"
#include "common/types.h"
#include "common/winhelp.h"

#include <string>
#include <vector>

namespace reluva::age {

class HelpViewer {
public:
    // Reads the help; false (and why) if it isn't there or can't be read.
    Error load(const SearchSet& files);
    bool loaded() const { return !topics_.empty(); }

    void open();
    bool isOpen() const { return open_; }
    void handle(const Event& ev); // closes itself on Esc from the list
    void draw(Surface& overlay) const;

private:
    void openTopic(int index);
    void scroll(int lines);
    int visibleRows() const;

    std::vector<winhelp::Topic> topics_; // those with a title and text
    bool open_ = false;
    int row_ = 0, top_ = 0;          // the list's chosen topic, and its first shown
    int reading_ = -1;               // the topic open, or -1 for the list
    std::vector<std::string> lines_; // its text, wrapped
    int line_ = 0;                   // its first line shown
};

} // namespace reluva::age
