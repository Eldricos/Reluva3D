// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The GDI stand-in (src/emulation/x86/gdi.cpp): objects, device contexts
// on pixels in the program's memory, and drawing into them, called as a
// program would.
//
//   reluva-tests common.unit.gdi

#include "emulation/x86/cpu.h"
#include "emulation/x86/gdi.h"
#include "emulation/x86/win32.h"
#include "tests/support/test.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace reluva;

namespace {

namespace stdfs = std::filesystem;

constexpr uint32_t kRed = 0x0000ff; // COLORREF: 0x00BBGGRR
constexpr uint32_t kBlue = 0xff0000;

struct Fixture {
    x86::Cpu cpu{16u << 20};
    x86::Win32Process process{cpu};
    x86::Gdi::Target target;
    uint32_t mem = 0;
    uint32_t dc = 0;

    explicit Fixture(const stdfs::path &tmp) {
        x86::Win32Process::Drive c;
        c.root = tmp;
        process.addDrive(c);
        // A 32 by 16 surface of 16-bit pixels, 5-6-5.
        target.width = 32;
        target.height = 16;
        target.pitch = 64;
        target.bits = 16;
        target.memory = process.memory().alloc(64 * 16);
        std::memset(cpu.memory().data() + target.memory, 0, 64 * 16);
        dc = process.gdi().createDc(target);
        mem = process.memory().alloc(1024);
    }
    uint32_t call(const char *dll, const char *name, const std::vector<uint32_t> &args) {
        uint32_t r = 0;
        CHECK(!cpu.call(process.standIn(dll, name), args, r));
        return r;
    }
    uint32_t gdi(const char *name, const std::vector<uint32_t> &args) { return call("GDI32.dll", name, args); }
    uint32_t user(const char *name, const std::vector<uint32_t> &args) { return call("USER32.dll", name, args); }
    uint16_t pixel(int x, int y) const { return cpu.read16(target.memory + uint32_t(y * 64 + x * 2)); }
    uint32_t rect(int l, int t, int r, int b) {
        cpu.write32(mem, uint32_t(l));
        cpu.write32(mem + 4, uint32_t(t));
        cpu.write32(mem + 8, uint32_t(r));
        cpu.write32(mem + 12, uint32_t(b));
        return mem;
    }
};

void testObjects(const stdfs::path &tmp) {
    Fixture f(tmp);
    const uint32_t brush = f.gdi("CreateSolidBrush", {kRed});
    const uint32_t pen = f.gdi("CreatePen", {0, 2, kBlue});
    CHECK(brush && pen && brush != pen);
    // SelectObject gives back what was selected: the stock white brush, black pen.
    CHECK(f.gdi("SelectObject", {f.dc, brush}) == f.gdi("GetStockObject", {0}));
    CHECK(f.gdi("SelectObject", {f.dc, pen}) == f.gdi("GetStockObject", {7}));
    // GetObjectA: LOGPEN's style, width, colour; its size with no buffer.
    CHECK(f.gdi("GetObjectA", {pen, 0, 0}) == 16);
    CHECK(f.gdi("GetObjectA", {pen, 16, f.mem + 512}) == 16);
    CHECK(f.cpu.read32(f.mem + 512 + 4) == 2 && f.cpu.read32(f.mem + 512 + 12) == kBlue);
    CHECK(f.gdi("DeleteObject", {brush}) == 1 && f.gdi("DeleteObject", {brush}) == 0);
    // Stock objects stay.
    CHECK(f.gdi("DeleteObject", {f.gdi("GetStockObject", {4})}) == 1 && f.gdi("GetStockObject", {4}) != 0);
    CHECK(f.gdi("SetTextColor", {f.dc, kRed}) == 0 && f.gdi("SetTextColor", {f.dc, 0}) == kRed);
    CHECK(f.gdi("SetBkMode", {f.dc, 1}) == 2);
}

void testDrawing(const stdfs::path &tmp) {
    Fixture f(tmp);
    // FillRect: red is 0xf800 in 5-6-5; the right and bottom edges left out.
    const uint32_t red = f.gdi("CreateSolidBrush", {kRed});
    CHECK(f.user("FillRect", {f.dc, f.rect(2, 2, 6, 5), red}) == 1);
    CHECK(f.pixel(2, 2) == 0xf800 && f.pixel(5, 4) == 0xf800 && f.pixel(6, 4) == 0 && f.pixel(5, 5) == 0);
    // A system colour's index + 1 as the brush: COLOR_WINDOW (5), white.
    CHECK(f.user("FillRect", {f.dc, f.rect(10, 0, 11, 1), 6}) == 1);
    CHECK(f.pixel(10, 0) == 0xffff);
    // LineTo leaves out its end.
    f.gdi("SelectObject", {f.dc, f.gdi("CreatePen", {0, 1, kBlue})});
    CHECK(f.gdi("MoveToEx", {f.dc, 0, 10, 0}) == 1 && f.gdi("LineTo", {f.dc, 4, 10}) == 1);
    CHECK(f.pixel(0, 10) == 0x001f && f.pixel(3, 10) == 0x001f && f.pixel(4, 10) == 0);
    // SetPixel gives the colour the pixel took.
    CHECK(f.gdi("SetPixel", {f.dc, 31, 15, 0x00ff00}) == 0x00ff00 && f.pixel(31, 15) == 0x07e0);
    CHECK(f.gdi("SetPixel", {f.dc, 32, 0, 0}) == 0xffffffffu);
    // An ellipse: filled inside its box, nothing at the corners.
    f.gdi("SelectObject", {f.dc, red});
    f.gdi("SelectObject", {f.dc, f.gdi("GetStockObject", {8})}); // NULL_PEN
    CHECK(f.gdi("Ellipse", {f.dc, 20, 0, 30, 10}) == 1);
    CHECK(f.pixel(25, 5) == 0xf800 && f.pixel(20, 0) == 0 && f.pixel(29, 9) == 0);
    // DrawTextA with DT_CALCRECT measures without drawing: 3 characters of
    // the built-in font, 6 pixels each, 11 high.
    f.cpu.writeString(f.mem + 256, "abc", 4);
    CHECK(f.user("DrawTextA", {f.dc, f.mem + 256, 0xffffffffu, f.rect(0, 0, 0, 0), 0x400}) == 11);
    CHECK(f.cpu.read32(f.mem + 8) == 18 && f.cpu.read32(f.mem + 12) == 11);
}

// A 4 by 2 8-bit BMP, its pixels i = x + 4y coloured (i * 30, 0, 0).
std::vector<uint8_t> smallBmp() {
    std::vector<uint8_t> b(54 + 1024 + 8, 0);
    auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&b[at], &v, 4); };
    b[0] = 'B';
    b[1] = 'M';
    put32(2, uint32_t(b.size()));
    put32(10, 54 + 1024);
    put32(14, 40);
    put32(18, 4);
    put32(22, 2);
    b[26] = 1;
    b[28] = 8;
    for (int i = 0; i < 8; ++i)
        b[54 + size_t(i) * 4 + 2] = uint8_t(i * 30); // red
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            b[54 + 1024 + size_t(1 - y) * 4 + size_t(x)] = uint8_t(x + 4 * y);
    return b;
}

