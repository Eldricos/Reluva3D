// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "backends/sdl3/sdl_compute.h"

#include <SDL3/SDL.h>

#include <cstring>
#include <unordered_map>

namespace reluva {

namespace {

class SdlCompute : public gfx::Compute {
public:
    explicit SdlCompute(SDL_GPUDevice* device) : device_(device) {}
    ~SdlCompute() override {
        if (cmd_)
            SDL_SubmitGPUCommandBuffer(cmd_);
        SDL_WaitForGPUIdle(device_);
        for (auto& [id, b] : buffers_)
            SDL_ReleaseGPUBuffer(device_, b);
        for (auto& [id, k] : kernels_)
            SDL_ReleaseGPUComputePipeline(device_, k);
        SDL_DestroyGPUDevice(device_);
    }

    std::string deviceName() const override {
        const char* driver = SDL_GetGPUDeviceDriver(device_);
        std::string name = driver ? driver : "?";
        const SDL_PropertiesID props = SDL_GetGPUDeviceProperties(device_);
        if (const char* n = SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_NAME_STRING, nullptr))
            name += std::string(", ") + n;
        return name;
    }

    Error createBuffer(uint32_t bytes, gfx::ComputeBuffer& out) override {
        SDL_GPUBufferCreateInfo info{};
        info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
        info.size = bytes;
        SDL_GPUBuffer* b = SDL_CreateGPUBuffer(device_, &info);
        if (!b)
            return Error::fail(std::string("Cannot create a GPU buffer: ") + SDL_GetError());
        out = next_++;
        buffers_[out] = b;
        return {};
    }

    void destroyBuffer(gfx::ComputeBuffer id) override {
        auto it = buffers_.find(id);
        if (it == buffers_.end())
            return;
        SDL_ReleaseGPUBuffer(device_, it->second); // released once the GPU is done with it
        buffers_.erase(it);
    }

    Error createKernel(const gfx::ComputeKernelCode& code, gfx::ComputeKernel& out) override {
        SDL_GPUComputePipelineCreateInfo info{};
        const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device_);
        if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {
            info.format = SDL_GPU_SHADERFORMAT_SPIRV;
            info.code = code.spirv;
            info.code_size = code.spirvSize;
            info.entrypoint = "main";
        } else if ((formats & SDL_GPU_SHADERFORMAT_MSL) && code.msl) {
            info.format = SDL_GPU_SHADERFORMAT_MSL;
            info.code = reinterpret_cast<const Uint8*>(code.msl);
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
        SDL_GPUComputePipeline* p = SDL_CreateGPUComputePipeline(device_, &info);
        if (!p)
            return Error::fail(std::string("Cannot create a compute kernel: ") + SDL_GetError());
        out = next_++;
        kernels_[out] = p;
        return {};
    }

    void upload(gfx::ComputeBuffer id, uint32_t offset, const void* data, uint32_t bytes, bool replaceAll) override {
        auto it = buffers_.find(id);
        if (it == buffers_.end() || !bytes)
            return;
        SDL_GPUTransferBufferCreateInfo tinfo{};
        tinfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tinfo.size = bytes;
        SDL_GPUTransferBuffer* t = SDL_CreateGPUTransferBuffer(device_, &tinfo);
        if (!t)
            return;
        if (void* p = SDL_MapGPUTransferBuffer(device_, t, false)) {
            std::memcpy(p, data, bytes);
            SDL_UnmapGPUTransferBuffer(device_, t);
            SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd());
            SDL_GPUTransferBufferLocation from{t, 0};
            SDL_GPUBufferRegion to{it->second, offset, bytes};
            SDL_UploadToGPUBuffer(pass, &from, &to, replaceAll);
            SDL_EndGPUCopyPass(pass);
        }
        SDL_ReleaseGPUTransferBuffer(device_, t);
    }

