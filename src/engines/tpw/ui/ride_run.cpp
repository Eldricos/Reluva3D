// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/ui/ride_run.h"

#include "common/fs.h"
#include "common/text.h"

#include <algorithm>
#include <cmath>

namespace reluva::tpw {

namespace {

constexpr uint32_t kTurnMs = 20;

std::string tail(const std::string &path) {
    const size_t slash = path.rfind('/');
    return fs::toLower(slash == std::string::npos ? path : path.substr(slash + 1));
}

} // namespace

Error RideRun::start(const SearchSet &files, const std::string &model) {
    _machine.reset();
    _host.reset();
    const size_t slash = model.rfind('/');
    const std::string folder = slash == std::string::npos ? std::string() : model.substr(0, slash);
    _scriptName = model.substr(0, model.size() - 4) + ".RSE";
    std::vector<uint8_t> data;
    if (!files.has(_scriptName))
        return Error::fail("no script " + _scriptName);
    if (Error err = files.read(_scriptName, data))
        return err;
    if (Error err = _script.parse(data))
        return err;
    _host = std::make_unique<SimulatedHost>(files, folder);
    _host->setGuests(true);
    _host->readAnimationLengths(_scriptName);
    _host->readSettings(_scriptName);
    _machine = std::make_unique<ScriptMachine>(_script, *_host);
    return Error::success();
}

void RideRun::advance(uint32_t ms) {
    if (_machine)
        runFor(*_machine, *_host, ms, kTurnMs);
}

void RideRun::pose(const LoadedModel &loaded, ModelView &view) const {
    view.animation = -1;
    view.frame = 0;
    if (!_host || _host->playing().number < 0)
        return;
    const SimulatedHost::Playing &p = _host->playing();
    const std::string file = tail(_host->animationFile(p.number));
    for (size_t i = 0; i < loaded.animationNames.size(); ++i)
        if (!file.empty() && fs::toLower(loaded.animationNames[i]) == file) {
            const float length = float(std::max(1u, loaded.animations[i].frames()));
            const float frame = float(_host->now() - p.startMs) * Animation::kFramesPerSecond / 1000;
            view.animation = int(i);
            view.frame = p.loop ? std::fmod(frame, length) : std::min(frame, length - 1);
            return;
        }
}

std::string RideRun::status() const {
    if (!_machine)
        return {};
    const SimulatedHost::Playing &p = _host->playing();
    const char *state = _machine->state() == ScriptMachine::State::kRunning   ? "running"
                        : _machine->state() == ScriptMachine::State::kWaiting ? "waiting"
                        : _machine->state() == ScriptMachine::State::kEnded   ? "ended"
                                                                              : "stopped";
    std::string line =
        text::format("%.1f s  %s %s", double(_host->now()) / 1000,
                     _machine->name().empty() ? tail(_scriptName).c_str() : _machine->name().c_str(), state);
    if (p.number >= 0) {
        const std::string file = tail(_host->animationFile(p.number));
        line += text::format(", animation %d (%s)%s", p.number, file.empty() ? "not known" : file.c_str(),
                             p.loop ? " looping" : "");
    }
    if (!_machine->error().empty())
        line += ": " + _machine->error();
    return line;
}

} // namespace reluva::tpw
