// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool's commands for Star Trek: Armada's text files
// (docs/engines/storm3d/games/armada/formats/).
//
//   reluva-tool storm3d odf <game folder> <name.odf>
//   reluva-tool storm3d odf-check <game folder>
//   reluva-tool storm3d config <game folder> <file>
//   reluva-tool storm3d config-check <game folder>
//   reluva-tool storm3d ldl <game folder> <file.ldl> [output file]
//   reluva-tool storm3d input-map <Input.map>
//   reluva-tool storm3d spr <game folder> <table.spr>
//   reluva-tool storm3d spr-check <game folder>

#include "common/archive.h"
#include "common/fs.h"
#include "common/text.h"
#include "engines/storm3d/formats/config_file.h"
#include "engines/storm3d/formats/input_map.h"
#include "engines/storm3d/formats/ldl.h"
#include "engines/storm3d/formats/odf.h"
#include "engines/storm3d/formats/sprite_table.h"
#include "engines/storm3d/game_files.h"
#include "engines/storm3d/formats/zfs_archive.h"
#include "devtools/tool.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

using reluva::Error;
using reluva::SearchSet;
using reluva::storm3d::ConfigFile;
using reluva::storm3d::Odf;
using reluva::storm3d::ZfsArchive;

// The install's own files over assets.zfs's: which the game prefers isn't
// known yet, and the two hold no name in common.
Error openGameFiles(const std::string &folder, SearchSet &files) {
    auto loose = std::make_unique<reluva::DirectoryArchive>();
    if (Error err = loose->open(folder, 0))
        return err;
    auto assets = std::make_unique<ZfsArchive>();
    if (Error err = assets->open(std::filesystem::path(folder) / "assets.zfs"))
        return err;
    files.add(std::move(loose), 1);
    files.add(std::move(assets), 0);
    return Error::success();
}

Odf::Loader loaderFor(const SearchSet &files) {
    return [&files](std::string_view name, std::string &text) {
        std::vector<uint8_t> data;
        if (Error err = files.read(name, data))
            return err;
        text.assign(data.begin(), data.end());
        return Error::success();
    };
}

int cmdOdf(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool storm3d odf <game folder> <name.odf>\n");
        return 2;
    }
    SearchSet files;
    if (Error err = openGameFiles(argv[2], files)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    Odf odf;
    if (Error err = odf.load(argv[3], loaderFor(files))) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    for (const Odf::Entry &e : odf.entries()) {
        std::string value = e.value;
        for (char &c : value)
            if (c == '\n')
                c = ' ';
        std::printf("%-14s %-18s %-28s %s\n", e.file.c_str(), e.section.c_str(), e.key.c_str(),
                    reluva::text::windows1252ToUtf8(value).c_str());
    }
    for (const std::string &p : odf.problems())
        std::printf("not read: %s\n", p.c_str());
    return 0;
}

int cmdOdfCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool storm3d odf-check <game folder>\n");
        return 2;
    }
    SearchSet files;
    if (Error err = openGameFiles(argv[2], files)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    int read = 0;
    int failed = 0;
    size_t entries = 0;
    size_t problems = 0;
    for (const std::string &name : files.list()) {
        if (!reluva::text::hasSuffixIgnoreCase(name, ".odf"))
            continue;
        // events.odf is the announcements' blocks, another format.
        std::vector<uint8_t> head;
        if (!files.read(name, head) && std::string(head.begin(), head.end()).rfind("#EVENT", 0) == 0) {
            std::printf("skipped: %s, an events file\n", name.c_str());
            continue;
        }
        Odf odf;
        if (Error err = odf.load(name, loaderFor(files))) {
            std::printf("%s\n", err.message.c_str());
            ++failed;
            continue;
        }
        ++read;
        entries += odf.entries().size();
        problems += odf.problems().size();
        for (const std::string &p : odf.problems())
            std::printf("not read: %s\n", p.c_str());
    }
    std::printf("%d files read, %d failed; %zu entries, includes resolved; %zu lines not read\n", read, failed, entries,
                problems);
    return failed ? 1 : 0;
}

// The game's settings files in the C-like language, from the install's
// folder (docs/engines/storm3d/games/armada/formats/config.md).
const char *const kConfigFiles[] = {"RTS_CFG.h", "game_cfg.h",    "shell_cfg.h", "Dynamic_Localized_Strings.h",
                                    "CCA.cfg",   "Collision.cfg", "woncfg.cfg",  "AI/strategic_AI_config.txt"};

// A file is looked for beside the one including it, then in the install's
// folder, ignoring case as Windows does.
ConfigFile::Loader configLoader(const std::filesystem::path &root, const std::filesystem::path &first) {
    return [root, dir = first.parent_path()](std::string_view name, std::string &text) {
        std::optional<std::filesystem::path> path = reluva::fs::findFile(root / dir, name);
        if (!path)
            path = reluva::fs::findFile(root, name);
        if (!path)
            return Error::fail(std::string(name) + " isn't in " + (root / dir).string() + " or " + root.string());
        std::ifstream in(*path, std::ios::binary);
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return Error::success();
    };
}

