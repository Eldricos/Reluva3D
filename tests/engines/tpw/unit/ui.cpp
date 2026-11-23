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
#include "engines/tpw/players.h"
#include "engines/tpw/ui/loading_screen.h"
#include "engines/tpw/ui/front_end_audio.h"
#include "engines/tpw/ui/front_end_effects.h"
#include "engines/tpw/ui/front_end_overlay.h"
#include "engines/tpw/ui/opening.h"
#include "engines/tpw/ui/personnel_file.h"
#include "engines/tpw/ui/sky.h"
#include "engines/tpw/ui/ui_model.h"
#include "image/image.h"

#include "tests/engines/tpw/fixtures.h"
#include "tests/support/ea_movie_writer.h"
#include "tests/support/test.h"
#include "tests/support/zip_writer.h"

#include <algorithm>
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

// Each of the Personnel File's controls is where it says it is, and only
// those its state shows can be clicked: an empty slot's tick once a name
// is typed, a player's buttons, the delete dialog's alone while it asks.
void testPersonnelFileControls() {
    using Control = tpw::PersonnelFile::Control;
    auto at = [](Control c, const tpw::PlayerSlots &players, const tpw::PersonnelFile::View &view) {
        const tpw::ScreenRect r = tpw::PersonnelFile::placeOf(c);
        return tpw::PersonnelFile::controlAt(r.x + r.w / 2, r.y + r.h / 2, players, view);
    };
    tpw::PlayerSlots players;
    tpw::PersonnelFile::View view;
    for (int c = int(Control::kTab0); c <= int(Control::kTab3); ++c)
        CHECK(at(Control(c), players, view) == Control(c));
    CHECK(at(Control::kQuitGame, players, view) == Control::kQuitGame);
    CHECK(at(Control::kEnterGame, players, view) == Control::kNone);
    CHECK(at(Control::kNameDone, players, view) == Control::kNone);
    view.placeholder = false;
    view.typed = "Tester";
    CHECK(at(Control::kNameDone, players, view) == Control::kNameDone);
    players[0].name = "Tester";
    for (Control c : {Control::kEnterGame, Control::kDeletePlayer, Control::kCoasterKit})
        CHECK(at(c, players, view) == c);
    CHECK(at(Control::kLoadGame, players, view) == Control::kNone);
    view.confirmingDelete = true;
    CHECK(at(Control::kTab1, players, view) == Control::kNone);
    for (Control c : {Control::kConfirmYes, Control::kConfirmNo, Control::kConfirmClose})
        CHECK(at(c, players, view) == c);
    CHECK(tpw::PersonnelFile::controlAt(0, 0, players, view) == Control::kNone);
}

// Players kept in a save folder and read back: names with spaces, their
// numbers, empty slots; a fresh record's numbers for what the file lacks.
void testPlayers(const std::filesystem::path &tmp) {
    tpw::PlayerFile fresh;
    fresh.sharesIssued = 30000;
    fresh.sharesOwned = 1000;
    fresh.worlds[0] = {0, true, 0, 10};
    fresh.worlds[1] = {1, false, 0, 10};
    fresh.worlds[2] = {3, false, 0, 10};
    tpw::PlayerSlots players = tpw::readPlayers(tmp / "none", fresh);
    CHECK(!players[0].exists() && players[3].sharesOwned == 1000);
    players[1] = fresh;
    players[1].name = "No Self";
    players[1].goldenTickets = 3;
    players[1].worlds[1].open = true;
    players[1].worlds[0].won = 2;
    players[1].objective = 0;
    CHECK(!tpw::writePlayers(tmp / "saves", players));
    const tpw::PlayerSlots back = tpw::readPlayers(tmp / "saves", fresh);
    CHECK(!back[0].exists() && !back[2].exists() && back[1].name == "No Self");
    CHECK(back[1].goldenTickets == 3 && back[1].worlds[1].open && back[1].worlds[0].won == 2);
    CHECK(back[1].objective == 0 && back[1].sharesIssued == 30000 && back[1].worlds[2].challenges == 10);
    CHECK(tpw::writePlayers({}, players));
}

// A burst of particles: 20 of them, drawn over a black screen in front
// of the camera, gone once their life is over.
void testParticles() {
    tpw::ParticleLibrary::Effect effect;
    effect.lifeMs = 1000;
    effect.colours.fill(0xffffffff);
    tpw::Particles particles;
    particles.burst(effect, {0, 0, 0}, 0);
    CHECK(particles.size() == 20);
    Surface screen;
    screen.create(64, 48, PixelFormat::kRGBA32);
    const soft3d::OrbitCamera camera({0, 0, 0}, 0, 0, 50, 64, 48);
    particles.draw(screen, camera, 100);
    bool lit = false;
    for (size_t i = 0; i < screen.pixels.size(); i += 4)
        lit = lit || screen.pixels[i] > 0;
    CHECK(lit);
    particles.forget(999);
    CHECK(particles.size() == 20);
    particles.forget(1000);
    CHECK(particles.size() == 0);
}

