// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Theme Park World engine's screens, run headless on installs built
// here: the opening (docs/engines/tpw/games/simcoaster/research/footage.md),
// a world's sky, the loading screen, and the front end's flat models and
// Personnel File.
//
//   reluva-tests tpw.unit.ui

#include "common/headless_system.h"
#include "engines/tpw/game_files.h"
#include "engines/tpw/ui/loading_screen.h"
#include "engines/tpw/ui/opening.h"
#include "engines/tpw/ui/personnel_file.h"
#include "engines/tpw/ui/sky.h"
#include "engines/tpw/ui/ui_model.h"
#include "image/image.h"

#include "tests/engines/tpw/fixtures.h"
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

// A picture of one colour, as a TGA.
std::vector<uint8_t> plainTga(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    image::Image img;
    img.w = 8;
    img.h = 8;
    for (int i = 0; i < 64; ++i)
        img.rgba.insert(img.rgba.end(), {r, g, b, a});
    return image::encodeTga(img, true);
}

// A world's sky: a red dome under clear clouds and band, its fog and light
// from its settings over the levels' own; drawn, the sky overhead is the
// dome's and the view down the fog's.
void testSky(const std::filesystem::path &tmp) {
    const std::filesystem::path data = tmp / "game" / "Data" / "levels";
    test::writeFile(data / "Standard.sam", test::toBytes("ThemeEngine.FogColour 255\r\nLightNormal.X 1\r\n"
                                                         "LightNormal.Y 0\r\nLightNormal.Z 0\r\n"));
    test::writeFile(data / "test" / "Standard.sam", test::toBytes("ThemeEngine.FogColour 65280\r\n"));
    test::writeFile(data / "test" / "sky" / "sky_rgb.tga", plainTga(255, 0, 0, 255));
    test::writeFile(data / "test" / "sky" / "sky.tga", plainTga(255, 255, 255, 255));
    test::writeFile(data / "test" / "sky" / "sky_cyl.tga", plainTga(0, 0, 0, 0));
    SearchSet files;
    std::vector<std::string> warnings;
    CHECK(!tpw::mountGame(files, tmp / "game", warnings));
    tpw::Sky sky;
    CHECK(!sky.load(files, "levels/test"));
    CHECK(sky.fog.r == 0 && sky.fog.g == 255 && sky.fog.b == 0);
    CHECK(sky.light[0] == 1 && sky.light[1] == 0);
    Surface screen;
    screen.create(64, 64, PixelFormat::kRGBA32);
    // Looking straight up, then straight down.
    tpw::drawSky(screen, soft3d::OrbitCamera({0, 0, 0}, 0, -1.5f, 10, 64, 64), sky);
    const uint8_t *up = screen.row(32) + 32 * 4;
    CHECK(up[0] == 255 && up[1] == 0 && up[2] == 0);
    tpw::drawSky(screen, soft3d::OrbitCamera({0, 0, 0}, 0, 1.5f, 10, 64, 64), sky);
    const uint8_t *down = screen.row(32) + 32 * 4;
    CHECK(down[0] == 0 && down[1] == 255 && down[2] == 0);
    CHECK(sky.load(files, "levels/none"));
}

// A picture of one colour, as a PNG.
std::vector<uint8_t> plainPng(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    image::Image img;
    img.w = w;
    img.h = h;
    for (int i = 0; i < w * h; ++i)
        img.rgba.insert(img.rgba.end(), {r, g, b, 255});
    return image::encodePng(img);
}

