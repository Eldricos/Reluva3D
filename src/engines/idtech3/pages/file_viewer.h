// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/pages/page.h"

#include <string>
#include <vector>

namespace reluva::idtech3 {

class FileViewer : public Page {
public:
    explicit FileViewer(ViewerContext& ctx);
    const char* name() const override { return "Files"; }
    size_t count() const override { return files_.size(); }
    void activate() override;
    void handleEvent(const Event& ev) override;

private:
    void draw();

    struct Row {
        std::string name;
        std::string source; // the archive's file name
    };
    std::vector<Row> files_;
    int top_ = 0;
};

} // namespace reluva::idtech3
