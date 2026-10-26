// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/netimmerse/alphateam/game_process.h"

#include "common/fs.h"
#include "common/pe.h"
#include "common/x86/msvcp60.h"
#include "common/x86/msvcrt.h"

#include <algorithm>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>

namespace reluva::netimmerse::alphateam {

namespace {

constexpr const char* kProgram = "LEGOATeam.exe";

Error readFile(const std::filesystem::path& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return Error::fail("Cannot read " + path.string());
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return Error::success();
}

} // namespace

GameProcess::GameProcess(std::filesystem::path gameDir, uint32_t memoryMb) : dir_(std::move(gameDir)) {
    cpu_ = std::make_unique<x86::Cpu>(memoryMb << 20);
    process_ = std::make_unique<x86::Win32Process>(*cpu_);
    process_->setProgram(kProgram, kInstallFolder);
    // Its files: the install folder is the game folder; the CD's label is
    // the retail CD's, so its check passes. Writing isn't allowed yet.
    process_->mapPath = [this](const std::string& windowsPath, bool forWriting) -> std::optional<std::filesystem::path> {
        const std::string folder = fs::toLower(kInstallFolder);
        const std::string path = fs::toLower(windowsPath);
        if (forWriting || path.rfind(folder, 0) != 0)
            return std::nullopt;
        std::string rest = windowsPath.substr(folder.size());
        std::replace(rest.begin(), rest.end(), '\\', '/');
        while (!rest.empty() && rest.back() == '/')
            rest.pop_back();
        if (rest.empty())
            return dir_;
        if (auto file = fs::findFile(dir_, rest))
            return file;
        return fs::findDirectory(dir_, rest);
    };
    process_->cdLabel = "LEGOATEN";
    x86::addMsvcrt(*process_);
    directX_ = std::make_unique<x86::DirectX>(*process_);
    directInput_ = std::make_unique<x86::DirectInput>(*process_);
}

Error GameProcess::load() {
    // The program and every DLL it needs that's in the game folder, found
    // by following imports; the others (Windows', the C runtime's) are
    // stood in for.
    struct Pending {
        std::string name;
        std::vector<uint8_t> file;
        std::vector<std::string> needs; // lower-case names of game modules it imports from
    };
    std::map<std::string, Pending> found; // by lower-case name
    std::function<Error(const std::string&)> find = [&](const std::string& name) -> Error {
        const std::string key = fs::toLower(name);
        if (found.count(key))
            return Error::success();
        const auto path = fs::findFile(dir_, name);
        if (!path)
            return Error::success(); // not the game's
        Pending p;
        p.name = path->filename().string();
        if (Error e = readFile(*path, p.file))
            return e;
        PeImage pe;
        if (Error e = parsePe(p.file, pe))
            return Error::fail(p.name + ": " + e.message);
        found[key] = std::move(p);
        for (const PeImage::ImportedDll& dll : pe.imports) {
            if (fs::findFile(dir_, dll.name)) {
                found[key].needs.push_back(fs::toLower(dll.name));
                if (Error e = find(dll.name))
                    return e;
            }
        }
        return Error::success();
    };
    if (!fs::findFile(dir_, kProgram))
        return Error::fail(std::string("No ") + kProgram + " in " + dir_.string());
    if (Error e = find(kProgram))
        return e;

    // Our C++ library first (the game's DLLs import Visual C++ 6's), then
    // each module after those it imports from.
    const x86::Win32Process::Module* library = nullptr;
    if (Error e = x86::loadMsvcp60(*process_, library))
        return e;
    order_.push_back(library->name);
    modules_.push_back(library);
    std::set<std::string> done;
    std::function<Error(const std::string&, int)> place = [&](const std::string& key, int depth) -> Error {
        if (done.count(key))
            return Error::success();
        if (depth > 64)
            return Error::fail("The modules import from each other in a loop");
        Pending& p = found.at(key);
        for (const std::string& need : p.needs)
            if (Error e = place(need, depth + 1))
                return e;
        done.insert(key);
        const x86::Win32Process::Module* m = nullptr;
        if (Error e = process_->load(p.name, p.file, m))
            return e;
        order_.push_back(p.name);
        modules_.push_back(m);
        return Error::success();
    };
    return place(fs::toLower(kProgram), 0);
}

Error GameProcess::attach() {
    for (const x86::Win32Process::Module* m : modules_)
        if (fs::toLower(m->name) != fs::toLower(kProgram))
            if (Error e = process_->attach(*m))
                return e;
    return Error::success();
}

Error GameProcess::run(uint32_t& exitCode) {
    const x86::Win32Process::Module* exe = process_->module(kProgram);
    if (!exe)
        return Error::fail(std::string(kProgram) + " isn't loaded");
    return process_->start(*exe, exitCode);
}

} // namespace reluva::netimmerse::alphateam
