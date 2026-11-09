// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Glide 3 (glide3x.dll's API) on the pipeline core (docs/engines/glide.md,
// The Glide 3 front end): each function with the meaning the Glide 3.0
// Reference Manual and Programming Guide give it, taking host values and
// pointers. glide3_x86.h puts a guest's calls through it.
//
// Most of Glide 3's state functions mean what Glide 2's did, with the same
// values; for those it uses a Glide 2 front end over the same pipeline
// (core()). What Glide 3 changed is here: vertices described by
// grVertexLayout rather than a GrVertex, window or clip coordinates,
// grGet/grGetString, grEnable/grDisable, log2 LOD and aspect constants, one
// palette and two NCC tables for all TMUs, the fog modes' numbers and a
// fog table of the board's size, per-channel gamma, contexts.

#include "common/glide/glide2.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace reluva::glide {

// Glide 3's GrTexInfo without its data pointer: log2 LODs and aspect.
struct Glide3TexInfo {
    int smallLodLog2 = 0, largeLodLog2 = 8, aspectRatioLog2 = 0, format = 0;
};

// One of grQueryResolutions' GrResolution records.
struct Glide3Resolution {
    int resolution = 0, refresh = 0, numColorBuffers = 2, numAuxBuffers = 1;
};

class Glide3 {
public:
    explicit Glide3(Board board = voodoo3Board());

    // The Glide 2 front end the unchanged functions go through.
    Glide2& core() { return core_; }
    Pipeline& pipeline() { return core_.pipeline(); }
    const Board& board() const { return board_; }
    void notImplemented(const std::string& function, const std::string& what = "not implemented") {
        core_.notImplemented(function, what);
    }

    // --- Start-up, the board ------------------------------------------------
    // grGet: writes up to `bytes` / 4 values into `out`; returns the bytes
    // written, 0 for a query not answered (Reference Manual, grGet).
    uint32_t get(uint32_t pname, uint32_t bytes, int32_t* out);
    // grGetString. The hardware name is what NFS III's Voodoo3 patch DLL
    // compares against for each board (RE, docs/engines/glide.md).
    std::string getString(uint32_t pname) const;
    // grQueryResolutions: every configuration matching the template
    // (g3::kQueryAny for any value in a field).
    std::vector<Glide3Resolution> queryResolutions(const Glide3Resolution& query) const;
    // grSstWinOpen: the context (nonzero), or 0.
    uint32_t sstWinOpen(uint32_t hwnd, int resolution, int refresh, int colorFormat, int origin, int colorBuffers,
                        int auxBuffers);
    void sstWinClose(uint32_t context);
    bool selectContext(uint32_t context);
    void enable(uint32_t mode);
    void disable(uint32_t mode);
    bool enabled(uint32_t mode) const { return mode < 8 && enabled_[mode]; }

    // --- Vertices -------------------------------------------------------------
    void coordinateSpace(uint32_t mode);
    void viewport(int x, int y, int width, int height);
    void depthRange(float nearValue, float farValue);
    // grVertexLayout: `param` at `offset` bytes from each vertex's pointer.
    void vertexLayout(uint32_t param, int32_t offset, uint32_t mode);
    // How far before and after a vertex's pointer the layout reads.
    int layoutMin() const;
    int layoutMax() const;
    // A vertex read by the layout from host memory (`at` is the vertex's
    // pointer, valid from layoutMin() to layoutMax()), in window
    // coordinates as the pipeline takes them. Parameters the layout leaves
    // out keep their last values (Reference Manual, grVertexLayout).
    Vertex readVertex(const uint8_t* at);
    // The layout and coordinate space as grGlideGetVertexLayout saves them.
    struct Layout {
        std::array<int32_t, 0x60> offset{};
        std::array<bool, 0x60> enabled{};
        uint32_t space = 0;
    };
    const Layout& layout() const { return layout_; }
    void setLayout(const Layout& l) { layout_ = l; }

    // --- Drawing ----------------------------------------------------------------
    void drawTriangle(const uint8_t* a, const uint8_t* b, const uint8_t* c);
    void drawLine(const uint8_t* a, const uint8_t* b);
    void drawPoint(const uint8_t* a);
    // grDrawVertexArray(Contiguous): `vertices` the pointers in order.
    void drawVertexArray(uint32_t mode, const std::vector<const uint8_t*>& vertices);

    // --- State Glide 3 changed --------------------------------------------------
    void fogMode(int mode);
    // grFogTable: grGet(GR_FOG_TABLE_ENTRIES) entries, resampled to the
    // core's 64 when the board has more.
    void fogTable(const uint8_t* table);
    void gammaCorrectionRgb(float red, float green, float blue);
    void texMipMapMode(int tmu, int mode, bool lodBlend);

    // --- Textures --------------------------------------------------------------
    uint32_t texCalcMemRequired(int smallLodLog2, int largeLodLog2, int aspectLog2, int format) const;
    uint32_t texTextureMemRequired(int evenOdd, const Glide3TexInfo& info) const;
    static uint32_t mipmapDataBytes(const Glide3TexInfo& info);
    static uint32_t levelDataBytes(int lodLog2, int aspectLog2, int format);
    void texSource(int tmu, uint32_t start, int evenOdd, const Glide3TexInfo& info);
    void texDownloadMipMap(int tmu, uint32_t start, int evenOdd, const Glide3TexInfo& info, const uint8_t* data);
    void texDownloadMipMapLevelPartial(int tmu, uint32_t start, int thisLodLog2, int largeLodLog2, int aspectLog2,
                                       int format, int evenOdd, const uint8_t* data, int firstRow, int lastRow);
    // One palette and two NCC tables for every TMU (Reference Manual,
    // grTexDownloadTable).
    void texDownloadTablePartial(int type, const uint8_t* data, int first, int last);
    void texNCCTable(int table);
    void texMultibaseAddress(int tmu, int range, uint32_t start);

    // grGlideGetState/SetState, as Glide 2's (a number kept in the state
    // buffer), with the vertex layout and enables too.
    uint32_t glideGetState();
    void glideSetState(uint32_t token);

private:
    static Glide2TexInfo toGlide2(const Glide3TexInfo& info);

    Board board_;
    Glide2 core_;
    Layout layout_;
    Vertex last_; // the values parameters left out keep
    int viewport_[4] = {0, 0, 640, 480};
    float depthNear_ = 0, depthFar_ = 1;
    std::array<bool, 8> enabled_{};
    std::array<int, kMaxTmus> mipmapAsked_{}; // grTexMipMapMode's mode, before GR_ALLOW_MIPMAP_DITHER
    std::array<bool, kMaxTmus> lodBlend_{};
    uint32_t context_ = 0;
    struct Saved {
        Layout layout;
        std::array<bool, 8> enabled{};
    };
    std::map<uint32_t, Saved> saved_;
    // For strips and fans continued by the next call.
    std::vector<Vertex> strip_;
    uint32_t lastMode_ = 0xffffffffu;
    size_t stripParity_ = 0;
};

} // namespace reluva::glide
