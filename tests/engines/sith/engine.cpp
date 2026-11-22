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
#include "engines/sith/folders.h"
#include "engines/sith/pages/pages.h"

#include "tests/engines/sith/fixtures.h"
#include "tests/support/record.h"
#include "tests/support/test.h"

using namespace reluva;

namespace {

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
    writeBytes(dir / "Resource/Res2.gob",
               makeGob({{"misc\\cmp\\uicolormap.cmp", makeColormapFile()}, {"mat\\test.mat", makeMaterialFile()}}));
    writeBytes(dir / "Resource/Res1hi.gob", makeGob({{"ui\\bm\\test.bm", makePictureFile()},
                                                     {"ui\\sft\\test.sft", makeFontFile()},
                                                     {"ui\\jkstrings.uni", makeStringsFile()}}));
    writeBytes(dir / "Episode/JK1.GOB", makeGob({{"misc\\cogstrings.uni", makeStringsFile()}}));
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
        rec.add(step + ".drawn", test::Hash().bytes(screen.pixels).hex());
        // A key that changes each page: transparency, light level, size, a line.
        const std::string name = page->name();
        page->handleKey(name == "Pictures"    ? uint32_t('k')
                        : name == "Materials" ? uint32_t('[')
                        : name == "Fonts"     ? uint32_t('+')
                                              : uint32_t(key::kDown));
        page->draw(screen);
        rec.add(step + ".after_keys", test::Hash().bytes(screen.pixels).hex());
    }

    rec.check(RELUVA_SOURCE_DIR "/tests/engines/sith/data/engine-record.txt", "sith.engine",
              "tests/engines/sith/engine.cpp");
}

} // namespace

static const reluva::test::Suite kSuite("sith.engine",
                                        {
                                            {"play through", [](const std::filesystem::path &tmp) { engineTest(tmp); }},
                                        });
