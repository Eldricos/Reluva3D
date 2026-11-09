// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "common/glide/glide2.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace reluva::glide {

namespace {

// GR_RESOLUTION_* (sst1vid.h's values) by number.
constexpr int kResolutions[][2] = {
    {320, 200},  {320, 240},  {400, 256},   {512, 384},   {640, 200},   {640, 350},   {640, 400},
    {640, 480},  {800, 600},  {960, 720},   {856, 480},   {512, 256},   {1024, 768},  {1280, 1024},
    {1600, 1200}, {400, 300}, {1152, 864},  {1280, 960},  {1600, 1024}, {1792, 1344}, {1856, 1392},
    {1920, 1440}, {2048, 1536}, {2048, 2048},
};

constexpr uint32_t kStateMagic = 0x52474c53; // "SLGR": a GrState Reluva filled

} // namespace

Glide2::Glide2(Board board) : board_(board), pipeline_(board) {}

void Glide2::notImplemented(const std::string& function, const std::string& what) {
    if (noted_.insert(function).second && log)
        log("glide2x: " + function + ": " + what);
}

void Glide2::glideInit() {}

void Glide2::glideShutdown() {
    if (pipeline_.isOpen())
        pipeline_.close();
}

std::string Glide2::glideGetVersion() const {
    // NFS III's voodoo2a.dll wants at least 2.42 and rates up to 2.5 best
    // (its THRASH_is, 0x60001bf0); a Voodoo2 Glide 2.4x it is.
    return "Glide Version 2.46";
}

bool Glide2::sstQueryBoards(Glide2HwConfig& out) const {
    out.numSst = 1;
    return true;
}

bool Glide2::sstQueryHardware(Glide2HwConfig& out) const {
    out.numSst = 1;
    switch (board_.generation) {
    case Generation::Voodoo1: out.type = 0; break;
    case Generation::VoodooRush: out.type = 1; break;
    case Generation::Voodoo2: out.type = 3; break;
    default: out.type = 4; break; // GR_SSTTYPE_Banshee
    }
    out.fbRamMb = int(board_.frameBufferMemory >> 20);
    out.fbiRev = board_.fbiRevision;
    out.nTexelfx = board_.tmus;
    out.sliDetect = board_.sli;
    for (int i = 0; i < 2; ++i) {
        out.tmuRev[i] = board_.tmuRevision;
        out.tmuRamMb[i] = i < board_.tmus ? int(board_.tmuMemory >> 20) : 0;
    }
    return true;
}

void Glide2::sstSelect(int) {}

bool Glide2::resolutionSize(int resolution, int& width, int& height) {
    if (resolution < 0 || resolution >= int(std::size(kResolutions)))
        return false;
    width = kResolutions[resolution][0];
    height = kResolutions[resolution][1];
    return true;
}

bool Glide2::sstWinOpen(uint32_t, int resolution, int, int colorFormat, int origin, int colorBuffers, int auxBuffers) {
    if (pipeline_.isOpen())
        return false; // "If grSstWinOpen is called on an already open window, FXFALSE will be returned."
    int w = 0, h = 0;
    // GR_RESOLUTION_NONE (a window's own size) is for the Voodoo Rush only;
    // elsewhere it fails (Glide 2.4 Reference Manual, grSstWinOpen).
    if (!resolutionSize(resolution, w, h))
        return false;
    if (!pipeline_.open(w, h, colorBuffers, auxBuffers))
        return false;
    colorFormat_ = colorFormat & 3;
    pipeline_.state().originLowerLeft = origin == 1;
    active_ = true;
    return true;
}

void Glide2::sstWinClose() { pipeline_.close(); }

bool Glide2::sstControl(uint32_t code) {
    if (code == 1)
        active_ = true; // GR_CONTROL_ACTIVATE
    else if (code == 2)
        active_ = false; // GR_CONTROL_DEACTIVATE
    return true;
}

uint32_t Glide2::sstScreenWidth() const { return uint32_t(pipeline_.width()); }
uint32_t Glide2::sstScreenHeight() const { return uint32_t(pipeline_.height()); }

