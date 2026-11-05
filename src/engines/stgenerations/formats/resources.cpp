// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/formats/resources.h"

#include <algorithm>
#include <map>
#include <optional>

#include "common/archive.h"
#include "common/log.h"
#include "engines/stgenerations/audio.h"
#include "engines/stgenerations/debug.h"

namespace reluva::stgenerations {

namespace {

const DataFileLocation kDataFiles[] = {
    { "COMMON.DAT", true, true, true },
    { "OPTIONS.DAT", true, true, true },
    { "AMARGOS1.DAT", true, true, false },
    { "AMARGOS2.DAT", true, false, true },
    { "ANTILIOS.DAT", true, true, false },
    { "ARVADA1.DAT", true, false, true },
    { "BERSUS.DAT", true, true, false },
    { "ENTERP.DAT", true, false, true },
    { "EPSION.DAT", true, true, false },
    { "GALORN1.DAT", true, false, true },
    { "GALORN2.DAT", true, false, true },
    { "HALEE1.DAT", true, false, true },
    { "QURASH.DAT", true, true, false },
    { "VERID1.DAT", true, false, true },
    { "COMBAT.DAT", false, true, true },
    { "STELLAR.DAT", false, true, true },
    { "MUSIC.1", false, true, false },
    { "MUSIC.2", false, false, true },
    { "MOVIE/MOV_01.AVI", false, true, false },
    { "MOVIE/MOV_02.AVI", false, true, true },
    { "MOVIE/MOV_03.AVI", false, true, false },
    { "MOVIE/MOV_05.AVI", false, true, false },
    { "MOVIE/MOV_06.AVI", false, false, true },
    { "MOVIE/MOV_07.AVI", false, true, false },
    { "MOVIE/MOV_08.AVI", false, true, false },
    { "MOVIE/MOV_09.AVI", false, false, true },
    { "MOVIE/MOV_10.AVI", false, false, true },
    { "MOVIE/MOV_13.AVI", false, false, true },
    { "MOVIE/MOV_14.AVI", false, false, true },
    { "MOVIE/MOV_15.AVI", false, false, true },
    { "MOVIE/MOV_16.AVI", false, false, true },
    { "MOVIE/MOV_18.AVI", false, true, false },
    { nullptr, false, false, false }
};

// The folder holding the game's data inside an install or a disc, if it
// has one.
std::filesystem::path findDataDir(const std::filesystem::path& root, const char* subdir) {
    if (auto dir = fs::findDirectory(root, subdir))
        return *dir;
    return root;
}

// An archive opened once, searched by name too.
class SharedArchive : public Archive {
public:
    explicit SharedArchive(std::shared_ptr<ZipArchive> zip) : zip_(std::move(zip)) {}
    std::vector<std::string> list() const override { return zip_->list(); }
    bool has(std::string_view name) const override { return zip_->has(name); }
    Error read(std::string_view name, std::vector<uint8_t>& out) const override { return zip_->read(name, out); }
    std::string describe() const override { return zip_->describe(); }

private:
    std::shared_ptr<ZipArchive> zip_;
};

} // namespace

const DataFileLocation* dataFiles() { return kDataFiles; }

void ResourceManager::addSearchPaths(const std::filesystem::path& gameDir, const std::filesystem::path& cd1Path,
                                     const std::filesystem::path& cd2Path) {
    // Each folder is searched by plain name ("COMMON.DAT",
    // "MOVIE/MOV_01.AVI") wherever the file is. Installed files win over
    // the discs.
    _dataDirs.clear();
    _files.clear();
    _dataDirs.push_back(findDataDir(gameDir, "DATA"));
    auto addDir = [this](const std::filesystem::path& dir, int priority, int depth) {
        auto archive = std::make_unique<DirectoryArchive>();
        if (!archive->open(dir, depth))
            _files.add(std::move(archive), priority);
    };
    addDir(gameDir, 0, 0);
    addDir(_dataDirs.back(), 10, 0);
    // The discs: in the game's folder as an import keeps them, or where
    // the settings say.
    std::filesystem::path cds[2] = {cd1Path, cd2Path};
    for (int i = 0; i < 2; i++) {
        if (cds[i].empty()) {
            if (auto dir = fs::findDirectory(gameDir, i == 0 ? "disc1" : "disc2"))
                cds[i] = *dir;
            else
                continue;
        }
        std::error_code ec;
        if (!std::filesystem::is_directory(cds[i], ec)) {
            log::warning("CD %d's folder '%s' is not a folder", i + 1, cds[i].string().c_str());
            continue;
        }
        _dataDirs.push_back(findDataDir(cds[i], "GENS"));
        addDir(_dataDirs.back(), 5 - i, 1);
    }
}

std::vector<std::string> ResourceManager::missingDataFiles() const {
    std::vector<std::string> missing;
    for (const DataFileLocation* f = kDataFiles; f->name; f++)
        if (!_files.has(f->name))
            missing.push_back(f->name);
    return missing;
}

bool ResourceManager::checkDataFiles() const {
    std::vector<std::string> missing[2];
    std::vector<std::string> missingInstalled;
    for (const DataFileLocation* f = kDataFiles; f->name; f++) {
        if (_files.has(f->name))
            continue;
        if (f->installed)
            missingInstalled.push_back(f->name);
        else if (f->onDisc1)
            missing[0].push_back(f->name);
        else
            missing[1].push_back(f->name);
    }

    if (!missingInstalled.empty())
        log::warning("%d installed data files not found (e.g. DATA/%s); reinstall the game or copy them from a CD's "
                     "GENS folder",
                     (int)missingInstalled.size(), missingInstalled[0].c_str());
    for (int i = 0; i < 2; i++) {
        if (missing[i].empty())
            continue;
        log::warning("%d files from CD %d not found (e.g. %s); import the game with both discs, or set cd%d to the "
                     "disc's folder",
                     (int)missing[i].size(), i + 1, missing[i][0].c_str(), i + 1);
    }
    return missingInstalled.empty() && missing[0].empty() && missing[1].empty();
}

void ResourceManager::openArchives() {
    _archives.clear();
    for (const DataFileLocation* f = kDataFiles; f->name; f++) {
        std::string name = f->name;
        if (!text::hasSuffixIgnoreCase(name, ".dat") && !text::matchGlob(name, "MUSIC.?"))
            continue;

        // Installed copies first, then the discs, as in addSearchPaths().
        std::optional<std::filesystem::path> file;
        for (const std::filesystem::path& dir : _dataDirs)
            if ((file = fs::findFile(dir, name)))
                break;
        if (!file)
            continue; // checkDataFiles() has already reported it
        auto zip = std::make_shared<ZipArchive>();
        if (Error err = zip->open(*file)) {
            log::warning("%s is not a readable ZIP archive: %s", name.c_str(), err.message.c_str());
            continue;
        }
        debugC(1, "resources", "%s: %d members", name.c_str(), (int)zip->entries().size());
        _archives[text::toUpper(name)] = zip;
    }

    // The shared resources are found by name, after the loose files.
    int priority = -1;
    for (const char* name : {"COMMON.DAT", "OPTIONS.DAT"}) {
        if (std::shared_ptr<ZipArchive> zip = getArchive(name))
            _files.add(std::make_unique<SharedArchive>(zip), priority--);
    }
    debugC(1, "resources", "Opened %d data archives", (int)_archives.size());
}

std::shared_ptr<ZipArchive> ResourceManager::getArchive(const std::string& name) const {
    auto it = _archives.find(text::toUpper(name));
    return it == _archives.end() ? nullptr : it->second;
}

void ResourceManager::scan() {
    _images.clear();
    _sounds.clear();

    auto add = [this](const std::string& archive, const std::string& name) {
        if (text::hasSuffixIgnoreCase(name, ".bmp") || text::hasSuffixIgnoreCase(name, ".ovr") ||
            text::hasSuffixIgnoreCase(name, ".gs2") || text::hasSuffixIgnoreCase(name, ".lv2"))
            _images.push_back({archive, name});
        else if (text::hasSuffixIgnoreCase(name, ".wav"))
            _sounds.push_back({archive, name});
    };

    // Archives in name order, members in archive order.
    for (const auto& entry : _archives)
        for (const ZipArchive::Entry& member : entry.second->entries())
            if (!member.name.empty() && member.name.back() != '/')
                add(entry.first, member.name);

    // Loose files in the data folders, if any, in each folder's order.
    for (const std::filesystem::path& dir : _dataDirs) {
        DirectoryArchive files;
        if (files.open(dir, 1))
            continue;
        std::vector<std::string> names = files.list();
        std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
            return normalizeArchiveName(a) < normalizeArchiveName(b);
        });
        for (const std::string& name : names)
            add("", name);
    }

    debugC(1, "resources", "Found %d images and %d sounds", (int)_images.size(), (int)_sounds.size());
}

