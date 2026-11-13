// Opens each of the game's windows over a real game and saves the screen.
#include <fstream>
#include <cstdio>
#include <array>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <filesystem>
#include <mutex>
#include <atomic>
#include <thread>
#include <optional>
#include <string_view>
#include "audio/mixer.h"
#include "video/avi.h"
#include "engines/clue/game_data.h"
#include "engines/clue/ui/ui.h"
#define private public
#include "engines/clue/game/play.h"
#undef private
using namespace reluva; using namespace reluva::clue;
struct Null : System {
    Error initGraphics(int, int, PixelFormat) override { return Error::success(); }
    void setPalette(const Color*, int, int) override {}
    void setWindowTitle(const std::string&) override {}
    bool pollEvent(Event&) override { return false; }
    uint64_t getMillis() const override { return 0; }
    void delayMillis(uint32_t) override {}
    Error startAudio(AudioCallback) override { return Error::fail("no"); }
    void stopAudio() override {}
    int audioRate() const override { return 44100; }
    void updateScreen(const Surface&) override {}
};
static void save(const Surface& s, const std::string& f) {
    std::ofstream o(f, std::ios::binary); o << "P6\n640 480\n255\n";
    for (int y = 0; y < 480; ++y) for (int x = 0; x < 640; ++x) o.write((const char*)s.row(y) + x * 4, 3);
}
int main(int argc, char** argv) {
    GameData data; data.open(argv[1], Language::US);
    Resi resi; { std::ifstream in(*data.find("dialogInfo.res"), std::ios::binary); std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), {}); parseResi(b, resi); }
    Fonts fonts(data, resi);
    DataDrive f; f.open(*data.find("clue.lvl")); Level lvl; parseLevel(f, lvl);
    Mansion m; m.build(data, lvl);
    Null sys; Surface s; s.create(640, 480, PixelFormat::RGBA32);
    Play play(sys, s, data, nullptr, resi, fonts, m, lvl);
    std::array<Player, 6> players; players[0].kind = Player::Human; players[1].kind = Player::Computer; players[2].kind = Player::Human;
    play.start(players, false, 5);
    uint64_t t = 1000; for (int i = 0; i < 200; ++i) { play.update(t += 16); }
    const std::string out = argv[2];
    play.panel_ = Play::Panel::Cards; play.panelOwner_ = 0; play.draw(); save(s, out + "/1_cards.ppm");
    play.panel_ = Play::Panel::Notepad; play.notes_[0][3] = {3, {3, 5}, "seen in hall"}; play.notes_[0][8] = {1, {2, 2}, {}}; play.editing_ = 3; play.draw(); save(s, out + "/2_notepad.ppm"); play.editing_ = -1;
    { for (int k=0;k<6;++k) std::printf("walker %d %d,%d anchor screen %d,%d\n", k, play.walkers_[k].x, play.walkers_[k].y, play.walkers_[k].x-play.walkers_[k].y-play.camX_, (play.walkers_[k].x+play.walkers_[k].y)/2-play.camY_); PlayLook lk; lk.mansion.lighting = true; play.setLook(lk); play.draw(); save(s, out + "/14_lit.ppm"); play.setLook(PlayLook()); }
    play.map_ = true; play.game_.rollDie(); play.walkers_[3].x += 300;
    { Event e; e.button = MouseButton::Left; e.mouseX = 208 + 8 + (29 - 9) * 13 + 6; e.mouseY = 5 + 15 + (12 - 9) * 13 + 6;
      e.type = EventType::MouseDown; play.handleEvent(e); e.type = EventType::MouseUp; play.handleEvent(e); }
    std::printf("map plan %zu roll %d\n", play.plan_.size(), play.game_.roll());
    play.draw(); save(s, out + "/13_map.ppm"); play.map_ = false; play.plan_.clear(); play.game_.phase_ = Game::Phase::Start;
    play.panel_ = Play::Panel::None;
    play.openChoose(true); play.draw(); save(s, out + "/3_choose_suspect.ppm");
    play.clickChoose(4); play.draw(); save(s, out + "/4_choose_weapon.ppm");
    play.clickChoose(13); play.draw(); save(s, out + "/5_choose_room.ppm");
    play.clickChoose(22); play.draw(); save(s, out + "/6_choose_summary.ppm");
    play.window_ = Play::Window::None; play.dialog_.reset();
    play.showTold(1, true); play.draw(); save(s, out + "/7_told.ppm"); play.told_.reset();
    // A suggestion White can answer: make one with a card of hers.
    Game& g = play.game_;
    g.phase_ = Game::Phase::Disprove; g.disprover_ = 2; int c = g.seats_[2].hand[0];
    g.suggestion_ = {c < 6 ? c : 0, c >= 6 && c < 12 ? c - 6 : 0, c >= 12 ? c - 12 : 0};
    play.openPick(); play.draw(); save(s, out + "/8_pick.ppm");
    play.clickPick(play.pickSlot(0)); play.draw(); save(s, out + "/9_picked.ppm");
    play.window_ = Play::Window::None; play.dialog_.reset();
    g.shown_ = 15; play.openShown(); play.draw(); save(s, out + "/10_shown.ppm");
    play.showSolution(6); play.draw(); save(s, out + "/11_solution.ppm");
    play.viewerOut_ = 0; play.openLoser(); play.draw(); save(s, out + "/12_loser.ppm");
    // A human's suggestion, by clicks.
    {
        Play p2(sys, s, data, nullptr, resi, fonts, m, lvl);
        std::array<Player, 6> pl; pl[0].kind = Player::Human; pl[1].kind = Player::Computer; pl[4].kind = Player::Computer;
        p2.start(pl, false, 11);
        uint64_t t2 = 1000; for (int i = 0; i < 100; ++i) p2.update(t2 += 16);
        Game& g2 = p2.game_; g2.places_[0] = {{}, Lounge}; g2.phase_ = Game::Phase::Moved; g2.enteredRoom_ = true; p2.placeWalkers(false); p2.centreOn(0);
        auto click = [&](int x, int y) { Event e; e.mouseX = x; e.mouseY = y; e.button = MouseButton::Left; e.type = EventType::MouseDown; p2.handleEvent(e); e.type = EventType::MouseUp; p2.handleEvent(e); };
        click(574 + 13, 440); std::printf("after bulb: window %d\n", int(p2.window_));
        click(434 + 2 + 30, 4 + 21 + 80);   // Scarlet? (2,21)
        std::printf("page %d suspect %d\n", p2.choosePage_, p2.chosen_.suspect);
        click(434 + 7 + 40, 4 + 48 + 50);   // knife
        std::printf("page %d weapon %d\n", p2.choosePage_, p2.chosen_.weapon);
        p2.draw(); save(s, out + "/20_summary.ppm");
        click(434 + 7 + 40, 4 + 337 + 15);  // Suggest
        std::printf("suggested: phase %d disprover %d\n", int(g2.phase()), g2.disprover());
        for (int i = 0; i < 2000 && p2.window_ != Play::Window::Shown && !(g2.phase() == Game::Phase::Suggested && p2.steps_.empty()); ++i) p2.update(t2 += 16);
        p2.draw(); save(s, out + "/21_after.ppm");
        std::printf("window %d shown %d ticker '%s'\n", int(p2.window_), g2.shown(), p2.ticker_.c_str());
        click(398 + 90 + 20, 4 + 386 + 8); // OK
        for (int i = 0; i < 50; ++i) p2.update(t2 += 16);
        std::printf("after OK: window %d phase %d ticker '%s'\n", int(p2.window_), int(g2.phase()), p2.ticker_.c_str());
        click(535 + 16, 440); // end the turn
        for (int i = 0; i < 100; ++i) p2.update(t2 += 16);
        std::printf("turn now %d ticker '%s'\n", g2.turn(), p2.ticker_.c_str());
    }
    // Hot spots.
    for (const char* name : {"billiardroom:Obrbear_ad", "lounge:Olglamp", "ballroom:Oblgpiano_ad"}) {
        Play p4(sys, s, data, nullptr, resi, fonts, m, lvl);
        std::array<Player, 6> pl; pl[0].kind = Player::Human; pl[1].kind = Player::Computer; pl[2].kind = Player::Computer;
        p4.start(pl, false, 3); uint64_t t4 = 1000; for (int i = 0; i < 50; ++i) p4.update(t4 += 16);
        p4.game_.phase_ = Game::Phase::Moved;
        int hx = 0, hy = 0;
        for (const auto& it : m.items()) if (it.object->name == name) { hx = it.rect.x + it.rect.w / 2; hy = it.rect.y + it.rect.h / 2; }
        p4.camX_ = hx - 320; p4.camY_ = hy - 200;
        int found = -1;
        for (int dy = -40; dy <= 40 && found < 0; dy += 4) for (int dx = -40; dx <= 40 && found < 0; dx += 4) {
            int i = m.itemAt(hx + dx, hy + dy, p4.look_.mansion, t4); if (i >= 0 && m.items()[i].object->name == name) { found = i; hx += dx; hy += dy; } }
        bool ok = found >= 0 && p4.reactAt(hx - p4.camX_, hy - p4.camY_, false);
        std::printf("%s: item %d clicked %d\n", name, found, int(ok));
        for (int k = 0; k < 4; ++k) { for (int i = 0; i < 25; ++i) p4.update(t4 += 16); p4.draw(); save(s, out + "/hot_" + std::string(name).substr(std::string(name).find(':') + 2) + std::to_string(k) + ".ppm"); }
    }
    // Endings.
    auto ending = [&](const char* what, std::array<Player, 6> pl, bool right) {
        Play p3(sys, s, data, nullptr, resi, fonts, m, lvl);
        p3.start(pl, false, 21);
        uint64_t t3 = 1000; for (int i = 0; i < 200; ++i) p3.update(t3 += 16);
        Accusation a = p3.game_.solution(); if (!right) a.room = (a.room + 1) % 9;
        p3.game_.phase_ = Game::Phase::Moved;
        p3.accusationMade(a);
        for (int i = 0; i < 600 && p3.window_ == Play::Window::None; ++i) p3.update(t3 += 16);
        std::printf("%s: window %d text %d over %d", what, int(p3.window_), p3.solutionText_, int(p3.game_.phase() == Game::Phase::Over));
        if (p3.dialog_) { Event e; e.button = MouseButton::Left; e.mouseX = 530; e.mouseY = 436; e.type = EventType::MouseDown; p3.handleEvent(e); e.type = EventType::MouseUp; p3.handleEvent(e); }
        for (int i = 0; i < 2000 && !p3.finished_; ++i) p3.update(t3 += 16);
        std::printf(" -> finished %d, turn %d\n", int(p3.finished_), p3.game_.turn());
    };
    std::array<Player, 6> one; one[0].kind = Player::Human; one[1].kind = Player::Computer; one[2].kind = Player::Computer;
    std::array<Player, 6> two = one; two[1].kind = Player::Human;
    ending("lone human wrong", one, false);
    ending("lone human right", one, true);
    ending("two humans, first wrong", two, false);
    std::printf("done\n");
}
