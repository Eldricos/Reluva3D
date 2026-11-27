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
#include "engines/tpw/ui/advisor.h"
#include "engines/tpw/ui/briefing.h"
#include "engines/tpw/ui/buy_window.h"
#include "engines/tpw/ui/hire_window.h"
#include "engines/tpw/ui/info_windows.h"
#include "engines/tpw/ui/object_names.h"
#include "engines/tpw/ui/park_windows.h"
#include "engines/tpw/ui/front_end_audio.h"
#include "engines/tpw/ui/front_end_effects.h"
#include "engines/tpw/ui/front_end_overlay.h"
#include "engines/tpw/ui/opening.h"
#include "engines/tpw/ui/park_hud.h"
#include "engines/tpw/ui/personnel_file.h"
#include "engines/tpw/ui/sky.h"
#include "engines/tpw/ui/ui_model.h"
#include "image/image.h"

#include "tests/engines/tpw/fixtures.h"

#include <cstring>
#include "tests/support/ea_movie_writer.h"
#include "tests/support/test.h"
#include "tests/support/zip_writer.h"

#include <algorithm>
#include <cstdio>
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
                    tpw::testing::makeEffectMap({{2,
                                                  {{100, 0x7fff, {{1, 0xffff, 40, 1}}, {{0, 10}, {11, 26}}},
                                                   {100, 0x8000, {{2, 0xffff, 40, 1}}, {{0, 10}, {11, 26}}}}}}));
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
    CHECK(sound.musicGroup() == 0);
    sound.setMusicValue(tpw::FrontEndAudio::kGlobeValue, true);
    CHECK(sound.musicGroup() == 1 && sound.piecesQueued().back() == 1);
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

// The briefing's controls are where it draws them, and nowhere else.
void testBriefingControls() {
    using Control = tpw::Briefing::Control;
    for (Control c : {Control::kObjectivesTab, Control::kChallengesTab, Control::kTurn, Control::kProceed,
                      Control::kCancel, Control::kClose, Control::kAccept}) {
        const tpw::ScreenRect r = tpw::Briefing::placeOf(c);
        CHECK(r.w > 0 && tpw::Briefing::controlAt(r.x + r.w / 2, r.y + r.h / 2) == c);
    }
    CHECK(tpw::Briefing::controlAt(0, 0) == Control::kNone);
    CHECK(tpw::Briefing::controlAt(639, 479) == Control::kNone);
    // The Challenges tab's rows, one under another.
    const tpw::ScreenRect row = tpw::Briefing::challengeRow(2);
    CHECK(tpw::Briefing::challengeRowAt(row.x + 5, row.y + row.h / 2) == 2);
    CHECK(tpw::Briefing::challengeRowAt(row.x + 5, row.y + row.h / 2 - row.h) == 1);
    CHECK(tpw::Briefing::challengeRowAt(0, 0) == -1);
}

