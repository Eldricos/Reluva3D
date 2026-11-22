// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The movie window and MCI (src/emulation/x86/mci.cpp), called as a
// program would: a movie window opening an AVI file built here, its
// length and device, a play whose end is posted, and mciSendCommandA's
// answers.
//
//   reluva-tests common.unit.mci

#include "emulation/x86/cpu.h"
#include "emulation/x86/mci.h"
#include "emulation/x86/win32.h"
#include "tests/support/test.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace reluva;

namespace {

using Bytes = std::vector<uint8_t>;

void le(Bytes &b, uint32_t v, int n) {
    for (int i = 0; i < n; ++i)
        b.push_back(uint8_t(v >> (8 * i)));
}

void put32(Bytes &b, size_t at, uint32_t v) {
    for (size_t i = 0; i < 4; ++i)
        b[at + i] = uint8_t(v >> (8 * i));
}

void tag(Bytes &b, const char *t) {
    b.insert(b.end(), t, t + 4);
}

void chunk(Bytes &b, const char *t, const Bytes &body) {
    tag(b, t);
    le(b, uint32_t(body.size()), 4);
    b.insert(b.end(), body.begin(), body.end());
    if (body.size() & 1)
        b.push_back(0);
}

Bytes list(const char *type, const Bytes &body) {
    Bytes b;
    tag(b, type);
    b.insert(b.end(), body.begin(), body.end());
    return b;
}

// A 16 by 8 Cinepak AVI of three frames at 15 a second, each empty (a
// dropped frame), which is all opening and timing need.
Bytes movie() {
    // The headers' fields past those set here are 0.
    Bytes avih(56);
    Bytes strh(56);
    Bytes strf(40);
    Bytes strl;
    Bytes hdrl;
    Bytes movi;
    Bytes avi;
    put32(avih, 0, 66667);
    std::copy_n("vidscvid", 8, strh.begin());
    put32(strf, 0, 40);
    put32(strf, 4, 16);
    put32(strf, 8, 8);
    chunk(strl, "strh", strh);
    chunk(strl, "strf", strf);
    chunk(hdrl, "avih", avih);
    chunk(hdrl, "LIST", list("strl", strl));
    for (int i = 0; i < 3; ++i)
        chunk(movi, "00dc", {});
    chunk(avi, "LIST", list("hdrl", hdrl));
    chunk(avi, "LIST", list("movi", movi));
    Bytes file;
    chunk(file, "RIFF", list("AVI ", avi));
    return file;
}

struct Fixture {
    x86::Cpu cpu{16u << 20};
    x86::Win32Process process{cpu};
    x86::MciVideo video{process};
    uint32_t mem = 0;
    explicit Fixture(const std::filesystem::path &tmp) {
        mem = process.memory().alloc(4096);
        const Bytes file = movie();
        std::ofstream(tmp / "spin.avi", std::ios::binary)
            .write(reinterpret_cast<const char *>(file.data()), std::streamsize(file.size()));
        process.mapPath = [tmp](const std::string &path, bool) -> std::optional<std::filesystem::path> {
            if (path == "C:\\spin.avi")
                return tmp / "spin.avi";
            return std::nullopt;
        };
    }
    uint32_t call(const char *dll, const char *name, const std::vector<uint32_t> &args) {
        uint32_t r = 0;
        CHECK(!cpu.call(process.standIn(dll, name), args, r));
        return r;
    }
    uint32_t user(const char *name, const std::vector<uint32_t> &args) { return call("USER32.dll", name, args); }
    uint32_t mci(uint32_t device, uint32_t command, uint32_t flags, uint32_t parms) {
        return call("WINMM.dll", "mciSendCommandA", {device, command, flags, parms});
    }
    uint32_t str(uint32_t offset, const std::string &s) {
        cpu.writeString(mem + offset, s, uint32_t(s.size() + 1));
        return mem + offset;
    }
};

// MM_MCINOTIFY, MCI_NOTIFY and MCI_NOTIFY_SUCCESSFUL.
constexpr uint32_t kMmMciNotify = 0x3b9;
constexpr uint32_t kMciNotify = 1;
constexpr uint32_t kNotifySuccessful = 1;

void testWindow(const std::filesystem::path &tmp) {
    Fixture f(tmp);
    CHECK(f.call("MSVFW32.dll", "MCIWndRegisterClass", {}) == 1);
    // As LEGO Loco makes its own: a child window with no controls.
    const uint32_t h =
        f.user("CreateWindowExA", {0, f.str(0, "MCIWndClass"), f.str(32, "AVI"), 0x4000400b, 0, 0, 0, 0, 0, 0, 0, 0});
    CHECK(h != 0);
    CHECK(f.user("SendMessageA", {h, 0x499, 0, f.str(64, "C:\\spin.avi")}) == 0); // MCIWNDM_OPENA
    CHECK(f.video.opened() == 1);
    const uint32_t device = f.user("SendMessageA", {h, 0x464, 0, 0}); // MCIWNDM_GETDEVICEID
    CHECK(device != 0);
    CHECK(f.user("SendMessageA", {h, 0x468, 0, 0}) == 3); // MCIWNDM_GETLENGTH
    // PUT_DEST takes a RECT: left, top, right, bottom.
    for (uint32_t i = 0; i < 4; ++i)
        f.cpu.write32(f.mem + 128 + 4 * i, i < 2 ? 10 : 42);
    CHECK(f.user("SendMessageA", {h, 0x48f, 0, f.mem + 128}) == 0);
    // Without host threads a movie ends as it starts, and says so. The
    // callback's low word names the window, as LEGO Loco gives it.
    f.cpu.write32(f.mem + 256, h & 0xffff);
    CHECK(f.mci(device, 0x806, kMciNotify, f.mem + 256) == 0); // MCI_PLAY
    CHECK(!f.video.playing());
    CHECK(f.user("PeekMessageA", {f.mem + 512, 0, 0, 0, 1}) == 1);
    CHECK(f.cpu.read32(f.mem + 512) == h && f.cpu.read32(f.mem + 516) == kMmMciNotify);
    CHECK(f.cpu.read32(f.mem + 520) == kNotifySuccessful && f.cpu.read32(f.mem + 524) == device);
    // Closing the window closes its movie.
    CHECK(f.user("DestroyWindow", {h}) == 1);
    CHECK(f.mci(device, 0x814, 0x100, f.mem + 256) == 257); // MCIERR_INVALID_DEVICE_ID
}

void testCommands(const std::filesystem::path &tmp) {
    Fixture f(tmp);
    // MCI_OPEN with the file as its element: MCI_OPEN_PARMS' device id.
    for (uint32_t i = 0; i < 20; i += 4)
        f.cpu.write32(f.mem + 256 + i, 0);
    f.cpu.write32(f.mem + 256 + 12, f.str(64, "C:\\spin.avi"));
    CHECK(f.mci(0, 0x803, 0x200, f.mem + 256) == 0);
    const uint32_t device = f.cpu.read32(f.mem + 256 + 4);
    CHECK(device != 0);
    // MCI_STATUS: the length (1) and the mode (4), stopped.
    f.cpu.write32(f.mem + 288 + 8, 1);
    CHECK(f.mci(device, 0x814, 0x100, f.mem + 288) == 0 && f.cpu.read32(f.mem + 288 + 4) == 3);
    f.cpu.write32(f.mem + 288 + 8, 4);
    CHECK(f.mci(device, 0x814, 0x100, f.mem + 288) == 0 && f.cpu.read32(f.mem + 288 + 4) == 525);
    // A file that isn't there, and a command not stood in for.
    f.cpu.write32(f.mem + 256 + 12, f.str(96, "C:\\none.avi"));
    CHECK(f.mci(0, 0x803, 0x200, f.mem + 256) == 275);  // MCIERR_FILE_NOT_FOUND
    CHECK(f.mci(device, 0x80d, 0, f.mem + 288) == 274); // MCI_SET: MCIERR_UNSUPPORTED_FUNCTION
    CHECK(f.mci(device, 0x804, 0, 0) == 0);             // MCI_CLOSE
    CHECK(f.mci(device, 0x804, 0, 0) == 257);
}

} // namespace

static const reluva::test::Suite kSuite("common.unit.mci",
                                        {
                                            {"window", [](const std::filesystem::path &tmp) { testWindow(tmp); }},
                                            {"commands", [](const std::filesystem::path &tmp) { testCommands(tmp); }},
                                        });
