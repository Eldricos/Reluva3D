// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// id Tech 3 unit tests: The game's files: mounting game folders and mods, detection, the game code's file lists.

#include "common/fs.h"
#include "engines/idtech3/bindings.h"
#include "engines/idtech3/client/joystick_input.h"
#include "engines/idtech3/client/player_input.h"
#include "engines/idtech3/botlib/aas.h"
#include "engines/idtech3/botlib/bot_library.h"
#include "engines/idtech3/botlib/bot_script.h"
#include "engines/idtech3/botlib/character.h"
#include "engines/idtech3/bsp.h"
#include "engines/idtech3/collision.h"
#include "engines/idtech3/cvars.h"
#include "engines/idtech3/eliteforce/cgame_host.h"
#include "engines/idtech3/eliteforce/game_host.h"
#include "engines/idtech3/eliteforce/game_info.h"
#include "engines/idtech3/eliteforce/net_client.h"
#include "engines/idtech3/raven/savegame.h"
#include "engines/idtech3/server/server_world.h"
#include "engines/idtech3/eliteforce/net_fields.h"
#include "engines/idtech3/eliteforce/net_server.h"
#include "engines/idtech3/eliteforce/sp_game_host.h"
#include "engines/idtech3/eliteforce/ui_host.h"
#include "engines/idtech3/entities.h"
#include "engines/idtech3/filesystem.h"
#include "engines/idtech3/info_string.h"
#include "engines/idtech3/games.h"
#include "engines/idtech3/lexer.h"
#include "engines/idtech3/marks.h"
#include "engines/idtech3/light_grid.h"
#include "engines/idtech3/md3.h"
#include "engines/idtech3/model_cache.h"
#include "engines/idtech3/wire/message.h"
#include "engines/idtech3/wire/netchan.h"
#include "engines/idtech3/patch.h"
#include "engines/idtech3/player_animation.h"
#include "engines/idtech3/pure.h"
#include "engines/idtech3/qvm.h"
#include "engines/idtech3/server_browser.h"
#include "backends/sockets/socket_net.h"
#include "engines/idtech3/shader.h"
#include "engines/idtech3/raven/ambient_sets.h"
#include "engines/idtech3/sound_system.h"
#include "engines/idtech3/sounds.h"
#include "engines/idtech3/pages/page.h"
#include "common/audio/wav.h"
#include "common/md4.h"
#include "common/zip.h"
#include "engines/registry.h"
#include "tests/engines/idtech3/data/qvm/fake_cgame_qvm.h"
#include "tests/engines/idtech3/data/qvm/fake_game_qvm.h"
#include "tests/engines/idtech3/data/qvm/fake_ui_qvm.h"
#include "tests/engines/idtech3/data/qvm/test_vm_qvm.h"
#include "engines/idtech3/vm_services.h"
#include "tests/engines/idtech3/fixtures.h"
#include "tests/support/test.h"
#include "tests/support/zip_writer.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <memory>
#include <thread>
#include <chrono>
#include <set>
#include <string>

using namespace reluva;
using namespace reluva::idtech3;
namespace q3 = reluva::idtech3;
using namespace reluva::idtech3::fixtures;