uint32_t Glide2::sstStatus() const {
    // Idle with empty FIFOs (Reference Manual, grSstStatus): PCI FIFO free
    // 0x3f, not in retrace, nothing busy, buffer 0 shown, memory FIFO free
    // 0xffff, no swaps pending.
    return 0x3fu | 0x40u | 0xffffu << 12;
}

bool Glide2::sstVRetraceOn() {
    // Alternates, so that a program waiting for the retrace to begin or to
    // end moves on.
    return (retraceCalls_++ & 1) != 0;
}

uint32_t Glide2::sstVideoLine() { return (retraceCalls_++ * 37) % uint32_t(std::max(pipeline_.height(), 1)); }

void Glide2::sstOrigin(int origin) { pipeline_.state().originLowerLeft = origin == 1; }

uint32_t Glide2::toArgb(uint32_t c) const {
    switch (colorFormat_) {
    case kColorAbgr: return (c & 0xff00ff00u) | (c >> 16 & 0xff) | (c & 0xff) << 16;
    case kColorRgba: return c >> 8 | c << 24;
    case kColorBgra: return (c & 0xff) << 24 | (c >> 8 & 0xff) << 16 | (c >> 16 & 0xff) << 8 | c >> 24;
    default: return c;
    }
}

Vertex Glide2::convert(const Glide2Vertex& v) const {
    Vertex o;
    o.x = v.x;
    o.y = v.y;
    o.ooz = v.ooz;
    o.oow = v.oow;
    o.r = v.r;
    o.g = v.g;
    o.b = v.b;
    o.a = v.a;
    for (int i = 0; i < 2; ++i) {
        o.tmu[size_t(i)].sow = v.tmuvtx[i].sow;
        o.tmu[size_t(i)].tow = v.tmuvtx[i].tow;
        o.tmu[size_t(i)].oow = v.tmuvtx[i].oow;
    }
    o.tmu[2] = o.tmu[1];
    return o;
}

// --- Drawing -------------------------------------------------------------------

void Glide2::drawTriangle(const Glide2Vertex& a, const Glide2Vertex& b, const Glide2Vertex& c) {
    pipeline_.triangle(convert(a), convert(b), convert(c));
    drawn();
}

void Glide2::drawLine(const Glide2Vertex& a, const Glide2Vertex& b) {
    pipeline_.line(convert(a), convert(b));
    drawn();
}

void Glide2::drawPoint(const Glide2Vertex& a) {
    pipeline_.point(convert(a));
    drawn();
}

void Glide2::drawPolygon(int count, const Glide2Vertex* v) {
    for (int i = 1; i + 1 < count; ++i)
        drawTriangle(v[0], v[i], v[i + 1]);
}

void Glide2::bufferClear(uint32_t color, int alpha, int depth) {
    pipeline_.clear(toArgb(color), alpha, depth);
    drawn();
}

void Glide2::bufferSwap(int) {
    pipeline_.swap();
    frontChanged_ = false;
    if (onSwap)
        onSwap();
}

void Glide2::renderBuffer(int buffer) {
    if (buffer == kBufferFront || buffer == kBufferBack)
        pipeline_.state().renderBuffer = buffer;
}

// --- State -----------------------------------------------------------------------

void Glide2::colorCombine(int function, int factor, int local, int other, bool invert) {
    pipeline_.state().color = {{function, factor, invert}, local, other};
}

void Glide2::alphaCombine(int function, int factor, int local, int other, bool invert) {
    pipeline_.state().alpha = {{function, factor, invert}, local, other};
}

void Glide2::alphaControlsITRGBLighting(bool enable) { pipeline_.state().alphaControlsLighting = enable; }

void Glide2::alphaBlendFunction(int rgbSrc, int rgbDst, int alphaSrc, int alphaDst) {
    State& s = pipeline_.state();
    s.blendRgbSrc = rgbSrc;
    s.blendRgbDst = rgbDst;
    s.blendAlphaSrc = alphaSrc;
    s.blendAlphaDst = alphaDst;
}

