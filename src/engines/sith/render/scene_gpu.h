// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "emulation/x86/directx/directx.h"
#include "graphics/gfx3d.h"

#include <cstdint>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

namespace reluva::sith {

/**
 * Draws the Direct3D scenes JK.EXE draws with the GPU renderer: the
 * triangles its device was given (x86::DirectX::Scene, kept with
 * SceneCapture::kInstead), already on the screen, depth tested as the
 * device would, with the 2D the game drew over them on top.
 */
class SceneGpu {
public:
    /**
     * The renderer to draw with; null when it's gone (its textures and
     * mesh with it).
     */
    void setRenderer(gfx::Renderer3D *renderer);
    /**
     * Draws `scene` (none: the overlay alone) and shows it, with `overlay`
     * (RGBA32, the renderer's size) over it, blended by its alpha.
     */
    void draw(const x86::DirectX::Scene *scene, const Surface &overlay);
    /** The next frame drawn, kept for capture() (RGBA, the renderer's size, without the overlay). */
    void captureNext() {
        if (_renderer)
            _renderer->captureNextFrame();
    }
    /** The frame captureNext() kept; false without a renderer, or if none was kept since the last call. */
    bool capture(std::vector<uint8_t> &rgba) { return _renderer && _renderer->takeCapture(rgba); }

private:
    // A texture as a draw samples it: keyed texels with alpha 0, the
    // texture's own alpha or none, wrapping or clamped, filtered or not.
    struct Variant {
        bool keyed;
        bool alpha;
        bool wrap;
        bool linear;
        bool operator<(const Variant &o) const {
            return std::tie(keyed, alpha, wrap, linear) < std::tie(o.keyed, o.alpha, o.wrap, o.linear);
        }
    };
    struct Made {
        std::weak_ptr<const x86::DirectX::Scene::Texture> source; // gone: the id is stale
        gfx::TextureId id;
        uint64_t used; // the frame it was last drawn in
    };
    gfx::TextureId texture(const std::shared_ptr<const x86::DirectX::Scene::Texture> &t, const Variant &v);
    bool upload(const x86::DirectX::Scene &scene);

    gfx::Renderer3D *_renderer = nullptr;
    gfx::MeshId _mesh = 0;
    size_t _indexCapacity = 0; // indices 0, 1, 2... set this far
    std::vector<gfx::Vertex> _vertices;
    std::map<std::pair<const void *, Variant>, Made> _textures;
    uint64_t _frame = 0;
};

} // namespace reluva::sith