static void testFilesystemAndDetection(const std::filesystem::path& tmp) {
    using test::toBytes;
    std::filesystem::remove_all(tmp);
    const auto base = tmp / "BaseEF";
    test::writeFile(base / "pak0.pk3", test::buildZip({{"a.txt", toBytes("pak0"), {}},
                                                       {"b.txt", toBytes("pak0"), {}},
                                                       {"scripts/x.shader", toBytes("textures/x\n{\n}\n"), {}}}));
    test::writeFile(base / "PAK1.pk3", test::buildZip({{"a.txt", toBytes("pak1"), {}},
                                                       {"b.txt", toBytes("pak1"), {}}}));
    test::writeFile(base / "zz_broken.pk3", toBytes("not a zip"));
    // In upper case '_' sorts after letters, so pak_c.pk3 beats pakb.pk3.
    test::writeFile(base / "pak_c.pk3", test::buildZip({{"c.txt", toBytes("underscore"), {}}}));
    test::writeFile(base / "pakb.pk3", test::buildZip({{"c.txt", toBytes("letter"), {}}}));
    test::writeFile(base / "b.txt", toBytes("loose"));

    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGameDir(set, tmp, "baseef", 0, warnings));
    CHECK(warnings.size() == 1);
    std::vector<uint8_t> data;
    CHECK(!set.read("a.txt", data) && data == toBytes("pak1"));
    CHECK(!set.read("b.txt", data) && data == toBytes("loose"));
    CHECK(!set.read("c.txt", data) && data == toBytes("underscore"));
    CHECK(mountGameDir(set, tmp, "missing", 1, warnings));

    ShaderLibrary lib;
    lib.load(set);
    CHECK(lib.size() == 1 && lib.find("textures/x"));

    // Images are found whichever extension is written, .tga first.
    test::writeFile(base / "textures" / "a.jpg", toBytes("jpg"));
    test::writeFile(base / "textures" / "b.tga", toBytes("tga"));
    test::writeFile(base / "textures" / "b.jpg", toBytes("jpg"));
    test::writeFile(base / "v1.5" / "c.tga", toBytes("tga"));
    SearchSet images;
    CHECK(!mountGameDir(images, tmp, "baseef", 0, warnings));
    CHECK(findImage(images, "textures/a.tga") == "textures/a.jpg");
    CHECK(findImage(images, "textures/a") == "textures/a.jpg");
    CHECK(findImage(images, "textures/b.jpg") == "textures/b.tga");
    CHECK(findImage(images, "v1.5/c") == "v1.5/c.tga"); // a dot in a folder isn't an extension
    CHECK(findImage(images, "textures/none.tga").empty());

    // Elite Force by its pak0.pk3 alone (no Windows program needed). The
    // test's isn't a known release: found by name, and only once, with
    // the patch level and what's missing.
    const MetaEngine* meta = findEngine("idtech3");
    auto found = meta ? meta->detect(tmp) : std::vector<DetectedGame>{};
    CHECK(found.size() == 1 && found[0].gameId == "stvef" && findGame(found[0].gameId));
    CHECK(found.size() == 1 && found[0].description.find("unknown release") != std::string::npos &&
          found[0].description.find("(1.0, no single player)") != std::string::npos);
    // Imported from a CD's layout (SETUP/BaseEF, the DLLs beside it in
    // capitals): its paks, key file and saves, the DLLs named as Reluva
    // looks for them; the folder made is found as the game.
    {
        const auto cd = tmp / "cd";
        test::writeFile(cd / "SETUP" / "BaseEF" / "pak0.pk3", toBytes("pak"));
        test::writeFile(cd / "SETUP" / "BaseEF" / "efq3.key", toBytes("key"));
        test::writeFile(cd / "SETUP" / "BaseEF" / "efconfig.cfg", toBytes("cfg"));
        test::writeFile(cd / "SETUP" / "BaseEF" / "saves" / "s1.sav", toBytes("sav"));
        test::writeFile(cd / "SETUP" / "EFGAMEX86.DLL", toBytes("MZ"));
        test::writeFile(cd / "SETUP" / "efuix86.dll", toBytes("MZ"));
        const auto plans = meta ? meta->planImport(cd) : std::vector<ImportPlan>{};
        CHECK(plans.size() == 1 && plans[0].files.size() == 5 && plans[0].totalBytes() == 13);
        std::set<std::string> to;
        for (const ImportPlan::File& f : plans.empty() ? std::vector<ImportPlan::File>{} : plans[0].files)
            to.insert(f.to);
        const std::set<std::string> expected{"baseEF/pak0.pk3", "baseEF/efq3.key", "baseEF/saves/s1.sav",
                                             "efgamex86.dll", "efuix86.dll"};
        CHECK(to == expected);
        const auto dest = tmp / "imported";
        CHECK(!plans.empty() && !runImport(plans[0], dest, nullptr));
        found = meta ? meta->detect(dest) : std::vector<DetectedGame>{};
        CHECK(found.size() == 1 && found[0].description.find("(1.0)") != std::string::npos);
        CHECK(meta && meta->planImport(tmp / "nothing").empty());
    }
    // Every known release names pak0.pk3's size and hash.
    for (const GameVariant& v : detectionTable())
        CHECK(!v.files.empty() && !v.files[0].md5.empty() && v.files[0].size > 0);
    CHECK(!findGame("nope"));
    std::filesystem::remove_all(tmp);
}

