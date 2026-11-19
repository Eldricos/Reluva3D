// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "backends/graphics/sdl3/sdl_compute.h"

#include <SDL3/SDL.h>

#include <cstring>
#include <deque>
#include <unordered_map>

namespace reluva {

namespace {

class SdlCompute : public gfx::Compute {
public:
    explicit SdlCompute(SDL_GPUDevice *device) : _device(device) {}
    ~SdlCompute() override {
        if (_cmd)
            SDL_SubmitGPUCommandBuffer(_cmd);
        SDL_WaitForGPUIdle(_device);
        releaseFences();
        for (auto &[id, b] : _buffers)
            SDL_ReleaseGPUBuffer(_device, b);
        for (auto &[id, k] : _kernels)
            SDL_ReleaseGPUComputePipeline(_device, k);
        SDL_DestroyGPUDevice(_device);
    }

    std::string deviceName() const override {
        const char *driver = SDL_GetGPUDeviceDriver(_device);
        std::string name = driver ? driver : "?";
        const SDL_PropertiesID props = SDL_GetGPUDeviceProperties(_device);
        if (const char *n = SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_NAME_STRING, nullptr))
            name += std::string(", ") + n;
        return name;
    }

    Error createBuffer(uint32_t bytes, gfx::ComputeBuffer &out) override {
        SDL_GPUBufferCreateInfo info{};
        info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
        info.size = bytes;
        SDL_GPUBuffer *b = SDL_CreateGPUBuffer(_device, &info);
        if (!b)
            return Error::fail(std::string("Cannot create a GPU buffer: ") + SDL_GetError());
        out = _next++;
        _buffers[out] = b;
        return {};
    }

    void destroyBuffer(gfx::ComputeBuffer id) override {
        auto it = _buffers.find(id);
        if (it == _buffers.end())
            return;
        SDL_ReleaseGPUBuffer(_device, it->second); // released once the GPU is done with it
        _buffers.erase(it);
    }

    Error createKernel(const gfx::ComputeKernelCode &code, gfx::ComputeKernel &out) override {
        SDL_GPUComputePipelineCreateInfo info{};
        const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(_device);
        if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {
            info.format = SDL_GPU_SHADERFORMAT_SPIRV;
            info.code = code.spirv;
            info.code_size = code.spirvSize;
            info.entrypoint = "main";
        } else if ((formats & SDL_GPU_SHADERFORMAT_MSL) && code.msl) {
            info.format = SDL_GPU_SHADERFORMAT_MSL;
            info.code = reinterpret_cast<const Uint8 *>(code.msl);
            info.code_size = std::strlen(code.msl);
            info.entrypoint = "main0";
        } else {
            return Error::fail("The GPU takes no shader format Reluva has");
        }
        info.num_readonly_storage_buffers = code.readOnlyBuffers;
        info.num_readwrite_storage_buffers = code.readWriteBuffers;
        info.num_uniform_buffers = code.uniformBuffers;
        info.threadcount_x = code.threadsX;
        info.threadcount_y = code.threadsY;
        info.threadcount_z = code.threadsZ;
        SDL_GPUComputePipeline *p = SDL_CreateGPUComputePipeline(_device, &info);
        if (!p)
            return Error::fail(std::string("Cannot create a compute kernel: ") + SDL_GetError());
        out = _next++;
        _kernels[out] = p;
        return {};
    }

    void upload(gfx::ComputeBuffer id, uint32_t offset, const void *data, uint32_t bytes, bool replaceAll) override {
        auto it = _buffers.find(id);
        if (it == _buffers.end() || !bytes)
            return;
        SDL_GPUTransferBufferCreateInfo tinfo{};
        tinfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tinfo.size = bytes;
        SDL_GPUTransferBuffer *t = SDL_CreateGPUTransferBuffer(_device, &tinfo);
        if (!t)
            return;
        if (void *p = SDL_MapGPUTransferBuffer(_device, t, false)) {
            std::memcpy(p, data, bytes);
            SDL_UnmapGPUTransferBuffer(_device, t);
            SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmd());
            SDL_GPUTransferBufferLocation from{t, 0};
            SDL_GPUBufferRegion to{it->second, offset, bytes};
            SDL_UploadToGPUBuffer(pass, &from, &to, replaceAll);
            SDL_EndGPUCopyPass(pass);
        }
        SDL_ReleaseGPUTransferBuffer(_device, t);
    }

