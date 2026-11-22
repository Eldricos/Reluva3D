// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/games.h"

#include "common/fs.h"
#include "common/installshield.h"

#include <algorithm>
#include <memory>
#include <map>
#include <string>
#include <system_error>

namespace reluva::tpw {

const std::vector<GameInfo> &games() {
    static const std::vector<GameInfo> kList = {
        {"simcoaster", "SimCoaster"},
    };
    return kList;
}

const GameInfo *findGame(std::string_view gameId) {
    for (const GameInfo &g : games())
        if (gameId == g.gameId)
            return &g;
    return nullptr;
}

// Detection keys on the data files, not Game.exe: the executable is
// SafeDisc's, and only has to exist. ui.wad holds the front end; the
// American string tables tell the US release from the European ones,
// whose data is the same game (docs/engines/tpw/games/simcoaster/README.md).
const std::vector<GameVariant> &detectionTable() {
    static const std::vector<GameVariant> kTable = {
        // US release, CD "Theme Park Inc", data version Beta_21.
        {"simcoaster",
         "SimCoaster (US)",
         "en",
         "windows",
         {{"Data/ui.wad", "f8caf97175f18ef3675b3609b03413f4", 2817221},
          {"Data/Language/American/OBJECT_NAMES.str", "04d5bf6ef8a6d85f15a25a33716fd895", 10200},
          {"Game.exe", "", -1}}},
    };
    return kTable;
}

// Challenges.sam is SimCoaster's own: Theme Park World has no challenges
// (docs/engines/tpw/games/simcoaster/UNCONFIRMED.md).
const std::vector<GameVariant> &fallbackTable() {
    static const std::vector<GameVariant> kTable = {
        {"simcoaster",
         "SimCoaster (unknown release)",
         "",
         "windows",
         {{"Data/ui.wad", "", -1}, {"Data/Challenges.sam", "", -1}, {"Game.exe", "", -1}}},
    };
    return kTable;
}

namespace {

// The cabinet's group of the game's data, installed in Data/.
constexpr const char *kDataGroup = "Data";

const InstallShieldGroup *findGroup(const InstallShieldCabinet &cabinet, std::string_view name) {
    for (const InstallShieldGroup &g : cabinet.groups)
        if (g.name == name)
            return &g;
    return nullptr;
}

// Whether `variant`'s files are on the CD: its Data/ files in the data
// group with their sizes, the rest beside data1.hdr. The MD5s are of what's
// unpacked, so not known yet: detection checks them once it's imported.
bool matches(const GameVariant &variant, const std::filesystem::path &source, const InstallShieldCabinet &cabinet,
             const InstallShieldGroup &data) {
    return std::all_of(variant.files.begin(), variant.files.end(), [&](const FileSignature &sig) {
        const std::string_view name = sig.name;
        if (name.substr(0, 5) != "Data/")
            return fs::findFile(source, name).has_value();
        const long i = cabinet.find(data, name.substr(5));
        return i >= 0 && (sig.size < 0 || cabinet.files[size_t(i)].size == uint64_t(sig.size));
    });
}

// The language's folder, as the data group's string tables name it
// ("Language/American/...").
std::string languageFolder(const InstallShieldCabinet &cabinet, const InstallShieldGroup &data) {
    for (size_t i = data.first; i <= data.last && i < cabinet.files.size(); ++i) {
        const std::string &path = cabinet.files[i].path;
        if (fs::toLower(path.substr(0, 9)) != "language/")
            continue;
        const size_t end = path.find('/', 9);
        if (end != std::string::npos)
            return path.substr(9, end - 9);
    }
    return {};
}

} // namespace

std::vector<ImportPlan> planImport(const std::filesystem::path &source) {
    const auto header = fs::findFile(source, "data1.hdr");
    const auto exe = fs::findFile(source, "Game.exe");
    if (!header || !exe)
        return {};
    auto cabinet = std::make_shared<InstallShieldCabinet>();
    if (readInstallShieldCabinet(*header, *cabinet))
        return {};
    const InstallShieldGroup *data = findGroup(*cabinet, kDataGroup);
    if (!data)
        return {};
    const GameVariant *variant = nullptr;
    for (const auto *table : {&detectionTable(), &fallbackTable()})
        for (const GameVariant &v : *table)
            if (!variant && matches(v, source, *cabinet, *data))
                variant = &v;
    if (!variant)
        return {};

    ImportPlan plan;
    plan.game = {"tpw", variant->gameId, variant->description, variant->language, variant->platform};
    plan.cabinet = cabinet;
    plan.addPatchLabel.clear();
    plan.addFolderLabel.clear();
    // Where each file is going, in lower case, and its place in the plan:
    // a later file of the same name takes an earlier one's place, as
    // setup copies the CD's folders over the cabinet's files.
    std::map<std::string, size_t> planned;
    auto add = [&](ImportPlan::File file) {
        const auto [it, added] = planned.emplace(fs::toLower(file.to), plan.files.size());
        if (added)
            plan.files.push_back(std::move(file));
        else
            plan.files[it->second] = std::move(file);
    };
    bool unreachable = false;
    auto addFromCabinet = [&](size_t i, const std::string &to) {
        unreachable = unreachable || !cabinet->reachable(i);
        add({*header, to, cabinet->files[i].size, std::nullopt, i});
    };

    // As setup installs them: the data group in Data/, the opening movie
    // and the sound's reverb settings beside Game.exe. The group's DLLs
    // (QSound's mixer, the online services) and SafeDisc's files aren't
    // needed, as the game isn't run.
    for (size_t i = data->first; i <= data->last && i < cabinet->files.size(); ++i)
        if (!cabinet->files[i].path.empty())
            addFromCabinet(i, "Data/" + cabinet->files[i].path);
    for (const char *name : {"TPinc30.avi", "SndReverb.map"})
        for (const InstallShieldGroup &g : cabinet->groups)
            if (const long i = cabinet->find(g, name); i >= 0) {
                addFromCabinet(size_t(i), name);
                break;
            }
    std::error_code ec;
    add({*exe, "Game.exe", std::filesystem::file_size(*exe, ec), std::nullopt, std::nullopt});

    // The CD's loose folders, each the language's own, which setup copies
    // as they are. Eleven of their files (the speech, the postcards'
    // pictures) are in the data group too, but not the same: these are
    // the ones installed.
    const std::string language = languageFolder(*cabinet, *data);
    const struct {
        std::string from;
        const char *to;
    } kLoose[] = {
        {"Speech/" + language, "Data/global/Speech"},
        {"CoasterData/" + language, "Data/my coasters"},
        {"PostCardData/" + language + "/postcard", "Data/postcard"},
        {"PostCardData/" + language + "/My Postcards", "My Postcards"},
    };
    for (const auto &loose : kLoose) {
        const auto dir = language.empty() ? std::nullopt : fs::findDirectory(source, loose.from);
        if (!dir) {
            plan.notes.push_back("The CD's " + loose.from + " folder isn't there: its files are left out");
            continue;
        }
        std::vector<std::filesystem::path> found;
        for (const auto &entry : std::filesystem::recursive_directory_iterator(*dir, ec))
            if (entry.is_regular_file() && fs::toLower(entry.path().filename().string()) != "vssver.scc")
                found.push_back(entry.path());
        std::sort(found.begin(), found.end());
        for (const auto &file : found)
            add({file, std::string(loose.to) + "/" + file.lexically_relative(*dir).generic_string(),
                 std::filesystem::file_size(file, ec), std::nullopt, std::nullopt});
    }
    if (unreachable)
        plan.notes.push_back("Some of the game's files are in a cabinet volume (data<n>.cab) that isn't beside "
                             "data1.hdr");
    return {plan};
}

} // namespace reluva::tpw