std::string describe(const ConfigFile::Value &v) {
    switch (v.kind) {
    case ConfigFile::Value::Kind::kNumber:
        return reluva::text::format("%g", v.number);
    case ConfigFile::Value::Kind::kString: {
        std::string text = reluva::text::windows1252ToUtf8(v.text);
        for (char &c : text)
            if (c == '\n')
                c = '|';
        return "\"" + text + "\"";
    }
    case ConfigFile::Value::Kind::kList: {
        std::string out = "{";
        for (size_t i = 0; i < v.items.size(); ++i)
            out += (i ? ", " : "") + describe(v.items[i]);
        return out + "}";
    }
    }
    return "";
}

int cmdConfig(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool storm3d config <game folder> <file>\n");
        return 2;
    }
    ConfigFile config;
    if (Error err = config.load(std::filesystem::path(argv[3]).filename().string(), configLoader(argv[2], argv[3]))) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    for (const auto &[name, s] : config.structs()) {
        std::string fields;
        for (const std::string &field : s.fields)
            fields += (fields.empty() ? "" : ", ") + field;
        std::printf("struct %s: %s\n", name.c_str(), fields.c_str());
    }
    for (const auto &[name, text] : config.defines()) {
        const ConfigFile::Value *v = config.find(name);
        std::printf("#define %-40s %s\n", name.c_str(), v ? describe(*v).c_str() : ("(text) " + text).c_str());
    }
    for (const auto &[name, v] : config.variables())
        std::printf("%-48s %s\n", name.c_str(), describe(v).c_str());
    for (const std::string &p : config.problems())
        std::printf("skipped: %s\n", p.c_str());
    return 0;
}

int cmdConfigCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool storm3d config-check <game folder>\n");
        return 2;
    }
    const std::filesystem::path root = argv[2];
    std::vector<std::filesystem::path> files;
    for (const char *name : kConfigFiles)
        files.emplace_back(name);
    // The computer player's personalities, beside their definitions.
    std::error_code ec;
    for (const char *dir : {"AI/AIPs", "misc"})
        if (const auto found = reluva::fs::findDirectory(root, dir))
            for (const auto &entry : std::filesystem::directory_iterator(*found, ec))
                if (reluva::text::hasSuffixIgnoreCase(entry.path().filename().string(), ".aip"))
                    files.push_back(std::filesystem::path(dir) / entry.path().filename());
    int read = 0;
    int failed = 0;
    size_t variables = 0;
    size_t defines = 0;
    for (const std::filesystem::path &file : files) {
        ConfigFile config;
        if (Error err = config.load(file.filename().string(), configLoader(root, file))) {
            std::printf("%s: %s\n", file.generic_string().c_str(), err.message.c_str());
            ++failed;
            continue;
        }
        ++read;
        variables += config.variables().size();
        defines += config.defines().size();
        for (const std::string &p : config.problems())
            std::printf("%s: skipped: %s\n", file.generic_string().c_str(), p.c_str());
    }
    std::printf("%d files read, %d failed; %zu variables, %zu defines, includes counted in each\n", read, failed,
                variables, defines);
    return failed ? 1 : 0;
}

int cmdLdl(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool storm3d ldl <game folder> <file.ldl> [output file]\n");
        return 2;
    }
    const auto exe = reluva::fs::findFile(argv[2], "Armada.exe");
    if (!exe) {
        std::fprintf(stderr, "Armada.exe isn't in %s\n", argv[2]);
        return 1;
    }
    std::vector<uint8_t> key;
    if (Error err = reluva::storm3d::readLdlKey(*exe, key)) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    std::ifstream in(argv[3], std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "Cannot open %s\n", argv[3]);
        return 1;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    reluva::storm3d::decodeLdl(data, key);
    if (argc > 4) {
        std::ofstream out(argv[4], std::ios::binary);
        if (!out.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()))) {
            std::fprintf(stderr, "Cannot write %s\n", argv[4]);
            return 1;
        }
        return 0;
    }
    const std::string text(data.begin(), data.end());
    std::fputs(reluva::text::windows1252ToUtf8(text).c_str(), stdout);
    return 0;
}

int cmdInputMap(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool storm3d input-map <Input.map>\n");
        return 2;
    }
    std::ifstream in(argv[2], std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "Cannot open %s\n", argv[2]);
        return 1;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    reluva::storm3d::InputMap map;
    if (Error err = map.parse(text)) {
        std::fprintf(stderr, "%s: %s\n", argv[2], err.message.c_str());
        return 1;
    }
    for (const auto &b : map.bindings) {
        std::string conditions;
        for (const auto &c : b.conditions)
            conditions +=
                (conditions.empty() ? "" : "  ") + std::string(c.down ? "+" : "-") + c.device + ":" + c.control;
        std::printf("%-28s %s\n", b.action.c_str(), conditions.c_str());
    }
    for (const std::string &p : map.problems)
        std::printf("not read: %s\n", p.c_str());
    std::printf("%zu actions\n", map.bindings.size());
    return 0;
}

