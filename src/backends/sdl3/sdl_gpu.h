// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/gfx3d.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <unordered_map>

namespace reluva {

// gfx::Renderer3D on SDL's GPU API (Vulkan, Metal). Frames are drawn into a
// texture of the requested size which SDL's renderer then scales into the
// window, with the 2D overlay on top, so the launcher's renderer and this
// share one GPU device.
class SdlGpuRenderer : public gfx::Renderer3D {
public:
    SdlGpuRenderer(SDL_GPUDevice* device, SDL_Renderer* renderer) : device_(device), renderer_(renderer) {}
    ~SdlGpuRenderer() override;
    SdlGpuRenderer(const SdlGpuRenderer&) = delete;
    SdlGpuRenderer& operator=(const SdlGpuRenderer&) = delete;

    Error init(int width, int height);

    Error createTexture(int width, int height, const uint8_t* rgba, const gfx::TextureParams& params,
                        gfx::TextureId& out) override;
    void destroyTexture(gfx::TextureId id) override;
    Error createMesh(const std::vector<gfx::Vertex>& vertices, const std::vector<uint32_t>& indices,
                     gfx::MeshId& out) override;
    void destroyMesh(gfx::MeshId id) override;
    int width() const override { return width_; }
    int height() const override { return height_; }
    std::string deviceName() const override;
    void beginFrame(const gfx::Mat4& viewProj, const gfx::Vec3& eye, Color clear) override;
    void draw(const gfx::DrawCall& call) override;
    void endFrame(const Surface* overlay) override;

private:
    struct Texture {
        SDL_GPUTexture* texture = nullptr;
        int sampler = 0; // index into samplers_
    };
    struct Mesh {
        SDL_GPUBuffer* vertices = nullptr;
        SDL_GPUBuffer* indices = nullptr;
    };

    // Copies `size` bytes into a GPU buffer or texture region through a
    // transfer buffer, in a command buffer of its own.
    bool upload(const void* data, uint32_t size, SDL_GPUBuffer* buffer);
    SDL_GPUGraphicsPipeline* pipeline(const gfx::DrawState& state);

    SDL_GPUDevice* device_;
    SDL_Renderer* renderer_;
    int width_ = 0, height_ = 0;
    SDL_GPUTextureFormat depthFormat_ = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    SDL_GPUTexture* color_ = nullptr; // the frame, wrapped by colorView_ for the renderer
    SDL_GPUTexture* depth_ = nullptr;
    SDL_Texture* colorView_ = nullptr;
    SDL_Texture* overlay_ = nullptr;
    SDL_GPUShader* vertexShader_ = nullptr;
    SDL_GPUShader* fragmentShader_ = nullptr;
    SDL_GPUSampler* samplers_[8] = {}; // by clamp | mipmaps << 1 | nearest << 2
    std::unordered_map<uint32_t, SDL_GPUGraphicsPipeline*> pipelines_; // by packed DrawState
    std::unordered_map<gfx::TextureId, Texture> textures_;
    std::unordered_map<gfx::MeshId, Mesh> meshes_;
    uint32_t nextId_ = 1;
    gfx::TextureId white_ = 0;

    // The frame being drawn.
    SDL_GPUCommandBuffer* cmd_ = nullptr;
    SDL_GPURenderPass* pass_ = nullptr;
    gfx::Mat4 viewProj_{};
    gfx::Vec3 eye_{};
    SDL_GPUGraphicsPipeline* boundPipeline_ = nullptr;
    gfx::MeshId boundMesh_ = 0;
};

} // namespace reluva
