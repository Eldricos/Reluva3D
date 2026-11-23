// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/storm3d/game_files.h"
#include "engines/storm3d/pages/page.h"

#include <string>
#include <vector>

namespace reluva::storm3d {

/**
 * The install's pictures: the textures (TGA, with their smaller copies
 * `.1` to `.4` after each) and the interface's BMPs.
 *   Left/Right: previous/next   PageUp/PageDown: 10 at a time   Home/End: first/last
 *   F: the next folder's first   A: alpha over a checkerboard on/off   I: the status line on/off
 */
class ImagePage : public Page {
public:
    /** Lists the pictures; `files`, `sys` and `screen` must outlive it. */
    ImagePage(System &sys, Surface &screen, const GameFiles &files);

    const char *name() const override { return "Images"; }
    size_t count() const override { return _names.size(); }
    void activate() override { show(); }
    void handleKey(uint32_t key) override;

    /**
     * The order pictures are listed in: by folder, then by name without
     * its extension, a texture before its smaller copies.
     */
    static bool listedBefore(const std::string &a, const std::string &b);
    /** Whether a file of the install is one of the pictures this page shows. */
    static bool isPicture(const std::string &name);

private:
    void show();

    System &_sys;
    Surface &_screen;
    const GameFiles &_files;
    std::vector<std::string> _names;
    size_t _current = 0;
    bool _alpha = false;
    bool _status = true;
};

} // namespace reluva::storm3d
