// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Headless driver for checking engine screens: scripted keys, frames saved as PPM.
#include "engines/engine.h"
namespace reluva { const MetaEngine& clueMetaEngine(); } // the Clue engine alone, without the registry
#include "engines/engine.h"
#include "backends/networking/sockets/socket_net.h"
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <string>
#include <vector>
using namespace reluva;
struct Headless : System {
    std::vector<std::string> script; size_t pc = 0; int wait = 0; uint64_t now = 1000;
    std::deque<Event> q; std::string outDir, title; int shots = 0;
    Error initGraphics(int, int, PixelFormat) override { return Error::success(); }
    void setPalette(const Color*, int, int) override {}
    void setWindowTitle(const std::string& t) override { title = t; }
    bool pollEvent(Event& e) override { if (q.empty()) return false; e = q.front(); q.pop_front(); return true; }
    uint64_t getMillis() const override { return now; }
    void delayMillis(uint32_t ms) override { now += ms; }
    Error startAudio(AudioCallback) override { return Error::fail("no audio"); }
    void stopAudio() override {}
    int audioRate() const override { return 44100; }
    Error openUdp(uint16_t port, std::unique_ptr<net::UdpSocket>& out) override { std::string e; out = net::openSystemUdp(port, e); return out ? Error::success() : Error::fail(e); }
    bool resolveHost(const std::string& h, uint32_t& ip) override { return net::resolveSystemHost(h, ip); }
    std::vector<uint32_t> broadcastAddresses() override { return net::systemBroadcastAddresses(); }
    void key(uint32_t k) { Event e; e.type = EventType::kKeyDown; e.key = k; q.push_back(e); }
    void updateScreen(const Surface& s) override {
        if (wait > 0) { --wait; return; }
        while (pc < script.size() && wait == 0) {
            const std::string c = script[pc++];
            if (c.rfind("wait:", 0) == 0) wait = std::stoi(c.substr(5));
            else if (c.rfind("shot:", 0) == 0) {
                std::ofstream f(outDir + "/" + c.substr(5) + ".ppm", std::ios::binary);
                f << "P6\n" << s.w << " " << s.h << "\n255\n";
                for (int y = 0; y < s.h; ++y) for (int x = 0; x < s.w; ++x) f.write((const char*)s.pixels.data() + (y * s.pitch + x * 4), 3);
                std::printf("shot %s  [%s]\n", c.substr(5).c_str(), title.c_str());
            } else if (c == "right") key(key::kRight); else if (c == "left") key(key::kLeft);
            else if (c == "up") key(key::kUp); else if (c == "down") key(key::kDown);
            else if (c == "tab") key(key::kTab); else if (c == "space") key(key::kSpace);
            else if (c == "pgdn") key(key::kPageDown);
            else if (c == "esc") key(key::kEscape);
            else if (c == "enter") key(key::kReturn);
            else if (c == "f9") key(key::kF1 + 8);
            else if (c.rfind("type:", 0) == 0) { Event e; e.type = EventType::kTextInput; std::snprintf(e.text, sizeof e.text, "%s", c.substr(5).c_str()); q.push_back(e); } else if (c == "end") key(key::kEnd);
            else if (c.rfind("key:", 0) == 0) key(uint32_t(c[4]));
            else if (c.rfind("mouse:", 0) == 0 || c.rfind("click:", 0) == 0) {
                const size_t comma = c.find(','); Event e; e.mouseX = std::stoi(c.substr(6, comma - 6)); e.mouseY = std::stoi(c.substr(comma + 1));
                e.type = EventType::kMouseMove; q.push_back(e);
                if (c[0] == 'c') { e.type = EventType::kMouseDown; e.button = MouseButton::kLeft; q.push_back(e); e.type = EventType::kMouseUp; q.push_back(e); }
            }
            else std::printf("unknown step %s\n", c.c_str());
        }
        if (pc >= script.size() && wait == 0) { Event e; e.type = EventType::kQuit; q.push_back(e); }
    }
};
int main(int argc, char** argv) {
    if (argc < 5) { std::fprintf(stderr, "harness <gameid> <dir> <outdir> steps...\n"); return 2; }
    Headless sys; sys.outDir = argv[3];
    for (int i = 4; i < argc; ++i) sys.script.push_back(argv[i]);
    GameTarget t{"t", "clue", argv[1], "", argv[2], {}};
    if (const char* sp = std::getenv("HARNESS_SAVE")) t.savePath = sp;
    auto eng = clueMetaEngine().create(sys, t);
    Error err = eng->run();
    if (err) { std::printf("error: %s\n", err.message.c_str()); return 1; }
    return 0;
}
