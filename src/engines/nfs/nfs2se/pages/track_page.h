// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/nfs/nfs2se/formats/col.h"
#include "engines/nfs/nfs2se/formats/hrz.h"
#include "engines/nfs/nfs2se/formats/trk.h"
#include "engines/nfs/nfs2se/glide_sky.h"
#include "engines/nfs/nfs2se/software_sky.h"
#include "engines/nfs/nfs2se/pages/quad_mesh.h"
#include "engines/nfs/nfs2se/track_sounds.h"
#include "engines/nfs/pages/viewers.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::nfs::nfs2se {

/**
 * NFS II SE's tracks in 3D: the ground at a level of detail and the placed
 * objects, with the 3dfx program's pictures, seen from the virtual road.
 *   PageUp/PageDown: track   Up/Down: along the road (Shift: faster)
 *   Left/Right: look   1/2/3: level   T: textures   O: objects   V: from above
 *   S: the 3dfx program's shades   L: painted lines   H: sky   F: fog
 *   G: the software program's look, not the 3dfx program's
 * The 3dfx program's look: its pictures (Tracks/SE/trNN0.qfs), sky and fog
 * (3trNN.hrz and its own tables). The software program's: its pictures
 * (Tracks/pc/trNN0.qfs, by the same places), sky (Tracks/pc/trNN.hrz) and
 * depth cueing (hrz.md).
 * Animated textures (the .col's) take their pictures in turn, at the game's
 * rate. The placed sounds (extrablock 17) play from the track's
 * TRAMBnn.BNK as the camera passes them, when there's a mixer.
 * RELUVA_NFS2SE_TRACK_VIEW="track point level" opens it there, for
 * screenshots that repeat.
 */
class TrackPage : public Page {
public:
    /** Shows the tracks of `files`, which must outlive the page; plays their sounds on `mixer`, if any. */
    explicit TrackPage(const GameFiles &files, audio::Mixer *mixer = nullptr);
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
    void buildLines();
    void buildDome();
    void drawSky(const gfx::Vec3 &eye, const gfx::Vec3 &ahead, const GlideFog &fog);
    void drawSoftwareSky(const gfx::Vec3 &eye, const gfx::Vec3 &ahead);
    void cue(const gfx::Vec3 &eye, const gfx::Vec3 &ahead);

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
    std::vector<gfx::TextureId> _lines;    // dtexture.fsh's lin0-lin7, by number
    gfx::MeshId _mesh = 0;
    std::vector<QuadMesh::Run> _runs;
    // The painted lines, each as it's drawn close up and far off; and far
    // off a block's at once, for a block whose lines are all far.
    struct LineMesh {
        gfx::MeshId mesh = 0;
        std::vector<std::vector<QuadMesh::Run>> runs; // by line, or by block
    };
    LineMesh _nearLines;
    LineMesh _farLines;
    LineMesh _farBlocks;
    std::vector<Point> _lineStart;                  // each line's vertex
    std::vector<std::array<size_t, 2>> _blockLines; // each block's lines, first and end
    size_t _quads = 0;
    int _level = TrackBlock::kHigh;
    bool _textured = true;
    bool _objects = true;
    bool _above = false;
    bool _shaded = true;
    bool _painted = true;
    bool _skyShown = true;
    bool _software = false;

    // The software program's look: its pictures by place, its sky, the
    // light of each depth bucket; each quad's vertices as built and the
    // list it's drawn in, whose bias sorts it; where the light was last
    // worked out from.
    enum class List : uint8_t { kGround, kSecond, kCollision, kVariable };
    std::vector<gfx::TextureId> _softwareTextures;
    Sky _softwareSky;
    bool _hasSoftwareSky = false;
    std::array<uint8_t, kDepthBuckets> _lights{};
    std::vector<gfx::Vertex> _vertices;
    std::vector<List> _quadLists;
    gfx::Vec3 _cuedEye{};
    gfx::Vec3 _cuedAhead{};
    bool _cued = false;
    gfx::MeshId _softwareSkyMesh = 0;
    bool _fogged = true;
    int _trackNumber = 0; // the NN of trNN
    gfx::MeshId _skyMesh = 0;
    gfx::MeshId _domeMesh = 0;
    gfx::TextureId _domeTexture = 0;
    uint32_t _domeIndices = 0;
    int _point = 0;  // on the virtual road
    float _look = 0; // turned from the road's way, radians

    TrackSounds _sounds;
};

} // namespace reluva::nfs::nfs2se