void Glide2::alphaTestFunction(int function) { pipeline_.state().alphaTest = function & 7; }
void Glide2::alphaTestReferenceValue(int value) { pipeline_.state().alphaRef = value & 0xff; }
void Glide2::chromakeyMode(int mode) { pipeline_.state().chromakey = mode != 0; }
void Glide2::chromakeyValue(uint32_t color) { pipeline_.state().chromakeyColor = toArgb(color) & 0xffffff; }

void Glide2::clipWindow(uint32_t minX, uint32_t minY, uint32_t maxX, uint32_t maxY) {
    // Values past the screen are ignored (Programming Guide, Clipping):
    // here, held to it.
    State& s = pipeline_.state();
    const uint32_t w = uint32_t(pipeline_.width()), h = uint32_t(pipeline_.height());
    s.clip[0] = int(std::min(minX, w));
    s.clip[1] = int(std::min(minY, h));
    s.clip[2] = int(std::min(maxX, w));
    s.clip[3] = int(std::min(maxY, h));
}

void Glide2::colorMask(bool rgb, bool alpha) {
    pipeline_.state().colorMask = rgb;
    pipeline_.state().alphaMask = alpha;
}

void Glide2::constantColorValue(uint32_t color) { pipeline_.state().constantColor = toArgb(color); }

void Glide2::constantColorValue4(float a, float r, float g, float b) {
    auto c = [](float v) { return uint32_t(std::clamp(int(v), 0, 255)); };
    pipeline_.state().constantColor = c(a) << 24 | c(r) << 16 | c(g) << 8 | c(b);
}

void Glide2::cullMode(int mode) { pipeline_.state().cull = mode; }
void Glide2::depthBiasLevel(int level) { pipeline_.state().depthBias = int(int16_t(level)); }
void Glide2::depthBufferFunction(int function) { pipeline_.state().depthFunc = function & 7; }
void Glide2::depthBufferMode(int mode) { pipeline_.state().depthMode = mode; }
void Glide2::depthMask(bool enable) { pipeline_.state().depthMask = enable; }

void Glide2::disableAllEffects() {
    // Blending, alpha test, chroma-key, fog and depth buffering off; clip,
    // dither and masks stay (Reference Manual, grDisableAllEffects).
    State& s = pipeline_.state();
    s.blendRgbSrc = s.blendAlphaSrc = kBlendOne;
    s.blendRgbDst = s.blendAlphaDst = kBlendZero;
    s.alphaTest = kCmpAlways;
    s.chromakey = false;
    s.fogMode = kFogDisable;
    s.depthMode = kDepthDisable;
}

void Glide2::ditherMode(int mode) { pipeline_.state().dither = mode; }
void Glide2::fogColorValue(uint32_t color) { pipeline_.state().fogColor = toArgb(color) & 0xffffff; }
void Glide2::fogMode(int mode) { pipeline_.state().fogMode = mode; }

void Glide2::fogTable(const uint8_t table[64]) {
    std::copy(table, table + 64, pipeline_.state().fogTable.begin());
}

void Glide2::gammaCorrectionValue(float gamma) {
    if (gamma > 0 && gamma <= 20)
        pipeline_.state().gamma = gamma;
}

void Glide2::hints(int type, uint32_t mask) {
    if (type == kHintStw)
        pipeline_.state().stwHints = mask;
}

uint32_t Glide2::glideGetState() {
    const uint32_t token = nextStateToken_++;
    savedStates_[token] = pipeline_.state();
    // Applications save and restore in pairs; the oldest go.
    while (savedStates_.size() > 64)
        savedStates_.erase(savedStates_.begin());
    return token ^ kStateMagic;
}

void Glide2::glideSetState(uint32_t token) {
    auto it = savedStates_.find(token ^ kStateMagic);
    if (it != savedStates_.end())
        pipeline_.state() = it->second;
    else
        notImplemented("grGlideSetState", "a GrState Reluva didn't fill: ignored");
}

// --- Textures --------------------------------------------------------------------

TextureInfo Glide2::textureInfo(int smallLod, int largeLod, int aspect, int format) {
    TextureInfo t;
    t.smallLod = lodToLog2(std::clamp(smallLod, 0, 8));
    t.largeLod = lodToLog2(std::clamp(largeLod, 0, 8));
    if (t.smallLod > t.largeLod)
        std::swap(t.smallLod, t.largeLod);
    t.aspect = aspectToLog2(std::clamp(aspect, 0, 6));
    t.format = format;
    return t;
}

