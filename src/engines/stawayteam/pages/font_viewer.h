// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/system.h"
#include "engines/stawayteam/formats/binary_object.h"
#include "engines/stawayteam/pages/file_list.h"
#include "engines/stawayteam/pages/page.h"

namespace reluva::stawayteam {

/**
 * The data viewer's page of fonts: the sequences in Cache/Fonts, each
 * frame a character from its First ASCII to its Last ASCII, shown in a grid
 * and in a line of text. The file keys of FileList; Up/Down scroll the
 * grid, B changes the background.
 */
class FontViewer : public Page {
public:
    /** `sys`, `screen` and `data` must outlive it. */
    FontViewer(System &sys, Surface &screen, const Archive &data);

    size_t count() const override { return _files.size(); }
    void activate() override;
    void handleKey(uint32_t key) override;

private:
    void load();
    void draw();

    System &_sys;
    Surface &_screen;
    const Archive &_data;
    FileList _files;
    BinaryObject _font;
    std::string _error;
    int _scroll = 0; // the grid's first row shown
    int _backdrop = 0;
};

} // namespace reluva::stawayteam
