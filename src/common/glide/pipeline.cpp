// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/glide/pipeline.h"

#include "common/glide/raster_setup.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace reluva::glide {

Board voodoo2Board() { return Board{}; }

Board voodoo3Board() {
    // A Voodoo3 3000 with 16 MB: Reluva gives 8 MB to the frame buffer and
    // 8 MB to the textures, each of the two TMUs reading all of it (here,
    // each TMU its own 8 MB). The documents don't give Glide's split.
    Board b;
    b.generation = Generation::Voodoo3;
    b.tmus = 2;
    b.tmuMemory = 8u << 20;
    b.frameBufferMemory = 8u << 20;
    b.fbiRevision = 1;
    b.tmuRevision = 1;
    b.maxWidth = 2048;
    b.maxHeight = 1536;
    return b;
}

Pipeline::Pipeline(Board board) : board_(board), renderer_(makeSoftwareRenderer()) {
    board_.tmus = std::clamp(board_.tmus, 1, kMaxTmus);
    tmuMemory_.assign(size_t(board_.tmus), std::vector<uint8_t>(board_.tmuMemory));
    palettes_.assign(size_t(board_.tmus), std::array<uint32_t, 256>{});
    ncc_.assign(size_t(board_.tmus), std::array<NccTable, 2>{});
}

Pipeline::~Pipeline() = default;

bool Pipeline::open(int width, int height, int colorBuffers, int auxBuffers) {
    if (width <= 0 || height <= 0 || width > board_.maxWidth || height > board_.maxHeight)
        return false;
    colorBuffers = std::clamp(colorBuffers, 1, 3);
    const uint64_t need = uint64_t(width) * uint64_t(height) * 2u * uint64_t(colorBuffers + (auxBuffers > 0 ? 1 : 0));
    if (need > uint64_t(board_.frameBufferMemory) * (board_.sli ? 2u : 1u))
        return false;
    width_ = width;
    height_ = height;
    colorBuffers_ = colorBuffers;
    // The auxiliary buffer is always kept: a board with room for it has it
    // (the application only chooses whether Glide checks for it).
    hasAux_ = true;
    color_.assign(3, std::vector<uint16_t>(size_t(width) * size_t(height), 0));
    aux_.assign(size_t(width) * size_t(height), 0);
    front_ = 0;
    back_ = 1;
    triple_ = 2;
    open_ = true;
    ++opened_;
    resetState();
    return true;
}

void Pipeline::close() {
    open_ = false;
    color_.clear();
    aux_.clear();
}

void Pipeline::resetState() {
    const std::array<float, 3> gamma = state_.gamma; // the video's gamma stays
    state_ = State{};
    state_.gamma = gamma;
    state_.clip[2] = width_;
    state_.clip[3] = height_;
    for (int i = 0; i < kMaxTmus; ++i)
        state_.tmus[size_t(i)] = TmuState{};
}

void Pipeline::swap() {
    if (colorBuffers_ >= 3) {
        const int oldFront = front_;
        front_ = back_;
        back_ = triple_;
        triple_ = oldFront;
    } else {
        std::swap(front_, back_);
    }
    ++swaps_;
    renderer_->swapped(*this);
}

void Pipeline::triangle(const Vertex& a, const Vertex& b, const Vertex& c) {
    if (open_)
        renderer_->triangle(*this, a, b, c);
}

void Pipeline::line(const Vertex& a, const Vertex& b) {
    if (open_)
        renderer_->line(*this, a, b);
}

void Pipeline::point(const Vertex& a) {
    if (open_)
        renderer_->point(*this, a);
}

void Pipeline::aaTriangle(const Vertex& a, const Vertex& b, const Vertex& c, bool ab, bool bc, bool ca) {
    if (!open_)
        return;
    renderer_->triangle(*this, a, b, c);
    const bool aa[3] = {ab, bc, ca};
    raster::Fragments f;
    raster::aaTriangleFringe(state_, width_, height_, a, b, c, aa, f);
    if (!f.list.empty())
        renderer_->fragments(*this, f);
}

void Pipeline::aaLine(const Vertex& a, const Vertex& b) {
    if (!open_)
        return;
    raster::Fragments f;
    raster::aaLinePixels(state_, width_, height_, a, b, f);
    if (!f.list.empty())
        renderer_->fragments(*this, f);
}

void Pipeline::aaPoint(const Vertex& a) {
    if (!open_)
        return;
    raster::Fragments f;
    raster::aaPointPixels(state_, width_, height_, a, f);
    if (!f.list.empty())
        renderer_->fragments(*this, f);
}

void Pipeline::fragments(const raster::Fragments& f) {
    if (open_ && !f.list.empty())
        renderer_->fragments(*this, f);
}

void Pipeline::clear(uint32_t argb, int alpha, int depth) {
    if (open_)
        renderer_->clear(*this, argb, alpha, depth);
}

void Pipeline::sync() { renderer_->sync(*this); }