uint32_t Glide2::texMinAddress(int) const { return 0; }

uint32_t Glide2::texMaxAddress(int) const {
    // The highest start address, valid for a 1x1 level (Reference Manual):
    // the last 8-byte unit.
    return pipeline_.tmuMemorySize() - 8;
}

uint32_t Glide2::texCalcMemRequired(int smallLod, int largeLod, int aspect, int format) const {
    return textureBytes(textureInfo(smallLod, largeLod, aspect, format), kLevelsBoth);
}

uint32_t Glide2::texTextureMemRequired(int evenOdd, const Glide2TexInfo& info) const {
    return textureBytes(textureInfo(info.smallLod, info.largeLod, info.aspectRatio, info.format), evenOdd);
}

uint32_t Glide2::levelDataBytes(int lod, int aspect, int format) {
    int w = 0, h = 0;
    levelSize(lodToLog2(std::clamp(lod, 0, 8)), aspectToLog2(std::clamp(aspect, 0, 6)), w, h);
    return uint32_t(w * h * texelBytes(format));
}

uint32_t Glide2::mipmapDataBytes(const Glide2TexInfo& info) {
    const TextureInfo t = textureInfo(info.smallLod, info.largeLod, info.aspectRatio, info.format);
    uint32_t bytes = 0;
    for (int l = t.largeLod; l >= t.smallLod; --l) {
        int w = 0, h = 0;
        levelSize(l, t.aspect, w, h);
        bytes += uint32_t(w * h * texelBytes(t.format));
    }
    return bytes;
}

void Glide2::texSource(int tmu, uint32_t start, int evenOdd, const Glide2TexInfo& info) {
    if (tmu < 0 || tmu >= board_.tmus)
        return;
    TmuState& t = pipeline_.state().tmus[size_t(tmu)];
    t.hasSource = true;
    t.start = start;
    t.evenOdd = evenOdd & 3 ? evenOdd & 3 : kLevelsBoth;
    t.info = textureInfo(info.smallLod, info.largeLod, info.aspectRatio, info.format);
    if (!knownFormat(info.format))
        notImplemented("grTexSource format " + std::to_string(info.format), "a reserved format");
    // The source's own address stands for GR_TEXBASE_256 (Reference
    // Manual, grTexMultibaseAddress).
    t.multibaseAddress[0] = start;
}

void Glide2::texDownloadMipMap(int tmu, uint32_t start, int evenOdd, const Glide2TexInfo& info, const uint8_t* data) {
    // The data holds every level, largest first; the levels evenOdd names
    // go to this TMU.
    const TextureInfo t = textureInfo(info.smallLod, info.largeLod, info.aspectRatio, info.format);
    const int eo = evenOdd & 3 ? evenOdd & 3 : kLevelsBoth;
    uint32_t at = 0;
    for (int l = t.largeLod; l >= t.smallLod; --l) {
        int w = 0, h = 0;
        levelSize(l, t.aspect, w, h);
        if (levelIncluded(l, eo))
            pipeline_.downloadLevel(tmu, start, t, eo, l, data + at, 0, h - 1);
        at += uint32_t(w * h * texelBytes(t.format));
    }
}

void Glide2::texDownloadMipMapLevel(int tmu, uint32_t start, int thisLod, int largeLod, int aspect, int format,
                                    int evenOdd, const uint8_t* data) {
    texDownloadMipMapLevelPartial(tmu, start, thisLod, largeLod, aspect, format, evenOdd, data, 0, 255);
}

void Glide2::texDownloadMipMapLevelPartial(int tmu, uint32_t start, int thisLod, int largeLod, int aspect, int format,
                                           int evenOdd, const uint8_t* data, int firstRow, int lastRow) {
    // The start address is the mipmap's (its largest level's); the data
    // the level's rows from firstRow.
    TextureInfo t = textureInfo(thisLod, largeLod, aspect, format);
    const int eo = evenOdd & 3 ? evenOdd & 3 : kLevelsBoth;
    pipeline_.downloadLevel(tmu, start, t, eo, lodToLog2(std::clamp(thisLod, 0, 8)), data, firstRow, lastRow);
}

