// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// IDirect3D and IDirect3D2, the Direct3D of DirectX 3 and 5, on the same
// DirectDraw object as IDirect3D7, as the DirectX 5 documentation describes
// them. Structure layouts are the DirectX 5 SDK's (d3dcaps.h).

#include "emulation/x86/directx/device.h"

#include <cstring>
#include <string_view>

namespace reluva::x86::dx {

namespace {

// DirectX 5's D3DDEVICEDESC; DirectX 3's is the first 172 bytes of it.
constexpr uint32_t kDeviceDescSize = 204;

struct LegacyDevice {
    const char *description;
    const char *name;
    const char *iid;
    bool hardware;
};
// Software emulation, then the hardware device, as Direct3D 7's list.
const LegacyDevice kLegacyDevices[] = {
    {"Microsoft Direct3D RGB Software Emulation", "RGB Emulation", kIidRgbDevice, false},
    {"Microsoft Direct3D Hardware acceleration through Direct3D HAL", "Direct3D HAL", kIidHalDevice, true},
};

// A D3DDEVICEDESC filled in, or (a software device's hardware description)
// only its size: a colour model of 0 is how programs tell the two apart.
void writeLegacyDesc(Cpu &c, uint32_t p, bool filled, bool hardware) {
    std::memset(c.memory().data() + p, 0, kDeviceDescSize);
    c.write32(p, kDeviceDescSize);
    if (!filled)
        return;
    c.write32(p + 4, 0x7ff); // every field below is valid
    c.write32(p + 8, 2);     // D3DCOLOR_RGB
    // FLOATTLVERTEX, execute buffers, TL vertices and textures in system and
    // video memory, DrawPrimitive with TL vertices, rendering after a flip
    c.write32(p + 12, 0x1 | 0x10 | 0x20 | 0x40 | 0x80 | 0x100 | 0x200 | 0x400 | 0x800 | (hardware ? 0x80000 : 0));
    c.write32(p + 16, 8); // D3DTRANSFORMCAPS: size, CLIP
    c.write32(p + 20, 1);
    c.write32(p + 24, 1);                      // clipping
    c.write32(p + 28, 16);                     // D3DLIGHTINGCAPS: size
    c.write32(p + 32, 0x1 | 0x2 | 0x4);        // point, spot, directional
    c.write32(p + 36, 1);                      // D3DLIGHTINGMODEL_RGB
    c.write32(p + 40, 8);                      // lights
    writePrimCaps(c, p + 44);                  // lines
    writePrimCaps(c, p + 100);                 // triangles
    c.write32(p + 156, 0x400 | 0x100);         // render target depths: 16, 32
    c.write32(p + 160, 0x400 | 0x200 | 0x100); // Z buffer depths: 16, 24, 32
    c.write32(p + 168, 0xffff);                // vertices in an execute buffer
    c.write32(p + 172, 1);
    c.write32(p + 176, 1);
    c.write32(p + 180, 2048);
    c.write32(p + 184, 2048);
}

} // namespace

std::optional<uint32_t> DirectDraw::invoke3d5(const ComInterface &iface, int index, Cpu &c) const {
    const std::string_view m = iface.methods[size_t(index)].name;
    if (m == "Initialize")
        return kDDOk;
    // EnumDevices(callback(guid*, description, name, hardware desc*, software desc*, context), context)
    if (m == "EnumDevices") {
        const uint32_t callback = c.arg(1);
        const uint32_t context = c.arg(2);
        const uint32_t guid = s->process.memory().alloc(16 + 2 * kDeviceDescSize + 256);
        const uint32_t hal = guid + 16;
        const uint32_t hel = hal + kDeviceDescSize;
        const uint32_t description = hel + kDeviceDescSize;
        const uint32_t name = description + 128;
        for (const LegacyDevice &k : kLegacyDevices) {
            if (k.hardware && emulationOnly)
                continue;
            ComHost::writeGuid(c, guid, k.iid);
            writeLegacyDesc(c, hal, k.hardware, true);
            writeLegacyDesc(c, hel, true, false);
            c.writeString(description, k.description, 128);
            c.writeString(name, k.name, 128);
            uint32_t keepGoing = 0;
            if (Error e = c.call(callback, {guid, description, name, hal, hel, context}, keepGoing))
                break;
            if (!keepGoing)
                break;
        }
        s->process.memory().free(guid);
        return kDDOk;
    }
    return std::nullopt;
}

} // namespace reluva::x86::dx
