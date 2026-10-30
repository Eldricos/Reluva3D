// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// What DirectDraw and Direct3D share: the interfaces, surfaces and pixel
// formats. Structure offsets are those of the DirectX 7 SDK's definitions.

#pragma once

#include "common/x86/com.h"
#include "common/x86/directx/directx.h"

#include <array>
#include <map>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace reluva::x86::dx {

// --- Results ----------------------------------------------------------------------------
constexpr uint32_t kDDOk = 0;
constexpr uint32_t kDDErrGeneric = 0x80004005u, kDDErrInvalidParams = 0x80070057u, kDDErrOutOfMemory = 0x8007000eu,
                   kDDErrUnsupported = 0x80004001u, kDDErrNotFound = 0x887600ffu, kDDErrInvalidObject = 0x88760082u,
                   kDDErrSurfaceBusy = 0x887601aeu, kDDErrNoClipList = 0x887600cdu, kDDErrNotLocked = 0x887602b8u,
                   kDDErrNoPalette = 0x887600ffu, kDDErrInvalidMode = 0x88760078u, kDDErrInvalidRect = 0x88760096u,
                   kDDErrNoBlitHw = 0x8876023au, kDDErrNotFlippable = 0x887601e5u, kDDErrNoColorKey = 0x887600d7u,
                   kD3DErrInvalidVertexType = 0x887602d4u, kDDErrCantCreateDc = 0x8876024bu;

// --- Pixel formats (DDPIXELFORMAT) ------------------------------------------------------
enum : uint32_t {
    kPfAlphaPixels = 0x1, kPfFourCC = 0x4, kPfPaletteIndexed8 = 0x20, kPfRgb = 0x40, kPfZBuffer = 0x400,
    kPfLuminance = 0x20000, kPfBumpDuDv = 0x80000
};
struct PixelFormat {
    uint32_t flags = 0, fourCC = 0, bits = 0, r = 0, g = 0, b = 0, a = 0; // for a Z buffer: bits, -, z mask, -
    bool operator==(const PixelFormat& o) const {
        return flags == o.flags && fourCC == o.fourCC && bits == o.bits && r == o.r && g == o.g && b == o.b && a == o.a;
    }
    bool isZ() const { return flags & kPfZBuffer; }
    uint32_t bytes() const { return (bits + 7) / 8; }
    static PixelFormat read(Cpu& c, uint32_t p);
    void write(Cpu& c, uint32_t p) const;
    // A pixel's value as 0xAARRGGBB (alpha 255 without an alpha mask), and back.
    uint32_t toArgb(uint32_t value, const uint32_t* palette) const;
    uint32_t fromArgb(uint32_t argb) const;
};
// PixelFormat::toArgb for many pixels of one format: each channel's
// position worked out once, its scaling to 8 bits a table.
class ArgbConverter {
public:
    ArgbConverter(const PixelFormat& format, const uint32_t* palette);
    uint32_t operator()(uint32_t value) const {
        if (wide_)
            return (*wide_)[value & 0xffff];
        if (general_)
            return format_.toArgb(value, palette_);
        uint32_t argb = alpha_;
        for (const Channel& c : channels_)
            argb |= uint32_t(c.table[((value >> c.shift) & c.mask) >> c.drop]) << c.to;
        return argb;
    }
    // `count` pixels from `bytes` (stored in the format) to `out`.
    void row(const uint8_t* bytes, int count, uint32_t* out) const;

private:
    struct Channel {
        uint32_t shift = 0, mask = 0, drop = 0, to = 0;
        uint8_t table[256]{};
    };
    PixelFormat format_;
    const uint32_t* palette_;
    bool general_ = false; // palettized or luminance: toArgb itself
    uint32_t alpha_ = 0;   // 0xff000000 without an alpha channel
    std::vector<Channel> channels_;
    // 16-bit formats: every value's colour, shared by every converter of
    // the format.
    std::shared_ptr<const std::vector<uint32_t>> wide_;
};
PixelFormat rgbFormat(int bits); // 8 (palettized), 16 (565), 24, 32 (X888)
PixelFormat zFormat(int bits);
// Texture formats offered: 565, 1555, 4444, 555, 8888, X888.
std::vector<PixelFormat> textureFormats();

