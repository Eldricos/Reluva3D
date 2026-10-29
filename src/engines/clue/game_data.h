// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/clue/level.h"
#include "engines/clue/resources.h"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::clue {

// Which of the CD's languages to use. An install has only the one it was
// installed with, in Data/Classic.
enum class Language { US, UK };

// The game's files, wherever they are: an install (Data/Classic/...) or the
// CD (Data/LangIndepend, Data/ClueSpecific and Data/US or Data/UK). Files in
// the game folder itself come first, as the original's dialogInfo.res does
// and as the 2.2 patch's files do when copied there.
class GameData {
public:
    Error open(const std::filesystem::path& gameDir, Language language);

    // Every file, by lower-case name, and the folders searched in order.
    const std::map<std::string, std::filesystem::path>& files() const { return files_; }
    const std::vector<std::filesystem::path>& folders() const { return folders_; }
    const std::filesystem::path* find(std::string_view fileName) const; // ignoring case
    // Files with an extension (".res", ".wav", ...), in name order.
    std::vector<std::filesystem::path> withExtension(std::string_view ext) const;

    // The resource drive named (c_scarlet), opening its .res for images, .a
    // for animations or .o for objects; nullptr if there's none or it's damaged.
    const DataDrive* drive(std::string_view driveName, char kind) const;
    // An image or animation by its full name ("c_scarlet:Imsidlee_01"),
    // decoded once and kept; nullptr if missing or damaged.
    const Image* image(std::string_view fullName) const;
    const Animation* animation(std::string_view fullName) const;
    const ObjectDef* object(std::string_view fullName) const; // "ballroom:Oblcouch_ad"
    // Why the last of those failed.
    const std::string& lastError() const { return lastError_; }

private:
    std::vector<std::filesystem::path> folders_;
    std::map<std::string, std::filesystem::path> files_;
    mutable std::map<std::string, std::unique_ptr<DataDrive>> drives_; // by file name; null if it failed
    mutable std::map<std::string, std::unique_ptr<Image>> images_;
    mutable std::map<std::string, std::unique_ptr<Animation>> animations_;
    mutable std::map<std::string, std::unique_ptr<ObjectDef>> objects_;
    mutable std::string lastError_;
};

} // namespace reluva::clue
