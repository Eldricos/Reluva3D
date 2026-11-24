// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/import.h"

#include "common/cue.h"
#include "common/fs.h"
#include "engines/sith/folders.h"
#include "engines/sith/games.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace reluva::sith {

namespace {

namespace stdfs = std::filesystem;

// What the installer copies from CD 1, and the names it gives them in the
// install (docs/engines/sith/games/jk/README.md).
struct InstalledFile {
    const char *onCd;
    const char *installed;
};

constexpr InstalledFile kInstalled[] = {
    {"GAMEDATA/EXE/JK.EXE", "JK.EXE"},
    {"GAMEDATA/EXE/SMACKW32.DLL", "SMACKW32.DLL"},
    {"GAMEDATA/MININSTALL/RES1HI.GOB", "Resource/Res1hi.gob"},
    {"GAMEDATA/RESOURCE/RES2.GOB", "Resource/Res2.gob"},
    {"GAMEDATA/EPISODE/JK1.GOB", "Episode/JK1.GOB"},
    {"GAMEDATA/EPISODE/JK1CTF.GOB", "Episode/JK1CTF.GOB"},
    {"GAMEDATA/EPISODE/JK1MP.GOB", "Episode/JK1MP.GOB"},
};

uint64_t sizeOf(const stdfs::path &file) {
    std::error_code ec;
    const auto size = stdfs::file_size(file, ec);
    return ec ? 0 : size;
}

// The regular files in `folder`, by their names on disk.
std::vector<stdfs::path> filesIn(const stdfs::path &folder) {
    std::vector<stdfs::path> out;
    std::error_code ec;
    for (const auto &e : stdfs::directory_iterator(folder, ec))
        if (e.is_regular_file(ec))
            out.push_back(e.path());
    std::sort(out.begin(), out.end());
    return out;
}

// The same in `relative` under `dir`.
std::vector<stdfs::path> filesIn(const stdfs::path &dir, std::string_view relative) {
    const auto folder = fs::findDirectory(dir, relative);
    return folder ? filesIn(*folder) : std::vector<stdfs::path>();
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

// A CD's music beside its files, as the game's CD audio plays it: a cue
// sheet with the images holding its audio tracks (a data track's alone is
// left behind, as the files are copied apart), or tracks ripped as
// trackNN.wav, .mp3, .ogg or .flac.
void addCdAudio(ImportPlan &plan, const stdfs::path &cd, const std::string &prefix) {
    std::set<stdfs::path> images;
    for (const stdfs::path &file : filesIn(cd)) {
        const std::string name = lower(file.filename().string());
        const std::string ext = lower(file.extension().string());
        if (ext == ".cue") {
            std::vector<CueTrack> tracks;
            if (readCueSheet(file, tracks))
                continue;
            bool audio = false;
            for (const CueTrack &t : tracks)
                if (t.audio) {
                    audio = true;
                    images.insert(t.file);
                }
            if (audio)
                plan.files.push_back(
                    {file, prefix + file.filename().string(), sizeOf(file), std::nullopt, std::nullopt});
        } else if (name.size() == 7 + ext.size() && name.rfind("track", 0) == 0 && std::isdigit(uint8_t(name[5])) &&
                   std::isdigit(uint8_t(name[6])) &&
                   (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac")) {
            plan.files.push_back({file, prefix + file.filename().string(), sizeOf(file), std::nullopt, std::nullopt});
        }
    }
    for (const stdfs::path &image : images)
        plan.files.push_back({image, prefix + image.filename().string(), sizeOf(image), std::nullopt, std::nullopt});
}

// A CD's marker, cut scenes and music, kept under `cd<n>/` with the CD's
// own paths, where findFolders and the CD audio look for them.
void addCdFiles(ImportPlan &plan, const stdfs::path &cd, int number) {
    const std::string folder = "cd" + std::to_string(number) + "/";
    const std::string prefix = folder + "GAMEDATA/RESOURCE/";
    if (auto mark = fs::findFile(cd, "GAMEDATA/RESOURCE/JK_.CD"))
        plan.files.push_back({*mark, prefix + "JK_.CD", sizeOf(*mark), std::nullopt, std::nullopt});
    for (const stdfs::path &video : filesIn(cd, "GAMEDATA/RESOURCE/VIDEO"))
        plan.files.push_back(
            {video, prefix + "VIDEO/" + video.filename().string(), sizeOf(video), std::nullopt, std::nullopt});
    addCdAudio(plan, cd, folder);
}

bool hasCd(const ImportPlan &plan, int number) {
    const std::string prefix = "cd" + std::to_string(number) + "/";
    return std::any_of(plan.files.begin(), plan.files.end(),
                       [&](const ImportPlan::File &f) { return f.to.rfind(prefix, 0) == 0; });
}

constexpr const char *kNoCd2 = "CD 2 has the cut scenes of levels 9 to 21: add it with \"Add other disc...\", or "
                               "keep a copy of it beside the game";

// The note asking for CD 2, while it isn't in the plan; the others stay.
void setNotes(ImportPlan &plan) {
    plan.notes.erase(std::remove(plan.notes.begin(), plan.notes.end(), kNoCd2), plan.notes.end());
    if (!hasCd(plan, 2))
        plan.notes.push_back(kNoCd2);
}

stdfs::path folderOf(const stdfs::path &source) {
    std::error_code ec;
    return stdfs::is_directory(source, ec) ? source : source.parent_path();
}

} // namespace

std::vector<ImportPlan> planImport(const stdfs::path &source) {
    const stdfs::path cd = folderOf(source);
    if (cdNumber(cd) != 1)
        return {};
    const std::vector<DetectedGame> found = detectCd(cd);
    if (found.empty())
        return {};

    ImportPlan plan;
    plan.game = found.front();
    std::string missing;
    for (const InstalledFile &f : kInstalled) {
        if (auto from = fs::findFile(cd, f.onCd))
            plan.files.push_back({*from, f.installed, sizeOf(*from), std::nullopt, std::nullopt});
        else
            missing += (missing.empty() ? "" : ", ") + std::string(f.onCd);
    }
    for (const stdfs::path &controls : filesIn(cd, "GAMEDATA/CONTROLS"))
        plan.files.push_back(
            {controls, "Controls/" + controls.filename().string(), sizeOf(controls), std::nullopt, std::nullopt});
    addCdFiles(plan, cd, 1);
    if (!missing.empty())
        plan.notes.push_back("Not on this copy of the CD, so the game won't run: " + missing);
    setNotes(plan);
    plan.addPatchLabel.clear();
    plan.addFolderLabel = "Add other disc...";
    plan.addFolderTooltip = "Copy CD 2's cut scenes too, from the CD or a copy of it";
    return {plan};
}

bool addToImport(ImportPlan &plan, const stdfs::path &folder, std::string &message) {
    const stdfs::path cd = folderOf(folder);
    const int number = cdNumber(cd);
    if (number == 0) {
        message = "not one of Jedi Knight's CDs, nor a copy of one";
        return false;
    }
    if (hasCd(plan, number)) {
        message = "CD " + std::to_string(number) + " is being copied already";
        return false;
    }
    addCdFiles(plan, cd, number);
    setNotes(plan);
    return true;
}

} // namespace reluva::sith
