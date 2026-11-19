// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/awe/game/scene_page.h"

#include <string>
#include <vector>

namespace reluva::awe {

/**
 * The data viewer's Scenes page: the game's scene page opening on the
 * scenes' files, each shown with its hotspots and what it holds. Left
 * and Right step through them, its other keys show more, and N starts a
 * game, where the keys the game doesn't take still come here. Only
 * reluva-viewer and the tools have it, not reluva.
 */
class SceneBrowser : public ScenePage {
public:
    explicit SceneBrowser(ViewerContext &ctx) : ScenePage(ctx) {}
    const char *name() const override { return "Scenes"; }

protected:
    void showFirst() override { load(); }
    void sceneLoading() override;
    void drawOverlays(const std::vector<const Ac2Record *> &objects) override;
    void drawFooter() override;
    std::string windowTitle() override;
    bool viewerEvent(const Event &ev) override;

private:
    // The camera's walk grid on the floor, the cell under the mouse and the
    // path last found.
    void drawGrid();

    int _selected = -1; // object highlighted, -1 none
    bool _hotspots = true;
    bool _info = true;
    bool _showGrid = false;
    WalkGrid::Cell _hover;
};

} // namespace reluva::awe
