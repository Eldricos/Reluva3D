// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/system.h"

#include <cstdint>
#include <string>
#include <vector>

namespace reluva::stawayteam {

/**
 * The data viewer's page of pictures: every .frm16 in DATA.dat, one at a
 * time. Left/Right step through them, PageUp/PageDown by 10, Home/End to
 * the ends, F to the next folder; B changes the background (dark, a
 * checkerboard, magenta), Z the zoom, I hides the status line.
 */
class PictureViewer {
public:
    /** Lists the pictures in `data`, which, like `sys` and `screen`, must outlive it. */
    PictureViewer(System &sys, Surface &screen, const Archive &data);

    size_t count() const { return _names.size(); }
    /** Draws the page as it was left. */
    void activate();
    void handleKey(uint32_t key);

private:
    void show(size_t index);

    System &_sys;
    Surface &_screen;
    const Archive &_data;
    std::vector<std::string> _names;
    size_t _current = 0;
    int _background = 0; // 0 dark, 1 checkerboard, 2 magenta
    int _zoom = 0;       // 0: as large as fits, up to 4 times; else that many times
    bool _info = true;
};

} // namespace reluva::stawayteam
