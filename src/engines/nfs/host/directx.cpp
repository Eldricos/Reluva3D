// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// DirectDraw, DirectInput and DirectSound as the game and its DLLs use
// them: minimal COM objects (Microsoft's published interfaces, their
// methods in vtable order) whose methods are native functions.
//
// - DirectDraw: the display, and surfaces in emulated memory that the
//   renderer DLL locks and draws into; a flip of the primary surface shows
//   a frame (onFrame).
// - DirectInput: the keyboard and mouse.
// - DirectSound: buffers the sound DLL fills, played in time (their
//   positions move as they would) but not heard yet.

#include "engines/nfs/host/game_host.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace reluva::nfs {

namespace {

using x86::Cpu;

constexpr uint32_t kOk = 0, kNoInterface = 0x80004002u, kInvalidParams = 0x80070057u;
// DDSCAPS bits.
constexpr uint32_t kBackBuffer = 0x4, kFlip = 0x10, kPrimary = 0x200;

std::string guid(const Cpu& c, uint32_t p) {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%08x-%04x-%04x", c.read32(p), c.read16(p + 4), c.read16(p + 6));
    return buf;
}

// A DDSURFACEDESC (108 bytes): size, flags, height, width, pitch, the
// surface's memory at 36, the pixel format at 72, the caps at 104.
void writeDesc(Cpu& c, uint32_t p, int w, int h, int bits, int pitch, uint32_t memory, uint32_t caps) {
    for (uint32_t i = 0; i < 108; i += 4)
        c.write32(p + i, 0);
    c.write32(p, 108);
    c.write32(p + 4, 0x1 | 0x2 | 0x4 | 0x8 | 0x1000); // caps, height, width, pitch, pixel format
    c.write32(p + 8, uint32_t(h));
    c.write32(p + 12, uint32_t(w));
    c.write32(p + 16, uint32_t(pitch));
    c.write32(p + 36, memory);
    c.write32(p + 72, 32);
    c.write32(p + 76, bits == 8 ? 0x20 | 0x40 : 0x40); // DDPF_PALETTEINDEXED8, DDPF_RGB
    c.write32(p + 84, uint32_t(bits));
    if (bits == 16) {
        c.write32(p + 88, 0xf800);
        c.write32(p + 92, 0x07e0);
        c.write32(p + 96, 0x001f);
    } else if (bits == 32) {
        c.write32(p + 88, 0xff0000);
        c.write32(p + 92, 0x00ff00);
        c.write32(p + 96, 0x0000ff);
    }
    c.write32(p + 104, caps);
}


// The joystick offered (GameHost::Options::joystick), as DirectInput
// describes a pad's objects in DIJOYSTATE (c_dfDIJoystick, the data format
// the game sets): five axes (X and Y the left stick, Z the triggers, Rx
// and Ry the right stick), a hat (the d-pad) and twelve buttons.
constexpr uint32_t kJoystickInstance = 0x52e1a000, kJoystickProduct = 0x52e1a001; // Reluva's own
constexpr int kJoystickAxes = 5, kJoystickButtons = 12;
constexpr uint32_t kPovOffset = 32, kButtonsOffset = 48, kJoyStateSize = 80;
// DirectInput's GUID_XAxis... (all A36D02xx-C9F3-11CF-BFC7-444553540000).
constexpr uint32_t kAxisGuids[kJoystickAxes] = {0xa36d02e0, 0xa36d02e1, 0xa36d02e2, 0xa36d02f4, 0xa36d02f5};
constexpr uint32_t kButtonGuid = 0xa36d02f0, kPovGuid = 0xa36d02f2;
// DI buttons from SDL's pad: A B X Y, the shoulders, Back, Start, the sticks, Guide.
constexpr GamepadButton kButtonOrder[kJoystickButtons - 1] = {PadSouth,         PadEast, PadWest,  PadNorth,
                                                               PadLeftShoulder, PadRightShoulder, PadBack, PadStart,
                                                               PadLeftStick,    PadRightStick,    PadGuide};

void writeGuid(Cpu& c, uint32_t at, uint32_t data1) {
    c.write32(at, data1);
    c.write16(at + 4, 0xc9f3);
    c.write16(at + 6, 0x11cf);
    const uint8_t rest[8] = {0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00};
    for (uint32_t i = 0; i < 8; ++i)
        c.write8(at + 8 + i, rest[i]);
}

// The object at a DIJOYSTATE offset: its GUID, DIDFT type and name; false
// if the pad has none there.
bool joystickObject(uint32_t offset, uint32_t& guid, uint32_t& type, std::string& name) {
    static const char* const axisNames[kJoystickAxes] = {"X Axis", "Y Axis", "Z Axis", "X Rotation", "Y Rotation"};
    if (offset < 4 * kJoystickAxes && offset % 4 == 0) {
        guid = kAxisGuids[offset / 4];
        type = 0x2 | (offset / 4) << 8; // DIDFT_ABSAXIS
        name = axisNames[offset / 4];
        return true;
    }
    if (offset == kPovOffset) {
        guid = kPovGuid;
        type = 0x10; // DIDFT_POV, instance 0
        name = "Hat Switch";
        return true;
    }
    if (offset >= kButtonsOffset && offset < kButtonsOffset + kJoystickButtons) {
        guid = kButtonGuid;
        type = 0x4 | (offset - kButtonsOffset) << 8; // DIDFT_PSHBUTTON
        name = "Button " + std::to_string(offset - kButtonsOffset + 1);
        return true;
    }
    return false;
}

} // namespace

uint32_t GameHost::newSurface(int width, int height, int bits, uint32_t caps) {
    DdSurface s;
    s.width = width;
    s.height = height;
    s.bits = bits;
    s.pitch = (width * bits / 8 + 3) & ~3;
    s.memory = process_->memory().alloc(uint32_t(s.pitch * height), 16);
    s.caps = caps;
    s.primary = caps & kPrimary;
    const uint32_t object = newObject(surfaceVtable_);
    surfaces_[object] = s;
    return object;
}

