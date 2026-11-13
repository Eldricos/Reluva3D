// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "backends/graphics/sdl3/sdl_gpu.h"
#include "backends/graphics/sdl3/shaders/compiled.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace reluva {

namespace {

constexpr uint8_t kStencilMarks = 3; // the stencil reference: a mirror's mark (1) and the sky's (2)

Error sdlError(const char *what) {
    return Error::fail(std::string(what) + ": " + SDL_GetError());
}

// The layouts of draw.vert's and draw.frag's uniform blocks (std140).
struct DeformUniforms {
    float a[4];
    float b[4];
    float c[4];
};
struct VertexUniforms {
    float viewProj[16];
    float model[16];
    float eye[4];
    float preRow0[4];
    float preRow1[4];
    float postRow0[4];
    float postRow1[4];
    float turb[4];
    float texVec0[4];
    float texVec1[4];
    float color[4];
    float mode[4];
    float mode2[4];
    float fogPlane[4];
    float ambient[4];
    float directed[4];
    float lightDir[4];
    DeformUniforms deforms[2];
};
struct FragmentUniforms {
    float params[4];
    float fog[4];   // rgb, and DrawCall::tableFog
    float blend[4]; // blendOut's factors (draw)
    float lightCount[4];
    float lightPos[gfx::kMaxDynamicLights][4]; // xyz, radius
    float lightColor[gfx::kMaxDynamicLights][4];
};

SDL_GPUBlendFactor blendFactor(gfx::Blend b) {
    switch (b) {
    case gfx::Blend::kZero:
        return SDL_GPU_BLENDFACTOR_ZERO;
    case gfx::Blend::kOne:
        return SDL_GPU_BLENDFACTOR_ONE;
    case gfx::Blend::kSrcColor:
        return SDL_GPU_BLENDFACTOR_SRC_COLOR;
    case gfx::Blend::kOneMinusSrcColor:
        return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
    case gfx::Blend::kDstColor:
        return SDL_GPU_BLENDFACTOR_DST_COLOR;
    case gfx::Blend::kOneMinusDstColor:
        return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
    case gfx::Blend::kSrcAlpha:
        return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    case gfx::Blend::kOneMinusSrcAlpha:
        return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    case gfx::Blend::kDstAlpha:
        return SDL_GPU_BLENDFACTOR_DST_ALPHA;
    case gfx::Blend::kOneMinusDstAlpha:
        return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
    }
    return SDL_GPU_BLENDFACTOR_ONE;
}

void waitAndRelease(SDL_GPUDevice *device, SDL_GPUFence *&fence) {
    if (!fence)
        return;
    SDL_WaitForGPUFences(device, true, &fence, 1);
    SDL_ReleaseGPUFence(device, fence);
    fence = nullptr;
}

uint32_t packState(const gfx::DrawState &s) {
    return uint32_t(s.src) | uint32_t(s.dst) << 4 | uint32_t(s.depthTest) << 8 | uint32_t(s.depthWrite) << 9 |
           uint32_t(s.cull) << 10 | uint32_t(s.depthEqual) << 12 | uint32_t(s.depthBias) << 13;
}

} // namespace

SdlGpuRenderer::~SdlGpuRenderer() {
    flush();
    if (_cmd) {
        if (_pass)
            SDL_EndGPURenderPass(_pass);
        SDL_SubmitGPUCommandBuffer(_cmd);
    }
    // Nothing may still be using the resources.
    SDL_WaitForGPUIdle(_device);
    for (SDL_GPUFence *&f : _frameFences)
        waitAndRelease(_device, f);
    _textures.erase(_frameTexture); // the frame itself, released below
    for (auto &[id, t] : _textures)
        SDL_ReleaseGPUTexture(_device, t.texture);
    for (auto &[id, m] : _meshes) {
        if (!m.sharedVertices)
            SDL_ReleaseGPUBuffer(_device, m.vertices);
        SDL_ReleaseGPUBuffer(_device, m.indices);
    }
    if (_transfer)
        SDL_ReleaseGPUTransferBuffer(_device, _transfer);
    for (auto &[key, p] : _pipelines)
        SDL_ReleaseGPUGraphicsPipeline(_device, p);
    for (SDL_GPUSampler *s : _samplers)
        if (s)
            SDL_ReleaseGPUSampler(_device, s);
    if (_vertexShader)
        SDL_ReleaseGPUShader(_device, _vertexShader);
    if (_fragmentShader)
        SDL_ReleaseGPUShader(_device, _fragmentShader);
    if (_overlay)
        SDL_DestroyTexture(_overlay);
    if (_colorView)
        SDL_DestroyTexture(_colorView);
    if (_gammaView)
        SDL_DestroyTexture(_gammaView);
    if (_gammaFrame)
        SDL_ReleaseGPUTexture(_device, _gammaFrame);
    if (_color)
        SDL_ReleaseGPUTexture(_device, _color);
    if (_depth)
        SDL_ReleaseGPUTexture(_device, _depth);
    if (_msaaColor)
        SDL_ReleaseGPUTexture(_device, _msaaColor);
    if (_msaaDepth)
        SDL_ReleaseGPUTexture(_device, _msaaDepth);
}

int SdlGpuRenderer::setMultisampling(int samples) {
    // The most the device has for both the frame's format and the depth's.
    _wantSamples = SDL_GPU_SAMPLECOUNT_1;
    for (SDL_GPUSampleCount c : {SDL_GPU_SAMPLECOUNT_4, SDL_GPU_SAMPLECOUNT_2}) {
        const int n = c == SDL_GPU_SAMPLECOUNT_4 ? 4 : 2;
        if (n <= samples && SDL_GPUTextureSupportsSampleCount(_device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, c) &&
            SDL_GPUTextureSupportsSampleCount(_device, _depthFormat, c)) {
            _wantSamples = c;
            break;
        }
    }
    return _wantSamples == SDL_GPU_SAMPLECOUNT_4 ? 4 : _wantSamples == SDL_GPU_SAMPLECOUNT_2 ? 2 : 1;
}

