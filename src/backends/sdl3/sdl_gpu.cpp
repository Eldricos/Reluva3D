// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "backends/sdl3/sdl_gpu.h"
#include "backends/sdl3/shaders/compiled.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string>

namespace reluva {

namespace {

Error sdlError(const char* what) { return Error::fail(std::string(what) + ": " + SDL_GetError()); }

// The layouts of draw.vert's and draw.frag's uniform blocks (std140).
struct DeformUniforms {
    float a[4], b[4], c[4];
};
struct VertexUniforms {
    float viewProj[16];
    float model[16];
    float eye[4];
    float preRow0[4], preRow1[4], postRow0[4], postRow1[4];
    float turb[4];
    float texVec0[4], texVec1[4];
    float color[4];
    float mode[4];
    float mode2[4];
    float ambient[4], directed[4], lightDir[4];
    DeformUniforms deforms[2];
};
struct FragmentUniforms {
    float params[4];
    float lightCount[4];
    float lightPos[gfx::kMaxDynamicLights][4]; // xyz, radius
    float lightColor[gfx::kMaxDynamicLights][4];
};

SDL_GPUBlendFactor blendFactor(gfx::Blend b) {
    switch (b) {
    case gfx::Blend::Zero: return SDL_GPU_BLENDFACTOR_ZERO;
    case gfx::Blend::One: return SDL_GPU_BLENDFACTOR_ONE;
    case gfx::Blend::SrcColor: return SDL_GPU_BLENDFACTOR_SRC_COLOR;
    case gfx::Blend::OneMinusSrcColor: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
    case gfx::Blend::DstColor: return SDL_GPU_BLENDFACTOR_DST_COLOR;
    case gfx::Blend::OneMinusDstColor: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
    case gfx::Blend::SrcAlpha: return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    case gfx::Blend::OneMinusSrcAlpha: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    case gfx::Blend::DstAlpha: return SDL_GPU_BLENDFACTOR_DST_ALPHA;
    case gfx::Blend::OneMinusDstAlpha: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
    }
    return SDL_GPU_BLENDFACTOR_ONE;
}

uint32_t packState(const gfx::DrawState& s) {
    return uint32_t(s.src) | uint32_t(s.dst) << 4 | uint32_t(s.depthTest) << 8 | uint32_t(s.depthWrite) << 9 |
           uint32_t(s.cull) << 10 | uint32_t(s.depthEqual) << 12 | uint32_t(s.depthBias) << 13;
}

} // namespace

SdlGpuRenderer::~SdlGpuRenderer() {
    if (cmd_) {
        if (pass_)
            SDL_EndGPURenderPass(pass_);
        SDL_SubmitGPUCommandBuffer(cmd_);
    }
    // Nothing may still be using the resources.
    SDL_WaitForGPUIdle(device_);
    for (auto& [id, t] : textures_)
        SDL_ReleaseGPUTexture(device_, t.texture);
    for (auto& [id, m] : meshes_) {
        SDL_ReleaseGPUBuffer(device_, m.vertices);
        SDL_ReleaseGPUBuffer(device_, m.indices);
    }
    for (auto& [key, p] : pipelines_)
        SDL_ReleaseGPUGraphicsPipeline(device_, p);
    for (SDL_GPUSampler* s : samplers_)
        if (s)
            SDL_ReleaseGPUSampler(device_, s);
    if (vertexShader_)
        SDL_ReleaseGPUShader(device_, vertexShader_);
    if (fragmentShader_)
        SDL_ReleaseGPUShader(device_, fragmentShader_);
    if (overlay_)
        SDL_DestroyTexture(overlay_);
    if (colorView_)
        SDL_DestroyTexture(colorView_);
    if (color_)
        SDL_ReleaseGPUTexture(device_, color_);
    if (depth_)
        SDL_ReleaseGPUTexture(device_, depth_);
}

Error SdlGpuRenderer::init(int width, int height) {
    width_ = width;
    height_ = height;

    // Shaders, in whichever format the device takes.
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device_);
    auto makeShader = [&](SDL_GPUShaderStage stage, const uint8_t* spirv, size_t spirvSize, const char* msl,
                          uint32_t samplers) -> SDL_GPUShader* {
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
            info.code = reinterpret_cast<const uint8_t*>(msl);
            info.code_size = std::strlen(msl);
            info.entrypoint = "main0";
        } else {
            return nullptr;
        }
        return SDL_CreateGPUShader(device_, &info);
    };
    vertexShader_ = makeShader(SDL_GPU_SHADERSTAGE_VERTEX, shaders::kDrawVertexSpirv, sizeof(shaders::kDrawVertexSpirv),
                               shaders::kDrawVertexMsl, 0);
    fragmentShader_ = makeShader(SDL_GPU_SHADERSTAGE_FRAGMENT, shaders::kDrawFragmentSpirv,
                                 sizeof(shaders::kDrawFragmentSpirv), shaders::kDrawFragmentMsl, 1);
    if (!vertexShader_ || !fragmentShader_)
        return sdlError("Cannot create the 3D shaders");

    for (int i = 0; i < 8; ++i) {
        const bool clamp = i & 1, mipmaps = i & 2, nearest = i & 4;
        SDL_GPUSamplerCreateInfo info{};
        info.min_filter = info.mag_filter = nearest ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
        info.mipmap_mode = nearest ? SDL_GPU_SAMPLERMIPMAPMODE_NEAREST : SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        info.address_mode_u = info.address_mode_v = info.address_mode_w =
            clamp ? SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE : SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        info.max_lod = mipmaps ? 1000.0f : 0.0f;
        samplers_[i] = SDL_CreateGPUSampler(device_, &info);
        if (!samplers_[i])
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
    color_ = SDL_CreateGPUTexture(device_, &ci);
    if (!color_)
        return sdlError("Cannot create the 3D frame");
    for (SDL_GPUTextureFormat f : {SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTUREFORMAT_D24_UNORM}) {
        if (SDL_GPUTextureSupportsFormat(device_, f, SDL_GPU_TEXTURETYPE_2D,
                                         SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
            depthFormat_ = f;
            break;
        }
    }
    ci.format = depthFormat_;
    ci.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    depth_ = SDL_CreateGPUTexture(device_, &ci);
    if (!depth_)
        return sdlError("Cannot create the depth buffer");

    // The renderer shows the frame through a texture wrapping it.
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGBA32);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STATIC);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, width);
    SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, height);
    SDL_SetPointerProperty(props, SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER, color_);
    colorView_ = SDL_CreateTextureWithProperties(renderer_, props);
    SDL_DestroyProperties(props);
    if (!colorView_)
        return sdlError("Cannot show the 3D frame");
    SDL_SetTextureBlendMode(colorView_, SDL_BLENDMODE_NONE);

    overlay_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!overlay_)
        return sdlError("Cannot create the overlay");
    SDL_SetTextureBlendMode(overlay_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(overlay_, SDL_SCALEMODE_NEAREST);

    const uint8_t white[4] = {255, 255, 255, 255};
    gfx::TextureParams wp;
    wp.mipmaps = false;
    return createTexture(1, 1, white, wp, white_);
}

