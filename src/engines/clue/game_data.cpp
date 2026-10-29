// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/game_data.h"

#include "common/fs.h"

#include <algorithm>

namespace reluva::clue {

namespace {

// The data's own folders, as Clue.exe names them.
const char* const kSubfolders[] = {"UI", "Resource", "Levels", "Sounds", "NSSounds", "Text", "Video"};

// The file a resource drive keeps each kind of resource in.
const char* kindExtension(char kind) {
    switch (kind) {
    case 'I': return ".res"; // images
    case 'A': return ".a";   // animations
    case 'O': return ".o";   // objects
    default: return nullptr;
    }
}

} // namespace

Error GameData::open(const std::filesystem::path& gameDir, Language language) {
    folders_.clear();
    files_.clear();
    drives_.clear();
    images_.clear();
    animations_.clear();
    folders_.push_back(gameDir);
    std::vector<std::filesystem::path> roots;
    if (auto classic = fs::findDirectory(gameDir, "Data/Classic")) {
        roots.push_back(*classic);
    } else {
        // The CD: the language's own files first, then the shared ones.
        for (const char* name : {language == Language::UK ? "Data/UK" : "Data/US", "Data/ClueSpecific",
                                 "Data/LangIndepend"})
            if (auto dir = fs::findDirectory(gameDir, name))
                roots.push_back(*dir);
    }
    if (roots.empty())
        return Error::fail("No Data/Classic (an install) or Data/LangIndepend (the CD) in " + gameDir.string());
    for (const auto& root : roots) {
        folders_.push_back(root);
        for (const char* sub : kSubfolders)
            if (auto dir = fs::findDirectory(root, sub))
                folders_.push_back(*dir);
    }
    for (const auto& dir : folders_) {
        std::error_code ec;
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file(ec))
                files_.emplace(fs::toLower(it->path().filename().string()), it->path()); // first one wins
    }
    return Error::success();
}

const std::filesystem::path* GameData::find(std::string_view fileName) const {
    auto it = files_.find(fs::toLower(fileName));
    return it == files_.end() ? nullptr : &it->second;
}

std::vector<std::filesystem::path> GameData::withExtension(std::string_view ext) const {
    std::vector<std::filesystem::path> out;
    const std::string want = fs::toLower(ext);
    for (const auto& [name, path] : files_)
        if (name.size() > want.size() && name.compare(name.size() - want.size(), want.size(), want) == 0)
            out.push_back(path);
    return out;
}

const DataDrive* GameData::drive(std::string_view driveName, char kind) const {
    const char* ext = kindExtension(kind);
    if (!ext)
        return nullptr;
    const std::string file = fs::toLower(driveName) + ext;
    auto it = drives_.find(file);
    if (it == drives_.end()) {
        std::unique_ptr<DataDrive> d;
        if (const auto* path = find(file)) {
            d = std::make_unique<DataDrive>();
            if (Error err = d->open(*path)) {
                lastError_ = err.message;
                d.reset();
            }
        } else {
            lastError_ = "no " + file;
        }
        it = drives_.emplace(file, std::move(d)).first;
    }
    return it->second.get();
}

const Image* GameData::image(std::string_view fullName) const {
    const std::string key = fs::toLower(fullName);
    auto it = images_.find(key);
    if (it != images_.end())
        return it->second.get();
    std::unique_ptr<Image> img;
    ResourceName n;
    if (!splitResourceName(fullName, n)) {
        lastError_ = "not a resource name: " + std::string(fullName);
    } else if (const DataDrive* d = drive(n.drive, 'I')) {
        if (const DataDrive::Entry* e = d->find(n.name)) {
            img = std::make_unique<Image>();
            if (Error err = decodeImage(d->read(*e), *img)) {
                lastError_ = std::string(fullName) + ": " + err.message;
                img.reset();
            }
        } else {
            lastError_ = "no " + n.name + " in " + n.drive + ".res";
        }
    }
    return images_.emplace(key, std::move(img)).first->second.get();
}

const Animation* GameData::animation(std::string_view fullName) const {
    const std::string key = fs::toLower(fullName);
    auto it = animations_.find(key);
    if (it != animations_.end())
        return it->second.get();
    std::unique_ptr<Animation> anim;
    ResourceName n;
    if (!splitResourceName(fullName, n)) {
        lastError_ = "not a resource name: " + std::string(fullName);
    } else if (const DataDrive* d = drive(n.drive, 'A')) {
        if (const DataDrive::Entry* e = d->find(n.name)) {
            anim = std::make_unique<Animation>();
            if (Error err = parseAnimation(d->read(*e), *anim)) {
                lastError_ = std::string(fullName) + ": " + err.message;
                anim.reset();
            }
        } else {
            lastError_ = "no " + n.name + " in " + n.drive + ".a";
        }
    }
    return animations_.emplace(key, std::move(anim)).first->second.get();
}

} // namespace reluva::clue
