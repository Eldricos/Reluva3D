// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Sith's unit tests: the data viewer's 3D drawing as the game draws, and
// the game's own Direct3D scenes as drawn on the GPU.
//
//   reluva-tests sith.unit.render

#include "engines/sith/render/draw3d.h"
#include "engines/sith/render/scene_gpu.h"

#include "tests/support/test.h"

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

using namespace reluva;
using namespace reluva::sith;

namespace {

constexpr int kSize = 64;

// A colormap whose every light level is the palette: colour 1 red, 2 blue.
Colormap plainColormap() {
    Colormap cmp;
    cmp.palette[1] = {255, 0, 0};
    cmp.palette[2] = {0, 0, 255};
    cmp.light.resize(size_t(Colormap::kLightLevels) * 256);
    for (size_t i = 0; i < cmp.light.size(); ++i)
        cmp.light[i] = uint8_t(i % 256);
    return cmp;
}

// A square across the middle of the view, `ahead` units in front of the
// camera at the origin looking along +y.
std::vector<Corner> square(float ahead, float half = 1) {
    std::vector<Corner> c(4);
    c[0].at = {-half, ahead, half};
    c[1].at = {half, ahead, half};
    c[2].at = {half, ahead, -half};
    c[3].at = {-half, ahead, -half};
    return c;
}

soft3d::OrbitCamera camera() {
    // From 1 away on the -y side of (0, 1, 0): at the origin, looking along +y.
    return soft3d::OrbitCamera({0, 1, 0}, 0, 0, 1, kSize, kSize);
}

const uint8_t *middle(const Surface &s) {
    return s.row(kSize / 2) + (kSize / 2) * 4;
}

// A see-through face covers 90/255 of what's behind it, and doesn't hide
// what's drawn after it.
void testTranslucent() {
    const Colormap cmp = plainColormap();
    const soft3d::OrbitCamera cam = camera();
    Surface screen;
    screen.create(kSize, kSize, PixelFormat::kRGBA32);
    Draw3D draw(screen, cam, cmp);
    Paint red;
    red.color = 1;
    draw.polygon(square(4), red);
    Paint blue;
    blue.color = 2;
    blue.alpha = kTranslucentAlpha;
    draw.polygon(square(2), blue);
    const uint8_t *p = middle(screen);
    CHECK(p[0] == 255 * (255 - kTranslucentAlpha) / 255 && p[2] == 255 * kTranslucentAlpha / 255);
    // Something nearer than the red, behind the blue, still shows.
    Paint again;
    again.color = 1;
    draw.polygon(square(3), again);
    CHECK(middle(screen)[0] == 255 && middle(screen)[2] == 0);
}

// A polygon lit by colour: each channel of its colour times the corners',
// made a byte; the light alone where the corners have none.
void testColouredLight() {
    Colormap cmp = plainColormap();
    cmp.palette[3] = {200, 100, 50};
    const soft3d::OrbitCamera cam = camera();
    Surface screen;
    screen.create(kSize, kSize, PixelFormat::kRGBA32);
    Draw3D draw(screen, cam, cmp);
    Paint paint;
    paint.color = 3;
    std::vector<Corner> c = square(2);
    for (Corner &corner : c)
        corner.colour = Vec3{1.0f, 0.5f, 0.0f};
    draw.polygon(c, paint);
    const uint8_t *p = middle(screen);
    CHECK(p[0] == 200 && p[1] == 100 * 127 / 255 && p[2] == 0);
    // Over 1, a channel is held at 1.
    for (Corner &corner : c)
        corner.colour = Vec3{2.0f, 1.0f, 0.2f};
    Draw3D again(screen, cam, cmp);
    again.polygon(c, paint);
    CHECK(p[0] == 200 && p[1] == 100 && p[2] == 50 * 51 / 255);
}

// A model's face turned from the camera isn't drawn, unless it's double-sided.
void testCulling() {
    const Colormap cmp = plainColormap();
    const soft3d::OrbitCamera cam = camera();
    GameFiles files;
    MaterialCache materials(files);
    Model model;
    model.nodes.resize(1);
    model.nodes[0].mesh = 0;
    model.geosets.resize(1);
    model.geosets[0].resize(1);
    Model::Mesh &mesh = model.geosets[0][0];
    mesh.vertices = {{-1, 0, 1}, {1, 0, 1}, {1, 0, -1}, {-1, 0, -1}};
    Model::Face face;
    face.geometryMode = 3;
    face.vertices = {0, 1, 2, 3};
    face.texVertices = {-1, -1, -1, -1};
    face.normal = {0, 1, 0}; // away from the camera at the origin
    mesh.faces.push_back(face);
    const Transform where = Transform::place({0, 3, 0}, {0, 0, 0});
    for (const bool doubleSided : {false, true}) {
        mesh.faces[0].type = doubleSided ? kFaceDoubleSided : 0;
        Surface screen;
        screen.create(kSize, kSize, PixelFormat::kRGBA32);
        Draw3D draw(screen, cam, cmp);
        drawModel(draw, materials, model, poseNodes(model), where, 0, &cmp);
        CHECK((draw.drawn() == 1) == doubleSided);
    }
}

// A horizon sky's texels follow the screen and the camera's turn, not the
// surface's texture vertices.
void testHorizonSky() {
    const Colormap cmp = plainColormap();
    const soft3d::OrbitCamera cam = camera();
    // A texture of two columns, red then blue.
    Material mat;
    mat.type = 2;
    Material::Cel cel;
    cel.texture = 0;
    mat.cels.push_back(cel);
    Material::Texture tex;
    tex.w = 2;
    tex.h = 1;
    tex.mips.push_back({1, 2});
    mat.textures.push_back(tex);
    Sky sky;
    sky.horizonDistance = cam.focal; // a texel a pixel
    sky.pixelsPerRev = 360;          // a texel a degree
    auto drawn = [&](float yaw, float uOffset) {
        sky.yaw = yaw;
        Surface screen;
        screen.create(kSize, kSize, PixelFormat::kRGBA32);
        Draw3D draw(screen, cam, cmp);
        Paint paint;
        paint.material = &mat;
        paint.sky = &sky;
        std::vector<Corner> c = square(2);
        for (Corner &k : c)
            k.u = uOffset;
        draw.polygon(c, paint);
        return std::vector<uint8_t>(middle(screen), middle(screen) + 8);
    };
    const std::vector<uint8_t> a = drawn(0, 0);
    CHECK(a == drawn(0, 1)); // the texture vertices don't matter
    CHECK(a[0] != a[4]);     // neighbouring pixels, neighbouring texels
    CHECK(a != drawn(1, 0)); // a degree's turn moves it a texel
    CHECK(a == drawn(2, 0)); // two, the width of the texture
}

// A renderer that keeps what it's given: the vertices, viewports, draws
// and the overlay shown.
class Recorder : public gfx::Renderer3D {
public:
    Recorder(int w, int h) : _w(w), _h(h) {}
    std::vector<gfx::Vertex> vertices;
    std::vector<std::array<int, 4>> viewports;
    std::vector<gfx::DrawCall> draws;
    Surface shown;
    float gamma = 0;
    Error createTexture(int, int, const uint8_t *, const gfx::TextureParams &, gfx::TextureId &out) override {
        out = gfx::TextureId(++_next);
        return Error::success();
    }
    void destroyTexture(gfx::TextureId) override {}
    Error createMesh(const std::vector<gfx::Vertex> &v, const std::vector<uint32_t> &, gfx::MeshId &out) override {
        vertices = v;
        out = gfx::MeshId(++_next);
        return Error::success();
    }
    Error createIndexedMesh(gfx::MeshId, const std::vector<uint32_t> &, gfx::MeshId &out) override {
        out = gfx::MeshId(++_next);
        return Error::success();
    }
    Error setMeshIndices(gfx::MeshId, const std::vector<uint32_t> &) override { return Error::success(); }
    Error setMeshVertices(gfx::MeshId, const std::vector<gfx::Vertex> &v) override {
        vertices = v;
        return Error::success();
    }
    void destroyMesh(gfx::MeshId) override {}
    int width() const override { return _w; }
    int height() const override { return _h; }
    std::string deviceName() const override { return "recorder"; }
    void beginFrame(const gfx::Mat4 &m, const gfx::Vec3 &, Color) override { _view = m; }
    void draw(const gfx::DrawCall &call) override { draws.push_back(call); }
    void setView(const gfx::Mat4 &m, const gfx::Vec3 &) override { _view = m; }
    void setDynamicLights(const std::vector<gfx::DynamicLight> &) override {}
    void setViewport(int x, int y, int w, int h) override {
        viewports.push_back({x, y, w, h});
        _port = {x, y, w, h};
    }
    void setDepthRange(float) override {}
    void clearDepth() override {}
    bool hasStencil() const override { return false; }
    void setStencilMode(StencilMode) override {}
    void setMirrored(bool) override {}
    void setGamma(float g) override { gamma = g; }
    void endFrame(const Surface *overlay) override {
        if (overlay)
            shown = *overlay;
    }
    void captureNextFrame() override {}
    bool takeCapture(std::vector<uint8_t> &) override { return false; }
    // Where a vertex lands in the frame, in pixels, through the view and
    // viewport set last.
    std::array<float, 2> place(const gfx::Vertex &v) const {
        const gfx::Mat4 &m = _view;
        const float x = m[0] * v.pos[0] + m[4] * v.pos[1] + m[8] * v.pos[2] + m[12];
        const float y = m[1] * v.pos[0] + m[5] * v.pos[1] + m[9] * v.pos[2] + m[13];
        const float w = m[3] * v.pos[0] + m[7] * v.pos[1] + m[11] * v.pos[2] + m[15];
        return {float(_port[0]) + (x / w + 1) / 2 * float(_port[2]),
                float(_port[1]) + (1 - y / w) / 2 * float(_port[3])};
    }

private:
    int _w;
    int _h;
    uint32_t _next = 0;
    gfx::Mat4 _view{};
    std::array<int, 4> _port{};
};

// A scene of the game's 640x480, drawn on a renderer twice the size: its
// viewport and pixels where they'd be at the larger size (Direct3D's pixel
// centres, on whole numbers, at the middle of the GPU's 2x2), depths
// turned round for its greater test, and the 2D enlarged.
void testSceneScaled() {
    using Scene = x86::DirectX::Scene;
    Scene scene;
    scene.width = 640;
    scene.height = 480;
    // x, y, z, 1/w, diffuse, specular, u, v
    // with a specular colour, its alpha the fog's factor
    scene.vertices = {{10, 20, 0.25f, 0.5f, 0xffffffffu, 0x40800000u, 0, 0},
                      {100, 20, 0.25f, 0.5f, 0xffffffffu, 0x40800000u, 1, 0},
                      {10, 60, 0.75f, 0.5f, 0xffffffffu, 0x40800000u, 0, 1}};
    // The display's gamma ramp: each colour's square root (a gamma of 2).
    for (int c = 0; c < 3; ++c)
        for (int i = 0; i < 256; ++i)
            scene.gamma.push_back(uint16_t(std::lround(std::sqrt(i / 255.0) * 65535)));
    Scene::Step depth;
    depth.clearDepth = true;
    depth.viewport[2] = 640, depth.viewport[3] = 480;
    Scene::Step step;
    step.count = 3;
    step.viewport[0] = 8, step.viewport[1] = 4, step.viewport[2] = 600, step.viewport[3] = 400;
    step.zTest = true, step.zWrite = true, step.zFunc = 5;           // greater
    step.alphaTest = true, step.alphaFunc = 7, step.alphaRef = 0.5f; // greater or equal
    step.specular = true;
    step.fog = true, step.fogColor = 0xff0000ffu; // blue, vertex fog
    scene.steps = {depth, step};
    Surface overlay;
    overlay.create(640, 480, PixelFormat::kRGBA32);
    std::memset(overlay.row(0), 0, size_t(overlay.pitch) * 480);
    overlay.row(1)[4 * 1 + 0] = 200; // pixel (1, 1): red, opaque
    overlay.row(1)[4 * 1 + 3] = 255;

    Recorder r(1280, 960);
    SceneGpu gpu;
    gpu.setRenderer(&r);
    gpu.draw(&scene, overlay);
    CHECK(r.viewports.size() == 2);
    if (r.viewports.size() == 2)
        CHECK((r.viewports[1] == std::array<int, 4>{16, 8, 1200, 800}));
    CHECK(r.draws.size() == 1 && r.vertices.size() >= 3);
    if (r.draws.size() == 1 && r.vertices.size() >= 3) {
        const gfx::DrawCall &d = r.draws[0];
        CHECK(d.screenDepth && d.state.depthTest && d.state.depthLess && !d.state.depthEqual);
        const auto a = r.place(r.vertices[0]);
        const auto b = r.place(r.vertices[1]);
        CHECK(std::abs(a[0] - 21) < 1e-3f && std::abs(a[1] - 41) < 1e-3f);
        CHECK(std::abs(b[0] - 201) < 1e-3f && std::abs(b[1] - 41) < 1e-3f);
        CHECK(r.vertices[0].uv2[1] == 0.75f && r.vertices[2].uv2[1] == 0.25f); // nearer is less
        // Direct3D's alpha test, specular colour and vertex fog as given.
        CHECK(d.state.alphaTest == gfx::AlphaTest::kCompare && d.alphaFunc == gfx::Compare::kGreaterEqual &&
              d.alphaRef == 0.5f);
        CHECK(d.specularFromNormal && std::abs(r.vertices[0].normal[0] - 128 / 255.0f) < 1e-6f &&
              r.vertices[0].normal[1] == 0 && r.vertices[0].normal[2] == 0);
        CHECK(d.vertexFog && d.tableFogColor[2] == 1 && d.tableFogColor[0] == 0 &&
              std::abs(r.vertices[1].uv2[0] - 64 / 255.0f) < 1e-6f);
    }
    CHECK(std::abs(r.gamma - 2) < 0.01f); // the ramp as the nearest power
    CHECK(r.shown.w == 1280 && r.shown.h == 960);
    if (r.shown.w == 1280) {
        CHECK(r.shown.row(2)[4 * 2] == 200 && r.shown.row(3)[4 * 3 + 3] == 255); // (1, 1) is (2, 2) to (3, 3)
        CHECK(r.shown.row(4)[4 * 4 + 3] == 0 && r.shown.row(1)[4 * 1 + 3] == 0);
    }
    gpu.setRenderer(nullptr);
}

} // namespace

static const reluva::test::Suite
    kSuite("sith.unit.render", {
                                   {"translucent", [](const std::filesystem::path &) { testTranslucent(); }},
                                   {"culling", [](const std::filesystem::path &) { testCulling(); }},
                                   {"coloured light", [](const std::filesystem::path &) { testColouredLight(); }},
                                   {"horizon sky", [](const std::filesystem::path &) { testHorizonSky(); }},
                                   {"scene scaled", [](const std::filesystem::path &) { testSceneScaled(); }},
                               });
