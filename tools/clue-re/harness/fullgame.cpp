// Opens each of the game's windows over a real game and saves the screen.
#include <fstream>
#include <algorithm>
#include <algorithm>
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
#include "common/audio/mixer.h"
#include "common/video/avi.h"
#include "engines/clue/game_data.h"
#include "engines/clue/ui.h"
#define private public
#include "engines/clue/play.h"
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
int mainOld(int argc, char** argv);
int main(int argc, char** argv) {
    GameData data; data.open(argv[1], Language::US);
    Resi resi; { std::ifstream in(*data.find("dialogInfo.res"), std::ios::binary); std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), {}); parseResi(b, resi); }
    Fonts fonts(data, resi);
    DataDrive f; f.open(*data.find("clue.lvl")); Level lvl; parseLevel(f, lvl);
    Mansion m; m.build(data, lvl);
    Null sys; Surface s; s.create(640, 480, PixelFormat::RGBA32);
    for (uint32_t seed = 1; seed <= std::atoi(argv[3]); ++seed) {
    Play play(sys, s, data, nullptr, resi, fonts, m, lvl);
    std::array<Player, 6> players; for (int k = 0; k < 6; ++k) players[k].kind = (seed + k) % 3 ? Player::Computer : Player::None;
    if (play.start(players, seed % 2, seed)) { std::printf("start failed\n"); continue; }
    uint64_t t = 1000; int turns = 0, last = -1;
    uint64_t since = t; bool sugg = false; std::vector<double> plain, withSugg;
    while (t < 1000 + 4ull * 3600 * 1000 && play.window_ != Play::Window::Solution) { play.update(t += 33); if (play.game_.phase() == Game::Phase::Disprove || play.game_.phase() == Game::Phase::Suggested) sugg = true; if (play.game_.turn() != last) { if (last >= 0) (sugg ? withSugg : plain).push_back((t - since) / 1000.0); since = t; sugg = false; ++turns; last = play.game_.turn(); } }
    auto avg = [](std::vector<double>& v) { std::sort(v.begin(), v.end()); return v.empty() ? 0.0 : v[v.size() / 2]; };
    std::printf("median turn without a suggestion %.1f s (%zu), with one %.1f s (%zu)\n", avg(plain), plain.size(), avg(withSugg), withSugg.size());
    std::printf("seed %u: %s after %.0f min, %d turns, winner %d\n", seed, play.window_ == Play::Window::Solution ? "ended" : "NOT ENDED", (t - 1000) / 60000.0, turns, play.game_.winner());
    if (seed == 1) { play.draw(); save(s, std::string(argv[2]) + "/end.ppm"); }
    }
    return 0;
}
int mainOld(int argc, char** argv) {
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
    play.panel_ = Play::Panel::Notepad; play.notes_[0][3].colour = 3; play.draw(); save(s, out + "/2_notepad.ppm");
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
    std::printf("done\n");
}