// The park's HUD: a tool chosen as the mouse is let go over it, not when
// dragged off; a compass button held while the mouse is down on it; the
// park's own clicks left to it.
void testParkHud() {
    using tpw::ParkHud;
    ParkHud hud;
    auto mouse = [&](EventType type, const tpw::ScreenRect &r) {
        Event ev;
        ev.type = type;
        ev.button = MouseButton::kLeft;
        ev.mouseX = r.x + r.w / 2;
        ev.mouseY = r.y + r.h / 2;
        return hud.handle(ev);
    };
    const tpw::ScreenRect path = ParkHud::toolRect(ParkHud::Tool::kPath);
    const tpw::ScreenRect camera = ParkHud::toolRect(ParkHud::Tool::kCamera);
    CHECK(hud.tool() == ParkHud::Tool::kPointer);
    CHECK(mouse(EventType::kMouseDown, path) && mouse(EventType::kMouseUp, path));
    CHECK(hud.tool() == ParkHud::Tool::kPath);
    CHECK(mouse(EventType::kMouseDown, camera) && mouse(EventType::kMouseUp, path));
    CHECK(hud.tool() == ParkHud::Tool::kPath);

    const tpw::ScreenRect zoomIn = ParkHud::compassRect(ParkHud::CompassButton::kZoomIn);
    CHECK(!hud.held(ParkHud::CompassButton::kZoomIn));
    CHECK(mouse(EventType::kMouseDown, zoomIn) && hud.held(ParkHud::CompassButton::kZoomIn));
    CHECK(!hud.held(ParkHud::CompassButton::kZoomOut));
    mouse(EventType::kMouseMove, {320, 240, 1, 1});
    CHECK(!hud.held(ParkHud::CompassButton::kZoomIn));
    CHECK(mouse(EventType::kMouseUp, zoomIn) && !hud.held(ParkHud::CompassButton::kZoomIn));

    CHECK(!mouse(EventType::kMouseDown, {320, 240, 1, 1}) && !mouse(EventType::kMouseUp, {320, 240, 1, 1}));
    for (size_t i = 0; i < ParkHud::kGizmos; ++i)
        CHECK(mouse(EventType::kMouseMove, ParkHud::gizmoRect(ParkHud::Gizmo(i))));

    // A menu opens on its button; its items pick only once it has risen;
    // Research Lab is greyed; a click beside it closes it, taken by the HUD.
    using Gizmo = ParkHud::Gizmo;
    const tpw::ScreenRect attractions = ParkHud::gizmoRect(Gizmo::kAttractions);
    CHECK(ParkHud::menuRect(Gizmo::kMessages).w == 0 && ParkHud::menuRect(Gizmo::kAttractions).h == 38 + 4 * 18);
    mouse(EventType::kMouseDown, attractions);
    mouse(EventType::kMouseUp, attractions);
    CHECK(hud.menuOpen());
    CHECK(mouse(EventType::kMouseDown, ParkHud::menuItemRect(Gizmo::kAttractions, 0)) && hud.takeChoice() == 0);
    hud.advance(ParkHud::kMenuOpenMillis);
    CHECK(hud.covers(ParkHud::menuRect(Gizmo::kAttractions).x + 1, ParkHud::menuRect(Gizmo::kAttractions).y + 1));
    CHECK(mouse(EventType::kMouseDown, ParkHud::menuItemRect(Gizmo::kAttractions, 2)) && hud.takeChoice() == 0);
    CHECK(hud.menuOpen());
    CHECK(mouse(EventType::kMouseDown, ParkHud::menuItemRect(Gizmo::kAttractions, 1)) && hud.takeChoice() == 835);
    CHECK(!hud.menuOpen() && hud.takeChoice() == 0);
    mouse(EventType::kMouseUp, ParkHud::menuItemRect(Gizmo::kAttractions, 1));

    const tpw::ScreenRect park = ParkHud::gizmoRect(Gizmo::kPark);
    mouse(EventType::kMouseDown, park);
    mouse(EventType::kMouseUp, park);
    hud.advance(ParkHud::kMenuOpenMillis);
    CHECK(mouse(EventType::kMouseDown, {320, 100, 1, 1}) && !hud.menuOpen());
    mouse(EventType::kMouseUp, {320, 100, 1, 1});
    mouse(EventType::kMouseDown, park);
    mouse(EventType::kMouseUp, park);
    hud.advance(ParkHud::kMenuOpenMillis);
    mouse(EventType::kMouseDown, ParkHud::menuItemRect(Gizmo::kPark, 3));
    CHECK(hud.takeChoice() == ParkHud::kObjectivesItem);
}

// The Buy window: shut, it takes nothing; open on an empty list, its tick
// buys nothing and its cross closes it; its tabs side by side.
void testBuyWindow() {
    using tpw::BuyWindow;
    BuyWindow window;
    auto click = [&](BuyWindow::Control c, size_t index = 0) {
        const tpw::ScreenRect r = BuyWindow::placeOf(c, index);
        Event ev;
        ev.type = EventType::kMouseDown;
        ev.button = MouseButton::kLeft;
        ev.mouseX = r.x + r.w / 2;
        ev.mouseY = r.y + r.h / 2;
        return window.handle(ev);
    };
    CHECK(click(BuyWindow::Control::kCancel) == BuyWindow::Result::kNone);
    const tpw::Attractions none;
    const tpw::Park park;
    window.open(none, tpw::Attractions::Kind::kShop, park);
    CHECK(window.isOpen() && !window.chosen());
    CHECK(click(BuyWindow::Control::kBuy) == BuyWindow::Result::kNone && window.isOpen());
    CHECK(click(BuyWindow::Control::kTab, 4) == BuyWindow::Result::kNone && window.isOpen());
    CHECK(click(BuyWindow::Control::kCancel) == BuyWindow::Result::kClosed && !window.isOpen());
    const tpw::ScreenRect first = BuyWindow::placeOf(BuyWindow::Control::kTab, 0);
    const tpw::ScreenRect second = BuyWindow::placeOf(BuyWindow::Control::kTab, 1);
    CHECK(second.x == first.x + first.w && second.y == first.y);
}

