// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Sith as a whole, without any game's files: the basic engine test, run
// by ctest and CI. It makes a small install of its own, takes the engine
// through what playing does, and writes what happens into a record (counts,
// checksums: tests/support/record.h), compared with
// tests/engines/sith/data/engine-record.txt.
//
//   reluva-tests sith.engine [--update]
//
// So far it recognises a Jedi Knight install, finds its CDs, and draws
// each of the data viewer's pages from archives of its own: add steps as
// the engine learns to read and run its games (see
// tests/engines/idtech3/engine.cpp), then --update and review the record.

#include "engines/registry.h"
#include "common/headless_system.h"
#include "engines/sith/folders.h"
#include "engines/sith/pages/pages.h"

#include "audio/wav.h"
#include "tests/common/data/smacker_videos.h"
#include "tests/engines/sith/fixtures.h"
#include "tests/support/record.h"
#include "tests/support/test.h"

using namespace reluva;

namespace {

// A page as drawn: its pixels' checksum; for the 3D pages, whose edges
// floating point rounds differently from one compiler to another, whether
// a good part of the screen (a thousand pixels) isn't the background.
std::string summary(const std::string &page, const Surface &screen) {
    if (page != "Models" && page != "Sprites" && page != "Levels")
        return test::Hash().bytes(screen.pixels).hex();
    size_t drawn = 0;
    for (size_t i = 0; i + 3 < screen.pixels.size(); i += 4)
        drawn += screen.pixels[i] != 40 || screen.pixels[i + 1] != 40 || screen.pixels[i + 2] != 48;
    return drawn > 1000 ? "drawn" : "nothing drawn";
}

void engineTest(const std::filesystem::path &tmp) {
    const std::filesystem::path dir = tmp / "install";
    std::filesystem::create_directories(dir);
    test::Record rec;
    const MetaEngine *engine = findEngine("sith");
    if (!engine)
        return test::fail("sith isn't registered");
    rec.add("detect.empty", static_cast<int64_t>(engine->detect(dir).size()));

    sith::testing::makeInstall(dir);
    const std::vector<DetectedGame> found = engine->detect(dir);
    rec.add("detect.games", static_cast<int64_t>(found.size()));
    rec.add("detect.release", found.empty() ? std::string() : found[0].gameId + ": " + found[0].description);

    // Played, the game is the install's JK.EXE, here an empty file: an
    // error saying so, with the window set up and nothing shown from it.
    {
        HeadlessSystem sys;
        const GameTarget target{"jk", "sith", "jk", "Jedi Knight", dir, tmp / "saves", {}};
        const Error err = engine->create(sys, target)->run();
        rec.add("play.empty_program", err ? err.message : std::string("(none)"));
        rec.add("play.window", std::to_string(sys.width()) + "x" + std::to_string(sys.height()));
    }

    sith::testing::makeCd(tmp / "Disc 1", 1);
    sith::testing::makeCd(dir / "cd2", 2);
    const sith::GameFolders folders = sith::findFolders(dir, {});
    for (int i = 0; i < sith::kCdCount; ++i)
        rec.add("folders.cd" + std::to_string(i + 1),
                folders.cds[i].empty()
                    ? std::string("none")
                    : folders.cds[i].lexically_relative(tmp).generic_string() + " (" + folders.cdSources[i] + ")");

    // Archives of the install's own, one file of each kind the viewer shows.
    using namespace sith::testing;
    const std::string spr = "top.mat 0 0.5 0.5 4 0 0 0.0 0.0 0.0 0.0\r\n";
    const std::vector<uint8_t> spriteText(spr.begin(), spr.end());
    const std::string par = "PAR 1.0\r\nSIZE .05\r\nMATERIAL top.mat\r\nRADIUS 0.5\r\nVERTICES 3\r\n"
                            "0: 0 0 0 0\r\n1: 0.3 0 0 0\r\n2: 0 0 0.3 0\r\n";
    const std::vector<uint8_t> particleText(par.begin(), par.end());
    audio::Sound tone;
    tone.rate = 22050;
    tone.channels = 1;
    for (int i = 0; i < 2205; ++i)
        tone.samples.push_back(int16_t((i % 50 - 25) * 800));
    const std::string snd = "moving\ttone.wav\t0x0001\t0.5\t4.0\r\nstopmove\tmissing.wav\r\n";
    writeBytes(dir / "Resource/Res1hi.gob", makeGob({{"ui\\bm\\test.bm", makePictureFile()},
                                                     {"ui\\sft\\test.sft", makeFontFile()},
                                                     {"ui\\jkstrings.uni", makeStringsFile()},
                                                     {"sound\\tone.wav", audio::encodePcmWav(tone)}}));
    writeBytes(dir / "Resource/Res2.gob", makeGob({{"misc\\cmp\\uicolormap.cmp", makeColormapFile()},
                                                   {"mat\\test.mat", makeMaterialFile()},
                                                   {"misc\\snd\\door.snd", {snd.begin(), snd.end()}},
                                                   {"3do\\test.3do", makeModelFile()},
                                                   {"3do\\key\\test.key", makeKeyframeFile()},
                                                   {"misc\\spr\\test.spr", spriteText},
                                                   {"misc\\par\\test.par", particleText},
                                                   {"3do\\mat\\top.mat", makeMaterialFile()}}));
    const std::string episode = "\"Test\"\r\nTYPE 1\r\nSEQ 2\r\n10: 1 1 CUT 01-02a.smk 0 0 11 -1\r\n"
                                "11: 1 1 LEVEL 01test.jkl 0 0 -1 -1\r\nend\r\n";
    writeBytes(dir / "Episode/JK1.GOB", makeGob({{"misc\\cogstrings.uni", makeStringsFile()},
                                                 {"episode.jk", {episode.begin(), episode.end()}},
                                                 {"jkl\\01test.jkl", makeLevelFile()}}));
    // The CDs' cut scenes: RAD's format as the game's are, 16-bit stereo sound.
    const std::vector<uint8_t> video(std::begin(kSmackerTest4), std::end(kSmackerTest4));
    writeBytes(tmp / "Disc 1/GAMEDATA/RESOURCE/VIDEO/01-02A.SMK", video);
    writeBytes(dir / "cd2/GAMEDATA/RESOURCE/VIDEO/23A.SMK", video);
    sith::ViewerData data;
    if (Error err = data.open(dir))
        rec.fail("viewer.open", err.message);
    rec.add("viewer.archives", int64_t(data.files.archives().size()));
    rec.add("viewer.colormaps", int64_t(data.colormaps.size()));
    rec.add("viewer.palettes", int64_t(data.palettes.size()));
    Surface screen;
    screen.create(sith::kViewerW, sith::kViewerH, PixelFormat::kRGBA32);
    for (auto &page : sith::makePages(data)) {
        const std::string step = std::string("page.") + page->name();
        rec.add(step + ".items", int64_t(page->count()));
        if (page->count() == 0)
            continue;
        page->draw(screen);
        rec.add(step + ".drawn", summary(page->name(), screen));
        // A key that changes each page: transparency, light level, size, a
        // line, the next frame.
        const std::string name = page->name();
        page->handleKey(name == "Pictures"     ? uint32_t('k')
                        : name == "Materials"  ? uint32_t('[')
                        : name == "Fonts"      ? uint32_t('+')
                        : name == "Cut scenes" ? uint32_t('.')
                        : name == "Models"     ? uint32_t('d')
                        : name == "Sprites"    ? uint32_t(key::kRight)
                        : name == "Levels"     ? uint32_t('v')
                                               : uint32_t(key::kDown));
        page->draw(screen);
        rec.add(step + ".after_keys", summary(page->name(), screen));
        if (name == "Models") {
            // The animation that fits it, five frames in.
            for (const char *key : {"k", ".", ".", ".", ".", "."})
                page->handleKey(uint32_t(key[0]));
            page->draw(screen);
            rec.add(step + ".animated", summary(name, screen));
        }
        if (name == "Cut scenes") {
            // Played without sound, by the clock: a second on, to its end.
            page->handleKey(' ');
            page->update(1000);
            const bool moved = page->update(2000);
            page->draw(screen);
            rec.add(step + ".played",
                    std::string(moved ? "moved " : "still ") + test::Hash().bytes(screen.pixels).hex());
        }
    }

    rec.check(RELUVA_SOURCE_DIR "/tests/engines/sith/data/engine-record.txt", "sith.engine",
              "tests/engines/sith/engine.cpp");
}

} // namespace

static const reluva::test::Suite kSuite("sith.engine",
                                        {
                                            {"play through", [](const std::filesystem::path &tmp) { engineTest(tmp); }},
                                        });
