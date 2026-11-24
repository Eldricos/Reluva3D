// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/host/game_process.h"

#include "common/fs.h"
#include "common/stream.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <system_error>
#include <vector>

namespace reluva::sith {

namespace {

constexpr const char *kRegistryKey = "HKEY_LOCAL_MACHINE\\Software\\LucasArts Entertainment Company\\JediKnight\\v1.0";

// What a machine of the time without the device answers: no waveOut
// device. The program learns it has none, as it would there. (DirectPlay's
// lobby is DirectPlay's own: it tells the game no lobby started it.)
void addAbsentDevices(x86::Win32Process &process) {
    auto absent = [&](const char *dll, const char *name, int args, uint32_t result) {
        process.provideStdcall(dll, name, args, [result](x86::Cpu &) { return result; });
    };
    absent("WINMM.dll", "waveOutOpen", 6, 2); // MMSYSERR_BADDEVICEID
    for (const char *name : {"waveOutClose", "waveOutReset"})
        absent("WINMM.dll", name, 1, 5); // MMSYSERR_INVALHANDLE
    for (const char *name : {"waveOutPrepareHeader", "waveOutUnprepareHeader", "waveOutWrite"})
        absent("WINMM.dll", name, 3, 5);
    absent("WINMM.dll", "waveOutSetVolume", 2, 5);
    absent("WINMM.dll", "waveOutGetID", 2, 5);
}

} // namespace

GameProcess::GameProcess(const GameFolders &folders, const std::filesystem::path &saveFolder,
                         const std::string &commandLine)
    : _folders(folders), _cpu(std::make_unique<x86::Cpu>(256u << 20)) {
    std::error_code ec;
    std::filesystem::create_directories(saveFolder, ec);
    _process = std::make_unique<x86::Win32Process>(*_cpu, "JK.EXE", true, true);
    x86::Win32Process &p = *_process;
    p.setProgram("JK.EXE", "C:\\");
    p.setCommandLine("JK.EXE" + std::string(commandLine.empty() ? "" : " ") + commandLine);
    p.addDrive({'C', folders.install, saveFolder, 3, "JEDI", 0x12345678});
    insertCd(1);
    // The settings the game keeps in the registry (its display, its last
    // player...), as it left them; then where it's installed, as its
    // installer would have them.
    _registryFile = saveFolder / "registry.txt";
    loadRegistry();
    p.setRegistryString(kRegistryKey, "CD Path", "D:\\");
    p.setRegistryString(kRegistryKey, "Install Path", "C:\\");
    p.registryWritten = [this](const std::string &key, const std::string &name) {
        if (fs::toLower(key) != fs::toLower(kRegistryKey))
            return;
        if (std::find(_registryNames.begin(), _registryNames.end(), name) == _registryNames.end())
            _registryNames.push_back(name);
        saveRegistry();
    };
    // The program's and Smacker's threads each on a thread of this machine.
    p.useHostThreads();
    _directX = std::make_unique<x86::DirectX>(p);
    // The menus and the software renderer draw in 8-bit colour.
    _directX->modes.insert(_directX->modes.begin(), {{640, 480, 8}, {800, 600, 8}, {1024, 768, 8}});
    _directInput = std::make_unique<x86::DirectInput>(p);
    _directPlay = std::make_unique<x86::DirectPlay>(p);
    // No joystick until the player's controller is connected (Player).
    _joysticks = std::make_unique<x86::Joysticks>(p);
    // The network answered between the game's own calls to DirectPlay too,
    // so players can join a host whose menus aren't asking; and a captured
    // joystick's messages posted, though the game captures none.
    p.onSlice = [this](x86::Cpu &) {
        const int64_t now =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        int64_t last = _networkAt.load();
        if (now - last >= 20 && _networkAt.compare_exchange_strong(last, now)) {
            _directPlay->update();
            _joysticks->update(uint32_t(now));
        }
    };
    _directSound = std::make_unique<x86::DirectSound>(p);
    // The music of the CD in drive D: its audio tracks, from a cue sheet and
    // its image, or ripped, beside its files.
    _cdAudio = std::make_unique<x86::CdAudio>(p);
    _cdAudio->disc = [this] {
        return _cdInDrive ? x86::CdAudio::discIn(_folders.cds[size_t(_cdInDrive - 1)]) : x86::CdAudio::Disc();
    };
    addAbsentDevices(p);
}

GameProcess::~GameProcess() = default;

Error GameProcess::load() {
    std::vector<uint8_t> file;
    const x86::Win32Process::Module *smack = nullptr;
    if (const auto path = fs::findFile(_folders.install, "SMACKW32.DLL")) {
        if (!readFile(*path, file))
            return Error::fail("can't read " + path->string());
        if (Error e = _process->load("SMACKW32.DLL", file, smack))
            return Error::fail("SMACKW32.DLL: " + e.message);
    }
    const auto exePath = fs::findFile(_folders.install, "JK.EXE");
    if (!exePath)
        return Error::fail("Jedi Knight's program, JK.EXE, isn't in " + _folders.install.string());
    if (!readFile(*exePath, file))
        return Error::fail("can't read " + exePath->string());
    if (Error e = _process->load("JK.EXE", file, _exe))
        return Error::fail("JK.EXE: " + e.message);
    hookCdCheck();
    if (smack)
        if (Error e = _process->attach(*smack))
            return e;
    return Error::success();
}

// The registry file: a value a line, its name, type and bytes in hex,
// separated by tabs.
void GameProcess::loadRegistry() {
    std::ifstream f(_registryFile);
    std::string line;
    while (std::getline(f, line)) {
        const size_t a = line.find('\t');
        const size_t b = a == std::string::npos ? a : line.find('\t', a + 1);
        if (b == std::string::npos)
            continue;
        const std::string name = line.substr(0, a);
        const uint32_t type = uint32_t(std::strtoul(line.substr(a + 1, b - a - 1).c_str(), nullptr, 10));
        const std::string hex = line.substr(b + 1);
        std::vector<uint8_t> data;
        for (size_t i = 0; i + 1 < hex.size(); i += 2)
            data.push_back(uint8_t(std::strtoul(hex.substr(i, 2).c_str(), nullptr, 16)));
        _registryNames.push_back(name);
        _process->setRegistryValue(kRegistryKey, name, type, std::move(data));
    }
}

void GameProcess::saveRegistry() {
    std::ofstream f(_registryFile);
    for (const std::string &name : _registryNames) {
        uint32_t type = 0;
        const auto value = _process->registryValue(kRegistryKey, name, &type);
        if (!value || name == "CD Path" || name == "Install Path")
            continue;
        f << name << '\t' << type << '\t';
        static const char kHex[] = "0123456789abcdef";
        for (uint8_t byte : *value)
            f << kHex[byte >> 4] << kHex[byte & 15];
        f << '\n';
    }
}

bool GameProcess::insertCd(int number) {
    if (number < 1 || number > kCdCount || _folders.cds[size_t(number - 1)].empty())
        return false;
    if (number == _cdInDrive)
        return true;
    _process->addDrive({'D',
                        _folders.cds[size_t(number - 1)],
                        {},
                        5,
                        "JEDI_KNIGHT_" + std::to_string(number),
                        0x0ced0u + uint32_t(number)});
    _cdInDrive = number;
    if (_cdAudio)
        _cdAudio->discChanged();
    return true;
}

// JK.EXE checks for the CD a step of its episode is on with the function
// at 0x40ebb0, its argument the CD's number (0 for either): it reads
// gamedata\resource\jk_.cd from the CD and, if it isn't that CD's, asks
// for it and waits (docs/engines/sith/games/jk/systems/hosting.md). Here a
// trampoline at its start puts that CD in drive D first, as a player would
// have, then runs the instruction it replaced and goes on.
bool GameProcess::saveSettings() {
    constexpr uint32_t kSave = 0x4148f0;
    // mov eax, [0x556588]; push eax: the US release's.
    const uint8_t expected[] = {0xa1, 0x88, 0x65, 0x55, 0x00, 0x50};
    x86::Cpu &c = *_cpu;
    for (uint32_t i = 0; i < sizeof expected; ++i)
        if (!c.valid(kSave + i, 1) || c.read8(kSave + i) != expected[i])
            return false;
    // The stopped thread's stack may be anywhere: a fresh one for the call.
    constexpr uint32_t kStack = 0x10000;
    const uint32_t stack = _process->memory().alloc(kStack);
    if (!stack)
        return false;
    const uint32_t esp = c.reg[x86::Cpu::ESP];
    c.reg[x86::Cpu::ESP] = stack + kStack - 16;
    uint32_t result = 0;
    c.clearStop();
    const Error e = c.call(kSave, {}, result);
    c.reg[x86::Cpu::ESP] = esp;
    _process->memory().free(stack);
    return !e;
}

void GameProcess::hookCdCheck() {
    constexpr uint32_t kCheck = 0x40ebb0;
    // sub esp, 0x18c; push ebx; push ebp: the US release's.
    const uint8_t expected[] = {0x81, 0xec, 0x8c, 0x01, 0x00, 0x00, 0x53, 0x55};
    x86::Cpu &c = *_cpu;
    for (uint32_t i = 0; i < sizeof expected; ++i)
        if (!c.valid(kCheck + i, 1) || c.read8(kCheck + i) != expected[i])
            return;
    // cdecl, called from the trampoline: its return address, the check's
    // caller's, then the CD's number.
    const uint32_t native = c.addNative("JK.EXE CD check", [this](x86::Cpu &cpu) {
        insertCd(int(cpu.arg(1)));
        return 0u;
    });
    const uint32_t trampoline = _process->memory().alloc(16);
    if (!native || !trampoline)
        return;
    const uint8_t code[16] = {0xe8, 0, 0, 0, 0, 0x81, 0xec, 0x8c, 0x01, 0x00, 0x00, 0xe9, 0, 0, 0, 0};
    for (uint32_t i = 0; i < 16; ++i)
        c.write8(trampoline + i, code[i]);
    c.write32(trampoline + 1, native - (trampoline + 5));         // call the native
    c.write32(trampoline + 12, (kCheck + 6) - (trampoline + 16)); // jmp back after sub esp
    c.write8(kCheck, 0xe9);                                       // jmp trampoline
    c.write32(kCheck + 1, trampoline - (kCheck + 5));
    c.write8(kCheck + 5, 0x90);
}

Error GameProcess::run(uint32_t &exitCode) {
    if (!_exe)
        return Error::fail("JK.EXE isn't loaded");
    return _process->run(*_exe, exitCode);
}

} // namespace reluva::sith
