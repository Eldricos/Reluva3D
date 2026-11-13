// Walks into the rooms, by Clue.exe's way-finder (0x41A0C0): from each
// door's square to the place found for the suspect (0x4242A2), and out of
// each secret passage (0x425CC0). Prints each way, and checks it: every
// step to a free cell of the room, a slanting one only between free cells,
// and how many ways a straight line would have taken through furniture.
#include "audio/mixer.h"
#include "engines/clue/game_data.h"
#include "engines/clue/ui.h"
#define private public
#include "engines/clue/rules.h"
#include "engines/clue/play.h"
#undef private
#include <cstdio>
#include <fstream>
#include <iterator>
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
static int cell(int v) { return (v + (v < 0 ? 31 : 0)) >> 5; }
int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: doorwalk <game folder>\n"); return 2; }
    GameData data; data.open(argv[1], Language::US);
    Resi resi; { std::ifstream in(*data.find("dialogInfo.res"), std::ios::binary); std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), {}); parseResi(b, resi); }
    Fonts fonts(data, resi);
    DataDrive f; f.open(*data.find("clue.lvl")); Level lvl; parseLevel(f, lvl);
    Mansion m; m.build(data, lvl);
    Null sys; Surface s; s.create(640, 480, PixelFormat::RGBA32);
    Play p(sys, s, data, nullptr, resi, fonts, m, lvl);
    std::array<Player, 6> pl; pl[0].kind = Player::Human; pl[1].kind = Player::Computer; pl[2].kind = Player::Computer;
    p.start(pl, false, 3); uint64_t t = 1000; for (int i = 0; i < 50; ++i) p.update(t += 16);
    int ways = 0, bad = 0, straightThrough = 0, none = 0;
    // A route's check: each step to a free cell, in the room, one cell away;
    // slanting only between free cells.
    auto check = [&](int room, int x0, int y0, const std::deque<std::pair<int, int>>& r) {
        int cx = cell(x0), cy = cell(y0); bool ok = true;
        for (auto& q : r) {
            const int nx = cell(q.first), ny = cell(q.second);
            const int dx = nx - cx, dy = ny - cy;
            if (std::abs(dx) > 1 || std::abs(dy) > 1 || p.blocked_.count({nx, ny}) ||
                p.board_.roomAt(Board::squareAt(nx * 32 + 16, ny * 32 + 16)) != room)
                ok = false;
            if (dx && dy && (p.blocked_.count({cx + dx, cy}) || p.blocked_.count({cx, cy + dy})))
                ok = false;
            cx = nx; cy = ny;
        }
        return ok;
    };
    // Would a straight line from a to b cross a blocked cell?
    auto throughThings = [&](int ax, int ay, int bx, int by) {
        for (int k = 1; k < 64; ++k) {
            const int x = ax + (bx - ax) * k / 64, y = ay + (by - ay) * k / 64;
            if (p.blocked_.count({cell(x), cell(y)})) return true;
        }
        return false;
    };
    for (const Board::Door& d : p.board_.doors()) {
        // Miss Scarlet on the square outside, then into the room.
        auto& w = p.walkers_[0];
        p.game_.places_[0] = Place{d.outside, kNoRoom};
        Board::standingPoint(d.outside, w.x, w.y);
        w.route.clear(); w.walking = false;
        p.game_.places_[0] = Place{{}, d.room};
        p.startWalk({Place{d.outside, kNoRoom}, Place{{}, d.room}});
        const auto route = w.route;
        // route[0] is the square outside, route[1] the door's inside square;
        // the rest the way in.
        std::deque<std::pair<int, int>> way(route.begin() + 2, route.end());
        const int x0 = Board::kGridOrigin + d.inside.x * Board::kSquareSize;
        const int y0 = Board::kGridOrigin + d.inside.y * Board::kSquareSize;
        const bool ok = !way.empty() && check(d.room, x0, y0, way);
        ++ways; bad += !ok;
        if (way.empty()) ++none;
        if (!way.empty() && throughThings(route[1].first, route[1].second, way.back().first, way.back().second)) ++straightThrough;
        if (getenv("DIAG")) {
            const auto sp = way.empty() ? std::pair<int,int>{0,0} : way.back();
            const int tx = cell(sp.first), ty = cell(sp.second), fx = cell(x0), fy = cell(y0);
            auto rm = [&](int cx, int cy, int off) { return p.board_.roomAt(Board::squareAt(cx * 32 + off, cy * 32 + off)); };
            std::printf("  from cell %d,%d room16 %d blocked %d; to cell %d,%d room16 %d room32 %d blocked %d\n", fx, fy, rm(fx, fy, 16), int(p.blocked_.count({fx, fy})), tx, ty, rm(tx, ty, 16), rm(tx, ty, 32), int(p.blocked_.count({tx, ty})));
            for (int dy = -1; dy <= 2; ++dy) { std::printf("   "); for (int dx = -1; dx <= 2; ++dx) std::printf(" %c%d", p.blocked_.count({fx+dx, fy+dy}) ? '#' : '.', rm(fx+dx, fy+dy, 16)); std::printf("\n"); }
        }
        std::printf("room %d door (%d,%d): %zu steps%s:", d.room, d.inside.x, d.inside.y, way.size(), ok ? "" : " BAD");
        for (auto& q : way) std::printf(" (%d,%d)", q.first, q.second);
        std::printf("\n");
        w.route.clear(); w.walking = false;
        for (int i = 0; i < 600; ++i) p.update(t += 16);
    }
    // Out of each passage.
    for (const auto& ps : p.passages_) {
        auto& w = p.walkers_[0];
        w.x = w.y = -100000; w.route.clear(); w.walking = false;
        p.game_.places_[0] = Place{{}, ps.room};
        p.game_.turn_ = 0;
        p.placeWalkers(true);
        std::printf("passage into room %d: %s, %zu steps, ends at %d,%d\n", ps.room, w.walking ? "walks" : "at once",
                    w.route.size(), w.route.empty() ? w.x : w.route.back().first, w.route.empty() ? w.y : w.route.back().second);
        w.route.clear(); w.walking = false;
    }
    std::printf("%d doors: %d ways with a bad step, %d with no way (straight), %d where a straight line crosses furniture\n",
                ways, bad, none, straightThrough);
    return bad ? 1 : 0;
}
