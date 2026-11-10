// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/gfx3d.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

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
    Error setMeshIndices(gfx::MeshId id, const std::vector<uint32_t>& indices) override;
    Error setMeshVertices(gfx::MeshId id, const std::vector<gfx::Vertex>& vertices) override;
    Error createIndexedMesh(gfx::MeshId vertices, const std::vector<uint32_t>& indices, gfx::MeshId& out) override;
    int width() const override { return width_; }
    int height() const override { return height_; }
    std::string deviceName() const override;
    void setKeepFrame(bool keep) override { keepFrame_ = keep; }
    void setFrameAlphaUnused(bool unused) override { frameAlphaUnused_ = unused; }
    void beginFrame(const gfx::Mat4& viewProj, const gfx::Vec3& eye, Color clear) override;
    void draw(const gfx::DrawCall& call) override;
    void setDynamicLights(const std::vector<gfx::DynamicLight>& lights) override {
        lights_.assign(lights.begin(), lights.begin() + std::min<ptrdiff_t>(ptrdiff_t(lights.size()),
                                                                             gfx::kMaxDynamicLights));
    }
    void setViewport(int x, int y, int w, int h) override;
    void setDepthRange(float maxDepth) override;
    void clearDepth() override;
    bool hasStencil() const override { return hasStencil_; }
    void setStencilMode(StencilMode mode) override;
    void setMirrored(bool mirrored) override { mirrored_ = mirrored; }
    void setGamma(float gamma) override { gamma_ = gamma > 0.05f ? gamma : 1.0f; }
    void setView(const gfx::Mat4& viewProj, const gfx::Vec3& eye) override {
        viewProj_ = viewProj;
        eye_ = eye;
    }
    void endFrame(const Surface* overlay) override;
    void captureNextFrame() override { captureWanted_ = true; }
    bool takeCapture(std::vector<uint8_t>& rgba) override {
        if (capture_.empty())
            return false;
        rgba.swap(capture_);
        capture_.clear();
        return true;
    }

private:
    void capture(SDL_GPUTexture* frame);
    struct Texture {
        SDL_GPUTexture* texture = nullptr;
        int sampler = 0; // index into samplers_
    };
    struct Mesh {
        SDL_GPUBuffer* vertices = nullptr;
        SDL_GPUBuffer* indices = nullptr;
        uint32_t indexBytes = 0; // the index buffer's size
        uint32_t vertexBytes = 0; // the vertex buffer's
        bool sharedVertices = false; // another mesh's (createIndexedMesh)
    };

    // Copies `size` bytes into a GPU buffer through a transfer buffer, in
    // the uploads' command buffer (copyPass). `cycle`: into a fresh copy of
    // the buffer if frames still on the GPU use it.
    bool upload(const void* data, uint32_t size, SDL_GPUBuffer* buffer, bool cycle = false);
    // Command buffers in flight are kept few. SDL's Vulkan backend gives
    // every command buffer a descriptor-set cache from a pool, and returns
    // it only once the buffer is known to be done (when a frame is shown
    // or a fence waited for); a cache is never freed or shrunk, and the
    // pool hands them out in turn, so in time each grows to a whole
    // frame's draws. With Mesa's software driver every descriptor set is
    // a memory mapping of its own: hundreds of command buffers in flight
    // at a load (one per upload, and one SDL submits for every texture it
    // creates) meant hundreds of caches, and after a few races more
    // mappings than a process may have.
    //
    // So uploads go into one command buffer, sent just before the frame's
    // (so the frame sees them), or on its own once kUploadBatchBytes or
    // kUploadBatchCount have gathered; and after kTexturesPerWait textures
    // made, the renderer waits for the GPU, which lets SDL take back the
    // command buffers done.
    SDL_GPUCopyPass* copyPass();
    void uploaded(uint32_t bytes); // after each: sends the batch on if it's big enough
    void submitUploads(bool alone);
    static constexpr uint64_t kUploadBatchBytes = 64ull << 20;
    static constexpr int kUploadBatchCount = 256;
    static constexpr int kTexturesPerWait = 8;
    // At most this many frames on the GPU: beginFrame waits for the one
    // this many before it.
    static constexpr int kFramesInFlight = 2;
    SDL_GPUGraphicsPipeline* pipeline(const gfx::DrawState& state);

    SDL_GPUDevice* device_;
    SDL_Renderer* renderer_;
    int width_ = 0, height_ = 0;
    SDL_GPUTextureFormat depthFormat_ = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    bool hasStencil_ = false;
    StencilMode stencilMode_ = StencilMode::None;
    bool mirrored_ = false;
    bool keepFrame_ = false; // frames begin with the last one's contents
    bool frameAlphaUnused_ = false; // source-alpha blends share a pipeline (draw)
    SDL_GPUTexture* color_ = nullptr; // the frame, wrapped by colorView_ for the renderer
    SDL_GPUTexture* depth_ = nullptr;
    SDL_Texture* colorView_ = nullptr;
    SDL_Texture* overlay_ = nullptr;
    // The frame through the brightness curve, shown instead when it isn't
    // 1: drawn from the frame (a texture of it, frameTexture_) with a quad
    // over the whole of it.
    SDL_GPUViewport viewport_{};
    float maxDepth_ = 1;
    float gamma_ = 1;
    SDL_GPUTexture* gammaFrame_ = nullptr;
    SDL_Texture* gammaView_ = nullptr;
    gfx::TextureId frameTexture_ = 0;
    bool captureWanted_ = false;
    std::vector<uint8_t> capture_; // the frame kept by captureNextFrame()
    gfx::MeshId screenQuad_ = 0;
    SDL_GPUShader* vertexShader_ = nullptr;
    SDL_GPUShader* fragmentShader_ = nullptr;
    SDL_GPUSampler* samplers_[32] = {}; // by clamp | mipmaps << 1 | nearest << 2 | mipNearest << 3 | anisotropic << 4
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
    int32_t boundOffset_ = 0, boundOldOffset_ = 0; // the vertex buffers' bindings, in vertices
    std::vector<gfx::DynamicLight> lights_;

    // Uploads waiting to be sent (copyPass).
    SDL_GPUCommandBuffer* uploadCmd_ = nullptr;
    SDL_GPUCopyPass* copyPass_ = nullptr;
    uint64_t uploadBytes_ = 0;
    int uploadCount_ = 0;
    SDL_GPUFence* uploadFence_ = nullptr; // the last batch sent on its own
    int texturesUnwaited_ = 0; // made since the last wait for everything
    SDL_GPUFence* frameFences_[kFramesInFlight] = {}; // the last frames sent, by frameIndex_
    unsigned frameIndex_ = 0;
};

} // namespace reluva