void GameHost::present(const DdSurface& s) {
    if (!s.memory)
        return;
    frame_.create(s.width, s.height, PixelFormat::RGBA32);
    const std::array<uint32_t, 256>* palette = nullptr;
    if (auto it = palettes_.find(s.palette); it != palettes_.end())
        palette = &it->second;
    for (int y = 0; y < s.height; ++y) {
        const uint8_t* src = cpu_->memory().data() + s.memory + size_t(y) * size_t(s.pitch);
        uint8_t* dst = frame_.row(y);
        for (int x = 0; x < s.width; ++x, dst += 4) {
            if (s.bits == 16) {
                const uint16_t v = uint16_t(src[2 * x] | src[2 * x + 1] << 8);
                dst[0] = uint8_t(((v >> 11) & 31) * 255 / 31);
                dst[1] = uint8_t(((v >> 5) & 63) * 255 / 63);
                dst[2] = uint8_t((v & 31) * 255 / 31);
            } else if (s.bits == 32) {
                dst[0] = src[4 * x + 2];
                dst[1] = src[4 * x + 1];
                dst[2] = src[4 * x];
            } else {
                const uint32_t rgba = palette ? (*palette)[src[x]] : uint32_t(src[x]) * 0x010101u;
                dst[0] = uint8_t(rgba);
                dst[1] = uint8_t(rgba >> 8);
                dst[2] = uint8_t(rgba >> 16);
            }
            dst[3] = 255;
        }
    }
    if (onFrame)
        onFrame(frame_);
}