// The names shown for attractions, from tables laid out as the game's:
// a pair named by its whole old name, its first string (the second a kind
// of ride) and its second (a kind of shop); a single string; a feature's
// one string; the loudspeakers kept; another world's name found when its
// own has none. And descriptions, by game, shop, flag and the rides' table.
void testObjectNames() {
    using tpw::Attractions;
    tpw::ObjectNames names;
    std::vector<std::string> shown(160);
    std::vector<std::string> old(160);
    auto put = [](std::vector<std::string> &t, size_t at, std::initializer_list<const char *> strings) {
        for (const char *s : strings)
            t[at++] = s;
    };
    put(old, 0,
        {"Lobster", "Laps", "Log Mill", "Special Ride", "Rocket Man", "Costume Shop", "Info Kiosk", "Small Bush",
         "Medium Bush", "Gramophone", "Loudspeaker", "Gem Grabber", "Obelisk Eye"});
    put(shown, 0,
        {"Super", "Sailcarts", "The", "Thrill Mill", "Costume", "Shop", "Info Kiosk", "Bush (small)", "Bush (medium)",
         "Loudspeaker:", "Desert Nights", "Grab-a-Prize", "Test Of Strength"});
    shown.resize(200);
    old.resize(200);
    put(old, 186, {"TV Sim", ""});
    put(shown, 186, {"Arctic", "Adventure"});
    std::vector<std::string> descriptions(104);
    descriptions[67] = "race";
    descriptions[70] = "costumes";
    descriptions[85] = "bin";
    descriptions[83] = "feature";
    descriptions[5] = "karts";
    names.set(shown, old, descriptions);

    auto of = [](Attractions::Kind kind, const char *name, const char *folder = "levels/science/rides/x") {
        Attractions::Attraction a;
        a.kind = kind;
        a.name = name;
        a.folder = folder;
        return a;
    };
    const auto ride = Attractions::Kind::kRide;
    using Lines = std::array<std::string, 2>;
    CHECK(names.lines(of(ride, "Lobster Laps")) == (Lines{"Super", "Sailcarts"}));
    CHECK(names.lines(of(ride, "Log Mill")) == (Lines{"The", "Thrill Mill"}));
    CHECK(names.shown(of(Attractions::Kind::kShop, "Costume Shop")) == "Costume Shop");
    CHECK(names.lines(of(Attractions::Kind::kFacility, "Info Kiosk")) == (Lines{"Info", "Kiosk"}));
    CHECK(names.shown(of(Attractions::Kind::kFeature, "Small Bush")) == "Bush (small)");
    CHECK(names.shown(of(Attractions::Kind::kFeature, "Loudspeaker")) == "Loudspeaker");
    CHECK(names.shown(of(Attractions::Kind::kSideshow, "Gem Grabber")) == "Grab-a-Prize");
    CHECK(names.shown(of(ride, "Unknown Ride")) == "Unknown Ride");
    CHECK(names.shown(of(ride, "TV sim", "levels/science/rides/tvsim")) == "Arctic Adventure");

    Attractions::Attraction game = of(Attractions::Kind::kSideshow, "Camel Racers");
    game.rideType = 23;
    CHECK(names.description(game) == "race");
    CHECK(names.description(of(Attractions::Kind::kShop, "Costume Shop")) == "costumes");
    Attractions::Attraction bin = of(Attractions::Kind::kFacility, "Urn");
    bin.holdsLitter = true;
    CHECK(names.description(bin) == "bin");
    CHECK(names.description(of(Attractions::Kind::kFeature, "Bus")) == "feature");
    Attractions::Attraction karts = of(ride, "Miniature Dreadnoughts");
    karts.id = 1150;
    CHECK(names.description(karts) == "karts");
    CHECK(names.description(of(ride, "Log Mill")).empty());
}