uint16_t* Pipeline::buffer(int id) {
    if (!open_)
        return nullptr;
    switch (id) {
    case kBufferFront: return color_[size_t(front_)].data();
    case kBufferBack: return color_[size_t(back_)].data();
    case kBufferTriple: return colorBuffers_ >= 3 ? color_[size_t(triple_)].data() : nullptr;
    case kBufferAux:
    case kBufferDepth:
    case kBufferAlpha: return hasAux_ ? aux_.data() : nullptr;
    default: return nullptr;
    }
}

int Pipeline::slotOf(int id) const {
    if (!open_)
        return -1;
    switch (id) {
    case kBufferFront: return front_;
    case kBufferBack: return back_;
    case kBufferTriple: return colorBuffers_ >= 3 ? triple_ : -1;
    case kBufferAux:
    case kBufferDepth:
    case kBufferAlpha: return hasAux_ ? 3 : -1;
    default: return -1;
    }
}

uint16_t* Pipeline::slotPixels(int slot) {
    if (!open_ || slot < 0 || slot >= kSlots)
        return nullptr;
    return slot == 3 ? (hasAux_ ? aux_.data() : nullptr) : color_[size_t(slot)].data();
}

const uint16_t* Pipeline::buffer(int id) const { return const_cast<Pipeline*>(this)->buffer(id); }

void Pipeline::downloadLevel(int tmu, uint32_t start, const TextureInfo& info, int evenOdd, int lod,
                             const uint8_t* data, int firstRow, int lastRow) {
    if (tmu < 0 || tmu >= board_.tmus || !data || !knownFormat(info.format))
        return;
    int w = 0, h = 0;
    levelSize(lod, info.aspect, w, h);
    firstRow = std::max(firstRow, 0);
    lastRow = std::min(lastRow, h - 1);
    if (firstRow > lastRow)
        return;
    const size_t rowBytes = size_t(w) * size_t(texelBytes(info.format));
    const uint32_t at = start + levelOffset(info, evenOdd, lod) + uint32_t(size_t(firstRow) * rowBytes);
    const size_t bytes = size_t(lastRow - firstRow + 1) * rowBytes;
    std::vector<uint8_t>& mem = tmuMemory_[size_t(tmu)];
    if (at >= mem.size())
        return;
    const size_t n = std::min(bytes, mem.size() - at);
    std::memcpy(mem.data() + at, data, n);
    renderer_->textureMemoryChanged(*this, tmu, at, uint32_t(n));
}

void Pipeline::writeTmuMemory(int tmu, uint32_t at, const uint8_t* data, size_t bytes) {
    if (tmu < 0 || tmu >= board_.tmus || at >= board_.tmuMemory)
        return;
    const size_t n = std::min(bytes, size_t(board_.tmuMemory - at));
    std::memcpy(tmuMemory_[size_t(tmu)].data() + at, data, n);
    renderer_->textureMemoryChanged(*this, tmu, at, uint32_t(n));
}

void Pipeline::setPalette(int tmu, int first, int last, const uint32_t* entries) {
    if (tmu < 0 || tmu >= board_.tmus)
        return;
    first = std::clamp(first, 0, 255);
    last = std::clamp(last, 0, 255);
    for (int i = first; i <= last; ++i)
        palettes_[size_t(tmu)][size_t(i)] = entries[i - first] & 0xffffff;
    renderer_->tablesChanged(*this, tmu);
}

void Pipeline::setNcc(int tmu, int which, const NccTable& table) {
    if (tmu < 0 || tmu >= board_.tmus)
        return;
    ncc_[size_t(tmu)][size_t(which & 1)] = table;
    renderer_->tablesChanged(*this, tmu);
}

uint32_t Pipeline::levelAddress(int tmu, int lod) const {
    const TmuState& t = state_.tmus[size_t(tmu)];
    if (!t.multibase)
        return t.start + levelOffset(t.info, t.evenOdd, lod);
    if (lod >= 8)
        return t.multibaseAddress[0];
    if (lod == 7)
        return t.multibaseAddress[1];
    if (lod == 6)
        return t.multibaseAddress[2];
    // GR_TEXBASE_32_TO_1: levels 32 to 1 in a row from the fourth base.
    uint32_t offset = 0;
    for (int l = std::min(5, t.info.largeLod); l > lod; --l)
        if (levelIncluded(l, t.evenOdd))
            offset += levelBytes(l, t.info.aspect, t.info.format);
    return t.multibaseAddress[3] + offset;
}

