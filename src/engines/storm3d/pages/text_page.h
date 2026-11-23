// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/storm3d/game_files.h"
#include "engines/storm3d/pages/page.h"

#include <string>
#include <vector>

namespace reluva::storm3d {

/**
 * The game's text files as they're written: the object definitions, the
 * missions' objectives and tech trees (decoded from the archives), the
 * settings, sprite tables, maps' descriptions, key maps and credits.
 *   Left/Right: previous/next file   Up/Down: a line   PageUp/PageDown: a screen
 *   Home/End: the file's start/end   F: the next kind of file
 */
class TextPage : public Page {
public:
    /** Lists the files; `files`, `sys` and `screen` must outlive it. */
    TextPage(System &sys, Surface &screen, const GameFiles &files);

    const char *name() const override { return "Text"; }
    size_t count() const override { return _names.size(); }
    void activate() override { show(); }
    void handleKey(uint32_t key) override;

    /** Whether a file is one of those the page shows, by its extension. */
    static bool isText(const std::string &name);

private:
    void load();
    void show();

    System &_sys;
    Surface &_screen;
    const GameFiles &_files;
    std::vector<std::string> _names;
    size_t _current = 0;
    std::vector<std::string> _lines; // the current file's, as UTF-8 with tabs expanded
    bool _loaded = false;
    size_t _top = 0; // the first line shown
};

} // namespace reluva::storm3d