    void dispatch(gfx::ComputeKernel kernel, const std::vector<gfx::ComputeBuffer>& readOnly,
                  const std::vector<gfx::ComputeBuffer>& readWrite, const void* uniforms, uint32_t uniformBytes,
                  uint32_t gx, uint32_t gy, uint32_t gz) override {
        auto k = kernels_.find(kernel);
        if (k == kernels_.end() || !gx || !gy || !gz)
            return;
        std::vector<SDL_GPUStorageBufferReadWriteBinding> rw(readWrite.size());
        for (size_t i = 0; i < readWrite.size(); ++i) {
            rw[i] = {};
            rw[i].buffer = buffers_[readWrite[i]];
            rw[i].cycle = false;
        }
        std::vector<SDL_GPUBuffer*> ro;
        for (gfx::ComputeBuffer b : readOnly)
            ro.push_back(buffers_[b]);
        SDL_GPUCommandBuffer* c = cmd();
        if (uniforms && uniformBytes)
            SDL_PushGPUComputeUniformData(c, 0, uniforms, uniformBytes);
        SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(c, nullptr, 0, rw.data(), uint32_t(rw.size()));
        SDL_BindGPUComputePipeline(pass, k->second);
        if (!ro.empty())
            SDL_BindGPUComputeStorageBuffers(pass, 0, ro.data(), uint32_t(ro.size()));
        SDL_DispatchGPUCompute(pass, gx, gy, gz);
        SDL_EndGPUComputePass(pass);
    }

    Error download(gfx::ComputeBuffer id, uint32_t offset, void* out, uint32_t bytes) override {
        auto it = buffers_.find(id);
        if (it == buffers_.end())
            return Error::fail("No such GPU buffer");
        SDL_GPUTransferBufferCreateInfo tinfo{};
        tinfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        tinfo.size = bytes;
        SDL_GPUTransferBuffer* t = SDL_CreateGPUTransferBuffer(device_, &tinfo);
        if (!t)
            return Error::fail(std::string("Cannot read back from the GPU: ") + SDL_GetError());
        SDL_GPUCopyPass* pass = SDL_BeginGPUCopyPass(cmd());
        SDL_GPUBufferRegion from{it->second, offset, bytes};
        SDL_GPUTransferBufferLocation to{t, 0};
        SDL_DownloadFromGPUBuffer(pass, &from, &to);
        SDL_EndGPUCopyPass(pass);
        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd_);
        cmd_ = nullptr;
        Error err;
        if (!fence || !SDL_WaitForGPUFences(device_, true, &fence, 1)) {
            err = Error::fail(std::string("The GPU's work failed: ") + SDL_GetError());
        } else if (const void* p = SDL_MapGPUTransferBuffer(device_, t, false)) {
            std::memcpy(out, p, bytes);
            SDL_UnmapGPUTransferBuffer(device_, t);
        }
        if (fence)
            SDL_ReleaseGPUFence(device_, fence);
        SDL_ReleaseGPUTransferBuffer(device_, t);
        return err;
    }

    void flush() override {
        if (cmd_) {
            SDL_SubmitGPUCommandBuffer(cmd_);
            cmd_ = nullptr;
        }
    }

private:
    SDL_GPUCommandBuffer* cmd() {
        if (!cmd_)
            cmd_ = SDL_AcquireGPUCommandBuffer(device_);
        return cmd_;
    }

    SDL_GPUDevice* device_;
    SDL_GPUCommandBuffer* cmd_ = nullptr;
    uint32_t next_ = 1;
    std::unordered_map<uint32_t, SDL_GPUBuffer*> buffers_;
    std::unordered_map<uint32_t, SDL_GPUComputePipeline*> kernels_;
};

} // namespace

std::unique_ptr<gfx::Compute> makeSdlCompute(std::string& error) {
    // SDL's GPU devices need its video subsystem; without a display (a
    // tool, a test) its offscreen driver does.
    if (!SDL_WasInit(SDL_INIT_VIDEO) && !SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            error = std::string("no video subsystem: ") + SDL_GetError();
            return nullptr;
        }
    }
    SDL_GPUDevice* device =
        SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
    if (!device) {
        error = std::string("no GPU device: ") + SDL_GetError();
        return nullptr;
    }
    return std::make_unique<SdlCompute>(device);
}

} // namespace reluva