static void testVmFileList() {
    using test::toBytes;
    auto dir = std::make_unique<ZipArchive>();
    const auto path = std::filesystem::temp_directory_path() / "reluva_idtech3_list.pk3";
    test::writeFile(path, test::buildZip({{"scripts/a.arena", toBytes("a"), {}},
                                          {"Scripts/B.ARENA", toBytes("b"), {}},
                                          {"scripts/c.txt", toBytes("c"), {}},
                                          {"scripts/deep/d.arena", toBytes("d"), {}},
                                          {"scripts/deep/e.arena", toBytes("e"), {}},
                                          {"scriptsx/f.arena", toBytes("f"), {}}}));
    CHECK(!dir->open(path));
    SearchSet set;
    set.add(std::move(dir), 0);
    const auto arenas = VmFiles::listNames(set, "scripts/", ".arena");
    CHECK(arenas.size() == 2 && arenas[0] == "a.arena" && arenas[1] == "B.ARENA");
    const auto folders = VmFiles::listNames(set, "scripts", "/");
    CHECK(folders.size() == 1 && folders[0] == "deep");
    CHECK(VmFiles::listNames(set, "scripts", "").size() == 3);
    std::filesystem::remove(path);

    HandleTable table;
    CHECK(table.add("gfx/a") == 1 && table.add("GFX\\A") == 1 && table.add("gfx/b") == 2 && table.add("") == 0);
    CHECK(table.name(2) == "gfx/b" && table.name(0).empty() && table.name(3).empty());
}

// The Expansion Pack engine's file listing (FS_GetFileList).
static void testExpansionFileList(const std::filesystem::path& tmp) {
    using reluva::idtech3::eliteforce::expansionFileList;
    using reluva::idtech3::eliteforce::packFileList;
    std::filesystem::remove_all(tmp);
    const std::vector<uint8_t> text = test::toBytes("x");
    test::writeFile(tmp / "BaseEF" / "pak0.pk3",
                    test::buildZip({{"ext_data/NPCs.cfg", text, {}},
                                    {"ext_data/Addon.NPC", text, {}},
                                    {"ext_data/sub/more.npc", text, {}},
                                    {"ext_data/sub/deeper/no.npc", text, {}},
                                    {"ext_dataX/odd.npc", text, {}},
                                    {"other/no.npc", text, {}}}));
    test::writeFile(tmp / "BaseEF" / "ext_data" / "loose.npc", text);
    test::writeFile(tmp / "BaseEF" / "ext_data" / "addon.npc", text);
    test::writeFile(tmp / "BaseEF" / "ext_data" / "sub" / "notloose.npc", text);
    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGameDir(set, tmp, "BaseEF", 0, warnings));
    std::vector<std::string> names = expansionFileList(set, "ext_data", ".npc");
    std::sort(names.begin(), names.end());
    // The pak's to one folder down, as the engine's (which also takes a
    // folder whose name only starts with the path's); the folder's own
    // loose files; addon.npc once.
    CHECK((names == std::vector<std::string>{"Addon.NPC", "X/odd.npc", "loose.npc", "sub/more.npc"}) ||
          (names == std::vector<std::string>{"X/odd.npc", "addon.npc", "loose.npc", "sub/more.npc"}));
    CHECK(expansionFileList(set, "ext_data", "").size() == 5);

    std::vector<uint8_t> out;
    CHECK(packFileList({"abc", "toolong", "de"}, 9, out) == 2);
    CHECK((out == std::vector<uint8_t>{'a', 'b', 'c', 0, 'd', 'e', 0, 0}));
    CHECK(packFileList({}, 4, out) == 0 && out == std::vector<uint8_t>{0});
}

// Folder listings in Windows' order on every system, and the mods list.
static void testModList(const std::filesystem::path& tmp) {
    using reluva::fs::windowsNameLess;
    CHECK(windowsNameLess("aZ", "a_b") && !windowsNameLess("a_b", "aZ")); // '_' after the letters
    CHECK(windowsNameLess("abc", "ABD") && windowsNameLess("ab", "abc") && !windowsNameLess("abc", "abc"));
    CHECK(windowsNameLess("ABC", "abc") != windowsNameLess("abc", "ABC")); // told apart all the same

    std::filesystem::remove_all(tmp);
    const std::vector<uint8_t> text = test::toBytes("x");
    test::writeFile(tmp / "BaseEF" / "pak0.pk3", text);
    test::writeFile(tmp / "zeta" / "z.PK3", text);
    test::writeFile(tmp / "mod_b" / "a.pk3", text);
    test::writeFile(tmp / "mod_b" / "description.txt", test::toBytes("A mod with a long description, longer than 48 bytes"));
    test::writeFile(tmp / "modA" / "a.pk3", text);
    test::writeFile(tmp / "modA" / "description.txt", {});
    test::writeFile(tmp / "nopaks" / "readme.txt", text);
    test::writeFile(tmp / ".hidden" / "a.pk3", text);
    const std::vector<ModFolder> mods = listMods(tmp, "baseef");
    CHECK(mods.size() == 3);
    if (mods.size() == 3) {
        CHECK(mods[0].name == "modA" && mods[0].description == "modA");
        CHECK(mods[1].name == "mod_b" && mods[1].description == "A mod with a long description, longer than 48 by");
        CHECK(mods[2].name == "zeta" && mods[2].description == "zeta");
    }
    std::vector<uint8_t> out;
    CHECK(packModList(mods, 4096, out) == 3 && out.size() == 5 + 5 + 6 + 49 + 5 + 5);
    CHECK(out[0] == 'm' && out[4] == 0 && out[9] == 0);
    CHECK(packModList(mods, 13, out) == 1 && out.size() == 10); // the second doesn't fit: it ends there
    CHECK(packModList(mods, 12, out) == 0 && out.size() == 1);   // 2 bytes to spare, and then some
    CHECK(packModList({}, 8, out) == 0 && out == std::vector<uint8_t>{0});

    // A folder's loose files, in that order too.
    DirectoryArchive dir;
    test::writeFile(tmp / "list" / "b_x.txt", text);
    test::writeFile(tmp / "list" / "bA.txt", text);
    test::writeFile(tmp / "list" / "a.txt", text);
    CHECK(!dir.open(tmp / "list"));
    CHECK((dir.list() == std::vector<std::string>{"a.txt", "bA.txt", "b_x.txt"}));
}