bool SdlGpuRenderer::upload(const void* data, uint32_t size, SDL_GPUBuffer* buffer) {
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = size;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device_, &ti);
    if (!tb)
        return false;
    void* mapped = SDL_MapGPUTransferBuffer(device_, tb, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(device_, tb);
        return false;
    }
    std::memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(device_, tb);
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src{};
    src.transfer_buffer = tb;
    SDL_GPUBufferRegion dst{};
    dst.buffer = buffer;
    dst.size = size;
    SDL_UploadToGPUBuffer(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    const bool ok = SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device_, tb);
    return ok;
}

Error SdlGpuRenderer::createTexture(int width, int height, const uint8_t* rgba, const gfx::TextureParams& params,
                                    gfx::TextureId& out) {
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
    // Generating mipmaps renders into the texture.
    ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | (levels > 1 ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET : 0);
    ci.width = uint32_t(width);
    ci.height = uint32_t(height);
    ci.layer_count_or_depth = 1;
    ci.num_levels = uint32_t(levels);
    SDL_GPUTexture* tex = SDL_CreateGPUTexture(device_, &ci);
    if (!tex)
        return sdlError("Cannot create a texture");

    const uint32_t size = uint32_t(width) * uint32_t(height) * 4;
    SDL_GPUTransferBufferCreateInfo ti{};
    ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    ti.size = size;
    SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(device_, &ti);
    void* mapped = tb ? SDL_MapGPUTransferBuffer(device_, tb, false) : nullptr;
    if (!mapped) {
        if (tb)
            SDL_ReleaseGPUTransferBuffer(device_, tb);
        SDL_ReleaseGPUTexture(device_, tex);
        return sdlError("Cannot upload a texture");
    }
    std::memcpy(mapped, rgba, size);
    SDL_UnmapGPUTransferBuffer(device_, tb);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src{};
    src.transfer_buffer = tb;
    SDL_GPUTextureRegion dst{};
    dst.texture = tex;
    dst.w = uint32_t(width);
    dst.h = uint32_t(height);
    dst.d = 1;
    SDL_UploadToGPUTexture(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    if (levels > 1)
        SDL_GenerateMipmapsForGPUTexture(cmd, tex);
    SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(device_, tb);

    out = nextId_++;
    textures_[out] = {tex, int(params.clamp) | int(levels > 1) << 1 | int(params.nearest) << 2};
    return Error::success();
}

std::string SdlGpuRenderer::deviceName() const {
    const SDL_PropertiesID props = SDL_GetGPUDeviceProperties(device_);
    std::string name = SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_NAME_STRING, "unknown GPU");
    name += std::string(" (") + SDL_GetGPUDeviceDriver(device_);
    if (const char* driver = SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING, nullptr))
        name += std::string(", ") + driver;
    if (const char* version = SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING, nullptr))
        name += std::string(" ") + version;
    return name + ")";
}

