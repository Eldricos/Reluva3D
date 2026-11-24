// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"
#include "engines/storm3d/game_files.h"
#include "engines/storm3d/scene/sod_meshes.h"
#include "graphics/gfx3d.h"

#include <cstddef>
#include <string>
#include <vector>

namespace reluva::storm3d {

/**
 * The screen shown while a mission loads, as the game draws it in 3D:
 * the Baku nebula's background (`mbgbaku.sod`) around the camera, the
 * game's logo (`stalogo.sod`) before it, lit from up and left, and along
 * the bottom a bar that grows as the mission's files are read. See
 * docs/engines/storm3d/games/armada/systems/front-end.md.
 */
class LoadingScreen {
public:
    explicit LoadingScreen(const GameFiles &files) : _files(files), _meshes(files) {}
    ~LoadingScreen() { stop(); }
    LoadingScreen(const LoadingScreen &) = delete;
    LoadingScreen &operator=(const LoadingScreen &) = delete;

    /**
     * Starts loading a mission: `map` its map ("fed1.bzn"), drawn through
     * `renderer`.
     * @return a failure if the map can't be read
     */
    Error start(gfx::Renderer3D *renderer, const std::string &map);
    /** Lets go of the renderer and what it made. */
    void stop();
    /**
     * Reads the next few of the mission's files.
     * @return whether there are more to read
     */
    bool step();
    /** How much is read, 0 to 1. */
    float progress() const { return _toRead.empty() ? 1.0f : float(_done) / float(_toRead.size()); }
    /** The files a mission's map needs, in the order read: each object's definition, then its model. */
    static std::vector<std::string> filesOf(const std::vector<std::string> &definitions);

    /** Draws it, the bar's length the progress. */
    void draw(uint64_t millis);

private:
    const GameFiles &_files;
    SodMeshes _meshes;
    gfx::Renderer3D *_renderer = nullptr;
    const SodMeshes::Built *_background = nullptr;
    const SodMeshes::Built *_logo = nullptr;
    Surface _overlay;
    std::vector<std::string> _toRead; // the mission's files, in order
    size_t _done = 0;
};

} // namespace reluva::storm3d