void GameHost::addDirectX() {
    // DirectDrawEnumerateA(callback, context): the one display.
    const uint32_t description = process_->memory().alloc(32), driver = process_->memory().alloc(16);
    cpu_->writeString(description, "Primary Display Driver", 32);
    cpu_->writeString(driver, "display", 16);
    provide("DDRAW.dll", "DirectDrawEnumerateA", 2, [description, driver](Cpu& c) {
        uint32_t result = 0;
        if (Error e = c.call(c.arg(0), {0, description, driver, c.arg(1)}, result))
            c.abort(e.message);
        return kOk;
    });

    // QueryInterface, AddRef, Release; QueryInterface giving the object
    // itself unless `refuse` says no to the interface.
    auto unknown = [this](const std::string& name, std::function<bool(uint32_t data1)> refuse = {}) {
        return std::vector<Method>{
            {"QueryInterface", 3,
             [this, name, refuse](Cpu& c) {
                 const uint32_t data1 = c.read32(c.arg(1));
                 if (refuse && refuse(data1)) {
                     say("  " + name + "::QueryInterface(" + guid(c, c.arg(1)) + "): no");
                     c.write32(c.arg(2), 0);
                     return kNoInterface;
                 }
                 c.write32(c.arg(2), c.arg(0));
                 return kOk;
             }},
            {"AddRef", 1, [](Cpu&) { return 2u; }},
            {"Release", 1, [](Cpu&) { return 0u; }},
        };
    };
    auto ok = [](Cpu&) { return kOk; };

    // Surfaces.
    {
        std::vector<Method> m = unknown("IDirectDrawSurface3");
        auto add = [&](const char* n, int args, std::function<uint32_t(Cpu&)> fn = {}) { m.push_back({n, args, fn}); };
        auto surface = [this](Cpu& c, uint32_t object) -> DdSurface* {
            auto it = surfaces_.find(object);
            if (it == surfaces_.end()) {
                c.abort("not a surface: " + std::to_string(object));
                return nullptr;
            }
            return &it->second;
        };
        // A rectangle argument, or the whole surface.
        auto rectOf = [](Cpu& c, uint32_t p, const DdSurface& s, int& l, int& t, int& r, int& b) {
            if (p) {
                l = int32_t(c.read32(p));
                t = int32_t(c.read32(p + 4));
                r = int32_t(c.read32(p + 8));
                b = int32_t(c.read32(p + 12));
            } else {
                l = t = 0;
                r = s.width;
                b = s.height;
            }
        };
        // Copies src's rectangle into dst's, scaled to fit, skipping the
        // source's colour key if asked.
        auto blit = [this](const DdSurface& dst, int dl, int dt, int dr, int db, const DdSurface& src, int sl, int st,
                           int sr, int sb, bool keyed) {
            const int bpp = dst.bits / 8;
            if (src.bits != dst.bits || dr <= dl || db <= dt || sr <= sl || sb <= st)
                return;
            uint8_t* mem = cpu_->memory().data();
            for (int y = std::max(dt, 0); y < std::min(db, dst.height); ++y) {
                const int sy = st + (y - dt) * (sb - st) / (db - dt);
                if (sy < 0 || sy >= src.height)
                    continue;
                for (int x = std::max(dl, 0); x < std::min(dr, dst.width); ++x) {
                    const int sx = sl + (x - dl) * (sr - sl) / (dr - dl);
                    if (sx < 0 || sx >= src.width)
                        continue;
                    const uint8_t* from = mem + src.memory + size_t(sy) * size_t(src.pitch) + size_t(sx * bpp);
                    uint32_t v = 0;
                    std::memcpy(&v, from, size_t(bpp));
                    if (keyed && v == src.colorKey)
                        continue;
                    std::memcpy(mem + dst.memory + size_t(y) * size_t(dst.pitch) + size_t(x * bpp), from, size_t(bpp));
                }
            }
        };
        auto fill = [this](const DdSurface& dst, int l, int t, int r, int b, uint32_t colour) {
            const int bpp = dst.bits / 8;
            uint8_t* mem = cpu_->memory().data();
            for (int y = std::max(t, 0); y < std::min(b, dst.height); ++y)
                for (int x = std::max(l, 0); x < std::min(r, dst.width); ++x)
                    std::memcpy(mem + dst.memory + size_t(y) * size_t(dst.pitch) + size_t(x * bpp), &colour,
                                size_t(bpp));
        };
        add("AddAttachedSurface", 2, [this, surface](Cpu& c) {
            if (DdSurface* s = surface(c, c.arg(0)))
                s->back = c.arg(1);
            return kOk;
        });
        add("AddOverlayDirtyRect", 2, ok);
        // Blt(this, destRect*, source, sourceRect*, flags, fx*): DDBLT_COLORFILL
        // (0x400) fills with fx's colour (+80); DDBLT_KEYSRC (0x8000) skips
        // the source's colour key.
        add("Blt", 6, [this, surface, rectOf, blit, fill](Cpu& c) {
            DdSurface* d = surface(c, c.arg(0));
            if (!d)
                return kOk;
            int l, t, r, b;
            rectOf(c, c.arg(1), *d, l, t, r, b);
            if (c.arg(4) & 0x400) {
                fill(*d, l, t, r, b, c.read32(c.arg(5) + 80));
            } else if (c.arg(2)) {
                DdSurface* s = surface(c, c.arg(2));
                if (!s)
                    return kOk;
                int sl, st, sr, sb;
                rectOf(c, c.arg(3), *s, sl, st, sr, sb);
                blit(*d, l, t, r, b, *s, sl, st, sr, sb, (c.arg(4) & 0x8000) && s->hasColorKey);
            }
            if (d->primary)
                present(*d);
            return kOk;
        });
        add("BltBatch", 4, ok);
        // BltFast(this, x, y, source, sourceRect*, flags): DDBLTFAST_SRCCOLORKEY (1).
        add("BltFast", 6, [this, surface, rectOf, blit](Cpu& c) {
            DdSurface* d = surface(c, c.arg(0));
            DdSurface* s = surface(c, c.arg(3));
            if (!d || !s)
                return kOk;
            int sl, st, sr, sb;
            rectOf(c, c.arg(4), *s, sl, st, sr, sb);
            const int x = int32_t(c.arg(1)), y = int32_t(c.arg(2));
            blit(*d, x, y, x + sr - sl, y + sb - st, *s, sl, st, sr, sb, (c.arg(5) & 1) && s->hasColorKey);
            if (d->primary)
                present(*d);
            return kOk;
        });
        add("DeleteAttachedSurface", 3, ok);
        add("EnumAttachedSurfaces", 3, ok);
        add("EnumOverlayZOrders", 4, ok);
        // Flip(this, target, flags): the back buffer's memory and the front's
        // change places, and the frame is shown.
        add("Flip", 3, [this, surface](Cpu& c) {
            DdSurface* front = surface(c, c.arg(0));
            if (!front)
                return kOk;
            if (front->back)
                if (DdSurface* back = surface(c, front->back))
                    std::swap(front->memory, back->memory);
            present(*front);
            return kOk;
        });
        add("GetAttachedSurface", 3, [this, surface](Cpu& c) {
            DdSurface* s = surface(c, c.arg(0));
            if (!s || !s->back) {
                c.write32(c.arg(2), 0);
                return 0x887600ffu; // DDERR_NOTFOUND
            }
            c.write32(c.arg(2), s->back);
            return kOk;
        });
        add("GetBltStatus", 2, ok);
        add("GetCaps", 2, [this, surface](Cpu& c) {
            if (DdSurface* s = surface(c, c.arg(0)))
                c.write32(c.arg(1), s->caps);
            return kOk;
        });
        add("GetClipper", 2);
        add("GetColorKey", 3, [this, surface](Cpu& c) {
            if (DdSurface* s = surface(c, c.arg(0))) {
                c.write32(c.arg(2), s->colorKey);
                c.write32(c.arg(2) + 4, s->colorKey);
            }
            return kOk;
        });
        add("GetDC", 2, [](Cpu&) { return 0x88760000u + 9; }); // no GDI drawing
        add("GetFlipStatus", 2, ok);
        add("GetOverlayPosition", 3);
        add("GetPalette", 2, [this, surface](Cpu& c) {
            DdSurface* s = surface(c, c.arg(0));
            c.write32(c.arg(1), s ? s->palette : 0);
            return s && s->palette ? kOk : 0x887600f1u; // DDERR_NOPALETTEATTACHED
        });
        add("GetPixelFormat", 2, [this, surface](Cpu& c) {
            DdSurface* s = surface(c, c.arg(0));
            if (!s)
                return kOk;
            const uint32_t scratch = process_->memory().alloc(108);
            writeDesc(c, scratch, s->width, s->height, s->bits, s->pitch, 0, 0);
            std::memcpy(c.memory().data() + c.arg(1), c.memory().data() + scratch + 72, 32);
            process_->memory().free(scratch);
            return kOk;
        });
        add("GetSurfaceDesc", 2, [this, surface](Cpu& c) {
            if (DdSurface* s = surface(c, c.arg(0)))
                writeDesc(c, c.arg(1), s->width, s->height, s->bits, s->pitch, 0, s->caps);
            return kOk;
        });
        add("Initialize", 3, ok);
        add("IsLost", 1, ok);
        // Lock(this, rect*, desc*, flags, event): the memory, at the rectangle.
        add("Lock", 5, [this, surface](Cpu& c) {
            DdSurface* s = surface(c, c.arg(0));
            if (!s)
                return kInvalidParams;
            uint32_t memory = s->memory;
            if (c.arg(1))
                memory += c.read32(c.arg(1) + 4) * uint32_t(s->pitch) + c.read32(c.arg(1)) * uint32_t(s->bits / 8);
            writeDesc(c, c.arg(2), s->width, s->height, s->bits, s->pitch, memory, s->caps);
            return kOk;
        });
        add("ReleaseDC", 2, ok);
        add("Restore", 1, ok);
        add("SetClipper", 2, ok);
        add("SetColorKey", 3, [this, surface](Cpu& c) {
            if (DdSurface* s = surface(c, c.arg(0))) {
                s->hasColorKey = c.arg(2) != 0;
                s->colorKey = c.arg(2) ? c.read32(c.arg(2)) : 0;
            }
            return kOk;
        });
        add("SetOverlayPosition", 3, ok);
        add("SetPalette", 2, [this, surface](Cpu& c) {
            if (DdSurface* s = surface(c, c.arg(0)))
                s->palette = c.arg(1);
            return kOk;
        });
        // Unlock: drawing straight onto the primary surface shows at once.
        add("Unlock", 2, [this, surface](Cpu& c) {
            if (DdSurface* s = surface(c, c.arg(0)))
                if (s->primary && !s->back)
                    present(*s);
            return kOk;
        });
        add("UpdateOverlay", 6, ok);
        add("UpdateOverlayDisplay", 2, ok);
        add("UpdateOverlayZOrder", 3, ok);
        add("GetDDInterface", 2);
        add("PageLock", 2, ok);
        add("PageUnlock", 2, ok);
        add("SetSurfaceDesc", 3, ok);
        surfaceVtable_ = makeVtable("IDirectDrawSurface3", m);
    }

    // Palettes: 256 entries of PALETTEENTRY (red, green, blue, flags).
    {
        std::vector<Method> m = unknown("IDirectDrawPalette");
        m.push_back({"GetCaps", 2, [](Cpu& c) {
                         c.write32(c.arg(1), 0x4); // DDPCAPS_8BIT
                         return kOk;
                     }});
        m.push_back({"GetEntries", 5, [this](Cpu& c) {
                         auto& p = palettes_[c.arg(0)];
                         for (uint32_t i = 0; i < c.arg(3) && c.arg(2) + i < 256; ++i)
                             c.write32(c.arg(4) + 4 * i, p[c.arg(2) + i] & 0xffffff);
                         return kOk;
                     }});
        m.push_back({"Initialize", 4, ok});
        m.push_back({"SetEntries", 5, [this](Cpu& c) {
                         auto& p = palettes_[c.arg(0)];
                         for (uint32_t i = 0; i < c.arg(3) && c.arg(2) + i < 256; ++i)
                             p[c.arg(2) + i] = c.read32(c.arg(4) + 4 * i) | 0xff000000u;
                         return kOk;
                     }});
        paletteVtable_ = makeVtable("IDirectDrawPalette", m);
    }
    {
        std::vector<Method> m = unknown("IDirectDrawClipper");
        for (auto [n, args] : {std::pair{"GetClipList", 4}, {"GetHWnd", 2}, {"Initialize", 3}, {"IsClipListChanged", 2},
                               {"SetClipList", 3}, {"SetHWnd", 3}})
            m.push_back({n, args, ok});
        clipperVtable_ = makeVtable("IDirectDrawClipper", m);
    }

    // IDirectDraw, its methods as IDirectDraw2's (SetDisplayMode takes 5
    // arguments there, and the game asks for that interface). Direct3D
    // isn't there: the software renderer is used.
    std::vector<Method> dd = unknown("IDirectDraw2", [](uint32_t data1) {
        return data1 == 0x3bba0080 || data1 == 0x6aae1ec1 || data1 == 0xbb223240; // IDirect3D, 2, 3
    });
    auto add = [&](const char* n, int args, std::function<uint32_t(Cpu&)> fn = {}) { dd.push_back({n, args, fn}); };
    add("Compact", 1, ok);
    add("CreateClipper", 4, [this](Cpu& c) {
        c.write32(c.arg(2), newObject(clipperVtable_));
        return kOk;
    });
    add("CreatePalette", 5, [this](Cpu& c) {
        const uint32_t object = newObject(paletteVtable_);
        auto& p = palettes_[object];
        for (uint32_t i = 0; i < 256; ++i)
            p[i] = c.arg(2) ? c.read32(c.arg(2) + 4 * i) | 0xff000000u : 0xff000000u;
        c.write32(c.arg(3), object);
        return kOk;
    });
    // CreateSurface(this, desc*, surface**, outer): a primary surface (with
    // its back buffers if it flips) at the display mode's size, or one of
    // the size asked for.
    add("CreateSurface", 4, [this](Cpu& c) {
        const uint32_t desc = c.arg(1), flags = c.read32(desc + 4);
        const uint32_t caps = flags & 1 ? c.read32(desc + 104) : 0;
        int w = modeWidth_, h = modeHeight_, bits = modeBits_;
        if (!(caps & kPrimary)) {
            if (flags & 4)
                w = int(c.read32(desc + 12));
            if (flags & 2)
                h = int(c.read32(desc + 8));
            if (flags & 0x1000)
                bits = int(c.read32(desc + 84));
        }
        w = std::clamp(w, 1, 2048);
        h = std::clamp(h, 1, 2048);
        if (bits != 8 && bits != 16 && bits != 32)
            bits = 16;
        const uint32_t object = newSurface(w, h, bits, caps);
        if ((caps & kPrimary) && (flags & 0x20) && c.read32(desc + 20) > 0) // DDSD_BACKBUFFERCOUNT
            surfaces_[object].back = newSurface(w, h, bits, kBackBuffer | kFlip);
        c.write32(c.arg(2), object);
        return kOk;
    });
    add("DuplicateSurface", 3);
    add("EnumDisplayModes", 5, [this](Cpu& c) {
        const uint32_t desc = process_->memory().alloc(108);
        for (int bits : {8, 16})
            for (auto [w, h] : {std::pair{320, 200}, {512, 384}, {640, 480}, {800, 600}, {1024, 768}}) {
                writeDesc(c, desc, w, h, bits, w * bits / 8, 0, 0);
                uint32_t more = 0;
                if (Error e = c.call(c.arg(4), {desc, c.arg(3)}, more)) {
                    c.abort(e.message);
                    return kOk;
                }
                if (!more)
                    return kOk;
            }
        return kOk;
    });
    add("EnumSurfaces", 5, ok);
    add("FlipToGDISurface", 1, ok);
    add("GetCaps", 3, [](Cpu& c) {
        // DDCAPS: its size kept, the rest nothing but video memory (60, 64).
        for (int i = 1; i <= 2; ++i)
            if (c.arg(i)) {
                const uint32_t size = c.read32(c.arg(i));
                for (uint32_t o = 4; o < size && o < 400; o += 4)
                    c.write32(c.arg(i) + o, 0);
                c.write32(c.arg(i) + 60, 8u << 20);
                c.write32(c.arg(i) + 64, 8u << 20);
            }
        return kOk;
    });
    add("GetDisplayMode", 2, [this](Cpu& c) {
        writeDesc(c, c.arg(1), modeWidth_, modeHeight_, modeBits_, modeWidth_ * modeBits_ / 8, 0, 0);
        return kOk;
    });
    add("GetFourCCCodes", 3, [](Cpu& c) {
        c.write32(c.arg(1), 0);
        return kOk;
    });
    add("GetGDISurface", 2);
    add("GetMonitorFrequency", 2, [](Cpu& c) {
        c.write32(c.arg(1), 60);
        return kOk;
    });
    add("GetScanLine", 2);
    add("GetVerticalBlankStatus", 2, [](Cpu& c) {
        c.write32(c.arg(1), 1);
        return kOk;
    });
    add("Initialize", 2, ok);
    add("RestoreDisplayMode", 1, ok);
    add("SetCooperativeLevel", 3, ok);
    add("SetDisplayMode", 6, [this](Cpu& c) {
        modeWidth_ = int(c.arg(1));
        modeHeight_ = int(c.arg(2));
        modeBits_ = int(c.arg(3));
        say("  display mode " + std::to_string(modeWidth_) + "x" + std::to_string(modeHeight_) + "x" +
            std::to_string(modeBits_));
        return kOk;
    });
    add("WaitForVerticalBlank", 3, ok);
    add("GetAvailableVidMem", 4, [](Cpu& c) {
        if (c.arg(2))
            c.write32(c.arg(2), 8u << 20);
        if (c.arg(3))
            c.write32(c.arg(3), 8u << 20);
        return kOk;
    });
    const uint32_t ddrawVtable = makeVtable("IDirectDraw2", dd);
    provide("DDRAW.dll", "DirectDrawCreate", 3, [this, ddrawVtable](Cpu& c) {
        c.write32(c.arg(1), newObject(ddrawVtable));
        return kOk;
    });

    // DirectInput: the keyboard and mouse, by their GUIDs.
    auto makeDevice = [this, unknown, ok](const std::string& kind) {
        std::vector<Method> m = unknown("IDirectInputDevice(" + kind + ")");
        auto add = [&](const char* n, int args, std::function<uint32_t(Cpu&)> fn = {}) { m.push_back({n, args, fn}); };
        // DIDEVCAPS: attached, a gamepad, its axes, buttons and hats.
        add("GetCapabilities", 2, [kind](Cpu& c) {
            const uint32_t p = c.arg(1), size = c.read32(p);
            for (uint32_t o = 4; o + 4 <= size && o < 44; o += 4)
                c.write32(p + o, 0);
            c.write32(p + 4, 1); // DIDC_ATTACHED
            if (kind == "joystick") {
                c.write32(p + 8, 4 | 5 << 8); // DIDEVTYPE_JOYSTICK, a gamepad
                c.write32(p + 12, kJoystickAxes);
                c.write32(p + 16, kJoystickButtons);
                c.write32(p + 20, 1);
            }
            return kOk;
        });
        add("EnumObjects", 4, ok);
        // Properties (DIPROPHEADER: size, header size, object, how): an
        // axis's range (DIPROP_RANGE, 4), by offset (how 1) or for all
        // (how 0); the rest taken and ignored.
        auto axes = [](Cpu& c) {
            const uint32_t header = c.arg(2), object = c.read32(header + 8), how = c.read32(header + 12);
            std::vector<size_t> out;
            for (uint32_t a = 0; a < kJoystickAxes; ++a)
                if (how == 0 || (how == 1 && object == 4 * a) || (how == 2 && (object & 0xff00) >> 8 == a && (object & 3)))
                    out.push_back(a);
            return out;
        };
        add("GetProperty", 3, [this, kind, axes](Cpu& c) {
            if (kind != "joystick" || c.arg(1) != 4)
                return 0x80004001u; // E_NOTIMPL
            for (size_t a : axes(c)) {
                c.write32(c.arg(2) + 16, uint32_t(axisRange_[a].first));
                c.write32(c.arg(2) + 20, uint32_t(axisRange_[a].second));
            }
            return kOk;
        });
        add("SetProperty", 3, [this, kind, axes](Cpu& c) {
            if (kind == "joystick" && c.arg(1) == 4)
                for (size_t a : axes(c))
                    axisRange_[a] = {int32_t(c.read32(c.arg(2) + 16)), int32_t(c.read32(c.arg(2) + 20))};
            return kOk;
        });
        add("Acquire", 1, ok);
        add("Unacquire", 1, ok);
        // GetDeviceState: the keys' states, or the mouse's movement since
        // last asked and its buttons (DIMOUSESTATE).
        add("GetDeviceState", 3, [this, kind](Cpu& c) {
            if (kind == "joystick") {
                joystickState(c, c.arg(2), c.arg(1));
                return kOk;
            }
            for (uint32_t i = 0; i < c.arg(1); ++i)
                c.write8(c.arg(2) + i, kind == "keyboard" && i < 256 ? keys_[i] : 0);
            if (kind == "mouse" && c.arg(1) >= 16) {
                c.write32(c.arg(2), uint32_t(mouseDx_));
                c.write32(c.arg(2) + 4, uint32_t(mouseDy_));
                c.write8(c.arg(2) + 12, mouseButton_ ? 0x80 : 0);
                mouseDx_ = mouseDy_ = 0;
            }
            return kOk;
        });
        // GetDeviceData(this, objectSize, data*, count*, flags): the keyboard's
        // presses and releases since last asked (DIDEVICEOBJECTDATA: scan
        // code, 0x80 or 0, time, sequence); DIGDD_PEEK (1) leaves them.
        add("GetDeviceData", 5, [this, kind](Cpu& c) {
            const uint32_t room = c.arg(3) ? c.read32(c.arg(3)) : 0, size = std::max(c.arg(1), 16u);
            uint32_t n = 0;
            if (kind == "mouse") {
                for (; n < room && n < bufferedMouse_.size(); ++n) {
                    const MouseEvent& e = bufferedMouse_[n];
                    if (c.arg(2)) {
                        const uint32_t p = c.arg(2) + n * size;
                        c.write32(p, e.offset);
                        c.write32(p + 4, e.data);
                        c.write32(p + 8, uint32_t(e.atMs));
                        c.write32(p + 12, ++inputSequence_);
                    }
                }
                if (!(c.arg(4) & 1))
                    bufferedMouse_.erase(bufferedMouse_.begin(), bufferedMouse_.begin() + n);
            } else if (kind == "keyboard") {
                for (; n < room && n < bufferedKeys_.size(); ++n) {
                    const KeyEvent& e = bufferedKeys_[n];
                    if (c.arg(2)) {
                        const uint32_t p = c.arg(2) + n * size;
                        c.write32(p, e.scan);
                        c.write32(p + 4, e.down ? 0x80 : 0);
                        c.write32(p + 8, uint32_t(e.atMs));
                        c.write32(p + 12, ++inputSequence_);
                    }
                }
                if (!(c.arg(4) & 1))
                    bufferedKeys_.erase(bufferedKeys_.begin(), bufferedKeys_.begin() + n);
            }
            if (c.arg(3))
                c.write32(c.arg(3), n);
            return kOk;
        });
        add("SetDataFormat", 2, ok);
        add("SetEventNotification", 2, [this, kind](Cpu& c) {
            (kind == "mouse" ? mouseEvent_ : keyboardEvent_) = c.arg(1);
            return kOk;
        });
        add("SetCooperativeLevel", 3, ok);
        // GetObjectInfo(this, DIDEVICEOBJECTINSTANCEA*, object, how): the
        // joystick's objects by their offset (how 1) in DIJOYSTATE.
        add("GetObjectInfo", 4, [kind](Cpu& c) {
            uint32_t guid = 0, type = 0;
            std::string name;
            if (kind != "joystick" || c.arg(3) != 1 || !joystickObject(c.arg(2), guid, type, name))
                return 0x80070002u; // DIERR_OBJECTNOTFOUND
            const uint32_t p = c.arg(1), size = c.read32(p);
            for (uint32_t o = 4; o + 4 <= size; o += 4)
                c.write32(p + o, 0);
            writeGuid(c, p + 4, guid);
            c.write32(p + 20, c.arg(2));
            c.write32(p + 24, type);
            c.write32(p + 28, (type & 2) ? 0x100 : 0); // DIDOI_ASPECTPOSITION
            c.writeString(p + 32, name, 260);
            return kOk;
        });
        add("GetDeviceInfo", 2);
        add("RunControlPanel", 3);
        add("Initialize", 4, ok);
        add("CreateEffect", 5);
        add("EnumEffects", 4, ok);
        add("GetEffectInfo", 3);
        add("GetForceFeedbackState", 2);
        add("SendForceFeedbackCommand", 2);
        add("EnumCreatedEffectObjects", 4, ok);
        add("Escape", 2);
        add("Poll", 1, ok);
        add("SendDeviceData", 5);
        return makeObject("IDirectInputDevice(" + kind + ")", m);
    };
    const uint32_t keyboard = makeDevice("keyboard"), mouse = makeDevice("mouse"), joystick = makeDevice("joystick");
    axisRange_.fill({0, 65535});
    std::vector<Method> di = unknown("IDirectInput");
    di.push_back({"CreateDevice", 4, [this, keyboard, mouse, joystick](Cpu& c) {
                      const uint32_t id = c.read32(c.arg(1));
                      if (id == kJoystickInstance && !options_.joystick.empty()) {
                          c.write32(c.arg(2), joystick);
                          return kOk;
                      }
                      if (id != 0x6f1d2b61 && id != 0x6f1d2b60) {
                          say("  DirectInput: no device " + guid(c, c.arg(1)));
                          return 0x80040154u; // REGDB_E_CLASSNOTREG
                      }
                      c.write32(c.arg(2), id == 0x6f1d2b61 ? keyboard : mouse);
                      return kOk;
                  }});
    // EnumDevices(this, type, callback, ref, flags): the joystick, if one is
    // offered, for all devices (0) or joysticks (4), as a DIDEVICEINSTANCEA.
    di.push_back({"EnumDevices", 5, [this](Cpu& c) {
                      if (options_.joystick.empty() || (c.arg(1) != 0 && c.arg(1) != 4))
                          return kOk;
                      const uint32_t d = process_->memory().alloc(0x244);
                      for (uint32_t i = 0; i < 0x244; i += 4)
                          c.write32(d + i, 0);
                      c.write32(d, 0x244);
                      writeGuid(c, d + 4, kJoystickInstance);
                      writeGuid(c, d + 20, kJoystickProduct);
                      c.write32(d + 36, 4 | 5 << 8); // DIDEVTYPE_JOYSTICK, a gamepad
                      c.writeString(d + 40, options_.joystick, 260);
                      c.writeString(d + 300, options_.joystick, 260);
                      uint32_t more = 0;
                      if (Error e = c.call(c.arg(2), {d, c.arg(3)}, more))
                          c.abort(e.message);
                      process_->memory().free(d);
                      return kOk;
                  }});
    di.push_back({"GetDeviceStatus", 2, [](Cpu&) { return 1u; }}); // DI_NOTATTACHED
    di.push_back({"RunControlPanel", 3, {}});
    di.push_back({"Initialize", 3, ok});
    const uint32_t input = makeObject("IDirectInput", di);
    provide("DINPUT.dll", "DirectInputCreateA", 4, [input](Cpu& c) {
        c.write32(c.arg(2), input);
        return kOk;
    });
}