// --- Surface capabilities (DDSCAPS2) ----------------------------------------------------
enum : uint32_t {
    kCapsAlpha = 0x2, kCapsBackBuffer = 0x4, kCapsComplex = 0x8, kCapsFlip = 0x10, kCapsFrontBuffer = 0x20,
    kCapsOffscreenPlain = 0x40, kCapsPalette = 0x100, kCapsPrimarySurface = 0x200, kCapsSystemMemory = 0x800,
    kCapsTexture = 0x1000, kCaps3DDevice = 0x2000, kCapsVideoMemory = 0x4000, kCapsVisible = 0x8000,
    kCapsZBuffer = 0x20000, kCapsMipMap = 0x400000, kCapsLocalVidMem = 0x10000000, kCapsNonLocalVidMem = 0x08000000
};
constexpr uint32_t kCaps2TextureManage = 0x10, kCaps2MipMapSubLevel = 0x10000;

// --- Surfaces ------------------------------------------------------------------------------
struct Rect {
    int left = 0, top = 0, right = 0, bottom = 0;
    int width() const { return right - left; }
    int height() const { return bottom - top; }
    static Rect read(Cpu& c, uint32_t p);
};

class DirectDraw;
class Surface;
class Palette;
class Clipper;

struct SurfaceData {
    int width = 0, height = 0;
    int pitch = 0;
    uint32_t memory = 0; // in the process
    PixelFormat format;
    std::array<uint32_t, 4> caps{};
    uint64_t version = 1; // changes whenever the pixels may have
};

// An IDirectDrawSurface7 (and the older surface interfaces).
class Surface : public ComObject {
public:
    SurfaceData d;
    bool hasSrcKey = false, hasDestKey = false;
    uint32_t srcKeyLow = 0, srcKeyHigh = 0, destKeyLow = 0, destKeyHigh = 0;
    std::shared_ptr<Palette> palette;
    std::shared_ptr<Clipper> clipper;
    // Surfaces attached to this one: a flipping chain's next buffer, its Z
    // buffer, the next smaller mip level.
    std::vector<std::shared_ptr<Surface>> attached;
    // A flipping chain's front buffer lists the chain: itself, then its back
    // buffers in order.
    std::vector<Surface*> chain;
    int locks = 0;
    uint32_t dc = 0; // GetDC's
    uint32_t priority = 0, lod = 0;
    DirectX::State* s = nullptr;

    std::optional<uint32_t> invoke(const ComInterface& iface, int index, Cpu& c) override;
    const ComInterface* query(const std::string& iid) override;
    ~Surface() override;

    uint32_t pixel(int x, int y) const; // the raw value
    void setPixel(int x, int y, uint32_t value);
    uint32_t argb(int x, int y) const;
    // The palette's 256 colours as 0xAARRGGBB, or nullptr.
    const uint32_t* paletteArgb() const;
    void touched() { ++d.version; }

    uint32_t flip(Cpu& c, const ComInterface& iface);
    uint32_t blt(Cpu& c);
    uint32_t bltFast(Cpu& c);
    // Copies src's `from` onto `to` here, stretching; with a source colour
    // key, its colours are left out.
    void copy(const Surface& src, Rect from, Rect to, bool keyed, uint32_t keyLow, uint32_t keyHigh);
    void fill(Rect r, uint32_t value);
    Rect bounds() const { return {0, 0, d.width, d.height}; }
};

class Palette : public ComObject {
public:
    std::array<uint32_t, 256> argb{}; // from PALETTEENTRY's r, g, b
    uint32_t caps = 0;
    DirectX::State* s = nullptr;
    std::optional<uint32_t> invoke(const ComInterface& iface, int index, Cpu& c) override;
    const ComInterface* query(const std::string& iid) override;
};

class Clipper : public ComObject {
public:
    uint32_t window = 0;
    DirectX::State* s = nullptr;
    std::optional<uint32_t> invoke(const ComInterface& iface, int index, Cpu& c) override;
    const ComInterface* query(const std::string& iid) override;
};

class Device;