void ResourceManager::logInventory() const {
    for (const auto& entry : _archives) {
        std::map<std::string, int> byExtension;
        for (const ZipArchive::Entry& member : entry.second->entries()) {
            size_t dot = member.name.rfind('.');
            byExtension[dot == std::string::npos ? "?" : text::toUpper(member.name.substr(dot + 1))]++;
        }
        std::string summary;
        for (const auto& ext : byExtension)
            summary += text::format(" %d %s,", ext.second, ext.first.c_str());
        if (!summary.empty())
            summary.pop_back();
        log::debug(1, "  %-12s%s", entry.first.c_str(), summary.c_str());
    }
}

bool ResourceManager::hasResource(const ResourceRef& ref) const {
    if (ref.archive.empty())
        return _files.has(ref.name);
    std::shared_ptr<ZipArchive> zip = getArchive(ref.archive);
    return zip && zip->has(ref.name);
}

std::unique_ptr<SeekableReadStream> ResourceManager::open(const ResourceRef& ref) const {
    std::vector<uint8_t> data;
    if (ref.archive.empty()) {
        if (_files.read(ref.name, data))
            return nullptr;
    } else {
        std::shared_ptr<ZipArchive> zip = getArchive(ref.archive);
        if (!zip || !zip->has(ref.name) || zip->read(ref.name, data))
            return nullptr;
    }
    return std::make_unique<MemoryReadStream>(std::move(data));
}

