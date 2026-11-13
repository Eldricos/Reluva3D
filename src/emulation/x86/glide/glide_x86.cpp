// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "emulation/x86/glide/glide_x86.h"

#include "graphics/glide/glide_utils.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace reluva::glide {

using x86::Cpu;

namespace {

// GR_FOG_TABLE_SIZE entries' eye-space w (Reference Manual,
// guFogTableIndexToW).
double fogIndexToW(int i) {
    return std::pow(2.0, 3.0 + double(i >> 2)) / double(8 - (i & 3));
}

} // namespace

void GlideDll::addFogUtilities() {
    // The utility library's fog tables, 64 entries (Glide 2's
    // GR_FOG_TABLE_SIZE; the boards Glide 3 runs here have 64 too).
    add("guFogTableIndexToW", 4, [](Cpu &c) {
        c.fpuPush(fogIndexToW(int(c.arg(0))));
        return 0u;
    });
    auto fogTable = [this](Cpu &c, const std::function<double(double w)> &f) {
        if (!guest(c.arg(0), 64))
            return 0u;
        for (int i = 0; i < 64; ++i)
            c.write8(c.arg(0) + uint32_t(i), uint8_t(std::clamp(std::lround(f(fogIndexToW(i)) * 255.0), 0L, 255L)));
        return 0u;
    };
    // Normalized so the last entry is the most fog (Reference Manual).
    add("guFogGenerateExp", 8, [fogTable](Cpu &c) {
        const double d = argFloat(c, 1);
        const double last = 1 - std::exp(-d * fogIndexToW(63));
        return fogTable(c, [d, last](double w) { return last > 0 ? (1 - std::exp(-d * w)) / last : 0.0; });
    });
    add("guFogGenerateExp2", 8, [fogTable](Cpu &c) {
        const double d = argFloat(c, 1);
        const double l = d * fogIndexToW(63);
        const double last = 1 - std::exp(-l * l);
        return fogTable(c, [d, last](double w) { return last > 0 ? (1 - std::exp(-(d * w) * (d * w))) / last : 0.0; });
    });
    add("guFogGenerateLinear", 12, [fogTable](Cpu &c) {
        const double nearW = argFloat(c, 1);
        const double farW = argFloat(c, 2);
        return fogTable(c, [nearW, farW](double w) { return farW != nearW ? (w - nearW) / (farW - nearW) : 1.0; });
    });
}

void GlideDll::add3dfLoaders(bool glide3) {
    // Gu3dfInfo (glideutl.h): Gu3dfHeader (width, height, small_lod,
    // large_lod, aspect_ratio, format: 24 bytes), the GuTexTable union
    // (1024 bytes), data, mem_required. Neither function is told the
    // file's size, so each reads it whole.
    auto read = [this](Cpu &c, std::vector<uint8_t> &bytes, File3df &f) {
        std::string why = "no file reader";
        if (!readFile || !readFile(cpu_.string(c.arg(0)), bytes))
            return false;
        if (!parse3dfHeader(bytes.data(), bytes.size(), f, &why)) {
            core_->notImplemented("gu3df " + cpu_.string(c.arg(0)), why);
            return false;
        }
        return true;
    };
    add("gu3dfGetInfo", 8, [this, read, glide3](Cpu &c) {
        std::vector<uint8_t> bytes;
        File3df f;
        const uint32_t info = c.arg(1);
        if (!guest(info, 1056) || !read(c, bytes, f))
            return 0u;
        c.write32(info, uint32_t(f.width));
        c.write32(info + 4, uint32_t(f.height));
        c.write32(info + 8, uint32_t(glide3 ? f.smallLodLog2 : 8 - f.smallLodLog2));
        c.write32(info + 12, uint32_t(glide3 ? f.largeLodLog2 : 8 - f.largeLodLog2));
        c.write32(info + 16, uint32_t(glide3 ? f.aspectLog2 : 3 - f.aspectLog2));
        c.write32(info + 20, uint32_t(f.format));
        c.write32(info + 1052, f.dataBytes); // mem_required
        return 1u;
    });
    add("gu3dfLoad", 8, [this, read](Cpu &c) {
        std::vector<uint8_t> bytes;
        File3df f;
        const uint32_t info = c.arg(1);
        if (!guest(info, 1056) || !read(c, bytes, f))
            return 0u;
        const uint32_t data = cpu_.read32(info + 1048);
        if (!guest(data, f.dataBytes))
            return 0u;
        std::vector<uint8_t> texels(f.dataBytes);
        if (!load3dfData(bytes.data(), bytes.size(), f, texels.data()))
            return 0u;
        std::memcpy(cpu_.memory().data() + data, texels.data(), texels.size());
        return 1u;
    });
}

