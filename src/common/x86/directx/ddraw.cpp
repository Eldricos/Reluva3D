// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// DDRAW.dll's functions and the DirectDraw object, as the DirectX 7
// documentation describes them. The display is one adapter ("Primary
// Display Driver") with 3D, the modes DirectX::modes lists, and
// DirectX::videoMemory of video memory.

#include "common/x86/directx/internal.h"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace reluva::x86::dx {

namespace {

enum : uint32_t {
    kSdCaps = 0x1, kSdHeight = 0x2, kSdWidth = 0x4, kSdPitch = 0x8, kSdBackBufferCount = 0x20,
    kSdZBufferBitDepth = 0x40, kSdPixelFormat = 0x1000, kSdCkSrcBlt = 0x10000, kSdMipMapCount = 0x20000,
    kSdRefreshRate = 0x40000
};
constexpr uint32_t kScExclusive = 0x10;
constexpr uint32_t kCreateEmulationOnly = 2;

// What the adapter can do (DDCAPS's fields).
constexpr uint32_t kDdCaps = 0x1 /* 3D */ | 0x40 /* BLT */ | 0x200 /* BLTSTRETCH */ | 0x400000 /* COLORKEY */ |
                             0x04000000 /* BLTCOLORFILL */ | 0x10000000 /* BLTDEPTHFILL */ |
                             0x80000000u /* CANBLTSYSMEM */;
constexpr uint32_t kDdCaps2 = 0x1000 /* WIDESURFACES */ | 0x80000 /* CANRENDERWINDOWED */ |
                              0x02000000 /* FLIPNOVSYNC */;
constexpr uint32_t kSurfaceCaps = kCapsBackBuffer | kCapsComplex | kCapsFlip | kCapsFrontBuffer | kCapsOffscreenPlain |
                                  kCapsPalette | kCapsPrimarySurface | kCapsSystemMemory | kCapsTexture | kCaps3DDevice |
                                  kCapsVideoMemory | kCapsVisible | kCapsZBuffer | kCapsMipMap | kCapsLocalVidMem;

int log2Size(int n) {
    int l = 0;
    while ((1 << (l + 1)) <= n)
        ++l;
    return l;
}

} // namespace

const ComInterface* DirectDraw::query(const std::string& iid) {
    Interfaces& i = s->interfaces;
    for (ComInterface* f : {&i.directDraw7, &i.directDraw4, &i.directDraw2, &i.directDraw, &i.direct3d7})
        if (iid == f->iid)
            return f;
    return nullptr;
}

void DirectDraw::writeCaps(Cpu& c, uint32_t p, bool hardware) {
    const uint32_t size = c.read32(p);
    if (size < 8 || !c.valid(p, size))
        return;
    std::memset(c.memory().data() + p + 4, 0, size - 4);
    auto put = [&](uint32_t at, uint32_t v) {
        if (at + 4 <= size)
            c.write32(p + at, v);
    };
    put(4, kDdCaps);
    put(8, kDdCaps2);
    put(12, 0x200); // dwCKeyCaps: DDCKEYCAPS_SRCBLT
    put(24, 0x4);   // dwPalCaps: DDPCAPS_8BIT
    put(56, 0x400 | 0x200 | 0x100); // dwZBufferBitDepths: 16, 24, 32
    const uint32_t total = hardware ? s->owner.videoMemory : 0;
    put(60, total);
    put(64, total - std::min(total, s->videoUsed));
    put(132, kSurfaceCaps); // ddsOldCaps
    put(364, kSurfaceCaps); // ddsCaps
}

uint32_t DirectDraw::enumModes(Cpu& c, const ComInterface& iface) {
    const uint32_t filter = c.arg(2), context = c.arg(3), callback = c.arg(4);
    const bool v2 = &iface == &s->interfaces.directDraw7 || &iface == &s->interfaces.directDraw4;
    const uint32_t size = v2 ? 124 : 108;
    const uint32_t desc = s->process.memory().alloc(size);
    uint32_t result = kDDOk;
    for (const DirectX::Mode& mode : s->owner.modes) {
        if (filter) {
            const uint32_t f = c.read32(filter + 4);
            if (((f & kSdWidth) && c.read32(filter + 12) != uint32_t(mode.width)) ||
                ((f & kSdHeight) && c.read32(filter + 8) != uint32_t(mode.height)) ||
                ((f & kSdPixelFormat) && c.read32(filter + 84) != uint32_t(mode.bits)))
                continue;
        }
        std::memset(c.memory().data() + desc, 0, size);
        const PixelFormat format = rgbFormat(mode.bits);
        c.write32(desc, size);
        c.write32(desc + 4, kSdWidth | kSdHeight | kSdPitch | kSdPixelFormat | kSdRefreshRate);
        c.write32(desc + 8, uint32_t(mode.height));
        c.write32(desc + 12, uint32_t(mode.width));
        c.write32(desc + 16, uint32_t(mode.width) * format.bytes());
        c.write32(desc + 24, 60);
        format.write(c, desc + 72);
        uint32_t keepGoing = 0;
        if (Error e = c.call(callback, {desc, context}, keepGoing)) {
            result = kDDErrGeneric;
            break;
        }
        if (!keepGoing) // DDENUMRET_CANCEL
            break;
    }
    s->process.memory().free(desc);
    return result;
}

