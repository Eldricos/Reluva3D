// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Recognising LEGO Island, and importing it from its CD or an install
// (docs/engines/legoisland/systems/game-files.md).

#include "engines/legoisland/detection.h"

#include "common/fs.h"
#include "common/text.h"

#include <algorithm>
#include <system_error>

namespace reluva::legoisland {

namespace {

// By the game's code and its world database, as the installer copies them.
const std::vector<GameVariant> kVariants = {
    {kGameId,
     "LEGO Island (English 1.0)",
     "en",
     "windows",
     {{"LEGO1.DLL", "b7ca2f80366a427797cc3f820c48a0f1", 1134080},
      {"lego/data/WORLD.WDB", "a2de77ebfa5164ec949f19cd5c7bb58c", 9944860}}},
};

// The same game by its files' names alone, for releases not in kVariants
// (1.1, other languages): used only when nothing there matches.
const std::vector<GameVariant> kFallback = {
    {kGameId, "LEGO Island (unknown release)", "", "windows", {{"LEGO1.DLL", "", -1}, {"lego/data/WORLD.WDB", "", -1}}},
};

// Where an imported game keeps the CD's archives, beside the installer's
// NOCD.SI: the folder as the installer names it.
constexpr const char *kScriptsFolder = "lego/Scripts/";

std::vector<std::string> listTree(const std::filesystem::path &dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_regular_file(ec))
            out.push_back(std::filesystem::relative(it->path(), dir, ec).generic_string());
    }
    std::sort(out.begin(), out.end(),
              [](const std::string &a, const std::string &b) { return fs::windowsNameLess(a, b); });
    return out;
}

uint64_t sizeOf(const std::filesystem::path &p) {
    std::error_code ec;
    const auto n = std::filesystem::file_size(p, ec);
    return ec ? 0 : uint64_t(n);
}

bool planned(const ImportPlan &plan, const std::string &to) {
    return std::any_of(plan.files.begin(), plan.files.end(),
                       [&](const ImportPlan::File &f) { return text::equalsIgnoreCase(f.to, to); });
}

bool isInstall(const std::filesystem::path &dir) {
    return fs::findFile(dir, "LEGO1.DLL") && fs::findFile(dir, "lego/data/WORLD.WDB");
}

// The CD's scripts: any of its archives but NOCD.SI, which the installer
// copies too.
bool hasScripts(const std::filesystem::path &dir) {
    return std::any_of(cdScripts().begin(), cdScripts().end(), [&](const std::string &name) {
        return !text::equalsIgnoreCase(name, "NOCD.SI") && fs::findFile(dir, "LEGO/SCRIPTS/" + name);
    });
}

// An install's file where an import puts it: the folders under lego/
// spelt as the installer spells them, whatever case the copy has (the CD
// has LEGO/DATA), so the CD's scripts join the same folder.
std::string installedPath(const std::string &rel) {
    const size_t first = rel.find('/');
    if (first == std::string::npos || !text::equalsIgnoreCase(rel.substr(0, first), "lego"))
        return rel;
    const size_t second = rel.find('/', first + 1);
    if (second == std::string::npos)
        return "lego" + rel.substr(first);
    const std::string folder = rel.substr(first + 1, second - first - 1);
    if (text::equalsIgnoreCase(folder, "data"))
        return "lego/data" + rel.substr(second);
    if (text::equalsIgnoreCase(folder, "scripts"))
        return "lego/Scripts" + rel.substr(second);
    return "lego" + rel.substr(first);
}

} // namespace

std::vector<DetectedGame> detectGame(const std::string &engineId, const std::filesystem::path &dir) {
    std::vector<DetectedGame> found = detectFromTable(engineId, kVariants, dir);
    return found.empty() ? detectFromTable(engineId, kFallback, dir) : found;
}

