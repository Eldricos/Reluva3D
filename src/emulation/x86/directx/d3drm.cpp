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
#include <optional>
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

// IDirect3DRMPickedArray: no IDirect3DRMObject methods.
std::vector<ComInterface::Method> pickedArrayMethods() {
    return with({{"GetSize", 0}, {"GetPick", 4}});
}

// IDirect3DRMFrameArray, LightArray and VisualArray: no IDirect3DRMObject
// methods either.
std::vector<ComInterface::Method> arrayMethods() {
    return with({{"GetSize", 0}, {"GetElement", 2}});
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

// The inverse of any invertible matrix (Gauss-Jordan, with partial
// pivoting): a projection's, which isn't affine.
Matrix inverseGeneral(const Matrix &m) {
    double a[4][8];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 8; ++j)
            a[i][j] = j < 4 ? m[size_t(i * 4 + j)] : (j - 4 == i ? 1 : 0);
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int r = col + 1; r < 4; ++r)
            if (std::fabs(a[r][col]) > std::fabs(a[pivot][col]))
                pivot = r;
        if (std::fabs(a[pivot][col]) < 1e-12)
            return identity();
        std::swap(a[col], a[pivot]);
        const double k = a[col][col];
        for (double &v : a[col])
            v /= k;
        for (int r = 0; r < 4; ++r)
            if (r != col) {
                const double f = a[r][col];
                for (int j = 0; j < 8; ++j)
                    a[r][j] -= f * a[col][j];
            }
    }
    Matrix r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r[size_t(i * 4 + j)] = float(a[i][j + 4]);
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
    // Handed out by Pick only, never asked for by its IID.
    ComInterface pickedArray{"IDirect3DRMPickedArray", "", pickedArrayMethods(), 0};
    ComInterface frameArray{"IDirect3DRMFrameArray", "", arrayMethods(), 0};
    ComInterface lightArray{"IDirect3DRMLightArray", "", arrayMethods(), 0};
    ComInterface visualArray{"IDirect3DRMVisualArray", "", arrayMethods(), 0};
    /** Every interface above, each given its vtable as the object's made. */
    std::vector<ComInterface *> interfaces() {
        return {&rm,       &rm2,     &frame,    &frame2, &device,      &device2,    &viewport,   &light,
                &material, &texture, &texture2, &mesh,   &pickedArray, &frameArray, &lightArray, &visualArray};
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

// An array of objects (IDirect3DRMFrameArray, LightArray, VisualArray):
// a frame's children, lights or visuals, or a pick's frames. Each element
// is handed out as `element`, or, with none, as a visual.
class RmArray : public ComObject {
public:
    Direct3DRM::State *s = nullptr;
    const ComInterface *element = nullptr;
    std::vector<std::shared_ptr<ComObject>> items;
    const ComInterface *query(const std::string &) override { return nullptr; }
    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        if (m == "GetSize")
            return uint32_t(items.size());
        if (m == "GetElement") { // (index, out), AddRef'd
            if (c.arg(1) >= items.size() || !c.valid(c.arg(2), 4))
                return kRmErrBadValue;
            const std::shared_ptr<ComObject> &o = items[c.arg(1)];
            const ComInterface *f = element ? element : o->query(kIidVisual);
            c.write32(c.arg(2), f ? s->com.pointer(o, *f) : 0);
            if (f)
                o->refs++;
            return kRmOk;
        }
        return std::nullopt;
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
    uint64_t seen = 0;    // `changes` when it was last drawn

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
        uint32_t quality = 0x1 | 0x8 | 0x80; // D3DRMRENDER_GOURAUD
        std::shared_ptr<Material> material;
        std::shared_ptr<Texture> texture;
    };
    std::vector<Group> groups;

    const char *className() const override { return "Mesh"; }
    std::shared_ptr<RmObject> copy() const override { return std::make_shared<Mesh>(*this); }
    const ComInterface *query(const std::string &iid) override {
        return iid == s->mesh.iid || iid == kIidVisual || iid == kIidObject ? &s->mesh : nullptr;
    }

private:
    std::optional<std::array<Vector, 2>> _bounds;
    bool _boundsMade = false;

public:
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

    // The box around every group's vertices (smallest, largest corner),
    // made when asked and forgotten when the mesh changes; none if empty.
    const std::optional<std::array<Vector, 2>> &bounds() {
        if (!_boundsMade) {
            _bounds.reset();
            for (const Group &g : groups)
                for (const Vertex &v : g.vertices) {
                    if (!_bounds)
                        _bounds = std::array<Vector, 2>{v.position, v.position};
                    for (size_t k = 0; k < 3; ++k) {
                        (*_bounds)[0][k] = std::min((*_bounds)[0][k], v.position[k]);
                        (*_bounds)[1][k] = std::max((*_bounds)[1][k], v.position[k]);
                    }
                }
            _boundsMade = true;
        }
        return _bounds;
    }

    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        if (m.substr(0, 3) != "Get") // anything but a Get may change the vertices
            _boundsMade = false;
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
        if (m == "GetChildren" || m == "GetLights" || m == "GetVisuals") { // (array out)
            if (!c.valid(c.arg(1), 4))
                return kRmErrBadValue;
            auto array = std::make_shared<RmArray>();
            array->s = s;
            const ComInterface *kind = &s->visualArray;
            if (m == "GetChildren") {
                array->element = &s->frame;
                array->items.assign(children.begin(), children.end());
                kind = &s->frameArray;
            } else if (m == "GetLights") {
                array->element = &s->light;
                array->items = lights;
                kind = &s->lightArray;
            } else {
                array->items = visuals;
            }
            c.write32(c.arg(1), s->com.pointer(array, *kind));
            return kRmOk;
        }
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
    uint32_t quality = 0x1 | 0x8 | 0x80; // D3DRMRENDER_GOURAUD: Gouraud, solid, lit
    uint32_t textureQuality = 0;         // D3DRMTEXTURE_NEAREST
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

// What Pick found: nothing, until scenes are drawn (and so can be picked).
// What's under a point: a visual, the frames from the root down to the one
// it's in, the face and group, and the point on the screen (x, y and its
// depth, 0 to 1).
struct PickHit {
    std::shared_ptr<ComObject> visual;
    std::vector<std::shared_ptr<Frame>> path;
    uint32_t face = 0;
    int32_t group = 0;
    float position[3]{};
};

// What a pick found, nearest first.
class PickedArray : public ComObject {
public:
    Direct3DRM::State *s = nullptr;
    std::vector<PickHit> hits;
    const ComInterface *query(const std::string &) override { return nullptr; }
    std::optional<uint32_t> invoke(const ComInterface &iface, int index, Cpu &c) override {
        const std::string_view m = iface.methods[size_t(index)].name;
        if (m == "GetSize")
            return uint32_t(hits.size());
        if (m == "GetPick") { // (index, visual out, frame array out, D3DRMPICKDESC*)
            if (c.arg(1) >= hits.size())
                return kRmErrBadValue;
            const PickHit &h = hits[c.arg(1)];
            if (c.valid(c.arg(2), 4)) {
                const ComInterface *visual = h.visual->query(kIidVisual);
                c.write32(c.arg(2), visual ? s->com.pointer(h.visual, *visual) : 0);
                if (visual)
                    h.visual->refs++;
            }
            if (c.valid(c.arg(3), 4)) {
                auto frames = std::make_shared<RmArray>();
                frames->s = s;
                frames->element = &s->frame;
                frames->items.assign(h.path.begin(), h.path.end());
                c.write32(c.arg(3), s->com.pointer(frames, s->frameArray));
            }
            if (c.valid(c.arg(4), 20)) {
                c.write32(c.arg(4), h.face);
                c.write32(c.arg(4) + 4, uint32_t(h.group));
                for (uint32_t i = 0; i < 3; ++i)
                    c.write32(c.arg(4) + 8 + 4 * i, bits(h.position[i]));
            }
            return kRmOk;
        }
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

    // The camera's projection onto the front plane (left-handed, looking
    // down its z, y up), as Direct3D takes it: x and y to -1..1 across the
    // view's edges, z to 0..1 from the front plane to the back (inferred).
    Matrix projectionMatrix() const {
        const auto [l, r, b, t] = plane();
        const float n = front;
        const float f = back > front ? back : front * 2;
        if (projection == 1 || projection == 3) // D3DRMPROJECT_ORTHOGRAPHIC, RIGHTHANDORTHOGRAPHIC
            return {2 / (r - l),       0,           0, 0, 0, 2 / (t - b), 0, 0, 0, 0, 1 / (f - n), 0, (l + r) / (l - r),
                    (t + b) / (b - t), n / (n - f), 1};
        return {2 * n / (r - l),   0,           0, 0, 0, 2 * n / (t - b), 0, 0, (l + r) / (l - r),
                (t + b) / (b - t), f / (f - n), 1, 0, 0, n * f / (n - f), 0};
    }

    void render(Frame &root);

    // Whether a box, taken to clip space by `m`, is wholly outside the view:
    // all its corners beyond one of its planes.
    static bool outside(const std::array<Vector, 2> &box, const Matrix &m) {
        int beyond[6] = {};
        for (int i = 0; i < 8; ++i) {
            const Vector q{box[size_t(i & 1)][0], box[size_t((i >> 1) & 1)][1], box[size_t((i >> 2) & 1)][2]};
            const float cx = q[0] * m[0] + q[1] * m[4] + q[2] * m[8] + m[12];
            const float cy = q[0] * m[1] + q[1] * m[5] + q[2] * m[9] + m[13];
            const float cz = q[0] * m[2] + q[1] * m[6] + q[2] * m[10] + m[14];
            const float cw = q[0] * m[3] + q[1] * m[7] + q[2] * m[11] + m[15];
            beyond[0] += cx < -cw;
            beyond[1] += cx > cw;
            beyond[2] += cy < -cw;
            beyond[3] += cy > cw;
            beyond[4] += cz < 0;
            beyond[5] += cz > cw;
        }
        return std::any_of(std::begin(beyond), std::end(beyond), [](int n) { return n == 8; });
    }

    /** A frame where it's drawn: its world transform, and the frames from the root down to it. */
    struct Placed {
        Frame *frame;
        Matrix world;
        std::vector<Frame *> path;
    };
    // The frame, the frames in it, and those made its visuals, each where it's drawn.
    static std::vector<Placed> place(Frame &root) {
        std::vector<Placed> placed;
        std::vector<Placed> todo{{&root, root.world(), {&root}}};
        while (!todo.empty() && placed.size() < 65536) { // a frame its own visual stops at the limit
            Placed p = std::move(todo.back());
            todo.pop_back();
            auto add = [&](Frame *f) {
                std::vector<Frame *> path = p.path;
                path.push_back(f);
                todo.push_back({f, multiply(f->transform, p.world), std::move(path)});
            };
            for (const auto &child : p.frame->children)
                add(child.get());
            for (const auto &v : p.frame->visuals)
                if (auto *vf = dynamic_cast<Frame *>(v.get()))
                    add(vf);
            placed.push_back(std::move(p));
        }
        return placed;
    }

    std::vector<PickHit> pick(int px, int py);

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
        if (m == "Render") { // (frame): it and what's in it, seen through the camera
            auto frame = s->com.shared<Frame>(c.arg(1));
            if (!frame)
                return kRmErrBadValue;
            render(*frame);
            return kRmOk;
        }
        if (m == "Pick") { // (x, y, out): what's under a point
            if (!c.valid(c.arg(3), 4))
                return kRmErrBadValue;
            auto picked = std::make_shared<PickedArray>();
            picked->s = s;
            picked->hits = pick(int32_t(c.arg(1)), int32_t(c.arg(2)));
            c.write32(c.arg(3), s->com.pointer(picked, s->pickedArray));
            return kRmOk;
        }
        if (m == "Transform") { // (D3DRMVECTOR4D* screen out, D3DVECTOR* world)
            if (!camera || !c.valid(c.arg(1), 16) || !c.valid(c.arg(2), 12))
                return kRmErrBadValue;
            // Homogeneous, the point on the screen (its pixel, and its depth
            // 0 to 1) times w, the clip space's w (inferred: the
            // documentation says only "a four-element homogeneous vector").
            const Vector p = readVector(c, c.arg(2));
            const Matrix all = multiply(inverse(camera->world()), projectionMatrix());
            const float cx = p[0] * all[0] + p[1] * all[4] + p[2] * all[8] + all[12];
            const float cy = p[0] * all[1] + p[1] * all[5] + p[2] * all[9] + all[13];
            const float cz = p[0] * all[2] + p[1] * all[6] + p[2] * all[10] + all[14];
            const float cw = p[0] * all[3] + p[1] * all[7] + p[2] * all[11] + all[15];
            const float out[4] = {(float(x) + float(width) / 2) * cw + cx * float(width) / 2,
                                  (float(y) + float(height) / 2) * cw - cy * float(height) / 2, cz, cw};
            for (uint32_t i = 0; i < 4; ++i)
                c.write32(c.arg(1) + 4 * i, bits(out[i]));
            return kRmOk;
        }
        if (m == "InverseTransform") { // (D3DVECTOR* world out, D3DRMVECTOR4D* screen): Transform undone
            if (!camera || width <= 0 || height <= 0 || !c.valid(c.arg(1), 12) || !c.valid(c.arg(2), 16))
                return kRmErrBadValue;
            float h[4];
            for (uint32_t i = 0; i < 4; ++i)
                h[i] = asFloat(c.read32(c.arg(2) + 4 * i));
            const float cw = h[3];
            const float clip[4] = {(h[0] - (float(x) + float(width) / 2) * cw) / (float(width) / 2),
                                   ((float(y) + float(height) / 2) * cw - h[1]) / (float(height) / 2), h[2], cw};
            const Matrix back = inverseGeneral(multiply(inverse(camera->world()), projectionMatrix()));
            float r[4];
            for (int j = 0; j < 4; ++j)
                r[j] = clip[0] * back[size_t(j)] + clip[1] * back[size_t(4 + j)] + clip[2] * back[size_t(8 + j)] +
                       clip[3] * back[size_t(12 + j)];
            writeVector(c, c.arg(1),
                        r[3] != 0 ? Vector{r[0] / r[3], r[1] / r[3], r[2] / r[3]} : Vector{r[0], r[1], r[2]});
            return kRmOk;
        }
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

namespace {

// D3DRMLIGHTTYPE.
enum : uint32_t { kLightAmbient = 0, kLightPoint = 1, kLightSpot = 2, kLightDirectional = 3, kLightParallelPoint = 4 };
// D3DRMRENDERQUALITY's parts.
constexpr uint32_t kShadeMask = 0x7;
constexpr uint32_t kLightOn = 0x8; // D3DRMLIGHT_ON
// The device's render states and texture stage states used here
// (DirectX 5's and 7's numbers).
enum : uint32_t {
    kRsZEnable = 7,
    kRsShadeMode = 9,
    kRsZWriteEnable = 14,
    kRsSrcBlend = 19,
    kRsDestBlend = 20,
    kRsCullMode = 22,
    kRsZFunc = 23,
    kRsAlphaBlendEnable = 27,
    kRsSpecularEnable = 29,
    kRsColorKeyEnable = 41,
    kRsLighting = 137,
    kRsAmbient = 139,
    kRsColorVertex = 141,
    kRsNormalizeNormals = 143,
};
enum : uint32_t {
    kTssColorOp = 1,
    kTssColorArg1 = 2,
    kTssColorArg2 = 3,
    kTssAlphaOp = 4,
    kTssAlphaArg1 = 5,
    kTssAlphaArg2 = 6,
    kTssAddressU = 13,
    kTssAddressV = 14,
    kTssMagFilter = 16,
    kTssMinFilter = 17
};
// A mesh's vertex as Direct3D takes it (D3DFVF_XYZ | NORMAL | DIFFUSE |
// TEX1): its position, normal, the group's colour (for when it's unlit) and
// its texture coordinates.
constexpr uint32_t kRmVertexFvf = 0x152;

dx::Color colorOf(uint32_t argb) {
    return {float((argb >> 16) & 0xff) / 255.0f, float((argb >> 8) & 0xff) / 255.0f, float(argb & 0xff) / 255.0f,
            float(argb >> 24) / 255.0f};
}

} // namespace

// Draws the frame, its visuals and its children's, lit by the lights in
// them, onto the device through Direct3D's pipeline; the device's own
// states are put back after.
void Viewport::render(Frame &root) {
    if (!device || !device->d3d || !camera || width <= 0 || height <= 0)
        return;
    dx::Device &d = *device->d3d;
    const auto savedStates = d.renderStates;
    const auto savedStages = d.stageStates;
    const auto savedTexture = d.textures[0];
    const auto savedTransforms = d.transforms;
    const auto savedLights = d.lights;
    const dx::Material savedMaterial = d.material;
    const dx::Viewport savedViewport = d.viewport;

    d.viewport = {x, y, width, height, 0, 1};
    d.transforms[2] = inverse(camera->world());
    d.transforms[3] = projectionMatrix();
    const Matrix viewProjection = multiply(d.transforms[2], d.transforms[3]);

    const std::vector<Placed> placed = place(root);

    // The lights: ambient ones added together, the others Direct3D's.
    d.lights.clear();
    dx::Color ambient;
    uint32_t lightIndex = 0;
    for (const Placed &p : placed)
        for (const auto &o : p.frame->lights) {
            const auto *l = dynamic_cast<Light *>(o.get());
            if (!l)
                continue;
            const dx::Color color = colorOf(l->color);
            if (l->type == kLightAmbient) {
                ambient.r += color.r, ambient.g += color.g, ambient.b += color.b;
                continue;
            }
            dx::Light dl;
            dl.type = l->type == kLightDirectional ? 3 : l->type == kLightSpot ? 2 : 1;
            dl.diffuse = color;
            dl.specular = color;
            const Vector at = transformPoint({0, 0, 0}, p.world);
            const Vector dir = normalized(transformDirection({0, 0, 1}, p.world)); // a light shines down its z
            std::copy(at.begin(), at.end(), dl.position);
            std::copy(dir.begin(), dir.end(), dl.direction);
            dl.range = l->type == kLightParallelPoint ? 1e30f : l->range;
            dl.att0 = l->constant;
            dl.att1 = l->linear;
            dl.att2 = l->quadratic;
            dl.theta = 2 * l->umbra; // umbra and penumbra as half angles (inferred)
            dl.phi = 2 * l->penumbra;
            dl.falloff = 1;
            dl.enabled = true;
            d.lights[lightIndex++] = dl;
        }
    auto clamp1 = [](float v) { return uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    d.renderStates[kRsAmbient] = 0xff000000u | clamp1(ambient.r) << 16 | clamp1(ambient.g) << 8 | clamp1(ambient.b);
    d.renderStates[kRsZEnable] = d.depth ? 1 : 0;
    d.renderStates[kRsZWriteEnable] = d.depth ? 1 : 0;
    d.renderStates[kRsZFunc] = 4;    // D3DCMP_LESSEQUAL
    d.renderStates[kRsCullMode] = 3; // a face's front is clockwise, as Direct3D's
    d.renderStates[kRsColorVertex] = 0;
    d.renderStates[kRsNormalizeNormals] = 1; // a scaled frame's normals, lit as unit ones
    d.renderStates[kRsColorKeyEnable] = 0;

    std::vector<uint8_t> vertices;
    std::vector<uint32_t> indices;
    for (const Placed &p : placed)
        for (const auto &v : p.frame->visuals) {
            auto *mesh = dynamic_cast<Mesh *>(v.get());
            if (!mesh || !mesh->bounds() || outside(*mesh->bounds(), multiply(p.world, viewProjection)))
                continue;
            d.transforms[1] = p.world;
            for (const Mesh::Group &g : mesh->groups) {
                if (g.vertices.empty() || g.faces.empty())
                    continue;
                const uint32_t quality = g.quality & device->quality;
                const uint32_t argb = p.frame->materialMode == 2 ? p.frame->color : g.color; // D3DRMMATERIAL_FROMFRAME
                const dx::Color color = colorOf(argb);
                dx::Material mat;
                mat.diffuse = color;
                mat.ambient = color;
                if (g.material) {
                    mat.power = g.material->power;
                    mat.specular = {g.material->specular[0], g.material->specular[1], g.material->specular[2], 1};
                    mat.emissive = {g.material->emissive[0], g.material->emissive[1], g.material->emissive[2], 1};
                }
                d.material = mat;
                d.renderStates[kRsLighting] = (quality & kLightOn) ? 1 : 0;
                d.renderStates[kRsSpecularEnable] = g.material && mat.power > 0 ? 1 : 0;
                d.renderStates[kRsShadeMode] = (quality & kShadeMask) == 0 ? 1 : 2; // flat, else Gouraud
                const bool translucent = color.a < 1;
                d.renderStates[kRsAlphaBlendEnable] = translucent ? 1 : 0;
                d.renderStates[kRsSrcBlend] = 5;  // D3DBLEND_SRCALPHA
                d.renderStates[kRsDestBlend] = 6; // D3DBLEND_INVSRCALPHA

                auto &stage = d.stageStates[0];
                std::shared_ptr<dx::Surface> surface = g.texture ? g.texture->surface : nullptr;
                if (surface && g.texture->changes != g.texture->seen) {
                    g.texture->seen = g.texture->changes;
                    surface->touched(); // its palette may be new: decoded again
                }
                d.textures[0] = surface;
                stage[kTssColorOp] = surface ? 4 : 2;   // MODULATE the texture and the colour, or the colour
                stage[kTssColorArg1] = surface ? 2 : 0; // D3DTA_TEXTURE, D3DTA_DIFFUSE
                stage[kTssColorArg2] = 0;
                stage[kTssAlphaOp] = surface ? 4 : 2;
                stage[kTssAlphaArg1] = surface ? 2 : 0;
                stage[kTssAlphaArg2] = 0;
                stage[kTssAddressU] = 1; // D3DTADDRESS_WRAP
                stage[kTssAddressV] = 1;
                stage[kTssMagFilter] = device->textureQuality ? 2 : 1; // linear, else point
                stage[kTssMinFilter] = device->textureQuality ? 2 : 1;
                d.stageStates[1][kTssColorOp] = 1; // D3DTOP_DISABLE
                d.stageStates[1][kTssAlphaOp] = 1;

                vertices.resize(g.vertices.size() * 36);
                for (size_t i = 0; i < g.vertices.size(); ++i) {
                    const Mesh::Vertex &mv = g.vertices[i];
                    uint8_t *o = vertices.data() + i * 36;
                    std::memcpy(o, mv.position.data(), 12);
                    std::memcpy(o + 12, mv.normal.data(), 12);
                    std::memcpy(o + 24, &argb, 4);
                    std::memcpy(o + 28, &mv.u, 4);
                    std::memcpy(o + 32, &mv.v, 4);
                }
                indices.clear();
                for (const auto &face : g.faces) // each face a fan
                    for (size_t i = 1; i + 1 < face.size(); ++i)
                        if (face[0] < g.vertices.size() && face[i] < g.vertices.size() &&
                            face[i + 1] < g.vertices.size())
                            indices.insert(indices.end(), {face[0], face[i], face[i + 1]});
                if (!indices.empty())
                    d.drawVertices(4, kRmVertexFvf, vertices.data(), 36, uint32_t(g.vertices.size()), &indices);
            }
        }

    d.renderStates = savedStates;
    d.stageStates = savedStages;
    d.textures[0] = savedTexture;
    d.transforms = savedTransforms;
    d.lights = savedLights;
    d.material = savedMaterial;
    d.viewport = savedViewport;
}

// Each face of the meshes in the camera's scene under the point, on the
// screen as render draws it: the nearest face of each visual, nearest first.
std::vector<PickHit> Viewport::pick(int px, int py) {
    std::vector<PickHit> hits;
    if (!camera || width <= 0 || height <= 0 || px < x || py < y || px >= x + width || py >= y + height)
        return hits;
    const Matrix viewProjection = multiply(inverse(camera->world()), projectionMatrix());
    // The point at a pixel's centre, as the rasterizer fills it.
    const float fx = float(px) + 0.5f;
    const float fy = float(py) + 0.5f;
    for (const Placed &p : place(*camera->scene()))
        for (const auto &v : p.frame->visuals) {
            const auto *mesh = dynamic_cast<Mesh *>(v.get());
            if (!mesh)
                continue;
            const Matrix all = multiply(p.world, viewProjection);
            PickHit best;
            bool found = false;
            for (size_t gi = 0; gi < mesh->groups.size(); ++gi) {
                const Mesh::Group &g = mesh->groups[gi];
                // Each vertex on the screen, or none if it's behind the front plane.
                std::vector<std::optional<std::array<float, 3>>> screen(g.vertices.size());
                for (size_t i = 0; i < g.vertices.size(); ++i) {
                    const Vector &q = g.vertices[i].position;
                    const float cx = q[0] * all[0] + q[1] * all[4] + q[2] * all[8] + all[12];
                    const float cy = q[0] * all[1] + q[1] * all[5] + q[2] * all[9] + all[13];
                    const float cz = q[0] * all[2] + q[1] * all[6] + q[2] * all[10] + all[14];
                    const float cw = q[0] * all[3] + q[1] * all[7] + q[2] * all[11] + all[15];
                    if (cw <= 0 || cz < 0)
                        continue;
                    screen[i] = std::array<float, 3>{float(x) + (cx / cw + 1) * float(width) / 2,
                                                     float(y) + (1 - cy / cw) * float(height) / 2, cz / cw};
                }
                for (size_t fi = 0; fi < g.faces.size(); ++fi) {
                    const auto &face = g.faces[fi];
                    for (size_t k = 1; k + 1 < face.size(); ++k) {
                        const uint32_t ids[3] = {face[0], face[k], face[k + 1]};
                        if (ids[0] >= screen.size() || ids[1] >= screen.size() || ids[2] >= screen.size() ||
                            !screen[ids[0]] || !screen[ids[1]] || !screen[ids[2]])
                            continue;
                        const auto &a = *screen[ids[0]];
                        const auto &b = *screen[ids[1]];
                        const auto &cc = *screen[ids[2]];
                        const float area = (b[0] - a[0]) * (cc[1] - a[1]) - (b[1] - a[1]) * (cc[0] - a[0]);
                        if (area == 0)
                            continue;
                        // Barycentric weights; either winding is picked.
                        const float wa = ((b[0] - fx) * (cc[1] - fy) - (b[1] - fy) * (cc[0] - fx)) / area;
                        const float wb = ((cc[0] - fx) * (a[1] - fy) - (cc[1] - fy) * (a[0] - fx)) / area;
                        const float wc = 1 - wa - wb;
                        if (wa < 0 || wb < 0 || wc < 0)
                            continue;
                        const float z = wa * a[2] + wb * b[2] + wc * cc[2];
                        if (z > 1 || (found && z >= best.position[2]))
                            continue;
                        found = true;
                        best.face = uint32_t(fi);
                        best.group = int32_t(gi);
                        best.position[0] = fx;
                        best.position[1] = fy;
                        best.position[2] = z;
                    }
                }
            }
            if (!found)
                continue;
            best.visual = v;
            for (Frame *f : p.path)
                best.path.push_back(std::static_pointer_cast<Frame>(f->shared_from_this()));
            hits.push_back(std::move(best));
        }
    std::stable_sort(hits.begin(), hits.end(),
                     [](const PickHit &a, const PickHit &b) { return a.position[2] < b.position[2]; });
    return hits;
}

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