// The Hire Staff window's clicks: an empty pool hires nobody; scientists
// want a research room; its tabs change the kind; cancel closes it.
void testHireWindow() {
    using tpw::HireWindow;
    HireWindow window;
    auto click = [&](HireWindow::Control c, size_t index = 0) {
        const tpw::ScreenRect r = HireWindow::placeOf(c, index);
        Event ev;
        ev.type = EventType::kMouseDown;
        ev.button = MouseButton::kLeft;
        ev.mouseX = r.x + r.w / 2;
        ev.mouseY = r.y + r.h / 2;
        return window.handle(ev);
    };
    CHECK(click(HireWindow::Control::kCancel) == HireWindow::Result::kNone);
    const tpw::Park park; // not loaded: no candidates
    window.open(park, tpw::Park::StaffKind::kJanitor);
    CHECK(window.isOpen() && window.kind() == tpw::Park::StaffKind::kJanitor);
    CHECK(click(HireWindow::Control::kHire) == HireWindow::Result::kNone && window.isOpen());
    CHECK(click(HireWindow::Control::kTab, 5) == HireWindow::Result::kNone &&
          window.kind() == tpw::Park::StaffKind::kScientist);
    CHECK(click(HireWindow::Control::kCancel) == HireWindow::Result::kClosed && !window.isOpen());
    const tpw::ScreenRect first = HireWindow::placeOf(HireWindow::Control::kTab, 0);
    const tpw::ScreenRect second = HireWindow::placeOf(HireWindow::Control::kTab, 1);
    CHECK(second.x == first.x + first.w && second.y == first.y);
    CHECK(!window.spriteOf(tpw::Park::StaffKind::kJanitor));
}