GlideDll::GlideDll(Cpu &cpu, x86::Arena &memory, std::string dllName)
    : cpu_(cpu), memory_(memory), dllName_(std::move(dllName)) {
}

float GlideDll::argFloat(const Cpu &c, int i) {
    const uint32_t u = c.arg(i);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

void GlideDll::provideTo(x86::Win32Process &process) const {
    for (const auto &[name, fn] : natives_)
        process.provide(dllName_, name, fn);
}

void GlideDll::add(const std::string &name, int argBytes, Fn fn) {
    const std::string decorated = "_" + name + "@" + std::to_string(argBytes);
    const uint32_t pop = uint32_t(argBytes);
    // The mode-setting functions' first arguments are counted too.
    static const char *const kModes[] = {"grFogMode",           "grTexFilterMode", "grTexMipMapMode", "grDitherMode",
                                         "grDepthBufferMode",   "grCullMode",      "grEnable",        "grDisable",
                                         "grAlphaTestFunction", "grChromakeyMode", "grTexClampMode"};
    const bool mode = std::find(std::begin(kModes), std::end(kModes), name) != std::end(kModes);
    const int modeArg = name == "grTexFilterMode" || name == "grTexMipMapMode" || name == "grTexClampMode" ? 1 : 0;
    Cpu::Native native = [this, name, fn = std::move(fn), pop, mode, modeArg](Cpu &c) {
        ++calls_[name];
        if (mode)
            ++modes_[name][c.arg(modeArg)];
        c.reg[Cpu::EAX] = fn(c);
        return pop;
    };
    natives_[decorated] = native;
    exports_[decorated] = cpu_.addNative(dllName_.substr(0, dllName_.find('.')) + " " + name, std::move(native));
}

uint32_t GlideDll::addProc(const std::string &name, int argBytes, Fn fn) {
    const uint32_t pop = uint32_t(argBytes);
    return cpu_.addNative(dllName_.substr(0, dllName_.find('.')) + " " + name,
                          [this, name, fn = std::move(fn), pop](Cpu &c) {
                              ++calls_[name];
                              c.reg[Cpu::EAX] = fn(c);
                              return pop;
                          });
}

void GlideDll::stub(const std::string &name, int argBytes, uint32_t result) {
    stubs_.push_back(name);
    add(name, argBytes, [this, name, result](Cpu &) {
        core_->notImplemented(name);
        return result;
    });
}

const uint8_t *GlideDll::guest(uint32_t address, uint32_t size) const {
    return address && cpu_.valid(address, std::max<uint32_t>(size, 1)) ? cpu_.memory().data() + address : nullptr;
}

void GlideDll::fillMirror(Mirror &m) {
    const Pipeline &p = core_->pipeline();
    const int w = p.width();
    const int h = p.height();
    uint8_t *g = cpu_.memory().data() + m.address;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const uint16_t v = core_->lfbRead(m.lock.buffer, x, y, m.lock.originLowerLeft);
            uint8_t *at = g + size_t(y) * size_t(m.lock.strideBytes) + size_t(x) * size_t(m.lock.bytesPerPixel);
            if (m.lock.bytesPerPixel == 2) {
                std::memcpy(at, &v, 2);
                continue;
            }
            // 32-bit modes: the pixel as that mode would write it.
            uint32_t out = 0;
            const Rgba c = from565(v);
            const int row = m.lock.originLowerLeft ? h - 1 - y : y;
            const uint16_t *aux = p.buffer(kBufferAux);
            const uint32_t depth = aux ? aux[size_t(row) * size_t(w) + size_t(x)] : 0;
            switch (m.lock.writeMode) {
            case kLfb888:
            case kLfb8888:
                out = 0xff000000u | uint32_t(c.r) << 16 | uint32_t(c.g) << 8 | uint32_t(c.b);
                break;
            case kLfb565Depth:
                out = uint32_t(v) << 16 | depth;
                break;
            default: { // 555 or 1555 with depth
                const uint32_t c555 = uint32_t(v >> 11) << 10 | uint32_t(v >> 6 & 31) << 5 | uint32_t(v & 31);
                out = (c555 | 0x8000u) << 16 | depth;
                break;
            }
            }
            std::memcpy(at, &out, 4);
        }
    m.snapshot.assign(g, g + m.size);
}

