// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/client/page.h"

#include <string>
#include <vector>

namespace reluva::idtech3 {

/**
 * A data viewer's page listing every file in the game's search path, each
 * with the archive it's read from, under the search order and the first
 * problem loading them.
 */
class FileViewer : public Page {
public:
    /** Lists the game's files. */
    explicit FileViewer(PageContext &ctx);
    const char *name() const override { return "Files"; }
    size_t count() const override { return _files.size(); }
    void activate() override;
    void handleEvent(const Event &ev) override;

private:
    void draw();

    struct Row {
        std::string name;
        std::string source; // the archive's file name
    };
    std::vector<Row> _files;
    int _top = 0;
};

} // namespace reluva::idtech3
