// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/legoisland/host/game_process.h"

#include "engines/legoisland/host/turning.h"
#include "engines/legoisland/play_settings.h"

#include "common/fs.h"
#include "common/stream.h"

#include <algorithm>
#include <utility>

namespace reluva::legoisland {

namespace {

constexpr const char *kProgram = "ISLE.EXE";
constexpr const char *kLibrary = "LEGO1.DLL";
// Where the installer records the game's folders (its log).
constexpr const char *kRegistryKey = "HKEY_LOCAL_MACHINE\\SOFTWARE\\Mindscape\\LEGO Island";

// What the installer and the setup (CONFIG.EXE) write there, as seen in a
// registry they wrote (systems/running.md): the install folder by its short
// name, the saves (History.gsi, Players.gsi) kept in it, the CD's drive, and
// the setup's defaults; but its 3D device is the hardware one, whose
// textures are smoothed, unless the settings say (kSoftwareDevice).
constexpr std::pair<const char *, const char *> kSetupValues[] = {
    {"cdpath", "D:"},
    {"diskpath", "C:\\PROGRA~1\\LEGOIS~1"},
    {"savepath", "C:\\PROGRA~1\\LEGOIS~1"},
    {"3D Device ID", kHardwareDevice.id},
    {"3D Device Name", kHardwareDevice.name},
    {"Display Bit Depth", "16"},
    {"Flip Surfaces", "NO"},
    {"Full Screen", "YES"},
    {"Back Buffers in Video RAM", "NO"},
    {"Wide View Angle", "YES"},
    {"3DSound", "YES"},
    {"Draw Cursor", "NO"},
    {"Island Quality", "2"},
    {"Island Texture", "1"},
    {"UseJoystick", "NO"},
    {"Music", "YES"},
    {"JoystickIndex", "-1"},
};

// `path` with `prefix` taken off, '/' between folders, if it starts with it.
std::optional<std::string> under(const std::string &path, const std::string &prefix) {
    if (fs::toLower(path).rfind(fs::toLower(prefix), 0) != 0)
        return std::nullopt;
    std::string rest = path.substr(prefix.size());
    std::replace(rest.begin(), rest.end(), '\\', '/');
    while (!rest.empty() && rest.front() == '/')
        rest.erase(rest.begin());
    while (!rest.empty() && rest.back() == '/')
        rest.pop_back();
    return rest;
}

} // namespace

GameProcess::GameProcess(std::filesystem::path gameDir, uint32_t memoryMb) : _dir(std::move(gameDir)) {
    _cpu = std::make_unique<x86::Cpu>(memoryMb << 20);
    _process = std::make_unique<x86::Win32Process>(*_cpu, kProgram, true, true);
    // Its streamer's threads wait on each other (mutexes, semaphores), so
    // each runs on a host thread of its own.
    _process->useHostThreads();
    _process->setProgram(kProgram, kInstallFolder);
    _process->timeStampFromClock();
    _process->mapPath = [this](const std::string &path, bool forWriting) { return mapPath(path, forWriting); };
    // The engine's settings change some (setSetting).
    for (const auto &[name, value] : kSetupValues)
        _process->setRegistryString(kRegistryKey, name, value);
    _directX = std::make_unique<x86::DirectX>(*_process);
    _directSound = std::make_unique<x86::DirectSound>(*_process);
    _retainedMode = std::make_unique<x86::Direct3DRM>(*_process, *_directX);
    _directInput = std::make_unique<x86::DirectInput>(*_process);
}

std::optional<std::filesystem::path> GameProcess::mapPath(const std::string &path, bool forWriting) const {
    // The install folder, by either name, and the CD's drive are all the
    // game folder: an import puts the CD's LEGO\SCRIPTS there.
    std::optional<std::string> rest = under(path, kInstallFolder);
    if (!rest)
        rest = under(path, kInstallFolderShort);
    if (!rest && !forWriting)
        rest = under(path, _process->cdDrive + ":");
    if (!rest)
        return std::nullopt;
    if (forWriting) {
        if (_saveDir.empty() || rest->empty())
            return std::nullopt;
        if (auto file = fs::findFile(_saveDir, *rest))
            return file;
        std::error_code ec;
        const std::filesystem::path to = _saveDir / *rest;
        std::filesystem::create_directories(to.parent_path(), ec);
        return ec ? std::nullopt : std::optional<std::filesystem::path>(to);
    }
    if (rest->empty())
        return _dir;
    if (!_saveDir.empty())
        if (auto file = fs::findFile(_saveDir, *rest))
            return file;
    if (auto file = fs::findFile(_dir, *rest))
        return file;
    return fs::findDirectory(_dir, *rest);
}

void GameProcess::setSetting(const std::string &name, const std::string &value) {
    _process->setRegistryString(kRegistryKey, name, value);
}

Error GameProcess::load() {
    // The DLL first: the program imports from it.
    for (const char *name : {kLibrary, kProgram}) {
        const auto path = fs::findFile(_dir, name);
        if (!path)
            return Error::fail(std::string("No ") + name + " in " + _dir.string());
        std::vector<uint8_t> file;
        if (!readFile(*path, file))
            return Error::fail("Cannot read " + path->string());
        const x86::Win32Process::Module *m = nullptr;
        if (Error e = _process->load(name, file, m))
            return Error::fail(std::string(name) + ": " + e.message);
        (name == kLibrary ? _dll : _exe) = m;
    }
    if (_turningByTime)
        _turnedByTime = turnByTime(*_cpu, _dll->base);
    return Error::success();
}

Error GameProcess::attach() {
    if (!_dll)
        return Error::fail(std::string(kLibrary) + " isn't loaded");
    return _process->attach(*_dll);
}

Error GameProcess::run(uint32_t &exitCode) {
    if (!_exe)
        return Error::fail(std::string(kProgram) + " isn't loaded");
    return _process->run(*_exe, exitCode);
}

} // namespace reluva::legoisland