void Glide2::texDownloadTable(int tmu, int type, const uint8_t* data) {
    texDownloadTablePartial(tmu, type, data, 0, 255);
}

void Glide2::texDownloadTablePartial(int tmu, int type, const uint8_t* data, int first, int last) {
    if (type == kTablePalette) {
        uint32_t entries[256];
        first = std::clamp(first, 0, 255);
        last = std::clamp(last, first, 255);
        for (int i = first; i <= last; ++i)
            std::memcpy(&entries[i - first], data + 4 * i, 4);
        pipeline_.setPalette(tmu, first, last, entries);
        return;
    }
    // GuNccTable: yRGB[16] bytes, then iRGB[4][3] and qRGB[4][3] as signed
    // 16-bit values.
    NccTable t;
    std::memcpy(t.y, data, 16);
    std::memcpy(t.i, data + 16, 24);
    std::memcpy(t.q, data + 40, 24);
    pipeline_.setNcc(tmu, type & 1, t);
}

void Glide2::texNCCTable(int tmu, int table) {
    if (tmu >= 0 && tmu < kMaxTmus)
        pipeline_.state().tmus[size_t(tmu)].nccTable = table & 1;
}

void Glide2::texClampMode(int tmu, int s, int t) {
    if (tmu < 0 || tmu >= kMaxTmus)
        return;
    pipeline_.state().tmus[size_t(tmu)].clampS = s == kClampClamp;
    pipeline_.state().tmus[size_t(tmu)].clampT = t == kClampClamp;
}

void Glide2::texCombine(int tmu, int rgbFunction, int rgbFactor, int alphaFunction, int alphaFactor, bool rgbInvert,
                        bool alphaInvert) {
    if (tmu < 0 || tmu >= kMaxTmus)
        return;
    TmuState& t = pipeline_.state().tmus[size_t(tmu)];
    t.rgb = {rgbFunction, rgbFactor, rgbInvert};
    t.alpha = {alphaFunction, alphaFactor, alphaInvert};
}

void Glide2::texCombineFunction(int tmu, int function) {
    // GR_TEXTURECOMBINE_*, as the Reference Manual's guTexCombineFunction
    // describes each, put as grTexCombine settings (Reluva's reading).
    int fn = kFuncLocal, rgbFactor = kFactorZero, alphaFactor = kFactorZero;
    bool invert = false;
    switch (function) {
    case 0x0: fn = kFuncZero; break;                                        // ZERO
    case 0x1: fn = kFuncLocal; break;                                       // DECAL
    case 0x2: fn = kFuncScaleOther; rgbFactor = alphaFactor = kFactorOne; break; // OTHER
    case 0x3: fn = kFuncScaleOtherAddLocal; rgbFactor = alphaFactor = kFactorOne; break; // ADD
    case 0x4: fn = kFuncScaleOther; rgbFactor = kFactorLocal; alphaFactor = kFactorLocalAlpha; break; // MULTIPLY
    case 0x5: fn = kFuncScaleOtherMinusLocal; rgbFactor = alphaFactor = kFactorOne; break; // SUBTRACT
    case 0x6: fn = kFuncScaleOtherMinusLocalAddLocal; rgbFactor = alphaFactor = kFactorTextureAlpha; break; // DETAIL
    case 0x7: fn = kFuncScaleOtherMinusLocalAddLocal; rgbFactor = alphaFactor = kFactorOneMinusTextureAlpha; break;
    case 0x8: fn = kFuncScaleOtherMinusLocalAddLocal; rgbFactor = alphaFactor = kFactorTextureRgb; break; // TRILINEAR_ODD
    case 0x9: fn = kFuncScaleOtherMinusLocalAddLocal; rgbFactor = alphaFactor = kFactorOneMinusLodFraction; break;
    case 0xa: fn = kFuncZero; invert = true; break; // ONE
    default: notImplemented("grTexCombineFunction " + std::to_string(function), "unknown function"); return;
    }
    texCombine(tmu, fn, rgbFactor, fn, alphaFactor, invert, invert);
}

