// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Direct3D Retained Mode: docs/common/x86/d3drm.md. Interface layouts,
// IIDs, structures and constants are the DirectX 5 SDK documentation's
// (d3drm.h, d3drmobj.h, d3drmdef.h).

#include "emulation/x86/directx/d3drm.h"

#include "emulation/x86/com.h"
#include "emulation/x86/directx/device.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string_view>
#include <vector>

namespace reluva::x86 {

namespace {

const char *const kIidDirect3DRM = "{2bc49361-8327-11cf-ac4a-0000c03825a1}";
const char *const kIidDirect3DRM2 = "{4516ecc8-8f20-11d0-9b6d-0000c0781bc3}";
const char *const kIidObject = "{eb16cb00-d271-11ce-ac48-0000c03825a1}";
const char *const kIidVisual = "{eb16cb04-d271-11ce-ac48-0000c03825a1}";
const char *const kIidFrame = "{eb16cb03-d271-11ce-ac48-0000c03825a1}";
const char *const kIidFrame2 = "{c3dfbd60-3988-11d0-9ec2-0000c0291ac3}";
const char *const kIidDevice = "{e9e19280-6e05-11cf-ac4a-0000c03825a1}";
const char *const kIidDevice2 = "{4516ec78-8f20-11d0-9b6d-0000c0781bc3}";
const char *const kIidViewport = "{eb16cb02-d271-11ce-ac48-0000c03825a1}";
const char *const kIidLight = "{eb16cb08-d271-11ce-ac48-0000c03825a1}";
const char *const kIidMaterial = "{eb16cb0b-d271-11ce-ac48-0000c03825a1}";
const char *const kIidTexture = "{eb16cb09-d271-11ce-ac48-0000c03825a1}";
const char *const kIidTexture2 = "{120f30c0-1629-11d0-941c-0080c80cfa7b}";
const char *const kIidMesh = "{a3a80d01-6e12-11cf-ac4a-0000c03825a1}";

constexpr uint32_t kRmOk = 0;
constexpr uint32_t kRmErrBadValue = 0x88760313u; // D3DRMERR_BADVALUE

const std::vector<ComInterface::Method> kUnknown = {{"QueryInterface", 2}, {"AddRef", 0}, {"Release", 0}};
// IDirect3DRMObject's, which every Retained Mode interface but IDirect3DRM
// starts with.
const std::vector<ComInterface::Method> kObject = {
    {"Clone", 3},      {"AddDestroyCallback", 2}, {"DeleteDestroyCallback", 2},
    {"SetAppData", 1}, {"GetAppData", 0},         {"SetName", 1},
    {"GetName", 2},    {"GetClassName", 2}};

std::vector<ComInterface::Method> with(std::vector<ComInterface::Method> m) {
    m.insert(m.begin(), kUnknown.begin(), kUnknown.end());
    return m;
}

std::vector<ComInterface::Method> withObject(std::vector<ComInterface::Method> m) {
    m.insert(m.begin(), kObject.begin(), kObject.end());
    return with(std::move(m));
}

// IDirect3DRM and IDirect3DRM2 (which adds CreateProgressiveMesh, and takes
// a resource by module, name and type).
std::vector<ComInterface::Method> rmMethods(bool two) {
    std::vector<ComInterface::Method> m = {{"CreateObject", 4},
                                           {"CreateFrame", 2},
                                           {"CreateMesh", 1},
                                           {"CreateMeshBuilder", 1},
                                           {"CreateFace", 1},
                                           {"CreateAnimation", 1},
                                           {"CreateAnimationSet", 1},
                                           {"CreateTexture", 2},
                                           {"CreateLight", 3},
                                           {"CreateLightRGB", 5},
                                           {"CreateMaterial", 2},
                                           {"CreateDevice", 3},
                                           {"CreateDeviceFromSurface", 4},
                                           {"CreateDeviceFromD3D", 3},
                                           {"CreateDeviceFromClipper", 5},
                                           {"CreateTextureFromSurface", 2},
                                           {"CreateShadow", 9},
                                           {"CreateViewport", 7},
                                           {"CreateWrap", 16},
                                           {"CreateUserVisual", 3},
                                           {"LoadTexture", 2},
                                           {"LoadTextureFromResource", two ? 4 : 2},
                                           {"SetSearchPath", 1},
                                           {"AddSearchPath", 1},
                                           {"GetSearchPath", 2},
                                           {"SetDefaultTextureColors", 1},
                                           {"SetDefaultTextureShades", 1},
                                           {"GetDevices", 1},
                                           {"GetNamedObject", 2},
                                           {"EnumerateObjects", 2},
                                           {"Load", 10},
                                           {"Tick", 1}};
    if (two)
        m.push_back({"CreateProgressiveMesh", 1});
    return with(std::move(m));
}

// IDirect3DRMFrame and IDirect3DRMFrame2, which adds to its end.
std::vector<ComInterface::Method> frameMethods(bool two) {
    std::vector<ComInterface::Method> m = {{"AddChild", 1},
                                           {"AddLight", 1},
                                           {"AddMoveCallback", 2},
                                           {"AddTransform", 2},
                                           {"AddTranslation", 4},
                                           {"AddScale", 4},
                                           {"AddRotation", 5},
                                           {"AddVisual", 1},
                                           {"GetChildren", 1},
                                           {"GetColor", 0},
                                           {"GetLights", 1},
                                           {"GetMaterialMode", 0},
                                           {"GetParent", 1},
                                           {"GetPosition", 2},
                                           {"GetRotation", 3},
                                           {"GetScene", 1},
                                           {"GetSortMode", 0},
                                           {"GetTexture", 1},
                                           {"GetTransform", 1},
                                           {"GetVelocity", 3},
                                           {"GetOrientation", 3},
                                           {"GetVisuals", 1},
                                           {"GetTextureTopology", 2},
                                           {"InverseTransform", 2},
                                           {"Load", 5},
                                           {"LookAt", 3},
                                           {"Move", 1},
                                           {"DeleteChild", 1},
                                           {"DeleteLight", 1},
                                           {"DeleteMoveCallback", 2},
                                           {"DeleteVisual", 1},
                                           {"GetSceneBackground", 0},
                                           {"GetSceneBackgroundDepth", 1},
                                           {"GetSceneFogColor", 0},
                                           {"GetSceneFogEnable", 0},
                                           {"GetSceneFogMode", 0},
                                           {"GetSceneFogParams", 3},
                                           {"SetSceneBackground", 1},
                                           {"SetSceneBackgroundRGB", 3},
                                           {"SetSceneBackgroundDepth", 1},
                                           {"SetSceneBackgroundImage", 1},
                                           {"SetSceneFogEnable", 1},
                                           {"SetSceneFogColor", 1},
                                           {"SetSceneFogMode", 1},
                                           {"SetSceneFogParams", 3},
                                           {"SetColor", 1},
                                           {"SetColorRGB", 3},
                                           {"GetZbufferMode", 0},
                                           {"SetMaterialMode", 1},
                                           {"SetOrientation", 7},
                                           {"SetPosition", 4},
                                           {"SetRotation", 5},
                                           {"SetSortMode", 1},
                                           {"SetTexture", 1},
                                           {"SetTextureTopology", 2},
                                           {"SetVelocity", 5},
                                           {"SetZbufferMode", 1},
                                           {"Transform", 2}};
    if (two) {
        const std::vector<ComInterface::Method> more = {
            {"AddMoveCallback2", 3}, {"GetBox", 1},         {"GetBoxEnable", 0},
            {"GetAxes", 2},          {"GetMaterial", 1},    {"GetInheritAxes", 0},
            {"GetHierarchyBox", 1},  {"SetBox", 1},         {"SetBoxEnable", 1},
            {"SetAxes", 6},          {"SetInheritAxes", 1}, {"SetMaterial", 1},
            {"SetQuaternion", 2},    {"RayPick", 4},        {"Save", 3}};
        m.insert(m.end(), more.begin(), more.end());
    }
    return withObject(std::move(m));
}

// IDirect3DRMDevice and IDirect3DRMDevice2, which adds to its end.
std::vector<ComInterface::Method> deviceMethods(bool two) {
    std::vector<ComInterface::Method> m = {{"Init", 2},
                                           {"InitFromD3D", 2},
                                           {"InitFromClipper", 4},
                                           {"Update", 0},
                                           {"AddUpdateCallback", 2},
                                           {"DeleteUpdateCallback", 2},
                                           {"SetBufferCount", 1},
                                           {"GetBufferCount", 0},
                                           {"SetDither", 1},
                                           {"SetShades", 1},
                                           {"SetQuality", 1},
                                           {"SetTextureQuality", 1},
                                           {"GetViewports", 1},
                                           {"GetDither", 0},
                                           {"GetShades", 0},
                                           {"GetHeight", 0},
                                           {"GetWidth", 0},
                                           {"GetTrianglesDrawn", 0},
                                           {"GetWireframeOptions", 0},
                                           {"GetQuality", 0},
                                           {"GetColorModel", 0},
                                           {"GetTextureQuality", 0},
                                           {"GetDirect3DDevice", 1}};
    if (two) {
        const std::vector<ComInterface::Method> more = {{"InitFromD3D2", 2},
                                                        {"InitFromSurface", 3},
                                                        {"SetRenderMode", 1},
                                                        {"GetRenderMode", 0},
                                                        {"GetDirect3DDevice2", 1}};
        m.insert(m.end(), more.begin(), more.end());
    }
    return withObject(std::move(m));
}

std::vector<ComInterface::Method> viewportMethods() {
    return withObject({{"Init", 7},
                       {"Clear", 0},
                       {"Render", 1},
                       {"SetFront", 1},
                       {"SetBack", 1},
                       {"SetField", 1},
                       {"SetUniformScaling", 1},
                       {"SetCamera", 1},
                       {"SetProjection", 1},
                       {"Transform", 2},
                       {"InverseTransform", 2},
                       {"Configure", 4},
                       {"ForceUpdate", 4},
                       {"SetPlane", 4},
                       {"GetCamera", 1},
                       {"GetDevice", 1},
                       {"GetPlane", 4},
                       {"Pick", 3},
                       {"GetUniformScaling", 0},
                       {"GetX", 0},
                       {"GetY", 0},
                       {"GetWidth", 0},
                       {"GetHeight", 0},
                       {"GetField", 0},
                       {"GetBack", 0},
                       {"GetFront", 0},
                       {"GetProjection", 0},
                       {"GetDirect3DViewport", 1}});
}

std::vector<ComInterface::Method> lightMethods() {
    return withObject({{"SetType", 1},
                       {"SetColor", 1},
                       {"SetColorRGB", 3},
                       {"SetRange", 1},
                       {"SetUmbra", 1},
                       {"SetPenumbra", 1},
                       {"SetConstantAttenuation", 1},
                       {"SetLinearAttenuation", 1},
                       {"SetQuadraticAttenuation", 1},
                       {"GetRange", 0},
                       {"GetUmbra", 0},
                       {"GetPenumbra", 0},
                       {"GetConstantAttenuation", 0},
                       {"GetLinearAttenuation", 0},
                       {"GetQuadraticAttenuation", 0},
                       {"GetColor", 0},
                       {"GetType", 0},
                       {"SetEnableFrame", 1},
                       {"GetEnableFrame", 1}});
}

std::vector<ComInterface::Method> materialMethods() {
    return withObject({{"SetPower", 1},
                       {"SetSpecular", 3},
                       {"SetEmissive", 3},
                       {"GetPower", 0},
                       {"GetSpecular", 3},
                       {"GetEmissive", 3}});
}

// IDirect3DRMTexture and IDirect3DRMTexture2, which adds to its end.
std::vector<ComInterface::Method> textureMethods(bool two) {
    std::vector<ComInterface::Method> m = {{"InitFromFile", 1},
                                           {"InitFromSurface", 1},
                                           {"InitFromResource", 1},
                                           {"Changed", 2},
                                           {"SetColors", 1},
                                           {"SetShades", 1},
                                           {"SetDecalSize", 2},
                                           {"SetDecalOrigin", 2},
                                           {"SetDecalScale", 1},
                                           {"SetDecalTransparency", 1},
                                           {"SetDecalTransparentColor", 1},
                                           {"GetDecalSize", 2},
                                           {"GetDecalOrigin", 2},
                                           {"GetImage", 0},
                                           {"GetShades", 0},
                                           {"GetColors", 0},
                                           {"GetDecalScale", 0},
                                           {"GetDecalTransparency", 0},
                                           {"GetDecalTransparentColor", 0}};
    if (two) {
        const std::vector<ComInterface::Method> more = {
            {"InitFromImage", 1}, {"InitFromResource2", 3}, {"GenerateMIPMap", 1}};
        m.insert(m.end(), more.begin(), more.end());
    }
    return withObject(std::move(m));
}

std::vector<ComInterface::Method> meshMethods() {
    return withObject({{"Scale", 3},
                       {"Translate", 3},
                       {"GetBox", 1},
                       {"AddGroup", 5},
                       {"SetVertices", 4},
                       {"SetGroupColor", 2},
                       {"SetGroupColorRGB", 4},
                       {"SetGroupMapping", 2},
                       {"SetGroupQuality", 2},
                       {"SetGroupMaterial", 2},
                       {"SetGroupTexture", 2},
                       {"GetGroupCount", 0},
                       {"GetGroup", 6},
                       {"GetVertices", 4},
                       {"GetGroupColor", 1},
                       {"GetGroupMapping", 1},
                       {"GetGroupQuality", 1},
                       {"GetGroupMaterial", 2},
                       {"GetGroupTexture", 2}});
}

float asFloat(uint32_t v) {
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

uint32_t bits(float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}

// A D3DVALUE returned: on the x87 stack, where a 32-bit Windows program
// takes a float a function returns (EAX means nothing then).
uint32_t returnFloat(Cpu &c, float v) {
    c.fpuPush(double(v));
    return 0;
}

// A D3DRMMATRIX4D: 4 rows of 4, row vectors (a point is a row, times the
// matrix), so the last row is the translation.
using Matrix = std::array<float, 16>;
using Vector = std::array<float, 3>;

Matrix identity() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

Matrix multiply(const Matrix &a, const Matrix &b) {
    Matrix r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k)
                r[size_t(i * 4 + j)] += a[size_t(i * 4 + k)] * b[size_t(k * 4 + j)];
    return r;
}

// The inverse of an affine matrix (its last column 0, 0, 0, 1): the 3 x 3
// part's inverse, and the translation taken back through it.
Matrix inverse(const Matrix &m) {
    const float a = m[0];
    const float b = m[1];
    const float c = m[2];
    const float d = m[4];
    const float e = m[5];
    const float f = m[6];
    const float g = m[8];
    const float h = m[9];
    const float i = m[10];
    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (det == 0)
        return identity();
    const float k = 1 / det;
    Matrix r = {(e * i - f * h) * k,
                (c * h - b * i) * k,
                (b * f - c * e) * k,
                0,
                (f * g - d * i) * k,
                (a * i - c * g) * k,
                (c * d - a * f) * k,
                0,
                (d * h - e * g) * k,
                (b * g - a * h) * k,
                (a * e - b * d) * k,
                0,
                0,
                0,
                0,
                1};
    for (int j = 0; j < 3; ++j)
        r[size_t(12 + j)] = -(m[12] * r[size_t(j)] + m[13] * r[size_t(4 + j)] + m[14] * r[size_t(8 + j)]);
    return r;
}

Vector transformPoint(const Vector &p, const Matrix &m) {
    return {p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12], p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13],
            p[0] * m[2] + p[1] * m[6] + p[2] * m[10] + m[14]};
}

