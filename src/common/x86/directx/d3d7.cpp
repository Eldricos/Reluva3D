// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// IDirect3D7, IDirect3DDevice7's states, and vertex buffers, as the DirectX
// 7 documentation describes them. The drawing is raster.cpp's.

#include "common/x86/directx/device.h"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace reluva::x86::dx {

namespace {

float asFloat(uint32_t v) {
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}
uint32_t asBits(float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}
Color readColor(Cpu& c, uint32_t p) {
    return {asFloat(c.read32(p)), asFloat(c.read32(p + 4)), asFloat(c.read32(p + 8)), asFloat(c.read32(p + 12))};
}
void writeColor(Cpu& c, uint32_t p, const Color& v) {
    c.write32(p, asBits(v.r));
    c.write32(p + 4, asBits(v.g));
    c.write32(p + 8, asBits(v.b));
    c.write32(p + 12, asBits(v.a));
}
Matrix readMatrix(Cpu& c, uint32_t p) {
    Matrix m;
    for (uint32_t i = 0; i < 16; ++i)
        m[i] = asFloat(c.read32(p + 4 * i));
    return m;
}
void writeMatrix(Cpu& c, uint32_t p, const Matrix& m) {
    for (uint32_t i = 0; i < 16; ++i)
        c.write32(p + 4 * i, asBits(m[i]));
}

// The devices offered, best first as programs expect to find them listed
// last: software emulation, then the hardware one.
struct DeviceKind {
    const char* description;
    const char* name;
    const char* iid;
    bool hardware;
};
const DeviceKind kDevices[] = {
    {"Microsoft Direct3D RGB Software Emulation", "RGB Emulation", kIidRgbDevice, false},
    {"Microsoft Direct3D Hardware acceleration through Direct3D HAL", "Direct3D HAL", kIidHalDevice, true},
};

void writePrimCaps(Cpu& c, uint32_t p) {
    const uint32_t v[14] = {
        56,
        0x2 | 0x10 | 0x20 | 0x40,                                         // misc: MASKZ, CULLNONE, CULLCW, CULLCCW
        0x1 | 0x10 | 0x80 | 0x100 | 0x2000 | 0x10000 | 0x100000 | 0x200000, // raster: DITHER, ZTEST, fog, LOD bias
        0xff,                                                             // Z compare: all
        0x1fff, 0x1fff,                                                   // source and destination blends
        0xff,                                                             // alpha compare: all
        0x2 | 0x8 | 0x40 | 0x200 | 0x1000 | 0x4000 | 0x80000,             // shading: flat and Gouraud colour, alpha, fog
        0x1 | 0x4 | 0x8 | 0x40 | 0x80,             // textures: PERSPECTIVE, ALPHA, TRANSPARENCY, any size repeats
        0x0303033f,                                // filters: point, linear, mip
        0xff,                                      // legacy texture blends
        0x1f,                                      // addressing: wrap, mirror, clamp, border, per axis
        0, 0};
    for (uint32_t i = 0; i < 14; ++i)
        c.write32(p + 4 * i, v[i]);
}

} // namespace