bool ResourceManager::loadImage(const ResourceRef& ref, BitmapDecoder& decoder) const {
    std::unique_ptr<SeekableReadStream> stream = open(ref);
    if (!stream) {
        log::warning("Could not open image %s", ref.toString().c_str());
        return false;
    }
    if (!decoder.loadStream(*stream)) {
        log::warning("Could not decode image %s", ref.toString().c_str());
        return false;
    }
    return true;
}

bool ResourceManager::loadArchivePalette(const std::string& archive, Palette& palette) const {
    for (const ResourceRef& ref : _images) {
        if (ref.archive != archive || !text::hasSuffixIgnoreCase(ref.name, ".bmp"))
            continue;
        BitmapDecoder decoder;
        if (loadImage(ref, decoder) && decoder.hasPalette()) {
            palette = decoder.getPalette();
            return true;
        }
    }
    return false;
}

std::unique_ptr<SoundStream> ResourceManager::loadSound(const ResourceRef& ref) const {
    std::unique_ptr<SeekableReadStream> stream = open(ref);
    if (!stream) {
        log::warning("Could not open sound %s", ref.toString().c_str());
        return nullptr;
    }
    return makeWavStream(*stream);
}

std::string ResourceManager::loadSubtitle(const ResourceRef& soundRef) const {
    size_t dot = soundRef.name.rfind('.');
    if (dot == std::string::npos)
        return std::string();
    // Archive lookups ignore case, so ".TXT" matches either spelling.
    std::string base = soundRef.name.substr(0, dot);
    ResourceRef textRef = {soundRef.archive, base + ".TXT"};
    std::unique_ptr<SeekableReadStream> stream = open(textRef);
    if (!stream)
        return std::string();

    std::string text;
    while (!stream->eos()) {
        std::string line = text::trim(stream->readLine());
        if (!line.empty())
            text += (text.empty() ? "" : " ") + line;
    }
    return text;
}

} // namespace reluva::stgenerations