bool SdlGpuRenderer::makeMultisampleTargets() {
    // Released once the GPU is done with them.
    if (_msaaColor)
        SDL_ReleaseGPUTexture(_device, _msaaColor);
    if (_msaaDepth)
        SDL_ReleaseGPUTexture(_device, _msaaDepth);
    _msaaColor = _msaaDepth = nullptr;
    for (auto &[key, p] : _pipelines)
        if (p)
            SDL_ReleaseGPUGraphicsPipeline(_device, p);
    _pipelines.clear();
    _samples = _wantSamples;
    if (_samples == SDL_GPU_SAMPLECOUNT_1)
        return true;
    SDL_GPUTextureCreateInfo ci{};
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    ci.width = uint32_t(_width);
    ci.height = uint32_t(_height);
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    ci.sample_count = _samples;
    _msaaColor = SDL_CreateGPUTexture(_device, &ci);
    ci.format = _depthFormat;
    ci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    _msaaDepth = SDL_CreateGPUTexture(_device, &ci);
    if (_msaaColor && _msaaDepth)
        return true;
    SDL_Log("Cannot create the multisampled frame: %s", SDL_GetError());
    if (_msaaColor)
        SDL_ReleaseGPUTexture(_device, _msaaColor);
    if (_msaaDepth)
        SDL_ReleaseGPUTexture(_device, _msaaDepth);
    _msaaColor = _msaaDepth = nullptr;
    _samples = _wantSamples = SDL_GPU_SAMPLECOUNT_1;
    return false;
}

SDL_GPURenderPass *SdlGpuRenderer::beginPass(const SDL_FColor *clear, bool keepStencil) {
    SDL_GPUColorTargetInfo color{};
    color.load_op = clear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
    if (clear)
        color.clear_color = *clear;
    SDL_GPUDepthStencilTargetInfo depth{};
    if (_msaaColor) {
        // Drawn multisampled and resolved into the frame; the samples are
        // kept too, for a pass that goes on after clearDepth().
        color.texture = _msaaColor;
        color.resolve_texture = _color;
        color.store_op = SDL_GPU_STOREOP_RESOLVE_AND_STORE;
        depth.texture = _msaaDepth;
    } else {
        color.texture = _color;
        color.store_op = SDL_GPU_STOREOP_STORE;
        depth.texture = _depth;
    }
    depth.clear_depth = 1.0f;
    depth.load_op = SDL_GPU_LOADOP_CLEAR;
    depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth.clear_stencil = 0;
    depth.stencil_load_op = !_hasStencil  ? SDL_GPU_LOADOP_DONT_CARE
                            : keepStencil ? SDL_GPU_LOADOP_LOAD
                                          : SDL_GPU_LOADOP_CLEAR;
    depth.stencil_store_op = _hasStencil ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
    _passSamples = _samples;
    return SDL_BeginGPURenderPass(_cmd, &color, 1, &depth);
}