const std::vector<std::string> &cdScripts() {
    static const std::vector<std::string> kScripts = {
        "ACT2/ACT2MAIN.SI",     "ACT3/ACT3.SI",         "BUILD/COPTER.SI",      "BUILD/DUNECAR.SI",
        "BUILD/JETSKI.SI",      "BUILD/RACECAR.SI",     "CREDITS.SI",           "GARAGE/GARAGE.SI",
        "HOSPITAL/HOSPITAL.SI", "INFOCNTR/ELEVBOTT.SI", "INFOCNTR/HISTBOOK.SI", "INFOCNTR/INFODOOR.SI",
        "INFOCNTR/INFOMAIN.SI", "INFOCNTR/INFOSCOR.SI", "INFOCNTR/REGBOOK.SI",  "INTRO.SI",
        "ISLE/ISLE.SI",         "ISLE/JUKEBOX.SI",      "ISLE/JUKEBOXW.SI",     "NOCD.SI",
        "POLICE/POLICE.SI",     "RACE/CARRACE.SI",      "RACE/CARRACER.SI",     "RACE/JETRACE.SI",
        "RACE/JETRACER.SI",     "SNDANIM.SI",
    };
    return kScripts;
}

std::vector<GamePart> findGameParts(const std::filesystem::path &dir) {
    std::vector<GamePart> parts;
    auto look = [&](const std::filesystem::path &d) {
        if (isInstall(d))
            parts.push_back({GamePart::kInstall, d});
        if (const auto disk = fs::findDirectory(d, "DATA/DISK"); disk && isInstall(*disk))
            parts.push_back({GamePart::kInstall, *disk});
        if (hasScripts(d))
            parts.push_back({GamePart::kScripts, d});
    };
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec))
        return parts;
    look(dir);
    std::vector<std::filesystem::path> children;
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec))
        if (entry.is_directory(ec))
            children.push_back(entry.path());
    std::sort(children.begin(), children.end(), [](const auto &a, const auto &b) {
        return fs::windowsNameLess(a.filename().string(), b.filename().string());
    });
    for (const auto &child : children)
        look(child);
    std::vector<GamePart> unique;
    for (const GamePart &p : parts)
        if (std::none_of(unique.begin(), unique.end(), [&](const GamePart &u) { return u.kind == p.kind; }))
            unique.push_back(p);
    return unique;
}

void addPartToImport(ImportPlan &plan, const GamePart &part) {
    if (part.kind == GamePart::kInstall) {
        for (const std::string &rel : listTree(part.dir)) {
            const std::string to = installedPath(rel);
            if (!planned(plan, to))
                plan.files.push_back({part.dir / rel, to, sizeOf(part.dir / rel), std::nullopt, std::nullopt});
        }
        return;
    }
    const std::filesystem::path scripts = *fs::findDirectory(part.dir, "LEGO/SCRIPTS");
    for (const std::string &rel : listTree(scripts)) {
        const std::string to = kScriptsFolder + rel;
        if (!planned(plan, to))
            plan.files.push_back({scripts / rel, to, sizeOf(scripts / rel), std::nullopt, std::nullopt});
    }
}

void noteImport(ImportPlan &plan) {
    plan.notes.clear();
    if (!planned(plan, "LEGO1.DLL") || !planned(plan, "lego/data/WORLD.WDB"))
        plan.notes.push_back("LEGO Island's own files aren't here: add its CD (or a copy of it), or the folder "
                             "it was installed to (the one with ISLE.EXE).");
    std::vector<std::string> missing;
    bool anyScripts = false;
    for (const std::string &name : cdScripts()) {
        if (!planned(plan, kScriptsFolder + name))
            missing.push_back(name);
        else if (!text::equalsIgnoreCase(name, "NOCD.SI"))
            anyScripts = true;
    }
    if (!anyScripts) {
        plan.notes.push_back("The CD's LEGO\\SCRIPTS folder isn't here: add the CD (or a copy of it). The game "
                             "plays its worlds from it, and can't run without it.");
    } else if (!missing.empty()) {
        std::string list;
        for (size_t i = 0; i < missing.size() && i < 4; i++)
            list += (i ? ", " : "") + missing[i];
        if (missing.size() > 4)
            list += ", ...";
        plan.notes.push_back("From the CD's LEGO\\SCRIPTS folder, not found: " + list +
                             ". The CD's copy may be incomplete, or another release's.");
    }
    if (plan.notes.empty())
        plan.notes.push_back("The game's files and its CD's: everything the game needs.");
}

} // namespace reluva::legoisland
