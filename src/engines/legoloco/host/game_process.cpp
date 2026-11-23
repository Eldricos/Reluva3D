// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/legoloco/host/game_process.h"

#include "common/fs.h"

#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

namespace reluva::legoloco {

namespace {

constexpr const char *kProgram = "Loco.exe";
constexpr const char *kExeFolder = "C:\\Exe\\";

Error readFile(const std::filesystem::path &path, std::string &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return Error::fail("Cannot read " + path.string());
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return Error::success();
}

} // namespace

GameProcess::GameProcess(std::filesystem::path gameDir, std::filesystem::path saveDir, uint32_t memoryMb)
    : _dir(std::move(gameDir)), _saveDir(std::move(saveDir)) {
    _cpu = std::make_unique<x86::Cpu>(memoryMb << 20);
    // loco.exe has no relocations: 0x400000 is kept for it. Its threads
    // (the multimedia timer's among them) wait on each other.
    _process = std::make_unique<x86::Win32Process>(*_cpu, kProgram, true);
    _process->useHostThreads();
    x86::Win32Process::Drive c;
    c.letter = 'C';
    c.root = _dir;
    c.writable = _saveDir;
    c.label = "LEGOLOCO";
    _process->addDrive(c);
    _process->setProgram(kProgram, kExeFolder);
    _process->timeStampFromClock();
    // As the installer leaves it.
    _process->setRegistryString("HKEY_LOCAL_MACHINE\\SOFTWARE\\Intelligent Games\\LEGO LOCO", "Path", kExeFolder);
    _directX = std::make_unique<x86::DirectX>(*_process);
    _directPlay = std::make_unique<x86::DirectPlay>(*_process);
    _video = std::make_unique<x86::MciVideo>(*_process);
    _directSound = std::make_unique<x86::DirectSound>(*_process);
}

// The game finds its files by LEGO.INI's [DIRECTORIES] and [Video], which
// name the folder it was installed in (or the CD's d:\loco). The save
// folder's copy, which the program reads first, names C:'s instead; the
// rest is the player's own: the copy's at first (the language), then what
// the game and the player keep there.
Error GameProcess::writeIni() {
    std::string text;
    const std::filesystem::path saved = _saveDir / "Exe" / "LEGO.INI";
    std::error_code exists;
    if (std::filesystem::is_regular_file(saved, exists)) {
        if (Error err = readFile(saved, text))
            return err;
    } else if (const auto own = fs::findFile(_dir, "Exe/LEGO.INI")) {
        if (Error err = readFile(*own, text))
            return err;
    }
    std::istringstream in(text);
    std::ostringstream out;
    std::string line;
    std::string section;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.empty() && line.front() == '[')
            section = fs::toLower(line);
        const std::string key = fs::toLower(line.substr(0, line.find('=')));
        if (section == "[directories]" && key == "res")
            line = "Res=C:\\art-res";
        else if (section == "[directories]" && key == "resfile")
            line = "ResFile=C:\\art-res\\resource.rfh";
        else if (section == "[video]" && key == "dir")
            line = "Dir=C:\\Video\\LocoIntr.avi";
        out << line << "\r\n";
    }
    if (text.empty())
        out << "[DIRECTORIES]\r\nRes=C:\\art-res\r\nResFile=C:\\art-res\\resource.rfh\r\nexe=loco.exe\r\n\r\n"
               "[Video]\r\nDir=C:\\Video\\LocoIntr.avi\r\n";
    std::error_code ec;
    std::filesystem::create_directories(_saveDir / "Exe", ec);
    std::ofstream file(_saveDir / "Exe" / "LEGO.INI", std::ios::binary);
    if (!(file << out.str()))
        return Error::fail("Cannot write " + (_saveDir / "Exe" / "LEGO.INI").string());
    return Error::success();
}

Error GameProcess::load() {
    if (_saveDir.empty())
        return Error::fail("No save folder for LEGO Loco");
    if (Error err = writeIni())
        return err;
    const auto path = fs::findFile(_dir, std::string("Exe/") + kProgram);
    if (!path)
        return Error::fail("No Exe/Loco.exe in " + _dir.string());
    std::ifstream in(*path, std::ios::binary);
    const std::vector<uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return _process->load(kProgram, file, _exe);
}

Error GameProcess::run(uint32_t &exitCode) {
    if (!_exe)
        return Error::fail("Loco.exe isn't loaded");
    return _process->run(*_exe, exitCode);
}

} // namespace reluva::legoloco
