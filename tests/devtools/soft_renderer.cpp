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

} // namespace

static const reluva::test::Suite kSuite("devtools.unit.soft_renderer",
                                        {
                                            {"shared edge", [](const std::filesystem::path &) { testSharedEdge(); }},
                                        });
