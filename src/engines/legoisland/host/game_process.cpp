// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/legoisland/host/game_process.h"

#include "common/fs.h"
#include "common/stream.h"

#include <algorithm>
#include <cstring>

namespace reluva::legoisland {

namespace {

constexpr const char *kProgram = "ISLE.EXE";
constexpr const char *kLibrary = "LEGO1.DLL";
// Where the installer records the game's folders (its log).
// The player turns as fast by time as it does a frame at this many frames
// a second: what a tester found right (systems/running.md).
constexpr double kTurnFramesPerSecond = 20;
// A long frame (a load) turns no more than a quarter second would.
constexpr double kLongestTurnFrame = 0.25;

constexpr const char *kRegistryKey = "HKEY_LOCAL_MACHINE\\SOFTWARE\\Mindscape\\LEGO Island";

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
    // As the installer leaves them: the install folder by its short name,
    // and the CD's drive.
    _process->setRegistryString(kRegistryKey, "diskpath", "C:\\PROGRA~1\\LEGOIS~1");
    _process->setRegistryString(kRegistryKey, "cdpath", "D:");
    // Where it keeps its saved games (History.gsi, Players.gsi): without
    // it, C:\, which isn't mapped. The install folder's writes go to the
    // save folder.
    _process->setRegistryString(kRegistryKey, "savepath", "C:\\PROGRA~1\\LEGOIS~1");
    // The island's detailed model (isle_hi: its roads textured, marked and
    // edged); without it, "1", the plain one.
    _process->setRegistryString(kRegistryKey, "Island Quality", "2");
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
        turnByTime();
    return Error::success();
}

// LEGO1.DLL's navigation (0x10054de0) moves the player by the time since
// it last did, but, unless it eases its turns in (a flag at +0x54 of it),
// turns it by its turn speed times a factor (+0x1c, +0x50) each time,
// whatever the time: in degrees, made radians just after. There the
// angle, worked out by `fld [esi+0x50]; fmul [edi]` (5 bytes), is made
// by a call to a native instead, times the frame's time (seconds, in the
// caller's [esp+0x14]) and kTurnFramesPerSecond. Only where the code is
// as expected.
void GameProcess::turnByTime() {
    static constexpr uint8_t kExpected[] = {0x80, 0x7e, 0x54, 0x00, // cmp byte [esi+0x54], 0
                                            0x74, 0x08,             // je the untimed turn
                                            0xd9, 0x07,             // fld [edi]
                                            0xd8, 0x4c, 0x24, 0x14, // fmul [esp+0x14]
                                            0xeb, 0x05,             // jmp past it
                                            0xd9, 0x46, 0x50, 0xd8,
                                            0x0f,        // the untimed turn: fld [esi+0x50]; fmul [edi]
                                            0xdc, 0x0d}; // fmul qword [degrees to radians]
    constexpr uint32_t kAt = 0x54f91;                    // from the DLL's base
    constexpr uint32_t kTurn = kAt + 14;
    x86::Cpu &c = *_cpu;
    const uint32_t at = _dll->base + kAt;
    if (!c.valid(at, sizeof kExpected))
        return;
    for (uint32_t i = 0; i < sizeof kExpected; ++i)
        if (c.read8(at + i) != kExpected[i])
            return;
    const uint32_t native = c.addNative("LEGO1.DLL turn by time", [](x86::Cpu &cpu) {
        auto f32 = [&cpu](uint32_t a) {
            const uint32_t v = cpu.read32(a);
            float f;
            std::memcpy(&f, &v, 4);
            return double(f);
        };
        const uint32_t esp = cpu.reg[x86::Cpu::ESP] + 4; // the caller's, past the return address
        const double seconds = std::clamp(f32(esp + 0x14), 0.0, kLongestTurnFrame);
        cpu.fpuPush(f32(cpu.reg[x86::Cpu::ESI] + 0x50) * f32(cpu.reg[x86::Cpu::EDI]) * seconds * kTurnFramesPerSecond);
        return 0u;
    });
    c.write8(_dll->base + kTurn, 0xe8); // CALL rel32
    c.write32(_dll->base + kTurn + 1, native - (_dll->base + kTurn + 5));
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