void testBitmaps(const stdfs::path &tmp) {
    Fixture f(tmp);
    const auto bmp = smallBmp();
    std::ofstream(tmp / "PIC.BMP", std::ios::binary)
        .write(reinterpret_cast<const char *>(bmp.data()), std::streamsize(bmp.size()));
    f.cpu.writeString(f.mem + 256, "C:\\pic.bmp", 16);
    // LoadImageA(instance, name, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE).
    const uint32_t bitmap = f.user("LoadImageA", {0, f.mem + 256, 0, 0, 0, 0x10});
    CHECK(bitmap != 0);
    CHECK(f.gdi("GetObjectA", {bitmap, 24, f.mem + 512}) == 24);
    CHECK(f.cpu.read32(f.mem + 512 + 4) == 4 && f.cpu.read32(f.mem + 512 + 8) == 2 &&
          f.cpu.read16(f.mem + 512 + 18) == 8);
    const uint32_t memory = f.gdi("CreateCompatibleDC", {0});
    CHECK(f.gdi("SelectObject", {memory, bitmap}) != 0);
    // Stretched twice as wide: pixel (x, y) of the target is the bitmap's (x / 2, y).
    CHECK(f.gdi("StretchBlt", {f.dc, 0, 0, 8, 2, memory, 0, 0, 4, 2, 0x00cc0020}) == 1);
    // (210, 0, 0) in 5-6-5 is 26 << 11; (0, 0, 0) is 0.
    CHECK(f.pixel(0, 0) == 0 && f.pixel(7, 1) == (210 >> 3) << 11 && f.pixel(6, 1) == f.pixel(7, 1));
    CHECK(f.gdi("DeleteDC", {memory}) == 1 && f.gdi("DeleteObject", {bitmap}) == 1);
    f.cpu.writeString(f.mem + 256, "C:\\none.bmp", 16);
    CHECK(f.user("LoadImageA", {0, f.mem + 256, 0, 0, 0, 0x10}) == 0);
}

