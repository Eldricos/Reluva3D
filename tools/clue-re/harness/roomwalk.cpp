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
#include "engines/clue/ui.h"
#define private public
#include "engines/clue/rules.h"
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
// Walks about a room (Clue.exe's way-finder, 0x41A0C0): the way to a few
// points of the billiard room from where Miss Scarlet stands, and the walk
// played out by the RoomWalk command.
int main(int argc, char** argv) {
    GameData data; data.open(argv[1], Language::US);
    Resi resi; { std::ifstream in(*data.find("dialogInfo.res"), std::ios::binary); std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), {}); parseResi(b, resi); }
    Fonts fonts(data, resi);
    DataDrive f; f.open(*data.find("clue.lvl")); Level lvl; parseLevel(f, lvl);
    Mansion m; m.build(data, lvl);
    Null sys; Surface s; s.create(640, 480, PixelFormat::RGBA32);
    Play p(sys, s, data, nullptr, resi, fonts, m, lvl);
    std::array<Player, 6> pl; pl[0].kind = Player::Human; pl[1].kind = Player::Computer; pl[2].kind = Player::Computer;
    p.start(pl, false, 3); uint64_t t = 1000; for (int i = 0; i < 50; ++i) p.update(t += 16);
    const int room = argc > 2 ? std::atoi(argv[2]) : 6;
    p.game_.places_[0] = Place{{}, room};
    p.walkers_[0].x = p.walkers_[0].y = -100000; // off the board: placed afresh in the room
    p.placeWalkers(false);
    const Play::Walker& w = p.walkers_[0];
    // Points of the room: each 32-unit cell's middle, a few of them.
    std::vector<std::pair<int,int>> cells;
    for (int y = 0; y < 3000; y += 32) for (int x = 0; x < 3000; x += 32)
        if (p.board_.roomAt(Board::squareAt(x + 16, y + 16)) == room && !p.blocked_.count({x / 32, y / 32})) cells.push_back({x + 16, y + 16});
    std::printf("%zu free cells in the room\n", cells.size());
    p.walkers_[0].x = cells[0].first; p.walkers_[0].y = cells[0].second; // a free cell of the room
    { const int cx = w.x / 32, cy = w.y / 32; std::printf("Miss Scarlet (turn %d) in room %d at %d,%d\n", p.game_.turn(), room, w.x, w.y); std::printf("start cell %d,%d blocked %d room %d\n", cx, cy, int(p.blocked_.count({cx, cy})), p.board_.roomAt(Board::squareAt(cx * 32 + 16, cy * 32 + 16))); }
    int reachable = 0, shown = 0;
    for (size_t i = 0; i < cells.size(); i += 7) {
        auto r = p.roomRoute(cells[i].first, cells[i].second);
        if (!r.empty()) ++reachable;
        if (shown < 4 && r.size() > 3) {
            ++shown;
            std::printf("to %d,%d:", cells[i].first, cells[i].second);
            for (auto& q : r) std::printf(" (%d,%d)", q.first, q.second);
            std::printf("\n");
        }
    }
    std::printf("reachable %d of %zu tried\n", reachable, (cells.size() + 6) / 7);
    // The walk, as a command.
    const auto goal = cells[cells.size() / 2];
    netplay::Command c; c.type = netplay::Command::RoomWalk; c.seat = 0; c.args = {int16_t(goal.first), int16_t(goal.second)};
    p.apply(c);
    for (int i = 0; i < 600 && p.walkers_[0].walking; ++i) p.update(t += 16);
    std::printf("walked to %d,%d (asked %d,%d), still walking %d, game place room %d\n", w.x, w.y, goal.first, goal.second, int(w.walking), p.game_.place(0).room);
}