// CreateSurface(desc, out, outer): a primary surface (with back buffers if
// it flips), a Z buffer, or an off-screen surface or texture (with its
// smaller mip levels).
uint32_t DirectDraw::createSurface(Cpu& c, const ComInterface& iface) {
    const uint32_t p = c.arg(1), out = c.arg(2);
    if (!c.valid(p, 108) || !c.valid(out, 4))
        return kDDErrInvalidParams;
    const bool desc2 = c.read32(p) == 124;
    const uint32_t flags = c.read32(p + 4);
    std::array<uint32_t, 4> caps{c.read32(p + 104), desc2 ? c.read32(p + 108) : 0, 0, 0};
    const ComInterface& surfaceIface = &iface == &s->interfaces.directDraw7   ? s->interfaces.surface7
                                       : &iface == &s->interfaces.directDraw4 ? s->interfaces.surface4
                                                                             : s->interfaces.surface;
    const int displayBits = s->bits ? s->bits : s->process.screenBits;
    int width = int(c.read32(p + 12)), height = int(c.read32(p + 8));
    PixelFormat format = (flags & kSdPixelFormat) ? PixelFormat::read(c, p + 72) : rgbFormat(displayBits);
    if (!(caps[0] & kCapsSystemMemory) && !(caps[1] & kCaps2TextureManage))
        caps[0] |= kCapsVideoMemory | kCapsLocalVidMem;

    std::shared_ptr<Surface> surface;
    if (caps[0] & kCapsPrimarySurface) {
        width = s->exclusive && s->width ? s->width : s->process.screenWidth;
        height = s->exclusive && s->height ? s->height : s->process.screenHeight;
        format = rgbFormat(displayBits);
        caps[0] |= kCapsVisible;
        const int backs = (caps[0] & kCapsFlip) && (flags & kSdBackBufferCount) ? int(c.read32(p + 20)) : 0;
        if (backs)
            caps[0] |= kCapsFrontBuffer;
        surface = s->makeSurface(width, height, format, caps);
        if (!surface)
            return kDDErrOutOfMemory;
        surface->chain.push_back(surface.get());
        Surface* previous = surface.get();
        for (int i = 0; i < backs; ++i) {
            std::array<uint32_t, 4> backCaps = caps;
            backCaps[0] &= ~(kCapsPrimarySurface | kCapsFrontBuffer | kCapsVisible);
            if (i == 0)
                backCaps[0] |= kCapsBackBuffer;
            auto back = s->makeSurface(width, height, format, backCaps);
            if (!back)
                return kDDErrOutOfMemory;
            previous->attached.push_back(back);
            surface->chain.push_back(back.get());
            previous = back.get();
        }
        s->primary = surface;
    } else {
        if (caps[0] & kCapsZBuffer) {
            if (!(flags & kSdPixelFormat))
                format = zFormat((flags & kSdZBufferBitDepth) && c.read32(p + 24) ? int(c.read32(p + 24)) : 16);
        }
        if (!(flags & kSdWidth) || !(flags & kSdHeight) || width <= 0 || height <= 0)
            return kDDErrInvalidParams;
        int levels = 1;
        if (caps[0] & kCapsMipMap)
            levels = (flags & kSdMipMapCount) ? int(c.read32(p + 24)) : log2Size(std::max(width, height)) + 1;
        surface = s->makeSurface(width, height, format, caps);
        if (!surface)
            return kDDErrOutOfMemory;
        Surface* previous = surface.get();
        for (int l = 1; l < levels; ++l) {
            std::array<uint32_t, 4> levelCaps = caps;
            levelCaps[1] |= kCaps2MipMapSubLevel;
            auto level = s->makeSurface(std::max(1, width >> l), std::max(1, height >> l), format, levelCaps);
            if (!level)
                return kDDErrOutOfMemory;
            previous->attached.push_back(level);
            previous = level.get();
        }
    }
    if (flags & kSdCkSrcBlt) {
        surface->hasSrcKey = true;
        surface->srcKeyLow = c.read32(p + 64);
        surface->srcKeyHigh = c.read32(p + 68);
    }
    c.write32(out, s->com.pointer(surface, surfaceIface));
    return kDDOk;
}