Vector transformDirection(const Vector &p, const Matrix &m) {
    return {p[0] * m[0] + p[1] * m[4] + p[2] * m[8], p[0] * m[1] + p[1] * m[5] + p[2] * m[9],
            p[0] * m[2] + p[1] * m[6] + p[2] * m[10]};
}

Vector normalized(const Vector &v) {
    const float n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    return n > 0 ? Vector{v[0] / n, v[1] / n, v[2] / n} : v;
}

Vector cross(const Vector &a, const Vector &b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

// A rotation of `theta` radians about `axis`, for row vectors.
Matrix rotation(Vector axis, float theta) {
    axis = normalized(axis);
    const float x = axis[0];
    const float y = axis[1];
    const float z = axis[2];
    const float c = std::cos(theta);
    const float sn = std::sin(theta);
    const float t = 1 - c;
    return {t * x * x + c,
            t * x * y + sn * z,
            t * x * z - sn * y,
            0,
            t * x * y - sn * z,
            t * y * y + c,
            t * y * z + sn * x,
            0,
            t * x * z + sn * y,
            t * y * z - sn * x,
            t * z * z + c,
            0,
            0,
            0,
            0,
            1};
}

Vector readVector(Cpu &c, uint32_t p) {
    return {asFloat(c.read32(p)), asFloat(c.read32(p + 4)), asFloat(c.read32(p + 8))};
}

void writeVector(Cpu &c, uint32_t p, const Vector &v) {
    if (c.valid(p, 12))
        for (uint32_t i = 0; i < 3; ++i)
            c.write32(p + 4 * i, bits(v[i]));
}

} // namespace