Error SdlGpuRenderer::init(int width, int height) {
    _width = width;
    _height = height;

    // Shaders, in whichever format the device takes.
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(_device);
    auto makeShader = [&](SDL_GPUShaderStage stage, const uint8_t *spirv, size_t spirvSize, const char *msl,
                          uint32_t samplers) -> SDL_GPUShader * {
        SDL_GPUShaderCreateInfo info{};
        info.stage = stage;
        info.num_samplers = samplers;
        info.num_uniform_buffers = 1;
        if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {
            info.format = SDL_GPU_SHADERFORMAT_SPIRV;
            info.code = spirv;
            info.code_size = spirvSize;
            info.entrypoint = "main";
        } else if (formats & SDL_GPU_SHADERFORMAT_MSL) {
            info.format = SDL_GPU_SHADERFORMAT_MSL;
            info.code = reinterpret_cast<const uint8_t *>(msl);
            info.code_size = std::strlen(msl);
            info.entrypoint = "main0";
        } else {
            return nullptr;
        }
        return SDL_CreateGPUShader(_device, &info);
    };
    _vertexShader = makeShader(SDL_GPU_SHADERSTAGE_VERTEX, shaders::kDrawVertexSpirv, sizeof(shaders::kDrawVertexSpirv),
                               shaders::kDrawVertexMsl, 0);
    _fragmentShader = makeShader(SDL_GPU_SHADERSTAGE_FRAGMENT, shaders::kDrawFragmentSpirv,
                                 sizeof(shaders::kDrawFragmentSpirv), shaders::kDrawFragmentMsl, 1);
    if (!_vertexShader || !_fragmentShader)
        return sdlError("Cannot create the 3D shaders");

    for (int i = 0; i < 32; ++i) {
        const bool clamp = i & 1;
        const bool mipmaps = i & 2;
        const bool nearest = i & 4;
        const bool mipNearest = i & 8;
        const bool anisotropic = i & 16;
        SDL_GPUSamplerCreateInfo info{};
        info.min_filter = info.mag_filter = nearest ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
        info.mipmap_mode = nearest || mipNearest ? SDL_GPU_SAMPLERMIPMAPMODE_NEAREST : SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        info.address_mode_u = info.address_mode_v = info.address_mode_w =
            clamp ? SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE : SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        info.max_lod = mipmaps ? 1000.0f : 0.0f;
        // Sharper textures seen at a slant; every Vulkan and Metal device
        // SDL takes has it.
        info.enable_anisotropy = anisotropic && mipmaps;
        info.max_anisotropy = 16.0f;
        _samplers[i] = SDL_CreateGPUSampler(_device, &info);
        if (!_samplers[i])
            return sdlError("Cannot create a sampler");
    }

    // The frame and its depth buffer.
    SDL_GPUTextureCreateInfo ci{};
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ci.width = uint32_t(width);
    ci.height = uint32_t(height);
    ci.layer_count_or_depth = 1;
    ci.num_levels = 1;
    _color = SDL_CreateGPUTexture(_device, &ci);
    if (!_color)
        return sdlError("Cannot create the 3D frame");
    // Depth with a stencil (for mirrors and portals) if the device has one.
    for (SDL_GPUTextureFormat f : {SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT, SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
                                   SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTUREFORMAT_D24_UNORM}) {
        if (SDL_GPUTextureSupportsFormat(_device, f, SDL_GPU_TEXTURETYPE_2D,
                                         SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
            _depthFormat = f;
            _hasStencil = f == SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT || f == SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
            break;
        }
    }
    ci.format = _depthFormat;
    ci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    _depth = SDL_CreateGPUTexture(_device, &ci);
    if (!_depth)
        return sdlError("Cannot create the depth buffer");

    // The renderer shows the frame through a texture wrapping it.
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGBA32);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STATIC);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, width);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, height);
    SDL_SetPointerProperty(props, SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER, _color);
    _colorView = SDL_CreateTextureWithProperties(_renderer, props);
    SDL_DestroyProperties(props);
    if (!_colorView)
        return sdlError("Cannot show the 3D frame");
    SDL_SetTextureBlendMode(_colorView, SDL_BLENDMODE_NONE);

    // The brightness pass: a second frame, the first as a texture to draw
    // from, and a quad covering it (clip space; v down the frame).
    ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ci.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    _gammaFrame = SDL_CreateGPUTexture(_device, &ci);
    if (!_gammaFrame)
        return sdlError("Cannot create the 3D frame");
    const SDL_PropertiesID gprops = SDL_CreateProperties();
    SDL_SetNumberProperty(gprops, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGBA32);
    SDL_SetNumberProperty(gprops, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STATIC);
    SDL_SetNumberProperty(gprops, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, width);
    SDL_SetNumberProperty(gprops, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, height);
    SDL_SetPointerProperty(gprops, SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER, _gammaFrame);
    _gammaView = SDL_CreateTextureWithProperties(_renderer, gprops);
    SDL_DestroyProperties(gprops);
    if (!_gammaView)
        return sdlError("Cannot show the 3D frame");
    SDL_SetTextureBlendMode(_gammaView, SDL_BLENDMODE_NONE);
    _frameTexture = _nextId++;
    _textures[_frameTexture] = Texture{_color, 1}; // clamped, no mipmaps

    _overlay = SDL_CreateTexture(_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!_overlay)
        return sdlError("Cannot create the overlay");
    SDL_SetTextureBlendMode(_overlay, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(_overlay, SDL_SCALEMODE_NEAREST);

    const uint8_t white[4] = {255, 255, 255, 255};
    gfx::TextureParams wp;
    wp.mipmaps = false;
    if (Error err = createTexture(1, 1, white, wp, _white))
        return err;
    std::vector<gfx::Vertex> quad(4);
    const float corners[4][4] = {{-1, -1, 0, 1}, {1, -1, 1, 1}, {1, 1, 1, 0}, {-1, 1, 0, 0}};
    for (size_t i = 0; i < 4; ++i) {
        quad[i].pos[0] = corners[i][0];
        quad[i].pos[1] = corners[i][1];
        quad[i].uv[0] = corners[i][2];
        quad[i].uv[1] = corners[i][3];
        quad[i].normal[2] = 1;
        std::fill(std::begin(quad[i].color), std::end(quad[i].color), uint8_t(255));
    }
    return createMesh(quad, {0, 1, 2, 0, 2, 3}, _screenQuad);
}

void SdlGpuRenderer::setViewport(int x, int y, int w, int h) {
    if (!_pass)
        return;
    SDL_GPUViewport v{};
    v.x = float(w > 0 && h > 0 ? x : 0);
    v.y = float(w > 0 && h > 0 ? y : 0);
    v.w = float(w > 0 && h > 0 ? w : _width);
    v.h = float(w > 0 && h > 0 ? h : _height);
    v.min_depth = 0;
    v.max_depth = _maxDepth;
    _viewport = v;
    SDL_SetGPUViewport(_pass, &v);
}

void SdlGpuRenderer::setDepthRange(float maxDepth) {
    maxDepth = std::clamp(maxDepth, 0.01f, 1.0f);
    if (maxDepth == _maxDepth)
        return;
    _maxDepth = maxDepth;
    if (!_pass)
        return;
    if (_viewport.w <= 0 || _viewport.h <= 0)
        _viewport = {0, 0, float(_width), float(_height), 0, 1};
    _viewport.max_depth = _maxDepth;
    SDL_SetGPUViewport(_pass, &_viewport);
}

void SdlGpuRenderer::clearDepth() {
    if (!_pass || !_cmd)
        return;
    // A new pass over the same frame: its colours kept, its depth cleared,
    // the marks kept.
    SDL_EndGPURenderPass(_pass);
    _pass = beginPass(nullptr, true);
    _boundPipeline = nullptr;
    _boundMesh = 0;
    if (_pass && _viewport.w > 0 && _viewport.h > 0)
        SDL_SetGPUViewport(_pass, &_viewport);
    if (_pass)
        SDL_SetGPUStencilReference(_pass, _stencilRef); // both kinds of mark, unless set
}

void SdlGpuRenderer::setStencilMode(StencilMode mode) {
    if (_hasStencil)
        _stencilMode = mode;
}

void SdlGpuRenderer::setStencilReference(uint8_t ref) {
    _stencilRef = ref;
    _stencilWhole = true;
    if (_pass)
        SDL_SetGPUStencilReference(_pass, ref);
}

bool SdlGpuRenderer::upload(const void *data, uint32_t size, SDL_GPUBuffer *buffer, bool cycle) {
    if (size == 0)
        return true;
    const auto offset = uint32_t(_staging.size());
    const auto *bytes = static_cast<const uint8_t *>(data);
    _staging.insert(_staging.end(), bytes, bytes + size);
    _pending.push_back({buffer, offset, size, cycle});
    return true;
}

