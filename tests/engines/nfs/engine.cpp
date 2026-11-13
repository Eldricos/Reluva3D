// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Need for Speed III as a whole, without the game's files: the basic engine
// test, run by ctest and CI. It makes an install of its own in miniature,
// laid out as the game's installer lays it out and with a copy of the CD
// beside it (the music and movies), its files written here: a track's
// .frd, a car's .viv holding its model and pictures, the front end's
// pictures, strings and font, a sound bank, a movie. The executable is a
// stand-in, not a program, so the Play page says it can't start the game.
//
// It finds the game as the launcher does, then plays it: every page the
// engine shows, a few keys on each, Tab to the next, back to the launcher
// at the end. The format readers read the same files on their own. What
// happens goes into a record (counts, and checksums of the screens, the
// titles and what each reader found), compared with
// tests/engines/nfs/data/engine-record.txt.
//
//   reluva-tests nfs.engine [--update]

#include "audio/sound.h"
#include "common/headless_system.h"
#include "engines/nfs/formats/bnk.h"
#include "engines/nfs/formats/frd.h"
#include "engines/nfs/formats/fsh.h"
#include "engines/nfs/formats/mad.h"
#include "engines/nfs/formats/text_table.h"
#include "engines/nfs/formats/viv.h"
#include "engines/nfs/game_files.h"
#include "engines/registry.h"
#include "tests/engines/nfs/fixtures.h"
#include "tests/support/record.h"
#include "tests/support/test.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::nfs;
using namespace reluva::nfs::testing;
using test::Hash;

namespace {

// The headless System with a player: keys at given times. The clock moves
// on a millisecond at each look for input as well as at each wait, so a page
// that never waits (a 3D page without a renderer) still sees time pass. It
// keeps a checksum of every screen shown, by the page it was shown on.
class PlayingSystem : public HeadlessSystem {
public:
    struct Input {
        uint64_t at;
        Event event;
    };
    std::vector<Input> inputs;
    std::map<std::string, Hash> screens; // by window title
    std::map<std::string, int> shown;
    std::vector<std::string> titles;

    void updateScreen(const Surface &s) override {
        HeadlessSystem::updateScreen(s);
        const std::string &t = titles.empty() ? std::string() : titles.back();
        screens[t].bytes(s.pixels);
        ++shown[t];
    }
    void setWindowTitle(const std::string &t) override {
        HeadlessSystem::setWindowTitle(t);
        if (titles.empty() || titles.back() != t)
            titles.push_back(t);
    }
    bool pollEvent(Event &ev) override {
        if (next_ < inputs.size() && inputs[next_].at <= getMillis()) {
            ev = inputs[next_++].event;
            return true;
        }
        HeadlessSystem::delayMillis(1);
        return false;
    }

private:
    size_t next_ = 0;
};

Event keyDown(uint32_t k) {
    Event e;
    e.type = EventType::KeyDown;
    e.key = k;
    return e;
}

void write(const stdfs::path &file, const Bytes &bytes) {
    stdfs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary)
        .write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
}

// A shape bank of two pictures: 8-bit with its palette, and 32-bit.
Bytes pictures() {
    Bytes indexed;
    putShapeHeader(indexed, 0x7b, 16 + 4, 2, 1, 0, 0, 0, 0);
    indexed.insert(indexed.end(), {1, 0, 0, 0});
    putShapeHeader(indexed, 0x2d, 0, 2, 1, 0, 0, 0, 0);
    put16(indexed, 0x7c00);
    put16(indexed, 0x8000 | 0x03e0);
    Bytes argb;
    putShapeHeader(argb, 0x7d, 0, 2, 2, 0, 0, 0, 0);
    argb.insert(argb.end(), {0x10, 0x20, 0x30, 0xff, 0x40, 0x50, 0x60, 0xff, 0x70, 0x80, 0x90, 0xff, 0, 0, 0, 0});
    return makeShapeBank({{"pal8", indexed}, {"argb", argb}});
}

// A string table of three strings.
Bytes strings() {
    Bytes b;
    put32(b, 12);
    put32(b, 13);
    put32(b, 19);
    b.push_back(0);
    putText(b, "Track", 6);
    putText(b, "Race", 5);
    return b;
}

