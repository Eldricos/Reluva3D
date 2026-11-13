// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "graphics/glide/pipeline.h"

#include "graphics/glide/raster_setup.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace reluva::glide {

Board voodoo1Board() {
    Board b;
    b.generation = Generation::kVoodoo1;
    b.extensions = 0; // none on the Voodoo Graphics and Rush (Board::extensions)
    b.tmus = 1;
    b.tmuMemory = 2u << 20;
    b.frameBufferMemory = 2u << 20;
    b.maxWidth = 800;
    b.maxHeight = 600;
    return b;
}

Board voodooRushBoard() {
    Board b = voodoo1Board();
    b.generation = Generation::kVoodooRush;
    b.frameBufferMemory = 4u << 20;
    b.palette = false;
    b.windowed = true;
    // "any resolution which fits in the frame buffer memory, is less than
    // 2K by 2K" (SST-96 specification, System Resolutions).
    b.maxWidth = 1600;
    b.maxHeight = 1200;
    // The desktop the window is on: 1024x768 at 16 bits, the first row of
    // the specification's in-place table (a choice; boardByName's
    // "desktop=" and setDesktop change it, to the host's desktop for one).
    b.desktopWidth = 1024;
    b.desktopHeight = 768;
    b.desktopBits = 16;
    b.lfbStride = 0;
    b.lfbPixelPipeline = false;
    b.lfbFormats = false;
    // "It is a no-op on SST-96 systems, which may lack gamma correction
    // hardware" (Glide 2.4 Reference Manual, grGammaCorrectionValue): no
    // table to load or ask about (Glide 3's grGet(GR_GAMMA_TABLE_ENTRIES)
    // and GR_BITS_GAMMA fail "if gamma correction is not available").
    b.gammaTableEntries = 0;
    return b;
}

// Board's defaults: chroma range ("Source and Destination Chroma-range
// functionality") and mirroring ("Texture coordinate clamping, wrapping,
// and mirroring", Voodoo2 specification, Features) among them.
Board voodoo2Board() {
    return Board{};
}

Board voodoo3Board() {
    // A Voodoo3 with 16 MB, unified: the frame buffer at the bottom, the
    // textures above it, both TMUs reading all of it.
    Board b;
    b.generation = Generation::kVoodoo3;
    b.tmus = 2;
    b.uma = true;
    b.tmuMemory = 16u << 20;
    b.frameBufferMemory = 16u << 20;
    b.fbiRevision = 1;
    b.tmuRevision = 1;
    b.maxWidth = 2048;
    b.maxHeight = 1536;
    b.textureAlign = 16;
    b.gammaTableEntries = 256;
    b.lfbStride = 2048;
    // Chroma range, mirroring, "Additional 6666 palettized texture format"
    // (Voodoo3 specification, Modifications from SST1).
    b.extensions = kExtChromaRange | kExtTexMirror | kExtPalette6666;
    return b;
}

Board bansheeBoard() {
    // The Banshee specification (revision 1.1, November 1998): one TMU
    // ("Combined TMU unit", TREX0 alone in its FBI/TMU diagram), 4 to 16 MB
    // of SGRAM or SDRAM shared by "RGB, alpha/depth-buffer, 2D desktop,
    // video, and texture memory" (Memory Architecture), texture base
    // addresses "in 16-byte units" (texBaseAddr), a 64-entry fog table, the
    // palette with "PalettedRGBA (8 expanded to ARGB 6-6-6-6)", chroma
    // range and texture mirroring (Modifications from SST1), a 256-entry
    // CLUT a channel for gamma (dacData: 24 bits, red, green and blue, "the
    // lower 256 entries" for the desktop), the 2048-pixel logical scan line
    // (Linear Frame Buffer Access), and a RAMDAC "capable of doing
    // 1600x1280 @ 76Hz" (Functional Overview): up to 1600x1200 here.
    // Revision_ID "1=.35u, 2 = .25u": 1, the first.
    Board b = voodoo3Board();
    b.generation = Generation::kBanshee;
    b.tmus = 1;
    b.fbiRevision = 1;
    b.tmuRevision = 1;
    b.maxWidth = 1600;
    b.maxHeight = 1200;
    return b;
}