    void dispatch(gfx::ComputeKernel kernel, const std::vector<gfx::ComputeBuffer> &readOnly,
                  const std::vector<gfx::ComputeBuffer> &readWrite, const void *uniforms, uint32_t uniformBytes,
                  uint32_t gx, uint32_t gy, uint32_t gz) override {
        auto k = _kernels.find(kernel);
        if (k == _kernels.end() || !gx || !gy || !gz)
            return;
        std::vector<SDL_GPUStorageBufferReadWriteBinding> rw(readWrite.size());
        for (size_t i = 0; i < readWrite.size(); ++i) {
            rw[i] = {};
            rw[i].buffer = _buffers[readWrite[i]];
            rw[i].cycle = false;
        }
        std::vector<SDL_GPUBuffer *> ro;
        for (gfx::ComputeBuffer b : readOnly)
            ro.push_back(_buffers[b]);
        SDL_GPUCommandBuffer *c = cmd();
        if (uniforms && uniformBytes)
            SDL_PushGPUComputeUniformData(c, 0, uniforms, uniformBytes);
        SDL_GPUComputePass *pass = SDL_BeginGPUComputePass(c, nullptr, 0, rw.data(), uint32_t(rw.size()));
        SDL_BindGPUComputePipeline(pass, k->second);
        if (!ro.empty())
            SDL_BindGPUComputeStorageBuffers(pass, 0, ro.data(), uint32_t(ro.size()));
        SDL_DispatchGPUCompute(pass, gx, gy, gz);
        SDL_EndGPUComputePass(pass);
    }

    Error download(gfx::ComputeBuffer id, uint32_t offset, void *out, uint32_t bytes) override {
        auto it = _buffers.find(id);
        if (it == _buffers.end())
            return Error::fail("No such GPU buffer");
        SDL_GPUTransferBufferCreateInfo tinfo{};
        tinfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        tinfo.size = bytes;
        SDL_GPUTransferBuffer *t = SDL_CreateGPUTransferBuffer(_device, &tinfo);
        if (!t)
            return Error::fail(std::string("Cannot read back from the GPU: ") + SDL_GetError());
        SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmd());
        SDL_GPUBufferRegion from{it->second, offset, bytes};
        SDL_GPUTransferBufferLocation to{t, 0};
        SDL_DownloadFromGPUBuffer(pass, &from, &to);
        SDL_EndGPUCopyPass(pass);
        SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(_cmd);
        _cmd = nullptr;
        Error err;
        if (!fence || !SDL_WaitForGPUFences(_device, true, &fence, 1)) {
            err = Error::fail(std::string("The GPU's work failed: ") + SDL_GetError());
        } else if (const void *p = SDL_MapGPUTransferBuffer(_device, t, false)) {
            std::memcpy(out, p, bytes);
            SDL_UnmapGPUTransferBuffer(_device, t);
        }
        if (fence)
            SDL_ReleaseGPUFence(_device, fence);
        SDL_ReleaseGPUTransferBuffer(_device, t);
        releaseFences(); // sent before, so done too
        return err;
    }

    void flush() override {
        if (!_cmd)
            return;
        // At most kInFlight sent and not done: SDL's Vulkan backend keeps a
        // descriptor-set cache for every command buffer in flight at once,
        // for good (see SdlGpuRenderer::copyPass), and a batch each, with
        // no wait until the frame's read-back, made hundreds.
        if (_inFlight.size() >= kInFlight) {
            SDL_GPUFence *oldest = _inFlight.front();
            _inFlight.pop_front();
            SDL_WaitForGPUFences(_device, true, &oldest, 1);
            SDL_ReleaseGPUFence(_device, oldest);
        }
        if (SDL_GPUFence *fence = SDL_SubmitGPUCommandBufferAndAcquireFence(_cmd))
            _inFlight.push_back(fence);
        _cmd = nullptr;
    }

private:
    static constexpr size_t kInFlight = 3;

    void releaseFences() {
        for (SDL_GPUFence *f : _inFlight)
            SDL_ReleaseGPUFence(_device, f);
        _inFlight.clear();
    }

    SDL_GPUCommandBuffer *cmd() {
        if (!_cmd)
            _cmd = SDL_AcquireGPUCommandBuffer(_device);
        return _cmd;
    }

    SDL_GPUDevice *_device;
    SDL_GPUCommandBuffer *_cmd = nullptr;
    std::deque<SDL_GPUFence *> _inFlight; // flushed, oldest first
    uint32_t _next = 1;
    std::unordered_map<uint32_t, SDL_GPUBuffer *> _buffers;
    std::unordered_map<uint32_t, SDL_GPUComputePipeline *> _kernels;
};

} // namespace

std::unique_ptr<gfx::Compute> makeSdlCompute(std::string &error) {
    // SDL's GPU devices need its video subsystem; without a display (a
    // tool, a test) its offscreen driver does.
    if (!SDL_WasInit(SDL_INIT_VIDEO) && !SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            error = std::string("no video subsystem: ") + SDL_GetError();
            return nullptr;
        }
    }
    SDL_GPUDevice *device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
    if (!device) {
        error = std::string("no GPU device: ") + SDL_GetError();
        return nullptr;
    }
    return std::make_unique<SdlCompute>(device);
}

} // namespace reluva