// The sprite tables are in Sprites/, and include each other by name alone.
reluva::storm3d::SpriteTable::Loader spriteLoader(const reluva::storm3d::GameFiles &files) {
    return [&files](std::string_view name, std::string &text) {
        std::vector<uint8_t> data;
        if (Error err = files.read("Sprites/" + std::string(name), data))
            return err;
        text.assign(data.begin(), data.end());
        return Error::success();
    };
}

int cmdSpr(int argc, char **argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: reluva-tool storm3d spr <game folder> <table.spr>\n");
        return 2;
    }
    reluva::storm3d::GameFiles files;
    reluva::storm3d::SpriteTable table;
    if (Error err = files.open(argv[2]); err || (err = table.load(argv[3], spriteLoader(files)))) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    for (const auto &s : table.sprites()) {
        std::string params;
        for (const std::string &a : s.animations)
            params += " anim=" + a;
        for (const auto &[k, v] : s.parameters)
            params += " " + k + (v.empty() ? "" : "=" + v);
        std::printf("sprite %-24s %-14s %g,%g %gx%g of %dx%d%s\n", s.name.c_str(), s.texture.c_str(), s.u, s.v, s.w,
                    s.h, s.referenceWidth, s.referenceHeight, params.c_str());
    }
    for (const auto &[name, a] : table.animations())
        std::printf("animation %-24s %s %d %g %s%s, %zu keyframes\n", name.c_str(), a.kind.c_str(), a.count, a.duration,
                    a.interpolation.c_str(), a.autoLayout.empty() ? "" : (" auto=" + a.autoLayout).c_str(),
                    a.keyframes.size());
    for (const auto &e : table.emitters())
        std::printf("emitter %s: %zu settings\n", e.name.c_str(), e.settings.size());
    std::printf("%zu sprites, %zu animations, %zu emitters, %zu sprite nodes\n", table.sprites().size(),
                table.animations().size(), table.emitters().size(), table.spriteNodes().size());
    for (const std::string &p : table.problems())
        std::printf("not read: %s\n", p.c_str());
    return 0;
}

int cmdSprCheck(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: reluva-tool storm3d spr-check <game folder>\n");
        return 2;
    }
    reluva::storm3d::GameFiles files;
    if (Error err = files.open(argv[2])) {
        std::fprintf(stderr, "%s\n", err.message.c_str());
        return 1;
    }
    int read = 0;
    int failed = 0;
    size_t problems = 0;
    std::set<std::string> animations;
    std::multimap<std::string, std::string> named; // an animation -> "table: sprite" using it
    for (const std::string &path : files.list()) {
        if (!reluva::text::hasSuffixIgnoreCase(path, ".spr"))
            continue;
        const std::string name = std::filesystem::path(path).filename().string();
        reluva::storm3d::SpriteTable table;
        if (Error err = table.load(name, spriteLoader(files))) {
            std::printf("%s: %s\n", name.c_str(), err.message.c_str());
            ++failed;
            continue;
        }
        ++read;
        problems += table.problems().size();
        for (const std::string &p : table.problems())
            std::printf("not read: %s\n", p.c_str());
        for (const auto &[an, a] : table.animations())
            if (a.autoLayout.empty() && int(a.keyframes.size()) != a.count)
                std::printf("%s: %s has %zu keyframes, its header says %d\n", name.c_str(), an.c_str(),
                            a.keyframes.size(), a.count);
        for (const auto &[an, a] : table.animations())
            animations.insert(an);
        for (const auto &sp : table.sprites())
            for (const std::string &a : sp.animations)
                named.insert({a, name + ": " + sp.name});
    }
    // An animation may be in another table than its sprite's: tex_anim.spr's
    // are used by most of the others.
    for (const auto &[a, user] : named)
        if (!animations.count(a))
            std::printf("%s names animation %s, which no table has\n", user.c_str(), a.c_str());
    std::printf("%d tables read, %d failed, %zu lines not read\n", read, failed, problems);
    return failed ? 1 : 0;
}

const reluva::tool::Commands kCommands(
    "storm3d",
    {
        {"odf", "<game folder> <name.odf>",
         "an object definition with the files it includes: file, section, key and value of each entry", cmdOdf},
        {"odf-check", "<game folder>",
         "read every object definition, in the install's folder and assets.zfs, and report what fails", cmdOdfCheck},
        {"config", "<game folder> <file>",
         "a settings file in the game's C-like language: its structures, defines and variables", cmdConfig},
        {"config-check", "<game folder>",
         "read every settings file in the C-like language, the AI's personalities too, and report what fails",
         cmdConfigCheck},
        {"ldl", "<game folder> <file.ldl> [output file]",
         "an .ldl file decoded with the key from the install's Armada.exe, printed or written", cmdLdl},
        {"input-map", "<Input.map>", "the actions in an Input.map, each with the controls that trigger it",
         cmdInputMap},
        {"spr", "<game folder> <table.spr>", "a sprite table with what it includes: sprites, animations, emitters",
         cmdSpr},
        {"spr-check", "<game folder>", "read every sprite table, and report what isn't read or doesn't add up",
         cmdSprCheck},
    });

} // namespace
