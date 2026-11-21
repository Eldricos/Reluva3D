// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/client/page.h"

#include <string>
#include <vector>

namespace reluva::idtech3 {

/**
 * A data viewer's page showing the game's images (.tga, .jpg, .png) one at
 * a time, scaled to fit; the arrow keys, Page Up/Down, Home and End move
 * through them.
 */
class TextureViewer : public Page {
public:
    /** Lists the images among the game's files. */
    explicit TextureViewer(PageContext &ctx);
    const char *name() const override { return "Textures"; }
    size_t count() const override { return _names.size(); }
    void activate() override { show(_index); }
    void handleEvent(const Event &ev) override;

private:
    void show(int index);

    std::vector<std::string> _names;
    int _index = 0;
};

} // namespace reluva::idtech3
