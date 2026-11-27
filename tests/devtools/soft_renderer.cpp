// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The software renderer reluva-tool's screenshots are drawn with
// (src/devtools/soft_renderer.h).

#include "devtools/soft_renderer.h"

#include "tests/support/test.h"

#include <vector>

using reluva::tools::SoftRenderer3D;
namespace gfx = reluva::gfx;

namespace {

// A square over the whole frame, its diagonal through the pixels' centres,
// added on twice: each pixel is drawn once a time, never by both of the
// square's triangles, so the diagonal doesn't show.
void testSharedEdge() {
    constexpr int kSize = 16;
    SoftRenderer3D renderer(kSize, kSize);
    std::vector<gfx::Vertex> corners(4);
    const float xy[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    for (size_t i = 0; i < 4; ++i) {
        corners[i] = gfx::Vertex{};
        corners[i].pos[0] = xy[i][0];
        corners[i].pos[1] = xy[i][1];
        corners[i].pos[2] = 0.5f;
        for (uint8_t &c : corners[i].color)
            c = 255;
    }
    gfx::MeshId mesh = 0;
    CHECK(!renderer.createMesh(corners, {0, 1, 2, 0, 2, 3}, mesh));
    renderer.beginFrame(gfx::identity(), {0, 0, 0}, reluva::Color{0, 0, 0});
    gfx::DrawCall call;
    call.mesh = mesh;
    call.indexCount = 6;
    call.color = {0.25f, 0.25f, 0.25f, 1};
    call.state.cull = gfx::Cull::kNone;
    call.state.depthTest = false;
    call.state.depthWrite = false;
    call.state.src = gfx::Blend::kOne;
    call.state.dst = gfx::Blend::kOne;
    renderer.draw(call);
    renderer.draw(call);
    renderer.endFrame(nullptr);
    const reluva::Surface &frame = renderer.frame();
    int odd = 0;
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x) {
            const uint8_t red = frame.row(y)[size_t(x) * 4];
            odd += red < 126 || red > 129;
        }
    CHECK(odd == 0);
}

// A square facing the camera, black but for its highlight: the light
// behind the camera gives the whole of it (s = 1), one from the side little
// (s = cos 45 degrees, at a power of 50: 0.046).
void testHighlight() {
    constexpr int kSize = 8;
    SoftRenderer3D renderer(kSize, kSize);
    std::vector<gfx::Vertex> corners(4);
    const float xy[4][2] = {{-0.4f, -0.4f}, {0.4f, -0.4f}, {0.4f, 0.4f}, {-0.4f, 0.4f}};
    for (size_t i = 0; i < 4; ++i) {
        corners[i] = gfx::Vertex{};
        corners[i].pos[0] = xy[i][0];
        corners[i].pos[1] = xy[i][1];
        corners[i].pos[2] = 0.5f;
        corners[i].normal[2] = -1;
        for (uint8_t &c : corners[i].color)
            c = 255;
    }
    gfx::MeshId mesh = 0;
    CHECK(!renderer.createMesh(corners, {0, 1, 2, 0, 2, 3}, mesh));
    // Looking along +z: w the depth.
    gfx::Mat4 viewProj = gfx::identity();
    viewProj[10] = 0.5f;
    viewProj[11] = 1;
    viewProj[15] = 0;
    auto middle = [&](gfx::Vec3 toLight) {
        renderer.beginFrame(viewProj, {0, 0, -1}, reluva::Color{0, 0, 0});
        gfx::DrawCall call;
        call.mesh = mesh;
        call.indexCount = 6;
        call.color = {0, 0, 0, 1};
        call.state.cull = gfx::Cull::kNone;
        call.state.depthTest = false;
        call.rgbSource = gfx::ColorSource::kLighting;
        call.materialLit = true;
        call.directedLight = {0.5f, 0.5f, 0.5f};
        call.lightDirection = toLight;
        call.materialSpecular = {1, 0.5f, 0};
        call.materialPower = 50;
        renderer.draw(call);
        renderer.endFrame(nullptr);
        const uint8_t *p = renderer.frame().row(kSize / 2) + size_t(kSize / 2) * 4;
        return std::vector<int>{p[0], p[1], p[2]};
    };
    CHECK(middle({0, 0, -1}) == std::vector<int>({128, 64, 0}));
    const std::vector<int> side = middle({1, 0, 0});
    CHECK(side[0] == 6 && side[1] == 3 && side[2] == 0);
}

} // namespace

static const reluva::test::Suite kSuite("devtools.unit.soft_renderer",
                                        {
                                            {"shared edge", [](const std::filesystem::path &) { testSharedEdge(); }},
                                            {"highlight", [](const std::filesystem::path &) { testHighlight(); }},
                                        });