void Glide2::texDetailControl(int tmu, int lodBias, int detailScale, float detailMax) {
    if (tmu < 0 || tmu >= kMaxTmus)
        return;
    TmuState& t = pipeline_.state().tmus[size_t(tmu)];
    t.detailBias = lodBias;
    t.detailScale = std::clamp(detailScale, 0, 7);
    t.detailMax = std::clamp(detailMax, 0.0f, 1.0f);
}

void Glide2::texFilterMode(int tmu, int minFilter, int magFilter) {
    if (tmu < 0 || tmu >= kMaxTmus)
        return;
    pipeline_.state().tmus[size_t(tmu)].minFilter = minFilter;
    pipeline_.state().tmus[size_t(tmu)].magFilter = magFilter;
}

void Glide2::texLodBiasValue(int tmu, float bias) {
    if (tmu >= 0 && tmu < kMaxTmus) // rounded to the nearest quarter
        pipeline_.state().tmus[size_t(tmu)].lodBias = std::round(std::clamp(bias, -8.0f, 7.75f) * 4) / 4;
}

void Glide2::texMipMapMode(int tmu, int mode, bool lodBlend) {
    if (tmu < 0 || tmu >= kMaxTmus)
        return;
    pipeline_.state().tmus[size_t(tmu)].mipmapMode = mode;
    pipeline_.state().tmus[size_t(tmu)].lodBlend = lodBlend;
}

void Glide2::texMultibase(int tmu, bool enable) {
    if (tmu >= 0 && tmu < kMaxTmus)
        pipeline_.state().tmus[size_t(tmu)].multibase = enable;
}

void Glide2::texMultibaseAddress(int tmu, int range, uint32_t start, int, const Glide2TexInfo&) {
    if (tmu >= 0 && tmu < kMaxTmus && range >= 0 && range < 4)
        pipeline_.state().tmus[size_t(tmu)].multibaseAddress[size_t(range)] = start;
}

void Glide2::guColorCombineFunction(int function) {
    // GR_COLORCOMBINE_*, as the Reference Manual's guColorCombineFunction
    // describes each, put as grColorCombine settings (Reluva's reading).
    auto set = [&](int fn, int factor, int local, int other, bool invert = false) {
        colorCombine(fn, factor, local, other, invert);
    };
    switch (function) {
    case 0x0: set(kFuncZero, kFactorZero, kLocalConstant, kOtherConstant); break;
    case 0x1: set(kFuncLocal, kFactorZero, kLocalConstant, kOtherConstant); break; // CCRGB
    case 0x2: // ITRGB
    case 0x3: set(kFuncLocal, kFactorZero, kLocalIterated, kOtherConstant); break; // ITRGB_DELTA0
    case 0x4: set(kFuncScaleOther, kFactorOne, kLocalConstant, kOtherTexture); break; // DECAL_TEXTURE
    case 0x5: set(kFuncScaleOther, kFactorLocal, kLocalConstant, kOtherTexture); break; // TEXTURE_TIMES_CCRGB
    case 0x6:
    case 0x7: set(kFuncScaleOther, kFactorLocal, kLocalIterated, kOtherTexture); break; // TEXTURE_TIMES_ITRGB(_DELTA0)
    case 0x8: set(kFuncScaleOtherAddLocalAlpha, kFactorLocal, kLocalIterated, kOtherTexture); break;
    case 0x9: set(kFuncScaleOther, kFactorLocalAlpha, kLocalIterated, kOtherTexture); break; // TEXTURE_TIMES_ALPHA
    case 0xa: // TEXTURE_TIMES_ALPHA_ADD_ITRGB
    case 0xe: set(kFuncScaleOtherAddLocal, kFactorLocalAlpha, kLocalIterated, kOtherTexture); break; // DIFF_SPEC_A
    case 0xb: set(kFuncScaleOtherAddLocal, kFactorOne, kLocalIterated, kOtherTexture); break; // TEXTURE_ADD_ITRGB
    case 0xc: set(kFuncScaleOtherMinusLocal, kFactorOne, kLocalIterated, kOtherTexture); break; // TEXTURE_SUB_ITRGB
    case 0xd: set(kFuncScaleOtherMinusLocalAddLocal, kFactorTextureAlpha, kLocalConstant, kOtherIterated); break;
    case 0xf: set(kFuncScaleOtherAddLocalAlpha, kFactorLocal, kLocalIterated, kOtherTexture); break; // DIFF_SPEC_B
    case 0x10: set(kFuncZero, kFactorZero, kLocalConstant, kOtherConstant, true); break; // ONE
    default: notImplemented("guColorCombineFunction " + std::to_string(function), "unknown function"); break;
    }
}

