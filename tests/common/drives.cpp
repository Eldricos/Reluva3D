// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The Windows emulator's drives (Win32Process::addDrive): folders of this
// machine as drive letters, with writes kept apart, and a serial port the
// host provides, read plainly and overlapped.
//
//   reluva-tests common.unit.drives

#include "emulation/x86/cpu.h"
#include "emulation/x86/win32.h"
#include "tests/support/test.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace reluva;

namespace {

void testWin32Files() {
    // Drives on folders: names matched ignoring case, writes kept apart.
    namespace stdfs = std::filesystem;
    const stdfs::path root = stdfs::temp_directory_path() / "reluva_win32_files";
    stdfs::remove_all(root);
    stdfs::create_directories(root / "game" / "GameData");
    std::ofstream(root / "game" / "GameData" / "Config.TXT") << "x";
    x86::Cpu cpu(16u << 20);
    x86::Win32Process process(cpu);
    x86::Win32Process::Drive c;
    c.root = root / "game";
    c.writable = root / "save";
    process.addDrive(c);
    process.setCurrentFolder("C:\\GameData");
    CHECK(process.fullPath("..\\x\\.\\y") == "C:\\x\\y");
    CHECK(process.fullPath("a/b") == "C:\\GameData\\a\\b");
    CHECK(process.fullPath("\\top") == "C:\\top");
    CHECK(process.hostPath("config.txt", false) == root / "game" / "GameData" / "Config.TXT");
    CHECK(process.hostPath("C:\\GAMEDATA\\CONFIG.TXT", true) == root / "save" / "GameData" / "Config.TXT");
    CHECK(process.hostPath("D:\\x", false).empty());
    // A new file goes into a folder only if the folder is there, in the
    // game's files or in the writes (NFS III tests folders so).
    auto call = [&](const char *name, std::vector<uint32_t> args) {
        uint32_t r = 0;
        CHECK(!cpu.call(process.standIn("KERNEL32.dll", name), args, r));
        return r;
    };
    const uint32_t mem = process.memory().alloc(256);
    cpu.writeString(mem, "C:\\NoSuchDir\\exist.nam", 64);
    CHECK(call("CreateFileA", {mem, 0xc0000000u, 0, 0, 2, 0x80, 0}) == 0xffffffffu);
    CHECK(call("GetLastError", {}) == 3); // ERROR_PATH_NOT_FOUND
    CHECK(!stdfs::exists(root / "save" / "NoSuchDir"));
    cpu.writeString(mem, "C:\\GameData\\exist.nam", 64);
    const uint32_t h = call("CreateFileA", {mem, 0xc0000000u, 0, 0, 2, 0x80, 0});
    CHECK(h != 0xffffffffu);
    call("CloseHandle", {h});
    CHECK(stdfs::exists(root / "save" / "GameData" / "exist.nam"));
    stdfs::remove_all(root);
}
// A serial port the host provides (Win32Process::openSerialPort): plain
// reads, and overlapped ones left pending until there are bytes.
void testWin32SerialPort() {
    struct Port : x86::DeviceObject {
        std::string incoming, written;
        bool read(x86::Cpu &c, uint32_t buffer, uint32_t size, bool, uint32_t &done, uint32_t &) override {
            done = uint32_t(std::min<size_t>(size, incoming.size()));
            for (uint32_t i = 0; i < done; ++i)
                c.write8(buffer + i, uint8_t(incoming[i]));
            incoming.erase(0, done);
            return true;
        }
        bool write(x86::Cpu &c, uint32_t buffer, uint32_t size, uint32_t &done, uint32_t &) override {
            for (uint32_t i = 0; i < size; ++i)
                written += char(c.read8(buffer + i));
            done = size;
            return true;
        }
        bool readable() override { return !incoming.empty(); }
    };
    x86::Cpu cpu(16u << 20);
    x86::Win32Process process(cpu);
    // Devices are the drives' stand-ins', and their events the host threads'.
    x86::Win32Process::Drive c;
    c.root = std::filesystem::temp_directory_path();
    process.addDrive(c);
    process.useHostThreads();
    auto port = std::make_shared<Port>();
    int opened = 0;
    process.openSerialPort = [&](int n) -> std::shared_ptr<x86::DeviceObject> {
        opened = n;
        return n == 2 ? port : nullptr;
    };
    auto call = [&](const char *name, std::vector<uint32_t> args) {
        uint32_t r = 0;
        CHECK(!cpu.call(process.standIn("KERNEL32.dll", name), args, r));
        return r;
    };
    const uint32_t mem = process.memory().alloc(256);
    cpu.writeString(mem, "\\\\.\\COM2", 16);
    const uint32_t h = call("CreateFileA", {mem, 0xc0000000u, 0, 0, 3, 0x40000080u, 0});
    CHECK(opened == 2 && h != 0xffffffffu && call("GetFileType", {h}) == 2);
    cpu.writeString(mem, "COM1", 16);
    CHECK(call("CreateFileA", {mem, 0xc0000000u, 0, 0, 3, 0, 0}) == 0xffffffffu && opened == 1);
    // Written and read plainly.
    cpu.writeString(mem + 16, "hi", 3);
    const uint32_t done = mem + 32;
    const uint32_t buffer = mem + 64;
    const uint32_t overlapped = mem + 128;
    CHECK(call("WriteFile", {h, mem + 16, 2, done, 0}) == 1 && port->written == "hi" && cpu.read32(done) == 2);
    port->incoming = "abc";
    CHECK(call("ReadFile", {h, buffer, 2, done, 0}) == 1 && cpu.read32(done) == 2 && cpu.read8(buffer + 1) == 'b');
    // Overlapped: what's there at once; then nothing, pending, its event
    // signalled when bytes come, which GetOverlappedResult takes.
    const uint32_t event = call("CreateEventA", {0, 1, 0, 0});
    cpu.write32(overlapped + 16, event);
    CHECK(call("ReadFile", {h, buffer, 8, done, overlapped}) == 1 && cpu.read32(done) == 1 && cpu.read8(buffer) == 'c');
    CHECK(call("ReadFile", {h, buffer, 8, done, overlapped}) == 0 && process.lastError() == 997);
    CHECK(call("WaitForSingleObject", {event, 0}) == 0x102); // WAIT_TIMEOUT
    CHECK(call("GetOverlappedResult", {h, overlapped, done, 0}) == 0 && process.lastError() == 996);
    port->incoming = "xyz";
    CHECK(call("WaitForSingleObject", {event, 0}) == 0);
    CHECK(call("GetOverlappedResult", {h, overlapped, done, 0}) == 1 && cpu.read32(done) == 3 &&
          cpu.read8(buffer + 2) == 'z');
    CHECK(call("CloseHandle", {h}) == 1);
}

} // namespace

static const reluva::test::Suite
    kSuite("common.unit.drives", {
                                     {"files", [](const std::filesystem::path &) { testWin32Files(); }},
                                     {"serial port", [](const std::filesystem::path &) { testWin32SerialPort(); }},
                                 });