std::optional<uint32_t> DirectDraw::invoke(const ComInterface& iface, int index, Cpu& c) {
    if (&iface == &s->interfaces.direct3d7)
        return invoke3d(iface, index, c);
    const std::string_view m = iface.methods[size_t(index)].name;
    if (m == "CreateSurface")
        return createSurface(c, iface);
    if (m == "EnumDisplayModes")
        return enumModes(c, iface);
    if (m == "GetCaps") {
        if (c.arg(1))
            writeCaps(c, c.arg(1), !emulationOnly);
        if (c.arg(2))
            writeCaps(c, c.arg(2), false);
        return kDDOk;
    }
    if (m == "GetDisplayMode") {
        const uint32_t p = c.arg(1);
        if (!c.valid(p, 108))
            return kDDErrInvalidParams;
        const uint32_t size = c.read32(p) == 124 ? 124 : 108;
        std::memset(c.memory().data() + p + 4, 0, size - 4);
        c.write32(p, size);
        const int w = s->width ? s->width : s->process.screenWidth;
        const int h = s->height ? s->height : s->process.screenHeight;
        const PixelFormat format = rgbFormat(s->bits ? s->bits : s->process.screenBits);
        c.write32(p + 4, kSdWidth | kSdHeight | kSdPitch | kSdPixelFormat | kSdRefreshRate);
        c.write32(p + 8, uint32_t(h));
        c.write32(p + 12, uint32_t(w));
        c.write32(p + 16, uint32_t(w) * format.bytes());
        c.write32(p + 24, 60);
        format.write(c, p + 72);
        return kDDOk;
    }
    if (m == "SetCooperativeLevel") {
        s->window = c.arg(1);
        s->exclusive = (c.arg(2) & kScExclusive) != 0;
        s->noWindowChanges = (c.arg(2) & 0x4) != 0; // DDSCL_NOWINDOWCHANGES
        return kDDOk;
    }
    if (m == "SetDisplayMode") {
        const int w = int(c.arg(1)), h = int(c.arg(2)), bits = int(c.arg(3));
        const auto& modes = s->owner.modes;
        if (std::none_of(modes.begin(), modes.end(),
                         [&](const DirectX::Mode& mode) { return mode.width == w && mode.height == h && mode.bits == bits; }))
            return kDDErrInvalidMode;
        // The desktop becomes the mode, and a fullscreen program's window
        // covers it.
        if (!s->desktopWidth) {
            s->desktopWidth = s->process.screenWidth;
            s->desktopHeight = s->process.screenHeight;
            s->desktopBits = s->process.screenBits;
        }
        s->width = w, s->height = h, s->bits = bits;
        s->process.screenWidth = w, s->process.screenHeight = h, s->process.screenBits = bits;
        if (s->exclusive && !s->noWindowChanges && s->window)
            s->process.moveWindow(s->window, 0, 0, w, h);
        return kDDOk;
    }
    if (m == "RestoreDisplayMode") {
        s->width = s->height = s->bits = 0;
        if (s->desktopWidth) {
            s->process.screenWidth = s->desktopWidth;
            s->process.screenHeight = s->desktopHeight;
            s->process.screenBits = s->desktopBits;
            s->desktopWidth = 0;
        }
        return kDDOk;
    }
    if (m == "GetAvailableVidMem") {
        const uint32_t total = c.arg(2), free = c.arg(3);
        if (total)
            c.write32(total, s->owner.videoMemory);
        if (free)
            c.write32(free, s->owner.videoMemory - std::min(s->owner.videoMemory, s->videoUsed));
        return kDDOk;
    }
    if (m == "GetDeviceIdentifier") {
        const uint32_t p = c.arg(1);
        const uint32_t size = &iface == &s->interfaces.directDraw7 ? 1072 : 1064;
        if (!c.valid(p, size))
            return kDDErrInvalidParams;
        std::memset(c.memory().data() + p, 0, size);
        c.writeString(p, "reluva.drv", 512);
        c.writeString(p + 512, "Reluva DirectDraw", 512);
        return kDDOk;
    }
    if (m == "CreateClipper") {
        auto clipper = std::make_shared<Clipper>();
        clipper->s = s;
        c.write32(c.arg(2), s->com.pointer(clipper, s->interfaces.clipper));
        return kDDOk;
    }
    if (m == "CreatePalette") {
        auto palette = std::make_shared<Palette>();
        palette->s = s;
        palette->caps = c.arg(1);
        const uint32_t entries = c.arg(2);
        if (entries && c.valid(entries, 256 * 4))
            for (uint32_t i = 0; i < 256; ++i) {
                const uint32_t e = c.read32(entries + 4 * i);
                palette->argb[i] = 0xff000000u | (e & 0xff) << 16 | (e & 0xff00) | (e >> 16 & 0xff);
            }
        c.write32(c.arg(3), s->com.pointer(palette, s->interfaces.palette));
        return kDDOk;
    }
    if (m == "GetFourCCCodes") {
        if (c.arg(1))
            c.write32(c.arg(1), 0);
        return kDDOk;
    }
    if (m == "GetMonitorFrequency") {
        c.write32(c.arg(1), 60);
        return kDDOk;
    }
    if (m == "GetScanLine") {
        c.write32(c.arg(1), 0);
        return kDDOk;
    }
    if (m == "GetVerticalBlankStatus") {
        c.write32(c.arg(1), 1);
        return kDDOk;
    }
    if (m == "Compact" || m == "FlipToGDISurface" || m == "RestoreAllSurfaces" || m == "TestCooperativeLevel" ||
        m == "WaitForVerticalBlank" || m == "Initialize")
        return kDDOk;
    return std::nullopt;
}

} // namespace reluva::x86::dx

