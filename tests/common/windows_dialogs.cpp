// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// USER32's windows past their making, timers and dialogs (win32_windows.cpp,
// win32_dialogs.cpp), called as a program would: a dialog made from a
// template in a DLL built here.
//
//   reluva-tests common.unit.windows_dialogs

#include "emulation/x86/cpu.h"
#include "emulation/x86/win32.h"
#include "tests/support/test.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using namespace reluva;

namespace {

using Bytes = std::vector<uint8_t>;

void put16(Bytes &b, size_t at, uint32_t v) {
    b[at] = uint8_t(v);
    b[at + 1] = uint8_t(v >> 8);
}

void put32(Bytes &b, size_t at, uint32_t v) {
    put16(b, at, v & 0xffff);
    put16(b, at + 2, v >> 16);
}

// A dialog template (DLGTEMPLATE): 100 by 50 dialog units, an auto radio
// button (id 10), another (id 11) and a list box (id 20).
Bytes dialogTemplate() {
    Bytes t;
    auto w16 = [&](uint32_t v) {
        t.push_back(uint8_t(v));
        t.push_back(uint8_t(v >> 8));
    };
    auto w32 = [&](uint32_t v) {
        w16(v & 0xffff);
        w16(v >> 16);
    };
    auto text = [&](const char *s) {
        for (; *s; ++s)
            w16(uint8_t(*s));
        w16(0);
    };
    auto align = [&] {
        while (t.size() % 4)
            t.push_back(0);
    };
    w32(0x80c80000); // WS_POPUP | WS_CAPTION | WS_SYSMENU
    w32(0);
    w16(3);
    w16(10); // x, y, cx, cy
    w16(20);
    w16(100);
    w16(50);
    w16(0); // no menu
    w16(0); // the dialog class
    text("Network");
    struct Item {
        uint32_t style;
        uint16_t id;
        uint16_t cls;
        const char *text;
    };
    for (const Item &i :
         {Item{0x50000009, 10, 0x80, "IPX"}, Item{0x50000009, 11, 0x80, "TCP/IP"}, Item{0x50a10103, 20, 0x83, ""}}) {
        align();
        w32(i.style);
        w32(0);
        w16(4);
        w16(4);
        w16(40);
        w16(10);
        w16(i.id);
        w16(0xffff);
        w16(i.cls);
        text(i.text);
        w16(0); // no creation data
    }
    return t;
}

// A DLL whose resources hold that template as dialog 100.
Bytes dialogDll() {
    const Bytes tpl = dialogTemplate();
    Bytes f(0x400 + ((tpl.size() + 0x1ff) & ~size_t(0x1ff)), 0);
    f[0] = 'M';
    f[1] = 'Z';
    put32(f, 0x3c, 0x80);
    put32(f, 0x80, 0x00004550);
    put16(f, 0x84, 0x14c);
    put16(f, 0x86, 1);
    put16(f, 0x94, 224);
    put16(f, 0x96, 0x2102); // a DLL
    put16(f, 0x98, 0x10b);
    put32(f, 0x98 + 28, 0x10000000); // image base
    put32(f, 0x98 + 32, 0x1000);
    put32(f, 0x98 + 36, 0x200);
    put32(f, 0x98 + 56, 0x3000);
    put32(f, 0x98 + 60, 0x200);
    put32(f, 0x98 + 92, 16);
    put32(f, 0x98 + 96 + 2 * 8, 0x1000);
    put32(f, 0x98 + 96 + 2 * 8 + 4, 0x100);
    const size_t sec = 0x98 + 224;
    std::memcpy(&f[sec], ".rsrc", 5);
    put32(f, sec + 8, uint32_t(f.size() - 0x200));
    put32(f, sec + 12, 0x1000);
    put32(f, sec + 16, uint32_t(f.size() - 0x200));
    put32(f, sec + 20, 0x200);
    // type 5 -> id 100 -> language 1033 -> the template, at RVA 0x1100.
    const size_t r = 0x200;
    put16(f, r + 14, 1);
    put32(f, r + 16, 5);
    put32(f, r + 20, 0x80000000 | 0x18);
    put16(f, r + 0x18 + 14, 1);
    put32(f, r + 0x18 + 16, 100);
    put32(f, r + 0x18 + 20, 0x80000000 | 0x30);
    put16(f, r + 0x30 + 14, 1);
    put32(f, r + 0x30 + 16, 1033);
    put32(f, r + 0x30 + 20, 0x48);
    put32(f, r + 0x48, 0x1100);
    put32(f, r + 0x48 + 4, uint32_t(tpl.size()));
    std::memcpy(&f[0x300], tpl.data(), tpl.size());
    return f;
}

struct Fixture {
    x86::Cpu cpu{16u << 20};
    x86::Win32Process process{cpu};
    uint32_t mem = 0;
    Fixture() { mem = process.memory().alloc(1024); }
    uint32_t user(const char *name, const std::vector<uint32_t> &args) {
        uint32_t r = 0;
        CHECK(!cpu.call(process.standIn("USER32.dll", name), args, r));
        return r;
    }
    uint32_t str(uint32_t offset, const std::string &s) {
        cpu.writeString(mem + offset, s, uint32_t(s.size() + 1));
        return mem + offset;
    }
};

void testDialog() {
    Fixture f;
    const x86::Win32Process::Module *dll = nullptr;
    CHECK(!f.process.load("dialogs.dll", dialogDll(), dll));
    if (!dll)
        return;
    // Modeless, no procedure of its own: its items are its children by id.
    const uint32_t dialog = f.user("CreateDialogParamA", {dll->base, 100, 0, 0, 0});
    CHECK(dialog != 0);
    const x86::Win32Process::Window *w = f.process.window(dialog);
    CHECK(w && w->title == "Network" && w->width == 150 && w->height == 81);
    const uint32_t ipx = f.user("GetDlgItem", {dialog, 10});
    const uint32_t tcp = f.user("GetDlgItem", {dialog, 11});
    const uint32_t list = f.user("GetDlgItem", {dialog, 20});
    CHECK(ipx && tcp && list && f.user("GetParent", {ipx}) == dialog);
    // Auto radio buttons: clicking one unchecks the other.
    CHECK(f.user("SendDlgItemMessageA", {dialog, 10, 0xf5, 0, 0}) == 0); // BM_CLICK
    CHECK(f.user("SendDlgItemMessageA", {dialog, 10, 0xf0, 0, 0}) == 1); // BM_GETCHECK
    f.user("SendDlgItemMessageA", {dialog, 11, 0xf5, 0, 0});
    CHECK(f.user("SendDlgItemMessageA", {dialog, 10, 0xf0, 0, 0}) == 0);
    CHECK(f.user("SendDlgItemMessageA", {dialog, 11, 0xf0, 0, 0}) == 1);
    // A list box's items.
    CHECK(f.user("SendDlgItemMessageA", {dialog, 20, 0x180, 0, f.str(0, "Session A")}) == 0); // LB_ADDSTRING
    CHECK(f.user("SendDlgItemMessageA", {dialog, 20, 0x180, 0, f.str(0, "Session B")}) == 1);
    CHECK(f.user("SendDlgItemMessageA", {dialog, 20, 0x18b, 0, 0}) == 2);           // LB_GETCOUNT
    CHECK(f.user("SendDlgItemMessageA", {dialog, 20, 0x188, 0, 0}) == 0xffffffffu); // LB_GETCURSEL
    CHECK(f.user("SendDlgItemMessageA", {dialog, 20, 0x186, 1, 0}) == 1);           // LB_SETCURSEL
    CHECK(f.user("SendDlgItemMessageA", {dialog, 20, 0x189, 1, f.mem + 512}) == 9 &&
          f.cpu.string(f.mem + 512) == "Session B");
    // A button's text through WM_GETTEXT.
    CHECK(f.user("SendDlgItemMessageA", {dialog, 11, 0x0d, 32, f.mem + 512}) == 6 &&
          f.cpu.string(f.mem + 512) == "TCP/IP");
    // EndDialog hides a modeless one.
    CHECK(f.user("EndDialog", {dialog, 1}) == 1 && !f.process.window(dialog)->visible);
}

void testWindows() {
    Fixture f;
    // A window class with no procedure of the program's: DefWindowProcA's.
    const uint32_t cls = f.mem + 256;
    for (uint32_t i = 0; i < 40; i += 4)
        f.cpu.write32(cls + i, 0);
    f.cpu.write32(cls + 4, f.process.standIn("USER32.dll", "DefWindowProcA"));
    f.cpu.write32(cls + 36, f.str(300, "Main"));
    CHECK(f.user("RegisterClassA", {cls}) != 0);
    const uint32_t h = f.user("CreateWindowExA", {0, f.mem + 300, f.str(320, "Title"), 0, 0, 0, 64, 48, 0, 0, 0, 0});
    CHECK(h != 0);
    // Longs: kept as set; the style and procedure are the window's own.
    CHECK(f.user("SetWindowLongA", {h, uint32_t(-21), 77}) == 0 && f.user("GetWindowLongA", {h, uint32_t(-21)}) == 77);
    CHECK(f.user("GetWindowLongA", {h, uint32_t(-4)}) == f.process.standIn("USER32.dll", "DefWindowProcA"));
    CHECK(f.user("SetWindowTextA", {h, f.str(340, "Loco")}) == 1);
    CHECK(f.user("GetWindowTextA", {h, f.mem + 512, 3}) == 2 && f.cpu.string(f.mem + 512) == "Lo");
    CHECK(f.user("EnableWindow", {h, 0}) == 0 && f.user("EnableWindow", {h, 1}) == 1);
    // A timer: nothing until it's due, then WM_TIMER (0x113) with its id.
    CHECK(f.user("SetTimer", {h, 5, 1000, 0}) == 5);
    CHECK(f.user("PeekMessageA", {f.mem + 600, 0, 0, 0, 1}) == 0);
    f.process.milliseconds = [] { return 5000u; };
    CHECK(f.user("PeekMessageA", {f.mem + 600, 0, 0, 0, 1}) == 1);
    CHECK(f.cpu.read32(f.mem + 604) == 0x113 && f.cpu.read32(f.mem + 608) == 5);
    CHECK(f.user("PeekMessageA", {f.mem + 600, 0, 0, 0, 1}) == 0);
    CHECK(f.user("KillTimer", {h, 5}) == 1 && f.user("KillTimer", {h, 5}) == 0);
    // Rectangles: an empty intersection is set to zeros.
    auto rect = [&](uint32_t at, int l, int t, int r, int b) {
        f.cpu.write32(at, uint32_t(l));
        f.cpu.write32(at + 4, uint32_t(t));
        f.cpu.write32(at + 8, uint32_t(r));
        f.cpu.write32(at + 12, uint32_t(b));
        return at;
    };
    const uint32_t out = f.mem + 700;
    CHECK(f.user("IntersectRect", {out, rect(f.mem + 720, 0, 0, 10, 10), rect(f.mem + 740, 5, 5, 20, 20)}) == 1);
    CHECK(f.cpu.read32(out) == 5 && f.cpu.read32(out + 8) == 10);
    CHECK(f.user("IntersectRect", {out, rect(f.mem + 720, 0, 0, 4, 4), rect(f.mem + 740, 5, 5, 20, 20)}) == 0);
    CHECK(f.cpu.read32(out + 8) == 0);
    CHECK(f.user("PtInRect", {rect(f.mem + 720, 0, 0, 4, 4), 3, 3}) == 1);
    CHECK(f.user("PtInRect", {f.mem + 720, 4, 3}) == 0);
}

// Windows' places, the window at a point, where the mouse's and keys'
// messages go, and what's painted again when a window goes.
void testPlaces(const std::filesystem::path &tmp) {
    Fixture f;
    const uint32_t cls = f.mem + 256;
    for (uint32_t i = 0; i < 40; i += 4)
        f.cpu.write32(cls + i, 0);
    f.cpu.write32(cls + 4, f.process.standIn("USER32.dll", "DefWindowProcA"));
    f.cpu.write32(cls + 36, f.str(300, "Main"));
    CHECK(f.user("RegisterClassA", {cls}) != 0);
    // The messages queued, as window and message pairs, taken off the queue.
    auto taken = [&] {
        std::vector<std::pair<uint32_t, uint32_t>> out;
        while (f.user("PeekMessageA", {f.mem + 600, 0, 0, 0, 1}))
            out.emplace_back(f.cpu.read32(f.mem + 600), f.cpu.read32(f.mem + 604));
        return out;
    };
    // Made maximized (WS_POPUP | WS_VISIBLE | WS_MAXIMIZE), the place given
    // is ignored: it fills the screen. Shown, it's to be painted.
    const uint32_t top = f.user("CreateWindowExA", {0, f.mem + 300, 0, 0x91000000, 12345, 678, 9, 9, 0, 0, 0, 0});
    const auto a = f.process.windowArea(top);
    CHECK(a && (*a)[0] == 0 && (*a)[1] == 0 && (*a)[2] == 800 && (*a)[3] == 600);
    CHECK((taken() == std::vector<std::pair<uint32_t, uint32_t>>{{top, 0xf}}));
    // A child's place is in its parent's client area, and so its child's.
    const uint32_t child = f.user("CreateWindowExA", {0, f.mem + 300, 0, 0x50000000, 100, 50, 200, 100, top, 0, 0, 0});
    const uint32_t inner = f.user("CreateWindowExA", {0, f.mem + 300, 0, 0x50000000, 10, 10, 20, 20, child, 0, 0, 0});
    taken();
    CHECK(f.user("GetWindowRect", {child, f.mem + 700}) == 1 && f.cpu.read32(f.mem + 700) == 100 &&
          f.cpu.read32(f.mem + 712) == 150);
    f.cpu.write32(f.mem + 720, 1);
    f.cpu.write32(f.mem + 724, 2);
    CHECK(f.user("ClientToScreen", {inner, f.mem + 720}) == 1 && f.cpu.read32(f.mem + 720) == 111 &&
          f.cpu.read32(f.mem + 724) == 62);
    // WindowFromPoint: the deepest window shown and enabled there.
    CHECK(f.user("WindowFromPoint", {115, 65}) == inner);
    CHECK(f.user("WindowFromPoint", {150, 140}) == child);
    CHECK(f.user("WindowFromPoint", {5, 5}) == top);
    // The pointer's messages go to the window under it, in its client area.
    f.process.mouseMoveTo(150, 140);
    CHECK(f.user("PeekMessageA", {f.mem + 600, 0, 0, 0, 1}) == 1 && f.cpu.read32(f.mem + 600) == child &&
          f.cpu.read32(f.mem + 604) == 0x200 && f.cpu.read32(f.mem + 612) == (90u << 16 | 50));
    // Captured, they go to the window that holds it, wherever the pointer is.
    f.user("SetCapture", {top});
    f.process.mouseButton(0, true);
    CHECK(f.user("PeekMessageA", {f.mem + 600, 0, 0, 0, 1}) == 1 && f.cpu.read32(f.mem + 600) == top &&
          f.cpu.read32(f.mem + 604) == 0x201 && f.cpu.read32(f.mem + 612) == (140u << 16 | 150));
    f.user("ReleaseCapture", {});
    f.process.mouseButton(0, false);
    // A disabled child isn't found there: its parent is.
    f.user("EnableWindow", {child, 0});
    CHECK(f.user("WindowFromPoint", {150, 140}) == top);
    taken();
    // Keys go to the window with the focus.
    f.user("SetFocus", {inner});
    f.process.key(0x41, 0x1e, true);
    CHECK((taken() == std::vector<std::pair<uint32_t, uint32_t>>{{inner, 0x100}}));
    // DefWindowProcA's hit-test: a captioned window's client area, its
    // caption, its border, and outside it.
    const uint32_t framed = f.user("CreateWindowExA", {0, f.mem + 300, 0, 0x10cf0000, 400, 300, 200, 100, 0, 0, 0, 0});
    taken();
    auto hit = [&](int x, int y) {
        return f.user("DefWindowProcA", {framed, 0x84, 0, uint32_t(y) << 16 | uint32_t(x)});
    };
    CHECK(hit(450, 350) == 1 && hit(450, 310) == 2 && hit(401, 350) == 18 && hit(399, 350) == 0);
    // The pointer on its caption: WM_NCMOUSEMOVE, with the hit-test code and the point on the screen.
    f.process.mouseMoveTo(450, 310);
    CHECK(f.user("PeekMessageA", {f.mem + 600, 0, 0, 0, 1}) == 1 && f.cpu.read32(f.mem + 604) == 0xa0 &&
          f.cpu.read32(f.mem + 608) == 2 && f.cpu.read32(f.mem + 612) == (310u << 16 | 450));
    taken();
    // Destroyed, a window takes its children with it, and what it covered is painted again.
    CHECK(f.user("DestroyWindow", {child}) == 1 && !f.process.window(inner));
    CHECK((taken() == std::vector<std::pair<uint32_t, uint32_t>>{{top, 0xf}}));
    // Moved and sized: WM_MOVE (the client area's corner) and WM_SIZE (its
    // size) sent, then it's painted. The messages sent come to a procedure
    // of the test's.
    std::vector<std::pair<uint32_t, uint32_t>> sent;
    f.process.provideStdcall("TEST", "Proc", 4, [&](x86::Cpu &c) {
        sent.emplace_back(c.arg(1), c.arg(3));
        return c.arg(1) == 0x81 ? 1u : 0u;
    });
    const uint32_t testCls = f.mem + 800;
    for (uint32_t i = 0; i < 40; i += 4)
        f.cpu.write32(testCls + i, 0);
    f.cpu.write32(testCls + 4, f.process.standIn("TEST", "Proc"));
    f.cpu.write32(testCls + 36, f.str(860, "Watched"));
    CHECK(f.user("RegisterClassA", {testCls}) != 0);
    const uint32_t watched = f.user("CreateWindowExA", {0, f.mem + 860, 0, 0x90000000, 10, 10, 50, 40, top, 0, 0, 0});
    taken();
    sent.clear();
    CHECK(f.user("SetWindowPos", {watched, 0, 20, 30, 60, 40, 0}) == 1);
    CHECK((sent == std::vector<std::pair<uint32_t, uint32_t>>{{0x3, 30u << 16 | 20}, {0x5, 40u << 16 | 60}}));
    CHECK((taken() == std::vector<std::pair<uint32_t, uint32_t>>{{top, 0xf}, {watched, 0xf}}));
    // A timer of the popup's, peeked without removing: still due, nothing queued.
    CHECK(f.user("SetTimer", {watched, 7, 100, 0}) == 7);
    f.process.milliseconds = [] { return 1000000u; };
    CHECK(f.user("PeekMessageA", {f.mem + 600, 0, 0, 0, 0}) == 1 && f.cpu.read32(f.mem + 604) == 0x113);
    CHECK(f.user("PeekMessageA", {f.mem + 600, 0, 0, 0, 1}) == 1 && f.cpu.read32(f.mem + 604) == 0x113);
    CHECK(f.user("PeekMessageA", {f.mem + 600, 0, 0, 0, 1}) == 0);
    // Its owner closed, the popup goes too, and its timer with it.
    f.process.milliseconds = [] { return 2000000u; };
    CHECK(f.user("DefWindowProcA", {top, 0x10, 0, 0}) == 0 && !f.process.window(watched) && !f.process.window(top));
    taken();
    CHECK(f.user("PeekMessageA", {f.mem + 600, 0, 0, 0, 1}) == 0);
    // A cursor from a file: a handle while the file's there, else none.
    std::ofstream(tmp / "busy.ani") << "RIFF";
    f.process.mapPath = [&tmp](const std::string &path, bool) -> std::optional<std::filesystem::path> {
        return tmp / (path == "C:\\busy.ani" ? "busy.ani" : "none.ani");
    };
    const uint32_t cursor = f.user("LoadCursorFromFileA", {f.str(900, "C:\\busy.ani")});
    CHECK(cursor != 0 && f.user("LoadCursorFromFileA", {f.str(900, "C:\\none.ani")}) == 0);
    // The one before: the arrow, as Windows starts with.
    const uint32_t arrow = f.user("LoadCursorA", {0, 32512});
    CHECK(f.user("SetCursor", {cursor}) == arrow && f.user("GetCursor", {}) == cursor);
}

void testWsprintf() {
    Fixture f;
    // wsprintfA is cdecl: the caller takes its arguments off.
    uint32_t r = 0;
    CHECK(!f.cpu.call(f.process.standIn("USER32.dll", "wsprintfA"),
                      {f.mem + 512, f.str(0, "%s%03d.ind"), f.str(32, "C:\\art-res\\"), 7}, r));
    CHECK(r == 18 && f.cpu.string(f.mem + 512) == "C:\\art-res\\007.ind");
}

} // namespace

static const reluva::test::Suite kSuite("common.unit.windows_dialogs",
                                        {
                                            {"dialog", [](const std::filesystem::path &) { testDialog(); }},
                                            {"windows", [](const std::filesystem::path &) { testWindows(); }},
                                            {"places", [](const std::filesystem::path &tmp) { testPlaces(tmp); }},
                                            {"wsprintf", [](const std::filesystem::path &) { testWsprintf(); }},
                                        });