struct Direct3DRM::State {
    Win32Process &process;
    Cpu &cpu;
    DirectX::State &dx;
    ComHost com;
    ComInterface rm{"IDirect3DRM", kIidDirect3DRM, rmMethods(false), 0};
    ComInterface rm2{"IDirect3DRM2", kIidDirect3DRM2, rmMethods(true), 0};
    ComInterface frame{"IDirect3DRMFrame", kIidFrame, frameMethods(false), 0};
    ComInterface frame2{"IDirect3DRMFrame2", kIidFrame2, frameMethods(true), 0};
    ComInterface device{"IDirect3DRMDevice", kIidDevice, deviceMethods(false), 0};
    ComInterface device2{"IDirect3DRMDevice2", kIidDevice2, deviceMethods(true), 0};
    ComInterface viewport{"IDirect3DRMViewport", kIidViewport, viewportMethods(), 0};
    ComInterface light{"IDirect3DRMLight", kIidLight, lightMethods(), 0};
    ComInterface material{"IDirect3DRMMaterial", kIidMaterial, materialMethods(), 0};
    ComInterface texture{"IDirect3DRMTexture", kIidTexture, textureMethods(false), 0};
    ComInterface texture2{"IDirect3DRMTexture2", kIidTexture2, textureMethods(true), 0};
    ComInterface mesh{"IDirect3DRMMesh", kIidMesh, meshMethods(), 0};
    /** Every interface above, each given its vtable as the object's made. */
    std::vector<ComInterface *> interfaces() {
        return {&rm,       &rm2,   &frame,    &frame2,  &device,   &device2,
                &viewport, &light, &material, &texture, &texture2, &mesh};
    }
    size_t made = 0;
    uint32_t defaultTextureColors = 32;
    uint32_t defaultTextureShades = 16;
    std::string searchPath;
    State(Win32Process &p, DirectX &d) : process(p), cpu(p.cpu()), dx(d.state()), com(p) {}
};

namespace {

// What every Retained Mode object has (IDirect3DRMObject): a name, the
// program's data, and callbacks for when it goes.
class RmObject : public ComObject {
public:
    Direct3DRM::State *s = nullptr;
    std::string name;
    uint32_t appData = 0;
    std::vector<std::pair<uint32_t, uint32_t>> destroyCallbacks; // function, argument

    /** Its class's name, as GetClassName gives it. */
    virtual const char *className() const = 0;
    /** A copy of it, for Clone; null for a kind that isn't copied. */
    virtual std::shared_ptr<RmObject> copy() const { return nullptr; }