namespace reluva::x86 {

DirectX::~DirectX() = default;

const DirectX::Frame& DirectX::frame() const { return s_->frame; }

DirectX::DirectX(Win32Process& process) : s_(std::make_unique<State>(*this, process)) {
    State* s = s_.get();
    dx::Interfaces& i = s->interfaces;
    for (ComInterface* f : {&i.directDraw, &i.directDraw2, &i.directDraw4, &i.directDraw7, &i.surface, &i.surface2,
                            &i.surface3, &i.surface4, &i.surface7, &i.palette, &i.clipper, &i.direct3d7, &i.device7,
                            &i.vertexBuffer7})
        s->com.define(*f);
    const char* dll = "DDRAW.dll";
    Cpu& c = process.cpu();
    auto text = [&](const std::string& t) {
        const uint32_t a = process.memory().alloc(uint32_t(t.size() + 1));
        c.writeString(a, t, uint32_t(t.size() + 1));
        return a;
    };
    const uint32_t description = text("Primary Display Driver"), name = text("display");

    auto create = [s](uint32_t guid) {
        auto dd = std::make_shared<dx::DirectDraw>();
        dd->s = s;
        dd->emulationOnly = guid == dx::kCreateEmulationOnly;
        s->directDraw = dd;
        return dd;
    };
    // DirectDrawCreate(guid, out, outer): an IDirectDraw.
    process.provideStdcall(dll, "DirectDrawCreate", 3, [s, create](Cpu& c) {
        if (!c.valid(c.arg(1), 4))
            return dx::kDDErrInvalidParams;
        c.write32(c.arg(1), s->com.pointer(create(c.arg(0)), s->interfaces.directDraw));
        return dx::kDDOk;
    });
    // DirectDrawCreateEx(guid, out, iid, outer): an IDirectDraw7.
    process.provideStdcall(dll, "DirectDrawCreateEx", 4, [s, create](Cpu& c) {
        if (!c.valid(c.arg(1), 4))
            return dx::kDDErrInvalidParams;
        if (ComHost::guid(c, c.arg(2)) != dx::kIidDirectDraw7)
            return dx::kDDErrInvalidParams;
        c.write32(c.arg(1), s->com.pointer(create(c.arg(0)), s->interfaces.directDraw7));
        return dx::kDDOk;
    });
    // The one adapter, as the primary display (no GUID).
    process.provideStdcall(dll, "DirectDrawEnumerateA", 2, [description, name](Cpu& c) {
        uint32_t r = 0;
        (void)c.call(c.arg(0), {0, description, name, c.arg(1)}, r);
        return dx::kDDOk;
    });
    process.provideStdcall(dll, "DirectDrawEnumerateExA", 3, [description, name](Cpu& c) {
        uint32_t r = 0;
        (void)c.call(c.arg(0), {0, description, name, c.arg(1), 0}, r);
        return dx::kDDOk;
    });
}

} // namespace reluva::x86