bool SdlGpuRenderer::flush() {
    if (_pending.empty())
        return true;
    const auto size = uint32_t(_staging.size());
    if (!_transfer || _transferBytes < size) {
        if (_transfer)
            SDL_ReleaseGPUTransferBuffer(_device, _transfer);
        SDL_GPUTransferBufferCreateInfo ti{};
        ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        ti.size = std::max(size + size / 2, uint32_t(1) << 20);
        _transfer = SDL_CreateGPUTransferBuffer(_device, &ti);
        _transferBytes = _transfer ? ti.size : 0;
    }
    bool ok = _transfer != nullptr;
    // Cycled as it's mapped: the last frame's copies may still be reading it.
    void *mapped = ok ? SDL_MapGPUTransferBuffer(_device, _transfer, true) : nullptr;
    ok = mapped != nullptr;
    SDL_GPUCommandBuffer *cmd = ok ? SDL_AcquireGPUCommandBuffer(_device) : nullptr;
    if (mapped) {
        std::memcpy(mapped, _staging.data(), size);
        SDL_UnmapGPUTransferBuffer(_device, _transfer);
    }
    if (cmd) {
        SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
        for (const PendingUpload &u : _pending) {
            SDL_GPUTransferBufferLocation src{};
            src.transfer_buffer = _transfer;
            src.offset = u.offset;
            SDL_GPUBufferRegion dst{};
            dst.buffer = u.buffer;
            dst.size = u.size;
            SDL_UploadToGPUBuffer(copy, &src, &dst, u.cycle);
        }
        SDL_EndGPUCopyPass(copy);
        ok = SDL_SubmitGPUCommandBuffer(cmd);
    } else {
        ok = false;
    }
    _pending.clear();
    _staging.clear();
    // Bound buffers may have been cycled: bound again on the next draw.
    _boundMesh = 0;
    return ok;
}

Error SdlGpuRenderer::createTexture(int width, int height, const uint8_t *rgba, const gfx::TextureParams &params,
                                    gfx::TextureId &out) {
    out = 0;
    if (width <= 0 || height <= 0)
        return Error::fail("Empty texture");
    int levels = 1;
    if (params.mipmaps)
        while ((std::max(width, height) >> levels) > 0)
            ++levels;
    SDL_GPUTextureCreateInfo ci{};
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ci.width = uint32_t(width);
    ci.height = uint32_t(height);
    ci.layer_count_or_depth = 1;
    ci.num_levels = uint32_t(levels);
    SDL_GPUTexture *tex = SDL_CreateGPUTexture(_device, &ci);
    if (!tex)
        return sdlError("Cannot create a texture");

    // Every level made here, each pixel the average of the 2x2 above it
    // (a lone row or column halves along the other side only), and all
    // uploaded: a level left to the GPU to fill may keep whatever the
    // memory held before.
    struct Level {
        int w;
        int h;
        uint32_t offset;
    };
    std::vector<Level> chain;
    uint32_t size = 0;
    for (int l = 0, w = width, h = height; l < levels; ++l, w = std::max(w / 2, 1), h = std::max(h / 2, 1)) {
        chain.push_back({w, h, size});
        size += uint32_t(w) * uint32_t(h) * 4;
    }
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = size;
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(_device, &ti);
    auto *mapped = static_cast<uint8_t *>(tb ? SDL_MapGPUTransferBuffer(_device, tb, false) : nullptr);
    if (!mapped) {
        if (tb)
            SDL_ReleaseGPUTransferBuffer(_device, tb);
        SDL_ReleaseGPUTexture(_device, tex);
        return sdlError("Cannot upload a texture");
    }
    std::memcpy(mapped, rgba, size_t(width) * size_t(height) * 4);
    for (size_t l = 1; l < chain.size(); ++l) {
        const Level &up = chain[l - 1];
        const Level &lv = chain[l];
        const uint8_t *in = mapped + up.offset;
        uint8_t *px = mapped + lv.offset;
        const int sx = up.w > 1 ? 2 : 1;
        const int sy = up.h > 1 ? 2 : 1;
        const int n = sx * sy;
        for (int y = 0; y < lv.h; ++y)
            for (int x = 0; x < lv.w; ++x)
                for (int c = 0; c < 4; ++c) {
                    int sum = 0;
                    for (int dy = 0; dy < sy; ++dy)
                        for (int dx = 0; dx < sx; ++dx)
                            sum += in[((y * sy + dy) * up.w + x * sx + dx) * 4 + c];
                    px[(y * lv.w + x) * 4 + c] = uint8_t((sum + n / 2) / n);
                }
    }
    SDL_UnmapGPUTransferBuffer(_device, tb);

    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(_device);
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
    for (size_t l = 0; l < chain.size(); ++l) {
        SDL_GPUTextureTransferInfo src{};
        src.transfer_buffer = tb;
        src.offset = chain[l].offset;
        SDL_GPUTextureRegion dst{};
        dst.texture = tex;
        dst.mip_level = uint32_t(l);
        dst.w = uint32_t(chain[l].w);
        dst.h = uint32_t(chain[l].h);
        dst.d = 1;
        SDL_UploadToGPUTexture(copy, &src, &dst, false);
    }
    SDL_EndGPUCopyPass(copy);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(_device, tb);
    // SDL submitted a command buffer of its own to make the texture, and
    // the upload is another: let it take them back now and then.
    if (++_texturesUnwaited >= kTexturesPerWait) {
        SDL_WaitForGPUIdle(_device);
        _texturesUnwaited = 0;
    }

    out = _nextId++;
    _textures[out] = {tex, int(params.clamp) | int(levels > 1) << 1 | int(params.nearest) << 2 |
                               int(params.mipNearest) << 3 | int(params.anisotropic) << 4};
    return Error::success();
}

