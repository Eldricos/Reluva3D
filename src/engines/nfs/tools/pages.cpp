// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Need for Speed engine's reluva-tool command for its data viewer's pages:
// one drawn after given keys, as a picture.

#include "backends/networking/sockets/socket_net.h"
#include "audio/wav.h"
#include "common/events.h"
#include "common/fs.h"
#include "image/image.h"
#include "emulation/x86/pokes.h"
#include "engines/nfs/formats/bnk.h"
#include "engines/nfs/formats/col.h"
#include "engines/nfs/nfs3/formats/fce.h"
#include "engines/nfs/formats/ffn.h"
#include "engines/nfs/nfs3/formats/frd.h"
#include "engines/nfs/folders.h"
#include "engines/nfs/formats/fsh.h"
#include "engines/nfs/game_files.h"
#include "engines/nfs/games.h"
#include "engines/nfs/host/game_host.h"
#include "engines/nfs/nfs3/formats/hrz.h"
#include "engines/nfs/nfs3/formats/mad.h"
#include "engines/nfs/formats/mus.h"
#include "engines/nfs/ui/play_page.h"
#include "engines/nfs/formats/refpack.h"
#include "engines/nfs/formats/text_table.h"
#include "engines/nfs/pages/viewers.h"
#include "engines/nfs/formats/viv.h"
#include "engines/nfs/nfs2se/formats/col.h"
#include "engines/nfs/nfs2se/formats/trk.h"
#include "engines/nfs/nfs2se/track_sounds.h"
#include "devtools/tool.h"
#include "engines/nfs/tools/helpers.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

using reluva::Error;
using reluva::nfs::ShapeBank;
using reluva::nfs::VivArchive;
using namespace reluva::nfs::tools;
namespace stdfs = std::filesystem;

// Draws one of the engine's viewer pages after the keys given, as the
// viewer would, without a window: for checking pages and taking pictures
// of them.
int cmdNfsPage(int argc, char **argv) {
    if (argc < 5) {
        std::fprintf(stderr, "usage: reluva-tool nfs page <game folder> <out.tga> <page> [key...]\n"
                             "  keys: left right up down pgup pgdn home end enter esc, or a letter or symbol;\n"
                             "  also +key and -key (held, let go), wait<ms>, click and move<dx>/<dy>\n"
                             "  (the Play page writes to nfs page-save)\n");
        return 2;
    }
    // The game the folder holds, as the launcher finds it; NFS III if none.
    const reluva::nfs::GameInfo *game = &reluva::nfs::games().front();
    auto found = reluva::detectFromTable("nfs", reluva::nfs::detectionTable(), argv[2]);
    if (found.empty())
        found = reluva::detectFromTable("nfs", reluva::nfs::fallbackTable(), argv[2]);
    if (!found.empty())
        if (const reluva::nfs::GameInfo *known = reluva::nfs::findGame(found[0].gameId))
            game = known;
    const auto folders = reluva::nfs::findFolders(argv[2], {}, game->executable);
    reluva::nfs::GameFiles files;
    files.scan(folders);
    auto pages = reluva::nfs::makePages(*game, folders, files, nullptr, "nfs page-save");
    reluva::nfs::Page *page = nullptr;
    for (auto &p : pages)
        if (lowerAscii(p->name()) == lowerAscii(argv[4]))
            page = p.get();
    if (!page) {
        std::fprintf(stderr, "no page '%s'; there are:", argv[4]);
        for (auto &p : pages)
            std::fprintf(stderr, " %s", p->name());
        std::fprintf(stderr, "\n");
        return 1;
    }
    // The Play page's game starts at once, as it would when shown.
    if (auto *play = dynamic_cast<reluva::nfs::PlayPage *>(page))
        play->start();
    const std::pair<const char *, uint32_t> names[] = {
        {"left", reluva::key::kLeft}, {"right", reluva::key::kRight}, {"up", reluva::key::kUp},
        {"down", reluva::key::kDown}, {"pgup", reluva::key::kPageUp}, {"pgdn", reluva::key::kPageDown},
        {"home", reluva::key::kHome}, {"end", reluva::key::kEnd},     {"enter", reluva::key::kReturn},
        {"esc", reluva::key::kEscape}};
    int mouseX = 320;
    int mouseY = 240;
    auto mouse = [&](reluva::EventType type) {
        reluva::Event ev;
        ev.type = type;
        ev.mouseX = mouseX;
        ev.mouseY = mouseY;
        ev.button = reluva::MouseButton::kLeft;
        page->handleEvent(ev);
    };
    mouse(reluva::EventType::kMouseMove);
    for (int i = 5; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("wait", 0) == 0 && arg.size() > 4) { // wait<ms>: the page ticking meanwhile
            const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::atoi(argv[i] + 4));
            while (std::chrono::steady_clock::now() < until) {
                page->tick(0);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            continue;
        }
        if (arg == "click") {
            mouse(reluva::EventType::kMouseDown);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            mouse(reluva::EventType::kMouseUp);
            continue;
        }
        if (arg.rfind("move", 0) == 0 && arg.find('/') != std::string::npos) { // move<dx>/<dy>
            mouseX += std::atoi(arg.c_str() + 4);
            mouseY += std::atoi(arg.c_str() + arg.find('/') + 1);
            mouse(reluva::EventType::kMouseMove);
            continue;
        }
        // +key and -key: pressed, let go; else both.
        const bool press = arg[0] != '-';
        const bool release = arg[0] != '+';
        const std::string keyArg = arg.size() > 1 && (arg[0] == '+' || arg[0] == '-') ? arg.substr(1) : arg;
        uint32_t key = 0;
        for (const auto &[name, code] : names)
            if (keyArg == name)
                key = code;
        if (!key && keyArg.size() == 1)
            key = static_cast<unsigned char>(keyArg[0]);
        if (!key) {
            std::fprintf(stderr, "unknown key '%s'\n", argv[i]);
            return 2;
        }
        reluva::Event ev;
        ev.key = key;
        if (press) {
            ev.type = reluva::EventType::kKeyDown;
            page->handleEvent(ev);
        }
        if (release) {
            ev.type = reluva::EventType::kKeyUp;
            page->handleEvent(ev);
        }
    }
    reluva::Surface screen;
    screen.create(640, 480, reluva::PixelFormat::kRGBA32);
    page->draw(screen);
    reluva::image::Image img;
    img.w = screen.w;
    img.h = screen.h;
    img.rgba = screen.pixels;
    if (!writeFile(argv[3], reluva::image::encodeTga(img))) {
        std::fprintf(stderr, "can't write %s\n", argv[3]);
        return 1;
    }
    return 0;
}

