// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/storm3d/formats/sod.h"
#include "graphics/gfx3d.h"

#include <cstddef>
#include <vector>

namespace reluva::storm3d {

/**
 * A model's tree of nodes, as the game places and shows them: each node's
 * place, its levels of detail, its parts shown only sometimes. See
 * docs/engines/storm3d/games/armada/formats/sod.md.
 */
struct ModelTree {
    /**
     * A node's transform as a matrix that places it in its parent:
     * the 3 x 3 part applied to a point as a row (p * M), then the
     * translation.
     */
    static gfx::Mat4 toMatrix(const SodModel::Transform &t);
    /**
     * Each node's place in the model, its parents' applied; a node whose
     * parent isn't there is placed as a root.
     */
    static std::vector<gfx::Mat4> worldMatrices(const SodModel &model);
    /**
     * The levels of detail: the numbers of the nodes named `lod<n>`,
     * lowest first; empty if there are none.
     */
    static std::vector<int> detailLevels(const SodModel &model);
    /** Whether a node is shown at a level of detail: not under another level's `lod` node. */
    static bool shownAtLevel(const SodModel &model, size_t node, int level);
    /**
     * Whether a node is one of the parts a ship or station has once the
     * Borg assimilate it: under the node `borg`, which nearly every ship and
     * station has, and which the game's skirmish doesn't draw on its own.
     */
    static bool assimilationPart(const SodModel &model, size_t node);
};

} // namespace reluva::storm3d
