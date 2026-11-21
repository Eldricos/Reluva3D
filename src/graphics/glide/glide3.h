// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// Glide 3 (glide3x.dll's API) on the pipeline core (docs/common/graphics/glide.md,
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

#include "graphics/glide/glide2.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace reluva::glide {

/** Glide 3's GrTexInfo without its data pointer: log2 LODs and aspect. */
struct Glide3TexInfo {
    int smallLodLog2 = 0;
    int largeLodLog2 = 8;
    int aspectRatioLog2 = 0;
    int format = 0;
};

/** One of grQueryResolutions' GrResolution records. */
struct Glide3Resolution {
    int resolution = 0;
    int refresh = 0;
    int numColorBuffers = 2;
    int numAuxBuffers = 1;
};

/**
 * Glide 3's functions, named as Glide 2's are (glide2.h), on a Glide 2
 * front end of their own: what this file's opening comment says Glide 3
 * changed is here, the rest is core()'s.
 */
class Glide3 {
public:
    /** Glide 3 for the board (by default the Voodoo3), its window not yet open. */
    explicit Glide3(Board board = voodoo3Board());

    /** The Glide 2 front end the unchanged functions go through. */
    Glide2 &core() { return _core; }
    /** The pipeline it draws on (core()'s). */
    Pipeline &pipeline() { return _core.pipeline(); }
    /** The board it stands in for. */
    const Board &board() const { return _board; }
    /** Notes, once, that `function` was called but isn't (fully) carried out (Glide2::notImplemented). */
    void notImplemented(const std::string &function, const std::string &what = "not implemented") {
        _core.notImplemented(function, what);
    }

    // Start-up, the board.

    /**
     * grGet: writes up to `bytes` / 4 values into `out` (Reference Manual,
     * grGet).
     * @return The bytes written; 0 for a query not answered, or if `out` is
     *         null or `bytes` too few for the answer.
     */
    uint32_t get(uint32_t pname, uint32_t bytes, int32_t *out);
    /**
     * grGetString (an empty string for an unknown query). The hardware name
     * is what NFS III's Voodoo3 patch DLL compares against for each board
     * (RE, docs/common/graphics/glide.md); the extensions the board's
     * (Board::extensions).
     */
    std::string getString(uint32_t pname) const;
    /**
     * Whether grGetProcAddress gives `procName`: an extension's procedure
     * (Reference Manual, grGetProcAddress's table) on a board with it.
     */
    bool offersProc(const std::string &procName) const;
    /**
     * grQueryResolutions: every configuration matching the template
     * (g3::kQueryAny for any value in a field) that the board's size and
     * memory allow.
     */
    std::vector<Glide3Resolution> queryResolutions(const Glide3Resolution &query) const;
    /** grSstWinOpen (as Glide2::sstWinOpen): the new context (nonzero), or 0 if it fails. */
    uint32_t sstWinOpen(uint32_t hwnd, int resolution, int refresh, int colorFormat, int origin, int colorBuffers,
                        int auxBuffers);
    /** grSstWinClose: closes the window, whatever the context. */
    void sstWinClose(uint32_t context);
    /** grSelectContext: whether `context` is the last one sstWinOpen gave. */
    bool selectContext(uint32_t context) const;
    /**
     * grEnable and grDisable (g3::EnableMode). GR_ALLOW_MIPMAP_DITHER
     * changes the TMUs' mipmap modes at once (texMipMapMode).
     */
    ///@{
    void enable(uint32_t mode);
    void disable(uint32_t mode);
    ///@}
    /** Whether a mode is enabled (false for an unknown one). */
    bool enabled(uint32_t mode) const { return mode < 8 && _enabled[mode]; }

    // Vertices.

    /**
     * grCoordinateSpace, grViewport and grDepthRange: how clip coordinates
     * become window ones. grSstWinOpen sets the viewport to the window.
     */
    ///@{
    void coordinateSpace(uint32_t mode);
    void viewport(int x, int y, int width, int height);
    void depthRange(float nearValue, float farValue);
    ///@}
    /**
     * grVertexLayout: `param` at `offset` bytes from each vertex's pointer,
     * or left out with g3::kParamDisable. An unknown parameter is ignored.
     */
    void vertexLayout(uint32_t param, int32_t offset, uint32_t mode);
    /** How far before and after a vertex's pointer the layout reads, in bytes. */
    ///@{
    int layoutMin() const;
    int layoutMax() const;
    ///@}
    /**
     * A vertex read by the layout from host memory (`at` is the vertex's
     * pointer, valid from layoutMin() to layoutMax()), in window
     * coordinates as the pipeline takes them. Parameters the layout leaves
     * out keep their last values (Reference Manual, grVertexLayout).
     */
    Vertex readVertex(const uint8_t *at);
    /** The layout and coordinate space as grGlideGetVertexLayout saves them. */
    struct Layout {
        std::array<int32_t, 0x60> offset{};
        std::array<bool, 0x60> enabled{};
        uint32_t space = 0;
    };
    /** The layout, as grGlideGetVertexLayout and grGlideSetVertexLayout read and set it. */
    ///@{
    const Layout &layout() const { return _layout; }
    void setLayout(const Layout &l) { _layout = l; }
    ///@}

    // Drawing.

