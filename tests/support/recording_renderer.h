// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "graphics/gfx3d.h"

#include "tests/support/record.h"

#include <cmath>
#include <string>
#include <vector>

namespace reluva::test {

/**
 * A 3D renderer that draws nothing but keeps checksums of what it's given:
 * the textures, the meshes, and each frame's draw calls, for engine tests.
 * Positions and matrices go in rounded to a thousandth, so the last bit's
 * differences between compilers' maths don't change a record.
 */
class RecordingRenderer : public gfx::Renderer3D {
public:
    Hash textures;
    Hash meshes;
    Hash draws;
    int textureCount = 0;
    int meshCount = 0;
    int frames = 0;
    int drawCount = 0;

    Error createTexture(int width, int height, const uint8_t *rgba, const gfx::TextureParams &params,
                        gfx::TextureId &out) override {
        textures.i32(width).i32(height).bytes(rgba, size_t(width) * size_t(height) * 4).i32(params.clamp);
        ++textureCount;
        out = gfx::TextureId(++_next);
        return Error::success();
    }
    void destroyTexture(gfx::TextureId) override {}
    Error createMesh(const std::vector<gfx::Vertex> &vertices, const std::vector<uint32_t> &indices,
                     gfx::MeshId &out) override {
        mesh(vertices);
        meshes.bytes(indices.data(), indices.size() * 4);
        ++meshCount;
        out = gfx::MeshId(++_next);
        return Error::success();
    }
    Error createIndexedMesh(gfx::MeshId, const std::vector<uint32_t> &indices, gfx::MeshId &out) override {
        meshes.bytes(indices.data(), indices.size() * 4);
        out = gfx::MeshId(++_next);
        return Error::success();
    }
    Error setMeshIndices(gfx::MeshId, const std::vector<uint32_t> &indices) override {
        meshes.bytes(indices.data(), indices.size() * 4);
        return Error::success();
    }
    Error setMeshVertices(gfx::MeshId, const std::vector<gfx::Vertex> &vertices) override {
        mesh(vertices);
        return Error::success();
    }
    void destroyMesh(gfx::MeshId) override {}
    int width() const override { return 640; }
    int height() const override { return 480; }
    std::string deviceName() const override { return "recording"; }
    void beginFrame(const gfx::Mat4 &viewProj, const gfx::Vec3 &, Color clear) override {
        matrix(viewProj);
        draws.i32(clear.r).i32(clear.g).i32(clear.b);
        ++frames;
    }
    void draw(const gfx::DrawCall &call) override {
        draws.i32(int32_t(call.mesh)).i32(int32_t(call.firstIndex)).i32(int32_t(call.indexCount));
        draws.i32(call.vertexOffset).i32(int32_t(call.texture));
        matrix(call.model);
        ++drawCount;
    }
    void setView(const gfx::Mat4 &viewProj, const gfx::Vec3 &) override { matrix(viewProj); }
    void setDynamicLights(const std::vector<gfx::DynamicLight> &lights) override { draws.i32(int32_t(lights.size())); }
    void setViewport(int, int, int, int) override {}
    void setDepthRange(float) override {}
    void clearDepth() override {}
    bool hasStencil() const override { return false; }
    void setStencilMode(StencilMode) override {}
    void setMirrored(bool) override {}
    void setGamma(float) override {}
    void endFrame(const Surface *overlay) override {
        if (overlay)
            draws.bytes(overlay->pixels);
    }
    void captureNextFrame() override {}
    bool takeCapture(std::vector<uint8_t> &) override { return false; }

private:
    uint32_t _next = 0;
    static int32_t rounded(float v) { return int32_t(std::lround(double(v) * 1000)); }
    void matrix(const gfx::Mat4 &m) {
        for (float v : m)
            draws.i32(rounded(v));
    }
    void mesh(const std::vector<gfx::Vertex> &vertices) {
        for (const gfx::Vertex &v : vertices) {
            for (float f : v.pos)
                meshes.i32(rounded(f));
            for (float f : v.uv)
                meshes.i32(rounded(f));
            meshes.bytes(v.color, 4);
        }
    }
};

} // namespace reluva::test