bool boardByName(const std::string &spec, Board &out, bool *desktopClamped) {
    // The name, then ",tmu=MB", ",fb=MB", ",tmus=N", ",tmurev=N" to change
    // the sizes ("voodoo1,tmu=4": the 6 MB card).
    if (desktopClamped)
        *desktopClamped = false;
    const std::string name = spec.substr(0, spec.find(','));
    int desktop = -1; // -1: the board's own
    int desktopW = 0;
    int desktopH = 0;
    int desktopBits = 0;
    if (name == "voodoo1" || name == "voodoo" || name == "sst1")
        out = voodoo1Board();
    else if (name == "rush" || name == "sst96")
        out = voodooRushBoard();
    else if (name == "voodoo2")
        out = voodoo2Board();
    else if (name == "banshee")
        out = bansheeBoard();
    else if (name == "voodoo3")
        out = voodoo3Board();
    else
        return false;
    for (size_t at = spec.find(','); at != std::string::npos; at = spec.find(',', at + 1)) {
        const std::string option = spec.substr(at + 1, spec.find(',', at + 1) - at - 1);
        const size_t eq = option.find('=');
        if (eq == std::string::npos)
            return false;
        const std::string key = option.substr(0, eq);
        const int value = std::atoi(option.c_str() + eq + 1);
        if (key == "tmu" && value > 0 && value <= 16)
            out.tmuMemory = uint32_t(value) << 20;
        else if (key == "fb" && value > 0 && value <= 16)
            out.frameBufferMemory = uint32_t(value) << 20;
        else if (key == "tmus" && value >= 1 && value <= kMaxTmus)
            out.tmus = value;
        else if (key == "tmurev" && value >= 0) {
            out.tmuRevision = value;
            out.palette = value >= 1 && out.generation != Generation::kVoodooRush;
        } else if (key == "desktop") {
            // WxHxBITS, or 0 for none: set after the loop, so against the
            // memory "fb=" gives in whatever order they come.
            const std::string v = option.substr(eq + 1);
            if (v == "0")
                desktop = 0;
            else if (std::sscanf(v.c_str(), "%dx%dx%d", &desktopW, &desktopH, &desktopBits) == 3)
                desktop = 1;
            else
                return false;
        } else
            return false;
    }
    if (desktop == 0)
        out.desktopWidth = out.desktopHeight = out.desktopBits = 0;
    else if (desktop == 1 && !setDesktop(out, desktopW, desktopH, desktopBits, desktopClamped))
        return false;
    return true;
}

bool setDesktop(Board &board, int width, int height, int bits, bool *clamped) {
    if (clamped)
        *clamped = false;
    if (!board.windowed)
        return true;
    if (width <= 0 || height <= 0)
        return false;
    int depth = 0;
    switch (bits) {
    case 8:
        depth = 8;
        break;
    case 15:
    case 16:
    case 24:
    case 32:
        depth = 16;
        break;
    default:
        return false;
    }
    // The tables' desktops (SST-96 specification, System Resolutions):
    // 1600x1200 down to 1024x768 at 8 bits, from 1280x1024 at 16.
    static constexpr int kSizes[][2] = {{1600, 1200}, {1280, 1024}, {1152, 864}, {1024, 768}};
    int size = depth == 8 ? 0 : 1;
    int w = std::min(width, kSizes[size][0]);
    int h = std::min(height, kSizes[size][1]);
    // A desktop that leaves no memory for a window: the next size down.
    while (uint64_t(w) * uint64_t(h) * uint64_t(depth / 8) >= board.frameBufferMemory && size < 3) {
        ++size;
        w = std::min(w, kSizes[size][0]);
        h = std::min(h, kSizes[size][1]);
    }
    if (clamped)
        *clamped = w != width || h != height || depth != bits;
    board.desktopWidth = w;
    board.desktopHeight = h;
    board.desktopBits = depth;
    return true;
}

