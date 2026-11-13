// Three machines in one process, over the in-memory network, each running the real Play: their
// humans played by scripts, each machine at its own frame rate. They must play the same game.
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
#include <fstream>
#include <filesystem>
#include <optional>
#include <string_view>
#include "audio/mixer.h"
#include "video/avi.h"
#include "engines/clue/game_data.h"
#include "engines/clue/ui/ui.h"
#define private public
#include "engines/clue/game/play.h"
#undef private
#include <cstdio>
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
static bool lossy(const net::Address&, const net::Address&, size_t i) { uint64_t h = i * 0x9E3779B97F4A7C15ull; h ^= h >> 29; h *= 0xBF58476D1CE4E5B9ull; h ^= h >> 32; return h % 4 == 0; }
int main(int argc, char** argv) {
    GameData data; data.open(argv[1], Language::US);
    Resi resi; { std::ifstream in(*data.find("dialogInfo.res"), std::ios::binary); std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), {}); parseResi(b, resi); }
    Fonts fonts(data, resi);
    DataDrive f; f.open(*data.find("clue.lvl")); Level lvl; parseLevel(f, lvl);
    const int games = std::atoi(argv[2]); const bool drop = argc > 3;
    for (int g = 0; g < games; ++g) {
        auto network = net::LoopbackNetwork::create();
        if (g % 2) network->setDropFilter(lossy);
        auto host = netplay::Session::host(network->open(netplay::kDefaultPort), "Game", "Ann", 4);
        auto b = netplay::Session::join(network->open(0), {0x7f000001, netplay::kDefaultPort}, "Ben");
        auto c = netplay::Session::join(network->open(0), {0x7f000001, netplay::kDefaultPort}, "Cat");
        std::vector<netplay::Session*> ss{host.get(), b.get(), c.get()};
        uint64_t t = 1000;
        auto pump = [&](int ms) { for (int k = 0; k < ms; k += 20) { t += 20; for (auto* s : ss) s->update(t); } };
        pump(2000);
        host->take(g % 6); b->take((g + 2) % 6); c->take((g + 4) % 6);
        host->setSeat((g + 1) % 6, netplay::Seat::Computer, 2); host->setNoDieRoll(g % 3 == 2);
        pump(2000);
        if (!host->canStart()) { std::printf("game %d: can't start\n", g); continue; }
        host->start(1000 + g); pump(1000);
        std::vector<std::unique_ptr<Mansion>> mansions; std::vector<std::unique_ptr<Play>> plays; std::vector<std::unique_ptr<ComputerPlayer>> bots;
        Null sys; Surface scr; scr.create(640, 480, PixelFormat::RGBA32);
        for (int k = 0; k < 3; ++k) {
            mansions.push_back(std::make_unique<Mansion>()); mansions.back()->build(data, lvl);
            plays.push_back(std::make_unique<Play>(sys, scr, data, nullptr, resi, fonts, *mansions.back(), lvl));
            PlayLook look; look.suggestMovies = false; plays.back()->setLook(look);
            if (plays.back()->startNetwork(*ss[size_t(k)])) { std::printf("start failed\n"); return 1; }
            bots.push_back(std::make_unique<ComputerPlayer>());
        }
        for (int k = 0; k < 3; ++k) for (int s = 0; s < 6; ++s) if (plays[k]->local(s)) bots[k]->reset(plays[k]->game_, s, 77 + g * 3 + k, 2);
        // Each machine its own frame time.
        const int frame[3] = {16, 23, 41};
        std::array<uint64_t, 3> clock{1000, 1000, 1000};
        int steps = 0; bool dropped = false;
        while (steps++ < 400000) {
            t += 7; for (auto* s : ss) if (s) s->update(t);
            for (int k = 0; k < 3; ++k) {
                if (!plays[k]) continue;
                if (clock[k] > t) continue;
                clock[k] += frame[k];
                Play& p = *plays[k];
                p.update(clock[k]);
                // The human here: the script's choices, through the same calls a click makes.
                if (p.window_ == Play::Window::Pick) { p.clickPick(p.pickSlot(0)); p.clickPick(10001); }
                if (p.window_ == Play::Window::Shown || p.window_ == Play::Window::Solution) { p.window_ = Play::Window::None; p.dialog_.reset(); }
                if (p.window_ == Play::Window::Loser) { p.autoDisprove_[size_t(p.viewerOut_)] = true; p.window_ = Play::Window::None; p.dialog_.reset(); }
                if (!p.localTurn() || p.busy() || !p.steps_.empty() || p.pending_ || p.window_ != Play::Window::None || p.game_.phase() == Game::Phase::Over) continue;
                ComputerPlayer& bot = *bots[k];
                const int s = p.game_.turn();
                auto cmd = [&](netplay::Command::Type ty, std::vector<int16_t> a = {}) { netplay::Command c; c.type = ty; c.seat = uint8_t(s); c.args = a; p.perform(c); };
                switch (bot.decide(p.game_)) {
                case ComputerPlayer::Action::Roll: cmd(netplay::Command::Roll); break;
                case ComputerPlayer::Action::Passage: cmd(netplay::Command::Passage); break;
                case ComputerPlayer::Action::Walk: { p.plan_ = bot.chooseWalk(p.game_); p.walkPlan(); break; }
                case ComputerPlayer::Action::Suggest: { auto a = bot.chooseSuggestion(p.game_); cmd(netplay::Command::Suggest, {int16_t(a.suspect), int16_t(a.weapon)}); break; }
                case ComputerPlayer::Action::Accuse: { auto a = bot.chooseAccusation(); cmd(netplay::Command::Accuse, {int16_t(a.suspect), int16_t(a.weapon), int16_t(a.room)}); break; }
                case ComputerPlayer::Action::EndTurn: cmd(netplay::Command::EndTurn); break;
                }
            }
            // Keep the bots' notebooks: they watch each machine's game.
            for (int k = 0; k < 3; ++k) if (plays[k]) {
                static std::array<size_t, 3> seen{};
                (void)seen;
            }
            // A machine gone half way through.
            if (drop && !dropped && plays[2] && plays[2]->game_.turn() == 0 && steps > 20000) { ss[2]->leave(); ss[2] = nullptr; plays[2].reset(); dropped = true; }
            bool over = true; for (auto& p : plays) if (p && p->game_.phase() != Game::Phase::Over && !p->finished_) over = false;
            if (over) break;
        }
        for (int k = 0; k < 3; ++k) if (plays[k]) { auto& p = *plays[k]; std::printf("  m%d turn %d phase %d busy %d steps %zu pending %d await %d incoming %zu window %d session %d\n", k, p.game_.turn(), int(p.game_.phase()), p.busy(), p.steps_.size(), p.pending_, p.awaitingShow_, p.incoming_.size(), int(p.window_), int(ss[k] ? int(ss[k]->state()) : -1)); }
        std::set<std::string> states; int winner = -2;
        for (auto& p : plays) if (p) { std::ostringstream o; p->game_.save(o); states.insert(o.str()); winner = p->game_.winner(); }
        std::printf("game %d (%s, %s): %s after %d steps, winner %d, %zu distinct final states%s\n", g, g % 2 ? "lossy" : "clean",
                    g % 3 == 2 ? "no die" : "die", plays[0]->game_.phase() == Game::Phase::Over ? "over" : "NOT OVER", steps, winner,
                    states.size(), dropped ? ", one machine dropped" : "");
    }
}
