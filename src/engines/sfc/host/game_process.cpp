// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sfc/host/game_process.h"

#include "common/fs.h"
#include "common/stream.h"

#include <algorithm>
#include <vector>

namespace reluva::sfc {

GameProcess::GameProcess(std::filesystem::path install, uint32_t memoryMb) : _dir(std::move(install)) {
    _cpu = std::make_unique<x86::Cpu>(memoryMb << 20);
    // StarFleet2.exe has no relocations: it has to have 0x400000.
    _process = std::make_unique<x86::Win32Process>(*_cpu, kProgram, true, true);
    _process->setProgram(kProgram, kInstallFolder);
    _process->timeStampFromClock();
    // Its threads wait on each other (events, mutexes, the multimedia
    // timer): each runs on a host thread of its own.
    _process->useHostThreads();
    // The install is the program's folder. What the program writes there
    // (its logs, its settings, saved games) goes to the save folder, which
    // is read first; the install isn't written to.
    _process->mapPath = [this](const std::string &windowsPath,
                               bool forWriting) -> std::optional<std::filesystem::path> {
        // The folder, named with or without its last backslash, and what's in it.
        const std::string folder = fs::toLower(kInstallFolder);
        const std::string path = fs::toLower(windowsPath);
        if (path.rfind(folder, 0) != 0 && path != folder.substr(0, folder.size() - 1))
            return std::nullopt;
        std::string rest = windowsPath.size() > folder.size() ? windowsPath.substr(folder.size()) : std::string();
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
        std::optional<std::filesystem::path> found;
        if (!_saveDir.empty())
            found = fs::findFile(_saveDir, rest);
        if (!found)
            found = fs::findFile(_dir, rest);
        if (!found)
            found = fs::findDirectory(_dir, rest);
        if (fileOpened)
            fileOpened(windowsPath, found.has_value());
        return found;
    };
    _directX = std::make_unique<x86::DirectX>(*_process);
    _directInput = std::make_unique<x86::DirectInput>(*_process);
    addSystem();
}

Error GameProcess::load() {
    const auto path = fs::findFile(_dir, kProgram);
    std::vector<uint8_t> file;
    if (!path || !readFile(*path, file))
        return Error::fail(std::string("No ") + kProgram + " in " + _dir.string());
    return _process->load(kProgram, file, _exe);
}

Error GameProcess::run(uint32_t &exitCode) {
    if (!_exe)
        return Error::fail(std::string(kProgram) + " isn't loaded");
    return _process->run(*_exe, exitCode);
}

} // namespace reluva::sfc