void GameHost::joystickState(Cpu& c, uint32_t to, uint32_t size) {
    for (uint32_t i = 0; i < size; ++i)
        c.write8(to + i, 0);
    if (size < kJoyStateSize)
        return;
    const GamepadState& j = joystick_;
    // Axes: -1..1, a dead zone round the middle, over the range the game set.
    const float values[kJoystickAxes] = {j.axes[0], j.axes[1], j.axes[4] - j.axes[5], j.axes[2], j.axes[3]};
    for (int a = 0; a < kJoystickAxes; ++a) {
        float v = std::clamp(values[a], -1.0f, 1.0f);
        const float dead = 0.1f;
        v = std::fabs(v) < dead ? 0 : (v - std::copysign(dead, v)) / (1 - dead);
        const auto [low, high] = axisRange_[size_t(a)];
        c.write32(to + 4 * uint32_t(a), uint32_t(int32_t(std::lround(low + (v + 1) / 2 * double(high - low)))));
    }
    // The hat: hundredths of a degree clockwise from up, or centred.
    const bool up = j.buttons & 1u << PadUp, down = j.buttons & 1u << PadDown;
    const bool left = j.buttons & 1u << PadLeft, right = j.buttons & 1u << PadRight;
    uint32_t pov = 0xffffffffu;
    if (up || down || left || right) {
        const int dx = int(right) - int(left), dy = int(down) - int(up);
        pov = uint32_t(std::lround(std::fmod(std::atan2(double(dx), double(-dy)) * 18000 / 3.14159265358979 + 36000, 36000)));
    }
    for (uint32_t i = 0; i < 4; ++i)
        c.write32(to + kPovOffset + 4 * i, i == 0 ? pov : 0xffffffffu);
    for (int b = 0; b < kJoystickButtons - 1; ++b)
        c.write8(to + kButtonsOffset + uint32_t(b), (j.buttons & 1u << kButtonOrder[b]) ? 0x80 : 0);
}

