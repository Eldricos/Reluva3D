// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sfc/host/game_process.h"

#include "common/fs.h"
#include "common/stream.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
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
    // What it writes to the registry is kept in the save folder, as
    // Reluva has no registry of its own on any system.
    _process->registryWritten = [this](const std::string &key, const std::string &name) {
        keepRegistryValue(key, name);
        if (registryWritten)
            registryWritten(key, name);
    };
    _directX = std::make_unique<x86::DirectX>(*_process);
    _directInput = std::make_unique<x86::DirectInput>(*_process);
    _miles = std::make_unique<x86::Miles>(*_process);
    _directPlay = std::make_unique<x86::DirectPlay>(*_process);
    addSystem();
}

void GameProcess::setSaveFolder(std::filesystem::path folder) {
    _saveDir = std::move(folder);
    loadRegistry();
}

namespace {
std::string registryId(const std::string &key, const std::string &name) {
    return fs::toLower(key) + '\n' + fs::toLower(name);
}
} // namespace

// registry.txt: a line a value, its key, name, type and bytes in hex,
// separated by tabs; lines starting with # are comments.
void GameProcess::loadRegistry() {
    _registry.clear();
    std::vector<uint8_t> file;
    if (_saveDir.empty() || !readFile(_saveDir / kRegistryFile, file))
        return;
    std::istringstream in(std::string(file.begin(), file.end()));
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        std::vector<std::string> fields;
        for (size_t at = 0;;) {
            const size_t tab = line.find('\t', at);
            fields.push_back(line.substr(at, tab == std::string::npos ? std::string::npos : tab - at));
            if (tab == std::string::npos)
                break;
            at = tab + 1;
        }
        if (fields.size() != 4 || fields[3].size() % 2)
            continue;
        RegistryValue v{fields[0], fields[1], uint32_t(std::strtoul(fields[2].c_str(), nullptr, 10)), {}};
        for (size_t i = 0; i < fields[3].size(); i += 2)
            v.data.push_back(uint8_t(std::strtoul(fields[3].substr(i, 2).c_str(), nullptr, 16)));
        _process->setRegistryValue(v.key, v.name, v.type, v.data);
        _registry[registryId(v.key, v.name)] = std::move(v);
    }
}

void GameProcess::keepRegistryValue(const std::string &key, const std::string &name) {
    uint32_t type = 0;
    const auto data = _process->registryValue(key, name, &type);
    if (!data || _saveDir.empty())
        return;
    _registry[registryId(key, name)] = RegistryValue{key, name, type, *data};
    std::string text = "# What StarFleet2.exe wrote to the registry: key, name, type, bytes.\n";
    for (const auto &[id, v] : _registry) {
        text += v.key + '\t' + v.name + '\t' + std::to_string(v.type) + '\t';
        for (const uint8_t b : v.data) {
            char hex[3];
            std::snprintf(hex, sizeof hex, "%02x", b);
            text += hex;
        }
        text += '\n';
    }
    std::error_code ec;
    std::filesystem::create_directories(_saveDir, ec);
    writeFile(_saveDir / kRegistryFile, std::vector<uint8_t>(text.begin(), text.end()));
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
