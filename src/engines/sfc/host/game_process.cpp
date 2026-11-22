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
    // The install is the program's folder; it may read anything in it and
    // write nothing yet.
    _process->mapPath = [this](const std::string &windowsPath,
                               bool forWriting) -> std::optional<std::filesystem::path> {
        const std::string folder = fs::toLower(kInstallFolder);
        if (forWriting || fs::toLower(windowsPath).rfind(folder, 0) != 0)
            return std::nullopt;
        std::string rest = windowsPath.substr(folder.size());
        std::replace(rest.begin(), rest.end(), '\\', '/');
        while (!rest.empty() && rest.back() == '/')
            rest.pop_back();
        if (rest.empty())
            return _dir;
        auto found = fs::findFile(_dir, rest);
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