void SdlGpuRenderer::destroyTexture(gfx::TextureId id) {
    auto it = textures_.find(id);
    if (it == textures_.end() || id == white_)
        return;
    SDL_ReleaseGPUTexture(device_, it->second.texture); // SDL defers it until the GPU is done
    textures_.erase(it);
}

Error SdlGpuRenderer::createMesh(const std::vector<gfx::Vertex>& vertices, const std::vector<uint32_t>& indices,
                                 gfx::MeshId& out) {
    out = 0;
    static_assert(sizeof(gfx::Vertex) == 44, "the pipeline's vertex layout");
    if (vertices.empty() || indices.empty())
        return Error::fail("Empty mesh");
    Mesh m;
    SDL_GPUBufferCreateInfo bi{};
    bi.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    bi.size = uint32_t(vertices.size() * sizeof(gfx::Vertex));
    m.vertices = SDL_CreateGPUBuffer(device_, &bi);
    bi.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    bi.size = uint32_t(indices.size() * sizeof(uint32_t));
    m.indices = SDL_CreateGPUBuffer(device_, &bi);
    if (!m.vertices || !m.indices ||
        !upload(vertices.data(), uint32_t(vertices.size() * sizeof(gfx::Vertex)), m.vertices) ||
        !upload(indices.data(), uint32_t(indices.size() * sizeof(uint32_t)), m.indices)) {
        Error err = sdlError("Cannot create a mesh");
        if (m.vertices)
            SDL_ReleaseGPUBuffer(device_, m.vertices);
        if (m.indices)
            SDL_ReleaseGPUBuffer(device_, m.indices);
        return err;
    }
    out = nextId_++;
    meshes_[out] = m;
    return Error::success();
}

void SdlGpuRenderer::destroyMesh(gfx::MeshId id) {
    auto it = meshes_.find(id);
    if (it == meshes_.end())
        return;
    SDL_ReleaseGPUBuffer(device_, it->second.vertices);
    SDL_ReleaseGPUBuffer(device_, it->second.indices);
    meshes_.erase(it);
}