// A sound bank with one 16-bit sound of a few hundred samples.
Bytes soundBank() {
    Bytes b = {'B', 'N', 'K', 'l', 2, 0, 1, 0};
    put32(b, 128);
    b.resize(12 + 4, 0);
    const Bytes pcm = ptHeader({{0x85, 400}, {0x88, 128}, {0x84, 22050}});
    putU32(b, 12, uint32_t(b.size() - 12));
    b.insert(b.end(), pcm.begin(), pcm.end());
    b.resize(128, 0);
    for (int i = 0; i < 400; ++i)
        put16(b, uint16_t(int16_t((i % 40 - 20) * 500)));
    return b;
}

// A movie of one key frame: four luma blocks, flat at three levels and one
// with a coefficient, then flat chroma (as nfs.unit.formats' movie).
Bytes movie() {
    MadBits bits;
    for (int dc : {10, -28, 0}) {
        bits.put(uint32_t(dc) & 0xff, 8);
        bits.code("10"); // the end of the block
    }
    bits.put(0, 8);
    bits.code("000001"); // escape
    bits.put(100, 10);
    bits.put(0, 6);
    bits.code("10");
    for (int i = 0; i < 2; ++i) {
        bits.put(0, 8);
        bits.code("10");
    }
    return madFrame('k', bits);
}

// The install, and the CD's files beside it (NFS3-CD, as findFolders looks
// for one).
void makeGame(const stdfs::path &root) {
    const stdfs::path install = root / "Need For Speed III", cd = root / "NFS3-CD";
    write(install / "nfs3.exe", Bytes{'M', 'Z', 0, 0});
    write(install / "GameData/Tracks/trk000/tr00.frd", makeTrack());
    write(install / "GameData/CarModel/Diab/car.viv",
          makeViv({{"carp.txt", "Serial Number\n0\n"}, {"car00.tga", std::string()}}));
    const Bytes art = pictures();
    write(install / "FeData/art/title.fsh", art);
    write(install / "FeData/text/text.eng", strings());
    write(install / "GameData/Audio/SFX/sound.bnk", soundBank());
    write(cd / "nfs3.exe", Bytes{'M', 'Z', 0, 0});
    write(cd / "gamedata/audio/pc/alpirock.mus", Bytes(64, 0));
    write(cd / "fedata/movies/titleav.mad", movie());
}

