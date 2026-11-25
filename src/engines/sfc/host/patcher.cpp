// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sfc/host/patcher.h"

#include "common/fs.h"
#include "common/installshield.h"
#include "common/mscab.h"
#include "common/stream.h"
#include "emulation/x86/cpu.h"
#include "emulation/x86/win32.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <optional>
#include <system_error>
#include <vector>

namespace reluva::sfc {

namespace {

constexpr uint32_t kMemoryMb = 512; // Patchw32.dll at its own base, 0x10000000

} // namespace

Error Patcher::unpack(const std::filesystem::path &download, const std::filesystem::path &to) {
    std::vector<uint8_t> file;
    if (!readFile(download, file))
        return Error::fail("Can't read " + download.string());
    std::vector<CabinetFile> setup;
    if (Error e = readMicrosoftCabinets(file, setup))
        return e;
    std::error_code ec;
    const std::filesystem::path setupDir = to / "setup";
    std::filesystem::create_directories(setupDir, ec);
    for (const CabinetFile &f : setup) {
        // Its name made a path under the folder: a cabinet's names start
        // with a backslash, and one that would climb out isn't taken.
        std::filesystem::path out = setupDir;
        bool climbs = false;
        std::string part;
        for (const char ch : f.name + "\\") {
            if (ch != '\\' && ch != '/') {
                part += ch;
                continue;
            }
            if (part == "..")
                climbs = true;
            else if (!part.empty() && part != ".")
                out /= part;
            part.clear();
        }
        if (climbs || out == setupDir)
            continue;
        std::filesystem::create_directories(out.parent_path(), ec);
        if (!writeFile(out, f.data))
            return Error::fail("Can't write " + out.string());
    }
    const auto header = fs::findFile(setupDir, "data1.hdr");
    if (!header)
        return Error::fail(download.string() + " isn't the 2.0.3.6 patch: no setup inside");
    InstallShieldCabinet cabinet;
    if (Error e = readInstallShieldCabinet(*header, cabinet))
        return e;
    bool rtp = false;
    for (size_t i = 0; i < cabinet.files.size(); ++i) {
        const std::string &path = cabinet.files[i].path;
        const std::string name = path.substr(path.find_last_of('/') + 1);
        std::vector<uint8_t> data;
        if (name.empty() || extractInstallShieldFile(cabinet, i, data))
            continue;
        if (!writeFile(to / name, data))
            return Error::fail("Can't write " + (to / name).string());
        rtp = rtp || fs::toLower(name) == "patchsfc2-2002-2036.rtp";
    }
    if (!rtp)
        return Error::fail(download.string() + " isn't the 2.0.3.6 patch: no PATCHSFC2-2002-2036.RTP");
    return Error::success();
}

Patcher::Patcher(std::filesystem::path install, std::filesystem::path patchFiles, std::filesystem::path scratch)
    : _install(std::move(install)), _patch(std::move(patchFiles)), _scratch(std::move(scratch)) {
}

Error Patcher::run(uint32_t &exitCode) {
    exitCode = 0;
    std::error_code ec;
    std::filesystem::create_directories(_scratch, ec);
    x86::Cpu cpu(kMemoryMb << 20);
    x86::Win32Process process(cpu, "Patch.exe", true, true);
    // The install is drive C:, which the patch changes in place; the patch's
    // own files D:, its program's folder, what it writes there going to the
    // scratch folder instead.
    x86::Win32Process::Drive install;
    install.letter = 'C';
    install.root = _install;
    process.addDrive(install);
    x86::Win32Process::Drive patch;
    patch.letter = 'D';
    patch.root = _patch;
    patch.writable = _scratch;
    process.addDrive(patch);
    process.setProgram("Patch.exe", "D:\\");
    // Its temporary files with what else it writes, not in the install.
    process.tempFolder = "D:\\TEMP\\";
    std::filesystem::create_directories(_scratch / "TEMP", ec);
    // As its batch file runs it: patch.exe and the install's folder.
    // The patch file by a DOS name (8.3), as the patcher looks for its file
    // by its short name, which Reluva's files haven't.
    const auto rtp = fs::findFile(_patch, "PATCHSFC2-2002-2036.RTP");
    if (!rtp)
        return Error::fail("No PATCHSFC2-2002-2036.RTP in " + _patch.string());
    std::filesystem::copy_file(*rtp, _scratch / "SFC2.RTP", std::filesystem::copy_options::overwrite_existing, ec);
    process.setCommandLine("patch.exe C:\\ D:\\SFC2.RTP");
    // Its waits and mutexes, as the game's.
    process.useHostThreads();
    process.output = [this](const std::string &text) {
        if (output)
            output(text);
    };
    // With logCalls, the last instructions run too, said if the run stops.
    std::vector<uint32_t> recent(64);
    size_t recentAt = 0;
    if (logCalls)
        cpu.trace = [this, &recent, &recentAt](x86::Cpu &c) {
            recent[recentAt++ % recent.size()] = c.eip;
            const std::string *name = c.nativeName(c.eip);
            if (!name || !output)
                return;
            std::string line = "call " + *name;
            const uint32_t first = c.arg(0);
            if (first >= 0x10000 && c.valid(first, 1)) {
                const std::string text = c.string(first, 80);
                if (!text.empty() && std::all_of(text.begin(), text.end(), [](char ch) { return ch >= 0x20; }))
                    line += " \"" + text + "\"";
            }
            output(line + "\n");
        };
    std::vector<uint8_t> file;
    const auto dllPath = fs::findFile(_patch, "Patchw32.dll");
    const auto exePath = fs::findFile(_patch, "Patch.exe");
    if (!dllPath || !exePath)
        return Error::fail("No Patch.exe and Patchw32.dll in " + _patch.string());
    // The DLL first, so Patch.exe's import of it binds.
    const x86::Win32Process::Module *dll = nullptr;
    if (!readFile(*dllPath, file))
        return Error::fail("Can't read " + dllPath->string());
    if (Error e = process.load("PATCHW32.dll", file, dll))
        return e;
    if (Error e = process.attach(*dll))
        return e;
    const x86::Win32Process::Module *exe = nullptr;
    if (!readFile(*exePath, file))
        return Error::fail("Can't read " + exePath->string());
    if (Error e = process.load("Patch.exe", file, exe))
        return e;
    if (output && !process.missingImports().empty()) {
        std::string list = "imports not stood in for:";
        for (const std::string &m : process.missingImports())
            list += " " + m;
        output(list + "\n");
    }
    Error err = process.run(*exe, exitCode);
    if (err && logCalls && output) {
        std::string line = "last instructions:";
        char at[16];
        for (size_t i = recentAt > recent.size() ? recentAt - recent.size() : 0; i < recentAt; ++i) {
            std::snprintf(at, sizeof(at), " %08x", recent[i % recent.size()]);
            line += at;
        }
        output(line + "\n");
    }
    return err;
}

} // namespace reluva::sfc