SDL_GPUGraphicsPipeline* SdlGpuRenderer::pipeline(const gfx::DrawState& state) {
    const uint32_t key = packState(state);
    if (auto it = pipelines_.find(key); it != pipelines_.end())
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
    const bool opaque = state.src == gfx::Blend::One && state.dst == gfx::Blend::Zero;
    target.blend_state.enable_blend = !opaque;
    target.blend_state.src_color_blendfactor = target.blend_state.src_alpha_blendfactor = blendFactor(state.src);
    target.blend_state.dst_color_blendfactor = target.blend_state.dst_alpha_blendfactor = blendFactor(state.dst);
    target.blend_state.color_blend_op = target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertexShader_;
    info.fragment_shader = fragmentShader_;
    info.vertex_input_state.vertex_buffer_descriptions = buffers;
    info.vertex_input_state.num_vertex_buffers = 2;
    info.vertex_input_state.vertex_attributes = attrs;
    info.vertex_input_state.num_vertex_attributes = 7;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = state.cull == gfx::Cull::Back    ? SDL_GPU_CULLMODE_BACK
                                      : state.cull == gfx::Cull::Front ? SDL_GPU_CULLMODE_FRONT
                                                                       : SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    if (state.depthBias) {
        // Towards the camera; depth runs 0 (near) to 1 (far).
        info.rasterizer_state.enable_depth_bias = true;
        info.rasterizer_state.depth_bias_constant_factor = -2.0f;
        info.rasterizer_state.depth_bias_slope_factor = -1.0f;
    }
    info.depth_stencil_state.enable_depth_test = state.depthTest;
    info.depth_stencil_state.enable_depth_write = state.depthWrite;
    info.depth_stencil_state.compare_op = state.depthEqual ? SDL_GPU_COMPAREOP_EQUAL : SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    info.target_info.color_target_descriptions = &target;
    info.target_info.num_color_targets = 1;
    info.target_info.depth_stencil_format = depthFormat_;
    info.target_info.has_depth_stencil_target = true;
    SDL_GPUGraphicsPipeline* p = SDL_CreateGPUGraphicsPipeline(device_, &info);
    if (!p)
        SDL_Log("Cannot create a 3D pipeline: %s", SDL_GetError());
    pipelines_[key] = p; // a failure is remembered too, and those draws skipped
    return p;
}

void SdlGpuRenderer::beginFrame(const gfx::Mat4& viewProj, const gfx::Vec3& eye, Color clear) {
    viewProj_ = viewProj;
    eye_ = eye;
    boundPipeline_ = nullptr;
    boundMesh_ = 0;
    cmd_ = SDL_AcquireGPUCommandBuffer(device_);
    if (!cmd_)
        return;
    SDL_GPUColorTargetInfo color{};
    color.texture = color_;
    color.clear_color = {clear.r / 255.0f, clear.g / 255.0f, clear.b / 255.0f, 1.0f};
    color.load_op = SDL_GPU_LOADOP_CLEAR;
    color.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo depth{};
    depth.texture = depth_;
    depth.clear_depth = 1.0f;
    depth.load_op = SDL_GPU_LOADOP_CLEAR;
    depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    pass_ = SDL_BeginGPURenderPass(cmd_, &color, 1, &depth);
}

