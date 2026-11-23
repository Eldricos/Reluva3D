// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/ui/main_menu.h"

#include "engines/tpw/ui/front_end.h"
#include "engines/tpw/ui/front_end_effects.h"
#include "engines/tpw/ui/globe.h"
#include "engines/tpw/ui/personnel_file.h"

#include <algorithm>
#include <cstring>

namespace reluva::tpw {

int runFrontEnd(System &sys, const SearchSet &files, const std::filesystem::path &saveFolder) {
    FrontEndScene scene;
    Globe globe;
    const bool drawn = !scene.load(files) && !globe.load(scene);
    FrontEndEffects effects;
    const bool withEffects = drawn && !effects.load(files, scene);
    if (drawn && !playFrontEnd(sys, scene, withEffects ? &effects : nullptr))
        return -1;

    const GlobeTurning turning(globe, sys.getMillis());
    soft3d::Rasterizer rasterizer;
    const Backdrop backdrop = [&](Surface &screen) {
        if (drawn)
            turning.draw(screen, rasterizer, sys.getMillis());
        else
            std::fill(screen.pixels.begin(), screen.pixels.end(), uint8_t(0));
    };
    PlayerFile player;
    if (!choosePlayer(sys, files, saveFolder, backdrop, player))
        return -1;

    std::array<bool, Globe::kWorlds> open{};
    for (size_t i = 0; i < open.size(); ++i)
        open[i] = player.worlds[i].open;
    if (std::none_of(open.begin(), open.end(), [](bool o) { return o; }))
        open[0] = true;
    if (!drawn)
        return int(std::find(open.begin(), open.end(), true) - open.begin());
    return chooseWorld(sys, files, turning, open);
}

} // namespace reluva::tpw