void Glide2::guAlphaSource(int mode) {
    switch (mode) {
    case 0x0: alphaCombine(kFuncLocal, kFactorZero, kLocalConstant, kOtherConstant, false); break; // CC_ALPHA
    case 0x1: alphaCombine(kFuncLocal, kFactorZero, kLocalIterated, kOtherConstant, false); break; // ITERATED
    case 0x2: alphaCombine(kFuncScaleOther, kFactorOne, kLocalConstant, kOtherTexture, false); break; // TEXTURE
    case 0x3: alphaCombine(kFuncScaleOther, kFactorLocal, kLocalIterated, kOtherTexture, false); break;
    default: notImplemented("guAlphaSource " + std::to_string(mode), "unknown mode"); break;
    }
}

// --- The linear frame buffer -----------------------------------------------------

bool Glide2::lfbLock(int type, int buffer, int writeMode, int origin, bool pixelPipeline, Glide2LfbLock& out) {
    if (!pipeline_.isOpen() || !pipeline_.buffer(buffer))
        return false;
    pipeline_.sync();
    const bool aux = buffer == kBufferAux || buffer == kBufferDepth || buffer == kBufferAlpha;
    out.buffer = buffer;
    if ((type & 1) == 0) // reads are always 16-bit (Voodoo2 specification, Linear frame buffer Reads)
        writeMode = aux ? kLfbZA16 : kLfb565;
    else if (writeMode == kLfbAny)
        writeMode = aux ? kLfbZA16 : kLfb565;
    switch (writeMode) {
    case kLfb565:
    case kLfb555:
    case kLfb1555:
    case kLfbZA16: out.bytesPerPixel = 2; break;
    case kLfb888:
    case kLfb8888:
    case kLfb565Depth:
    case kLfb555Depth:
    case kLfb1555Depth: out.bytesPerPixel = 4; break;
    default: return false; // a mode the board doesn't take: the lock fails
    }
    if (aux != (writeMode == kLfbZA16))
        return false;
    out.writeMode = writeMode;
    out.originLowerLeft = origin == 1;
    out.pixelPipeline = pixelPipeline && (type & 1);
    out.strideBytes = 1024 * out.bytesPerPixel;
    ++locks_;
    return true;
}

bool Glide2::lfbUnlock(int, int) {
    locks_ = std::max(0, locks_ - 1);
    return true;
}

void Glide2::lfbConstantAlpha(int alpha) { pipeline_.state().lfbConstantAlpha = alpha & 0xff; }
void Glide2::lfbConstantDepth(int depth) { pipeline_.state().lfbConstantDepth = depth & 0xffff; }

void Glide2::lfbWrite(const Glide2LfbLock& lock, int x, int y, uint32_t value) {
    const int row = lock.originLowerLeft ? pipeline_.height() - 1 - y : y;
    uint32_t v = value;
    if (lock.bytesPerPixel == 4 && (lock.writeMode == kLfb888 || lock.writeMode == kLfb8888))
        v = toArgb(value);
    pipeline_.lfbWrite(lock.buffer, x, row, lock.writeMode, v, lock.pixelPipeline);
    frontChanged_ |= lock.buffer == kBufferFront;
}

uint16_t Glide2::lfbRead(int buffer, int x, int y, bool originLowerLeft) const {
    const uint16_t* b = pipeline_.buffer(buffer);
    if (!b || x < 0 || y < 0 || x >= pipeline_.width() || y >= pipeline_.height())
        return 0;
    const int row = originLowerLeft ? pipeline_.height() - 1 - y : y;
    return b[size_t(row) * size_t(pipeline_.width()) + size_t(x)];
}

