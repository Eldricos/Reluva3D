// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/clue/game_data.h"

#include "common/fs.h"

#include <algorithm>

namespace reluva::clue {

namespace {

// The data's own folders, as Clue.exe names them.
const char *const kSubfolders[] = {"UI", "Resource", "Levels", "Sounds", "NSSounds", "Text", "Video"};

// The file a resource drive keeps each kind of resource in.
const char *kindExtension(char kind) {
    switch (kind) {
    case 'I':
        return ".res"; // images
    case 'A':
        return ".a"; // animations
    case 'O':
        return ".o"; // objects
    default:
        return nullptr;
    }
}

} // namespace

Error GameData::open(const std::filesystem::path &gameDir, Language language) {
    _language = language;
    _folders.clear();
    _files.clear();
    _drives.clear();
    _images.clear();
    _animations.clear();
    _objects.clear();
    _folders.push_back(gameDir);
    std::vector<std::filesystem::path> roots;
    if (auto classic = fs::findDirectory(gameDir, "Data/Classic")) {
        roots.push_back(*classic);
    } else {
        // The CD: the language's own files first, then the shared ones.
        for (const char *name :
             {language == Language::kUK ? "Data/UK" : "Data/US", "Data/ClueSpecific", "Data/LangIndepend"})
            if (auto dir = fs::findDirectory(gameDir, name))
                roots.push_back(*dir);
    }
    if (roots.empty())
        return Error::fail("No Data/Classic (an install) or Data/LangIndepend (the CD) in " + gameDir.string());
    for (const auto &root : roots) {
        _folders.push_back(root);
        for (const char *sub : kSubfolders)
            if (auto dir = fs::findDirectory(root, sub))
                _folders.push_back(*dir);
    }
    for (const auto &dir : _folders) {
        std::error_code ec;
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file(ec))
                _files.emplace(fs::toLower(it->path().filename().string()), it->path()); // first one wins
    }
    return Error::success();
}

const std::filesystem::path *GameData::find(std::string_view fileName) const {
    auto it = _files.find(fs::toLower(fileName));
    return it == _files.end() ? nullptr : &it->second;
}

std::vector<std::filesystem::path> GameData::withExtension(std::string_view ext) const {
    std::vector<std::filesystem::path> out;
    const std::string want = fs::toLower(ext);
    for (const auto &[name, path] : _files)
        if (name.size() > want.size() && name.compare(name.size() - want.size(), want.size(), want) == 0)
            out.push_back(path);
    return out;
}

const DataDrive *GameData::drive(std::string_view driveName, char kind) const {
    const char *ext = kindExtension(kind);
    if (!ext)
        return nullptr;
    const std::string file = fs::toLower(driveName) + ext;
    auto it = _drives.find(file);
    if (it == _drives.end()) {
        std::unique_ptr<DataDrive> d;
        if (const auto *path = find(file)) {
            d = std::make_unique<DataDrive>();
            if (Error err = d->open(*path)) {
                _lastError = err.message;
                d.reset();
            }
        } else {
            _lastError = "no " + file;
        }
        it = _drives.emplace(file, std::move(d)).first;
    }
    return it->second.get();
}

const Image *GameData::image(std::string_view fullName) const {
    const std::string key = fs::toLower(fullName);
    auto it = _images.find(key);
    if (it != _images.end())
        return it->second.get();
    std::unique_ptr<Image> img;
    ResourceName n;
    if (!splitResourceName(fullName, n)) {
        _lastError = "not a resource name: " + std::string(fullName);
    } else if (const DataDrive *d = drive(n.drive, 'I')) {
        if (const DataDrive::Entry *e = d->find(n.name)) {
            img = std::make_unique<Image>();
            if (Error err = decodeImage(d->read(*e), *img)) {
                _lastError = std::string(fullName) + ": " + err.message;
                img.reset();
            }
        } else {
            _lastError = "no " + n.name + " in " + n.drive + ".res";
        }
    }
    return _images.emplace(key, std::move(img)).first->second.get();
}

const Animation *GameData::animation(std::string_view fullName) const {
    const std::string key = fs::toLower(fullName);
    auto it = _animations.find(key);
    if (it != _animations.end())
        return it->second.get();
    std::unique_ptr<Animation> anim;
    ResourceName n;
    if (!splitResourceName(fullName, n)) {
        _lastError = "not a resource name: " + std::string(fullName);
    } else if (const DataDrive *d = drive(n.drive, 'A')) {
        if (const DataDrive::Entry *e = d->find(n.name)) {
            anim = std::make_unique<Animation>();
            if (Error err = parseAnimation(d->read(*e), *anim)) {
                _lastError = std::string(fullName) + ": " + err.message;
                anim.reset();
            }
        } else {
            _lastError = "no " + n.name + " in " + n.drive + ".a";
        }
    }
    return _animations.emplace(key, std::move(anim)).first->second.get();
}

const ObjectDef *GameData::object(std::string_view fullName) const {
    const std::string key = fs::toLower(fullName);
    auto it = _objects.find(key);
    if (it != _objects.end())
        return it->second.get();
    std::unique_ptr<ObjectDef> obj;
    ResourceName n;
    if (!splitResourceName(fullName, n)) {
        _lastError = "not a resource name: " + std::string(fullName);
    } else if (const DataDrive *d = drive(n.drive, 'O')) {
        if (const DataDrive::Entry *e = d->find(n.name)) {
            obj = std::make_unique<ObjectDef>();
            if (Error err = parseObject(d->read(*e), *obj)) {
                _lastError = std::string(fullName) + ": " + err.message;
                obj.reset();
            }
        } else {
            _lastError = "no " + n.name + " in " + n.drive + ".o";
        }
    }
    return _objects.emplace(key, std::move(obj)).first->second.get();
}

} // namespace reluva::clue
