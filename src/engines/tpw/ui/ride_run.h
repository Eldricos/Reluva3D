// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/tpw/formats/rse.h"
#include "engines/tpw/script/machine.h"
#include "engines/tpw/script/simulated_host.h"
#include "engines/tpw/ui/model_view.h"

#include <memory>
#include <string>

namespace reluva::tpw {

/**
 * A ride, shop or feature run by its own script: the script machine on
 * the stand-in for the park, guests coming, and the model posed by the
 * animation the script last played.
 */
class RideRun {
public:
    /**
     * Starts the script beside `model` of its name (`wateride.MD2`:
     * `Wateride.RSE`), the stand-in's guests coming if `guests`. Fails when
     * there is none, or it can't be read.
     */
    Error start(const SearchSet &files, const std::string &model, bool guests = true);

    /** Runs the script on for `ms` milliseconds, in turns of 20. */
    void advance(uint32_t ms);

    /**
     * Poses `view` as the script has the model now: the animation it plays
     * (in `loaded.animations`), at its frame, held at its end unless it
     * loops; the rest pose for one whose file isn't known.
     */
    void pose(const LoadedModel &loaded, ModelView &view) const;

    /** A line saying where the script is and what it plays. */
    std::string status() const;

    const ScriptMachine *machine() const { return _machine.get(); }
    /** The script's machine, to set what the park tells it. */
    ScriptMachine *machine() { return _machine.get(); }
    const SimulatedHost *host() const { return _host.get(); }
    /** The stand-in, to offer it the park's guests. */
    SimulatedHost *host() { return _host.get(); }

private:
    std::string _scriptName;
    Script _script;
    std::unique_ptr<SimulatedHost> _host;
    std::unique_ptr<ScriptMachine> _machine;
};

} // namespace reluva::tpw