bool Glide2::lfbWriteRegion(int buffer, uint32_t x, uint32_t y, int format, uint32_t width, uint32_t height,
                            int stride, const uint8_t* data, size_t dataBytes) {
    if (!pipeline_.isOpen() || !pipeline_.buffer(buffer))
        return false;
    pipeline_.sync();
    const int bpp = format == kLfb565 || format == kLfb555 || format == kLfb1555 || format == kLfbZA16 ? 2
                    : format == kLfbRle16                                                            ? 0
                                                                                                     : 4;
    if (format == kLfbRle16) {
        // A 16-bit count, then a 16-bit colour (Reference Manual,
        // grLfbWriteRegion): runs across the rows, stride ignored.
        notImplemented("grLfbWriteRegion RLE16", "runs read across rows as Reluva's reading");
        uint32_t px = 0, py = 0;
        const uint8_t* p = data;
        const uint8_t* end = dataBytes == SIZE_MAX ? nullptr : data + dataBytes;
        while (py < height && (!end || end - p >= 4)) {
            const int16_t count = int16_t(p[0] | p[1] << 8);
            const uint16_t color = uint16_t(p[2] | p[3] << 8);
            p += 4;
            for (int i = 0; i < count && py < height; ++i) {
                pipeline_.lfbWrite(buffer, int(x + px), int(y + py), kLfb565, color, false);
                if (++px == width) {
                    px = 0;
                    ++py;
                }
            }
            if (count <= 0 && px == 0 && py == 0)
                break; // nothing usable
        }
        return true;
    }
    if (format != kLfb565 && format != kLfb555 && format != kLfb1555 && format != kLfb888 && format != kLfb8888 &&
        format != kLfb565Depth && format != kLfb555Depth && format != kLfb1555Depth && format != kLfbZA16)
        return false;
    for (uint32_t row = 0; row < height; ++row) {
        const uint8_t* p = data + int64_t(row) * stride;
        for (uint32_t col = 0; col < width; ++col) {
            uint32_t v = 0;
            std::memcpy(&v, p + size_t(col) * size_t(bpp), size_t(bpp));
            if (format == kLfb888 || format == kLfb8888)
                v = toArgb(v);
            pipeline_.lfbWrite(buffer, int(x + col), int(y + row), format, v, false);
        }
    }
    frontChanged_ |= buffer == kBufferFront;
    return true;
}

bool Glide2::lfbReadRegion(int buffer, uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t stride,
                           uint8_t* data) {
    if (!pipeline_.isOpen() || !pipeline_.buffer(buffer) || stride < width * 2)
        return false;
    pipeline_.sync();
    for (uint32_t row = 0; row < height; ++row)
        for (uint32_t col = 0; col < width; ++col) {
            const uint16_t v = lfbRead(buffer, int(x + col), int(y + row), false);
            std::memcpy(data + size_t(row) * stride + size_t(col) * 2, &v, 2);
        }
    return true;
}

// --- Statistics ------------------------------------------------------------------

void Glide2::sstPerfStats(uint32_t out[5]) const {
    const PixelStats& s = pipeline_.stats;
    const uint64_t v[5] = {s.pixelsIn, s.chromaFail, s.depthFail, s.alphaFail, s.pixelsOut};
    for (int i = 0; i < 5; ++i)
        out[i] = uint32_t((v[i] - statsBase_[i]) & 0xffffff); // 24-bit counters
}

void Glide2::sstResetPerfStats() {
    const PixelStats& s = pipeline_.stats;
    const uint64_t v[5] = {s.pixelsIn, s.chromaFail, s.depthFail, s.alphaFail, s.pixelsOut};
    std::copy(v, v + 5, statsBase_);
}

void Glide2::triStats(uint32_t& processed, uint32_t& drawn) const {
    processed = uint32_t(pipeline_.stats.trianglesIn - triBase_[0]);
    drawn = uint32_t(pipeline_.stats.trianglesOut - triBase_[1]);
}

void Glide2::resetTriStats() {
    triBase_[0] = pipeline_.stats.trianglesIn;
    triBase_[1] = pipeline_.stats.trianglesOut;
}

} // namespace reluva::glide
