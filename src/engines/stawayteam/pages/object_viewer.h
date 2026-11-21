// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/system.h"
#include "engines/stawayteam/pages/file_list.h"
#include "engines/stawayteam/pages/page.h"

#include <string>
#include <vector>

namespace reluva::stawayteam {

/**
 * The data viewer's page of objects: every object of DATA.dat, in text
 * (.txt, .zax, .MDS, .STY) or in binary (.mdl16, .seq16, .way), and every
 * script (.RDS), shown as Reluva reads it, a field or entry a line. The
 * file keys of FileList; Up/Down scroll a line, , and . a page.
 */
class ObjectViewer : public Page {
public:
    /** `sys`, `screen` and `data` must outlive it. */
    ObjectViewer(System &sys, Surface &screen, const Archive &data);

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
    std::vector<std::string> _lines;
    size_t _scroll = 0;
};

} // namespace reluva::stawayteam