void writeDeviceDesc(Cpu& c, uint32_t p, const std::string& iid) {
    std::memset(c.memory().data() + p, 0, 236);
    const bool hardware = iid == kIidHalDevice;
    c.write32(p, 0x1 | 0x10 | 0x40 | 0x100 | 0x200 | 0x400 | 0x800 | 0x1000 | 0x2000 | 0x8000 | (hardware ? 0x80000 : 0));
    writePrimCaps(c, p + 4);  // lines
    writePrimCaps(c, p + 60); // triangles
    c.write32(p + 116, 0x400 | 0x100);         // render target depths: 16, 32
    c.write32(p + 120, 0x400 | 0x200 | 0x100); // Z buffer depths: 16, 24, 32
    c.write32(p + 124, 1);
    c.write32(p + 128, 1);
    c.write32(p + 132, 2048);
    c.write32(p + 136, 2048);
    c.write32(p + 140, 8192); // texture repeat
    c.write32(p + 144, 2048); // aspect ratio
    c.write32(p + 148, 1);    // anisotropy
    c.write32(p + 176, 8);    // FVF: 8 texture coordinate sets
    c.write32(p + 180, 0xffff); // texture operations: DISABLE to BLENDCURRENTALPHA
    c.write16(p + 184, 4);      // blend stages
    c.write16(p + 186, 2);      // simultaneous textures
    c.write32(p + 188, 8);      // active lights
    c.write32(p + 192, asBits(1e10f));
    ComHost::writeGuid(c, p + 196, iid);
    c.write32(p + 216, 0x1 | 0x2 | 0x8 | 0x10 | 0x20); // vertex processing
}

Matrix identity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }

Matrix multiply(const Matrix& a, const Matrix& b) {
    Matrix r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float v = 0;
            for (int k = 0; k < 4; ++k)
                v += a[size_t(i * 4 + k)] * b[size_t(k * 4 + j)];
            r[size_t(i * 4 + j)] = v;
        }
    return r;
}

VertexLayout::VertexLayout(uint32_t fvf) {
    const uint32_t pos = fvf & 0xe;
    rhw = pos == 0x4;
    position = pos == 0x2 ? 3 : pos == 0x4 ? 4 : pos >= 0x6 ? 3 + int((pos - 0x6) / 2 + 1) : 0;
    int at = position * 4;
    normal = fvf & 0x10;
    if (normal) {
        normalAt = at;
        at += 12;
    }
    reserved = fvf & 0x20;
    if (reserved)
        at += 4;
    diffuse = fvf & 0x40;
    if (diffuse) {
        diffuseAt = at;
        at += 4;
    }
    specular = fvf & 0x80;
    if (specular) {
        specularAt = at;
        at += 4;
    }
    texSets = int((fvf >> 8) & 0xf);
    for (int i = 0; i < texSets && i < 8; ++i) {
        const uint32_t code = (fvf >> (16 + 2 * i)) & 3; // 0: 2 floats, 1: 3, 2: 4, 3: 1
        texSize[i] = code == 0 ? 2 : code == 1 ? 3 : code == 2 ? 4 : 1;
        texAt[i] = at;
        at += texSize[i] * 4;
    }
    size = at;
}

// --- IDirect3D7 (on the DirectDraw object)