std::string SdlGpuRenderer::deviceName() const {
    const SDL_PropertiesID props = SDL_GetGPUDeviceProperties(_device);
    std::string name = SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_NAME_STRING, "unknown GPU");
    name += std::string(" (") + SDL_GetGPUDeviceDriver(_device);
    if (const char *driver = SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING, nullptr))
        name += std::string(", ") + driver;
    if (const char *version = SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING, nullptr))
        name += std::string(" ") + version;
    return name + ")";
}

void SdlGpuRenderer::destroyTexture(gfx::TextureId id) {
    auto it = _textures.find(id);
    if (it == _textures.end() || id == _white)
        return;
    SDL_ReleaseGPUTexture(_device, it->second.texture); // SDL defers it until the GPU is done
    _textures.erase(it);
}

Error SdlGpuRenderer::createMesh(const std::vector<gfx::Vertex> &vertices, const std::vector<uint32_t> &indices,
                                 gfx::MeshId &out) {
    out = 0;
    static_assert(sizeof(gfx::Vertex) == 44, "the pipeline's vertex layout");
    if (vertices.empty() || indices.empty())
        return Error::fail("Empty mesh");
    Mesh m;
    SDL_GPUBufferCreateInfo bi{};
    bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bi.size = uint32_t(vertices.size() * sizeof(gfx::Vertex));
    m.vertices = SDL_CreateGPUBuffer(_device, &bi);
    bi.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    bi.size = uint32_t(indices.size() * sizeof(uint32_t));
    m.indices = SDL_CreateGPUBuffer(_device, &bi);
    if (!m.vertices || !m.indices ||
        !upload(vertices.data(), uint32_t(vertices.size() * sizeof(gfx::Vertex)), m.vertices) ||
        !upload(indices.data(), uint32_t(indices.size() * sizeof(uint32_t)), m.indices)) {
        Error err = sdlError("Cannot create a mesh");
        if (m.vertices)
            SDL_ReleaseGPUBuffer(_device, m.vertices);
        if (m.indices)
            SDL_ReleaseGPUBuffer(_device, m.indices);
        return err;
    }
    m.indexBytes = uint32_t(indices.size() * sizeof(uint32_t));
    m.vertexBytes = uint32_t(vertices.size() * sizeof(gfx::Vertex));
    out = _nextId++;
    _meshes[out] = m;
    return Error::success();
}

Error SdlGpuRenderer::setMeshVertices(gfx::MeshId id, const std::vector<gfx::Vertex> &vertices) {
    auto it = _meshes.find(id);
    if (it == _meshes.end() || vertices.empty() || it->second.sharedVertices)
        return Error::fail("No such mesh, or no vertices");
    // As setMeshIndices: into the buffer there is when they fit (cycled),
    // else a bigger one.
    const auto bytes = uint32_t(vertices.size() * sizeof(gfx::Vertex));
    if (bytes <= it->second.vertexBytes) {
        if (!upload(vertices.data(), bytes, it->second.vertices, true))
            return sdlError("Cannot replace a mesh's vertices");
    } else {
        flush(); // nothing waiting for the old buffer
        SDL_GPUBufferCreateInfo bi{};
        bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        bi.size = bytes + bytes / 2; // room to grow: geometry made anew each frame
        SDL_GPUBuffer *buffer = SDL_CreateGPUBuffer(_device, &bi);
        if (!buffer || !upload(vertices.data(), bytes, buffer)) {
            if (buffer)
                SDL_ReleaseGPUBuffer(_device, buffer);
            return sdlError("Cannot replace a mesh's vertices");
        }
        SDL_ReleaseGPUBuffer(_device, it->second.vertices);
        it->second.vertices = buffer;
        it->second.vertexBytes = bi.size;
    }
    if (_boundMesh == id)
        _boundMesh = 0;
    return Error::success();
}

Error SdlGpuRenderer::setMeshIndices(gfx::MeshId id, const std::vector<uint32_t> &indices) {
    auto it = _meshes.find(id);
    if (it == _meshes.end() || indices.empty())
        return Error::fail("No such mesh, or no indices");
    // Into the buffer there is when they fit (cycled, as frames still on
    // the GPU may use it: changed every frame, as the view turns); else a
    // bigger one, the old released once those frames are done.
    const auto bytes = uint32_t(indices.size() * sizeof(uint32_t));
    if (bytes <= it->second.indexBytes) {
        if (!upload(indices.data(), bytes, it->second.indices, true))
            return sdlError("Cannot replace a mesh's indices");
    } else {
        SDL_GPUBufferCreateInfo bi{};
        bi.usage = SDL_GPU_BUFFERUSAGE_INDEX;
        bi.size = bytes + bytes / 2;
        flush(); // nothing waiting for the old buffer
        SDL_GPUBuffer *buffer = SDL_CreateGPUBuffer(_device, &bi);
        if (!buffer || !upload(indices.data(), bytes, buffer)) {
            if (buffer)
                SDL_ReleaseGPUBuffer(_device, buffer);
            return sdlError("Cannot replace a mesh's indices");
        }
        SDL_ReleaseGPUBuffer(_device, it->second.indices);
        it->second.indices = buffer;
        it->second.indexBytes = bi.size;
    }
    if (_boundMesh == id)
        _boundMesh = 0; // bind the new indices on its next draw
    return Error::success();
}

Error SdlGpuRenderer::createIndexedMesh(gfx::MeshId vertices, const std::vector<uint32_t> &indices, gfx::MeshId &out) {
    out = 0;
    auto source = _meshes.find(vertices);
    if (source == _meshes.end() || indices.empty())
        return Error::fail("No such mesh, or no indices");
    Mesh m;
    m.vertices = source->second.vertices;
    m.sharedVertices = true;
    SDL_GPUBufferCreateInfo bi{};
    bi.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    bi.size = uint32_t(indices.size() * sizeof(uint32_t));
    m.indices = SDL_CreateGPUBuffer(_device, &bi);
    if (!m.indices || !upload(indices.data(), bi.size, m.indices)) {
        if (m.indices)
            SDL_ReleaseGPUBuffer(_device, m.indices);
        return sdlError("Cannot create a mesh");
    }
    m.indexBytes = bi.size;
    out = _nextId++;
    _meshes[out] = m;
    return Error::success();
}

