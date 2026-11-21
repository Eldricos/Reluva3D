// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/system.h"
#include "engines/stawayteam/pages/file_list.h"
#include "engines/stawayteam/pages/page.h"

namespace reluva::stawayteam {

/**
 * The data viewer's page of pictures: every .frm16 in DATA.dat, one at a
 * time. The file keys of FileList; B changes the background (dark, a
 * checkerboard, magenta), Z the zoom, I hides the status line.
 */
class PictureViewer : public Page {
public:
    /** `sys`, `screen` and `data` must outlive it. */
    PictureViewer(System &sys, Surface &screen, const Archive &data);

    size_t count() const override { return _files.size(); }
    void activate() override { show(); }
    void handleKey(uint32_t key) override;

private:
    void show();

    System &_sys;
    Surface &_screen;
    const Archive &_data;
    FileList _files;
    int _backdrop = 0;
    int _zoom = 0; // 0: as large as fits, up to 4 times; else that many times
    bool _info = true;
};

} // namespace reluva::stawayteam