std::optional<uint32_t> DirectDraw::invoke3d(const ComInterface& iface, int index, Cpu& c) {
    const std::string_view m = iface.methods[size_t(index)].name;
    // EnumDevices(callback(description, name, D3DDEVICEDESC7*, context), context)
    if (m == "EnumDevices") {
        const uint32_t callback = c.arg(1), context = c.arg(2);
        const uint32_t desc = s->process.memory().alloc(236 + 256);
        for (const DeviceKind& k : kDevices) {
            if (k.hardware && emulationOnly)
                continue;
            writeDeviceDesc(c, desc, k.iid);
            c.writeString(desc + 236, k.description, 128);
            c.writeString(desc + 236 + 128, k.name, 128);
            uint32_t keepGoing = 0;
            if (Error e = c.call(callback, {desc + 236, desc + 236 + 128, desc, context}, keepGoing))
                break;
            if (!keepGoing)
                break;
        }
        s->process.memory().free(desc);
        return kDDOk;
    }
    // EnumZBufferFormats(device iid, callback(DDPIXELFORMAT*, context), context)
    if (m == "EnumZBufferFormats") {
        const uint32_t callback = c.arg(2), context = c.arg(3);
        const uint32_t pf = s->process.memory().alloc(32);
        for (int bits : {16, 24, 32}) {
            PixelFormat f = zFormat(bits == 24 ? 32 : bits);
            if (bits == 24)
                f.g = 0xffffff;
            f.write(c, pf);
            uint32_t keepGoing = 0;
            if (Error e = c.call(callback, {pf, context}, keepGoing))
                break;
            if (!keepGoing)
                break;
        }
        s->process.memory().free(pf);
        return kDDOk;
    }
    // CreateDevice(class, render target, out)
    if (m == "CreateDevice") {
        const std::string kind = ComHost::guid(c, c.arg(1));
        std::shared_ptr<Surface> target = s->com.shared<Surface>(c.arg(2));
        const uint32_t out = c.arg(3);
        if (!target || !c.valid(out, 4))
            return kDDErrInvalidParams;
        if (kind != kIidHalDevice && kind != kIidRgbDevice && kind != kIidMmxDevice && kind != kIidRefDevice &&
            kind != kIidTnLHalDevice)
            return kDDErrInvalidParams;
        auto device = std::make_shared<Device>(s, target);
        c.write32(out, s->com.pointer(device, s->interfaces.device7));
        return kDDOk;
    }
    // CreateVertexBuffer(D3DVERTEXBUFFERDESC*, out, flags)
    if (m == "CreateVertexBuffer") {
        const uint32_t desc = c.arg(1), out = c.arg(2);
        if (!c.valid(desc, 16) || !c.valid(out, 4))
            return kDDErrInvalidParams;
        auto vb = std::make_shared<VertexBuffer>();
        vb->s = s;
        vb->caps = c.read32(desc + 4);
        vb->fvf = c.read32(desc + 8);
        vb->count = c.read32(desc + 12);
        vb->memory = s->process.memory().alloc(uint32_t(VertexLayout(vb->fvf).size) * std::max(vb->count, 1u));
        if (!vb->memory)
            return kDDErrOutOfMemory;
        c.write32(out, s->com.pointer(vb, s->interfaces.vertexBuffer7));
        return kDDOk;
    }
    if (m == "EvictManagedTextures")
        return kDDOk;
    return std::nullopt;
}

// --- Vertex buffers

VertexBuffer::~VertexBuffer() {
    if (s && memory)
        s->process.memory().free(memory);
}

const ComInterface* VertexBuffer::query(const std::string& iid) {
    return iid == s->interfaces.vertexBuffer7.iid ? &s->interfaces.vertexBuffer7 : nullptr;
}

std::optional<uint32_t> VertexBuffer::invoke(const ComInterface& iface, int index, Cpu& c) {
    const std::string_view m = iface.methods[size_t(index)].name;
    if (m == "Lock") { // (flags, data out, size out)
        if (c.arg(2))
            c.write32(c.arg(2), memory);
        if (c.arg(3))
            c.write32(c.arg(3), uint32_t(VertexLayout(fvf).size) * count);
        return kDDOk;
    }
    if (m == "Unlock" || m == "Optimize")
        return kDDOk;
    if (m == "GetVertexBufferDesc") {
        const uint32_t p = c.arg(1);
        c.write32(p, 16);
        c.write32(p + 4, caps);
        c.write32(p + 8, fvf);
        c.write32(p + 12, count);
        return kDDOk;
    }
    return std::nullopt;
}

// --- The device's states

