// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/netimmerse/alphateam/game_process.h"

#include "common/fs.h"
#include "common/pe.h"
#include "emulation/x86/msvcp60.h"
#include "emulation/x86/msvcrt.h"

#include <algorithm>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>

namespace reluva::netimmerse::alphateam {

namespace {

constexpr const char *kProgram = "LEGOATeam.exe";

Error readFile(const std::filesystem::path &path, std::vector<uint8_t> &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return Error::fail("Cannot read " + path.string());
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return Error::success();
}

} // namespace

GameProcess::GameProcess(std::filesystem::path gameDir, uint32_t memoryMb) : _dir(std::move(gameDir)) {
    _cpu = std::make_unique<x86::Cpu>(memoryMb << 20);
    _process = std::make_unique<x86::Win32Process>(*_cpu, kProgram, true, true);
    _process->setProgram(kProgram, kInstallFolder);
    _process->timeStampFromClock();
    // Its files: the install folder is the game folder; the CD's label is
    // the retail CD's, so its check passes. What the program writes there
    // (Game.txt: its settings and progress) goes to the save folder, and
    // is read back from there first. Nothing else may be written.
    _process->mapPath = [this](const std::string &windowsPath,
                               bool forWriting) -> std::optional<std::filesystem::path> {
        const std::string folder = fs::toLower(kInstallFolder);
        const std::string path = fs::toLower(windowsPath);
        if (path.rfind(folder, 0) != 0)
            return std::nullopt;
        std::string rest = windowsPath.substr(folder.size());
        std::replace(rest.begin(), rest.end(), '\\', '/');
        while (!rest.empty() && rest.back() == '/')
            rest.pop_back();
        if (forWriting) {
            if (_saveDir.empty() || rest.empty())
                return std::nullopt;
            if (auto file = fs::findFile(_saveDir, rest))
                return file;
            std::error_code ec;
            const std::filesystem::path to = _saveDir / rest;
            std::filesystem::create_directories(to.parent_path(), ec);
            if (ec)
                return std::nullopt;
            return to;
        }
        if (rest.empty())
            return _dir;
        if (!_saveDir.empty())
            if (auto file = fs::findFile(_saveDir, rest))
                return file;
        if (auto file = fs::findFile(_dir, rest))
            return file;
        return fs::findDirectory(_dir, rest);
    };
    _process->cdLabel = "LEGOATEN";
    x86::addMsvcrt(*_process);
    _directX = std::make_unique<x86::DirectX>(*_process);
    _directInput = std::make_unique<x86::DirectInput>(*_process);
    _a3d = std::make_unique<x86::A3d>(*_process);
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
    std::function<Error(const std::string &)> find = [&](const std::string &name) -> Error {
        const std::string key = fs::toLower(name);
        if (found.count(key))
            return Error::success();
        const auto path = fs::findFile(_dir, name);
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
        for (const PeImage::ImportedDll &dll : pe.imports) {
            if (fs::findFile(_dir, dll.name)) {
                found[key].needs.push_back(fs::toLower(dll.name));
                if (Error e = find(dll.name))
                    return e;
            }
        }
        return Error::success();
    };
    if (!fs::findFile(_dir, kProgram))
        return Error::fail(std::string("No ") + kProgram + " in " + _dir.string());
    if (Error e = find(kProgram))
        return e;

    // Our C++ library first (the game's DLLs import Visual C++ 6's), then
    // each module after those it imports from.
    const x86::Win32Process::Module *library = nullptr;
    if (Error e = x86::loadMsvcp60(*_process, library))
        return e;
    _order.push_back(library->name);
    _modules.push_back(library);
    std::set<std::string> done;
    std::function<Error(const std::string &, int)> place = [&](const std::string &key, int depth) -> Error {
        if (done.count(key))
            return Error::success();
        if (depth > 64)
            return Error::fail("The modules import from each other in a loop");
        Pending &p = found.at(key);
        for (const std::string &need : p.needs)
            if (Error e = place(need, depth + 1))
                return e;
        done.insert(key);
        const x86::Win32Process::Module *m = nullptr;
        if (Error e = _process->load(p.name, p.file, m))
            return e;
        _order.push_back(p.name);
        _modules.push_back(m);
        return Error::success();
    };
    if (Error e = place(fs::toLower(kProgram), 0))
        return e;
    // DDCycleCounter (DDPackedDirFileLib) times the program's work with
    // RDTSC around a great many of its functions, for a profile it shows
    // only when asked (the interface behaviour's, PE0300). Interpreted,
    // its Start and Stop were over a quarter of the instructions run while
    // playing. They're made to return at once (both cdecl, void); the
    // profile then reads zero.
    if (const auto *lib = _process->module("DDPackedDirFileLib.dll"))
        for (const char *name : {"?Start@DDCycleCounter@@SAXH@Z", "?Stop@DDCycleCounter@@SAXXZ"})
            if (const uint32_t at = lib->exportAddress(name))
                _cpu->write8(at, 0xc3); // RET
    return Error::success();
}

Error GameProcess::attach() {
    for (const x86::Win32Process::Module *m : _modules)
        if (fs::toLower(m->name) != fs::toLower(kProgram))
            if (Error e = _process->attach(*m))
                return e;
    return Error::success();
}

Error GameProcess::run(uint32_t &exitCode) {
    const x86::Win32Process::Module *exe = _process->module(kProgram);
    if (!exe)
        return Error::fail(std::string(kProgram) + " isn't loaded");
    return _process->start(*exe, exitCode);
}

} // namespace reluva::netimmerse::alphateam