// The DirectDraw object: IDirectDraw to IDirectDraw7, and IDirect3D7 (which
// programs get from it with QueryInterface).
class DirectDraw : public ComObject {
public:
    DirectX::State* s = nullptr;
    bool emulationOnly = false; // made for DirectDraw's software emulation
    std::optional<uint32_t> invoke(const ComInterface& iface, int index, Cpu& c) override;
    const ComInterface* query(const std::string& iid) override;
    // IDirect3D7's methods (d3d7.cpp).
    std::optional<uint32_t> invoke3d(const ComInterface& iface, int index, Cpu& c);

private:
    uint32_t createSurface(Cpu& c, const ComInterface& iface);
    uint32_t enumModes(Cpu& c, const ComInterface& iface);
    void writeCaps(Cpu& c, uint32_t p, bool hardware);
};

// --- Interfaces ------------------------------------------------------------------------------
struct Interfaces {
    ComInterface directDraw, directDraw2, directDraw4, directDraw7;
    ComInterface surface, surface2, surface3, surface4, surface7;
    ComInterface palette, clipper, gammaControl;
    ComInterface direct3d7, device7, vertexBuffer7;
    Interfaces();
};

// IIDs.
extern const char* const kIidDirectDraw;
extern const char* const kIidDirectDraw2;
extern const char* const kIidDirectDraw4;
extern const char* const kIidDirectDraw7;
extern const char* const kIidSurface;
extern const char* const kIidSurface2;
extern const char* const kIidSurface3;
extern const char* const kIidSurface4;
extern const char* const kIidSurface7;
extern const char* const kIidPalette;
extern const char* const kIidClipper;
extern const char* const kIidGammaControl;
extern const char* const kIidDirect3D7;
extern const char* const kIidDevice7;
extern const char* const kIidVertexBuffer7;
extern const char* const kIidHalDevice;
extern const char* const kIidTnLHalDevice;
extern const char* const kIidRgbDevice;
extern const char* const kIidMmxDevice;
extern const char* const kIidRefDevice;
extern const char* const kIidTexture2;


} // namespace reluva::x86::dx

namespace reluva::x86 {

struct DirectX::State {
    DirectX& owner;
    Win32Process& process;
    Cpu& cpu;
    // The Direct3D devices, whose draws may still be drawing on threads of
    // their own: finishDraws waits for them, before anything else touches
    // surfaces' pixels. (Before com, which holds the devices, so it's there
    // while they go.)
    std::set<dx::Device*> devices;
    // With DirectX::presentLate: whether a flip's frame waits to be shown at
    // the next, and the draws each device had queued by that flip. (Before
    // com too: a device going takes itself out.)
    bool latePending = false;
    std::map<dx::Device*, uint64_t> lateDraws;
    ComHost com;
    dx::Interfaces interfaces;
    DirectX::Frame frame;
    // The display: its mode, and whether the program owns it (fullscreen).
    int width = 0, height = 0, bits = 0;
    bool exclusive = false, noWindowChanges = false, fpuPreserve = false;
    // The display's gamma ramp (IDirectDrawGammaControl): red, green and
    // blue, 256 entries each, 0..65535; applied to the frames shown.
    std::array<uint16_t, 768> gamma = identityGamma();
    static std::array<uint16_t, 768> identityGamma() {
        std::array<uint16_t, 768> g{};
        for (size_t i = 0; i < 768; ++i)
            g[i] = uint16_t((i % 256) * 257);
        return g;
    }
    int desktopWidth = 0, desktopHeight = 0, desktopBits = 0; // while a mode is set: the desktop's
    uint32_t window = 0;
    uint32_t videoUsed = 0;
    std::weak_ptr<dx::Surface> primary;
    std::shared_ptr<dx::DirectDraw> directDraw;

    State(DirectX& o, Win32Process& p) : owner(o), process(p), cpu(p.cpu()), com(p) {}
    void finishDraws();
    // Copies the primary surface to the frame and tells the engine: once
    // its draws are done, or (presentShown) as it is.
    void present();
    void presentShown();

    // Makes a surface's memory; 0 if there's no room.
    uint32_t allocate(dx::SurfaceData& d);
    std::shared_ptr<dx::Surface> makeSurface(int width, int height, const dx::PixelFormat& format,
                                             const std::array<uint32_t, 4>& caps);
};

} // namespace reluva::x86
