// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Sith's unit tests: the data viewer's 3D drawing as the game draws.
//
//   reluva-tests sith.unit.render

#include "engines/sith/render/draw3d.h"

#include "tests/support/test.h"

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

} // namespace

static const reluva::test::Suite kSuite("sith.unit.render",
                                        {
                                            {"translucent", [](const std::filesystem::path &) { testTranslucent(); }},
                                            {"culling", [](const std::filesystem::path &) { testCulling(); }},
                                            {"horizon sky", [](const std::filesystem::path &) { testHorizonSky(); }},
                                        });
