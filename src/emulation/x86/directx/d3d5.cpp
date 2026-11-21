// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// IDirect3D2, DirectX 5's Direct3D, as the DirectX 5 documentation
// describes it: the devices it offers, as D3DDEVICEDESC (DirectX 5's,
// 204 bytes). They are the devices IDirect3D7 offers (d3d7.cpp).

#include "emulation/x86/directx/device.h"

#include <cstring>
#include <string_view>

namespace reluva::x86::dx {

namespace {

// A D3DDEVICEDESC's size in DirectX 5, which programs of then set and
// check.
constexpr uint32_t kDeviceDescSize = 204;
// D3DFINDDEVICERESULT: its size, the device's GUID, its hardware and
// software descriptions.
constexpr uint32_t kFindResultSize = 4 + 16 + 2 * kDeviceDescSize;

struct DeviceKind {
    const char *description;
    const char *name;
    const char *iid;
    bool hardware;
};

const DeviceKind kDevices[] = {
    {"Microsoft Direct3D RGB Software Emulation", "RGB Emulation", kIidRgbDevice, false},
    {"Microsoft Direct3D Hardware acceleration through Direct3D HAL", "Direct3D HAL", kIidHalDevice, true},
};

// A D3DDEVICEDESC at `p`: what the device does, or, for the side it isn't
// (a hardware device's software description, say), only its size: a
// program tells hardware from emulation by which has flags.
void writeDeviceDesc5(Cpu &c, uint32_t p, bool filled, bool hardware) {
    std::memset(c.memory().data() + p, 0, kDeviceDescSize);
    c.write32(p, kDeviceDescSize);
    if (!filled)
        return;
    c.write32(p + 4, 0x7ff); // dwFlags: every field below is valid
    c.write32(p + 8, 2);     // dcmColorModel: D3DCOLOR_RGB
    // dwDevCaps: floating-point TL vertices, execute buffers, TL vertices
    // and textures in system memory, DrawPrimitive; in video memory too,
    // and rasterized in hardware, for the hardware device.
    c.write32(p + 12, 0x1 | 0x10 | 0x40 | 0x100 | 0x400 | (hardware ? 0x20 | 0x80 | 0x200 | 0x80000 : 0));
    c.write32(p + 16, 8);                      // dtcTransformCaps: its size,
    c.write32(p + 20, 0x1);                    // D3DTRANSFORMCAPS_CLIP
    c.write32(p + 24, 1);                      // bClipping
    c.write32(p + 28, 16);                     // dlcLightingCaps: its size,
    c.write32(p + 32, 0xf);                    // point, spot, directional and parallel point lights,
    c.write32(p + 36, 1);                      // the RGB lighting model,
    c.write32(p + 40, 8);                      // 8 lights
    writePrimCaps(c, p + 44);                  // lines
    writePrimCaps(c, p + 100);                 // triangles
    c.write32(p + 156, 0x400 | 0x100);         // render target depths: 16, 32
    c.write32(p + 160, 0x400 | 0x200 | 0x100); // Z buffer depths: 16, 24, 32
    c.write32(p + 168, 65535);                 // dwMaxVertexCount
    c.write32(p + 172, 1);                     // smallest texture
    c.write32(p + 176, 1);
    c.write32(p + 180, 2048); // largest texture
    c.write32(p + 184, 2048);
}

} // namespace

std::optional<uint32_t> DirectDraw::invoke3d2(const ComInterface &iface, int index, Cpu &c) {
    const std::string_view m = iface.methods[size_t(index)].name;
    // EnumDevices(callback(guid*, description, name, hardware desc*,
    // software desc*, context), context)
    if (m == "EnumDevices") {
        const uint32_t callback = c.arg(1);
        const uint32_t context = c.arg(2);
        const uint32_t block = s->process.memory().alloc(16 + 2 * kDeviceDescSize + 256);
        if (!block)
            return kDDErrOutOfMemory;
        const uint32_t guid = block;
        const uint32_t hal = block + 16;
        const uint32_t hel = hal + kDeviceDescSize;
        const uint32_t text = hel + kDeviceDescSize;
        for (const DeviceKind &k : kDevices) {
            if (k.hardware && emulationOnly)
                continue;
            ComHost::writeGuid(c, guid, k.iid);
            writeDeviceDesc5(c, hal, k.hardware, true);
            writeDeviceDesc5(c, hel, !k.hardware, k.hardware);
            c.writeString(text, k.description, 128);
            c.writeString(text + 128, k.name, 128);
            uint32_t keepGoing = 0;
            if (Error e = c.call(callback, {guid, text, text + 128, hal, hel, context}, keepGoing))
                break;
            if (!keepGoing)
                break;
        }
        s->process.memory().free(block);
        return kDDOk;
    }
    // FindDevice(D3DFINDDEVICESEARCH*, D3DFINDDEVICERESULT*): the first
    // device of the GUID asked for (D3DFDS_GUID, 2) and hardware or not
    // (D3DFDS_HARDWARE, 4), as asked.
    if (m == "FindDevice") {
        const uint32_t search = c.arg(1);
        const uint32_t result = c.arg(2);
        if (!c.valid(search, 32) || !c.valid(result, kFindResultSize))
            return kDDErrInvalidParams;
        const uint32_t flags = c.read32(search + 4);
        const bool hardware = c.read32(search + 8) != 0;
        const std::string guid = ComHost::guid(c, search + 16);
        for (const DeviceKind &k : kDevices) {
            if ((k.hardware && emulationOnly) || ((flags & 0x2) && guid != k.iid) ||
                ((flags & 0x4) && hardware != k.hardware))
                continue;
            c.write32(result, kFindResultSize);
            ComHost::writeGuid(c, result + 4, k.iid);
            writeDeviceDesc5(c, result + 20, k.hardware, true);
            writeDeviceDesc5(c, result + 20 + kDeviceDescSize, !k.hardware, k.hardware);
            return kDDOk;
        }
        return kDDErrNotFound;
    }
    // CreateDevice(class, render target, out): the device IDirect3D7 makes,
    // as IDirect3DDevice2.
    if (m == "CreateDevice") {
        const std::string kind = ComHost::guid(c, c.arg(1));
        std::shared_ptr<Surface> target = s->com.shared<Surface>(c.arg(2));
        const uint32_t out = c.arg(3);
        if (!target || !c.valid(out, 4))
            return kDDErrInvalidParams;
        if (kind != kIidHalDevice && kind != kIidRgbDevice && kind != kIidMmxDevice)
            return kDDErrInvalidParams;
        c.write32(out, s->com.pointer(std::make_shared<Device>(s, target), s->interfaces.device2));
        return kDDOk;
    }
    return std::nullopt;
}

std::optional<uint32_t> Device::invoke2(std::string_view m, Cpu &c) const {
    // GetCaps(hardware desc*, software desc*): as the hardware device.
    if (m == "GetCaps") {
        if (c.valid(c.arg(1), kDeviceDescSize))
            writeDeviceDesc5(c, c.arg(1), true, true);
        if (c.valid(c.arg(2), kDeviceDescSize))
            writeDeviceDesc5(c, c.arg(2), false, true);
        return kDDOk;
    }
    // EnumTextureFormats(callback(DDSURFACEDESC*, context), context): each
    // format in a surface description (108 bytes, its pixel format at 72).
    if (m == "EnumTextureFormats") {
        const uint32_t desc = s->process.memory().alloc(108);
        if (!desc)
            return kDDErrOutOfMemory;
        for (const PixelFormat &f : textureFormats()) {
            std::memset(c.memory().data() + desc, 0, 108);
            c.write32(desc, 108);
            c.write32(desc + 4, 0x1000); // DDSD_PIXELFORMAT
            f.write(c, desc + 72);
            uint32_t keepGoing = 0;
            if (Error e = c.call(c.arg(1), {desc, c.arg(2)}, keepGoing))
                break;
            if (!keepGoing)
                break;
        }
        s->process.memory().free(desc);
        return kDDOk;
    }
    // GetDirect3D(out): the DirectDraw object's IDirect3D2.
    if (m == "GetDirect3D") {
        if (!c.valid(c.arg(1), 4) || !s->directDraw)
            return kDDErrInvalidParams;
        c.write32(c.arg(1), s->com.pointer(s->directDraw, s->interfaces.direct3d2));
        return kDDOk;
    }
    return std::nullopt;
}

} // namespace reluva::x86::dx