    // The object's own methods, by name; nothing if it hasn't the method.
    std::optional<uint32_t> objectMethod(std::string_view m, Cpu &c) {
        if (m == "SetAppData") {
            appData = c.arg(1);
            return kRmOk;
        }
        if (m == "Clone") { // (outer, iid, out): a copy, as the interface asked for
            std::shared_ptr<RmObject> o = copy();
            if (!o)
                return kENotImpl;
            const ComInterface *f = o->query(ComHost::guid(c, c.arg(2)));
            if (!f || !c.valid(c.arg(3), 4))
                return kENoInterface;
            o->s = s;
            o->refs = 1;
            o->destroyCallbacks.clear();
            s->made++;
            c.write32(c.arg(3), s->com.pointer(o, *f));
            return kRmOk;
        }
        if (m == "GetAppData")
            return appData;
        if (m == "SetName") {
            name = c.arg(1) ? c.string(c.arg(1)) : "";
            return kRmOk;
        }
        if (m == "GetName" || m == "GetClassName") // (size*, buffer): the size with its NUL
            return text(c, m == "GetName" ? name : std::string(className()));
        if (m == "AddDestroyCallback") {
            destroyCallbacks.emplace_back(c.arg(1), c.arg(2));
            return kRmOk;
        }
        if (m == "DeleteDestroyCallback") {
            const std::pair<uint32_t, uint32_t> cb{c.arg(1), c.arg(2)};
            destroyCallbacks.erase(std::remove(destroyCallbacks.begin(), destroyCallbacks.end(), cb),
                                   destroyCallbacks.end());
            return kRmOk;
        }
        return std::nullopt;
    }

private:
    static uint32_t text(Cpu &c, const std::string &t) {
        const uint32_t size = c.arg(1);
        const uint32_t buffer = c.arg(2);
        if (!c.valid(size, 4))
            return kRmErrBadValue;
        const uint32_t need = uint32_t(t.size() + 1);
        if (buffer) {
            if (c.read32(size) < need || !c.valid(buffer, need))
                return kRmErrBadValue;
            c.writeString(buffer, t, need);
        }
        c.write32(size, need);
        return kRmOk;
    }
};

// A D3DCOLOR from parts 0 to 1, as D3DRMCreateColorRGB makes it.
uint32_t colorRgb(float r, float g, float b, float a = 1) {
    auto part = [](float f) { return uint32_t(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return part(a) << 24 | part(r) << 16 | part(g) << 8 | part(b);
}

// A light: what it is, its colour, how far it reaches and fades.
class Light : public RmObject {
public:
    uint32_t type = 0; // D3DRMLIGHT_AMBIENT, POINT, SPOT, DIRECTIONAL, PARALLELPOINT
    uint32_t color = 0xffffffff;
    float range = 256;
    float umbra = 0.4f;
    float penumbra = 0.5f;
    float constant = 1;
    float linear = 0;
    float quadratic = 0;
    uint32_t enableFrame = 0;

    const char *className() const override { return "Light"; }
    std::shared_ptr<RmObject> copy() const override { return std::make_shared<Light>(*this); }
    const ComInterface *query(const std::string &iid) override {
        return iid == s->light.iid || iid == kIidObject ? &s->light : nullptr;
    }

    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        if (auto r = objectMethod(m, c))
            return r;
        if (m == "SetType" || m == "SetColor") {
            (m == "SetType" ? type : color) = c.arg(1);
            return kRmOk;
        }
        if (m == "SetColorRGB") {
            color = colorRgb(asFloat(c.arg(1)), asFloat(c.arg(2)), asFloat(c.arg(3)));
            return kRmOk;
        }
        if (m == "GetType" || m == "GetColor")
            return m == "GetType" ? type : color;
        float *values[] = {&range, &umbra, &penumbra, &constant, &linear, &quadratic};
        const char *names[] = {
            "Range", "Umbra", "Penumbra", "ConstantAttenuation", "LinearAttenuation", "QuadraticAttenuation"};
        for (size_t i = 0; i < 6; ++i) {
            if (m.substr(3) != names[i])
                continue;
            if (m.substr(0, 3) == "Set") {
                *values[i] = asFloat(c.arg(1));
                return kRmOk;
            }
            return returnFloat(c, *values[i]);
        }
        if (m == "SetEnableFrame") {
            enableFrame = c.arg(1);
            return kRmOk;
        }
        if (m == "GetEnableFrame") {
            if (c.valid(c.arg(1), 4))
                c.write32(c.arg(1), enableFrame);
            return kRmOk;
        }
        return std::nullopt;
    }
};

// A material: how shiny it is, and the colours it reflects and gives off.
class Material : public RmObject {
public:
    float power = 1;
    Vector specular{1, 1, 1};
    Vector emissive{0, 0, 0};

    const char *className() const override { return "Material"; }
    std::shared_ptr<RmObject> copy() const override { return std::make_shared<Material>(*this); }
    const ComInterface *query(const std::string &iid) override {
        return iid == s->material.iid || iid == kIidObject ? &s->material : nullptr;
    }

    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        if (auto r = objectMethod(m, c))
            return r;
        if (m == "SetPower") {
            power = asFloat(c.arg(1));
            return kRmOk;
        }
        if (m == "GetPower")
            return returnFloat(c, power);
        if (m == "SetSpecular" || m == "SetEmissive") {
            (m == "SetSpecular" ? specular : emissive) =
                Vector{asFloat(c.arg(1)), asFloat(c.arg(2)), asFloat(c.arg(3))};
            return kRmOk;
        }
        if (m == "GetSpecular" || m == "GetEmissive") {
            const Vector &v = m == "GetSpecular" ? specular : emissive;
            for (int i = 0; i < 3; ++i)
                if (c.valid(c.arg(1 + i), 4))
                    c.write32(c.arg(1 + i), bits(v[size_t(i)]));
            return kRmOk;
        }
        return std::nullopt;
    }
};

// A texture: a DirectDraw surface's pixels, drawn on what it's put on.
class Texture : public RmObject {
public:
    std::shared_ptr<dx::Surface> surface;
    uint32_t colors = 0;
    uint32_t shades = 0;
    int decalWidth = 1;
    int decalHeight = 1;
    int decalOriginX = 0;
    int decalOriginY = 0;
    uint32_t decalScale = 1;
    uint32_t decalTransparency = 0;
    uint32_t decalTransparentColor = 0;
    uint64_t changes = 0; // how often the program has called Changed: new pixels or palette

    const char *className() const override { return "Texture"; }
    std::shared_ptr<RmObject> copy() const override { return std::make_shared<Texture>(*this); }
    const ComInterface *query(const std::string &iid) override {
        if (iid == s->texture2.iid)
            return &s->texture2;
        if (iid == s->texture.iid || iid == kIidVisual || iid == kIidObject)
            return &s->texture;
        return nullptr;
    }

    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        if (auto r = objectMethod(m, c))
            return r;
        if (m == "InitFromSurface") {
            surface = s->dx.com.shared<dx::Surface>(c.arg(1));
            return surface ? kRmOk : kRmErrBadValue;
        }
        if (m == "Changed") { // (pixels changed, palette changed)
            changes++;
            if (surface)
                surface->touched();
            return kRmOk;
        }
        if (m == "SetColors" || m == "SetShades" || m == "SetDecalScale" || m == "SetDecalTransparency" ||
            m == "SetDecalTransparentColor") {
            (m == "SetColors"              ? colors
             : m == "SetShades"            ? shades
             : m == "SetDecalScale"        ? decalScale
             : m == "SetDecalTransparency" ? decalTransparency
                                           : decalTransparentColor) = c.arg(1);
            return kRmOk;
        }
        if (m == "GetColors" || m == "GetShades" || m == "GetDecalScale" || m == "GetDecalTransparency" ||
            m == "GetDecalTransparentColor")
            return m == "GetColors"              ? colors
                   : m == "GetShades"            ? shades
                   : m == "GetDecalScale"        ? decalScale
                   : m == "GetDecalTransparency" ? decalTransparency
                                                 : decalTransparentColor;
        if (m == "SetDecalSize" || m == "SetDecalOrigin") { // (width or x, height or y), D3DVALUEs or longs
            if (m == "SetDecalSize") {
                decalWidth = int(asFloat(c.arg(1)));
                decalHeight = int(asFloat(c.arg(2)));
            } else {
                decalOriginX = int32_t(c.arg(1));
                decalOriginY = int32_t(c.arg(2));
            }
            return kRmOk;
        }
        if (m == "GetImage") // a texture made from a surface has no D3DRMIMAGE
            return 0u;
        return std::nullopt;
    }
};

// A mesh: groups of faces, each group its own vertices, colour, quality,
// material and texture.
class Mesh : public RmObject {
public:
    /** A D3DRMVERTEX: 36 bytes. */
    struct Vertex {
        Vector position{};
        Vector normal{};
        float u = 0;
        float v = 0;
        uint32_t color = 0;
    };
    struct Group {
        std::vector<Vertex> vertices;
        std::vector<std::vector<uint32_t>> faces; // each face's vertex indices
        uint32_t vertsPerFace = 0;                // 0: each face's count is given before it
        uint32_t color = 0xffffffff;
        uint32_t mapping = 0;
        uint32_t quality = 0x1 | 0x40 | 0x80; // D3DRMRENDER_GOURAUD
        std::shared_ptr<Material> material;
        std::shared_ptr<Texture> texture;
    };
    std::vector<Group> groups;

    const char *className() const override { return "Mesh"; }
    std::shared_ptr<RmObject> copy() const override { return std::make_shared<Mesh>(*this); }
    const ComInterface *query(const std::string &iid) override {
        return iid == s->mesh.iid || iid == kIidVisual || iid == kIidObject ? &s->mesh : nullptr;
    }