// The loading screen from its pieces: the scene, the logo at the top left,
// the legal text at the bottom right, the Bullfrog logos at the bottom
// left; without its scene, none.
void testLoadingScreen(const std::filesystem::path &tmp) {
    const std::filesystem::path init = tmp / "game" / "Data" / "Init" / "640";
    test::writeFile(init / "welcome.png", plainPng(640, 480, 0, 0, 255));
    test::writeFile(init / "welcome_american.png", plainPng(384, 144, 255, 0, 0));
    test::writeFile(init / "Legal_american.png", plainPng(320, 96, 0, 255, 0));
    test::writeFile(init / "bull_american.png", plainPng(64, 144, 255, 255, 0));
    SearchSet files;
    std::vector<std::string> warnings;
    CHECK(!tpw::mountGame(files, tmp / "game", warnings));
    image::Image screen;
    CHECK(!tpw::composeLoadingScreen(files, "American", screen));
    CHECK(screen.w == 640 && screen.h == 480);
    if (screen.w != 640 || screen.h != 480)
        return;
    auto is = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        const uint8_t *p = screen.pixel(x, y);
        return p[0] == r && p[1] == g && p[2] == b;
    };
    CHECK(is(383, 143, 255, 0, 0) && is(384, 143, 0, 0, 255));
    CHECK(is(320, 384, 0, 255, 0) && is(639, 479, 0, 255, 0) && is(319, 479, 0, 0, 255));
    CHECK(is(0, 479, 255, 255, 0) && is(63, 336, 255, 255, 0) && is(64, 479, 0, 0, 255));
    CHECK(!tpw::composeLoadingScreen(files, "French", screen));
    CHECK(is(0, 0, 0, 0, 255));
    SearchSet none;
    CHECK(tpw::composeLoadingScreen(none, "American", screen));
}

// A flat model's mesh stretched into a rectangle: the test model's first
// face (its corners (0, 0), (4, 0) and (4, 1), textured) fills the lower
// right half of it; its second, untextured, isn't drawn. Then the same
// half blended over what's there by its texture's alpha.
void testUiModel() {
    tpw::LoadedModel model;
    CHECK(!model.model.parse(tpw::testing::makeModel()));
    model.textures.resize(2);
    image::Image &red = model.textures[1];
    red.w = red.h = 1;
    red.rgba = {255, 0, 0, 255};
    Surface screen;
    screen.create(64, 48, PixelFormat::kRGBA32);
    tpw::drawUiMesh(screen, model, 0, {10, 10, 40, 20});
    auto pixel = [&](int x, int y) { return screen.row(y) + x * 4; };
    CHECK(pixel(45, 25)[0] == 255 && pixel(45, 25)[1] == 0);
    CHECK(pixel(15, 25)[0] == 0 && pixel(45, 8)[0] == 0 && pixel(52, 25)[0] == 0);
    red.rgba[3] = 128;
    screen.create(64, 48, PixelFormat::kRGBA32);
    tpw::drawUiMesh(screen, model, 0, {10, 10, 40, 20});
    CHECK(pixel(45, 25)[0] == 128);
    CHECK(tpw::uiMeshRect(model, 1).w == 0);
}

// Each of the Personnel File's controls is where it says it is.
void testPersonnelFileControls() {
    using Control = tpw::PersonnelFile::Control;
    for (int c = int(Control::kTab0); c <= int(Control::kCoasterKit); ++c) {
        const tpw::ScreenRect r = tpw::PersonnelFile::placeOf(Control(c));
        CHECK(r.w > 0 && tpw::PersonnelFile::controlAt(r.x + r.w / 2, r.y + r.h / 2) == Control(c));
    }
    CHECK(tpw::PersonnelFile::controlAt(0, 0) == Control::kNone);
}

} // namespace

static const reluva::test::Suite
    kSuite("tpw.unit.ui", {
                              {"opening", [](const std::filesystem::path &tmp) { testOpening(tmp); }},
                              {"sky", [](const std::filesystem::path &tmp) { testSky(tmp); }},
                              {"loading", [](const std::filesystem::path &tmp) { testLoadingScreen(tmp); }},
                              {"ui-model", [](const std::filesystem::path &) { testUiModel(); }},
                              {"personnel-file", [](const std::filesystem::path &) { testPersonnelFileControls(); }},
                          });