Pipeline::Pipeline(Board board) : _board(board), _renderer(makeSoftwareRenderer()) {
    _board.tmus = std::clamp(_board.tmus, 1, kMaxTmus);
    _tmuMemory.assign(size_t(_board.tmus), std::vector<uint8_t>(_board.tmuMemory));
    _palettes.assign(size_t(_board.tmus), std::array<uint32_t, 256>{});
    _ncc.assign(size_t(_board.tmus), std::array<NccTable, 2>{});
}

Pipeline::~Pipeline() = default;

uint64_t Pipeline::bufferBytes(int width, int height, int colorBuffers, int auxBuffers, bool windowed) const {
    colorBuffers = std::clamp(colorBuffers, 1, 3);
    const uint64_t buffer = uint64_t(std::max(width, 0)) * uint64_t(std::max(height, 0)) * 2u;
    if (!windowed || _board.desktopBits == 0)
        return buffer * uint64_t(colorBuffers + (auxBuffers > 0 ? 1 : 0));
    // Windowed on a shared memory (SST-96 specification, System
    // Resolutions: "windowed 3D requires the additional memory space of
    // the 2D desktop"): the desktop, then the 3D buffers off screen. On a
    // 16-bit desktop the front buffer is the window's own place on it
    // ("In-Place 3D (one on-screen 3D buffer & one off-screen 3D buffer)"),
    // on an 8-bit one every colour buffer is off screen ("Out-of-Place 3D
    // (two off-screen 3D buffers)"). Its tables' totals add up so, for
    // instance 1,572,864 + 614,400 for 640x480 double buffered on a
    // 16-bit 1024x768 desktop (its 2,187,264).
    const uint64_t desktop =
        uint64_t(_board.desktopWidth) * uint64_t(_board.desktopHeight) * uint64_t(_board.desktopBits / 8);
    const int offScreen = _board.desktopBits == 16 ? colorBuffers - 1 : colorBuffers;
    return desktop + buffer * uint64_t(offScreen + (auxBuffers > 0 ? 1 : 0));
}

bool Pipeline::fits(int width, int height, int colorBuffers, int auxBuffers, bool windowed) const {
    if (width <= 0 || height <= 0 || width > _board.maxWidth || height > _board.maxHeight)
        return false;
    if (windowed && _board.desktopBits != 0 && (width > _board.desktopWidth || height > _board.desktopHeight))
        return false; // a window larger than the desktop it is on
    return bufferBytes(width, height, colorBuffers, auxBuffers, windowed) <=
           uint64_t(_board.frameBufferMemory) * (_board.sli ? 2u : 1u);
}

int Pipeline::lfbStride() const {
    if (_board.lfbStride > 0)
        return _board.lfbStride;
    return (std::max(_width, 1) + 3) & ~3; // the X stride in quad pixels
}

bool Pipeline::resize(int width, int height) {
    if (!_open || !_windowed || !fits(width, height, _colorBuffers, _hasAuxAsked ? 1 : 0, true))
        return false;
    const State kept = _state;
    const uint64_t swaps = _swaps;
    if (!open(width, height, _colorBuffers, _hasAuxAsked ? 1 : 0, true))
        return false;
    const std::array<int, 4> clip = {_state.clip[0], _state.clip[1], _state.clip[2], _state.clip[3]};
    _state = kept;
    std::copy(clip.begin(), clip.end(), _state.clip);
    _swaps = swaps;
    return true;
}