    static Vertex readVertex(Cpu &c, uint32_t p) {
        return {readVector(c, p), readVector(c, p + 12), asFloat(c.read32(p + 24)), asFloat(c.read32(p + 28)),
                c.read32(p + 32)};
    }
    static void writeVertex(Cpu &c, uint32_t p, const Vertex &v) {
        writeVector(c, p, v.position);
        writeVector(c, p + 12, v.normal);
        c.write32(p + 24, bits(v.u));
        c.write32(p + 28, bits(v.v));
        c.write32(p + 32, v.color);
    }
    Group *group(uint32_t id) { return id < groups.size() ? &groups[id] : nullptr; }

    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        if (auto r = objectMethod(m, c))
            return r;
        if (m == "AddGroup") { // (vertices, faces, vertices a face, face data, id out)
            const uint32_t vertexCount = c.arg(1);
            const uint32_t faceCount = c.arg(2);
            const uint32_t perFace = c.arg(3);
            uint32_t at = c.arg(4);
            Group g;
            g.vertices.resize(vertexCount);
            g.vertsPerFace = perFace;
            for (uint32_t f = 0; f < faceCount; ++f) {
                uint32_t n = perFace;
                if (!n) {
                    if (!c.valid(at, 4))
                        return kRmErrBadValue;
                    n = c.read32(at);
                    at += 4;
                }
                if (n > 1024 || !c.valid(at, 4 * n))
                    return kRmErrBadValue;
                std::vector<uint32_t> face(n);
                for (uint32_t i = 0; i < n; ++i, at += 4) {
                    face[i] = c.read32(at);
                    if (face[i] >= vertexCount)
                        return kRmErrBadValue;
                }
                g.faces.push_back(std::move(face));
            }
            groups.push_back(std::move(g));
            if (c.valid(c.arg(5), 4))
                c.write32(c.arg(5), uint32_t(groups.size() - 1));
            return kRmOk;
        }
        if (m == "SetVertices" || m == "GetVertices") { // (group, first, count, D3DRMVERTEX*)
            Group *g = group(c.arg(1));
            const uint32_t first = c.arg(2);
            const uint32_t count = c.arg(3);
            if (!g || first > g->vertices.size() || count > g->vertices.size() - first ||
                !c.valid(c.arg(4), 36 * count))
                return kRmErrBadValue;
            for (uint32_t i = 0; i < count; ++i) {
                if (m == "SetVertices")
                    g->vertices[first + i] = readVertex(c, c.arg(4) + 36 * i);
                else
                    writeVertex(c, c.arg(4) + 36 * i, g->vertices[first + i]);
            }
            return kRmOk;
        }
        if (m == "GetGroupCount")
            return uint32_t(groups.size());
        if (m == "Scale" || m == "Translate") { // every vertex, of every group
            const Vector v{asFloat(c.arg(1)), asFloat(c.arg(2)), asFloat(c.arg(3))};
            for (Group &g : groups)
                for (Vertex &x : g.vertices)
                    for (size_t k = 0; k < 3; ++k)
                        x.position[k] = m == "Scale" ? x.position[k] * v[k] : x.position[k] + v[k];
            return kRmOk;
        }
        if (m == "GetBox") { // D3DRMBOX: the smallest and largest corners
            Vector lo{1e30f, 1e30f, 1e30f};
            Vector hi{-1e30f, -1e30f, -1e30f};
            for (const Group &g : groups)
                for (const Vertex &x : g.vertices)
                    for (size_t k = 0; k < 3; ++k) {
                        lo[k] = std::min(lo[k], x.position[k]);
                        hi[k] = std::max(hi[k], x.position[k]);
                    }
            writeVector(c, c.arg(1), lo);
            writeVector(c, c.arg(1) + 12, hi);
            return kRmOk;
        }
        Group *g = group(c.arg(1));
        if (m.rfind("SetGroup", 0) == 0 || m.rfind("GetGroup", 0) == 0) {
            if (!g)
                return kRmErrBadValue;
            if (m == "SetGroupColor") {
                g->color = c.arg(2);
            } else if (m == "SetGroupColorRGB") {
                g->color = colorRgb(asFloat(c.arg(2)), asFloat(c.arg(3)), asFloat(c.arg(4)));
            } else if (m == "SetGroupMapping") {
                g->mapping = c.arg(2);
            } else if (m == "SetGroupQuality") {
                g->quality = c.arg(2);
            } else if (m == "SetGroupMaterial") {
                g->material = s->com.shared<Material>(c.arg(2));
            } else if (m == "SetGroupTexture") {
                g->texture = s->com.shared<Texture>(c.arg(2));
            } else if (m == "GetGroupColor") {
                return g->color;
            } else if (m == "GetGroupMapping") {
                return g->mapping;
            } else if (m == "GetGroupQuality") {
                return g->quality;
            } else if (m == "GetGroupMaterial" || m == "GetGroupTexture") { // (group, out), AddRef'd
                if (!c.valid(c.arg(2), 4))
                    return kRmErrBadValue;
                std::shared_ptr<ComObject> o = m == "GetGroupMaterial" ? std::shared_ptr<ComObject>(g->material)
                                                                       : std::shared_ptr<ComObject>(g->texture);
                c.write32(c.arg(2), o ? s->com.pointer(o, m == "GetGroupMaterial" ? s->material : s->texture) : 0);
                if (o)
                    o->refs++;
            } else if (m == "GetGroup") { // (group, vertices*, faces*, vertices a face*, data size*, data*)
                uint32_t size = 0;
                for (const auto &f : g->faces)
                    size += uint32_t(f.size()) + (g->vertsPerFace ? 0 : 1);
                const uint32_t values[4] = {uint32_t(g->vertices.size()), uint32_t(g->faces.size()), g->vertsPerFace,
                                            size};
                for (int i = 0; i < 4; ++i)
                    if (c.arg(2 + i) && c.valid(c.arg(2 + i), 4))
                        c.write32(c.arg(2 + i), values[i]);
                if (c.arg(6) && c.valid(c.arg(6), 4 * size)) {
                    uint32_t at = c.arg(6);
                    for (const auto &f : g->faces) {
                        if (!g->vertsPerFace)
                            c.write32(at, uint32_t(f.size())), at += 4;
                        for (uint32_t i : f)
                            c.write32(at, i), at += 4;
                    }
                }
            } else {
                return std::nullopt;
            }
            return kRmOk;
        }
        return std::nullopt;
    }
};

// A frame: where its children and visuals are, from its parent; the root
// of a scene has none. Its transform is its own, its world transform its
// own times its parent's.
class Frame : public RmObject {
public:
    std::weak_ptr<Frame> parent;
    std::vector<std::shared_ptr<Frame>> children;
    std::vector<std::shared_ptr<ComObject>> visuals;
    std::vector<std::shared_ptr<ComObject>> lights;
    Matrix transform = identity();
    uint32_t color = 0xffffffff;
    uint32_t materialMode = 1; // D3DRMMATERIAL_FROMFRAME
    uint32_t sortMode = 0;
    uint32_t zbufferMode = 0;
    // a scene's
    uint32_t background = 0;
    float backgroundDepth = 1;
    bool fogEnable = false;
    uint32_t fogColor = 0;
    uint32_t fogMode = 0;
    float fogStart = 0;
    float fogEnd = 1;
    float fogDensity = 1;

    const char *className() const override { return "Frame"; }

    const ComInterface *query(const std::string &iid) override {
        if (iid == s->frame2.iid)
            return &s->frame2;
        if (iid == s->frame.iid || iid == kIidVisual || iid == kIidObject)
            return &s->frame;
        return nullptr;
    }