Device::Device(DirectX::State* state, std::shared_ptr<Surface> renderTarget) : target(std::move(renderTarget)) {
    s = state;
    s->devices.insert(this);
    for (const auto& a : target->attached)
        if (a->d.caps[0] & kCapsZBuffer)
            depth = a;
    // Direct3D 7's defaults.
    renderStates[7] = depth ? 1 : 0;     // ZENABLE
    renderStates[8] = 3;                 // FILLMODE: solid
    renderStates[9] = 2;                 // SHADEMODE: Gouraud
    renderStates[14] = 1;                // ZWRITEENABLE
    renderStates[19] = 2;                // SRCBLEND: one
    renderStates[20] = 1;                // DESTBLEND: zero
    renderStates[22] = 3;                // CULLMODE: counter-clockwise
    renderStates[23] = 4;                // ZFUNC: less or equal
    renderStates[25] = 8;                // ALPHAFUNC: always
    renderStates[37] = asBits(1.0f);     // FOGEND
    renderStates[38] = asBits(1.0f);     // FOGDENSITY
    renderStates[60] = 0xffffffffu;      // TEXTUREFACTOR
    renderStates[136] = 1;               // CLIPPING
    renderStates[137] = 1;               // LIGHTING
    renderStates[141] = 1;               // COLORVERTEX
    renderStates[142] = 1;               // LOCALVIEWER
    renderStates[145] = 1;               // DIFFUSEMATERIALSOURCE: vertex colour 1
    renderStates[146] = 2;               // SPECULARMATERIALSOURCE: vertex colour 2
    for (uint32_t i = 0; i < 8; ++i) {
        auto& st = stageStates[i];
        st[1] = i == 0 ? 4 : 1; // COLOROP: modulate on the first stage
        st[2] = 2;              // COLORARG1: texture
        st[3] = 1;              // COLORARG2: current
        st[4] = i == 0 ? 2 : 1; // ALPHAOP: the texture's on the first stage
        st[5] = 2;
        st[6] = 1;
        st[11] = i;             // TEXCOORDINDEX
        st[12] = st[13] = st[14] = 1; // ADDRESS(U, V): wrap
        st[16] = st[17] = 1;          // MAG/MINFILTER: point
        st[18] = 1;                   // MIPFILTER: none
    }
    for (uint32_t t : {1u, 2u, 3u})
        transforms[t] = identity();
    for (uint32_t t = 16; t < 24; ++t)
        transforms[t] = identity();
    viewport.width = target->d.width;
    viewport.height = target->d.height;
}

const ComInterface* Device::query(const std::string& iid) {
    return iid == s->interfaces.device7.iid ? &s->interfaces.device7 : nullptr;
}

void Device::setRenderState(uint32_t state, uint32_t value) {
    if (state < renderStates.size())
        renderStates[state] = value;
}