void SdlGpuRenderer::destroyMesh(gfx::MeshId id) {
    auto it = _meshes.find(id);
    if (it == _meshes.end())
        return;
    flush(); // nothing waiting for its buffers
    if (!it->second.sharedVertices)
        SDL_ReleaseGPUBuffer(_device, it->second.vertices);
    SDL_ReleaseGPUBuffer(_device, it->second.indices);
    _meshes.erase(it);
}

SDL_GPUGraphicsPipeline *SdlGpuRenderer::pipeline(const gfx::DrawState &state) {
    const uint32_t key = packState(state) | uint32_t(_stencilMode) << 14 | uint32_t(_mirrored) << 17 |
                         uint32_t(_stencilWhole) << 18 | uint32_t(_passSamples) << 19;
    if (auto it = _pipelines.find(key); it != _pipelines.end())
        return it->second;

    // The mesh twice: its vertices, and (slot 1) the ones it's blended
    // towards, another animation frame's.
    SDL_GPUVertexBufferDescription buffers[2] = {};
    for (uint32_t i = 0; i < 2; ++i) {
        buffers[i].slot = i;
        buffers[i].pitch = sizeof(gfx::Vertex);
        buffers[i].input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    }
    SDL_GPUVertexAttribute attrs[7] = {};
    attrs[0] = {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, uint32_t(offsetof(gfx::Vertex, pos))};
    attrs[1] = {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, uint32_t(offsetof(gfx::Vertex, uv))};
    attrs[2] = {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, uint32_t(offsetof(gfx::Vertex, uv2))};
    attrs[3] = {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, uint32_t(offsetof(gfx::Vertex, normal))};
    attrs[4] = {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, uint32_t(offsetof(gfx::Vertex, color))};
    attrs[5] = {5, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, uint32_t(offsetof(gfx::Vertex, pos))};
    attrs[6] = {6, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, uint32_t(offsetof(gfx::Vertex, normal))};

    SDL_GPUColorTargetDescription target{};
    target.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    const bool opaque = state.src == gfx::Blend::kOne && state.dst == gfx::Blend::kZero;
    target.blend_state.enable_blend = !opaque;
    target.blend_state.src_color_blendfactor = target.blend_state.src_alpha_blendfactor = blendFactor(state.src);
    target.blend_state.dst_color_blendfactor = target.blend_state.dst_alpha_blendfactor = blendFactor(state.dst);
    target.blend_state.color_blend_op = target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = _vertexShader;
    info.fragment_shader = _fragmentShader;
    info.vertex_input_state.vertex_buffer_descriptions = buffers;
    info.vertex_input_state.num_vertex_buffers = 2;
    info.vertex_input_state.vertex_attributes = attrs;
    info.vertex_input_state.num_vertex_attributes = 7;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = state.cull == gfx::Cull::kBack    ? SDL_GPU_CULLMODE_BACK
                                      : state.cull == gfx::Cull::kFront ? SDL_GPU_CULLMODE_FRONT
                                                                        : SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face = _mirrored ? SDL_GPU_FRONTFACE_CLOCKWISE : SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    if (state.depthBias) {
        // Towards the camera; depth runs 0 (near) to 1 (far).
        info.rasterizer_state.enable_depth_bias = true;
        info.rasterizer_state.depth_bias_constant_factor = -2.0f;
        info.rasterizer_state.depth_bias_slope_factor = -1.0f;
    }
    info.depth_stencil_state.enable_depth_test = state.depthTest;
    info.depth_stencil_state.enable_depth_write = state.depthWrite;
    info.depth_stencil_state.compare_op = state.depthEqual ? SDL_GPU_COMPAREOP_EQUAL : SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    if (_stencilMode != StencilMode::kNone) {
        // Mark: 1 wherever drawn (and the depth test passed); Inside:
        // drawn only where it's 1. Mirrors' and portals' marks are the
        // stencil's bit 1, the sky's bit 2: the reference has both, each
        // mode masks its own. With a reference set (setStencilReference),
        // Mark writes it whole and Inside compares the whole value.
        const bool mark = _stencilMode == StencilMode::kMark || _stencilMode == StencilMode::kSkyMark;
        const bool sky = _stencilMode == StencilMode::kSkyMark || _stencilMode == StencilMode::kSkyInside;
        SDL_GPUStencilOpState op{};
        op.fail_op = op.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
        op.pass_op = mark ? SDL_GPU_STENCILOP_REPLACE : SDL_GPU_STENCILOP_KEEP;
        op.compare_op = mark ? SDL_GPU_COMPAREOP_ALWAYS : SDL_GPU_COMPAREOP_EQUAL;
        info.depth_stencil_state.enable_stencil_test = true;
        info.depth_stencil_state.front_stencil_state = info.depth_stencil_state.back_stencil_state = op;
        info.depth_stencil_state.compare_mask = info.depth_stencil_state.write_mask = _stencilWhole ? 0xff
                                                                                      : sky         ? 2
                                                                                                    : 1;
    }
    info.multisample_state.sample_count = _passSamples;
    info.target_info.color_target_descriptions = &target;
    info.target_info.num_color_targets = 1;
    info.target_info.depth_stencil_format = _depthFormat;
    info.target_info.has_depth_stencil_target = true;
    SDL_GPUGraphicsPipeline *p = SDL_CreateGPUGraphicsPipeline(_device, &info);
    if (!p)
        SDL_Log("Cannot create a 3D pipeline: %s", SDL_GetError());
    _pipelines[key] = p; // a failure is remembered too, and those draws skipped
    return p;
}

