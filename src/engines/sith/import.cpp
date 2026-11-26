// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/import.h"

#include "common/cue.h"
#include "common/fs.h"
#include "common/iso9660.h"
#include "common/md5.h"
#include "engines/sith/folders.h"
#include "engines/sith/games.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <memory>
#include <set>

namespace reluva::sith {

namespace {

namespace stdfs = std::filesystem;

// What each game's installer copies from its CD 1, and the names it gives
// them in the install (docs/engines/sith/games/jk/README.md,
// docs/engines/sith/games/mots/README.md).
struct InstalledFile {
    const char *onCd;
    const char *installed;
};

constexpr InstalledFile kJkInstalled[] = {
    {"GAMEDATA/EXE/JK.EXE", "JK.EXE"},
    {"GAMEDATA/EXE/SMACKW32.DLL", "SMACKW32.DLL"},
    {"GAMEDATA/MININSTALL/RES1HI.GOB", "Resource/Res1hi.gob"},
    {"GAMEDATA/RESOURCE/RES2.GOB", "Resource/Res2.gob"},
    {"GAMEDATA/EPISODE/JK1.GOB", "Episode/JK1.GOB"},
    {"GAMEDATA/EPISODE/JK1CTF.GOB", "Episode/JK1CTF.GOB"},
    {"GAMEDATA/EPISODE/JK1MP.GOB", "Episode/JK1MP.GOB"},
};

constexpr InstalledFile kMotsInstalled[] = {
    {"GAMEDATA/EXE/JKM.EXE", "JKM.EXE"},
    {"GAMEDATA/EXE/OLHOOK.DLL", "OLHOOK.DLL"},
    {"GAMEDATA/MININSTALL/JKMSNDLO.GOO", "Resource/JKMsndLO.goo"},
    {"GAMEDATA/RESOURCE/JKMRES.GOO", "Resource/JKMRES.GOO"},
    {"GAMEDATA/EPISODE/JKM.GOO", "Episode/JKM.GOO"},
    {"GAMEDATA/EPISODE/JKM_KFY.GOO", "Episode/JKM_KFY.GOO"},
    {"GAMEDATA/EPISODE/JKM_MP.GOO", "Episode/JKM_MP.GOO"},
    {"GAMEDATA/EPISODE/JKM_SABER.GOO", "Episode/JKM_SABER.GOO"},
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

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

stdfs::path folderOf(const stdfs::path &source) {
    std::error_code ec;
    return stdfs::is_directory(source, ec) ? source : source.parent_path();
}

// A CD to import from: a folder of its files (the CD, or a copy of it),
// or else the image of it in the folder (a cue sheet or an .iso) whose
// data track holds a CD marker.
class Disc {
public:
    bool open(const stdfs::path &source) {
        _folder = folderOf(source);
        if (fs::findFile(_folder, "GAMEDATA/RESOURCE/JK_.CD"))
            return true;
        for (const stdfs::path &file : discImagesIn(_folder)) {
            auto image = std::make_shared<IsoImage>();
            if (!image->openFile(file) && image->find("GAMEDATA/RESOURCE/JK_.CD")) {
                _image = std::move(image);
                return true;
            }
        }
        return false;
    }

    const stdfs::path &folder() const { return _folder; }

    // Its marker's bytes; empty without one.
    std::vector<uint8_t> mark() const {
        std::vector<uint8_t> out;
        if (_image) {
            if (const IsoFile *f = _image->find("GAMEDATA/RESOURCE/JK_.CD"))
                _image->readAll(*f, out, 16);
            return out;
        }
        if (const auto path = fs::findFile(_folder, "GAMEDATA/RESOURCE/JK_.CD")) {
            std::ifstream in(*path, std::ios::binary);
            out.resize(16);
            in.read(reinterpret_cast<char *>(out.data()), std::streamsize(out.size()));
            out.resize(size_t(in.gcount()));
        }
        return out;
    }

    FileLookup lookup() const {
        if (!_image)
            return folderLookup(_folder);
        return [image = _image](const std::string &name) -> std::optional<FoundFile> {
            const IsoFile *f = image->find(name);
            if (!f)
                return std::nullopt;
            return FoundFile{f->size, [image, file = *f]() -> std::optional<std::string> {
                                 IsoTrackReader reader;
                                 std::vector<uint8_t> bytes(size_t(std::min<uint64_t>(file.size, kDetectionHashBytes)));
                                 if (reader.open(image->track()) ||
                                     reader.read(file.sector, 0, bytes.data(), bytes.size()))
                                     return std::nullopt;
                                 Md5 md5;
                                 md5.update(bytes.data(), bytes.size());
                                 return md5.hexDigest();
                             }};
        };
    }

    // The file at `onCd` copied to `to`; none if it isn't there.
    std::optional<ImportPlan::File> file(const std::string &onCd, const std::string &to) const {
        if (_image) {
            const IsoFile *f = _image->find(onCd);
            if (!f)
                return std::nullopt;
            return fromImage(*f, to);
        }
        const auto path = fs::findFile(_folder, onCd);
        if (!path)
            return std::nullopt;
        return ImportPlan::File{*path, to, sizeOf(*path), std::nullopt, std::nullopt};
    }

    // The files directly in `onCd`, each copied into `to` under its name.
    std::vector<ImportPlan::File> filesIn(const std::string &onCd, const std::string &to) const {
        std::vector<ImportPlan::File> out;
        if (_image) {
            for (const IsoFile *f : _image->filesIn(onCd))
                out.push_back(fromImage(*f, to + f->path.substr(f->path.find_last_of('/') + 1)));
            return out;
        }
        if (const auto folder = fs::findDirectory(_folder, onCd))
            for (const stdfs::path &p : sith::filesIn(*folder))
                out.push_back({p, to + p.filename().string(), sizeOf(p), std::nullopt, std::nullopt});
        return out;
    }

private:
    ImportPlan::File fromImage(const IsoFile &f, const std::string &to) const {
        ImportPlan::File out{_image->track().file, to, f.size, std::nullopt, std::nullopt};
        out.disc = _image->track();
        out.discSector = f.sector;
        return out;
    }

    stdfs::path _folder;
    std::shared_ptr<const IsoImage> _image;
};

// A CD's music beside its files or image, as the game's CD audio plays it:
// a cue sheet with the images holding its audio tracks (a data track's
// alone is left behind, as the files are copied apart), or tracks ripped
// as trackNN.wav, .mp3, .ogg or .flac.
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

bool isMots(const ImportPlan &plan) {
    return plan.game.gameId == "mots";
}

// A CD's marker, cut scenes and music, kept under `cd<n>/` with the CD's
// own paths, where findFolders and the CD audio look for them. Mysteries
// of the Sith's CD 2 is Jedi Knight's, in the drive only for the game's
// one check of it: its marker is all it needs.
void addCdFiles(ImportPlan &plan, const Disc &cd, int number) {
    const std::string folder = "cd" + std::to_string(number) + "/";
    const std::string prefix = folder + "GAMEDATA/RESOURCE/";
    if (auto mark = cd.file("GAMEDATA/RESOURCE/JK_.CD", prefix + "JK_.CD"))
        plan.files.push_back(*mark);
    if (isMots(plan) && number == 2)
        return;
    for (ImportPlan::File &video : cd.filesIn("GAMEDATA/RESOURCE/VIDEO", prefix + "VIDEO/"))
        plan.files.push_back(std::move(video));
    addCdAudio(plan, cd.folder(), folder);
}

bool hasCd(const ImportPlan &plan, int number) {
    const std::string prefix = "cd" + std::to_string(number) + "/";
    return std::any_of(plan.files.begin(), plan.files.end(),
                       [&](const ImportPlan::File &f) { return f.to.rfind(prefix, 0) == 0; });
}

constexpr const char *kNoCd2 = "CD 2 has the cut scenes of levels 9 to 21: add it with \"Add other disc...\", or "
                               "keep a copy of it beside the game";
constexpr const char *kNoJkCd2 = "The game asks once for Jedi Knight's CD 2, to show you have Jedi Knight: add that "
                                 "CD (or a copy or image of it) with \"Add other disc...\"";

// The note asking for CD 2, while it isn't in the plan; the others stay.
void setNotes(ImportPlan &plan) {
    const char *note = isMots(plan) ? kNoJkCd2 : kNoCd2;
    plan.notes.erase(std::remove(plan.notes.begin(), plan.notes.end(), note), plan.notes.end());
    if (!hasCd(plan, 2))
        plan.notes.push_back(note);
}

} // namespace

std::vector<ImportPlan> planImport(const stdfs::path &source) {
    Disc cd;
    if (!cd.open(source))
        return {};
    const std::vector<DetectedGame> found = detectCd(cd.lookup());
    if (found.empty())
        return {};
    const GameInfo *game = findGame(found.front().gameId);
    if (!game || cdNumber(*game, cd.mark()) != 1)
        return {};

    ImportPlan plan;
    plan.game = found.front();
    std::string missing;
    const bool mots = isMots(plan);
    const InstalledFile *begin = mots ? std::begin(kMotsInstalled) : std::begin(kJkInstalled);
    const InstalledFile *end = mots ? std::end(kMotsInstalled) : std::end(kJkInstalled);
    for (const InstalledFile *f = begin; f != end; ++f) {
        if (auto file = cd.file(f->onCd, f->installed))
            plan.files.push_back(*file);
        else
            missing += (missing.empty() ? "" : ", ") + std::string(f->onCd);
    }
    for (ImportPlan::File &controls : cd.filesIn("GAMEDATA/CONTROLS", "Controls/"))
        plan.files.push_back(std::move(controls));
    addCdFiles(plan, cd, 1);
    if (!missing.empty())
        plan.notes.push_back("Not on this copy of the CD, so the game won't run: " + missing);
    setNotes(plan);
    plan.addPatchLabel.clear();
    plan.addFolderLabel = "Add other disc...";
    plan.addFolderTooltip = mots ? "Jedi Knight's CD 2, a copy of it or its image: the game asks for it once"
                                 : "Copy CD 2's cut scenes too, from the CD, a copy of it or its image";
    return {plan};
}

bool addToImport(ImportPlan &plan, const stdfs::path &folder, std::string &message) {
    const GameInfo *game = findGame(plan.game.gameId);
    Disc cd;
    const int number = game && cd.open(folder) ? cdNumber(*game, cd.mark()) : 0;
    if (number == 0) {
        message = isMots(plan) ? "neither Mysteries of the Sith's CD nor Jedi Knight's CD 2, nor a copy of one"
                               : "not one of Jedi Knight's CDs, nor a copy of one";
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
