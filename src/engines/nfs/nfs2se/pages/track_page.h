// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/nfs/nfs2se/formats/col.h"
#include "engines/nfs/nfs2se/formats/hrz.h"
#include "engines/nfs/nfs2se/formats/trk.h"
#include "engines/nfs/nfs2se/pages/quad_mesh.h"
#include "engines/nfs/pages/viewers.h"

#include <string>
#include <vector>

namespace reluva::nfs::nfs2se {

/**
 * NFS II SE's tracks in 3D: the ground at a level of detail and the placed
 * objects, with the 3dfx program's pictures, seen from the virtual road.
 *   PageUp/PageDown: track   Up/Down: along the road (Shift: faster)
 *   Left/Right: look   1/2/3: level   T: textures   O: objects   V: from above
 * RELUVA_NFS2SE_TRACK_VIEW="track point level" opens it there, for
 * screenshots that repeat.
 */
class TrackPage : public Page {
public:
    /** Shows the tracks of `files`, which must outlive the page. */
    explicit TrackPage(const GameFiles &files);
    ~TrackPage() override;
    const char *name() const override { return "Tracks"; }
    bool empty() const override { return _tracks.empty(); }
    void activate(System &sys) override;
    void deactivate() override;
    bool uses3D() const override { return _renderer != nullptr; }
    void handleKey(uint32_t key, uint16_t mods) override;
    void draw(Surface &screen) override; // when 3D isn't available
    void update(uint64_t nowMs) override;

private:
    void load(int index);
    void build();
    void release();

    const GameFiles &_files;
    std::vector<FileRef> _tracks;
    int _index = 0;
    gfx::Renderer3D *_renderer = nullptr;
    std::string _unavailable;
    Surface _overlay;
    Track _track;
    Collision _collision;
    Sky _sky;
    bool _hasSky = false;
    std::string _error;
    std::vector<gfx::TextureId> _textures; // by place in the .qfs
    gfx::MeshId _mesh = 0;
    std::vector<QuadMesh::Run> _runs;
    size_t _quads = 0;
    int _level = TrackBlock::kHigh;
    bool _textured = true;
    bool _objects = true;
    bool _above = false;
    int _point = 0;  // on the virtual road
    float _look = 0; // turned from the road's way, radians
};

} // namespace reluva::nfs::nfs2se