bool Pipeline::open(int width, int height, int colorBuffers, int auxBuffers, bool windowed) {
    windowed = windowed && _board.windowed;
    if (!fits(width, height, colorBuffers, auxBuffers, windowed))
        return false;
    colorBuffers = std::clamp(colorBuffers, 1, 3);
    const uint64_t need = bufferBytes(width, height, colorBuffers, auxBuffers, false);
    _width = width;
    _height = height;
    _colorBuffers = colorBuffers;
    _windowed = windowed;
    _hasAuxAsked = auxBuffers > 0;
    // The auxiliary buffer is always kept: a board with room for it has it
    // (the application only chooses whether Glide checks for it).
    _hasAux = true;
    // On a UMA board the textures go above the buffers.
    _textureBase = 0;
    if (_board.uma) {
        const uint32_t align = uint32_t(std::max(_board.textureAlign, 1));
        _textureBase = uint32_t((need + align - 1) / align * align);
    }
    _color.assign(3, std::vector<uint16_t>(size_t(width) * size_t(height), 0));
    _aux.assign(size_t(width) * size_t(height), 0);
    _front = 0;
    _back = 1;
    _triple = 2;
    _open = true;
    ++_opened;
    resetState();
    return true;
}

void Pipeline::close() {
    _open = false;
    _color.clear();
    _aux.clear();
}

void Pipeline::resetState() {
    const std::array<float, 3> gamma = _state.gamma; // the video's gamma stays
    _state = State{};
    _state.gamma = gamma;
    _state.clip[2] = _width;
    _state.clip[3] = _height;
    for (int i = 0; i < kMaxTmus; ++i)
        _state.tmus[size_t(i)] = TmuState{};
}

void Pipeline::swap() {
    if (_colorBuffers >= 3) {
        const int oldFront = _front;
        _front = _back;
        _back = _triple;
        _triple = oldFront;
    } else {
        std::swap(_front, _back);
    }
    ++_swaps;
    _renderer->swapped(*this);
}

void Pipeline::triangle(const Vertex &a, const Vertex &b, const Vertex &c) {
    if (_open)
        _renderer->triangle(*this, a, b, c);
}

void Pipeline::line(const Vertex &a, const Vertex &b) {
    if (_open)
        _renderer->line(*this, a, b);
}

void Pipeline::point(const Vertex &a) {
    if (_open)
        _renderer->point(*this, a);
}

void Pipeline::aaTriangle(const Vertex &a, const Vertex &b, const Vertex &c, bool ab, bool bc, bool ca) {
    if (!_open)
        return;
    _renderer->triangle(*this, a, b, c);
    const bool aa[3] = {ab, bc, ca};
    raster::Fragments f;
    raster::aaTriangleFringe(_state, _width, _height, a, b, c, aa, f);
    if (!f.list.empty())
        _renderer->fragments(*this, f);
}

void Pipeline::aaLine(const Vertex &a, const Vertex &b) {
    if (!_open)
        return;
    raster::Fragments f;
    raster::aaLinePixels(_state, _width, _height, a, b, f);
    if (!f.list.empty())
        _renderer->fragments(*this, f);
}

void Pipeline::aaPoint(const Vertex &a) {
    if (!_open)
        return;
    raster::Fragments f;
    raster::aaPointPixels(_state, _width, _height, a, f);
    if (!f.list.empty())
        _renderer->fragments(*this, f);
}

void Pipeline::fragments(const raster::Fragments &f) {
    if (_open && !f.list.empty())
        _renderer->fragments(*this, f);
}

void Pipeline::clear(uint32_t argb, int alpha, int depth) {
    if (_open)
        _renderer->clear(*this, argb, alpha, depth);
}

void Pipeline::sync() {
    _renderer->sync(*this);
}

uint16_t *Pipeline::buffer(int id) {
    if (!_open)
        return nullptr;
    switch (id) {
    case kBufferFront:
        return _color[size_t(_front)].data();
    case kBufferBack:
        return _color[size_t(_back)].data();
    case kBufferTriple:
        return _colorBuffers >= 3 ? _color[size_t(_triple)].data() : nullptr;
    case kBufferAux:
    case kBufferDepth:
    case kBufferAlpha:
        return _hasAux ? _aux.data() : nullptr;
    default:
        return nullptr;
    }
}

