// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/script/simulated_host.h"

#include "common/fs.h"
#include "common/text.h"
#include "engines/tpw/formats/animation.h"
#include "engines/tpw/formats/sam.h"

#include <algorithm>
#include <utility>
#include <string>

namespace reluva::tpw {

namespace {

// Every ride's first two variables: the guest at the front of its queue,
// and the one it lets off (docs/engines/tpw/games/simcoaster/formats/rse.md#behaviour).
constexpr size_t kLetMeOn = 0;
constexpr size_t kLetMeOff = 1;
constexpr uint16_t kFlushAnim = 15;
constexpr uint16_t kTrigAnim = 16;
constexpr uint16_t kWaitAnim = 17;
constexpr uint16_t kLoopAnim = 18;
constexpr uint16_t kTrigWaitAnim = 19;
constexpr uint16_t kTrigAnimChannel = 23;
constexpr uint16_t kGetAnimChannel = 27;
constexpr uint16_t kHush = 42;
constexpr uint16_t kHop = 43;
constexpr uint16_t kWait4Anim = 46;
constexpr uint16_t kTour = 53;
constexpr uint16_t kBump = 54;
constexpr uint16_t kAddHead = 56;
constexpr uint16_t kDelHead = 57;
constexpr uint16_t kLimbo = 58;
constexpr uint16_t kUnlimbo = 59;
constexpr uint16_t kForceUnlimbo = 60;
constexpr uint16_t kInLimbo = 61;
constexpr uint16_t kLimboSpace = 62;
constexpr uint16_t kBounce = 72;
constexpr uint16_t kUnbounce = 73;
constexpr uint16_t kForceUnbounce = 74;
constexpr uint16_t kBouncing = 75;
constexpr uint16_t kWalkOff = 77;
constexpr uint16_t kWalkGet = 78;
constexpr uint16_t kFindScriptRand = 90;
constexpr uint16_t kSetTimer = 95;
constexpr uint16_t kGetTimer = 96;
// The checks of a ride that answer yes here, by OpenTPW's lists of the
// commands: TOUR 10, a car in the station; BUMP 5, the track valid.
constexpr int kTourCarInStation = 10;
constexpr int kBumpTrackValid = 5;
// LIMBO's and BOUNCE's times are seconds: the shops' 5 and the arcades' 15
// would be too short as milliseconds, as would the bouncy rides' durations of 3.
constexpr uint32_t kMillisPerSecond = 1000;

// The opcodes that wait for their own animation to end. TRIGWAITANIM
// waits for the one playing instead, then starts its own and goes on.
bool waitsForAnimation(uint16_t opcode) {
    switch (opcode) {
    case 17: // WAITANIM
    case 24: // WAITANIM_CH
    case 26: // TRIGWAITANIM_CH
        return true;
    default:
        return false;
    }
}

// The guest of a list whose time has come (the first, if `force`) taken
// off it; 0 for none.
int takeDue(std::vector<std::pair<int, uint32_t>> &list, uint32_t now, bool force) {
    if (list.empty() || (!force && int32_t(now - list.front().second) < 0))
        return 0;
    const int guest = list.front().first;
    list.erase(list.begin());
    return guest;
}

} // namespace

void SimulatedHost::readAnimationLengths(const std::string &script) {
    // The animation numbers whose files are known, by their names' letters.
    static const std::pair<int, const char *> kLetters[] = {{0, "c"}, {2, "i"}, {3, "l"}, {4, "s"},  {5, "m"},
                                                            {6, "e"}, {7, "u"}, {9, "b"}, {10, "r"}, {11, "o"}};
    // As many variants as the most a script plays (the ATM's 15).
    constexpr int kVariants = 16;
    const size_t dot = script.rfind('.');
    const std::string stem = script.substr(0, dot);
    for (const auto &[number, letter] : kLetters)
        for (int variant = 0; variant < kVariants; ++variant) {
            std::vector<std::string> names{stem + letter + std::to_string(variant + 1) + ".MD2"};
            if (variant == 0)
                names.insert(names.begin(), stem + letter + ".MD2");
            for (const std::string &name : names) {
                std::vector<uint8_t> data;
                Animation animation;
                if (_animationMs.count({number, variant}) || _files.read(name, data) || animation.parse(data))
                    continue;
                _animationMs[{number, variant}] =
                    uint32_t(float(animation.frames()) * 1000 / Animation::kFramesPerSecond);
                _animationFiles[{number, variant}] = name;
            }
        }
}

void SimulatedHost::readSettings(const std::string &script) {
    const size_t slash = script.rfind('/');
    const std::string folder = script.substr(0, slash == std::string::npos ? 0 : slash);
    const std::string stem = script.substr(0, script.rfind('.'));
    // The kind's folder holds the general file of its name: rides/Rides.sam.
    const size_t kindEnd = folder.rfind('/');
    const std::string kind = folder.substr(0, kindEnd == std::string::npos ? 0 : kindEnd);
    const std::string kindName = kind.substr(kind.rfind('/') + 1);
    for (const std::string &name : {stem + ".sam", kind + "/" + kindName + ".sam"}) {
        std::vector<uint8_t> data;
        SamFile sam;
        if (_files.read(name, data) || sam.parse(data))
            continue;
        if (!_capacity)
            _capacity = int(sam.number("Upgrades[0].InitCapacity"));
        if (!_duration)
            _duration = int(sam.number("Upgrades[0].InitDuration"));
    }
}

std::string SimulatedHost::animationFile(int number, int variant) const {
    const auto found = _animationFiles.find({number, variant});
    return found == _animationFiles.end() ? std::string() : found->second;
}

uint32_t SimulatedHost::animationMs(int number, int variant) const {
    const auto known = _animationMs.find({number, variant});
    return known != _animationMs.end() ? known->second : kAnimationMs;
}

uint32_t SimulatedHost::remainingMs() const {
    return _playing.number >= 0 && !_playing.loop && _now - _playing.startMs < _playing.lengthMs
               ? _playing.lengthMs - (_now - _playing.startMs)
               : 0;
}

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
    auto value = [&](size_t i) { return i < values.size() ? values[i] : 0; };
    // An animation instruction's number and variant come first.
    const uint32_t ownMs = animationMs(value(0), value(1));
    if (waitsForAnimation(instruction.opcode))
        r.waitMs = ownMs;
    // What the ride's own script plays on its model (its children play
    // sounds); WAIT4ANIM waits for what is left of it.
    if (!machine.parent()) {
        switch (instruction.opcode) {
        case kTrigAnim:
        case kWaitAnim:
        case kLoopAnim:
            _playing = {value(0), value(1), _now, ownMs, instruction.opcode == kLoopAnim};
            break;
        case kTrigWaitAnim:
            r.waitMs = remainingMs();
            _playing = {value(0), value(1), _now + r.waitMs, ownMs, false};
            break;
        case kFlushAnim:
            _playing = {};
            break;
        case kWait4Anim:
            r.waitMs = remainingMs();
            break;
        default:
            break;
        }
    }
    const auto &ops = instruction.operands;
    auto setResult = [&](size_t operand, int v) {
        if (operand < ops.size() && ops[operand].kind == Script::OperandKind::kVariable)
            machine.setVariable(size_t(ops[operand].value), v);
    };
    switch (instruction.opcode) {
    case kTrigAnim:
        // Its last operand gets the animation's length: the bus waits for
        // it, less 3 seconds.
        setResult(2, int(ownMs));
        break;
    case kTrigAnimChannel:
        // TRIGANIM_CH number variant ? channel.
        _channelEnds[{&machine, value(3)}] = _now + ownMs;
        break;
    case kGetAnimChannel: {
        // GETANIM_CH ? channel: playing on, or -1 once done, which the
        // shooting gallery waits for.
        const auto end = _channelEnds.find({&machine, value(1)});
        r.value = end != _channelEnds.end() && int32_t(end->second - _now) > 0 ? int(end->second - _now) : -1;
        break;
    }
    case kTour:
        r.value = value(0) == kTourCarInStation;
        break;
    case kBump:
        r.value = value(0) == kBumpTrackValid;
        break;
    case kLimbo:
        // LIMBO guest time: the guest is inside for that long, in seconds.
        _limbo.emplace_back(value(0), _now + uint32_t(std::max(0, value(1))) * kMillisPerSecond);
        break;
    case kInLimbo:
        r.value = int(_limbo.size());
        setResult(0, r.value);
        break;
    case kLimboSpace:
        // Room for another, of the script's #setlimbo.
        r.value = std::max(0, int(machine.script().limboSize()) - int(_limbo.size()));
        break;
    case kUnlimbo:
    case kForceUnlimbo:
        // The next guest whose time is up, or the first one forced, into
        // the operand (VAR_LETMEOFF); 0 for none.
        r.value = takeDue(_limbo, _now, instruction.opcode == kForceUnlimbo);
        setResult(0, r.value);
        break;
    case kBounce:
        _bouncing.emplace_back(value(0), _now + uint32_t(std::max(0, value(1))) * kMillisPerSecond);
        break;
    case kBouncing:
        r.value = int(_bouncing.size());
        setResult(0, r.value);
        break;
    case kUnbounce:
    case kForceUnbounce:
        r.value = takeDue(_bouncing, _now, instruction.opcode == kForceUnbounce);
        setResult(0, r.value);
        break;
    case kAddHead:
        // The rider shown in the first seat free, or a new one.
        if (value(0)) {
            const auto free = std::find(_heads.begin(), _heads.end(), 0);
            if (free != _heads.end())
                *free = value(0);
            else
                _heads.push_back(value(0));
        }
        break;
    case kDelHead:
        std::replace(_heads.begin(), _heads.end(), value(0), 0);
        break;
    case kHush:
        if (value(0))
            _onRide.push_back(value(0));
        break;
    case kHop:
        // The next rider off, into the operand: "Wait until AI has taken
        // him", test.rss says after it of VAR_LETMEOFF.
        if (!_onRide.empty()) {
            r.value = _onRide.front();
            _onRide.erase(_onRide.begin());
        }
        setResult(0, r.value);
        break;
    case kWalkOff:
        if (value(0))
            _walkingOff.emplace_back(value(0), _now + kWalkOffMs);
        break;
    case kWalkGet:
        r.value = takeDue(_walkingOff, _now, false);
        setResult(0, r.value);
        break;
    case kSetTimer:
        _timerEnds[&machine] = _now + uint32_t(std::max(0, value(0)));
        break;
    case kGetTimer: {
        const auto end = _timerEnds.find(&machine);
        r.value = end != _timerEnds.end() && int32_t(end->second - _now) > 0 ? int(end->second - _now) : 0;
        setResult(0, r.value);
        break;
    }
    case kFindScriptRand:
        // No other scripts run here to find.
        setResult(1, 0);
        break;
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

void SimulatedHost::offerGuest(int id) {
    _offering = true;
    if (_pending == 0 && id != 0)
        _pending = id;
}

void SimulatedHost::queueGuest(ScriptMachine &machine) {
    if (machine.locked() || machine.variables().size() <= kLetMeOff)
        return;
    // The ride's settings, once, before its script's first turn.
    if (std::find(_given.begin(), _given.end(), &machine) == _given.end()) {
        _given.push_back(&machine);
        const auto &names = machine.script().variables();
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i] == "VAR_CAPACITY" && _capacity)
                machine.setVariable(i, _capacity);
            if (names[i] == "VAR_DURATION" && _duration)
                machine.setVariable(i, _duration);
        }
    }
    if (_offering) {
        if (machine.variable(kLetMeOn) == 0 && _offered != 0)
            _boarded.push_back(std::exchange(_offered, 0));
        if (machine.variable(kLetMeOn) == 0 && _pending != 0) {
            _offered = std::exchange(_pending, 0);
            machine.setVariable(kLetMeOn, _offered);
        }
        if (machine.variable(kLetMeOff) != 0) {
            _letOff.push_back(machine.variable(kLetMeOff));
            // Off the ride, out of their seat, whether the script said so or not.
            std::replace(_heads.begin(), _heads.end(), machine.variable(kLetMeOff), 0);
        }
    } else if (_guests && machine.variable(kLetMeOn) == 0) {
        machine.setVariable(kLetMeOn, _nextGuest++);
    }
    // The guest let off walks away, freeing the exit.
    if (_guests || _offering)
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
