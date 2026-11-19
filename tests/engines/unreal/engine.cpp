// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Unreal engine as a whole, without any game's files: the basic engine
// test, run by ctest and CI. It makes a small install of its own (a System
// folder with its .ini and packages built here, a Textures folder), finds
// it as The Fallen, opens its packages through the .ini's search path,
// reads the level it would start in and decodes a texture, and writes what
// happens into a record (tests/support/record.h), compared with
// tests/engines/unreal/data/engine-record.txt.
//
//   reluva-tests unreal.engine [--update]

#include "engines/registry.h"
#include "engines/unreal/formats/package_set.h"
#include "engines/unreal/formats/texture.h"
#include "image/image.h"

#include "tests/engines/unreal/support.h"
#include "tests/support/record.h"
#include "tests/support/test.h"

#include <fstream>

using namespace reluva;
using namespace reluva::unreal;

namespace {

void write(const std::filesystem::path &file, const std::vector<uint8_t> &data) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
}

void writeText(const std::filesystem::path &file, const std::string &text) {
    write(file, std::vector<uint8_t>(text.begin(), text.end()));
}

void engineTest(const std::filesystem::path &tmp) {
    const std::filesystem::path dir = tmp / "install";
    test::Record rec;
    const MetaEngine *engine = findEngine("unreal");
    if (!engine)
        return test::fail("unreal isn't registered");

    // Nothing there yet: not a game.
    std::filesystem::create_directories(dir);
    rec.add("detect.empty", static_cast<int64_t>(engine->detect(dir).size()));

    // The install: Core and Engine (empty packages are enough to be found),
    // the .ini, and a texture package where its Paths say.
    const std::vector<uint8_t> empty = test_data::PackageWriter().build();
    write(dir / "System" / "Core.u", empty);
    write(dir / "System" / "Engine.u", empty);
    writeText(dir / "System" / "DS9.ini", "[URL]\r\nLocalMap=Title.dsm\r\n\r\n[Core.System]\r\nPaths=*.u\r\n"
                                          "Paths=../Maps/*.dsm\r\nPaths=../Textures/*.utx\r\n");
    write(dir / "Textures" / "Walls.utx", test_data::texturePackage());
    const std::vector<DetectedGame> games = engine->detect(dir);
    rec.add("detect.games", static_cast<int64_t>(games.size()));
    rec.add("detect.game", games.empty() ? "" : games[0].gameId);

    PackageSet packages;
    std::vector<std::string> warnings;
    if (Error err = packages.open(dir, "DS9.ini", &warnings))
        rec.fail("packages.open", err.message);
    std::string names;
    for (const std::string &name : packages.names())
        names += (names.empty() ? "" : " ") + name;
    rec.add("packages.names", names);
    // The Maps folder its Paths name isn't there.
    for (const std::string &warning : warnings)
        rec.add("packages.warning", warning);

    std::ifstream ini(packages.iniFile(), std::ios::binary);
    const std::string iniText((std::istreambuf_iterator<char>(ini)), std::istreambuf_iterator<char>());
    rec.add("ini.local_map", iniValue(iniText, "url", "localmap"));

    const Package *walls = packages.get("walls");
    if (!walls) {
        rec.fail("texture", packages.error("walls"));
    } else {
        image::Image picture;
        const int brick = walls->findExport("Brick", "Texture");
        if (Error err = decodeTexture(packages, *walls, brick, picture))
            rec.fail("texture", err.message);
        else
            rec.add("texture.brick", std::to_string(picture.w) + "x" + std::to_string(picture.h) + " " +
                                         test::Hash().bytes(picture.rgba).hex());
    }
    rec.check(RELUVA_SOURCE_DIR "/tests/engines/unreal/data/engine-record.txt", "unreal.engine",
              "tests/engines/unreal/engine.cpp");
}

} // namespace

static const reluva::test::Suite kSuite("unreal.engine",
                                        {
                                            {"play through", [](const std::filesystem::path &tmp) { engineTest(tmp); }},
                                        });