    Matrix world() const {
        Matrix m = transform;
        for (auto p = parent.lock(); p; p = p->parent.lock())
            m = multiply(m, p->transform);
        return m;
    }
    std::shared_ptr<Frame> scene() {
        std::shared_ptr<Frame> f = std::static_pointer_cast<Frame>(shared_from_this());
        while (auto p = f->parent.lock())
            f = p;
        return f;
    }
    // A reference frame's world transform; with none, the frame's parent's.
    Matrix reference(uint32_t pointer) const {
        if (Frame *ref = s->com.as<Frame>(pointer))
            return ref->world();
        const auto p = parent.lock();
        return p ? p->world() : identity();
    }
    Matrix parentWorld() const {
        const auto p = parent.lock();
        return p ? p->world() : identity();
    }
    // `m` combined with the transform: replacing it, before it or after it.
    void combine(uint32_t how, const Matrix &m) {
        if (how == 0)
            transform = m;
        else if (how == 1)
            transform = multiply(m, transform);
        else
            transform = multiply(transform, m);
    }
    void detach() {
        if (auto p = parent.lock()) {
            auto &list = p->children;
            list.erase(std::remove_if(list.begin(), list.end(), [this](const auto &f) { return f.get() == this; }),
                       list.end());
        }
        parent.reset();
    }

    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        if (auto r = objectMethod(m, c))
            return r;
        auto vectorAt = [&](int first) {
            return Vector{asFloat(c.arg(first)), asFloat(c.arg(first + 1)), asFloat(c.arg(first + 2))};
        };
        if (m == "AddChild") {
            auto child = s->com.shared<Frame>(c.arg(1));
            if (!child)
                return kRmErrBadValue;
            // It keeps where it is in the world.
            const Matrix was = child->world();
            child->detach();
            child->parent = std::static_pointer_cast<Frame>(shared_from_this());
            child->transform = multiply(was, inverse(world()));
            children.push_back(child);
            return kRmOk;
        }
        if (m == "DeleteChild") {
            auto child = s->com.shared<Frame>(c.arg(1));
            if (!child || child->parent.lock().get() != this)
                return kRmErrBadValue;
            child->transform = child->world();
            child->detach();
            return kRmOk;
        }
        if (m == "AddVisual" || m == "AddLight") {
            ComObject *o = s->com.object(c.arg(1));
            if (!o)
                return kRmErrBadValue;
            (m == "AddVisual" ? visuals : lights).push_back(o->shared_from_this());
            return kRmOk;
        }
        if (m == "DeleteVisual" || m == "DeleteLight") {
            ComObject *o = s->com.object(c.arg(1));
            auto &list = m == "DeleteVisual" ? visuals : lights;
            list.erase(std::remove_if(list.begin(), list.end(), [o](const auto &v) { return v.get() == o; }),
                       list.end());
            return kRmOk;
        }
        if (m == "GetParent" || m == "GetScene") { // (out): AddRef'd, as COM hands out
            std::shared_ptr<Frame> f = m == "GetParent" ? parent.lock() : scene();
            if (!c.valid(c.arg(1), 4))
                return kRmErrBadValue;
            uint32_t p = 0;
            if (f) {
                p = s->com.pointer(f, iface);
                f->refs++;
            }
            c.write32(c.arg(1), p);
            return kRmOk;
        }
        if (m == "AddTransform") { // (combine, D3DRMMATRIX4D)
            Matrix t;
            for (uint32_t i = 0; i < 16; ++i)
                t[i] = asFloat(c.read32(c.arg(2) + 4 * i));
            combine(c.arg(1), t);
            return kRmOk;
        }
        if (m == "AddTranslation" || m == "AddScale") { // (combine, x, y, z)
            const Vector v = vectorAt(2);
            Matrix t = identity();
            if (m == "AddTranslation") {
                t[12] = v[0], t[13] = v[1], t[14] = v[2];
            } else {
                t[0] = v[0], t[5] = v[1], t[10] = v[2];
            }
            combine(c.arg(1), t);
            return kRmOk;
        }
        if (m == "AddRotation") { // (combine, x, y, z, theta)
            combine(c.arg(1), rotation(vectorAt(2), asFloat(c.arg(5))));
            return kRmOk;
        }
        if (m == "GetTransform") { // (D3DRMMATRIX4D out): its own
            if (!c.valid(c.arg(1), 64))
                return kRmErrBadValue;
            for (uint32_t i = 0; i < 16; ++i)
                c.write32(c.arg(1) + 4 * i, bits(transform[i]));
            return kRmOk;
        }
        if (m == "SetPosition") { // (reference, x, y, z)
            const Vector at = transformPoint(transformPoint(vectorAt(2), reference(c.arg(1))), inverse(parentWorld()));
            transform[12] = at[0], transform[13] = at[1], transform[14] = at[2];
            return kRmOk;
        }
        if (m == "GetPosition") { // (reference, out)
            const Vector here = {world()[12], world()[13], world()[14]};
            writeVector(c, c.arg(2), transformPoint(here, inverse(reference(c.arg(1)))));
            return kRmOk;
        }
        if (m == "SetOrientation") { // (reference, direction xyz, up xyz): its z and y axes
            const Matrix toParent = multiply(reference(c.arg(1)), inverse(parentWorld()));
            const Vector z = normalized(transformDirection(vectorAt(2), toParent));
            Vector y = transformDirection(vectorAt(5), toParent);
            const Vector x = normalized(cross(y, z));
            y = cross(z, x);
            const Vector axes[3] = {x, y, z};
            for (int r = 0; r < 3; ++r)
                for (int k = 0; k < 3; ++k)
                    transform[size_t(r * 4 + k)] = axes[r][size_t(k)];
            return kRmOk;
        }
        if (m == "GetOrientation") { // (reference, direction out, up out)
            const Matrix toRef = multiply(world(), inverse(reference(c.arg(1))));
            writeVector(c, c.arg(2), normalized(transformDirection({0, 0, 1}, toRef)));
            writeVector(c, c.arg(3), normalized(transformDirection({0, 1, 0}, toRef)));
            return kRmOk;
        }
        if (m == "Transform" || m == "InverseTransform") { // (out, in): the frame's coordinates and the world's
            const Matrix w = m == "Transform" ? world() : inverse(world());
            writeVector(c, c.arg(1), transformPoint(readVector(c, c.arg(2)), w));
            return kRmOk;
        }
        if (m == "SetColor" || m == "SetColorRGB") {
            color = m == "SetColor"
                        ? c.arg(1)
                        : 0xff000000u | uint32_t(std::clamp(asFloat(c.arg(1)), 0.0f, 1.0f) * 255.0f + 0.5f) << 16 |
                              uint32_t(std::clamp(asFloat(c.arg(2)), 0.0f, 1.0f) * 255.0f + 0.5f) << 8 |
                              uint32_t(std::clamp(asFloat(c.arg(3)), 0.0f, 1.0f) * 255.0f + 0.5f);
            return kRmOk;
        }
        if (m == "GetColor")
            return color;
        if (m == "SetMaterialMode" || m == "SetSortMode" || m == "SetZbufferMode") {
            (m == "SetMaterialMode" ? materialMode : m == "SetSortMode" ? sortMode : zbufferMode) = c.arg(1);
            return kRmOk;
        }
        if (m == "GetMaterialMode" || m == "GetSortMode" || m == "GetZbufferMode")
            return m == "GetMaterialMode" ? materialMode : m == "GetSortMode" ? sortMode : zbufferMode;
        if (m == "SetSceneBackground" || m == "SetSceneBackgroundRGB") {
            background = m == "SetSceneBackground"
                             ? c.arg(1)
                             : 0xff000000u | uint32_t(std::clamp(asFloat(c.arg(1)), 0.0f, 1.0f) * 255.0f + 0.5f) << 16 |
                                   uint32_t(std::clamp(asFloat(c.arg(2)), 0.0f, 1.0f) * 255.0f + 0.5f) << 8 |
                                   uint32_t(std::clamp(asFloat(c.arg(3)), 0.0f, 1.0f) * 255.0f + 0.5f);
            return kRmOk;
        }
        if (m == "GetSceneBackground")
            return background;
        if (m == "SetSceneBackgroundDepth") {
            backgroundDepth = asFloat(c.arg(1));
            return kRmOk;
        }
        if (m == "SetSceneFogEnable" || m == "SetSceneFogColor" || m == "SetSceneFogMode") {
            if (m == "SetSceneFogEnable")
                fogEnable = c.arg(1) != 0;
            else
                (m == "SetSceneFogColor" ? fogColor : fogMode) = c.arg(1);
            return kRmOk;
        }
        if (m == "SetSceneFogParams") { // (start, end, density)
            fogStart = asFloat(c.arg(1));
            fogEnd = asFloat(c.arg(2));
            fogDensity = asFloat(c.arg(3));
            return kRmOk;
        }
        if (m == "GetSceneFogEnable" || m == "GetSceneFogColor" || m == "GetSceneFogMode")
            return m == "GetSceneFogEnable" ? uint32_t(fogEnable) : m == "GetSceneFogColor" ? fogColor : fogMode;
        return std::nullopt;
    }
};