void SdlGpuRenderer::beginFrame(const gfx::Mat4 &viewProj, const gfx::Vec3 &eye, Color clear) {
    _viewProj = viewProj;
    _eye = eye;
    _boundPipeline = nullptr;
    _maxDepth = 1;
    _viewport = {0, 0, float(_width), float(_height), 0, 1};
    _boundMesh = 0;
    // No more than kFramesInFlight frames on the GPU: each holds command
    // buffers, whose descriptor-set caches SDL keeps.
    waitAndRelease(_device, _frameFences[_frameIndex % kFramesInFlight]);
    _cmd = SDL_AcquireGPUCommandBuffer(_device);
    if (!_cmd)
        return;
    if (_wantSamples != _samples)
        makeMultisampleTargets();
    _stencilMode = StencilMode::kNone;
    _stencilRef = kStencilMarks;
    _stencilWhole = false;
    _mirrored = false;
    // No marks to start with.
    const SDL_FColor c = {clear.r / 255.0f, clear.g / 255.0f, clear.b / 255.0f, 1.0f};
    _pass = beginPass(_keepFrame ? nullptr : &c, false);
    if (_pass)
        SDL_SetGPUStencilReference(_pass, _stencilRef); // both kinds of mark, unless set
}

void SdlGpuRenderer::draw(const gfx::DrawCall &call) {
    if (!_pass || call.indexCount == 0)
        return;
    flush();
    auto mesh = _meshes.find(call.mesh);
    if (mesh == _meshes.end())
        return;
    // With the frame's alpha unused, blends of the source alpha go through
    // one pipeline, (one, one minus source alpha), the factors applied in
    // the fragment shader (blendOut). Games that alternate, say, blended
    // and added polygons (a race of Need for Speed III: about 1,500
    // switches a frame) then don't switch pipelines, each switch taking
    // SDL four new descriptor sets.
    gfx::DrawState state = call.state;
    // FragmentUniforms::blend
    float blendSrc = 0;
    float blendDst = 0;
    if (_frameAlphaUnused && !(state.src == gfx::Blend::kOne && state.dst == gfx::Blend::kZero)) {
        const int s = state.src == gfx::Blend::kZero       ? 0
                      : state.src == gfx::Blend::kOne      ? 1
                      : state.src == gfx::Blend::kSrcAlpha ? 2
                                                           : -1;
        const int d = state.dst == gfx::Blend::kZero               ? 1
                      : state.dst == gfx::Blend::kOne              ? 2
                      : state.dst == gfx::Blend::kOneMinusSrcAlpha ? 3
                                                                   : -1;
        if (s >= 0 && d >= 0) {
            state.src = gfx::Blend::kOne;
            state.dst = gfx::Blend::kOneMinusSrcAlpha;
            blendSrc = float(s);
            blendDst = float(d);
        }
    }
    SDL_GPUGraphicsPipeline *p = pipeline(state);
    if (!p)
        return;
    if (p != _boundPipeline) {
        SDL_BindGPUGraphicsPipeline(_pass, p);
        _boundPipeline = p;
    }
    // The frame's vertices and the old frame's, by where the bindings
    // start; without a blend both are the same.
    const int32_t oldOffset = call.backLerp != 0 ? call.oldVertexOffset : call.vertexOffset;
    if (call.mesh != _boundMesh || call.vertexOffset != _boundOffset || oldOffset != _boundOldOffset) {
        SDL_GPUBufferBinding vb[2] = {
            {mesh->second.vertices, uint32_t(call.vertexOffset) * uint32_t(sizeof(gfx::Vertex))},
            {mesh->second.vertices, uint32_t(oldOffset) * uint32_t(sizeof(gfx::Vertex))}};
        SDL_BindGPUVertexBuffers(_pass, 0, vb, 2);
        if (call.mesh != _boundMesh) {
            SDL_GPUBufferBinding ib{mesh->second.indices, 0};
            SDL_BindGPUIndexBuffer(_pass, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        }
        _boundMesh = call.mesh;
        _boundOffset = call.vertexOffset;
        _boundOldOffset = oldOffset;
    }
    auto tex = _textures.find(call.texture);
    if (tex == _textures.end())
        tex = _textures.find(_white);
    SDL_GPUTextureSamplerBinding binding{tex->second.texture, _samplers[tex->second.sampler]};
    SDL_BindGPUFragmentSamplers(_pass, 0, &binding, 1);

    VertexUniforms vu{};
    auto put = [](float *out, const auto &in) { std::copy(in.begin(), in.end(), out); };
    put(vu.viewProj, _viewProj);
    put(vu.model, call.model);
    put(vu.ambient, call.ambientLight);
    put(vu.directed, call.directedLight);
    put(vu.lightDir, call.lightDirection);
    put(vu.eye, _eye);
    vu.eye[3] = call.time;
    put(vu.preRow0, call.preRow0);
    put(vu.preRow1, call.preRow1);
    put(vu.postRow0, call.postRow0);
    put(vu.postRow1, call.postRow1);
    vu.turb[0] = call.turbAmplitude;
    vu.turb[1] = call.turbPhase;
    put(vu.texVec0, call.texVector0);
    put(vu.texVec1, call.texVector1);
    put(vu.color, call.color);
    vu.mode[0] = float(call.texGen);
    vu.mode[1] = float(call.rgbSource);
    vu.mode[2] = float(call.alphaSource);
    vu.mode[3] = call.fogDistance;
    vu.mode2[0] = call.followCamera ? 1.0f : 0.0f;
    vu.mode2[1] = call.backLerp;
    vu.mode2[2] = call.fogDepth;
    vu.mode2[3] = call.tableFogFromDepth ? 1.0f : 0.0f;
    put(vu.fogPlane, call.fogPlane);
    for (size_t i = 0; i < call.deforms.size(); ++i) {
        const gfx::Deform &d = call.deforms[i];
        DeformUniforms &u = vu.deforms[i];
        u.a[0] = float(d.type);
        u.a[1] = float(d.wave.func);
        u.a[2] = d.wave.base;
        u.a[3] = d.wave.amplitude;
        u.b[0] = d.wave.phase;
        u.b[1] = d.wave.frequency;
        u.b[2] = d.spread;
        if (d.type == gfx::Deform::Type::kBulge) {
            u.c[0] = d.width;
            u.c[1] = d.height;
            u.c[2] = d.speed;
        } else {
            put(u.c, d.vector);
        }
    }
    SDL_PushGPUVertexUniformData(_cmd, 0, &vu, sizeof(vu));
    FragmentUniforms fu{};
    fu.params[0] = float(call.state.alphaTest);
    fu.params[3] = call.fogDepth > 0 ? 1.0f : 0.0f;
    fu.blend[0] = blendSrc;
    fu.blend[1] = blendDst;
    std::copy(call.tableFogColor.begin(), call.tableFogColor.end(), fu.fog);
    fu.fog[3] = call.tableFog;
    if (call.texture == _frameTexture) {
        fu.params[1] = 2; // the brightness pass
        fu.params[2] = 1.0f / _gamma;
    }
    if (call.dynamicLights) {
        fu.params[1] = 1;
        fu.lightCount[0] = float(_lights.size());
        for (size_t i = 0; i < _lights.size(); ++i) {
            std::copy(_lights[i].origin.begin(), _lights[i].origin.end(), fu.lightPos[i]);
            fu.lightPos[i][3] = _lights[i].radius;
            std::copy(_lights[i].color.begin(), _lights[i].color.end(), fu.lightColor[i]);
        }
    }
    SDL_PushGPUFragmentUniformData(_cmd, 0, &fu, sizeof(fu));

    SDL_DrawGPUIndexedPrimitives(_pass, call.indexCount, 1, call.firstIndex, 0, 0);
}

// Submits the frame's commands with a copy of `frame` into memory, waits
// for them and keeps it.
void SdlGpuRenderer::capture(SDL_GPUTexture *frame) {
    const uint32_t size = uint32_t(_width) * uint32_t(_height) * 4;
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    ti.size = size;
    SDL_GPUTransferBuffer *buffer = SDL_CreateGPUTransferBuffer(_device, &ti);
    if (!buffer) {
        SDL_SubmitGPUCommandBuffer(_cmd);
        return;
    }
    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(_cmd);
    SDL_GPUTextureRegion from{};
    from.texture = frame;
    from.w = uint32_t(_width);
    from.h = uint32_t(_height);
    from.d = 1;
    SDL_GPUTextureTransferInfo to{};
    to.transfer_buffer = buffer;
    SDL_DownloadFromGPUTexture(copy, &from, &to);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(_cmd);
    if (fence) {
        SDL_WaitForGPUFences(_device, true, &fence, 1);
        SDL_ReleaseGPUFence(_device, fence);
        if (const auto *pixels = static_cast<const uint8_t *>(SDL_MapGPUTransferBuffer(_device, buffer, false))) {
            _capture.assign(pixels, pixels + size);
            SDL_UnmapGPUTransferBuffer(_device, buffer);
        }
    }
    SDL_ReleaseGPUTransferBuffer(_device, buffer);
}

void SdlGpuRenderer::endFrame(const Surface *overlay) {
    flush();
    _stencilMode = StencilMode::kNone; // the brightness pass draws everything
    _mirrored = false;
    if (_pass) {
        SDL_EndGPURenderPass(_pass);
        _pass = nullptr;
    }
    // Brightness: the frame drawn into the second one through its curve.
    const bool brighten = _cmd && _gamma != 1;
    if (brighten) {
        SDL_GPUColorTargetInfo color{};
        color.texture = _gammaFrame;
        color.load_op = SDL_GPU_LOADOP_DONT_CARE;
        color.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPUDepthStencilTargetInfo depth{};
        depth.texture = _depth;
        depth.load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
        depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        _pass = SDL_BeginGPURenderPass(_cmd, &color, 1, &depth);
        _passSamples = SDL_GPU_SAMPLECOUNT_1;
        _boundPipeline = nullptr;
        _boundMesh = 0;
        _viewProj = gfx::identity();
        gfx::DrawCall call;
        call.mesh = _screenQuad;
        call.indexCount = 6;
        call.texture = _frameTexture;
        call.state.depthTest = call.state.depthWrite = false;
        call.state.cull = gfx::Cull::kNone;
        draw(call);
        SDL_EndGPURenderPass(_pass);
        _pass = nullptr;
    }
    // Submitted before the renderer's own commands, which then sample it.
    if (_cmd && _captureWanted)
        capture(brighten ? _gammaFrame : _color); // waits for it
    else if (_cmd) {
        SDL_GPUFence *&fence = _frameFences[_frameIndex % kFramesInFlight];
        waitAndRelease(_device, fence); // (none: beginFrame took it)
        fence = SDL_SubmitGPUCommandBufferAndAcquireFence(_cmd);
    }
    ++_frameIndex;
    _cmd = nullptr;
    _captureWanted = false;
    SDL_SetRenderDrawColor(_renderer, 0, 0, 0, 255);
    SDL_RenderClear(_renderer);
    SDL_RenderTexture(_renderer, brighten ? _gammaView : _colorView, nullptr, nullptr);
    if (overlay && overlay->w == _width && overlay->h == _height && overlay->format == PixelFormat::kRGBA32) {
        SDL_UpdateTexture(_overlay, nullptr, overlay->pixels.data(), overlay->pitch);
        SDL_RenderTexture(_renderer, _overlay, nullptr, nullptr);
    }
    if (_beforePresent)
        _beforePresent();
    SDL_RenderPresent(_renderer);
}

} // namespace reluva
