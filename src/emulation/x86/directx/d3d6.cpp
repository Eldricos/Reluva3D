// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Direct3D 6 (IDirect3D3, IDirect3DDevice3, viewports, materials, lights
// and IDirect3DTexture2), as the DirectX 6 documentation describes them,
// on Direct3D 7's device: the device is the same object, which draws as
// Direct3D 7's does. Structure layouts are the DirectX 6 SDK's (d3dtypes.h,
// d3dcaps.h).

#include "emulation/x86/directx/device.h"

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

Color readColor(Cpu &c, uint32_t p) {
    return {asFloat(c.read32(p)), asFloat(c.read32(p + 4)), asFloat(c.read32(p + 8)), asFloat(c.read32(p + 12))};
}

void writeColor(Cpu &c, uint32_t p, const Color &v) {
    c.write32(p, asBits(v.r));
    c.write32(p + 4, asBits(v.g));
    c.write32(p + 8, asBits(v.b));
    c.write32(p + 12, asBits(v.a));
}

// A colour of 0 to 1 a channel as a D3DCOLOR, 0xAARRGGBB.
uint32_t packColor(const Color &v) {
    const auto channel = [](float f) { return uint32_t(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return channel(v.a) << 24 | channel(v.r) << 16 | channel(v.g) << 8 | channel(v.b);
}

constexpr uint32_t kDescSize = 252; // Direct3D 6's D3DDEVICEDESC

// The draws' flags: D3DDP_DONOTLIGHT leaves the vertices unlit.
constexpr uint32_t kDontLight = 0x10;

// The light states (D3DLIGHTSTATETYPE) that are render states in Direct3D
// 7: AMBIENT, FOGMODE, FOGSTART, FOGEND, FOGDENSITY and COLORVERTEX, by
// their numbers 2 and 4 to 8.
uint32_t renderStateOfLightState(uint32_t state) {
    switch (state) {
    case 2:
        return 139; // AMBIENT
    case 4:
        return 140; // FOGVERTEXMODE
    case 5:
        return 36; // FOGSTART
    case 6:
        return 37; // FOGEND
    case 7:
        return 38; // FOGDENSITY
    case 8:
        return 141; // COLORVERTEX
    default:
        return 0;
    }
}

constexpr uint32_t kLightingState = 137; // Direct3D 7's LIGHTING
constexpr uint32_t kTextureHandleState = 1;

// The devices IDirect3D3 offers, in the order programs expect, the best last.
struct DeviceKind {
    const char *description;
    const char *name;
    const char *iid;
    bool hardware;
};
const DeviceKind kDevices6[] = {
    {"Microsoft Direct3D RGB Software Emulation", "RGB Emulation", kIidRgbDevice, false},
    {"Microsoft Direct3D Hardware acceleration through Direct3D HAL", "Direct3D HAL", kIidHalDevice, true},
};

// A D3DLIGHT or D3DLIGHT2 (by its dwSize: 76 or 80) as a Direct3D 7 light:
// its one colour diffuse, and specular too unless D3DLIGHT_NO_SPECULAR.
void readLight(Cpu &c, uint32_t p, LightObject &l) {
    l.size = c.read32(p);
    Light &d = l.light;
    d.type = c.read32(p + 4);
    const Color color = readColor(c, p + 8);
    for (uint32_t i = 0; i < 3; ++i) {
        d.position[i] = asFloat(c.read32(p + 24 + 4 * i));
        d.direction[i] = asFloat(c.read32(p + 36 + 4 * i));
    }
    d.range = asFloat(c.read32(p + 48));
    d.falloff = asFloat(c.read32(p + 52));
    d.att0 = asFloat(c.read32(p + 56));
    d.att1 = asFloat(c.read32(p + 60));
    d.att2 = asFloat(c.read32(p + 64));
    d.theta = asFloat(c.read32(p + 68));
    d.phi = asFloat(c.read32(p + 72));
    l.flags = l.size >= 80 ? c.read32(p + 76) : 1;
    d.diffuse = color;
    d.specular = (l.flags & 0x2) ? Color{} : color;
    d.ambient = {};
    d.enabled = (l.flags & 0x1) != 0;
}

void writeLight(Cpu &c, uint32_t p, const LightObject &l) {
    const uint32_t size = std::min(c.read32(p) ? c.read32(p) : l.size, 80u);
    const Light &d = l.light;
    c.write32(p + 4, d.type);
    writeColor(c, p + 8, d.diffuse);
    for (uint32_t i = 0; i < 3; ++i) {
        c.write32(p + 24 + 4 * i, asBits(d.position[i]));
        c.write32(p + 36 + 4 * i, asBits(d.direction[i]));
    }
    const float rest[7] = {d.range, d.falloff, d.att0, d.att1, d.att2, d.theta, d.phi};
    for (uint32_t i = 0; i < 7; ++i)
        c.write32(p + 48 + 4 * i, asBits(rest[i]));
    if (size >= 80)
        c.write32(p + 76, l.flags);
}

// A D3DMATERIAL: dwSize, diffuse, ambient, specular, emissive, power,
// then a texture handle and a ramp size that only ramp devices use.
Material readMaterial(Cpu &c, uint32_t p) {
    return {readColor(c, p + 4), readColor(c, p + 20), readColor(c, p + 36), readColor(c, p + 52),
            asFloat(c.read32(p + 68))};
}

void writeMaterial(Cpu &c, uint32_t p, const Material &m) {
    writeColor(c, p + 4, m.diffuse);
    writeColor(c, p + 20, m.ambient);
    writeColor(c, p + 36, m.specular);
    writeColor(c, p + 52, m.emissive);
    c.write32(p + 68, asBits(m.power));
}

void singlePrecision(const DirectX::State &s, Cpu &c) {
    if (!s.fpuPreserve)
        c.fpu.control &= ~0x300;
}

// A method's index in an interface, by its name.
int methodIndex(const ComInterface &iface, std::string_view name) {
    for (size_t i = 0; i < iface.methods.size(); ++i)
        if (name == iface.methods[i].name)
            return int(i);
    return 0;
}

// The viewport made current on a device: its rectangle, and its clip
// volume as a matrix after the projection.
void useViewport(Device &d, const ViewportObject &v) {
    d.viewport = v.area;
    d.clipAdjust = v.clipMatrix();
}

} // namespace

void writeDeviceDesc6(Cpu &c, uint32_t p, uint32_t size, bool filled) {
    size = std::min(size ? size : kDescSize, kDescSize);
    if (!c.valid(p, size))
        return;
    std::memset(c.memory().data() + p, 0, size);
    c.write32(p, size);
    if (!filled)
        return;
    // Everything up to dwMaxVertexCount is DirectX 3's; then 5's, then 6's.
    uint8_t buffer[kDescSize] = {};
    auto put = [&](uint32_t at, uint32_t v) { std::memcpy(buffer + at, &v, 4); };
    put(0, size);
    put(4, 0x7ff); // every D3DDD_ field up to MAXVERTEXCOUNT
    put(8, 2);     // dcmColorModel: RGB
    put(12, 0x1 | 0x10 | 0x40 | 0x100 | 0x200 | 0x400 | 0x800 | 0x1000 | 0x2000 | 0x8000 | 0x80000); // dwDevCaps
    put(16, 8); // D3DTRANSFORMCAPS: its size, then CLIP
    put(20, 1);
    put(24, 1);  // bClipping
    put(28, 16); // D3DLIGHTINGCAPS: size, POINT, SPOT, DIRECTIONAL, RGB model, 8 lights
    put(32, 0x1 | 0x2 | 0x4);
    put(36, 1);
    put(40, 8);
    put(156, 0x400 | 0x100);         // render target depths: 16, 32
    put(160, 0x400 | 0x200 | 0x100); // Z buffer depths: 16, 24, 32
    put(168, 65535);                 // dwMaxVertexCount
    put(172, 1);                     // texture sizes
    put(176, 1);
    put(180, 2048);
    put(184, 2048);
    put(204, 8192);                    // dwMaxTextureRepeat
    put(208, 2048);                    // dwMaxTextureAspectRatio
    put(212, 1);                       // dwMaxAnisotropy
    put(240, 8);                       // dwFVFCaps: 8 texture coordinate sets
    put(244, 0xffff);                  // dwTextureOpCaps
    const uint16_t stages[2] = {4, 2}; // blend stages, simultaneous textures
    std::memcpy(buffer + 248, stages, 4);
    std::memcpy(c.memory().data() + p, buffer, size);
    if (size >= 100)
        writePrimCaps(c, p + 44); // lines
    if (size >= 156)
        writePrimCaps(c, p + 100); // triangles
}

// IDirect3D3 (on the DirectDraw object)

std::optional<uint32_t> DirectDraw::invoke3d3(const ComInterface &iface, int index, Cpu &c) {
    const std::string_view m = iface.methods[size_t(index)].name;
    // EnumDevices(callback(guid*, description, name, hardware's D3DDEVICEDESC*,
    // software's, context), context)
    if (m == "EnumDevices") {
        const uint32_t callback = c.arg(1);
        const uint32_t context = c.arg(2);
        const uint32_t block = s->process.memory().alloc(16 + 256 + 2 * kDescSize);
        const uint32_t hal = block + 16 + 256;
        const uint32_t hel = hal + kDescSize;
        for (const DeviceKind &k : kDevices6) {
            if (k.hardware && emulationOnly)
                continue;
            ComHost::writeGuid(c, block, k.iid);
            c.writeString(block + 16, k.description, 128);
            c.writeString(block + 16 + 128, k.name, 128);
            writeDeviceDesc6(c, hal, kDescSize, k.hardware);
            writeDeviceDesc6(c, hel, kDescSize, true);
            uint32_t keepGoing = 0;
            if (Error e = c.call(callback, {block, block + 16, block + 16 + 128, hal, hel, context}, keepGoing))
                break;
            if (!keepGoing)
                break;
        }
        s->process.memory().free(block);
        return kDDOk;
    }
    // FindDevice(D3DFINDDEVICESEARCH*, D3DFINDDEVICERESULT*): the hardware
    // device unless the search asks for software (D3DFDS_HARDWARE, 4, with
    // bHardware false) or names another (D3DFDS_GUID, 2).
    if (m == "FindDevice") {
        const uint32_t search = c.arg(1);
        const uint32_t result = c.arg(2);
        if (!c.valid(search, 32) || !c.valid(result, 20 + 2 * kDescSize))
            return kDDErrInvalidParams;
        const uint32_t flags = c.read32(search + 4);
        bool hardware = !emulationOnly;
        if (flags & 0x4)
            hardware = hardware && c.read32(search + 8) != 0;
        std::string iid = hardware ? kIidHalDevice : kIidRgbDevice;
        if (flags & 0x2) {
            iid = ComHost::guid(c, search + 16);
            if (iid != kIidHalDevice && iid != kIidRgbDevice)
                return kDDErrNotFound;
            hardware = iid == kIidHalDevice;
        }
        ComHost::writeGuid(c, result + 4, iid);
        writeDeviceDesc6(c, result + 20, kDescSize, hardware);
        writeDeviceDesc6(c, result + 20 + kDescSize, kDescSize, true);
        return kDDOk;
    }
    // CreateDevice(class, IDirectDrawSurface4*, out, outer)
    if (m == "CreateDevice") {
        const std::string kind = ComHost::guid(c, c.arg(1));
        std::shared_ptr<Surface> target = s->com.shared<Surface>(c.arg(2));
        const uint32_t out = c.arg(3);
        if (!target || !c.valid(out, 4))
            return kDDErrInvalidParams;
        if (kind != kIidHalDevice && kind != kIidRgbDevice && kind != kIidMmxDevice && kind != kIidRefDevice &&
            kind != kIidRampDevice)
            return kDDErrInvalidParams;
        auto device = std::make_shared<Device>(s, target);
        singlePrecision(*s, c);
        c.write32(out, s->com.pointer(device, s->interfaces.device3));
        return kDDOk;
    }
    if (m == "CreateLight" || m == "CreateMaterial" || m == "CreateViewport") { // (out, outer)
        const uint32_t out = c.arg(1);
        if (!c.valid(out, 4))
            return kDDErrInvalidParams;
        if (m == "CreateLight") {
            auto l = std::make_shared<LightObject>();
            l->s = s;
            c.write32(out, s->com.pointer(l, s->interfaces.light));
        } else if (m == "CreateMaterial") {
            auto mat = std::make_shared<MaterialObject>();
            mat->s = s;
            c.write32(out, s->com.pointer(mat, s->interfaces.material3));
        } else {
            auto v = std::make_shared<ViewportObject>();
            v->s = s;
            c.write32(out, s->com.pointer(v, s->interfaces.viewport3));
        }
        return kDDOk;
    }
    // CreateVertexBuffer(D3DVERTEXBUFFERDESC*, out, flags, outer): as
    // Direct3D 7's, handed out as IDirect3DVertexBuffer.
    if (m == "CreateVertexBuffer") {
        const uint32_t out = c.arg(2);
        const ComInterface &d3d7 = s->interfaces.direct3d7;
        const std::optional<uint32_t> r = invoke3d(d3d7, methodIndex(d3d7, "CreateVertexBuffer"), c);
        if (r && *r == kDDOk) {
            std::shared_ptr<VertexBuffer> vb = s->com.shared<VertexBuffer>(c.read32(out));
            if (vb)
                c.write32(out, s->com.pointer(vb, s->interfaces.vertexBuffer));
        }
        return r;
    }
    if (m == "EnumZBufferFormats")
        return invoke3d(s->interfaces.direct3d7, methodIndex(s->interfaces.direct3d7, "EnumZBufferFormats"), c);
    if (m == "EvictManagedTextures")
        return kDDOk;
    return std::nullopt;
}

// IDirect3DDevice3

std::optional<uint32_t> Device::invoke6(const ComInterface &iface, int index, Cpu &c) {
    const std::string_view m = iface.methods[size_t(index)].name;
    if (m == "GetCaps") { // (hardware's D3DDEVICEDESC*, software's)
        for (uint32_t i = 1; i <= 2; ++i)
            if (c.arg(int(i)))
                writeDeviceDesc6(c, c.arg(int(i)), c.read32(c.arg(int(i))), true);
        return kDDOk;
    }
    if (m == "GetStats") { // a D3DSTATS of zeros, but its size
        const uint32_t p = c.arg(1);
        const uint32_t size = std::min(c.read32(p), 64u);
        for (uint32_t i = 4; i + 4 <= size; i += 4)
            c.write32(p + i, 0);
        return kDDOk;
    }
    if (m == "AddViewport" || m == "DeleteViewport" || m == "SetCurrentViewport") {
        std::shared_ptr<ViewportObject> v = s->com.shared<ViewportObject>(c.arg(1));
        if (!v)
            return kDDErrInvalidParams;
        auto it = std::find(viewports.begin(), viewports.end(), v);
        if (m == "AddViewport") {
            if (it == viewports.end()) {
                viewports.push_back(v);
                v->device = std::static_pointer_cast<Device>(shared_from_this());
            }
        } else if (m == "DeleteViewport") {
            if (it == viewports.end())
                return kDDErrInvalidParams;
            viewports.erase(it);
            if (currentViewport == v)
                currentViewport = nullptr;
            v->device.reset();
        } else {
            if (it == viewports.end())
                return kDDErrInvalidParams;
            currentViewport = v;
            useViewport(*this, *v);
        }
        return kDDOk;
    }
    if (m == "GetCurrentViewport") {
        if (!currentViewport)
            return kDDErrNotFound;
        ++currentViewport->refs;
        c.write32(c.arg(1), s->com.pointer(currentViewport, s->interfaces.viewport3));
        return kDDOk;
    }
    if (m == "NextViewport") { // (viewport, out, D3DNEXT_NEXT 1, HEAD 2, TAIL 4)
        const uint32_t flags = c.arg(3);
        std::shared_ptr<ComObject> found;
        if (flags & 0x2)
            found = viewports.empty() ? nullptr : viewports.front();
        else if (flags & 0x4)
            found = viewports.empty() ? nullptr : viewports.back();
        else {
            std::shared_ptr<ViewportObject> v = s->com.shared<ViewportObject>(c.arg(1));
            auto it = std::find(viewports.begin(), viewports.end(), v);
            if (it != viewports.end() && it + 1 != viewports.end())
                found = *(it + 1);
        }
        c.write32(c.arg(2), found ? s->com.pointer(found, s->interfaces.viewport3) : 0);
        return found ? kDDOk : kDDErrNotFound;
    }
    if (m == "GetDirect3D") {
        ++s->directDraw->refs;
        c.write32(c.arg(1), s->com.pointer(s->directDraw, s->interfaces.direct3d3));
        return kDDOk;
    }
    if (m == "GetRenderTarget") {
        ++target->refs;
        c.write32(c.arg(1), s->com.pointer(target, s->interfaces.surface4));
        return kDDOk;
    }
    if (m == "GetTexture") {
        const auto &t = c.arg(1) < 8 ? textures[c.arg(1)] : nullptr;
        if (t)
            ++t->refs;
        c.write32(c.arg(2), t ? s->com.pointer(t, s->interfaces.texture2) : 0);
        return kDDOk;
    }
    if (m == "GetLightState" || m == "SetLightState") { // (state, value or out)
        const uint32_t state = c.arg(1);
        if (state == 1) { // MATERIAL: a handle
            if (m == "SetLightState")
                materialHandle = c.arg(2);
            else
                c.write32(c.arg(2), materialHandle);
            return kDDOk;
        }
        if (state == 3) { // COLORMODEL: RGB, whatever's asked
            if (m == "GetLightState")
                c.write32(c.arg(2), 2);
            return kDDOk;
        }
        const uint32_t rs = renderStateOfLightState(state);
        if (!rs)
            return kDDErrInvalidParams;
        if (m == "SetLightState")
            setRenderState(rs, c.arg(2));
        else
            c.write32(c.arg(2), renderStates[rs]);
        return kDDOk;
    }
    // Direct3D 5's TEXTUREHANDLE render state: the first stage's texture,
    // by its handle (0 for none).
    if (m == "SetRenderState" && c.arg(1) == kTextureHandleState) {
        auto it = s->textureHandles.find(c.arg(2));
        textures[0] = it == s->textureHandles.end() ? nullptr : it->second.lock();
        setRenderState(kTextureHandleState, c.arg(2));
        return kDDOk;
    }
    if (m == "ComputeSphereVisibility") { // (centres, radii, count, flags, results): each wholly inside
        for (uint32_t i = 0; i < c.arg(3); ++i)
            c.write32(c.arg(5) + 4 * i, 0);
        return kDDOk;
    }
    // The draws: Direct3D 6 lights the vertices that have normals, unless
    // the draw's flags say not to; the current viewport's lights, and the
    // material whose handle the MATERIAL light state holds.
    const bool drawing =
        m == "DrawPrimitive" || m == "DrawIndexedPrimitive" || m == "DrawPrimitiveVB" || m == "DrawIndexedPrimitiveVB";
    if (!drawing)
        return std::nullopt;
    VertexBuffer *vb = nullptr;
    uint32_t fvf = c.arg(2);
    uint32_t flags = 0;
    if (m == "DrawPrimitive")
        flags = c.arg(5);
    else if (m == "DrawIndexedPrimitive")
        flags = c.arg(7);
    else {
        vb = s->com.as<VertexBuffer>(c.arg(2));
        if (!vb)
            return kDDErrInvalidParams;
        fvf = vb->fvf;
        flags = c.arg(5);
    }
    lights.clear();
    if (auto v = std::dynamic_pointer_cast<ViewportObject>(currentViewport)) {
        uint32_t i = 0;
        for (const auto &o : v->lights)
            if (auto l = std::dynamic_pointer_cast<LightObject>(o))
                lights[i++] = l->light;
    }
    if (materialHandle) {
        auto it = s->materialHandles.find(materialHandle);
        if (it != s->materialHandles.end())
            if (auto mat = std::dynamic_pointer_cast<MaterialObject>(it->second.lock()))
                material = mat->material;
    }
    const uint32_t wasLighting = renderStates[kLightingState];
    renderStates[kLightingState] = !(flags & kDontLight) && VertexLayout(fvf).normal ? 1 : 0;
    const uint32_t stride = uint32_t(VertexLayout(fvf).size);
    if (m == "DrawPrimitive") // (type, fvf, vertices, count, flags)
        draw(c.arg(1), fvf, c.arg(3), stride, c.arg(4), 0, 0);
    else if (m == "DrawIndexedPrimitive") // (type, fvf, vertices, vertex count, indices, index count, flags)
        draw(c.arg(1), fvf, c.arg(3), stride, c.arg(4), c.arg(5), c.arg(6));
    else if (m == "DrawPrimitiveVB") // (type, buffer, start, count, flags)
        draw(c.arg(1), fvf, vb->memory + c.arg(3) * stride, stride, c.arg(4), 0, 0);
    else // (type, buffer, indices, index count, flags): the buffer's every vertex
        draw(c.arg(1), fvf, vb->memory, stride, vb->count, c.arg(3), c.arg(4));
    renderStates[kLightingState] = wasLighting;
    return kDDOk;
}

// IDirect3DViewport3

Matrix ViewportObject::clipMatrix() const {
    // From the clip volume (x from clipX across clipWidth, y down from clipY
    // across clipHeight, z from minZ to maxZ) to Direct3D 7's (x and y -1 to
    // 1, z 0 to 1), each times w, as row vectors take it.
    Matrix m{};
    const float w = clipWidth != 0 ? clipWidth : 2;
    const float h = clipHeight != 0 ? clipHeight : 2;
    const float depth = maxZ != minZ ? maxZ - minZ : 1;
    m[0] = 2 / w;
    m[12] = -2 * clipX / w - 1;
    m[5] = 2 / h;
    m[13] = -2 * (clipY - h) / h - 1;
    m[10] = 1 / depth;
    m[14] = -minZ / depth;
    m[15] = 1;
    return m;
}

const ComInterface *ViewportObject::query(const std::string &iid) {
    return iid == s->interfaces.viewport3.iid ? &s->interfaces.viewport3 : nullptr;
}

std::optional<uint32_t> ViewportObject::invoke(const ComInterface &iface, int index, Cpu &c) {
    const std::string_view m = iface.methods[size_t(index)].name;
    const uint32_t p = c.arg(1);
    auto device = this->device.lock();
    // A change to the rectangle or the clip volume reaches the device it's current on.
    auto changed = [&] {
        if (device && device->currentViewport.get() == this)
            useViewport(*device, *this);
    };
    if (m == "Initialize")
        return kDDOk;
    // D3DVIEWPORT: dwSize, dwX, dwY, dwWidth, dwHeight, dvScaleX, dvScaleY,
    // dvMaxX, dvMaxY, dvMinZ, dvMaxZ; the clip volume is ±dvMaxX by ±dvMaxY.
    if (m == "SetViewport" || m == "SetViewport2") {
        if (!c.valid(p, 44))
            return kDDErrInvalidParams;
        area = {int(c.read32(p + 4)), int(c.read32(p + 8)), int(c.read32(p + 12)), int(c.read32(p + 16)), 0, 1};
        if (m == "SetViewport") {
            const float maxX = asFloat(c.read32(p + 28));
            const float maxY = asFloat(c.read32(p + 32));
            clipX = -maxX;
            clipWidth = 2 * maxX;
            clipY = maxY;
            clipHeight = 2 * maxY;
        } else { // D3DVIEWPORT2: dvClipX, dvClipY, dvClipWidth, dvClipHeight
            clipX = asFloat(c.read32(p + 20));
            clipY = asFloat(c.read32(p + 24));
            clipWidth = asFloat(c.read32(p + 28));
            clipHeight = asFloat(c.read32(p + 32));
        }
        minZ = asFloat(c.read32(p + 36));
        maxZ = asFloat(c.read32(p + 40));
        changed();
        return kDDOk;
    }
    if (m == "GetViewport" || m == "GetViewport2") {
        if (!c.valid(p, 44))
            return kDDErrInvalidParams;
        c.write32(p + 4, uint32_t(area.x));
        c.write32(p + 8, uint32_t(area.y));
        c.write32(p + 12, uint32_t(area.width));
        c.write32(p + 16, uint32_t(area.height));
        if (m == "GetViewport") {
            c.write32(p + 20, asBits(clipWidth != 0 ? float(area.width) / clipWidth : 0));
            c.write32(p + 24, asBits(clipHeight != 0 ? float(area.height) / clipHeight : 0));
            c.write32(p + 28, asBits(clipWidth / 2));
            c.write32(p + 32, asBits(clipHeight / 2));
        } else {
            c.write32(p + 20, asBits(clipX));
            c.write32(p + 24, asBits(clipY));
            c.write32(p + 28, asBits(clipWidth));
            c.write32(p + 32, asBits(clipHeight));
        }
        c.write32(p + 36, asBits(minZ));
        c.write32(p + 40, asBits(maxZ));
        return kDDOk;
    }
    if (m == "SetBackground") {
        background = c.arg(1);
        return kDDOk;
    }
    if (m == "GetBackground") { // (handle out, valid out)
        c.write32(c.arg(1), background);
        if (c.arg(2))
            c.write32(c.arg(2), background ? 1 : 0);
        return kDDOk;
    }
    // A background picture for the Z buffer isn't kept: Clear fills it with
    // the farthest depth.
    if (m == "SetBackgroundDepth" || m == "SetBackgroundDepth2")
        return kDDOk;
    if (m == "GetBackgroundDepth" || m == "GetBackgroundDepth2") {
        c.write32(c.arg(1), 0);
        if (c.arg(2))
            c.write32(c.arg(2), 0);
        return kDDOk;
    }
    // Clear(count, D3DRECT*, flags): the target with the background
    // material's diffuse colour, the Z buffer with the farthest depth.
    // Clear2(count, rects, flags, colour, z, stencil).
    if (m == "Clear" || m == "Clear2") {
        if (!device)
            return kDDErrInvalidParams;
        uint32_t color = 0;
        float z = 1;
        if (m == "Clear2") {
            color = c.arg(4);
            z = asFloat(c.arg(5));
        } else if (background) {
            auto it = s->materialHandles.find(background);
            if (it != s->materialHandles.end())
                if (auto mat = std::dynamic_pointer_cast<MaterialObject>(it->second.lock()))
                    color = packColor(mat->material.diffuse);
        }
        const Viewport kept = device->viewport;
        device->viewport = area;
        device->clear(c.arg(1), c.arg(2), c.arg(3) & 3, color, z);
        device->viewport = kept;
        return kDDOk;
    }
    if (m == "AddLight" || m == "DeleteLight") {
        std::shared_ptr<LightObject> l = s->com.shared<LightObject>(c.arg(1));
        if (!l)
            return kDDErrInvalidParams;
        auto it = std::find(lights.begin(), lights.end(), l);
        if (m == "AddLight") {
            if (it == lights.end())
                lights.push_back(l);
        } else {
            if (it == lights.end())
                return kDDErrInvalidParams;
            lights.erase(it);
        }
        return kDDOk;
    }
    if (m == "NextLight") { // (light, out, D3DNEXT_NEXT 1, HEAD 2, TAIL 4)
        const uint32_t flags = c.arg(3);
        std::shared_ptr<ComObject> found;
        if (flags & 0x2)
            found = lights.empty() ? nullptr : lights.front();
        else if (flags & 0x4)
            found = lights.empty() ? nullptr : lights.back();
        else {
            std::shared_ptr<LightObject> l = s->com.shared<LightObject>(c.arg(1));
            auto it = std::find(lights.begin(), lights.end(), l);
            if (it != lights.end() && it + 1 != lights.end())
                found = *(it + 1);
        }
        c.write32(c.arg(2), found ? s->com.pointer(found, s->interfaces.light) : 0);
        return found ? kDDOk : kDDErrNotFound;
    }
    if (m == "LightElements")
        return kDDErrUnsupported;
    return std::nullopt;
}

// IDirect3DMaterial3

MaterialObject::~MaterialObject() {
    if (s && handle)
        s->materialHandles.erase(handle);
}

const ComInterface *MaterialObject::query(const std::string &iid) {
    return iid == s->interfaces.material3.iid ? &s->interfaces.material3 : nullptr;
}

std::optional<uint32_t> MaterialObject::invoke(const ComInterface &iface, int index, Cpu &c) {
    const std::string_view m = iface.methods[size_t(index)].name;
    if (m == "SetMaterial") {
        if (!c.valid(c.arg(1), 72))
            return kDDErrInvalidParams;
        material = readMaterial(c, c.arg(1));
        return kDDOk;
    }
    if (m == "GetMaterial") {
        if (!c.valid(c.arg(1), 72))
            return kDDErrInvalidParams;
        writeMaterial(c, c.arg(1), material);
        return kDDOk;
    }
    if (m == "GetHandle") { // (device, handle out): the same handle for every device
        if (!handle) {
            handle = s->nextHandle++;
            s->materialHandles[handle] = shared_from_this();
        }
        c.write32(c.arg(2), handle);
        return kDDOk;
    }
    return std::nullopt;
}

// IDirect3DLight

const ComInterface *LightObject::query(const std::string &iid) {
    return iid == s->interfaces.light.iid ? &s->interfaces.light : nullptr;
}

std::optional<uint32_t> LightObject::invoke(const ComInterface &iface, int index, Cpu &c) {
    const std::string_view m = iface.methods[size_t(index)].name;
    if (m == "Initialize")
        return kDDOk;
    if (m == "SetLight" || m == "GetLight") {
        if (!c.valid(c.arg(1), 76))
            return kDDErrInvalidParams;
        if (m == "SetLight")
            readLight(c, c.arg(1), *this);
        else
            writeLight(c, c.arg(1), *this);
        return kDDOk;
    }
    return std::nullopt;
}

// IDirect3DTexture2, on a texture surface

std::optional<uint32_t> Surface::invokeTexture(const ComInterface &iface, int index, Cpu &c) {
    const std::string_view m = iface.methods[size_t(index)].name;
    if (m == "GetHandle") { // (device, handle out): one handle a surface
        uint32_t handle = 0;
        for (const auto &[h, t] : s->textureHandles)
            if (t.lock().get() == this)
                handle = h;
        if (!handle) {
            handle = s->nextHandle++;
            s->textureHandles[handle] = std::static_pointer_cast<Surface>(shared_from_this());
        }
        c.write32(c.arg(2), handle);
        return kDDOk;
    }
    if (m == "PaletteChanged") {
        s->finishDraws();
        touched();
        return kDDOk;
    }
    // Load(source texture): its pixels, mip levels, palette and colour key.
    if (m == "Load") {
        std::shared_ptr<Surface> src = s->com.shared<Surface>(c.arg(1));
        if (!src)
            return kDDErrInvalidParams;
        s->finishDraws();
        Surface *d = this;
        const Surface *from = src.get();
        auto next = [](const Surface *x) -> Surface * {
            for (const auto &a : x->attached)
                if (a->d.caps[0] & kCapsMipMap)
                    return a.get();
            return nullptr;
        };
        while (d && from) {
            d->copy(*from, from->bounds(), d->bounds(), false, 0, 0);
            d->palette = src->palette;
            d->hasSrcKey = from->hasSrcKey;
            d->srcKeyLow = from->srcKeyLow;
            d->srcKeyHigh = from->srcKeyHigh;
            d = next(d);
            from = next(from);
        }
        return kDDOk;
    }
    return std::nullopt;
}

} // namespace reluva::x86::dx
