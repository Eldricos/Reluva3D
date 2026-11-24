// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "emulation/x86/win32.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace reluva::x86 {

/**
 * DirectDraw and Direct3D 7 for the programs of 1997 to 2001 that use
 * them (DDRAW.dll's DirectDrawCreate, DirectDrawCreateEx and
 * DirectDrawEnumerate, and the objects they lead to), written from
 * Microsoft's DirectX 7 documentation. Surfaces live in the process's
 * memory, as the programs lock them and write to them; Direct3D's drawing
 * is done into them by a software rasterizer. What reaches the screen
 * (flips to, and blits onto, the primary surface) is handed to the engine
 * as a frame.
 */
class DirectX {
public:
    /**
     * Adds DDRAW.dll's functions to `process`. They point into this object,
     * so it has to outlive the program's use of them.
     */
    explicit DirectX(Win32Process &process);
    ~DirectX();

    /**
     * A Direct3D scene kept for the engine to draw itself (sceneCapture):
     * what the program drew into a buffer of the display's flipping chain,
     * in order, after projection.
     */
    struct Scene {
        /** A texture as a draw used it: its top level's pixels. */
        struct Texture {
            int width = 0;
            int height = 0;
            std::vector<uint32_t> argb;
            std::vector<uint8_t> keyed; // 1 where the surface's colour key matches
        };
        /**
         * A vertex on the screen: x and y in pixels, whole numbers at the
         * pixels' centres; z 0 to 1, as the viewport maps it; 1/w.
         */
        struct Vertex {
            float x;
            float y;
            float z;
            float rhw;
            uint32_t diffuse;  // 0xAARRGGBB
            uint32_t specular; // alpha: the fog factor
            float u;           // the first texture stage's coordinates
            float v;
        };
        /**
         * One draw's triangles (three vertices each), with the render
         * states they were drawn with; or, with clearColor or clearDepth,
         * a clear of the viewport instead.
         */
        struct Step {
            uint32_t first = 0; // in vertices
            uint32_t count = 0;
            bool clearColor = false;
            bool clearDepth = false;
            uint32_t color = 0; // the clear's, 0xAARRGGBB
            float depth = 0;    // the clear's, 0 to 1
            int viewport[4]{};  // x, y, width, height
            std::shared_ptr<const Texture> texture;
            // Render and texture stage states as Direct3D 7 numbers them
            // (D3DCMPFUNC, D3DBLEND, D3DTEXTUREOP, D3DTA_*, D3DTADDRESS).
            bool zTest = false;
            bool zWrite = false;
            uint32_t zFunc = 0;
            bool alphaTest = false;
            uint32_t alphaFunc = 0;
            float alphaRef = 0; // 0 to 1
            bool blend = false;
            uint32_t srcBlend = 0;
            uint32_t destBlend = 0;
            bool colorKey = false;
            bool linear = false; // bilinear filtering, magnified
            uint32_t addressU = 0;
            uint32_t addressV = 0;
            uint32_t colorOp = 0;
            uint32_t colorArg1 = 0;
            uint32_t colorArg2 = 0;
            uint32_t alphaOp = 0;
            uint32_t alphaArg1 = 0;
            uint32_t alphaArg2 = 0;
            bool fog = false;
            bool specular = false;
            uint32_t cull = 0; // D3DCULL; the vertices are already culled by it
        };
        std::vector<Vertex> vertices;
        std::vector<Step> steps;
        // SceneCapture::kAlso: the target as drawn in software at EndScene
        // (0xAARRGGBB, the target's size), for comparing.
        std::vector<uint32_t> software;
        int width = 0; // the target's
        int height = 0;
    };

    /**
     * What's on the screen: the primary surface as of the last flip or
     * blit onto it, as 0xAARRGGBB, `width` × `height`.
     */
    struct Frame {
        int width = 0;
        int height = 0;
        /** Where it is on the screen: a window's client area's corner, windowed; 0, 0 full screen. */
        int x = 0;
        int y = 0;
        std::vector<uint32_t> pixels;
        uint64_t number = 0; // counts the frames shown
        // With sceneCapture kInstead: the scene drawn in this frame, to be
        // drawn under `pixels`, whose alpha is then 0 where it shows.
        std::shared_ptr<const Scene> scene;
    };
    /** The frame shown last. */
    const Frame &frame() const;
    // Called each time a new frame is shown.
    std::function<void(const Frame &)> presented;

    /** A display mode offered: its size, and bits per pixel. */
    struct Mode {
        int width;
        int height;
        int bits;
    };
    // The display modes offered; the desktop's comes from the process.
    std::vector<Mode> modes = {{640, 480, 16}, {800, 600, 16}, {1024, 768, 16},
                               {640, 480, 32}, {800, 600, 32}, {1024, 768, 32}};
    // Video memory reported (bytes): what's free is this less the surfaces
    // made in it.
    uint32_t videoMemory = 32u << 20;
    /**
     * How a draw's triangles are drawn: on this thread (kOne), or by rows on
     * as many threads as there are processors, each at its own pace (kAuto
     * and kAlways, which do the same). What's drawn is the same either way.
     */
    enum class DrawThreads { kOne, kAuto, kAlways } drawThreads = DrawThreads::kAuto;
    // Frames shown a flip late: at a flip the frame before is shown, its
    // drawing finished by then, and this one's drawing goes on while the
    // program works out the next. The program needn't wait for the drawing
    // unless it reads what's drawn. Off, a flip waits for its frame's
    // drawing and shows it at once, which tests and frame counts need.
    bool presentLate = false;
    /**
     * Direct3D's triangles drawn into the display's flipping chain drawn
     * here (kNone); kept for the engine (Frame::scene) instead (kInstead),
     * the buffer filled with a colour of its own where the scene is drawn,
     * so the 2D drawn after it is on top (Frame::pixels's alpha); or kept
     * and drawn (kAlso), with each scene the picture drawn
     * (Scene::software), for comparing. Draws into other surfaces are
     * drawn here whatever this says.
     */
    enum class SceneCapture { kNone, kInstead, kAlso } sceneCapture = SceneCapture::kNone;

    struct State;

private:
    std::unique_ptr<State> _s;
};

} // namespace reluva::x86
