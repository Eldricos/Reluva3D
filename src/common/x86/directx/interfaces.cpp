// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The DirectX 7 interfaces' methods in vtable order and their argument
// counts, and their IIDs, as the SDK's ddraw.h and d3d.h declare them.

#include "common/x86/directx/internal.h"

namespace reluva::x86::dx {

const char* const kIidDirectDraw = "{6c14db80-a733-11ce-a521-0020af0be560}";
const char* const kIidDirectDraw2 = "{b3a6f3e0-2b43-11cf-a2de-00aa00b93356}";
const char* const kIidDirectDraw4 = "{9c59509a-39bd-11d1-8c4a-00c04fd930c5}";
const char* const kIidDirectDraw7 = "{15e65ec0-3b9c-11d2-b92f-00609797ea5b}";
const char* const kIidSurface = "{6c14db81-a733-11ce-a521-0020af0be560}";
const char* const kIidSurface2 = "{57805885-6eec-11cf-9441-a82303c10e27}";
const char* const kIidSurface3 = "{da044e00-69b2-11d0-a1d5-00aa00b8dfbb}";
const char* const kIidSurface4 = "{0b2b8630-ad35-11d0-8ea6-00609797ea5b}";
const char* const kIidSurface7 = "{06675a80-3b9b-11d2-b92f-00609797ea5b}";
const char* const kIidPalette = "{6c14db84-a733-11ce-a521-0020af0be560}";
const char* const kIidClipper = "{6c14db85-a733-11ce-a521-0020af0be560}";
const char* const kIidDirect3D7 = "{f5049e77-4861-11d2-a407-00a0c90629a8}";
const char* const kIidDevice7 = "{f5049e79-4861-11d2-a407-00a0c90629a8}";
const char* const kIidVertexBuffer7 = "{f5049e7d-4861-11d2-a407-00a0c90629a8}";
const char* const kIidHalDevice = "{84e63de0-46aa-11cf-816f-0000c020156e}";
const char* const kIidTnLHalDevice = "{f5049e78-4861-11d2-a407-00a0c90629a8}";
const char* const kIidRgbDevice = "{a4665c60-2673-11cf-a31a-00aa00b93356}";
const char* const kIidMmxDevice = "{881949a1-d6f3-11d0-89ab-00a0c9054129}";
const char* const kIidRefDevice = "{50936643-13e9-11d1-89aa-00a0c9054129}";
const char* const kIidTexture2 = "{93281502-8cf8-11d0-89ab-00a0c9054129}";

namespace {

const std::vector<ComInterface::Method> kUnknown = {{"QueryInterface", 2}, {"AddRef", 0}, {"Release", 0}};

std::vector<ComInterface::Method> with(std::vector<ComInterface::Method> m) {
    m.insert(m.begin(), kUnknown.begin(), kUnknown.end());
    return m;
}

// IDirectDraw7's, after IUnknown's.
const std::vector<ComInterface::Method> kDirectDraw7 = with({
    {"Compact", 0}, {"CreateClipper", 3}, {"CreatePalette", 4}, {"CreateSurface", 3}, {"DuplicateSurface", 2},
    {"EnumDisplayModes", 4}, {"EnumSurfaces", 4}, {"FlipToGDISurface", 0}, {"GetCaps", 2}, {"GetDisplayMode", 1},
    {"GetFourCCCodes", 2}, {"GetGDISurface", 1}, {"GetMonitorFrequency", 1}, {"GetScanLine", 1},
    {"GetVerticalBlankStatus", 1}, {"Initialize", 1}, {"RestoreDisplayMode", 0}, {"SetCooperativeLevel", 2},
    {"SetDisplayMode", 5}, {"WaitForVerticalBlank", 2},
    {"GetAvailableVidMem", 3},                                                                 // IDirectDraw2
    {"GetSurfaceFromDC", 2}, {"RestoreAllSurfaces", 0}, {"TestCooperativeLevel", 0}, {"GetDeviceIdentifier", 2}, // 4
    {"StartModeTest", 3}, {"EvaluateMode", 2},                                                 // 7
});

const std::vector<ComInterface::Method> kSurface7 = with({
    {"AddAttachedSurface", 1}, {"AddOverlayDirtyRect", 1}, {"Blt", 5}, {"BltBatch", 3}, {"BltFast", 5},
    {"DeleteAttachedSurface", 2}, {"EnumAttachedSurfaces", 2}, {"EnumOverlayZOrders", 3}, {"Flip", 2},
    {"GetAttachedSurface", 2}, {"GetBltStatus", 1}, {"GetCaps", 1}, {"GetClipper", 1}, {"GetColorKey", 2},
    {"GetDC", 1}, {"GetFlipStatus", 1}, {"GetOverlayPosition", 2}, {"GetPalette", 1}, {"GetPixelFormat", 1},
    {"GetSurfaceDesc", 1}, {"Initialize", 2}, {"IsLost", 0}, {"Lock", 4}, {"ReleaseDC", 1}, {"Restore", 0},
    {"SetClipper", 1}, {"SetColorKey", 2}, {"SetOverlayPosition", 2}, {"SetPalette", 1}, {"Unlock", 1},
    {"UpdateOverlay", 5}, {"UpdateOverlayDisplay", 1}, {"UpdateOverlayZOrder", 2},
    {"GetDDInterface", 1}, {"PageLock", 1}, {"PageUnlock", 1},                                  // 2
    {"SetSurfaceDesc", 2},                                                                      // 3
    {"SetPrivateData", 4}, {"GetPrivateData", 3}, {"FreePrivateData", 1}, {"GetUniquenessValue", 1},
    {"ChangeUniquenessValue", 0},                                                               // 4
    {"SetPriority", 1}, {"GetPriority", 1}, {"SetLOD", 1}, {"GetLOD", 1},                       // 7
});

std::vector<ComInterface::Method> first(const std::vector<ComInterface::Method>& m, size_t n) {
    return {m.begin(), m.begin() + long(n)};
}

} // namespace

Interfaces::Interfaces() {
    directDraw7 = {"IDirectDraw7", kIidDirectDraw7, kDirectDraw7};
    directDraw4 = {"IDirectDraw4", kIidDirectDraw4, first(kDirectDraw7, 28)};
    directDraw2 = {"IDirectDraw2", kIidDirectDraw2, first(kDirectDraw7, 24)};
    directDraw = {"IDirectDraw", kIidDirectDraw, first(kDirectDraw7, 23)};
    directDraw.methods[21].args = 3; // the first SetDisplayMode has no refresh rate or flags
    surface7 = {"IDirectDrawSurface7", kIidSurface7, kSurface7};
    surface4 = {"IDirectDrawSurface4", kIidSurface4, first(kSurface7, 45)};
    surface3 = {"IDirectDrawSurface3", kIidSurface3, first(kSurface7, 40)};
    surface2 = {"IDirectDrawSurface2", kIidSurface2, first(kSurface7, 39)};
    surface = {"IDirectDrawSurface", kIidSurface, first(kSurface7, 36)};
    palette = {"IDirectDrawPalette", kIidPalette,
               with({{"GetCaps", 1}, {"GetEntries", 4}, {"Initialize", 3}, {"SetEntries", 4}})};
    clipper = {"IDirectDrawClipper", kIidClipper,
               with({{"GetClipList", 3}, {"GetHWnd", 1}, {"Initialize", 2}, {"IsClipListChanged", 1},
                     {"SetClipList", 2}, {"SetHWnd", 2}})};
    direct3d7 = {"IDirect3D7", kIidDirect3D7,
                 with({{"EnumDevices", 2}, {"CreateDevice", 3}, {"CreateVertexBuffer", 3},
                       {"EnumZBufferFormats", 3}, {"EvictManagedTextures", 0}})};
    device7 = {"IDirect3DDevice7", kIidDevice7,
               with({{"GetCaps", 1},
                     {"EnumTextureFormats", 2},
                     {"BeginScene", 0},
                     {"EndScene", 0},
                     {"GetDirect3D", 1},
                     {"SetRenderTarget", 2},
                     {"GetRenderTarget", 1},
                     {"Clear", 6},
                     {"SetTransform", 2},
                     {"GetTransform", 2},
                     {"SetViewport", 1},
                     {"MultiplyTransform", 2},
                     {"GetViewport", 1},
                     {"SetMaterial", 1},
                     {"GetMaterial", 1},
                     {"SetLight", 2},
                     {"GetLight", 2},
                     {"SetRenderState", 2},
                     {"GetRenderState", 2},
                     {"BeginStateBlock", 0},
                     {"EndStateBlock", 1},
                     {"PreLoad", 1},
                     {"DrawPrimitive", 5},
                     {"DrawIndexedPrimitive", 7},
                     {"SetClipStatus", 1},
                     {"GetClipStatus", 1},
                     {"DrawPrimitiveStrided", 5},
                     {"DrawIndexedPrimitiveStrided", 7},
                     {"DrawPrimitiveVB", 5},
                     {"DrawIndexedPrimitiveVB", 7},
                     {"ComputeSphereVisibility", 5},
                     {"GetTexture", 2},
                     {"SetTexture", 2},
                     {"GetTextureStageState", 3},
                     {"SetTextureStageState", 3},
                     {"ValidateDevice", 1},
                     {"ApplyStateBlock", 1},
                     {"CaptureStateBlock", 1},
                     {"DeleteStateBlock", 1},
                     {"CreateStateBlock", 2},
                     {"Load", 5},
                     {"LightEnable", 2},
                     {"GetLightEnable", 2},
                     {"SetClipPlane", 2},
                     {"GetClipPlane", 2},
                     {"GetInfo", 3}})};
    vertexBuffer7 = {"IDirect3DVertexBuffer7", kIidVertexBuffer7,
                     with({{"Lock", 3},
                           {"Unlock", 0},
                           {"ProcessVertices", 7},
                           {"GetVertexBufferDesc", 1},
                           {"Optimize", 2},
                           {"ProcessVerticesStrided", 7}})};
}

} // namespace reluva::x86::dx
