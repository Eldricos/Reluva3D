// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "graphics/gfx3d.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace reluva {

// gfx::Renderer3D on SDL's GPU API (Vulkan, Metal). Frames are drawn into a
// texture of the requested size which SDL's renderer then scales into the
// window, with the 2D overlay on top, so the launcher's renderer and this
// share one GPU device.
class SdlGpuRenderer : public gfx::Renderer3D {
public:
    SdlGpuRenderer(SDL_GPUDevice *device, SDL_Renderer *renderer) : _device(device), _renderer(renderer) {}
    ~SdlGpuRenderer() override;
    SdlGpuRenderer(const SdlGpuRenderer &) = delete;
    SdlGpuRenderer &operator=(const SdlGpuRenderer &) = delete;

    Error init(int width, int height);
    // Called after each frame is drawn into the window, before it's
    // presented (in the frame's coordinates): the backend's software cursor.
    void setBeforePresent(std::function<void()> fn) { _beforePresent = std::move(fn); }

    Error createTexture(int width, int height, const uint8_t *rgba, const gfx::TextureParams &params,
                        gfx::TextureId &out) override;
    void destroyTexture(gfx::TextureId id) override;
    Error createMesh(const std::vector<gfx::Vertex> &vertices, const std::vector<uint32_t> &indices,
                     gfx::MeshId &out) override;
    void destroyMesh(gfx::MeshId id) override;
    Error setMeshIndices(gfx::MeshId id, const std::vector<uint32_t> &indices) override;
    Error setMeshVertices(gfx::MeshId id, const std::vector<gfx::Vertex> &vertices) override;
    Error createIndexedMesh(gfx::MeshId vertices, const std::vector<uint32_t> &indices, gfx::MeshId &out) override;
    int width() const override { return _width; }
    int height() const override { return _height; }
    std::string deviceName() const override;
    void setKeepFrame(bool keep) override { _keepFrame = keep; }
    void setFrameAlphaUnused(bool unused) override { _frameAlphaUnused = unused; }
    void beginFrame(const gfx::Mat4 &viewProj, const gfx::Vec3 &eye, Color clear) override;
    void draw(const gfx::DrawCall &call) override;
    void setDynamicLights(const std::vector<gfx::DynamicLight> &lights) override {
        _lights.assign(lights.begin(),
                       lights.begin() + std::min<ptrdiff_t>(ptrdiff_t(lights.size()), gfx::kMaxDynamicLights));
    }
    void setViewport(int x, int y, int w, int h) override;
    void setDepthRange(float maxDepth) override;
    void clearDepth() override;
    bool hasStencil() const override { return _hasStencil; }
    void setStencilMode(StencilMode mode) override;
    void setStencilReference(uint8_t ref) override;
    void setMirrored(bool mirrored) override { _mirrored = mirrored; }
    void setGamma(float gamma) override { _gamma = gamma > 0.05f ? gamma : 1.0f; }
    int setMultisampling(int samples) override;
    void setView(const gfx::Mat4 &viewProj, const gfx::Vec3 &eye) override {
        _viewProj = viewProj;
        _eye = eye;
    }
    void endFrame(const Surface *overlay) override;
    void captureNextFrame() override { _captureWanted = true; }
    bool takeCapture(std::vector<uint8_t> &rgba) override {
        if (_capture.empty())
            return false;
        rgba.swap(_capture);
        _capture.clear();
        return true;
    }

private:
    void capture(SDL_GPUTexture *frame);
    // The multisampled targets for samples_ (none for 1), made again
    // when it changes; the pipelines go with them.
    bool makeMultisampleTargets();
    // A pass over the frame: cleared (with `clear`) or kept, its depth
    // cleared, its stencil cleared or kept.
    SDL_GPURenderPass *beginPass(const SDL_FColor *clear, bool keepStencil);
    struct Texture {
        SDL_GPUTexture *texture = nullptr;
        int sampler = 0; // index into samplers_
    };
    struct Mesh {
        SDL_GPUBuffer *vertices = nullptr;
        SDL_GPUBuffer *indices = nullptr;
        uint32_t indexBytes = 0;     // the index buffer's size
        uint32_t vertexBytes = 0;    // the vertex buffer's
        bool sharedVertices = false; // another mesh's (createIndexedMesh)
    };