int Pipeline::slotOf(int id) const {
    if (!_open)
        return -1;
    switch (id) {
    case kBufferFront:
        return _front;
    case kBufferBack:
        return _back;
    case kBufferTriple:
        return _colorBuffers >= 3 ? _triple : -1;
    case kBufferAux:
    case kBufferDepth:
    case kBufferAlpha:
        return _hasAux ? 3 : -1;
    default:
        return -1;
    }
}

uint16_t *Pipeline::slotPixels(int slot) {
    if (!_open || slot < 0 || slot >= kSlots)
        return nullptr;
    return slot == 3 ? (_hasAux ? _aux.data() : nullptr) : _color[size_t(slot)].data();
}

const uint16_t *Pipeline::buffer(int id) const {
    return const_cast<Pipeline *>(this)->buffer(id);
}

void Pipeline::downloadLevel(int tmu, uint32_t start, const TextureInfo &info, int evenOdd, int lod,
                             const uint8_t *data, int firstRow, int lastRow) {
    if (tmu < 0 || tmu >= _board.tmus || !data || !knownFormat(info.format))
        return;
    int w = 0;
    int h = 0;
    levelSize(lod, info.aspect, w, h);
    firstRow = std::max(firstRow, 0);
    lastRow = std::min(lastRow, h - 1);
    if (firstRow > lastRow)
        return;
    const size_t rowBytes = size_t(w) * size_t(texelBytes(info.format));
    const uint32_t at = start + levelOffset(info, evenOdd, lod) + uint32_t(size_t(firstRow) * rowBytes);
    const size_t bytes = size_t(lastRow - firstRow + 1) * rowBytes;
    std::vector<uint8_t> &mem = _tmuMemory[size_t(tmu)];
    if (at >= mem.size())
        return;
    const size_t n = std::min(bytes, mem.size() - at);
    // On a UMA board every TMU reads the one memory: written to each copy.
    for (int t = 0; t < _board.tmus; ++t) {
        if (t != tmu && !_board.uma)
            continue;
        std::memcpy(_tmuMemory[size_t(t)].data() + at, data, n);
        _renderer->textureMemoryChanged(*this, t, at, uint32_t(n));
    }
}

void Pipeline::writeTmuMemory(int tmu, uint32_t at, const uint8_t *data, size_t bytes) {
    if (tmu < 0 || tmu >= _board.tmus || at >= _board.tmuMemory)
        return;
    const size_t n = std::min(bytes, size_t(_board.tmuMemory - at));
    for (int t = 0; t < _board.tmus; ++t) {
        if (t != tmu && !_board.uma)
            continue;
        std::memcpy(_tmuMemory[size_t(t)].data() + at, data, n);
        _renderer->textureMemoryChanged(*this, t, at, uint32_t(n));
    }
}

void Pipeline::setPalette(int tmu, int first, int last, const uint32_t *entries) {
    if (tmu < 0 || tmu >= _board.tmus)
        return;
    first = std::clamp(first, 0, 255);
    last = std::clamp(last, 0, 255);
    for (int i = first; i <= last; ++i)
        _palettes[size_t(tmu)][size_t(i)] = entries[i - first] & 0xffffff;
    _palette6666[size_t(tmu)] = false;
    _renderer->tablesChanged(*this, tmu);
}

void Pipeline::setPalette6666(int tmu, int first, int last, const uint32_t *entries) {
    if (tmu < 0 || tmu >= _board.tmus)
        return;
    first = std::clamp(first, 0, 255);
    last = std::clamp(last, 0, 255);
    for (int i = first; i <= last; ++i) {
        const uint32_t e = entries[i - first];
        _palettes[size_t(tmu)][size_t(i)] =
            (e >> 26 & 63) << 18 | (e >> 18 & 63) << 12 | (e >> 10 & 63) << 6 | (e >> 2 & 63);
    }
    _palette6666[size_t(tmu)] = true;
    _renderer->tablesChanged(*this, tmu);
}