void SdlGpuRenderer::draw(const gfx::DrawCall& call) {
    if (!pass_ || call.indexCount == 0)
        return;
    auto mesh = meshes_.find(call.mesh);
    if (mesh == meshes_.end())
        return;
    SDL_GPUGraphicsPipeline* p = pipeline(call.state);
    if (!p)
        return;
    if (p != boundPipeline_) {
        SDL_BindGPUGraphicsPipeline(pass_, p);
        boundPipeline_ = p;
    }
    // The frame's vertices and the old frame's, by where the bindings
    // start; without a blend both are the same.
    const int32_t oldOffset = call.backLerp != 0 ? call.oldVertexOffset : call.vertexOffset;
    if (call.mesh != boundMesh_ || call.vertexOffset != boundOffset_ || oldOffset != boundOldOffset_) {
        SDL_GPUBufferBinding vb[2] = {
            {mesh->second.vertices, uint32_t(call.vertexOffset) * uint32_t(sizeof(gfx::Vertex))},
            {mesh->second.vertices, uint32_t(oldOffset) * uint32_t(sizeof(gfx::Vertex))}};
        SDL_BindGPUVertexBuffers(pass_, 0, vb, 2);
        if (call.mesh != boundMesh_) {
            SDL_GPUBufferBinding ib{mesh->second.indices, 0};
            SDL_BindGPUIndexBuffer(pass_, &ib, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        }
        boundMesh_ = call.mesh;
        boundOffset_ = call.vertexOffset;
        boundOldOffset_ = oldOffset;
    }
    auto tex = textures_.find(call.texture);
    if (tex == textures_.end())
        tex = textures_.find(white_);
    SDL_GPUTextureSamplerBinding binding{tex->second.texture, samplers_[tex->second.sampler]};
    SDL_BindGPUFragmentSamplers(pass_, 0, &binding, 1);

    VertexUniforms vu{};
    auto put = [](float* out, const auto& in) { std::copy(in.begin(), in.end(), out); };
    put(vu.viewProj, viewProj_);
    put(vu.model, call.model);
    put(vu.ambient, call.ambientLight);
    put(vu.directed, call.directedLight);
    put(vu.lightDir, call.lightDirection);
    put(vu.eye, eye_);
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
    for (size_t i = 0; i < call.deforms.size(); ++i) {
        const gfx::Deform& d = call.deforms[i];
        DeformUniforms& u = vu.deforms[i];
        u.a[0] = float(d.type);
        u.a[1] = float(d.wave.func);
        u.a[2] = d.wave.base;
        u.a[3] = d.wave.amplitude;
        u.b[0] = d.wave.phase;
        u.b[1] = d.wave.frequency;
        u.b[2] = d.spread;
        if (d.type == gfx::Deform::Type::Bulge) {
            u.c[0] = d.width;
            u.c[1] = d.height;
            u.c[2] = d.speed;
        } else {
            put(u.c, d.vector);
        }
    }
    SDL_PushGPUVertexUniformData(cmd_, 0, &vu, sizeof(vu));
    FragmentUniforms fu{};
    fu.params[0] = float(call.state.alphaTest);
    if (call.dynamicLights) {
        fu.params[1] = 1;
        fu.lightCount[0] = float(lights_.size());
        for (size_t i = 0; i < lights_.size(); ++i) {
            std::copy(lights_[i].origin.begin(), lights_[i].origin.end(), fu.lightPos[i]);
            fu.lightPos[i][3] = lights_[i].radius;
            std::copy(lights_[i].color.begin(), lights_[i].color.end(), fu.lightColor[i]);
        }
    }
    SDL_PushGPUFragmentUniformData(cmd_, 0, &fu, sizeof(fu));

    SDL_DrawGPUIndexedPrimitives(pass_, call.indexCount, 1, call.firstIndex, 0, 0);
}

void SdlGpuRenderer::endFrame(const Surface* overlay) {
    if (pass_) {
        SDL_EndGPURenderPass(pass_);
        pass_ = nullptr;
    }
    // Submitted before the renderer's own commands, which then sample it.
    if (cmd_) {
        SDL_SubmitGPUCommandBuffer(cmd_);
        cmd_ = nullptr;
    }
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    SDL_RenderTexture(renderer_, colorView_, nullptr, nullptr);
    if (overlay && overlay->w == width_ && overlay->h == height_ && overlay->format == PixelFormat::RGBA32) {
        SDL_UpdateTexture(overlay_, nullptr, overlay->pixels.data(), overlay->pitch);
        SDL_RenderTexture(renderer_, overlay_, nullptr, nullptr);
    }
    SDL_RenderPresent(renderer_);
}

} // namespace reluva