    // Copies `size` bytes into a GPU buffer, `cycle`: into a fresh copy of
    // the buffer if frames still on the GPU use it. Kept till flush(),
    // which sends all those waiting in one command buffer of their own
    // (done before any draw and at the frame's end, so draws see them).
    bool upload(const void *data, uint32_t size, SDL_GPUBuffer *buffer, bool cycle = false);
    bool flush();
    struct PendingUpload {
        SDL_GPUBuffer *buffer;
        uint32_t offset, size; // in staging_
        bool cycle;
    };
    std::vector<PendingUpload> _pending;
    std::vector<uint8_t> _staging;
    SDL_GPUTransferBuffer *_transfer = nullptr;
    uint32_t _transferBytes = 0;
    // Command buffers in flight are kept few. SDL's Vulkan backend gives
    // every command buffer a descriptor-set cache from a pool, and returns
    // it only once the buffer is known to be done (when a frame is shown
    // or a fence waited for); a cache is never freed or shrunk, and the
    // pool hands them out in turn, so in time each grows to a whole
    // frame's draws. With Mesa's software driver every descriptor set is
    // a memory mapping of its own: hundreds of command buffers in flight
    // at a load (one SDL submits for every texture it creates, and one
    // for each texture's upload) meant hundreds of caches, and after a
    // few races more mappings than a process may have. So after
    // kTexturesPerWait textures made, the renderer waits for the GPU,
    // which lets SDL take back the command buffers done; and no more than
    // kFramesInFlight frames are on the GPU (beginFrame waits for the one
    // that many before it).
    static constexpr int kTexturesPerWait = 8;
    static constexpr int kFramesInFlight = 2;
    int _texturesUnwaited = 0;                        // made since the last wait for everything
    SDL_GPUFence *_frameFences[kFramesInFlight] = {}; // the last frames sent, by frameIndex_
    unsigned _frameIndex = 0;
    SDL_GPUGraphicsPipeline *pipeline(const gfx::DrawState &state);

    SDL_GPUDevice *_device;
    SDL_Renderer *_renderer;
    int _width = 0, _height = 0;
    SDL_GPUTextureFormat _depthFormat = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    bool _hasStencil = false;
    StencilMode _stencilMode = StencilMode::kNone;
    uint8_t _stencilRef = 3;    // kStencilMarks: both kinds of mark
    bool _stencilWhole = false; // a reference set this frame: written and compared whole
    bool _mirrored = false;
    bool _keepFrame = false;          // frames begin with the last one's contents
    bool _frameAlphaUnused = false;   // source-alpha blends share a pipeline (draw)
    SDL_GPUTexture *_color = nullptr; // the frame, wrapped by colorView_ for the renderer
    SDL_GPUTexture *_depth = nullptr;
    // Multisampling: the count wanted, the count the targets have, and
    // those targets, resolved into color_ at the end of each pass. The
    // passes over the frame use passSamples_ (1 for the brightness pass).
    SDL_GPUSampleCount _wantSamples = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUSampleCount _samples = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUSampleCount _passSamples = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture *_msaaColor = nullptr;
    SDL_GPUTexture *_msaaDepth = nullptr;
    SDL_Texture *_colorView = nullptr;
    SDL_Texture *_overlay = nullptr;
    std::function<void()> _beforePresent;
    // The frame through the brightness curve, shown instead when it isn't
    // 1: drawn from the frame (a texture of it, frameTexture_) with a quad
    // over the whole of it.
    SDL_GPUViewport _viewport{};
    float _maxDepth = 1;
    float _gamma = 1;
    SDL_GPUTexture *_gammaFrame = nullptr;
    SDL_Texture *_gammaView = nullptr;
    gfx::TextureId _frameTexture = 0;
    bool _captureWanted = false;
    std::vector<uint8_t> _capture; // the frame kept by captureNextFrame()
    gfx::MeshId _screenQuad = 0;
    SDL_GPUShader *_vertexShader = nullptr;
    SDL_GPUShader *_fragmentShader = nullptr;
    SDL_GPUSampler *_samplers[32] = {}; // by clamp | mipmaps << 1 | nearest << 2 | mipNearest << 3 | anisotropic << 4
    std::unordered_map<uint32_t, SDL_GPUGraphicsPipeline *> _pipelines; // by packed DrawState and pass
    std::unordered_map<gfx::TextureId, Texture> _textures;
    std::unordered_map<gfx::MeshId, Mesh> _meshes;
    uint32_t _nextId = 1;
    gfx::TextureId _white = 0;

    // The frame being drawn.
    SDL_GPUCommandBuffer *_cmd = nullptr;
    SDL_GPURenderPass *_pass = nullptr;
    gfx::Mat4 _viewProj{};
    gfx::Vec3 _eye{};
    SDL_GPUGraphicsPipeline *_boundPipeline = nullptr;
    gfx::MeshId _boundMesh = 0;
    int32_t _boundOffset = 0, _boundOldOffset = 0; // the vertex buffers' bindings, in vertices
    std::vector<gfx::DynamicLight> _lights;
};

} // namespace reluva
