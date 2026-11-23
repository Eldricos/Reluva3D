// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/script/simulated_host.h"

#include "common/text.h"

#include <string>

namespace reluva::tpw {

namespace {

// Every ride's first two variables: the guest at the front of its queue,
// and the one it lets off (docs/engines/tpw/games/simcoaster/formats/rse.md#behaviour).
constexpr size_t kLetMeOn = 0;
constexpr size_t kLetMeOff = 1;
constexpr uint16_t kTrigAnim = 16;
constexpr uint16_t kTour = 53;
constexpr uint16_t kBump = 54;
constexpr uint16_t kLimbo = 58;
constexpr uint16_t kUnlimbo = 59;
constexpr uint16_t kForceUnlimbo = 60;
constexpr uint16_t kInLimbo = 61;
// The checks of a ride that answer yes here, by OpenTPW's lists of the
// commands: TOUR 10, a car in the station; BUMP 5, the track valid.
constexpr int kTourCarInStation = 10;
constexpr int kBumpTrackValid = 5;

// The opcodes that wait for their animation to end.
bool waitsForAnimation(uint16_t opcode) {
    switch (opcode) {
    case 17: // WAITANIM
    case 19: // TRIGWAITANIM
    case 24: // WAITANIM_CH
    case 26: // TRIGWAITANIM_CH
    case 46: // WAIT4ANIM
        return true;
    default:
        return false;
    }
}

} // namespace

int SimulatedHost::random(int below) {
    // A linear congruential sequence, the same on every run.
    _seed = _seed * 1103515245u + 12345u;
    return below > 0 ? int((_seed >> 16) % uint32_t(below)) : 0;
}

Error SimulatedHost::loadScript(const ScriptMachine &, const std::string &name, Script &out) {
    std::vector<uint8_t> data;
    const std::string path = _folder.empty() ? name : _folder + "/" + name;
    if (Error err = _files.read(path, data))
        return err;
    return out.parse(data);
}

HostResult SimulatedHost::perform(ScriptMachine &machine, const Script::Instruction &instruction,
                                  const std::vector<int> &values) {
    std::string line =
        text::format("%7u  %-20s %s", _now, machine.name().c_str(), Script::opcodeName(instruction.opcode));
    for (int v : values)
        line += " " + std::to_string(v);
    HostResult r;
    if (waitsForAnimation(instruction.opcode))
        r.waitMs = kAnimationMs;
    const auto &ops = instruction.operands;
    auto setResult = [&](size_t operand, int v) {
        if (operand < ops.size() && ops[operand].kind == Script::OperandKind::kVariable)
            machine.setVariable(size_t(ops[operand].value), v);
    };
    switch (instruction.opcode) {
    case kTrigAnim:
        // Its last operand gets the animation's length: the bus waits for
        // it, less 3 seconds.
        setResult(2, int(kAnimationMs));
        break;
    case kTour:
        r.value = values[0] == kTourCarInStation;
        break;
    case kBump:
        r.value = values[0] == kBumpTrackValid;
        break;
    case kLimbo:
        // LIMBO guest time: the guest is inside for that long.
        _limbo.emplace_back(values[0], _now + uint32_t(values[1]));
        break;
    case kInLimbo:
        r.value = int(_limbo.size());
        setResult(0, r.value);
        break;
    case kUnlimbo:
    case kForceUnlimbo: {
        // The next guest whose time is up, or the first one forced, into
        // the operand (VAR_LETMEOFF); 0 for none.
        const bool force = instruction.opcode == kForceUnlimbo;
        if (!_limbo.empty() && (force || int32_t(_now - _limbo.front().second) >= 0)) {
            r.value = _limbo.front().first;
            _limbo.erase(_limbo.begin());
        }
        setResult(0, r.value);
        break;
    }
    default:
        break;
    }
    if (r.value)
        line += text::format(" -> %d", r.value);
    _trace.push_back(line);
    // Trimmed in halves, so that the copying is rare.
    if (_traceLimit && _trace.size() >= 2 * _traceLimit)
        _trace.erase(_trace.begin(), _trace.end() - long(_traceLimit));
    return r;
}

void SimulatedHost::queueGuest(ScriptMachine &machine) {
    if (!_guests || machine.locked() || machine.variables().size() <= kLetMeOff)
        return;
    if (machine.variable(kLetMeOn) == 0)
        machine.setVariable(kLetMeOn, _nextGuest++);
    // The guest let off walks away, freeing the exit.
    machine.setVariable(kLetMeOff, 0);
}

void runFor(ScriptMachine &machine, SimulatedHost &host, uint32_t ms, uint32_t turnMs) {
    for (uint32_t t = 0;
         t < ms && machine.state() != ScriptMachine::State::kEnded && machine.state() != ScriptMachine::State::kFailed;
         t += turnMs) {
        host.queueGuest(machine);
        machine.tick();
        host.advance(turnMs);
    }
}

} // namespace reluva::tpw
