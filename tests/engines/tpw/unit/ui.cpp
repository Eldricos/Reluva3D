// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Theme Park World engine's screens, run headless on installs built
// here: the opening (docs/engines/tpw/games/simcoaster/research/footage.md).
//
//   reluva-tests tpw.unit.ui

#include "common/headless_system.h"
#include "engines/tpw/game_files.h"
#include "engines/tpw/ui/opening.h"
#include "image/image.h"

#include "tests/support/ea_movie_writer.h"
#include "tests/support/test.h"
#include "tests/support/zip_writer.h"

#include <vector>

using namespace reluva;

namespace {

// Keeps the last frame shown, and how many.
struct Screen : HeadlessSystem {
    Surface last;
    int shown = 0;
    void updateScreen(const Surface &s) override {
        HeadlessSystem::updateScreen(s);
        last = s;
        ++shown;
    }
};

// An install with the logo movie, the language's character set (by name:
// it only says which language), and a splash of one red pixel.
void makeInstall(const std::filesystem::path &dir, bool movie, bool splash) {
    if (movie)
        test::writeFile(dir / "Data" / "movies" / "bfSim.tgq", test::makeEaMovie());
    test::writeFile(dir / "Data" / "Language" / "American" / "MBToUni.dat", test::toBytes("BFMU"));
    if (splash) {
        image::Image red;
        red.w = 640;
        red.h = 480;
        red.rgba.assign(size_t(640) * 480 * 4, 255);
        red.rgba[1] = red.rgba[2] = 0; // its first pixel red
        test::writeFile(dir / "Data" / "Init" / "640" / "Splash_American.png", image::encodePng(red));
    }
}

void testOpening(const std::filesystem::path &tmp) {
    makeInstall(tmp / "game", true, true);
    SearchSet files;
    std::vector<std::string> warnings;
    CHECK(!tpw::mountGame(files, tmp / "game", warnings) && tpw::languageOf(files) == "American");
    Screen sys;
    CHECK(tpw::playOpening(sys, nullptr, tmp / "game", files));
    // The movie, at 640x480, then the splash, held for 3 seconds.
    CHECK(sys.width() == tpw::kOpeningWidth && sys.height() == tpw::kOpeningHeight && sys.last.w == 640);
    const uint8_t *p = sys.last.row(0);
    CHECK(p[0] == 255 && p[1] == 0 && p[2] == 0 && sys.last.row(479)[639 * 4 + 1] == 255);
    CHECK(sys.getMillis() >= 3000 + 66 && sys.shown >= 3);

    // Without the movie or the splash, nothing to show.
    makeInstall(tmp / "bare", false, false);
    SearchSet bare;
    CHECK(!tpw::mountGame(bare, tmp / "bare", warnings));
    Screen quiet;
    CHECK(tpw::playOpening(quiet, nullptr, tmp / "bare", bare) && quiet.shown == 0);
}

} // namespace

static const reluva::test::Suite kSuite("tpw.unit.ui",
                                        {
                                            {"opening", [](const std::filesystem::path &tmp) { testOpening(tmp); }},
                                        });