uint32_t GameHost::DsBuffer::position(uint64_t nowMs) {
    if (!playing || !size)
        return startPosition;
    const uint64_t played = (nowMs - startMs) * uint64_t(rate) / 1000 * uint64_t(blockAlign);
    const uint64_t at = startPosition + played;
    if (!looping && at >= size) {
        playing = false;
        startPosition = 0;
        return 0;
    }
    return uint32_t(at % size) / uint32_t(blockAlign) * uint32_t(blockAlign);
}

namespace {

class SoundStream : public audio::Stream {
public:
    explicit SoundStream(GameHost& host) : host_(host) {}
    void read(int16_t* out, int frames, int rate) override { host_.mixSound(out, frames, rate); }

private:
    GameHost& host_;
};

// DirectSound's attenuation, in hundredths of a decibel (0 down to
// -10000), as a gain.
double decibels(int32_t hundredths) { return std::pow(10.0, std::min(hundredths, 0) / 2000.0); }

} // namespace

std::shared_ptr<audio::Stream> GameHost::soundStream() { return std::make_shared<SoundStream>(*this); }

void GameHost::mixSound(int16_t* out, int frames, int rate) {
    if (frames <= 0)
        return;
    std::lock_guard<std::mutex> lock(soundMutex_);
    soundMix_.assign(size_t(frames) * 2, 0);
    const uint64_t now = process_->now();
    const uint8_t* memory = cpu_->memory().data();
    for (auto& [object, b] : soundBuffers_) {
        if (b.primary || !b.size || b.blockAlign <= 0 || rate <= 0)
            continue;
        const uint32_t at = b.position(now);
        if (!b.playing)
            continue;
        const int bytes = b.bits == 8 ? 1 : 2;
        const uint32_t count = b.size / uint32_t(b.blockAlign);
        if (!count || b.channels < 1 || !cpu_->valid(b.memory, b.size))
            continue;
        // Follow the play cursor the game sees; within a tenth of a second,
        // keep to our own steady pace instead.
        const double target = double(at / uint32_t(b.blockAlign));
        double drift = std::fmod(target - b.outCursor + 1.5 * count, double(count)) - 0.5 * count;
        if (!b.outSynced || std::fabs(drift) > b.rate / 10.0) {
            b.outCursor = target;
            b.outSynced = true;
        }
        const double gain = decibels(b.volume);
        const double left = gain * (b.pan > 0 ? decibels(-b.pan) : 1.0);
        const double right = gain * (b.pan < 0 ? decibels(b.pan) : 1.0);
        const double step = double(b.rate) / double(rate);
        auto sample = [&](uint32_t frame, int channel) {
            const uint8_t* p = memory + b.memory + frame * uint32_t(b.blockAlign) + uint32_t(channel * bytes);
            return bytes == 1 ? (int(p[0]) - 128) << 8 : int(int16_t(p[0] | p[1] << 8));
        };
        for (int i = 0; i < frames; ++i) {
            if (b.outCursor >= double(count)) {
                if (!b.looping)
                    break;
                b.outCursor = std::fmod(b.outCursor, double(count));
            }
            const uint32_t f = uint32_t(b.outCursor);
            const int l = sample(f, 0), r = b.channels > 1 ? sample(f, 1) : l;
            soundMix_[size_t(i) * 2] += int32_t(l * left);
            soundMix_[size_t(i) * 2 + 1] += int32_t(r * right);
            b.outCursor += step;
        }
    }
    for (size_t i = 0; i < soundMix_.size(); ++i)
        out[i] = int16_t(std::clamp<int32_t>(soundMix_[i], -32768, 32767));
}

