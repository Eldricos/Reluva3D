// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/engine.h"

#include <memory>

namespace reluva {

/**
 * An engine's data viewer: pages showing what a game's files hold, for
 * working on the engine.
 *
 * Viewers are built into reluva-viewer (and the tests and reluva-tool),
 * never into reluva, which players get: an engine declares its viewer with
 * reluva_add_engine_viewers in its CMakeLists.txt, in a library of its own.
 */
class MetaViewer {
public:
    virtual ~MetaViewer() = default;

    /** The id of the engine whose games it shows (MetaEngine::id). */
    virtual const char *engineId() const = 0;

    /**
     * A viewer for the game in `target`, run as a game's Engine is: until
     * the user leaves it (Esc) or quits.
     */
    virtual std::unique_ptr<Engine> create(System &system, const GameTarget &target) const = 0;
};

} // namespace reluva
