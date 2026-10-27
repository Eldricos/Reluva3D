// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/gfx3d.h"
#include "engines/nfs/host/thrash_native.h"

#include <map>
#include <utility>

namespace reluva::nfs {

// Draws THRASH's frames (host/thrash_native.h, ThrashFrame) with the GPU
// renderer: screen space triangles in their batches, in order, without
// depth (the game draws back to front for the software renderer).
class ThrashGpu {
public:
    // The renderer to draw with; null when it's gone (its textures and
    // mesh with it). Textures are made again from the copies kept.
    void setRenderer(gfx::Renderer3D* renderer);
    // Textures made or changed, as a frame brings them.
    void updateTextures(const ThrashFrame& frame);
    // Draws frames in order, each over what the one before left (with the
    // textures each brought, applied as it's reached), and shows the
    // result once, with `overlay` on top if given.
    void draw(const std::vector<const ThrashFrame*>& frames, const Surface* overlay);
    void draw(const ThrashFrame& frame, const Surface* overlay) { draw(std::vector<const ThrashFrame*>{&frame}, overlay); }
    // The next frame drawn, kept for capture() (RGBA, the renderer's size).
    void captureNext() {
        if (renderer_)
            renderer_->captureNextFrame();
    }
    bool capture(std::vector<uint8_t>& rgba) { return renderer_ && renderer_->takeCapture(rgba); }
    // Point-sampled textures, as the software renderer draws (else
    // bilinear).
    bool nearest = true;
    // The renderer's frame against the game's (640x480): 2 draws at
    // 1280x960, the same picture with sharper edges.
    int scale = 1;

private:
    gfx::TextureId texture(uint32_t handle, bool wrap);

    gfx::Renderer3D* renderer_ = nullptr;
    gfx::MeshId mesh_ = 0;
    size_t indexCapacity_ = 0; // indices 0, 1, 2... set this far
    std::vector<gfx::Vertex> vertices_;
    Surface scaledOverlay_; // an overlay of the game's size, enlarged to the renderer's
    std::map<uint32_t, ThrashFrame::Texture> textures_;                 // by handle: what they hold
    std::map<std::pair<uint32_t, bool>, gfx::TextureId> gpuTextures_; // by handle and wrapping
    // Textures replaced while a frame was being drawn (a draw already made
    // may use them): destroyed once it's shown.
    std::vector<gfx::TextureId> retired_;
};

} // namespace reluva::nfs