// The right-click menu: an item clicked is chosen, a click elsewhere
// closes it. The gates' window: + raises the entry price, the door shuts
// the park.
void testParkWindows() {
    auto at = [](EventType type, const tpw::ScreenRect &r, MouseButton button = MouseButton::kLeft) {
        Event ev;
        ev.type = type;
        ev.button = button;
        ev.mouseX = r.x + r.w / 2;
        ev.mouseY = r.y + r.h / 2;
        return ev;
    };
    tpw::PopupMenu menu;
    menu.open("Dizzy Droid", {524, 526, 531}, 600, 470); // kept on the screen
    CHECK(menu.isOpen() && menu.itemRect(2).y + menu.itemRect(2).h <= 480 && menu.itemRect(0).x + 130 <= 640);
    CHECK(menu.handle(at(EventType::kMouseMove, menu.itemRect(1))) == tpw::WindowResult::kNone);
    CHECK(menu.handle(at(EventType::kMouseDown, menu.itemRect(1))) == tpw::WindowResult::kChose);
    CHECK(menu.chosen() == 526 && !menu.isOpen());
    menu.open("Dizzy Droid", {524}, 100, 100);
    CHECK(menu.handle(at(EventType::kMouseDown, {0, 0, 2, 2})) == tpw::WindowResult::kClosed && !menu.isOpen());

    tpw::Park park;
    tpw::GateWindow gates;
    gates.open();
    const int fee = park.admissionFee();
    gates.handle(at(EventType::kMouseDown, tpw::GateWindow::placeOf(tpw::GateWindow::Control::kPlus)), park);
    CHECK(park.admissionFee() == fee + 1);
    gates.handle(at(EventType::kMouseDown, tpw::GateWindow::placeOf(tpw::GateWindow::Control::kDoor)), park);
    CHECK(!park.parkOpen());
    CHECK(gates.handle(at(EventType::kMouseDown, tpw::GateWindow::placeOf(tpw::GateWindow::Control::kOk)), park) ==
              tpw::WindowResult::kClosed &&
          !gates.isOpen());

    // The Research Lab: + raises the work rate; Start New Project wants a
    // project chosen for the first slot; the tick closes it.
    using Lab = tpw::ResearchLabWindow;
    Lab lab;
    lab.open();
    const int rate = park.workRate();
    lab.handle(at(EventType::kMouseDown, Lab::placeOf(Lab::Control::kRateUp)), park);
    CHECK(park.workRate() == std::min(100, rate + 5));
    CHECK(lab.handle(at(EventType::kMouseDown, Lab::placeOf(Lab::Control::kStart)), park) == Lab::Wants::kChoose &&
          lab.slot() == 0);
    CHECK(lab.handle(at(EventType::kMouseDown, Lab::placeOf(Lab::Control::kEdit, 1)), park) == Lab::Wants::kNothing);
    lab.handle(at(EventType::kMouseDown, Lab::placeOf(Lab::Control::kOk)), park);
    CHECK(!lab.isOpen());

    // Naming: a character off, two typed, Return keeps it.
    tpw::RenameWindow rename;
    rename.open(3, "Dizzy Droid");
    Event press;
    press.type = EventType::kKeyDown;
    press.key = key::kBackspace;
    rename.handle(press);
    Event typed;
    typed.type = EventType::kTextInput;
    std::snprintf(typed.text, sizeof typed.text, "d!");
    rename.handle(typed);
    press.key = key::kReturn;
    CHECK(rename.handle(press) == tpw::WindowResult::kChose && !rename.isOpen());
    CHECK(rename.object() == 3 && rename.typed() == "Dizzy Droid!");

    // Attraction Finance Controls: on to the entry price, set to an amount,
    // + twice and the tick sets it.
    using Prices = tpw::AttractionFinanceWindow;
    Prices prices;
    prices.open();
    prices.handle(at(EventType::kMouseDown, Prices::placeOf(Prices::Control::kKindLeft)), park);
    prices.handle(at(EventType::kMouseDown, Prices::placeOf(Prices::Control::kLeft)), park);
    for (int i = 0; i < 2; ++i)
        prices.handle(at(EventType::kMouseDown, Prices::placeOf(Prices::Control::kPlus)), park);
    CHECK(prices.handle(at(EventType::kMouseDown, Prices::placeOf(Prices::Control::kOk)), park) ==
              tpw::WindowResult::kChose &&
          park.admissionFee() == 2);
    prices.handle(at(EventType::kMouseDown, Prices::placeOf(Prices::Control::kClose)), park);
    CHECK(!prices.isOpen());

    // The loans: with none on offer, there's nothing to take.
    using Loans = tpw::LoansWindow;
    Loans loans;
    loans.open();
    loans.handle(at(EventType::kMouseDown, Loans::placeOf(Loans::Control::kAct)), park);
    CHECK(park.loans().empty() && loans.handle(at(EventType::kMouseDown, Loans::placeOf(Loans::Control::kOk)), park) ==
                                      tpw::WindowResult::kClosed);

    tpw::MessageWindow message;
    message.open({262, 263, {}, {}});
    CHECK(message.handle(at(EventType::kMouseDown, message.okRect())) == tpw::WindowResult::kClosed &&
          !message.isOpen());

    // The game's menu: loading, saving, resuming answer; choosing a new
    // player and quitting once said yes to; going online doesn't; Esc
    // resumes, or takes back a question.
    tpw::GameMenu game;
    game.open();
    CHECK(game.handle(at(EventType::kMouseDown, tpw::GameMenu::itemRect(3))) == tpw::GameMenu::Choice::kNone &&
          game.isOpen());
    CHECK(game.handle(at(EventType::kMouseDown, tpw::GameMenu::itemRect(2))) == tpw::GameMenu::Choice::kNone &&
          game.isOpen());
    CHECK(game.handle(at(EventType::kMouseDown, tpw::GameMenu::askRect(false))) == tpw::GameMenu::Choice::kNone &&
          game.isOpen());
    CHECK(game.handle(at(EventType::kMouseDown, tpw::GameMenu::itemRect(2))) == tpw::GameMenu::Choice::kNone);
    CHECK(game.handle(at(EventType::kMouseDown, tpw::GameMenu::askRect(true))) == tpw::GameMenu::Choice::kNewPlayer &&
          !game.isOpen());
    game.open();
    CHECK(game.handle(at(EventType::kMouseDown, tpw::GameMenu::itemRect(0))) == tpw::GameMenu::Choice::kLoad);
    game.open();
    CHECK(game.handle(at(EventType::kMouseDown, tpw::GameMenu::itemRect(1))) == tpw::GameMenu::Choice::kSave);
    game.open();
    CHECK(game.handle(at(EventType::kMouseDown, tpw::GameMenu::itemRect(6))) == tpw::GameMenu::Choice::kResume &&
          !game.isOpen());
    game.open();
    CHECK(game.handle(at(EventType::kMouseDown, tpw::GameMenu::itemRect(7))) == tpw::GameMenu::Choice::kNone);
    CHECK(game.handle(at(EventType::kMouseDown, tpw::GameMenu::askRect(true))) == tpw::GameMenu::Choice::kQuit);
    game.open();
    Event escape;
    escape.type = EventType::kKeyDown;
    escape.key = key::kEscape;
    CHECK(game.handle(escape) == tpw::GameMenu::Choice::kResume && !game.isOpen());

    // The Message Center: the second listed deleted, the first left.
    std::vector<tpw::MessageCenterWindow::Received> inbox{{{262, 263, {}, {}, false}, 3, false},
                                                          {{168, 0, {}, {}, true}, 9, false}};
    using Center = tpw::MessageCenterWindow;
    Center center;
    center.open();
    center.handle(at(EventType::kMouseDown, Center::placeOf(Center::Control::kRow, 1)), inbox);
    center.handle(at(EventType::kMouseDown, Center::placeOf(Center::Control::kDelete)), inbox);
    CHECK(inbox.size() == 1 && inbox[0].message.title == 168);
    CHECK(center.handle(at(EventType::kMouseDown, Center::placeOf(Center::Control::kOk)), inbox) ==
              tpw::WindowResult::kClosed &&
          !center.isOpen());

    // The Park Map: a cell's place on it is that cell's; a click there chooses it.
    using Map = tpw::ParkMapWindow;
    const tpw::Park::Cell cell{47, 17};
    const Map::Spot spot = Map::placeOf(cell);
    CHECK(Map::cellAt(spot.x, spot.y) == cell);
    Map map;
    map.open();
    CHECK(map.handle(at(EventType::kMouseDown, {spot.x, spot.y, 1, 1})) == tpw::WindowResult::kChose &&
          map.target() == cell);
    CHECK(map.handle(at(EventType::kMouseDown, Map::okRect())) == tpw::WindowResult::kClosed);
    map.open(Map::Mode::kOpinion);
    CHECK(map.isOpen() && map.mode() == Map::Mode::kOpinion);
    // Its layers' buttons turn them on and off.
    CHECK(map.shows(Map::Layer::kAttractions) && !map.shows(Map::Layer::kProblems));
    map.handle(at(EventType::kMouseDown, Map::layerRect(Map::Layer::kProblems)));
    map.handle(at(EventType::kMouseDown, Map::layerRect(Map::Layer::kAttractions)));
    CHECK(!map.shows(Map::Layer::kAttractions) && map.shows(Map::Layer::kProblems) && map.isOpen());

    // The advisor says each cue's line once, one after another.
    tpw::Advisor advisor;
    advisor.cue(tpw::Advisor::Cue::kBuyWindow);
    advisor.cue(tpw::Advisor::Cue::kBuyWindow);
    advisor.cue(tpw::Advisor::Cue::kPathTool);
    CHECK(advisor.talking() && advisor.said() == std::vector<uint32_t>{380});
    advisor.advance(6000);
    CHECK(advisor.said() == (std::vector<uint32_t>{380, 376}));
    advisor.advance(6000);
    CHECK(!advisor.talking());

    // Saving: New Save, then its name typed; an old save once it's said
    // to replace it. Loading: a save chosen, then the tick.
    using SaveLoad = tpw::SaveLoadWindow;
    const auto click = [&](SaveLoad &w, SaveLoad::Control c, size_t i = 0) {
        return w.handle(at(EventType::kMouseDown, SaveLoad::placeOf(c, i)));
    };
    SaveLoad saving;
    saving.open(SaveLoad::Mode::kSave, {{"Old", "1.Jan.00"}});
    click(saving, SaveLoad::Control::kRow, 0);
    CHECK(click(saving, SaveLoad::Control::kOk) == tpw::WindowResult::kNone && saving.naming());
    CHECK(click(saving, SaveLoad::Control::kOk) == tpw::WindowResult::kNone && saving.isOpen());
    Event name{};
    name.type = EventType::kTextInput;
    std::strcpy(name.text, "Mine");
    saving.handle(name);
    CHECK(click(saving, SaveLoad::Control::kOk) == tpw::WindowResult::kChose && saving.chosen() == -1 &&
          saving.typed() == "Mine");
    saving.open(SaveLoad::Mode::kSave, {{"Old", "1.Jan.00"}});
    click(saving, SaveLoad::Control::kRow, 1);
    CHECK(click(saving, SaveLoad::Control::kOk) == tpw::WindowResult::kNone && !saving.naming());
    CHECK(click(saving, SaveLoad::Control::kOk) == tpw::WindowResult::kChose && saving.chosen() == 0);
    SaveLoad loading;
    loading.open(SaveLoad::Mode::kLoad, {{"Old", "1.Jan.00"}});
    CHECK(click(loading, SaveLoad::Control::kOk) == tpw::WindowResult::kNone);
    click(loading, SaveLoad::Control::kRow, 0);
    CHECK(click(loading, SaveLoad::Control::kOk) == tpw::WindowResult::kChose && loading.chosen() == 0);
    loading.open(SaveLoad::Mode::kLoad, {});
    CHECK(click(loading, SaveLoad::Control::kCancel) == tpw::WindowResult::kClosed);
    // Deleting: a save chosen, the bin, then the tick says yes.
    loading.open(SaveLoad::Mode::kLoad, {{"Old", "1.Jan.00"}, {"Older", "1.Jan.00"}});
    click(loading, SaveLoad::Control::kDelete);
    CHECK(loading.takeDelete() == -1);
    click(loading, SaveLoad::Control::kRow, 1);
    click(loading, SaveLoad::Control::kDelete);
    CHECK(loading.takeDelete() == -1 && click(loading, SaveLoad::Control::kOk) == tpw::WindowResult::kNone);
    CHECK(loading.takeDelete() == 1 && loading.isOpen());

    // Choosing an ability: one already theirs is said so; another they'd
    // lose is asked about first; none to learn, the tick sends them anyway.
    using Ability = tpw::AbilityWindow;
    Ability ability;
    const auto pick = [&](Ability::Control c, size_t i = 0) {
        return ability.handle(at(EventType::kMouseDown, Ability::placeOf(c, i)));
    };
    ability.open(7, tpw::Park::StaffKind::kGardener, 10, {10, 11});
    CHECK(pick(Ability::Control::kOk) == tpw::WindowResult::kNone);
    pick(Ability::Control::kRow, 0);
    CHECK(pick(Ability::Control::kOk) == tpw::WindowResult::kNone && ability.isOpen());
    pick(Ability::Control::kRow, 1);
    CHECK(pick(Ability::Control::kOk) == tpw::WindowResult::kNone && ability.isOpen());
    CHECK(pick(Ability::Control::kOk) == tpw::WindowResult::kChose && ability.chosen() == 11 && ability.staff() == 7);
    ability.open(8, tpw::Park::StaffKind::kScientist, 0, {});
    CHECK(pick(Ability::Control::kOk) == tpw::WindowResult::kChose && ability.chosen() == 0);

    // Choosing who clears an obstacle: each click chooses, or unchooses.
    using Employee = tpw::EmployeeWindow;
    Employee employees;
    employees.open({4, 5, 6});
    employees.handle(at(EventType::kMouseDown, Employee::placeOf(Employee::Control::kRow, 0)));
    employees.handle(at(EventType::kMouseDown, Employee::placeOf(Employee::Control::kRow, 2)));
    employees.handle(at(EventType::kMouseDown, Employee::placeOf(Employee::Control::kRow, 0)));
    CHECK(employees.handle(at(EventType::kMouseDown, Employee::placeOf(Employee::Control::kOk))) ==
              tpw::WindowResult::kChose &&
          employees.chosen() == std::vector<int>{6});

    // Options: the volumes, the speed and the advisor change, the rest don't.
    using Options = tpw::OptionsWindow;
    Options options;
    options.open(tpw::GameOptions{});
    const auto option = [&](Options::Control c, size_t i = 0) {
        return options.handle(at(EventType::kMouseDown, Options::placeOf(c, i)));
    };
    option(Options::Control::kTab, 1);
    CHECK(option(Options::Control::kMinus, 1) == tpw::WindowResult::kChose && options.options().volume[1] == 90);
    CHECK(option(Options::Control::kPlus, 0) == tpw::WindowResult::kNone && options.options().volume[0] == 100);
    option(Options::Control::kTab, 2);
    CHECK(option(Options::Control::kPlus, Options::kSpeedRow) == tpw::WindowResult::kChose &&
          options.options().gameSpeed == 80);
    CHECK(option(Options::Control::kAdvisor) == tpw::WindowResult::kChose && !options.options().advisor);
    CHECK(option(Options::Control::kOk) == tpw::WindowResult::kClosed && !options.isOpen());

    // The Overview Map goes to another open world, not a closed one or
    // the park's own.
    using Overview = tpw::OverviewMapWindow;
    Overview overview;
    overview.open({{1, true, 2, 10}, {2, false, 0, 10}, {0, true, 0, 10}}, 1);
    CHECK(overview.handle(at(EventType::kMouseDown, Overview::rowRect(0))) == tpw::WindowResult::kNone);
    CHECK(overview.handle(at(EventType::kMouseDown, Overview::rowRect(1))) == tpw::WindowResult::kNone);
    CHECK(overview.handle(at(EventType::kMouseDown, Overview::rowRect(2))) == tpw::WindowResult::kChose &&
          overview.chosen() == 0 && !overview.isOpen());
    overview.open({{1, true, 0, 10}}, 1);
    CHECK(overview.handle(at(EventType::kMouseDown, Overview::okRect())) == tpw::WindowResult::kClosed);

    // Visitor Statistics and the visitors' list close by their buttons;
    // a visitor's arrows do nothing with none in the park.
    tpw::VisitorStatsWindow stats;
    stats.open();
    stats.handle(
        at(EventType::kMouseDown, tpw::VisitorStatsWindow::placeOf(tpw::VisitorStatsWindow::Control::kTab, 2)));
    CHECK(stats.handle(
              at(EventType::kMouseDown, tpw::VisitorStatsWindow::placeOf(tpw::VisitorStatsWindow::Control::kClose))) ==
          tpw::WindowResult::kClosed);
    tpw::VisitorWindow visitor;
    visitor.open(7);
    visitor.handle(at(EventType::kMouseDown, tpw::VisitorWindow::placeOf(tpw::VisitorWindow::Control::kNext)), park);
    CHECK(visitor.visitor() == 7 && visitor.isOpen());
    tpw::VisitorListWindow visitors;
    visitors.open();
    CHECK(visitors.handle(
              at(EventType::kMouseDown, tpw::VisitorListWindow::placeOf(tpw::VisitorListWindow::Control::kOk)), park) ==
          tpw::WindowResult::kClosed);

    tpw::FinanceSummaryWindow summary;
    summary.open(0, "Dizzy Droid");
    CHECK(summary.handle(at(EventType::kMouseDown, tpw::FinanceSummaryWindow::okRect())) ==
              tpw::WindowResult::kClosed &&
          !summary.isOpen());
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
                              {"briefing", [](const std::filesystem::path &) { testBriefingControls(); }},
                              {"park-hud", [](const std::filesystem::path &) { testParkHud(); }},
                              {"buy-window", [](const std::filesystem::path &) { testBuyWindow(); }},
                              {"object-names", [](const std::filesystem::path &) { testObjectNames(); }},
                              {"hire-window", [](const std::filesystem::path &) { testHireWindow(); }},
                              {"park-windows", [](const std::filesystem::path &) { testParkWindows(); }},
                          });
