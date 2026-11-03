// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/import.h"
#include "engines/boggle/classic/options.h"
#include "engines/boggle/frontend/media.h"
#include "common/fs.h"
#include <map>

namespace reluva::boggle {
namespace {
// Resolve only the named components. Reject case collisions and links instead
// of choosing an arbitrary copy or copying files outside the selected tree.
Error locate(const std::filesystem::path& root, const std::string& relative,
             std::filesystem::path& out) {
    out = root;
    for (const auto& component : std::filesystem::path(relative)) {
        const auto wanted = fs::toLower(component.string());
        std::filesystem::path found;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(out,ec),end; !ec && it!=end; it.increment(ec)) {
            if (fs::toLower(it->path().filename().string()) != wanted) continue;
            if (!found.empty()) return Error::fail("Ambiguous import path: " + relative);
            found = it->path();
        }
        if (ec) return Error::fail("Cannot list import folder: " + out.string());
        if (found.empty()) return Error::fail("Missing Classic import file: " + relative);
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(found,ec)) || ec)
            return Error::fail("Import path must not be a symbolic link: " + relative);
        out = std::move(found);
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(out,ec) || ec)
        return Error::fail("Import path is not a regular file: " + relative);
    return Error::success();
}
}
Error classicImportPlan(const std::filesystem::path& root, const DetectedGame& game,
                        const std::vector<FileSignature>& identity, ImportPlan& out) {
    out = {};
    std::map<std::string,std::string> paths;
    auto add = [&](std::string path) { const auto key = fs::toLower(path); paths.emplace(key,std::move(path)); };
    for (const auto& file : identity) add(file.name);
    for (const auto* path : {"Dicts/English.dic", "Dicts/EngOrg.dst", "Dicts/EngMas.dst",
         "Language/English.dll", "Palettes/ClsO1Pal.pal", "Palettes/ClsM1Pal.pal",
         "Textures/Letters.bmp", "Textures/LettersQu.bmp", "Anims/ClsO1All.avi", "Anims/ClsM1All.avi"}) add(path);
    for (const auto* name : {"Shake","ClkDown","ClkUp","Writing","Seven","Eight","Tick","GameOver"})
        add(std::string("Waves/")+name+".wav");
    for (const auto* name : classicMusicFiles) add(std::string("Waves/")+name+".wav");
    if (fs::findFile(root,"Bitmaps/Select1.bmp")) add("Bitmaps/Select1.bmp");
    if (fs::findFile(root,"Anims/Hasbro.avi")) add("Anims/Hasbro.avi");
    for (const auto* name : menuMovieNames) {
        const auto path=std::string("Anims/")+name+".avi";
        if(fs::findFile(root,path))add(path);
    }
    ImportPlan plan; plan.game = game;
    for (const auto& [key,path] : paths) {
        std::filesystem::path from;
        if (auto err = locate(root,path,from)) return err;
        std::error_code ec; const auto size = std::filesystem::file_size(from,ec);
        if (ec || !size) return Error::fail("Cannot read nonempty import file: " + path);
        plan.files.push_back({from,path,uint64_t(size),std::nullopt});
    }
    plan.notes = {
        "U.S. English Classic Original and Master, with available intro/menu movies, round clips, effects and WAV music.",
        "Boggle.exe is retained only for release identification; no original code is executed.",
        "Copies selected resources only. Installers, bundled system libraries and personal dictionaries are excluded.",
        "Other game modes, languages and CD-device music are not supported by this import."
    };
    out = std::move(plan); return Error::success();
}
} // namespace reluva::boggle