void GameHost::addDirectSound() {
    auto ok = [](Cpu&) { return kOk; };
    auto buffer = [this](Cpu& c) -> DsBuffer* {
        auto it = soundBuffers_.find(c.arg(0));
        if (it == soundBuffers_.end()) {
            c.abort("not a sound buffer");
            return nullptr;
        }
        return &it->second;
    };
    // A WAVEFORMATEX: tag, channels, rate, bytes a second, block align, bits.
    auto readFormat = [](Cpu& c, uint32_t p, DsBuffer& b) {
        if (!p)
            return;
        b.channels = c.read16(p + 2);
        b.rate = int(c.read32(p + 4));
        b.blockAlign = std::max<int>(c.read16(p + 12), 1);
        b.bits = c.read16(p + 14);
    };
    std::vector<Method> m = {
        {"QueryInterface", 3, [](Cpu&) { return kNoInterface; }},
        {"AddRef", 1, [](Cpu&) { return 2u; }},
        {"Release", 1, [this](Cpu& c) {
             auto it = soundBuffers_.find(c.arg(0));
             if (it != soundBuffers_.end() && !it->second.primary) {
                 process_->memory().free(it->second.memory);
                 soundBuffers_.erase(it);
             }
             return 0u;
         }},
        {"GetCaps", 2, [buffer](Cpu& c) {
             if (DsBuffer* b = buffer(c)) {
                 c.write32(c.arg(1) + 4, b->primary ? 1 : 0x8); // DSBCAPS_PRIMARYBUFFER / STATIC
                 c.write32(c.arg(1) + 8, b->size);
             }
             return kOk;
         }},
        {"GetCurrentPosition", 3, [this, buffer](Cpu& c) {
             if (DsBuffer* b = buffer(c)) {
                 const uint32_t at = b->position(process_->now());
                 if (c.arg(1))
                     c.write32(c.arg(1), at);
                 if (c.arg(2)) // the write cursor, a little ahead
                     c.write32(c.arg(2), b->size ? (at + uint32_t(b->rate / 100 * b->blockAlign)) % b->size : 0);
             }
             return kOk;
         }},
        {"GetFormat", 4, [buffer](Cpu& c) {
             DsBuffer* b = buffer(c);
             if (!b)
                 return kOk;
             if (c.arg(1) && c.arg(2) >= 16) {
                 c.write16(c.arg(1), 1);
                 c.write16(c.arg(1) + 2, uint16_t(b->channels));
                 c.write32(c.arg(1) + 4, uint32_t(b->rate));
                 c.write32(c.arg(1) + 8, uint32_t(b->rate * b->blockAlign));
                 c.write16(c.arg(1) + 12, uint16_t(b->blockAlign));
                 c.write16(c.arg(1) + 14, uint16_t(b->bits));
             }
             if (c.arg(3))
                 c.write32(c.arg(3), 16);
             return kOk;
         }},
        {"GetVolume", 2, [buffer](Cpu& c) {
             if (DsBuffer* b = buffer(c))
                 c.write32(c.arg(1), uint32_t(b->volume));
             return kOk;
         }},
        {"GetPan", 2, [buffer](Cpu& c) {
             if (DsBuffer* b = buffer(c))
                 c.write32(c.arg(1), uint32_t(b->pan));
             return kOk;
         }},
        {"GetFrequency", 2, [buffer](Cpu& c) {
             if (DsBuffer* b = buffer(c))
                 c.write32(c.arg(1), uint32_t(b->rate));
             return kOk;
         }},
        {"GetStatus", 2, [this, buffer](Cpu& c) {
             if (DsBuffer* b = buffer(c)) {
                 b->position(process_->now());
                 c.write32(c.arg(1), (b->playing ? 1u : 0u) | (b->looping && b->playing ? 4u : 0u));
             }
             return kOk;
         }},
        {"Initialize", 3, ok},
        // Lock(this, offset, bytes, part1**, size1*, part2**, size2*, flags):
        // the bytes asked for, wrapping round the end as a second part.
        {"Lock", 8, [this, buffer](Cpu& c) {
             DsBuffer* b = buffer(c);
             if (!b || !b->size)
                 return kInvalidParams;
             uint32_t offset = c.arg(1), bytes = c.arg(2);
             if (c.arg(7) & 1) // DSBLOCK_FROMWRITECURSOR
                 offset = b->position(process_->now());
             if (c.arg(7) & 2) // DSBLOCK_ENTIREBUFFER
                 bytes = b->size;
             offset %= b->size;
             bytes = std::min(bytes, b->size);
             const uint32_t first = std::min(bytes, b->size - offset);
             c.write32(c.arg(3), b->memory + offset);
             c.write32(c.arg(4), first);
             if (c.arg(5))
                 c.write32(c.arg(5), bytes > first ? b->memory : 0);
             if (c.arg(6))
                 c.write32(c.arg(6), bytes - first);
             return kOk;
         }},
        {"Play", 4, [this, buffer](Cpu& c) {
             if (DsBuffer* b = buffer(c)) {
                 b->startPosition = b->position(process_->now());
                 b->startMs = process_->now();
                 b->playing = true;
                 b->outSynced = false;
                 b->looping = c.arg(3) & 1; // DSBPLAY_LOOPING
             }
             return kOk;
         }},
        {"SetCurrentPosition", 2, [this, buffer](Cpu& c) {
             if (DsBuffer* b = buffer(c)) {
                 b->startPosition = b->size ? c.arg(1) % b->size : 0;
                 b->startMs = process_->now();
                 b->outSynced = false;
             }
             return kOk;
         }},
        {"SetFormat", 2, [this, readFormat, buffer](Cpu& c) {
             if (DsBuffer* b = buffer(c)) {
                 readFormat(c, c.arg(1), *b);
                 say("  SetFormat: " + std::to_string(b->rate) + " Hz, " + std::to_string(b->channels) + " channels, " +
                     std::to_string(b->bits) + " bits" + (b->primary ? " (primary)" : ""));
             }
             return kOk;
         }},
        {"SetVolume", 2, [buffer](Cpu& c) {
             if (DsBuffer* b = buffer(c))
                 b->volume = int32_t(c.arg(1));
             return kOk;
         }},
        {"SetPan", 2, [buffer](Cpu& c) {
             if (DsBuffer* b = buffer(c))
                 b->pan = int32_t(c.arg(1));
             return kOk;
         }},
        {"SetFrequency", 2, [this, buffer](Cpu& c) {
             if (DsBuffer* b = buffer(c)) {
                 b->startPosition = b->position(process_->now());
                 b->startMs = process_->now();
                 if (c.arg(1))
                     b->rate = int(c.arg(1));
             }
             return kOk;
         }},
        {"Stop", 1, [this, buffer](Cpu& c) {
             if (DsBuffer* b = buffer(c)) {
                 b->startPosition = b->position(process_->now());
                 b->playing = false;
             }
             return kOk;
         }},
        {"Unlock", 5, ok},
        {"Restore", 1, ok},
    };
    // The audio thread reads the buffers too (mixSound).
    for (Method& method : m)
        if (method.fn)
            method.fn = [this, fn = std::move(method.fn)](Cpu& c) {
                std::lock_guard<std::mutex> lock(soundMutex_);
                return fn(c);
            };
    soundBufferVtable_ = makeVtable("IDirectSoundBuffer", m);

    std::vector<Method> ds = {
        {"QueryInterface", 3, [](Cpu&) { return kNoInterface; }},
        {"AddRef", 1, [](Cpu&) { return 2u; }},
        {"Release", 1, [](Cpu&) { return 0u; }},
        // CreateSoundBuffer(this, desc*, buffer**, outer): DSBUFFERDESC's
        // flags (+4; DSBCAPS_PRIMARYBUFFER 1), size (+8) and format (+16).
        {"CreateSoundBuffer", 4, [this, readFormat](Cpu& c) {
             const uint32_t desc = c.arg(1);
             DsBuffer b;
             b.primary = c.read32(desc + 4) & 1;
             b.size = b.primary ? 22050 * 4 / 4 : c.read32(desc + 8);
             readFormat(c, c.read32(desc + 16), b);
             say("  CreateSoundBuffer: " + std::string(b.primary ? "primary" : "secondary") + ", " +
                 std::to_string(b.size) + " bytes, " + std::to_string(b.rate) + " Hz, " + std::to_string(b.channels) +
                 " channels, " + std::to_string(b.bits) + " bits, flags " + std::to_string(c.read32(desc + 4)));
             b.memory = process_->memory().alloc(std::max(b.size, 16u));
             if (!b.memory)
                 return 0x8007000eu; // E_OUTOFMEMORY
             const uint32_t object = newObject(soundBufferVtable_);
             std::lock_guard<std::mutex> lock(soundMutex_);
             soundBuffers_[object] = b;
             c.write32(c.arg(2), object);
             return kOk;
         }},
        // GetCaps: a sound card that does 8 and 16 bits, mono and stereo, at
        // any rate, in software.
        {"GetCaps", 2, [](Cpu& c) {
             const uint32_t p = c.arg(1), size = c.read32(p);
             for (uint32_t o = 4; o < size && o < 96; o += 4)
                 c.write32(p + o, 0);
             c.write32(p + 4, 0x1 | 0x2 | 0x4 | 0x8 | 0x10);
             c.write32(p + 8, 100);
             c.write32(p + 12, 100000);
             return kOk;
         }},
        {"DuplicateSoundBuffer", 3, [](Cpu&) { return 0x80004001u; }},
        {"SetCooperativeLevel", 3, ok},
        {"Compact", 1, ok},
        {"GetSpeakerConfig", 2, [](Cpu& c) {
             c.write32(c.arg(1), 4); // DSSPEAKER_STEREO
             return kOk;
         }},
        {"SetSpeakerConfig", 2, ok},
        {"Initialize", 2, ok},
    };
    const uint32_t sound = makeObject("IDirectSound", ds);
    provide("DSOUND.dll", "DirectSoundCreate", 3, [sound](Cpu& c) {
        c.write32(c.arg(1), sound);
        return kOk;
    });
}

} // namespace reluva::nfs