    /**
     * grDrawTriangle, grDrawLine and grDrawPoint: each vertex read by the
     * layout from its pointer; in clip coordinates, clipped against the
     * view volume; anti-aliased while GR_AA_ORDERED is enabled.
     */
    ///@{
    void drawTriangle(const uint8_t *a, const uint8_t *b, const uint8_t *c);
    void drawLine(const uint8_t *a, const uint8_t *b);
    void drawPoint(const uint8_t *a);
    ///@}
    /**
     * grAADrawTriangle: the edges asked for anti-aliased, whether or not
     * GR_AA_ORDERED is on ("independent of the GR_AA_ORDERED mode").
     */
    void aaDrawTriangle(const uint8_t *a, const uint8_t *b, const uint8_t *c, bool ab, bool bc, bool ca);
    /**
     * grDrawVertexArray(Contiguous): `vertices` the pointers in order, `mode`
     * a g3::Primitive. With GR_AA_ORDERED on, everything but strips and fans
     * is anti-aliased.
     */
    void drawVertexArray(uint32_t mode, const std::vector<const uint8_t *> &vertices);

    // State Glide 3 changed.

    /**
     * grFogMode in Glide 3's numbers (g3::FogMode, with kFogMult2 or
     * kFogAdd2); GR_FOG_WITH_TABLE_ON_FOGCOORD_EXT, not offered, is taken as
     * the table on q.
     */
    void fogMode(int mode);
    /**
     * grFogTable: grGet(GR_FOG_TABLE_ENTRIES) entries, resampled to the
     * core's 64 when the board has another number.
     */
    void fogTable(const uint8_t *table);
    /**
     * guGammaCorrectionRGB: each channel's gamma, from above 0 to 20; if one
     * isn't, none is set.
     */
    void gammaCorrectionRgb(float red, float green, float blue);
    /**
     * grTexMipMapMode: GR_MIPMAP_NEAREST_DITHER only while
     * GR_ALLOW_MIPMAP_DITHER is enabled, nearest otherwise.
     */
    void texMipMapMode(int tmu, int mode, bool lodBlend);

    // Textures.

    /** Glide 2's functions of the same names, in Glide 3's log2 LODs and aspect ratios. */
    ///@{
    uint32_t texCalcMemRequired(int smallLodLog2, int largeLodLog2, int aspectLog2, int format) const;
    uint32_t texTextureMemRequired(int evenOdd, const Glide3TexInfo &info) const;
    static uint32_t mipmapDataBytes(const Glide3TexInfo &info);
    static uint32_t levelDataBytes(int lodLog2, int aspectLog2, int format);
    void texSource(int tmu, uint32_t start, int evenOdd, const Glide3TexInfo &info);
    void texDownloadMipMap(int tmu, uint32_t start, int evenOdd, const Glide3TexInfo &info, const uint8_t *data);
    void texDownloadMipMapLevelPartial(int tmu, uint32_t start, int thisLodLog2, int largeLodLog2, int aspectLog2,
                                       int format, int evenOdd, const uint8_t *data, int firstRow, int lastRow);
    ///@}
    /**
     * grTexDownloadTablePartial and grTexNCCTable: one palette and two NCC
     * tables for every TMU (Reference Manual, grTexDownloadTable).
     */
    ///@{
    void texDownloadTablePartial(int type, const uint8_t *data, int first, int last);
    void texNCCTable(int table);
    ///@}
    /** grTexMultibaseAddress, its ranges in Glide 3's numbers (g3::TexBase). */
    void texMultibaseAddress(int tmu, int range, uint32_t start);

    /**
     * grGlideGetState/SetState, as Glide 2's (a number kept in the state
     * buffer), with the vertex layout and enables too.
     */
    ///@{
    uint32_t glideGetState();
    void glideSetState(uint32_t token);
    ///@}

    /**
     * A vertex as the layout gives it before the division by w: clip
     * coordinates (or, `window`, a window-coordinate vertex as drawn).
     */
    struct ClipVertex {
        bool window = false;
        Vertex v; // colours and the parameters the layout leaves out
        float x = 0;
        float y = 0;
        float z = 0;
        float w = 1;
        float q = 1;
        bool hasZ = false;
        float s[kMaxTmus] = {};
        float t[kMaxTmus] = {};
        float qi[kMaxTmus] = {1, 1, 1};
    };

private:
    ClipVertex readClip(const uint8_t *at);
    Vertex project(const ClipVertex &c) const;
    // Drawn after clipping against the view volume (clip coordinates),
    // anti-aliased if `aa`.
    void clipTriangle(const ClipVertex &a, const ClipVertex &b, const ClipVertex &c, bool aa);
    // An anti-aliased convex polygon (a triangle, GR_POLYGON) clipped
    // against the view volume and drawn as a fan with a fringe outside edge
    // i (vertex i to i + 1) where edges[i] is set; the edges clipping makes
    // have one too (docs/common/graphics/glide.md, Clip coordinates).
    void clipAaPolygon(std::vector<ClipVertex> polygon, std::vector<bool> edges);
    void clipLine(const ClipVertex &a, const ClipVertex &b, bool aa);
    void clipPoint(const ClipVertex &a, bool aa);
    static Glide2TexInfo toGlide2(const Glide3TexInfo &info);

    Board _board;
    Glide2 _core;
    Layout _layout;
    Vertex _last; // the values parameters left out keep
    int _viewport[4] = {0, 0, 640, 480};
    float _depthNear = 0;
    float _depthFar = 1;
    std::array<bool, 8> _enabled{};
    std::array<int, kMaxTmus> _mipmapAsked{}; // grTexMipMapMode's mode, before GR_ALLOW_MIPMAP_DITHER
    std::array<bool, kMaxTmus> _lodBlend{};
    uint32_t _context = 0;
    struct Saved {
        Layout layout;
        std::array<bool, 8> enabled{};
    };
    std::map<uint32_t, Saved> _saved;
    // For strips and fans continued by the next call.
    std::vector<ClipVertex> _strip;
    ClipVertex _clip; // the last vertex read in clip coordinates
    uint32_t _lastMode = 0xffffffffu;
    size_t _stripParity = 0;
};

} // namespace reluva::glide