void Pipeline::lfbWrite(int bufferId, int x, int y, int mode, uint32_t value, bool pipeline) {
    if (!open_ || x < 0 || y < 0 || x >= width_ || y >= height_)
        return;
    // The pixel's colour, alpha and depth, as the mode carries them.
    Rgba c{0, 0, 0, state_.lfbConstantAlpha};
    int depth = state_.lfbConstantDepth;
    bool hasColor = true;
    auto from555 = [](uint32_t v) {
        const int r = int(v >> 10 & 31), g = int(v >> 5 & 31), b = int(v & 31);
        return Rgba{r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2, 255};
    };
    switch (mode) {
    case kLfb565: c = from565(uint16_t(value)); c.a = state_.lfbConstantAlpha; break;
    case kLfb555: c = from555(value); c.a = state_.lfbConstantAlpha; break;
    case kLfb1555: c = from555(value); c.a = value & 0x8000 ? 255 : 0; break;
    case kLfb888: c = unpackArgb(value); c.a = state_.lfbConstantAlpha; break;
    case kLfb8888: c = unpackArgb(value); break;
    case kLfb565Depth: c = from565(uint16_t(value >> 16)); c.a = state_.lfbConstantAlpha; depth = int(value & 0xffff); break;
    case kLfb555Depth: c = from555(value >> 16); c.a = state_.lfbConstantAlpha; depth = int(value & 0xffff); break;
    case kLfb1555Depth: c = from555(value >> 16); c.a = value & 0x80000000u ? 255 : 0; depth = int(value & 0xffff); break;
    case kLfbZA16: hasColor = false; depth = int(value & 0xffff); break;
    default: return;
    }
    const size_t at = size_t(y) * size_t(width_) + size_t(x);
    if (!pipeline)
        renderer_->sync(*this); // what's drawn is in the buffers before they're written
    if (!hasColor) { // the auxiliary buffer's 16 bits
        if (bufferId == kBufferAux || bufferId == kBufferDepth || bufferId == kBufferAlpha) {
            aux_[at] = uint16_t(depth);
            renderer_->hostWrote(*this, 3, x, y);
        }
        return;
    }
    uint16_t* target = buffer(bufferId);
    if (!target)
        return;
    if (!pipeline) {
        // Straight in: 16-bit modes as they are, 24-bit ones dithered as
        // the state says; the depth too for the modes that carry one.
        if (mode == kLfb565 || mode == kLfb565Depth)
            target[at] = uint16_t(mode == kLfb565 ? value : value >> 16);
        else
            target[at] = to565(c.r, c.g, c.b, x, y, mode == kLfb888 || mode == kLfb8888 ? state_.dither : kDitherDisable);
        renderer_->hostWrote(*this, slotOf(bufferId), x, y);
        // A mode with alpha (1555, 8888, 1555 with depth) writes it into the
        // alpha buffer when alpha buffering is on, and then 1555 with depth
        // leaves the depth (Voodoo2 specification, lfbMode: "the alpha
        // component is written into the alpha buffer"; "the depth component
        // is ignored").
        const bool hasAlpha = mode == kLfb1555 || mode == kLfb8888 || mode == kLfb1555Depth;
        if (hasAlpha && state_.alphaMask && state_.depthMode == kDepthDisable) {
            aux_[at] = uint16_t(c.a);
            renderer_->hostWrote(*this, 3, x, y);
            return;
        }
        if (mode == kLfb565Depth || mode == kLfb555Depth || mode == kLfb1555Depth) {
            aux_[at] = uint16_t(depth);
            renderer_->hostWrote(*this, 3, x, y);
        }
        return;
    }
    // Through the pipeline: drawn as a point of this colour and depth, the
    // colour as the iterated colour.
    Vertex v;
    v.x = float(x) + 0.5f;
    v.y = float(state_.originLowerLeft ? height_ - 1 - y : y) + 0.5f;
    v.r = float(c.r);
    v.g = float(c.g);
    v.b = float(c.b);
    v.a = float(c.a);
    v.ooz = float(depth);
    const int savedBuffer = state_.renderBuffer;
    state_.renderBuffer = bufferId;
    renderer_->point(*this, v);
    state_.renderBuffer = savedBuffer;
}

void Pipeline::present(Surface& out) {
    if (open_)
        renderer_->syncBuffer(*this, kBufferFront);
    if (!open_)
        return;
    if (out.w != width_ || out.h != height_ || out.format != PixelFormat::RGBA32)
        out.create(width_, height_, PixelFormat::RGBA32);
    GammaTables gamma{};
    for (size_t ch = 0; ch < 3; ++ch) {
        const double g = state_.gamma[ch] > 0 ? state_.gamma[ch] : 1.0;
        for (int i = 0; i < 256; ++i)
            gamma[ch][size_t(i)] = uint8_t(std::clamp(std::lround(std::pow(i / 255.0, 1.0 / g) * 255.0), 0L, 255L));
    }
    const uint16_t* src = color_[size_t(front_)].data();
    if (outputFilter) {
        outputFilter(src, width_, height_, gamma, out);
        return;
    }
    for (int y = 0; y < height_; ++y) {
        uint8_t* dst = out.row(y);
        const uint16_t* row = src + size_t(y) * size_t(width_);
        for (int x = 0; x < width_; ++x, dst += 4) {
            const Rgba c = from565(row[x]);
            dst[0] = gamma[0][size_t(c.r)];
            dst[1] = gamma[1][size_t(c.g)];
            dst[2] = gamma[2][size_t(c.b)];
            dst[3] = 255;
        }
    }
}

} // namespace reluva::glide