int Pipeline::textureFormat(int tmu) const {
    const int format = _state.tmus[size_t(tmu)].info.format;
    return format == kTexP8 && tmu >= 0 && tmu < _board.tmus && _palette6666[size_t(tmu)] ? 6 : format;
}

void Pipeline::setNcc(int tmu, int which, const NccTable &table) {
    if (tmu < 0 || tmu >= _board.tmus)
        return;
    _ncc[size_t(tmu)][size_t(which & 1)] = table;
    _renderer->tablesChanged(*this, tmu);
}

uint32_t Pipeline::levelAddress(int tmu, int lod) const {
    const TmuState &t = _state.tmus[size_t(tmu)];
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
    if (!_open || x < 0 || y < 0 || x >= _width || y >= _height)
        return;
    // The pixel's colour, alpha and depth, as the mode carries them.
    Rgba c{0, 0, 0, _state.lfbConstantAlpha};
    int depth = _state.lfbConstantDepth;
    bool hasColor = true;
    auto from555 = [](uint32_t v) {
        const int r = int(v >> 10 & 31);
        const int g = int(v >> 5 & 31);
        const int b = int(v & 31);
        return Rgba{r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2, 255};
    };
    switch (mode) {
    case kLfb565:
        c = from565(uint16_t(value));
        c.a = _state.lfbConstantAlpha;
        break;
    case kLfb555:
        c = from555(value);
        c.a = _state.lfbConstantAlpha;
        break;
    case kLfb1555:
        c = from555(value);
        c.a = value & 0x8000 ? 255 : 0;
        break;
    case kLfb888:
        c = unpackArgb(value);
        c.a = _state.lfbConstantAlpha;
        break;
    case kLfb8888:
        c = unpackArgb(value);
        break;
    case kLfb565Depth:
        c = from565(uint16_t(value >> 16));
        c.a = _state.lfbConstantAlpha;
        depth = int(value & 0xffff);
        break;
    case kLfb555Depth:
        c = from555(value >> 16);
        c.a = _state.lfbConstantAlpha;
        depth = int(value & 0xffff);
        break;
    case kLfb1555Depth:
        c = from555(value >> 16);
        c.a = value & 0x80000000u ? 255 : 0;
        depth = int(value & 0xffff);
        break;
    case kLfbZA16:
        hasColor = false;
        depth = int(value & 0xffff);
        break;
    default:
        return;
    }
    const size_t at = size_t(y) * size_t(_width) + size_t(x);
    if (!pipeline)
        _renderer->sync(*this); // what's drawn is in the buffers before they're written
    if (!hasColor) {            // the auxiliary buffer's 16 bits
        if (bufferId == kBufferAux || bufferId == kBufferDepth || bufferId == kBufferAlpha) {
            _aux[at] = uint16_t(depth);
            _renderer->hostWrote(*this, 3, x, y);
        }
        return;
    }
    uint16_t *target = buffer(bufferId);
    if (!target)
        return;
    if (!pipeline) {
        // Straight in: 16-bit modes as they are, 24-bit ones dithered as
        // the state says; the depth too for the modes that carry one.
        if (mode == kLfb565 || mode == kLfb565Depth)
            target[at] = uint16_t(mode == kLfb565 ? value : value >> 16);
        else
            target[at] =
                to565(c.r, c.g, c.b, x, y, mode == kLfb888 || mode == kLfb8888 ? _state.dither : kDitherDisable);
        _renderer->hostWrote(*this, slotOf(bufferId), x, y);
        // A mode with alpha (1555, 8888, 1555 with depth) writes it into the
        // alpha buffer when alpha buffering is on, and then 1555 with depth
        // leaves the depth (Voodoo2 specification, lfbMode: "the alpha
        // component is written into the alpha buffer"; "the depth component
        // is ignored").
        const bool hasAlpha = mode == kLfb1555 || mode == kLfb8888 || mode == kLfb1555Depth;
        if (hasAlpha && _state.alphaMask && _state.depthMode == kDepthDisable) {
            _aux[at] = uint16_t(c.a);
            _renderer->hostWrote(*this, 3, x, y);
            return;
        }
        if (mode == kLfb565Depth || mode == kLfb555Depth || mode == kLfb1555Depth) {
            _aux[at] = uint16_t(depth);
            _renderer->hostWrote(*this, 3, x, y);
        }
        return;
    }
    // Through the pipeline: drawn as a point of this colour and depth, the
    // colour as the iterated colour.
    Vertex v;
    v.x = float(x) + 0.5f;
    v.y = float(_state.originLowerLeft ? _height - 1 - y : y) + 0.5f;
    v.r = float(c.r);
    v.g = float(c.g);
    v.b = float(c.b);
    v.a = float(c.a);
    v.ooz = float(depth);
    const int savedBuffer = _state.renderBuffer;
    _state.renderBuffer = bufferId;
    _renderer->point(*this, v);
    _state.renderBuffer = savedBuffer;
}