// A mod's files over the game's: the install's, then the home folder's.
static void testMountMod(const std::filesystem::path& tmp) {
    std::filesystem::remove_all(tmp);
    const std::filesystem::path install = tmp / "install", home = tmp / "home";
    auto zip = [](const char* name, const char* text) { return test::buildZip({{name, test::toBytes(text), {}}}); };
    test::writeFile(install / "BaseEF" / "pak0.pk3", zip("a.txt", "base"));
    test::writeFile(install / "BaseEF" / "pak1.pk3", zip("b.txt", "base"));
    test::writeFile(install / "BaseEF" / "pak2.pk3", zip("c.txt", "base"));
    test::writeFile(home / "BaseEF" / "dl.pk3", zip("b.txt", "home base"));
    test::writeFile(install / "MyMod" / "zz.pk3", zip("a.txt", "mod"));
    test::writeFile(install / "MyMod" / "c.txt", test::toBytes("mod loose"));
    test::writeFile(home / "MyMod" / "dl.pk3", zip("c.txt", "home mod"));
    auto text = [](const SearchSet& set, const char* name) {
        std::vector<uint8_t> data;
        return set.read(name, data) ? std::string() : std::string(data.begin(), data.end());
    };
    SearchSet set;
    std::vector<std::string> warnings;
    CHECK(!mountGame(set, install, home, "BaseEF", "", warnings));
    CHECK(text(set, "a.txt") == "base" && text(set, "b.txt") == "home base" && text(set, "c.txt") == "base");
    CHECK(!mountGame(set, install, home, "BaseEF", "mymod", warnings)); // found ignoring case
    CHECK(text(set, "a.txt") == "mod" && text(set, "b.txt") == "home base" && text(set, "c.txt") == "home mod");
    CHECK(!mountGame(set, install, {}, "BaseEF", "MyMod", warnings));
    CHECK(text(set, "c.txt") == "mod loose");
    CHECK(!mountGame(set, install, home, "BaseEF", "NotThere", warnings) && text(set, "a.txt") == "base");
    CHECK(mountGame(set, tmp / "nothing", home, "BaseEF", "", warnings)); // the base game's folder is needed

    CHECK(modFolderName("MyMod", "baseEF") == "MyMod" && modFolderName("BASEEF", "baseEF").empty());
    CHECK(modFolderName("../x", "baseEF").empty() && modFolderName(".git", "baseEF").empty() &&
          modFolderName("a\\b", "baseEF").empty() && modFolderName("", "baseEF").empty());
    CHECK(modSavePath(home, "MyMod") == home / "mods" / "MyMod" && modSavePath(home, "") == home);
}

static const reluva::test::Suite suite("idtech3.unit.files", {
    {"filesystem and detection", [](const std::filesystem::path& tmp) { testFilesystemAndDetection(tmp / "fs"); }},
    {"vm file list", [](const std::filesystem::path&) { testVmFileList(); }},
    {"expansion file list", [](const std::filesystem::path& tmp) { testExpansionFileList(tmp / "filelist"); }},
    {"mod list", [](const std::filesystem::path& tmp) { testModList(tmp / "mods"); }},
    {"mounting mods", [](const std::filesystem::path& tmp) { testMountMod(tmp / "mount"); }},
});