// An NFS II SE track's placed sounds as heard at a point of its virtual
// road, 1.5 above it, facing along it, in its block: mixed as the Tracks page plays
// them (track_sounds.h), the game's frames counted 30 a second, into a
// WAV file. For listening to them, and checking they play, without an
// audio device.
int cmdTrackSounds(int argc, char **argv) {
    if (argc < 7) {
        std::fprintf(stderr,
                     "usage: reluva-tool nfs track-sounds <game folder> <trNN> <road point> <seconds> <out.wav>\n");
        return 2;
    }
    const auto folders = reluva::nfs::findFolders(argv[2], {}, "nfs2sen.exe");
    reluva::nfs::GameFiles files;
    files.scan(folders);
    const std::string stem = lowerAscii(argv[3]);
    auto read = [&](const std::string &name, std::vector<uint8_t> &out) {
        for (const reluva::nfs::FileRef &f : files.files())
            if (f.inner.empty() && lowerAscii(f.name).ends_with("tracks/se/" + name))
                return !files.read(f, out);
        return false;
    };
    std::vector<uint8_t> data;
    reluva::nfs::nfs2se::Track track;
    reluva::nfs::nfs2se::Collision collision;
    if (!read(stem + ".trk", data) || reluva::nfs::nfs2se::parseTrack(data, track)) {
        std::fprintf(stderr, "no readable %s.trk in %s\n", stem.c_str(), argv[2]);
        return 1;
    }
    if (!read(stem + ".col", data) || reluva::nfs::nfs2se::parseCollision(data, collision) || collision.road.empty()) {
        std::fprintf(stderr, "no readable %s.col (its virtual road) in %s\n", stem.c_str(), argv[2]);
        return 1;
    }
    const size_t point = std::min<size_t>(size_t(std::max(0, std::atoi(argv[4]))), collision.road.size() - 1);
    const double seconds = std::max(0.1, std::atof(argv[5]));
    const auto &r = collision.road[point];
    const std::array<float, 3> eye = {r.position[0], r.position[1] + 1.5f, r.position[2]};
    const float across = std::hypot(float(r.across[0]), float(r.across[2]));
    const std::array<float, 3> right = {across > 0 ? r.across[0] / across : 1.0f, 0,
                                        across > 0 ? r.across[2] / across : 0.0f};

    constexpr int kRate = 22050;
    reluva::audio::Mixer mixer(kRate);
    reluva::nfs::nfs2se::TrackSounds sounds(files, &mixer);
    sounds.load(stem, track);
    if (!sounds.loaded()) {
        std::fprintf(stderr, "no readable bank of %s's sounds (Audio/SFX/pc/tramb%s.bnk)\n", stem.c_str(),
                     stem.substr(2).c_str());
        return 1;
    }
    reluva::audio::Sound out;
    out.rate = kRate;
    out.channels = 2;
    const int frames = int(seconds * 30);
    int peak = 0;
    for (int f = 0; f < frames; ++f) {
        sounds.update(uint32_t(f), r.block, eye, right);
        const int count = (f + 1) * kRate / 30 - f * kRate / 30;
        const size_t at = out.samples.size();
        out.samples.resize(at + size_t(count) * 2);
        mixer.mix(out.samples.data() + at, count);
    }
    for (int16_t v : out.samples)
        peak = std::max(peak, std::abs(int(v)));
    if (!writeFile(argv[6], reluva::audio::encodePcmWav(out))) {
        std::fprintf(stderr, "can't write %s\n", argv[6]);
        return 1;
    }
    std::printf("%s, road point %zu of %zu: %zu placed sounds; %.1f s, loudest sample %d of 32767\n", stem.c_str(),
                point, collision.road.size(), sounds.placed(), seconds, peak);
    return 0;
}

const reluva::tool::Commands
    kCommands("nfs",
              {
                  {"page", "<game folder> <out.tga> <page> [key...]",
                   "one of the engine's pages drawn after the keys given, as a TGA, without a window", cmdNfsPage},
                  {"track-sounds", "<game folder> <trNN> <road point> <seconds> <out.wav>",
                   "an NFS II SE track's placed sounds as heard at a point of its road, as a WAV file", cmdTrackSounds},
              });

} // namespace
