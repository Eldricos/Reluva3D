// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// IDirect3D and IDirect3D2, the Direct3D of DirectX 3 and 5, on the same
// DirectDraw object as IDirect3D7, and Direct3D 1's device, execute
// buffers, viewports, materials, lights and textures, as the DirectX 5
// documentation describes them. Structure layouts, opcodes and states are
// the DirectX 5 SDK's (d3dcaps.h, d3dtypes.h). The device draws through a
// Direct3D 7 Device on the same surface.

#include "emulation/x86/directx/device.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <string_view>
#include <vector>

namespace reluva::x86::dx {

namespace {

// DirectX 5's D3DDEVICEDESC; DirectX 3's is the first 172 bytes of it.
constexpr uint32_t kDeviceDescSize = 204;

struct DeviceKind5 {
    const char *description;
    const char *name;
    const char *iid;
    bool hardware;
};
// Software emulation, then the hardware device, as Direct3D 7's list.
const DeviceKind5 kLegacyDevices[] = {
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

namespace {

float asFloat1(uint32_t v) {
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
uint32_t asBits1(float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}
Color readColor1(Cpu &c, uint32_t p) {
    return {asFloat1(c.read32(p)), asFloat1(c.read32(p + 4)), asFloat1(c.read32(p + 8)), asFloat1(c.read32(p + 12))};
}
uint32_t argbOf(const Color &v) {
    auto ch = [](float f) { return uint32_t(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return ch(v.a) << 24 | ch(v.r) << 16 | ch(v.g) << 8 | ch(v.b);
}

// Vertex layouts as D3DFVF codes: D3DTLVERTEX, D3DLVERTEX and D3DVERTEX,
// 32 bytes each.
constexpr uint32_t kFvfTlVertex = 0x1c4;
constexpr uint32_t kFvfLVertex = 0x1e2;
constexpr uint32_t kFvfVertex = 0x112;
constexpr uint32_t kVertexSize = 32;
// The vertices PROCESSVERTICES can put in a device's pool.
constexpr uint32_t kPoolVertices = 0x10000;

// Render states whose meaning Direct3D 7 replaced by texture stages.
enum : uint32_t {
    kRsTextureHandle = 1,
    kRsTextureAddress = 3,
    kRsTextureMag = 17,
    kRsTextureMin = 18,
    kRsTextureMapBlend = 21,
    kRsBorderColor = 43,
    kRsTextureAddressU = 44,
    kRsTextureAddressV = 45,
};
// Texture stage states (D3DTSS_).
enum : uint32_t {
    kTssColorOp = 1,
    kTssColorArg1 = 2,
    kTssColorArg2 = 3,
    kTssAlphaOp = 4,
    kTssAlphaArg1 = 5,
    kTssAlphaArg2 = 6,
    kTssAddressU = 13,
    kTssAddressV = 14,
    kTssBorderColor = 15,
    kTssMagFilter = 16,
    kTssMinFilter = 17,
    kTssMipFilter = 18,
};

class LegacyDevice;

// An IDirect3DMaterial or IDirect3DMaterial2: a D3DMATERIAL, and its handle.
class LegacyMaterial : public ComObject {
public:
    DirectX::State *s = nullptr;
    Material material;
    uint32_t handle = 0;
    const ComInterface *query(const std::string &iid) override {
        for (const ComInterface *f : {&s->interfaces.material, &s->interfaces.material2})
            if (iid == f->iid)
                return f;
        return nullptr;
    }
    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        // D3DMATERIAL: size, diffuse, ambient, specular, emissive (D3DCOLORVALUEs), power.
        if (m == "SetMaterial" || m == "GetMaterial") {
            const uint32_t p = c.arg(1);
            if (!c.valid(p, 72))
                return kDDErrInvalidParams;
            if (m == "SetMaterial") {
                material.diffuse = readColor1(c, p + 4);
                material.ambient = readColor1(c, p + 20);
                material.specular = readColor1(c, p + 36);
                material.emissive = readColor1(c, p + 52);
                material.power = asFloat1(c.read32(p + 68));
            } else {
                const Color *colors[4] = {&material.diffuse, &material.ambient, &material.specular, &material.emissive};
                for (uint32_t i = 0; i < 4; ++i) {
                    c.write32(p + 4 + 16 * i, asBits1(colors[i]->r));
                    c.write32(p + 8 + 16 * i, asBits1(colors[i]->g));
                    c.write32(p + 12 + 16 * i, asBits1(colors[i]->b));
                    c.write32(p + 16 + 16 * i, asBits1(colors[i]->a));
                }
                c.write32(p + 68, asBits1(material.power));
            }
            return kDDOk;
        }
        if (m == "GetHandle") { // (device, handle out)
            if (!handle) {
                handle = s->nextHandle++;
                s->materialHandles[handle] = shared_from_this();
            }
            if (c.valid(c.arg(2), 4))
                c.write32(c.arg(2), handle);
            return kDDOk;
        }
        if (m == "Initialize" || m == "Reserve" || m == "Unreserve")
            return kDDOk;
        return std::nullopt;
    }
};

// An IDirect3DLight: kept, not lit with (Direct3D 7's lights do the lighting
// of D3DVERTEX vertices only when a program sets them through the device).
class LegacyLight : public ComObject {
public:
    DirectX::State *s = nullptr;
    std::vector<uint8_t> light;
    const ComInterface *query(const std::string &iid) override {
        return iid == s->interfaces.light.iid ? &s->interfaces.light : nullptr;
    }
    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        if (m == "SetLight" || m == "GetLight") { // D3DLIGHT, its size first
            const uint32_t p = c.arg(1);
            const uint32_t size = c.valid(p, 4) ? std::min(c.read32(p), 128u) : 0;
            if (!size || !c.valid(p, size))
                return kDDErrInvalidParams;
            if (m == "SetLight")
                light.assign(c.memory().data() + p, c.memory().data() + p + size);
            else if (!light.empty())
                std::memcpy(c.memory().data() + p + 4, light.data() + 4, std::min<size_t>(size, light.size()) - 4);
            return kDDOk;
        }
        if (m == "Initialize")
            return kDDOk;
        return std::nullopt;
    }
};

// An IDirect3DViewport or IDirect3DViewport2: the area drawn to, the
// background a Clear fills it with.
class LegacyViewport : public ComObject {
public:
    DirectX::State *s = nullptr;
    Viewport viewport;
    bool set = false;
    uint32_t background = 0; // a material handle
    LegacyDevice *device = nullptr;
    const ComInterface *query(const std::string &iid) override {
        for (const ComInterface *f : {&s->interfaces.viewport2, &s->interfaces.viewport})
            if (iid == f->iid)
                return f;
        return nullptr;
    }
    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override;
};

// An IDirect3DExecuteBuffer: its memory in the process, and what's in it
// (D3DEXECUTEDATA): vertices from an offset, instructions from another.
class ExecuteBuffer : public ComObject {
public:
    DirectX::State *s = nullptr;
    uint32_t memory = 0;
    uint32_t size = 0;
    uint32_t vertexOffset = 0;
    uint32_t vertexCount = 0;
    uint32_t instructionOffset = 0;
    uint32_t instructionLength = 0;
    ~ExecuteBuffer() override {
        if (memory)
            s->process.memory().free(memory);
    }
    const ComInterface *query(const std::string &iid) override {
        return iid == s->interfaces.executeBuffer.iid ? &s->interfaces.executeBuffer : nullptr;
    }
    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        // Lock(D3DEXECUTEBUFFERDESC*): size, flags, caps, buffer size, data.
        if (m == "Lock") {
            const uint32_t p = c.arg(1);
            if (!c.valid(p, 20))
                return kDDErrInvalidParams;
            c.write32(p + 4, 0x2 | 0x4); // D3DDEB_BUFSIZE, D3DDEB_LPDATA
            c.write32(p + 12, size);
            c.write32(p + 16, memory);
            return kDDOk;
        }
        // D3DEXECUTEDATA: size, vertex offset, vertex count, instruction
        // offset, instruction length, H-vertex offset, status.
        if (m == "SetExecuteData" || m == "GetExecuteData") {
            const uint32_t p = c.arg(1);
            if (!c.valid(p, 24))
                return kDDErrInvalidParams;
            if (m == "SetExecuteData") {
                vertexOffset = c.read32(p + 4);
                vertexCount = c.read32(p + 8);
                instructionOffset = c.read32(p + 12);
                instructionLength = c.read32(p + 16);
            } else {
                c.write32(p + 4, vertexOffset);
                c.write32(p + 8, vertexCount);
                c.write32(p + 12, instructionOffset);
                c.write32(p + 16, instructionLength);
            }
            return kDDOk;
        }
        if (m == "Unlock" || m == "Initialize" || m == "Validate" || m == "Optimize")
            return kDDOk;
        return std::nullopt;
    }
};

// An IDirect3DDevice: Direct3D 1's, asked of a surface by its device's
// IID. Its execute buffers run through a Direct3D 7 Device on the surface:
// their vertices processed into a pool in the process's memory, their
// triangles drawn from it, their states set on the Device.
class LegacyDevice : public ComObject {
public:
    DirectX::State *s = nullptr;
    std::shared_ptr<Device> device;
    std::string kind; // the device's IID
    std::map<uint32_t, Matrix> matrices;
    uint32_t nextMatrix = 1;
    std::vector<std::shared_ptr<LegacyViewport>> viewports;
    uint32_t pool = 0;    // kPoolVertices vertices
    uint32_t poolFvf = 0; // the layout of what's in it
    uint32_t indices = 0; // room for the triangles' indices
    uint32_t indexRoom = 0;
    uint32_t status = 0; // D3DSTATUS's dwStatus, for BRANCHFORWARD

    ~LegacyDevice() override {
        if (pool)
            s->process.memory().free(pool);
        if (indices)
            s->process.memory().free(indices);
    }
    const ComInterface *query(const std::string &iid) override {
        return iid == s->interfaces.device1.iid ? &s->interfaces.device1 : nullptr;
    }
    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override;
    void execute(Cpu &c, ExecuteBuffer &b);
    void renderState(uint32_t state, uint32_t value);
    // Draws `count` primitives of `type` (1 points, 2 lines, 4 triangles)
    // whose vertex indices are `per` WORDs each, `stride` bytes apart.
    void drawIndexed(Cpu &c, uint32_t type, uint32_t at, uint32_t count, uint32_t stride, uint32_t per);
};

std::optional<uint32_t> LegacyViewport::invoke(const ComInterface &iface, int index, Cpu &c) {
    const std::string_view m = iface.methods[size_t(index)].name;
    // D3DVIEWPORT: size, x, y, width, height, scale x and y, max x and y,
    // min and max z. D3DVIEWPORT2: size, x, y, width, height, the clip
    // volume's x, y, width and height, min and max z.
    if (m == "SetViewport" || m == "SetViewport2") {
        const uint32_t p = c.arg(1);
        if (!c.valid(p, 44))
            return kDDErrInvalidParams;
        viewport.x = int(c.read32(p + 4));
        viewport.y = int(c.read32(p + 8));
        viewport.width = int(c.read32(p + 12));
        viewport.height = int(c.read32(p + 16));
        viewport.minZ = asFloat1(c.read32(p + 36));
        viewport.maxZ = asFloat1(c.read32(p + 40));
        if (viewport.maxZ <= viewport.minZ) {
            viewport.minZ = 0;
            viewport.maxZ = 1;
        }
        set = true;
        return kDDOk;
    }
    if (m == "GetViewport" || m == "GetViewport2") {
        const uint32_t p = c.arg(1);
        if (!c.valid(p, 44))
            return kDDErrInvalidParams;
        c.write32(p + 4, uint32_t(viewport.x));
        c.write32(p + 8, uint32_t(viewport.y));
        c.write32(p + 12, uint32_t(viewport.width));
        c.write32(p + 16, uint32_t(viewport.height));
        c.write32(p + 36, asBits1(viewport.minZ));
        c.write32(p + 40, asBits1(viewport.maxZ));
        return kDDOk;
    }
    if (m == "SetBackground") {
        background = c.arg(1);
        return kDDOk;
    }
    if (m == "GetBackground") {
        if (c.valid(c.arg(1), 4))
            c.write32(c.arg(1), background);
        if (c.valid(c.arg(2), 4))
            c.write32(c.arg(2), background ? 1 : 0);
        return kDDOk;
    }
    // Clear(count, D3DRECTs, flags): D3DCLEAR_TARGET 1 to the background
    // material's diffuse colour, D3DCLEAR_ZBUFFER 2 to the far plane.
    if (m == "Clear") {
        if (!device || !device->device)
            return kDDErrInvalidParams;
        uint32_t color = 0;
        if (auto it = s->materialHandles.find(background); it != s->materialHandles.end())
            if (auto material = std::dynamic_pointer_cast<LegacyMaterial>(it->second.lock()))
                color = argbOf(material->material.diffuse);
        Device &d = *device->device;
        if (set)
            d.viewport = viewport;
        d.clear(c.arg(1), c.arg(2), c.arg(3) & 3, color, 1.0f);
        return kDDOk;
    }
    if (m == "Initialize" || m == "SetBackgroundDepth" || m == "AddLight" || m == "DeleteLight")
        return kDDOk;
    return std::nullopt;
}

void LegacyDevice::renderState(uint32_t state, uint32_t value) {
    Device &d = *device;
    auto &stage = d.stageStates[0];
    switch (state) {
    case kRsTextureHandle: { // the texture drawn with, by its handle; 0 for none
        auto it = s->textureHandles.find(value);
        d.textures[0] = it == s->textureHandles.end() ? nullptr : it->second.lock();
        break;
    }
    case kRsTextureAddress:
        stage[kTssAddressU] = stage[kTssAddressV] = value;
        break;
    case kRsTextureAddressU:
        stage[kTssAddressU] = value;
        break;
    case kRsTextureAddressV:
        stage[kTssAddressV] = value;
        break;
    case kRsBorderColor:
        stage[kTssBorderColor] = value;
        break;
    case kRsTextureMag: // D3DFILTER_NEAREST 1, LINEAR 2...
        stage[kTssMagFilter] = value == 1 || value == 3 || value == 5 ? 1 : 2;
        break;
    case kRsTextureMin: // NEAREST, LINEAR, MIPNEAREST, MIPLINEAR, LINEARMIPNEAREST, LINEARMIPLINEAR
        stage[kTssMinFilter] = value == 1 || value == 3 || value == 4 ? 1 : 2;
        stage[kTssMipFilter] = value <= 2 ? 1 : value == 3 || value == 5 ? 2 : 3;
        break;
    case kRsTextureMapBlend: {
        // As the DirectX 6 documentation maps the legacy blends to stages:
        // DECAL and COPY the texture; MODULATE its colour times the
        // vertex's, the alpha the texture's if it has one; DECALALPHA the
        // texture over the vertex's colour by its alpha; MODULATEALPHA
        // both modulated; ADD the colours added.
        const bool textureAlpha = d.textures[0] && (d.textures[0]->d.format.flags & kPfAlphaPixels);
        stage[kTssColorArg1] = 2; // the texture
        stage[kTssColorArg2] = 0; // the vertex's colour
        stage[kTssAlphaArg1] = 2;
        stage[kTssAlphaArg2] = 0;
        switch (value) {
        case 1: // DECAL
        case 7: // COPY
            stage[kTssColorOp] = 2;
            stage[kTssAlphaOp] = 2;
            break;
        case 3:                      // DECALALPHA
            stage[kTssColorOp] = 13; // BLENDTEXTUREALPHA
            stage[kTssAlphaOp] = 3;
            break;
        case 4: // MODULATEALPHA
            stage[kTssColorOp] = 4;
            stage[kTssAlphaOp] = 4;
            break;
        case 8: // ADD
            stage[kTssColorOp] = 7;
            stage[kTssAlphaOp] = 3;
            break;
        default: // MODULATE
            stage[kTssColorOp] = 4;
            stage[kTssAlphaOp] = textureAlpha ? 2 : 3;
            break;
        }
        break;
    }
    default:
        if (state < d.renderStates.size())
            d.renderStates[state] = value;
        break;
    }
    // No texture: the vertex's colour alone.
    if (state == kRsTextureHandle && !d.textures[0]) {
        stage[kTssColorOp] = 3; // SELECTARG2: the diffuse colour
        stage[kTssAlphaOp] = 3;
    } else if (state == kRsTextureHandle && stage[kTssColorOp] == 3 && stage[kTssColorArg2] == 0 &&
               d.renderStates[kRsTextureMapBlend] == 0) {
        stage[kTssColorOp] = 4;
        stage[kTssAlphaOp] = 2;
    }
    if (state == kRsTextureMapBlend)
        d.renderStates[kRsTextureMapBlend] = value;
}

void LegacyDevice::drawIndexed(Cpu &c, uint32_t type, uint32_t at, uint32_t count, uint32_t stride, uint32_t per) {
    if (!count || !poolFvf)
        return;
    const uint32_t n = count * per;
    if (n > indexRoom) {
        if (indices)
            s->process.memory().free(indices);
        indexRoom = std::max(n, 4096u);
        indices = s->process.memory().alloc(indexRoom * 2);
        if (!indices) {
            indexRoom = 0;
            return;
        }
    }
    uint32_t highest = 0;
    for (uint32_t i = 0; i < count; ++i)
        for (uint32_t k = 0; k < per; ++k) {
            const uint16_t v = c.read16(at + i * stride + 2 * k);
            highest = std::max<uint32_t>(highest, v);
            c.write16(indices + 2 * (i * per + k), v);
        }
    if (highest >= kPoolVertices)
        return;
    device->draw(type, poolFvf, pool, kVertexSize, highest + 1, indices, n);
}

// The instructions (D3DINSTRUCTION: opcode, size of each item, count of
// them, the items after it) from the buffer's instruction offset.
void LegacyDevice::execute(Cpu &c, ExecuteBuffer &b) {
    Device &d = *device;
    uint32_t at = b.memory + b.instructionOffset;
    const uint32_t end = at + b.instructionLength;
    if (!c.valid(b.memory, b.size) || b.instructionOffset + b.instructionLength > b.size)
        return;
    const uint32_t vertices = b.memory + b.vertexOffset;
    while (at + 4 <= end) {
        const uint8_t op = c.read8(at);
        const uint32_t size = c.read8(at + 1);
        const uint32_t count = c.read16(at + 2);
        const uint32_t items = at + 4;
        const uint32_t next = items + size * count;
        if (next > end)
            break;
        switch (op) {
        case 1: // POINT: D3DPOINT, a count of vertices and the first
            for (uint32_t i = 0; i < count; ++i) {
                const uint32_t first = c.read16(items + i * size + 2);
                const uint32_t n = c.read16(items + i * size);
                if (first + n <= kPoolVertices && poolFvf)
                    d.draw(1, poolFvf, pool + first * kVertexSize, kVertexSize, n, 0, 0);
            }
            break;
        case 2: // LINE: D3DLINE, two vertex indices
            drawIndexed(c, 2, items, count, size, 2);
            break;
        case 3: // TRIANGLE: D3DTRIANGLE, three vertex indices and flags
            drawIndexed(c, 4, items, count, size, 3);
            break;
        case 4: // MATRIXLOAD: D3DMATRIXLOAD, destination and source handles
            for (uint32_t i = 0; i < count; ++i)
                matrices[c.read32(items + i * size)] = matrices[c.read32(items + i * size + 4)];
            break;
        case 5: // MATRIXMULTIPLY: D3DMATRIXMULTIPLY, destination = source 1 x source 2
            for (uint32_t i = 0; i < count; ++i) {
                const uint32_t p = items + i * size;
                matrices[c.read32(p)] = multiply(matrices[c.read32(p + 4)], matrices[c.read32(p + 8)]);
            }
            break;
        case 6: // STATETRANSFORM: D3DSTATE, WORLD 1, VIEW 2, PROJECTION 3, and a matrix handle
            for (uint32_t i = 0; i < count; ++i) {
                const uint32_t which = c.read32(items + i * size);
                auto it = matrices.find(c.read32(items + i * size + 4));
                if (which >= 1 && which <= 3)
                    d.transforms[which] = it == matrices.end() ? identity() : it->second;
            }
            break;
        case 7: // STATELIGHT: D3DSTATE, MATERIAL 1 (a handle), AMBIENT 2
            for (uint32_t i = 0; i < count; ++i) {
                const uint32_t which = c.read32(items + i * size);
                const uint32_t value = c.read32(items + i * size + 4);
                if (which == 1) {
                    if (auto it = s->materialHandles.find(value); it != s->materialHandles.end())
                        if (auto m = std::dynamic_pointer_cast<LegacyMaterial>(it->second.lock()))
                            d.material = m->material;
                } else if (which == 2) {
                    d.renderStates[139] = value; // AMBIENT
                }
            }
            break;
        case 8: // STATERENDER: D3DSTATE, a render state and its value
            for (uint32_t i = 0; i < count; ++i)
                renderState(c.read32(items + i * size), c.read32(items + i * size + 4));
            break;
        case 9: // PROCESSVERTICES: flags, start, destination, count
            for (uint32_t i = 0; i < count; ++i) {
                const uint32_t p = items + i * size;
                const uint32_t flags = c.read32(p);
                const uint32_t start = c.read16(p + 4);
                const uint32_t dest = c.read16(p + 6);
                const uint32_t n = c.read32(p + 8);
                // D3DPROCESSVERTICES_TRANSFORMLIGHT 0, TRANSFORM 1, COPY 2.
                const uint32_t kind = flags & 7;
                poolFvf = kind == 2 ? kFvfTlVertex : kind == 1 ? kFvfLVertex : kFvfVertex;
                d.renderStates[137] = kind == 0 ? 1 : 0; // LIGHTING
                if (dest + n > kPoolVertices || !c.valid(vertices + start * kVertexSize, n * kVertexSize))
                    continue;
                std::memmove(c.memory().data() + pool + dest * kVertexSize,
                             c.memory().data() + vertices + start * kVertexSize, n * kVertexSize);
            }
            break;
        case 10: // TEXTURELOAD: destination and source texture handles
            for (uint32_t i = 0; i < count; ++i) {
                auto to = s->textureHandles.find(c.read32(items + i * size));
                auto from = s->textureHandles.find(c.read32(items + i * size + 4));
                if (to != s->textureHandles.end() && from != s->textureHandles.end())
                    if (auto t = to->second.lock())
                        if (auto f = from->second.lock()) {
                            t->copy(*f, f->bounds(), t->bounds(), false, 0, 0);
                            t->palette = f->palette;
                        }
            }
            break;
        case 11: // EXIT
            return;
        case 12: { // BRANCHFORWARD: D3DBRANCH, mask, value, negate, offset (0: exit)
            const uint32_t mask = c.read32(items);
            const uint32_t value = c.read32(items + 4);
            const bool negate = c.read32(items + 8) != 0;
            const uint32_t offset = c.read32(items + 12);
            if (((status & mask) == value) != negate) {
                if (!offset)
                    return;
                at += offset;
                continue;
            }
            break;
        }
        case 14: // SETSTATUS: D3DSTATUS, flags, status, extent
            status = c.read32(items + 4);
            break;
        default: // SPAN, and anything else, isn't drawn
            break;
        }
        at = next;
    }
}

std::optional<uint32_t> LegacyDevice::invoke(const ComInterface &iface, int index, Cpu &c) {
    const std::string_view m = iface.methods[size_t(index)].name;
    if (m == "Initialize")
        return kDDOk;
    if (m == "GetCaps") { // (hardware D3DDEVICEDESC, software D3DDEVICEDESC)
        const bool hardware = kind == kIidHalDevice;
        if (c.valid(c.arg(1), kDeviceDescSize))
            writeLegacyDesc(c, c.arg(1), hardware, true);
        if (c.valid(c.arg(2), kDeviceDescSize))
            writeLegacyDesc(c, c.arg(2), true, false);
        return kDDOk;
    }
    if (m == "GetStats") { // D3DSTATS: size, then counts, all 0
        const uint32_t p = c.arg(1);
        if (c.valid(p, 4)) {
            const uint32_t size = std::min(c.read32(p), 64u);
            if (size > 4 && c.valid(p, size))
                std::memset(c.memory().data() + p + 4, 0, size - 4);
        }
        return kDDOk;
    }
    // EnumTextureFormats(callback(DDSURFACEDESC*, context), context)
    if (m == "EnumTextureFormats") {
        const uint32_t desc = s->process.memory().alloc(108);
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
    // CreateExecuteBuffer(D3DEXECUTEBUFFERDESC*, out, outer): its size at +12.
    if (m == "CreateExecuteBuffer") {
        const uint32_t p = c.arg(1);
        if (!c.valid(p, 20) || !c.valid(c.arg(2), 4))
            return kDDErrInvalidParams;
        auto b = std::make_shared<ExecuteBuffer>();
        b->s = s;
        b->size = c.read32(p + 12);
        b->memory = b->size ? s->process.memory().alloc(b->size) : 0;
        if (!b->memory)
            return kDDErrOutOfMemory;
        c.write32(c.arg(2), s->com.pointer(b, s->interfaces.executeBuffer));
        return kDDOk;
    }
    // Execute(buffer, viewport, flags): drawn within the viewport.
    if (m == "Execute") {
        auto b = s->com.shared<ExecuteBuffer>(c.arg(1));
        auto v = s->com.shared<LegacyViewport>(c.arg(2));
        if (!b)
            return kDDErrInvalidParams;
        if (v && v->set)
            device->viewport = v->viewport;
        execute(c, *b);
        return kDDOk;
    }
    if (m == "AddViewport") {
        auto v = s->com.shared<LegacyViewport>(c.arg(1));
        if (!v)
            return kDDErrInvalidParams;
        v->device = this;
        viewports.push_back(v);
        return kDDOk;
    }
    if (m == "DeleteViewport") {
        auto v = s->com.shared<LegacyViewport>(c.arg(1));
        std::erase(viewports, v);
        if (v)
            v->device = nullptr;
        return kDDOk;
    }
    if (m == "CreateMatrix") {
        const uint32_t handle = nextMatrix++;
        matrices[handle] = identity();
        if (c.valid(c.arg(1), 4))
            c.write32(c.arg(1), handle);
        return kDDOk;
    }
    if (m == "SetMatrix" || m == "GetMatrix") { // (handle, D3DMATRIX*)
        const uint32_t p = c.arg(2);
        if (!c.valid(p, 64))
            return kDDErrInvalidParams;
        Matrix &mx = matrices[c.arg(1)];
        for (uint32_t i = 0; i < 16; ++i) {
            if (m == "SetMatrix")
                mx[i] = asFloat1(c.read32(p + 4 * i));
            else
                c.write32(p + 4 * i, asBits1(mx[i]));
        }
        return kDDOk;
    }
    if (m == "DeleteMatrix") {
        matrices.erase(c.arg(1));
        return kDDOk;
    }
    if (m == "BeginScene" || m == "EndScene") {
        if (m == "BeginScene" && !s->fpuPreserve)
            c.fpu.control &= ~0x300; // single precision, as Direct3D leaves it
        if (m == "EndScene")
            device->endScene();
        return kDDOk;
    }
    if (m == "GetDirect3D") {
        ++s->directDraw->refs;
        if (c.valid(c.arg(1), 4))
            c.write32(c.arg(1), s->com.pointer(s->directDraw, s->interfaces.direct3d));
        return kDDOk;
    }
    if (m == "SwapTextureHandles") {
        auto a = s->com.shared<Surface>(c.arg(1));
        auto b = s->com.shared<Surface>(c.arg(2));
        if (a && b) {
            std::swap(a->textureHandle, b->textureHandle);
            if (a->textureHandle)
                s->textureHandles[a->textureHandle] = a;
            if (b->textureHandle)
                s->textureHandles[b->textureHandle] = b;
        }
        return kDDOk;
    }
    return std::nullopt;
}

} // namespace

std::shared_ptr<ComObject> Surface::aggregate(const std::string &iid, const ComInterface *&iface) {
    iface = nullptr;
    if (iid != kIidHalDevice && iid != kIidRgbDevice && iid != kIidMmxDevice && iid != kIidRampDevice &&
        iid != kIidRefDevice)
        return nullptr;
    // Drawn into: a surface made for 3D, in an RGB format of 16 or 32 bits.
    if (!(d.format.flags & kPfRgb) || (d.format.bits != 16 && d.format.bits != 32))
        return nullptr;
    if (!legacyDevice) {
        auto l = std::make_shared<LegacyDevice>();
        l->s = s;
        l->kind = iid;
        l->device = std::make_shared<Device>(s, std::static_pointer_cast<Surface>(shared_from_this()));
        l->device->renderStates[22] = 1; // CULLMODE: none, Direct3D 1's default
        l->pool = s->process.memory().alloc(kPoolVertices * kVertexSize);
        if (!l->pool)
            return nullptr;
        l->refs = 0; // QueryInterface counts the program's
        legacyDevice = l;
    }
    iface = &s->interfaces.device1;
    return legacyDevice;
}

std::optional<uint32_t> Surface::invokeTexture(const ComInterface &iface, int index, Cpu &c) {
    const std::string_view m = iface.methods[size_t(index)].name;
    if (m == "GetHandle") { // (device, handle out)
        if (!textureHandle) {
            textureHandle = s->nextHandle++;
            s->textureHandles[textureHandle] = std::static_pointer_cast<Surface>(shared_from_this());
        }
        if (c.valid(c.arg(2), 4))
            c.write32(c.arg(2), textureHandle);
        return kDDOk;
    }
    if (m == "Load") { // (source texture): its pixels and palette copied here
        auto from = s->com.shared<Surface>(c.arg(1));
        if (!from)
            return kDDErrInvalidParams;
        copy(*from, from->bounds(), bounds(), false, 0, 0);
        if (from->palette)
            palette = from->palette;
        touched();
        return kDDOk;
    }
    if (m == "Initialize" || m == "PaletteChanged" || m == "Unload")
        return kDDOk;
    return std::nullopt;
}

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
        for (const DeviceKind5 &k : kLegacyDevices) {
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
    // CreateViewport, CreateMaterial, CreateLight(out, outer)
    if (m == "CreateViewport" || m == "CreateMaterial" || m == "CreateLight") {
        if (!c.valid(c.arg(1), 4))
            return kDDErrInvalidParams;
        std::shared_ptr<ComObject> made;
        const ComInterface *f = nullptr;
        const bool second = &iface == &s->interfaces.direct3d2;
        if (m == "CreateViewport") {
            auto v = std::make_shared<LegacyViewport>();
            v->s = s;
            made = v;
            f = second ? &s->interfaces.viewport2 : &s->interfaces.viewport;
        } else if (m == "CreateMaterial") {
            auto mat = std::make_shared<LegacyMaterial>();
            mat->s = s;
            made = mat;
            f = second ? &s->interfaces.material2 : &s->interfaces.material;
        } else {
            auto l = std::make_shared<LegacyLight>();
            l->s = s;
            made = l;
            f = &s->interfaces.light;
        }
        c.write32(c.arg(1), s->com.pointer(made, *f));
        return kDDOk;
    }
    return std::nullopt;
}

} // namespace reluva::x86::dx