// A device: what a viewport draws to, on a Direct3D device and the
// surface it draws into.
class RmDevice : public RmObject {
public:
    std::shared_ptr<dx::Device> d3d;
    uint32_t d3dPointer = 0; // the program's IDirect3DDevice2, as it gave it
    uint32_t direct3dPointer = 0;
    uint32_t quality = 0x1 | 0x40 | 0x80; // D3DRMRENDER_GOURAUD: Gouraud, solid, lit
    uint32_t textureQuality = 0;          // D3DRMTEXTURE_NEAREST
    uint32_t shades = 32;
    uint32_t bufferCount = 1;
    uint32_t renderMode = 0;
    bool dither = true;
    std::vector<std::pair<uint32_t, uint32_t>> updateCallbacks; // function, argument

    const char *className() const override { return "Device"; }

    const ComInterface *query(const std::string &iid) override {
        if (iid == s->device2.iid)
            return &s->device2;
        if (iid == s->device.iid || iid == kIidObject)
            return &s->device;
        return nullptr;
    }
    int width() const { return d3d && d3d->target ? d3d->target->d.width : 0; }
    int height() const { return d3d && d3d->target ? d3d->target->d.height : 0; }

    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        if (auto r = objectMethod(m, c))
            return r;
        if (m == "GetWidth")
            return uint32_t(width());
        if (m == "GetHeight")
            return uint32_t(height());
        if (m == "SetQuality" || m == "SetTextureQuality" || m == "SetShades" || m == "SetBufferCount" ||
            m == "SetRenderMode") {
            (m == "SetQuality"          ? quality
             : m == "SetTextureQuality" ? textureQuality
             : m == "SetShades"         ? shades
             : m == "SetBufferCount"    ? bufferCount
                                        : renderMode) = c.arg(1);
            return kRmOk;
        }
        if (m == "GetQuality" || m == "GetTextureQuality" || m == "GetShades" || m == "GetBufferCount" ||
            m == "GetRenderMode")
            return m == "GetQuality"          ? quality
                   : m == "GetTextureQuality" ? textureQuality
                   : m == "GetShades"         ? shades
                   : m == "GetBufferCount"    ? bufferCount
                                              : renderMode;
        if (m == "SetDither") {
            dither = c.arg(1) != 0;
            return kRmOk;
        }
        if (m == "GetDither")
            return uint32_t(dither);
        if (m == "GetColorModel")
            return 2u;                                               // D3DCOLOR_RGB
        if (m == "GetDirect3DDevice" || m == "GetDirect3DDevice2") { // (out), AddRef'd
            if (!c.valid(c.arg(1), 4))
                return kRmErrBadValue;
            if (d3d) {
                const ComInterface &want = s->dx.interfaces.device2;
                c.write32(c.arg(1), s->dx.com.pointer(d3d, want));
                d3d->refs++;
            } else {
                c.write32(c.arg(1), 0);
            }
            return kRmOk;
        }
        if (m == "AddUpdateCallback") {
            updateCallbacks.emplace_back(c.arg(1), c.arg(2));
            return kRmOk;
        }
        if (m == "DeleteUpdateCallback") {
            const std::pair<uint32_t, uint32_t> cb{c.arg(1), c.arg(2)};
            updateCallbacks.erase(std::remove(updateCallbacks.begin(), updateCallbacks.end(), cb),
                                  updateCallbacks.end());
            return kRmOk;
        }
        if (m == "Update") // the program flips its own surfaces
            return kRmOk;
        return std::nullopt;
    }
};

// A viewport: a rectangle of a device, seen through a camera frame.
class Viewport : public RmObject {
public:
    std::shared_ptr<RmDevice> device;
    std::shared_ptr<Frame> camera;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    float front = 1;
    float back = 100;
    float field = 0.5f;
    uint32_t projection = 0; // D3DRMPROJECT_PERSPECTIVE
    bool uniformScaling = true;
    // SetPlane's: the view's edges on the front plane; else from the field.
    bool planeSet = false;
    float left = 0;
    float right = 0;
    float bottom = 0;
    float top = 0;

    const char *className() const override { return "Viewport"; }

    const ComInterface *query(const std::string &iid) override {
        return iid == s->viewport.iid || iid == kIidObject ? &s->viewport : nullptr;
    }

    // The view's edges on the front plane: SetPlane's, or the field's, the
    // shorter side of the rectangle (both, scaled uniformly) spanning it.
    std::array<float, 4> plane() const {
        if (planeSet)
            return {left, right, bottom, top};
        float w = field;
        float h = field;
        if (uniformScaling && width > 0 && height > 0) {
            if (width > height)
                w = field * float(width) / float(height);
            else
                h = field * float(height) / float(width);
        }
        return {-w, w, -h, h};
    }

    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        if (auto r = objectMethod(m, c))
            return r;
        if (m == "Clear") { // its rectangle, to the camera's scene's background, and its depth
            if (!device || !device->d3d)
                return kRmOk;
            dx::Device &d = *device->d3d;
            const dx::Viewport was = d.viewport;
            d.viewport = {x, y, width, height, 0, 1};
            const uint32_t color = camera ? camera->scene()->background : 0;
            d.clear(0, 0, 0x1 | 0x2, color, 1.0f);
            d.viewport = was;
            return kRmOk;
        }
        if (m == "Render") // nothing in its scenes is drawable yet
            return kRmOk;
        if (m == "ForceUpdate")
            return kRmOk;
        if (m == "SetFront" || m == "SetBack" || m == "SetField") {
            (m == "SetFront" ? front : m == "SetBack" ? back : field) = asFloat(c.arg(1));
            return kRmOk;
        }
        if (m == "GetFront" || m == "GetBack" || m == "GetField")
            return returnFloat(c, m == "GetFront" ? front : m == "GetBack" ? back : field);
        if (m == "SetProjection") {
            projection = c.arg(1);
            return kRmOk;
        }
        if (m == "GetProjection")
            return projection;
        if (m == "SetUniformScaling") {
            uniformScaling = c.arg(1) != 0;
            return kRmOk;
        }
        if (m == "GetUniformScaling")
            return uint32_t(uniformScaling);
        if (m == "SetCamera") {
            camera = s->com.shared<Frame>(c.arg(1));
            return camera ? kRmOk : kRmErrBadValue;
        }
        if (m == "GetCamera" || m == "GetDevice") { // (out), AddRef'd
            if (!c.valid(c.arg(1), 4))
                return kRmErrBadValue;
            std::shared_ptr<ComObject> o;
            const ComInterface *f = nullptr;
            if (m == "GetCamera" && camera)
                o = camera, f = &s->frame;
            if (m == "GetDevice" && device)
                o = device, f = &s->device;
            c.write32(c.arg(1), o ? s->com.pointer(o, *f) : 0);
            if (o)
                o->refs++;
            return kRmOk;
        }
        if (m == "Configure") { // (x, y, width, height)
            x = int32_t(c.arg(1));
            y = int32_t(c.arg(2));
            width = int32_t(c.arg(3));
            height = int32_t(c.arg(4));
            return kRmOk;
        }
        if (m == "GetX" || m == "GetY" || m == "GetWidth" || m == "GetHeight")
            return uint32_t(m == "GetX" ? x : m == "GetY" ? y : m == "GetWidth" ? width : height);
        if (m == "SetPlane") { // (left, right, bottom, top)
            planeSet = true;
            left = asFloat(c.arg(1));
            right = asFloat(c.arg(2));
            bottom = asFloat(c.arg(3));
            top = asFloat(c.arg(4));
            return kRmOk;
        }
        if (m == "GetPlane") { // (left*, right*, bottom*, top*)
            const std::array<float, 4> p = plane();
            for (int i = 0; i < 4; ++i)
                if (c.valid(c.arg(1 + i), 4))
                    c.write32(c.arg(1 + i), bits(p[size_t(i)]));
            return kRmOk;
        }
        return std::nullopt;
    }
};