// The format readers on the files made, on their own.
void readFormats(const stdfs::path &install, const stdfs::path &cd, test::Record &rec) {
    auto read = [](const stdfs::path &p) {
        std::ifstream in(p, std::ios::binary);
        return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    Track track;
    if (Error err = parseTrack(read(install / "GameData/Tracks/trk000/tr00.frd"), track))
        rec.add("formats.frd", "error: " + err.message);
    else {
        Hash h;
        size_t vertices = 0, polygons = 0;
        for (const auto &b : track.blocks) {
            for (const auto &v : b.vertices)
                h.f32(v[0]).f32(v[1]).f32(v[2]);
            vertices += b.vertices.size();
            polygons += b.polygons.size();
        }
        rec.add("formats.frd", std::to_string(track.blocks.size()) + " blocks, " + std::to_string(vertices) +
                                   " vertices, " + std::to_string(polygons) + " polygons " + h.hex());
    }
    VivArchive viv;
    if (Error err = viv.open(install / "GameData/CarModel/Diab/car.viv"))
        rec.add("formats.viv", "error: " + err.message);
    else {
        std::string names;
        for (const std::string &n : viv.list())
            names += (names.empty() ? "" : " ") + n;
        rec.add("formats.viv", names);
    }
    ShapeBank bank;
    if (Error err = parseShapeBank(read(install / "FeData/art/title.fsh"), bank))
        rec.add("formats.fsh", "error: " + err.message);
    else {
        Hash h;
        for (const Shape &s : bank.shapes) {
            image::Image img;
            if (!decodeShape(s, img))
                h.text(s.name).i32(img.w).i32(img.h).bytes(img.rgba);
        }
        rec.add("formats.fsh", std::to_string(bank.shapes.size()) + " pictures " + h.hex());
    }
    TextTable table;
    if (Error err = parseTextTable(read(install / "FeData/text/text.eng"), table))
        rec.add("formats.text", "error: " + err.message);
    else {
        Hash h;
        for (const std::string &s : table.strings)
            h.text(s);
        rec.add("formats.text", std::to_string(table.strings.size()) + " strings " + h.hex());
    }
    const Bytes bnk = read(install / "GameData/Audio/SFX/sound.bnk");
    SoundBank sounds;
    if (Error err = parseSoundBank(bnk, sounds))
        rec.add("formats.bnk", "error: " + err.message);
    else {
        Hash h;
        int decoded = 0;
        for (const auto &slot : sounds.slots) {
            audio::Sound s;
            if (slot && !decodeBankSound(bnk, *slot, s)) {
                ++decoded;
                h.i32(s.rate).bytes(s.samples.data(), s.samples.size() * 2);
            }
        }
        rec.add("formats.bnk",
                std::to_string(decoded) + " of " + std::to_string(sounds.slots.size()) + " sounds " + h.hex());
    }
    MadMovie mad;
    if (Error err = mad.open(read(cd / "fedata/movies/titleav.mad")))
        rec.add("formats.mad", "error: " + err.message);
    else {
        MadDecoder decoder;
        Hash h;
        int decoded = 0;
        for (size_t i = 0; i < mad.frames().size(); ++i)
            if (!mad.decodeFrame(decoder, i)) {
                ++decoded;
                Surface s;
                decoder.toSurface(s);
                h.bytes(s.pixels);
            }
        rec.add("formats.mad", std::to_string(decoded) + " of " + std::to_string(mad.frames().size()) + " frames, " +
                                   std::to_string(mad.width()) + "x" + std::to_string(mad.height()) + " " + h.hex());
    }
}

void engineTest(const stdfs::path &tmp) {
    test::Record rec;
    const MetaEngine *engine = findEngine("nfs");
    if (!engine)
        return test::fail("nfs isn't registered");
    makeGame(tmp);
    const stdfs::path install = tmp / "Need For Speed III";

    // Found as the launcher finds it: in the install, and in the CD's copy.
    auto found = engine->detect(install);
    rec.add("detect.install", found.empty() ? "(none)" : found[0].gameId + " " + found[0].description);
    rec.add("detect.cd", int64_t(engine->detect(tmp / "NFS3-CD").size()));

    // What the engine finds there: the folders, and the files by kind.
    GameFolders folders = findFolders(install, {}, "nfs3.exe");
    rec.add("folders.cd", folders.cd.empty() ? "(none)" : folders.cd.lexically_relative(tmp).generic_string());
    GameFiles files;
    files.scan(folders);
    rec.add("files", int64_t(files.files().size()));
    rec.add("files.pictures", int64_t(files.withExtensions({".fsh", ".qfs"}).size()));
    rec.add("files.strings", int64_t(files.textTables().size()));

    readFormats(install, folders.cd, rec);

    // Played: each page in turn (Tab), with the keys its page takes, then
    // back to the launcher.
    GameTarget target{"nfs3",        "nfs", found.empty() ? "nfs3" : found[0].gameId, "Need for Speed III", install,
                      tmp / "saves", {}};
    PlayingSystem sys;
    uint64_t at = 200;
    for (int page = 0; page < 12; ++page) {
        for (uint32_t k : {key::Down, key::Right, key::Space})
            sys.inputs.push_back({at += 50, keyDown(k)});
        sys.inputs.push_back({at += 200, keyDown(key::Tab)});
    }
    sys.inputs.push_back({at + 200, Event{EventType::ReturnToLauncher}});
    auto game = engine->create(sys, target);
    const Error err = game->run();
    rec.add("run.result", err ? "error: " + err.message : std::string("ok"));
    rec.add("run.size", std::to_string(sys.width()) + "x" + std::to_string(sys.height()));
    std::string titles;
    for (const std::string &t : sys.titles) {
        if (titles.find(t) != std::string::npos)
            continue;
        titles += (titles.empty() ? "" : " / ") + t;
    }
    rec.add("run.titles", titles);
    for (const auto &[title, hash] : sys.screens)
        rec.add("run.screens " + title, std::to_string(sys.shown[title]) + " " + hash.hex());

    rec.check(RELUVA_SOURCE_DIR "/tests/engines/nfs/data/engine-record.txt", "nfs.engine",
              "tests/engines/nfs/engine.cpp");
}

} // namespace

static const reluva::test::Suite kSuite("nfs.engine",
                                        {
                                            {"play through", [](const std::filesystem::path &tmp) { engineTest(tmp); }},
                                        });