// The front end's overlay: the advisor shown while he speaks, the button
// skip until the camera is over the globe and play after; a click on the
// button or a key presses it, the mouse over it lights it.
void testFrontEndOverlay() {
    using Overlay = tpw::FrontEndOverlay;
    CHECK(!Overlay::advisorShown(0) && Overlay::advisorShown(300) && Overlay::advisorShown(25199));
    CHECK(!Overlay::advisorShown(26000) && Overlay::advisorShown(30000) && !Overlay::advisorShown(52200));
    CHECK(Overlay::buttonAt(0) == Overlay::Button::kSkip);
    CHECK(Overlay::buttonAt(Overlay::kGlobeMillis - 1) == Overlay::Button::kSkip);
    CHECK(Overlay::buttonAt(Overlay::kGlobeMillis) == Overlay::Button::kPlay);
    CHECK(Overlay::kGlobeMillis < Overlay::kPersonnelFileMillis);

    Overlay overlay;
    const tpw::ScreenRect r = Overlay::buttonRect();
    Event ev;
    ev.type = EventType::kMouseDown;
    ev.button = MouseButton::kLeft;
    ev.mouseX = r.x + r.w / 2;
    ev.mouseY = r.y + r.h / 2;
    CHECK(overlay.handle(ev));
    ev.mouseX = r.x - 1;
    CHECK(!overlay.handle(ev));
    ev = Event{};
    ev.type = EventType::kKeyDown;
    CHECK(overlay.handle(ev));
    ev.repeat = true;
    CHECK(!overlay.handle(ev));
    ev = Event{};
    ev.type = EventType::kMouseMove;
    CHECK(!overlay.handle(ev));
}

// The front end's music and speech from a game of two tiny banks: the
// music queued from its group and cut over to another, the advisor's
// lines said at their times, a line whose portrait has gone by not said.
void testFrontEndAudio(const std::filesystem::path &tmp) {
    using tpw::testing::Bytes;
    using tpw::testing::put32;
    const std::filesystem::path data = tmp / "game" / "Data";
    auto bankOf = [](size_t sounds, bool stereo) {
        Bytes bank;
        put32(bank, 0, uint32_t(sounds));
        bank.resize(4 + 4 * sounds);
        for (size_t i = 0; i < sounds; ++i) {
            put32(bank, 4 + 4 * i, uint32_t(bank.size()));
            tpw::testing::putSound(bank, "s.wav", stereo ? 0x03 : 0x02, 22050, Bytes(4000, uint8_t(0x10 + i)), 0);
        }
        return bank;
    };
    using tpw::testing::MapGroup;
    test::writeFile(data / "levels" / "wIntro" / "Music" / "cat_musicSFX.map",
                    tpw::testing::makeEffectMap(
                        {{2, {{100, 0x7fff, {{1, 0xffff, 45, 1}}, 2}, {100, 0x8000, {{2, 0xffff, 45, 1}}, 2}}}}));
    test::writeFile(data / "levels" / "wIntro" / "Music" / "cat_musicBANK.map",
                    tpw::testing::makeBankMap({"Music\\Music"}));
    test::writeFile(data / "levels" / "wIntro" / "Music" / "MusicHD.sdt", bankOf(2, true));
    std::vector<std::pair<uint32_t, std::vector<MapGroup>>> lines;
    for (uint32_t id = 1018; id <= 1020; ++id)
        lines.push_back({id, {{100, 0xffff, {{id - 1017, 0xffff, 90, 1}}}}});
    test::writeFile(data / "global" / "Speech" / "cat_speechSFX.map", tpw::testing::makeEffectMap(lines));
    test::writeFile(data / "global" / "Speech" / "cat_speechBANK.map", tpw::testing::makeBankMap({"speech\\speech"}));
    test::writeFile(data / "global" / "Speech" / "speechHD.sdt", bankOf(3, false));
    SearchSet files;
    std::vector<std::string> warnings;
    CHECK(!tpw::mountGame(files, tmp / "game", warnings));

    tpw::FrontEndAudio sound;
    CHECK(!sound.load(files));
    audio::Mixer mixer(22050);
    sound.setMixer(&mixer);
    sound.update(0);
    CHECK(!sound.piecesQueued().empty() && sound.piecesQueued()[0] == 0 && sound.linesStarted().empty());
    std::vector<int16_t> out(2 * 64);
    mixer.mix(out.data(), 64);
    CHECK(std::any_of(out.begin(), out.end(), [](int16_t v) { return v != 0; }));
    sound.update(2000);
    CHECK((sound.linesStarted() == std::vector<uint32_t>{1018}));
    sound.setMusicGroup(tpw::FrontEndAudio::kGlobeGroup);
    CHECK(sound.piecesQueued().back() == 1);
    // Skipped to the globe: the second line's portrait has gone by.
    sound.update(tpw::FrontEndOverlay::kGlobeMillis);
    CHECK((sound.linesStarted() == std::vector<uint32_t>{1018, 1020}));
    sound.stop();
    CHECK(!mixer.isTypePlaying(audio::SoundType::kMusic) && !mixer.isTypePlaying(audio::SoundType::kSpeech));
    sound.setMixer(nullptr);

    tpw::FrontEndAudio silent;
    CHECK(!silent.load(files));
    silent.update(30000);
    CHECK((silent.linesStarted() == std::vector<uint32_t>{1019}) && silent.piecesQueued().empty());
    silent.stopSpeech(true);
    silent.update(40000);
    CHECK(silent.linesStarted().size() == 1);
}

} // namespace

static const reluva::test::Suite
    kSuite("tpw.unit.ui", {
                              {"opening", [](const std::filesystem::path &tmp) { testOpening(tmp); }},
                              {"sky", [](const std::filesystem::path &tmp) { testSky(tmp); }},
                              {"loading", [](const std::filesystem::path &tmp) { testLoadingScreen(tmp); }},
                              {"ui-model", [](const std::filesystem::path &) { testUiModel(); }},
                              {"personnel-file", [](const std::filesystem::path &) { testPersonnelFileControls(); }},
                              {"players", [](const std::filesystem::path &tmp) { testPlayers(tmp); }},
                              {"particles", [](const std::filesystem::path &) { testParticles(); }},
                              {"front-end-overlay", [](const std::filesystem::path &) { testFrontEndOverlay(); }},
                              {"front-end-audio", [](const std::filesystem::path &tmp) { testFrontEndAudio(tmp); }},
                          });