std::optional<uint32_t> Device::invoke(const ComInterface& iface, int index, Cpu& c) {
    const std::string_view m = iface.methods[size_t(index)].name;
    if (m == "GetCaps") {
        writeDeviceDesc(c, c.arg(1), kIidHalDevice);
        return kDDOk;
    }
    if (m == "EnumTextureFormats") { // (callback(DDPIXELFORMAT*, context), context)
        const uint32_t pf = s->process.memory().alloc(32);
        for (const PixelFormat& f : textureFormats()) {
            f.write(c, pf);
            uint32_t keepGoing = 0;
            if (Error e = c.call(c.arg(1), {pf, c.arg(2)}, keepGoing))
                break;
            if (!keepGoing)
                break;
        }
        s->process.memory().free(pf);
        return kDDOk;
    }
    if (m == "BeginScene" || m == "EndScene" || m == "PreLoad")
        return kDDOk;
    if (m == "GetDirect3D") {
        ++s->directDraw->refs;
        c.write32(c.arg(1), s->com.pointer(s->directDraw, s->interfaces.direct3d7));
        return kDDOk;
    }
    if (m == "SetRenderTarget") {
        std::shared_ptr<Surface> t = s->com.shared<Surface>(c.arg(1));
        if (!t)
            return kDDErrInvalidParams;
        target = t;
        depth = nullptr;
        for (const auto& a : target->attached)
            if (a->d.caps[0] & kCapsZBuffer)
                depth = a;
        return kDDOk;
    }
    if (m == "GetRenderTarget") {
        ++target->refs;
        c.write32(c.arg(1), s->com.pointer(target, s->interfaces.surface7));
        return kDDOk;
    }
    if (m == "Clear") { // (count, D3DRECT*, flags, colour, z, stencil)
        clear(c.arg(1), c.arg(2), c.arg(3), c.arg(4), asFloat(c.arg(5)));
        return kDDOk;
    }
    if (m == "SetTransform" || m == "MultiplyTransform") {
        const Matrix given = readMatrix(c, c.arg(2));
        auto it = transforms.find(c.arg(1));
        const Matrix current = it == transforms.end() ? identity() : it->second;
        Matrix& t = transforms[c.arg(1)];
        t = m == "SetTransform" ? given : multiply(given, current);
        if (recording)
            recording->transforms[c.arg(1)] = t;
        return kDDOk;
    }
    if (m == "GetTransform") {
        auto it = transforms.find(c.arg(1));
        writeMatrix(c, c.arg(2), it == transforms.end() ? identity() : it->second);
        return kDDOk;
    }
    if (m == "SetViewport") { // D3DVIEWPORT7: x, y, width, height, min z, max z
        const uint32_t p = c.arg(1);
        viewport = {int(c.read32(p)), int(c.read32(p + 4)), int(c.read32(p + 8)), int(c.read32(p + 12)),
                    asFloat(c.read32(p + 16)), asFloat(c.read32(p + 20))};
        if (recording)
            recording->viewport = viewport;
        return kDDOk;
    }
    if (m == "GetViewport") {
        const uint32_t p = c.arg(1);
        c.write32(p, uint32_t(viewport.x));
        c.write32(p + 4, uint32_t(viewport.y));
        c.write32(p + 8, uint32_t(viewport.width));
        c.write32(p + 12, uint32_t(viewport.height));
        c.write32(p + 16, asBits(viewport.minZ));
        c.write32(p + 20, asBits(viewport.maxZ));
        return kDDOk;
    }
    if (m == "SetMaterial" || m == "GetMaterial") {
        const uint32_t p = c.arg(1);
        if (m == "SetMaterial") {
            material = {readColor(c, p), readColor(c, p + 16), readColor(c, p + 32), readColor(c, p + 48),
                        asFloat(c.read32(p + 64))};
            if (recording)
                recording->material = material;
        } else {
            writeColor(c, p, material.diffuse);
            writeColor(c, p + 16, material.ambient);
            writeColor(c, p + 32, material.specular);
            writeColor(c, p + 48, material.emissive);
            c.write32(p + 64, asBits(material.power));
        }
        return kDDOk;
    }
    if (m == "SetLight") { // (index, D3DLIGHT7*)
        const uint32_t p = c.arg(2);
        Light& l = lights[c.arg(1)];
        l.type = c.read32(p);
        l.diffuse = readColor(c, p + 4);
        l.specular = readColor(c, p + 20);
        l.ambient = readColor(c, p + 36);
        for (uint32_t i = 0; i < 3; ++i) {
            l.position[i] = asFloat(c.read32(p + 52 + 4 * i));
            l.direction[i] = asFloat(c.read32(p + 64 + 4 * i));
        }
        l.range = asFloat(c.read32(p + 76));
        l.falloff = asFloat(c.read32(p + 80));
        l.att0 = asFloat(c.read32(p + 84));
        l.att1 = asFloat(c.read32(p + 88));
        l.att2 = asFloat(c.read32(p + 92));
        l.theta = asFloat(c.read32(p + 96));
        l.phi = asFloat(c.read32(p + 100));
        if (recording)
            recording->lights[c.arg(1)] = l;
        return kDDOk;
    }
    if (m == "LightEnable") {
        lights[c.arg(1)].enabled = c.arg(2) != 0;
        if (recording)
            recording->lights[c.arg(1)] = lights[c.arg(1)];
        return kDDOk;
    }
    if (m == "GetLightEnable") {
        auto it = lights.find(c.arg(1));
        c.write32(c.arg(2), it != lights.end() && it->second.enabled ? 1 : 0);
        return kDDOk;
    }
    if (m == "SetRenderState") {
        setRenderState(c.arg(1), c.arg(2));
        if (recording)
            recording->renderStates[c.arg(1)] = c.arg(2);
        return kDDOk;
    }
    if (m == "GetRenderState") {
        c.write32(c.arg(2), c.arg(1) < renderStates.size() ? renderStates[c.arg(1)] : 0);
        return kDDOk;
    }
    if (m == "SetTextureStageState") { // (stage, state, value)
        if (c.arg(1) < 8 && c.arg(2) < 32) {
            stageStates[c.arg(1)][c.arg(2)] = c.arg(3);
            if (c.arg(2) == 12) // ADDRESS sets both
                stageStates[c.arg(1)][13] = stageStates[c.arg(1)][14] = c.arg(3);
        }
        if (recording)
            recording->stageStates[{c.arg(1), c.arg(2)}] = c.arg(3);
        return kDDOk;
    }
    if (m == "GetTextureStageState") {
        c.write32(c.arg(3), c.arg(1) < 8 && c.arg(2) < 32 ? stageStates[c.arg(1)][c.arg(2)] : 0);
        return kDDOk;
    }
    if (m == "SetTexture") { // (stage, surface or 0)
        if (c.arg(1) >= 8)
            return kDDErrInvalidParams;
        textures[c.arg(1)] = c.arg(2) ? s->com.shared<Surface>(c.arg(2)) : nullptr;
        if (recording)
            recording->textures[c.arg(1)] = textures[c.arg(1)];
        return kDDOk;
    }
    if (m == "GetTexture") {
        const auto& t = c.arg(1) < 8 ? textures[c.arg(1)] : nullptr;
        if (t)
            ++t->refs;
        c.write32(c.arg(2), t ? s->com.pointer(t, s->interfaces.surface7) : 0);
        return kDDOk;
    }
    if (m == "ValidateDevice") {
        if (c.arg(1))
            c.write32(c.arg(1), 1);
        return kDDOk;
    }
    // DrawPrimitive(type, fvf, vertices, count, flags)
    if (m == "DrawPrimitive") {
        draw(c.arg(1), c.arg(2), c.arg(3), uint32_t(VertexLayout(c.arg(2)).size), c.arg(4), 0, 0);
        return kDDOk;
    }
    // DrawIndexedPrimitive(type, fvf, vertices, vertex count, indices, index count, flags)
    if (m == "DrawIndexedPrimitive") {
        draw(c.arg(1), c.arg(2), c.arg(3), uint32_t(VertexLayout(c.arg(2)).size), c.arg(4), c.arg(5), c.arg(6));
        return kDDOk;
    }
    // DrawPrimitiveVB(type, buffer, start, count, flags)
    if (m == "DrawPrimitiveVB" || m == "DrawIndexedPrimitiveVB") {
        VertexBuffer* vb = s->com.as<VertexBuffer>(c.arg(2));
        if (!vb)
            return kDDErrInvalidParams;
        const uint32_t stride = uint32_t(VertexLayout(vb->fvf).size);
        const uint32_t start = c.arg(3);
        if (m == "DrawPrimitiveVB")
            draw(c.arg(1), vb->fvf, vb->memory + start * stride, stride, c.arg(4), 0, 0);
        else // (type, buffer, start, vertex count, indices, index count, flags)
            draw(c.arg(1), vb->fvf, vb->memory + start * stride, stride, c.arg(4), c.arg(5), c.arg(6));
        return kDDOk;
    }
    if (m == "SetClipStatus" || m == "GetClipStatus") {
        const uint32_t p = c.arg(1);
        for (uint32_t i = 0; i < 6; ++i)
            if (m == "SetClipStatus")
                clipStatus[i] = c.read32(p + 4 * i);
            else
                c.write32(p + 4 * i, clipStatus[i]);
        return kDDOk;
    }
    // State blocks.
    if (m == "BeginStateBlock") {
        recording = StateBlock{};
        return kDDOk;
    }
    if (m == "EndStateBlock") {
        if (!recording)
            return kDDErrGeneric;
        const uint32_t handle = nextBlock++;
        blocks[handle] = std::move(*recording);
        recording.reset();
        c.write32(c.arg(1), handle);
        return kDDOk;
    }
    if (m == "ApplyStateBlock") {
        auto it = blocks.find(c.arg(1));
        if (it == blocks.end())
            return kDDErrInvalidParams;
        const StateBlock& b = it->second;
        for (const auto& [k, v] : b.renderStates)
            setRenderState(k, v);
        for (const auto& [k, v] : b.stageStates)
            stageStates[k.first][k.second] = v;
        for (const auto& [k, v] : b.textures)
            textures[k] = v;
        for (const auto& [k, v] : b.transforms)
            transforms[k] = v;
        if (b.viewport)
            viewport = *b.viewport;
        if (b.material)
            material = *b.material;
        for (const auto& [k, v] : b.lights)
            lights[k] = v;
        return kDDOk;
    }
    if (m == "CaptureStateBlock") {
        auto it = blocks.find(c.arg(1));
        if (it == blocks.end())
            return kDDErrInvalidParams;
        StateBlock& b = it->second;
        for (auto& [k, v] : b.renderStates)
            v = renderStates[k];
        for (auto& [k, v] : b.stageStates)
            v = stageStates[k.first][k.second];
        for (auto& [k, v] : b.textures)
            v = textures[k];
        for (auto& [k, v] : b.transforms)
            v = transforms[k];
        if (b.viewport)
            b.viewport = viewport;
        if (b.material)
            b.material = material;
        for (auto& [k, v] : b.lights)
            v = lights[k];
        return kDDOk;
    }
    if (m == "CreateStateBlock") { // (type, out): every state, as it is
        StateBlock b;
        for (uint32_t i = 0; i < renderStates.size(); ++i)
            b.renderStates[i] = renderStates[i];
        for (uint32_t st = 0; st < 8; ++st)
            for (uint32_t i = 0; i < 32; ++i)
                b.stageStates[{st, i}] = stageStates[st][i];
        for (uint32_t i = 0; i < 8; ++i)
            b.textures[i] = textures[i];
        b.transforms = transforms;
        b.viewport = viewport;
        b.material = material;
        b.lights = lights;
        const uint32_t handle = nextBlock++;
        blocks[handle] = std::move(b);
        c.write32(c.arg(2), handle);
        return kDDOk;
    }
    if (m == "DeleteStateBlock") {
        blocks.erase(c.arg(1));
        return kDDOk;
    }
    // Load(dest, destPoint, src, srcRect, flags): copies a texture and its
    // mip levels.
    if (m == "Load") {
        std::shared_ptr<Surface> dest = s->com.shared<Surface>(c.arg(1)), src = s->com.shared<Surface>(c.arg(3));
        if (!dest || !src)
            return kDDErrInvalidParams;
        Surface* d = dest.get();
        const Surface* from = src.get();
        while (d && from) {
            d->copy(*from, from->bounds(), d->bounds(), false, 0, 0);
            d->palette = src->palette;
            d->hasSrcKey = from->hasSrcKey;
            d->srcKeyLow = from->srcKeyLow, d->srcKeyHigh = from->srcKeyHigh;
            auto next = [](const Surface* x) -> Surface* {
                for (const auto& a : x->attached)
                    if (a->d.caps[0] & kCapsMipMap)
                        return a.get();
                return nullptr;
            };
            d = next(d);
            from = next(from);
        }
        return kDDOk;
    }
    return std::nullopt;
}

} // namespace reluva::x86::dx

namespace reluva::x86 {

void DirectX::State::finishDraws() {
    for (dx::Device* d : devices)
        d->finishDraws();
}

} // namespace reluva::x86
