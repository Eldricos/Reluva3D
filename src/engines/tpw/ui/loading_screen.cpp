// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/ui/loading_screen.h"

#include "common/fs.h"
#include "engines/tpw/textures.h"

#include <algorithm>
#include <cstring>

namespace reluva::tpw {

namespace {

// The pictures for a 640 x 480 screen.
constexpr const char *kFolder = "Init/640/";

// Draws `piece` over `out` with its top left at (x, y), clipped. The
// pieces carry the scene behind them, so they are drawn whole.
void place(image::Image &out, const image::Image &piece, int x, int y) {
    for (int py = 0; py < piece.h; ++py) {
        const int oy = y + py;
        if (oy < 0 || oy >= out.h)
            continue;
        const int from = std::max(0, -x);
        const int to = std::min(piece.w, out.w - x);
        if (to > from)
            std::memcpy(out.pixel(x + from, oy), piece.pixel(from, py), size_t(to - from) * 4);
    }
}

} // namespace

Error composeLoadingScreen(const SearchSet &files, const std::string &language, image::Image &out) {
    out = {};
    if (Error err = loadPicture(files, std::string(kFolder) + "welcome.png", out))
        return err;
    const std::string lang = fs::toLower(language);
    image::Image piece;
    // The positions, by the recording's loading screen, pixel for pixel:
    // the logo at the top left, the legal text at the bottom right.
    if (!loadPicture(files, std::string(kFolder) + "welcome_" + lang + ".png", piece))
        place(out, piece, 0, 0);
    if (!loadPicture(files, std::string(kFolder) + "Legal_" + lang + ".png", piece))
        place(out, piece, out.w - piece.w, out.h - piece.h);
    // Over the QSound logo the scene has at its bottom left (guessed: the
    // US release, recorded, has none of these).
    if (!loadPicture(files, std::string(kFolder) + "bull_" + lang + ".png", piece))
        place(out, piece, 0, out.h - piece.h);
    return Error::success();
}

} // namespace reluva::tpw
