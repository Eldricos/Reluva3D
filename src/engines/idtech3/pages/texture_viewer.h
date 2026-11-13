// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/pages/page.h"

#include <string>
#include <vector>

namespace reluva::idtech3 {

class TextureViewer : public Page {
public:
    explicit TextureViewer(ViewerContext& ctx);
    const char* name() const override { return "Textures"; }
    size_t count() const override { return names_.size(); }
    void activate() override { show(index_); }
    void handleEvent(const Event& ev) override;

private:
    void show(int index);

    std::vector<std::string> names_;
    int index_ = 0;
};

} // namespace reluva::idtech3