void GlideDll::flushMirror(Mirror &m) {
    const Pipeline &p = core_->pipeline();
    const int w = p.width();
    const int h = p.height();
    const uint8_t *g = cpu_.memory().data() + m.address;
    const int bpp = m.lock.bytesPerPixel;
    // grLfbWriteColorSwizzle works on the 32-bit words written: for the
    // 16-bit modes, two pixels at a time.
    const bool swizzle = core_->lfbSwizzleBytes() || core_->lfbSwapWords();
    for (int y = 0; y < h; ++y) {
        const size_t row = size_t(y) * size_t(m.lock.strideBytes);
        if (std::memcmp(g + row, m.snapshot.data() + row, size_t(w) * size_t(bpp)) == 0)
            continue;
        if (swizzle) {
            const int step = bpp == 2 ? 2 : 1;
            for (int x = 0; x < w; x += step) {
                const size_t at = row + size_t(x) * size_t(bpp);
                if (std::memcmp(g + at, m.snapshot.data() + at, 4) == 0)
                    continue;
                uint32_t word = 0;
                std::memcpy(&word, g + at, 4);
                word = core_->lfbSwizzle(m.lock.writeMode, word);
                if (bpp == 4) {
                    core_->lfbWrite(m.lock, x, y, word);
                } else {
                    core_->lfbWrite(m.lock, x, y, word & 0xffff);
                    if (x + 1 < w)
                        core_->lfbWrite(m.lock, x + 1, y, word >> 16);
                }
            }
            continue;
        }
        for (int x = 0; x < w; ++x) {
            const size_t at = row + size_t(x) * size_t(bpp);
            if (std::memcmp(g + at, m.snapshot.data() + at, size_t(bpp)) == 0)
                continue;
            uint32_t v = 0;
            std::memcpy(&v, g + at, size_t(bpp));
            core_->lfbWrite(m.lock, x, y, v);
        }
    }
}

uint32_t GlideDll::lfbLock(uint32_t type, uint32_t buffer, uint32_t writeMode, uint32_t origin, bool pipeline,
                           uint32_t info) {
    // GrLfbInfo_t: size (20, set by the caller), lfbPtr, strideInBytes,
    // writeMode, origin.
    if (!guest(info, 20) || cpu_.read32(info) < 20)
        return 0;
    Glide2LfbLock lock;
    if (!core_->lfbLock(int(type), int(buffer), int(writeMode), int(origin), pipeline, lock))
        return 0;
    const uint32_t key = uint32_t(lock.buffer) << 16 | uint32_t(lock.writeMode) << 1 | uint32_t(lock.originLowerLeft);
    Mirror &m = mirrors_[key];
    if (!m.address) {
        m.size = uint32_t(lock.strideBytes) * uint32_t(core_->pipeline().height());
        m.address = memory_.alloc(m.size, 4096);
        if (!m.address) {
            core_->lfbUnlock(int(type), int(buffer));
            core_->notImplemented("grLfbLock", "no guest memory for the buffer's copy");
            return 0;
        }
    }
    m.lock = lock;
    if (m.locks++ == 0) {
        m.written = false;
        fillMirror(m);
    }
    m.written |= (type & 1) != 0;
    if (lock.pixelPipeline)
        core_->notImplemented("grLfbLock pixelPipeline", "pixels written through the pipeline when unlocked");
    cpu_.write32(info + 4, m.address);
    cpu_.write32(info + 8, uint32_t(lock.strideBytes));
    cpu_.write32(info + 12, uint32_t(lock.writeMode));
    cpu_.write32(info + 16, lock.originLowerLeft ? 1u : 0u);
    return 1;
}

uint32_t GlideDll::lfbUnlock(uint32_t type, uint32_t buffer) {
    core_->lfbUnlock(int(type), int(buffer));
    // The mirrors of this buffer whose last lock this is.
    for (auto &[key, m] : mirrors_) {
        if (m.lock.buffer != int(buffer) || m.locks == 0)
            continue;
        if (--m.locks == 0 && m.written)
            flushMirror(m);
        break;
    }
    return 1;
}

} // namespace reluva::glide