void Pipeline::loadGammaTable(int n, const uint32_t *r, const uint32_t *g, const uint32_t *b) {
    const int size = std::max(_board.gammaTableEntries, 2);
    const uint32_t *channel[3] = {r, g, b};
    for (size_t ch = 0; ch < 3; ++ch) {
        std::vector<uint8_t> &t = _gammaTable[ch];
        if (!_gammaLoaded || int(t.size()) != size) {
            // Starting from the table the current gamma gives.
            t.resize(size_t(size));
            const double gv = _state.gamma[ch] > 0 ? _state.gamma[ch] : 1.0;
            for (int i = 0; i < size; ++i)
                t[size_t(i)] = uint8_t(std::lround(std::pow(double(i) / (size - 1), 1.0 / gv) * 255.0));
        }
        // "If nEntries is less than the size of the hardware-dependent gamma
        // table, the first part of the table will be overwritten ... If
        // nEntries is greater than the gamma table size, the extra values are
        // discarded."
        for (int i = 0; i < std::min(n, size); ++i)
            t[size_t(i)] = uint8_t(std::min<uint32_t>(channel[ch][i], 255));
    }
    _gammaLoaded = true;
}

void Pipeline::present(Surface &out) {
    if (_open)
        _renderer->syncBuffer(*this, kBufferFront);
    if (!_open)
        return;
    if (out.w != _width || out.h != _height || out.format != PixelFormat::kRGBA32)
        out.create(_width, _height, PixelFormat::kRGBA32);
    GammaTables gamma{};
    for (size_t ch = 0; ch < 3; ++ch) {
        const double g = _state.gamma[ch] > 0 ? _state.gamma[ch] : 1.0;
        for (int i = 0; i < 256; ++i)
            gamma[ch][size_t(i)] = uint8_t(std::clamp(std::lround(std::pow(i / 255.0, 1.0 / g) * 255.0), 0L, 255L));
        if (_gammaLoaded) {
            const std::vector<uint8_t> &t = _gammaTable[ch];
            const int n = int(t.size());
            for (int i = 0; i < 256; ++i) {
                const int pos = i * (n - 1); // in 255ths of an entry
                const int k = std::min(pos / 255, n - 2);
                const int f = pos - k * 255;
                gamma[ch][size_t(i)] = uint8_t((t[size_t(k)] * (255 - f) + t[size_t(k + 1)] * f + 127) / 255);
            }
        }
    }
    const uint16_t *src = _color[size_t(_front)].data();
    if (outputFilter) {
        outputFilter(src, _width, _height, gamma, out);
        return;
    }
    for (int y = 0; y < _height; ++y) {
        uint8_t *dst = out.row(y);
        const uint16_t *row = src + size_t(y) * size_t(_width);
        for (int x = 0; x < _width; ++x, dst += 4) {
            const Rgba c = from565(row[x]);
            dst[0] = gamma[0][size_t(c.r)];
            dst[1] = gamma[1][size_t(c.g)];
            dst[2] = gamma[2][size_t(c.b)];
            dst[3] = 255;
        }
    }
}

} // namespace reluva::glide