// A window's DC draws on screen(), or nowhere without one.
void testWindowDc(const stdfs::path &tmp) {
    Fixture f(tmp);
    const uint32_t lost = f.user("GetDC", {0x10010});
    CHECK(f.user("FillRect", {lost, f.rect(0, 0, 1, 1), 6}) == 1 && f.pixel(0, 0) == 0);
    CHECK(f.user("ReleaseDC", {0x10010, lost}) == 1);
    const x86::Gdi::Target screen = f.target;
    f.process.gdi().screen = [screen] { return screen; };
    const uint32_t screenDc = f.user("GetDC", {0});
    CHECK(f.user("FillRect", {screenDc, f.rect(0, 0, 1, 1), 6}) == 1 && f.pixel(0, 0) == 0xffff);
    // A window's own draws in its client area: a popup at (4, 2), 8 by 6.
    const uint32_t cls = f.mem + 256;
    for (uint32_t i = 0; i < 40; i += 4)
        f.cpu.write32(cls + i, 0);
    f.cpu.write32(cls + 4, f.process.standIn("USER32.dll", "DefWindowProcA"));
    f.cpu.writeString(f.mem + 320, "Plain", 8);
    f.cpu.write32(cls + 36, f.mem + 320);
    CHECK(f.user("RegisterClassA", {cls}) != 0);
    const uint32_t h = f.user("CreateWindowExA", {0, f.mem + 320, 0, 0x90000000, 4, 2, 8, 6, 0, 0, 0, 0});
    const uint32_t window = f.user("GetDC", {h});
    CHECK(f.user("FillRect", {window, f.rect(0, 0, 1, 1), 6}) == 1 && f.pixel(4, 2) == 0xffff && f.pixel(3, 2) == 0);
    // Clipped to it.
    CHECK(f.user("FillRect", {window, f.rect(7, 5, 20, 20), 6}) == 1 && f.pixel(11, 7) == 0xffff &&
          f.pixel(12, 7) == 0 && f.pixel(11, 8) == 0);
    // A window with a border (WS_BORDER) shown: its frame painted in the
    // face colour, (192, 192, 192), its client area left to it.
    const uint16_t face = (192 >> 3) << 11 | (192 >> 2) << 5 | (192 >> 3);
    const uint32_t framed = f.user("CreateWindowExA", {0, f.mem + 320, 0, 0x90800000, 16, 8, 12, 6, 0, 0, 0, 0});
    CHECK(f.pixel(16, 8) == face && f.pixel(27, 13) == face && f.pixel(17, 9) == 0);
    // Gone, what it covered is the desktop again: black.
    f.user("FillRect", {screenDc, f.rect(17, 9, 18, 10), 6});
    CHECK(f.user("DestroyWindow", {framed}) == 1 && f.pixel(16, 8) == 0 && f.pixel(17, 9) == 0);
    // The other window's place is left as it is.
    CHECK(f.pixel(4, 2) == 0xffff);
}

} // namespace

static const reluva::test::Suite kSuite("common.unit.gdi",
                                        {
                                            {"objects", [](const stdfs::path &tmp) { testObjects(tmp); }},
                                            {"drawing", [](const stdfs::path &tmp) { testDrawing(tmp); }},
                                            {"bitmaps", [](const stdfs::path &tmp) { testBitmaps(tmp); }},
                                            {"window dc", [](const stdfs::path &tmp) { testWindowDc(tmp); }},
                                        });