// The Direct3DRM object: it makes the others.
class Rm : public ComObject {
public:
    Direct3DRM::State *s = nullptr;

    const ComInterface *query(const std::string &iid) override {
        if (iid == s->rm.iid)
            return &s->rm;
        if (iid == s->rm2.iid)
            return &s->rm2;
        return nullptr;
    }

    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        const bool two = &iface == &s->rm2;
        if (m == "CreateFrame") { // (parent, out): IDirect3DRMFrame, or Frame2 from IDirect3DRM2
            if (!c.valid(c.arg(2), 4))
                return kRmErrBadValue;
            auto f = make<Frame>();
            if (auto p = s->com.shared<Frame>(c.arg(1))) {
                f->parent = p;
                p->children.push_back(f);
            }
            c.write32(c.arg(2), s->com.pointer(f, two ? s->frame2 : s->frame));
            return kRmOk;
        }
        if (m == "CreateViewport") { // (device, camera, x, y, width, height, out)
            auto device = s->com.shared<RmDevice>(c.arg(1));
            auto camera = s->com.shared<Frame>(c.arg(2));
            if (!device || !camera || !c.valid(c.arg(7), 4))
                return kRmErrBadValue;
            auto v = make<Viewport>();
            v->device = device;
            v->camera = camera;
            v->x = int32_t(c.arg(3));
            v->y = int32_t(c.arg(4));
            v->width = int32_t(c.arg(5));
            v->height = int32_t(c.arg(6));
            c.write32(c.arg(7), s->com.pointer(v, s->viewport));
            return kRmOk;
        }
        if (m == "CreateLight" || m == "CreateLightRGB") { // (type, colour or r, g, b, out)
            const uint32_t out = c.arg(m == "CreateLight" ? 3 : 5);
            if (!c.valid(out, 4))
                return kRmErrBadValue;
            auto l = make<Light>();
            l->type = c.arg(1);
            l->color =
                m == "CreateLight" ? c.arg(2) : colorRgb(asFloat(c.arg(2)), asFloat(c.arg(3)), asFloat(c.arg(4)));
            c.write32(out, s->com.pointer(l, s->light));
            return kRmOk;
        }
        if (m == "CreateMaterial") { // (power, out)
            if (!c.valid(c.arg(2), 4))
                return kRmErrBadValue;
            auto mat = make<Material>();
            mat->power = asFloat(c.arg(1));
            c.write32(c.arg(2), s->com.pointer(mat, s->material));
            return kRmOk;
        }
        if (m == "CreateTextureFromSurface") { // (surface, out): IDirect3DRMTexture2 from IDirect3DRM2
            auto surface = s->dx.com.shared<dx::Surface>(c.arg(1));
            if (!surface || !c.valid(c.arg(2), 4))
                return kRmErrBadValue;
            auto t = make<Texture>();
            t->surface = surface;
            t->colors = s->defaultTextureColors;
            t->shades = s->defaultTextureShades;
            c.write32(c.arg(2), s->com.pointer(t, two ? s->texture2 : s->texture));
            return kRmOk;
        }
        if (m == "CreateMesh") { // (out)
            if (!c.valid(c.arg(1), 4))
                return kRmErrBadValue;
            c.write32(c.arg(1), s->com.pointer(make<Mesh>(), s->mesh));
            return kRmOk;
        }
        if (m == "SetDefaultTextureColors" || m == "SetDefaultTextureShades") {
            (m == "SetDefaultTextureColors" ? s->defaultTextureColors : s->defaultTextureShades) = c.arg(1);
            return kRmOk;
        }
        if (m == "SetSearchPath" || m == "AddSearchPath") { // a list of folders, between semicolons
            const std::string path = c.arg(1) ? c.string(c.arg(1)) : "";
            if (m == "SetSearchPath" || s->searchPath.empty())
                s->searchPath = path;
            else if (!path.empty())
                s->searchPath += ";" + path;
            return kRmOk;
        }
        if (m == "GetSearchPath") { // (size*, buffer): the size with its NUL
            const uint32_t need = uint32_t(s->searchPath.size() + 1);
            if (!c.valid(c.arg(1), 4))
                return kRmErrBadValue;
            if (c.arg(2)) {
                if (c.read32(c.arg(1)) < need || !c.valid(c.arg(2), need))
                    return kRmErrBadValue;
                c.writeString(c.arg(2), s->searchPath, need);
            }
            c.write32(c.arg(1), need);
            return kRmOk;
        }
        if (m == "Tick") // nothing moves by itself here: no move callbacks or velocities yet
            return kRmOk;
        if (m == "CreateDeviceFromD3D") { // (IDirect3D2*, IDirect3DDevice2*, out)
            auto d3d = s->dx.com.shared<dx::Device>(c.arg(2));
            if (!d3d || !c.valid(c.arg(3), 4))
                return kRmErrBadValue;
            auto d = make<RmDevice>();
            d->d3d = d3d;
            d->d3dPointer = c.arg(2);
            d->direct3dPointer = c.arg(1);
            c.write32(c.arg(3), s->com.pointer(d, two ? s->device2 : s->device));
            return kRmOk;
        }
        return std::nullopt;
    }

    template<class T>
    std::shared_ptr<T> make() const {
        auto o = std::make_shared<T>();
        o->s = s;
        s->made++;
        return o;
    }
};

} // namespace

Direct3DRM::~Direct3DRM() = default;

Direct3DRM::Direct3DRM(Win32Process &process, DirectX &directX) : _s(std::make_unique<State>(process, directX)) {
    State *s = _s.get();
    for (ComInterface *f : s->interfaces())
        s->com.define(*f);
    // Direct3DRMCreate(out): an IDirect3DRM.
    process.provideStdcall("d3drm.dll", "Direct3DRMCreate", 1, [s](Cpu &c) -> uint32_t {
        if (!c.valid(c.arg(0), 4))
            return kRmErrBadValue;
        auto rm = std::make_shared<Rm>();
        rm->s = s;
        c.write32(c.arg(0), s->com.pointer(rm, s->rm));
        return kRmOk;
    });
    // D3DRMCreateColorRGBA(r, g, b, a): a D3DCOLOR, each part 0 to 1.
    process.provideStdcall("d3drm.dll", "D3DRMCreateColorRGBA", 4, [](Cpu &c) -> uint32_t {
        auto part = [&](int i) {
            float f;
            const uint32_t v = c.arg(i);
            std::memcpy(&f, &v, 4);
            return uint32_t(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f);
        };
        return part(3) << 24 | part(0) << 16 | part(1) << 8 | part(2);
    });
}

size_t Direct3DRM::objectsMade() const {
    return _s->made;
}

} // namespace reluva::x86
