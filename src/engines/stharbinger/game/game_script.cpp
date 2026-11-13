// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The scene script interpreter: control flow, variables and the commands that
// change scene state. Addresses refer to DEEP.EXE.

#include "engines/stharbinger/game/game.h"

#include "common/fs.h"
#include "common/log.h"
#include "common/text.h"
#include "engines/stharbinger/formats/audio.h"
#include "engines/stharbinger/harbinger.h"
#include "engines/stharbinger/platform.h"

namespace reluva::stharbinger {

namespace {

// Guards against scripts that loop without ever reaching RETURN or a wait.
const int kMaxDirectivesPerRun = 100000;
// GOSUB stack size (0x7145C).
const size_t kMaxCallDepth = 24;
// ACTIONSEQ result when nothing else is returned.
const uint16_t kActionSeqDefault = 999;

// The Watcom C library rand(), as used by RANDOM.
uint32_t g_randSeed = 1;
uint16_t watcomRand() {
    g_randSeed = g_randSeed * 1103515245u + 12345u;
    return uint16_t((g_randSeed >> 16) & 0x7FFF);
}

// Countdown lengths (seconds, before the difficulty bonus) by location and
// stage 1..5: DEEP.EXE table 0x54F14, read by ACTIONSEQ 0xF1..0xF5.
const uint8_t kCountdown[82][5] = {
    {25, 37, 37, 26, 9},  {24, 36, 36, 25, 8},  {23, 35, 35, 24, 7}, {22, 34, 34, 23, 6},  {21, 33, 33, 22, 5},
    {20, 32, 32, 21, 4},  {19, 31, 31, 20, 3},  {18, 30, 30, 19, 2}, {17, 29, 29, 18, 1},  {16, 28, 28, 17, 0},
    {15, 27, 27, 16, 1},  {14, 26, 26, 15, 2},  {13, 25, 25, 14, 3}, {12, 24, 24, 13, 4},  {11, 23, 23, 12, 5},
    {10, 22, 22, 11, 6},  {9, 21, 21, 10, 7},   {10, 22, 22, 11, 8}, {11, 23, 23, 12, 9},  {12, 24, 24, 13, 10},
    {13, 25, 25, 14, 11}, {14, 26, 26, 15, 12}, {8, 20, 20, 9, 8},   {7, 19, 19, 8, 9},    {6, 18, 18, 7, 10},
    {5, 17, 17, 6, 11},   {4, 16, 16, 5, 12},   {3, 15, 15, 4, 13},  {2, 14, 14, 3, 14},   {1, 13, 13, 2, 15},
    {0, 14, 14, 3, 16},   {0, 15, 15, 4, 17},   {0, 16, 16, 5, 18},  {0, 17, 17, 6, 19},   {4, 18, 18, 7, 20},
    {5, 19, 19, 8, 21},   {2, 12, 12, 1, 16},   {3, 11, 11, 0, 17},  {4, 10, 10, 1, 18},   {5, 9, 9, 2, 19},
    {6, 8, 8, 3, 20},     {7, 7, 7, 4, 21},     {8, 6, 6, 5, 22},    {9, 5, 5, 6, 23},     {10, 4, 5, 7, 24},
    {11, 5, 6, 8, 25},    {11, 3, 4, 8, 25},    {12, 2, 3, 9, 26},   {13, 1, 2, 10, 27},   {14, 1, 3, 11, 28},
    {15, 2, 4, 12, 29},   {16, 3, 5, 13, 30},   {17, 4, 6, 14, 31},  {18, 5, 7, 15, 32},   {19, 6, 8, 16, 33},
    {20, 7, 9, 17, 34},   {21, 8, 10, 18, 35},  {22, 9, 11, 19, 36}, {22, 10, 10, 18, 36}, {21, 9, 9, 17, 35},
    {20, 8, 8, 16, 34},   {19, 7, 7, 15, 33},   {18, 6, 6, 14, 32},  {17, 5, 5, 13, 31},   {16, 4, 4, 12, 30},
    {15, 3, 3, 11, 29},   {14, 2, 2, 10, 28},   {13, 2, 2, 9, 27},   {12, 3, 3, 8, 26},    {11, 4, 4, 7, 25},
    {10, 5, 5, 8, 24},    {11, 6, 6, 9, 25},    {14, 3, 1, 10, 28},  {15, 4, 0, 12, 29},   {16, 5, 1, 11, 30},
    {17, 6, 2, 12, 31},   {16, 5, 1, 12, 30},   {16, 4, 2, 13, 30},  {15, 3, 1, 12, 29},   {16, 4, 3, 13, 30},
    {15, 3, 2, 12, 29},   {14, 2, 1, 11, 28},
};

// IF commands compare unsigned 16-bit values.
bool compare(uint8_t command, uint16_t a, uint16_t b) {
    switch (command) {
    case kCmdIfE:
    case kCmdIfESubr:
        return a == b;
    case kCmdIfNE:
    case kCmdIfNESubr:
        return a != b;
    case kCmdIfGT:
    case kCmdIfGTSubr:
        return a > b;
    case kCmdIfGE:
    case kCmdIfGESubr:
        return a >= b;
    case kCmdIfLT:
    case kCmdIfLTSubr:
        return a < b;
    case kCmdIfLE:
    case kCmdIfLESubr:
        return a <= b;
    default:
        return false;
    }
}

bool isSymbol(const Argument *a) {
    return a && a->type == kArgSymbol;
}

} // namespace

// ---- Values and variables ------------------------------------------------

std::string Game::variableName(const Argument *arg) const {
    if (!arg)
        return std::string();
    const auto &vars = scene_->lists[kListVariables];
    if (arg->value < vars.size())
        return fs::toLower(vars[arg->value].name);
    return text::format("@%u", arg->value);
}

uint16_t Game::getVariable(const Argument *arg) const {
    auto it = variables_.find(variableName(arg));
    return it == variables_.end() ? 0 : it->second;
}

// A type-02 argument is a variable, anything else its own number (0x12E94).
uint16_t Game::getValue(const Argument *arg) const {
    if (!arg)
        return 0xFFFF;
    return arg->type == kArgSymbol ? getVariable(arg) : uint16_t(arg->value);
}

void Game::setVariable(const Argument *arg, uint16_t value) {
    std::string name = variableName(arg);
    if (name.empty())
        return;
    log::debug(3, "  $%s = %u", name.c_str(), value);
    variables_[name] = value;
}

// ---- Labels and control flow ---------------------------------------------

void Game::buildLabels() {
    // LABEL n marks the directive after it as label n (0x10A36).
    labels_.clear();
    for (uint32_t i = 0; i < scene_->script.size(); i++) {
        const Directive &d = scene_->script[i];
        const Argument *a = d.getArg(0);
        if (d.command == kCmdLabel && a)
            labels_[a->value] = i + 1;
    }
}

void Game::jumpToLabel(int label) {
    auto it = labels_.find(uint32_t(label));
    thread_.active = true;
    thread_.wait = kWaitNone;
    if (it == labels_.end()) {
        // The game ends up on a NULL directive and restarts the script.
        log::warning("%s: label %d is not defined", scene_->name.c_str(), label);
        thread_.wait = kWaitRestart;
        return;
    }
    thread_.pc = it->second;
}

bool Game::jumpTo(const Argument *arg) {
    jumpToLabel(arg ? int(arg->value) : -1);
    return thread_.wait == kWaitNone;
}

bool Game::gosub(const Argument *arg) {
    // A full stack is reported and the call becomes a jump (0x12E2D).
    if (thread_.callStack.size() < kMaxCallDepth)
        thread_.callStack.push_back(thread_.pc);
    else
        log::warning("%s: subroutine nesting too deep", scene_->name.c_str());
    return jumpTo(arg);
}

bool Game::isWaitOver() {
    switch (thread_.wait) {
    case kWaitAnim:
        return thread_.waitArg < 0 || thread_.waitArg >= int(anims_.size()) || !anims_[thread_.waitArg].suspended;
    case kWaitFade:
        return !fade_.active;
    case kWaitRestart:
        thread_.pc = 0;
        return true;
    case kWaitConversation:
        return !conv_.active;
    case kWaitVideo:
        return !video_.active;
    case kWaitArcade:
        return !arcade_;
    case kWaitCredits:
        return !credits_.active;
    default:
        return true;
    }
}

void Game::runThread() {
    int count = 0;
    while (thread_.active && pendingScene_ < 0) {
        if (thread_.wait != kWaitNone) {
            if (!isWaitOver())
                return;
            thread_.wait = kWaitNone;
        }
        if (thread_.pc >= scene_->script.size()) {
            // Running off the end restarts the script, without reloading (0x10901).
            thread_.wait = kWaitRestart;
            return;
        }
        if (++count > kMaxDirectivesPerRun) {
            log::warning("%s: script did not return after %d directives; stopped", scene_->name.c_str(), count);
            thread_.active = false;
            return;
        }
        const Directive &d = scene_->script[thread_.pc++];
        log::debug(4, "%s %04u: %s", scene_->name.c_str(), thread_.pc - 1, describe(d).c_str());
        if (!executeDirective(d))
            return;
    }
}

std::string Game::describe(const Directive &d) const {
    std::string s = getCommandName(d.command);
    for (const Argument &a : d.args)
        s += text::format(" %u:%02X:%u", a.slot, a.type, a.value);
    return s;
}

// ---- Commands -------------------------------------------------------------

bool Game::executeDirective(const Directive &d) {
    const Argument *a0 = d.getArg(0);
    const Argument *a1 = d.getArg(1);
    const Argument *a2 = d.getArg(2);
    Timer *timer = a0 && a0->value < timers_.size() ? &timers_[a0->value] : nullptr;

    switch (d.command) {
    // Variables. The destination is always read as a variable.
    case kCmdLet:
        setVariable(a1, getValue(a0));
        return true;
    case kCmdAdd:
        setVariable(a1, uint16_t(getVariable(a1) + getValue(a0)));
        return true;
    case kCmdSub: {
        uint16_t v = getVariable(a1), n = getValue(a0);
        if (n > v)
            log::debug(1, "%s: result of subtraction negative", scene_->name.c_str());
        else
            setVariable(a1, uint16_t(v - n));
        return true;
    }
    case kCmdMultiply:
        // Variable first. (The game's overflow check is inverted; unused by the data.)
        setVariable(a0, uint16_t(uint32_t(getVariable(a0)) * getValue(a1)));
        return true;
    case kCmdDivide:
        if (getValue(a1))
            setVariable(a0, uint16_t(getVariable(a0) / getValue(a1)));
        return true;
    case kCmdRandom: {
        uint16_t n = a1 && (a1->type == kArgSymbol || a1->type == kArgConst) ? getValue(a1) : 0xFFFF;
        setVariable(a0, n ? uint16_t(watcomRand() % n) : 0);
        return true;
    }

    // Control flow.
    case kCmdJump:
        return jumpTo(a0);
    case kCmdGosub:
        return gosub(a0);
    case kCmdIfE:
    case kCmdIfNE:
    case kCmdIfGT:
    case kCmdIfGE:
    case kCmdIfLT:
    case kCmdIfLE:
        return compare(d.command, getValue(a0), getValue(a1)) ? jumpTo(a2) : true;
    case kCmdIfESubr:
    case kCmdIfNESubr:
    case kCmdIfGTSubr:
    case kCmdIfGESubr:
    case kCmdIfLTSubr:
    case kCmdIfLESubr:
        return compare(d.command, getValue(a0), getValue(a1)) ? gosub(a2) : true;
    case kCmdSwitch: {
        // Jumps to label k if k < 7 and that slot holds one; otherwise falls through.
        uint16_t k = getVariable(a0);
        const Argument *target = k < 7 ? d.getArg(1 + k) : nullptr;
        return isSymbol(target) ? jumpTo(target) : true;
    }
    case kCmdReturn:
    case kCmdReturnVal:
        if (!thread_.callStack.empty()) {
            thread_.pc = thread_.callStack.back();
            thread_.callStack.pop_back();
            return true;
        }
        if (thread_.nested && d.command == kCmdReturnVal)
            thread_.returnValue = evalReturnValue(a0);
        thread_.active = false; // idle: the player has control
        if (!thread_.nested)
            idlePc_ = thread_.pc - 1;
        return false;

    // Scenes.
    case kCmdNewScene: {
        // The argument is the scene's SCENE number. An unknown one does nothing.
        int index = vm_->findSceneByNumber(a0 ? int(a0->value) : -1);
        if (index < 0) {
            log::warning("NEWSCENE: no scene number %u", a0 ? a0->value : 0);
            return true;
        }
        pendingScene_ = index;
        thread_.active = false;
        return false;
    }
    case kCmdBackground:
        setBackground(a0 ? int(a0->value) : -1, isSymbol(a1) && a1->value == 0);
        return true;
    case kCmdFadeOut:
    case kCmdFadeIn:
        fade_.active = true;
        fade_.in = d.command == kCmdFadeIn;
        fade_.steps = std::max<int>(1, getValue(a0));
        fade_.startMillis = millis_;
        fadeLevel_ = fade_.in ? 0 : 256;
        applyPalette();
        thread_.wait = kWaitFade;
        return false;
    case kCmdMapOn:
        mapEnabled_ = true;
        return true;
    case kCmdMapOff:
        mapEnabled_ = false;
        return true;
    case kCmdDeath:
        // The game sets its "dead" flag and opens the game menu.
        dead_ = true;
        thread_.active = false;
        idlePc_ = thread_.pc - 1;
        openMenu(false);
        return false;
    case kCmdRequireDisc:
        // Asks for the other CD if needed; all the data is available here.
        disc_ = getValue(a0);
        return true;

    // Hotspots.
    case kCmdHotspot: {
        if (!a0 || a0->value >= hotspots_.size())
            return true;
        Hotspot &h = hotspots_[a0->value];
        if (h.defined) {
            log::debug(1, "%s: duplicate hotspot definition", scene_->name.c_str());
            return true;
        }
        h.defined = true;
        h.enabled = false;
        h.x = d.getArg(1) ? int(d.getArg(1)->value) : 0;
        h.y = d.getArg(2) ? int(d.getArg(2)->value) : 0;
        h.w = d.getArg(3) ? int(d.getArg(3)->value) : 0;
        h.h = d.getArg(4) ? int(d.getArg(4)->value) : 0;
        h.label = d.getArg(5) ? int(d.getArg(5)->value) : -1;
        h.cursor = d.getArg(6) ? int(d.getArg(6)->value) : kCursorNormal;
        return true;
    }
    case kCmdSetRect:
        if (a0)
            setRectHotspot_ = int(a0->value); // ACTIONSEQ 0xC2 fires from it
        if (a0 && a0->value < hotspots_.size()) {
            Hotspot &h = hotspots_[a0->value];
            h.x = getValue(d.getArg(1));
            h.y = getValue(d.getArg(2));
            h.w = getValue(d.getArg(3));
            h.h = getValue(d.getArg(4));
        }
        return true;
    case kCmdHotOn:
    case kCmdHotOff:
        for (const Argument &a : d.args)
            if (a.type == kArgSymbol && a.value < hotspots_.size())
                hotspots_[a.value].enabled = d.command == kCmdHotOn;
        return true;
    case kCmdHotType:
        if (a0 && a0->value < hotspots_.size() && a1)
            hotspots_[a0->value].cursor = int(a1->value);
        return true;

    // Timers: one-shot, counted in 100 ms ticks.
    case kCmdTimer:
        if (timer) {
            timer->defined = true;
            timer->active = true;
            timer->reload = timer->count = getValue(a1);
            timer->label = a2 ? int(a2->value) : -1;
            const Argument *kind = d.getArg(3);
            timer->kind = isSymbol(kind) ? int(kind->value) : kTimerScene;
        }
        return true;
    case kCmdTimerOn:
        if (timer)
            timer->active = true;
        return true;
    case kCmdTimerOff:
        if (timer)
            timer->active = false;
        return true;
    case kCmdTimerSet:
        if (timer)
            timer->reload = timer->count = getValue(a1);
        return true;
    case kCmdTimerGet:
        if (timer)
            setVariable(a1, timer->count);
        return true;
    case kCmdTimerLabel:
    case kCmdTimerLabel2:
        if (timer && a1)
            timer->label = int(a1->value);
        return true;
    case kCmdTimerReload:
        if (timer)
            timer->count = timer->reload;
        return true;

    // Sound.
    case kCmdSound:
        playSound(a0 ? int(a0->value) : -1, a1);
        return true;

    case kCmdConverse:
        // Blocks until the conversation ends; then the script continues, or jumps
        // to a hotspot the player clicked to end it.
        actionSeqPc_ = thread_.pc - 1;
        startConversation(a0 ? int(a0->value) : -1, a1 ? int(a1->value) : -1);
        return thread_.active && thread_.wait == kWaitNone;
    // Action sequences (Smacker videos with their arcade parts) are not
    // implemented yet; they return the default result.
    case kCmdActionSeq:
        actionSequence(d);
        return thread_.wait == kWaitNone;

    default:
        if (executeAnimCommand(d))
            return thread_.active && thread_.wait == kWaitNone;
        // SCENE, LABEL, NOP and the dialogue lip-sync commands do nothing here.
        return true;
    }
}

int Game::randomNumber(int n) {
    return n > 0 ? watcomRand() % n : 0;
}

void Game::drawBeam(Surface &dst, const Beam &b) {
    if (b.enemy)
        drawEnemyBeam(dst, b.x0, b.y0, b.x1, b.y1, b.color, 8, 460, [this](int n) { return randomNumber(n); });
    else
        drawPhaserBeam(dst, b.x0, b.y0, b.x1, b.y1, b.color);
}

void Game::drawBeams(Surface &dst) {
    for (const Beam &b : beams_)
        drawBeam(dst, b);
}

void Game::addBeam(Beam b) {
    // The box between the end points, at least 2 pixels each way; the renderer
    // later wipes it 3 pixels wider on each side.
    int right = std::max(b.x0, b.x1), bottom = std::max(b.y0, b.y1);
    b.left = std::min(b.x0, b.x1);
    b.top = std::min(b.y0, b.y1);
    if (right - b.left < 2) {
        b.left--;
        right++;
    }
    if (bottom - b.top < 2) {
        b.top--;
        bottom++;
    }
    b.w = right - b.left;
    b.h = bottom - b.top;
    beams_.push_back(b);
    // Shown at once; a second beam does not reset the renders left.
    drawBeam(screen_, b);
    if (beamLife_ == 0)
        beamLife_ = 3;
}

void Game::updateTick() {
    // The tick runs from the timer interrupt in the game: everywhere, menus too.
    if (tickPeriod_ == 0xFFFF)
        return;
    tickPhase_ += 20;
    if (tickPhase_ >= tickPeriod_) {
        tickPhase_ = 0;
        if (audio::Mixer *mixer = system_->getMixer())
            if (!tickSound_.empty())
                mixer->play(makeRawStream(tickSound_), audio::SoundType::Effects);
    }
}

// RETURNVAL's value for a conversation (0x10E83), with bit 15 set.
uint16_t Game::evalReturnValue(const Argument *a) const {
    if (!a)
        return 0x8000;
    uint16_t v;
    switch (a->type) {
    case kArgSymbol:
        v = getVariable(a);
        break;
    case kArgPair08:
        v = uint16_t((((a->value & 0xFF) << 8) | (a->value >> 8)) + 0x80);
        break;
    case kArgPair10:
        v = uint16_t((((a->value & 0xFF) << 8) | (a->value >> 8)) + 0x40);
        break;
    default:
        v = a->value < 3 ? uint16_t(a->value) : 1;
        break;
    }
    return uint16_t(v | 0x8000);
}

void Game::actionSequence(const Directive &d) {
    uint16_t id = getValue(d.getArg(0));
    int type = id >= 256 ? id >> 8 : id;
    uint16_t result = kActionSeqDefault;
    actionSeqPc_ = thread_.pc - 1;
    if (type == 0xA0) {
        // Where the player is, for the deck map.
        mapLocation_ = id & 0xFF;
        setVariable(d.getArg(1), result);
        return;
    }
    // Types 0x00..0x1F are videos: cut-scenes and action sequences.
    if (type < 0x20 && playActionSequenceVideo(type, d.getArg(1)))
        return;
    switch (type) {
    case 0x80:
    case 0x81:
        // The tricorder icon drawn over each background.
        tricorderIcon_ = type == 0x80;
        break;
    case 0xC0:
    case 0xC1: {
        // The player's phaser: a beam from the bottom centre to the cursor. The
        // script decides what it hit (0x258BC).
        Beam b;
        Point aim = cursorHitPoint(false);
        b.x0 = 320;
        b.y0 = 479;
        b.x1 = aim.x;
        b.y1 = aim.y;
        b.color = uint8_t(palette_.findBestColor(255, 255, 255));
        addBeam(b);
        break;
    }
    case 0xC2: {
        // An enemy shot from the SETRECT hotspot to a random point at the
        // bottom: it hits the player if it lands in the middle third (0x25B0E).
        Beam b;
        b.enemy = true;
        if (setRectHotspot_ >= 0 && setRectHotspot_ < int(hotspots_.size())) {
            const Hotspot &h = hotspots_[setRectHotspot_];
            b.x0 = h.x + h.w / 2;
            b.y0 = h.y + h.h - 5;
        }
        b.x1 = randomNumber(620) + 10;
        b.y1 = 460;
        b.color = 1;
        addBeam(b);
        if (b.x1 >= 213 && b.x1 <= 426)
            result = 0xFFFE;
        break;
    }
    case 0xC3:
        break;
    case 0xF0:
        tickPeriod_ = 0xFFFF; // the countdown's tick stops
        break;
    case 0xF1:
    case 0xF2:
    case 0xF3:
    case 0xF4:
    case 0xF5: {
        // The countdown: a tick sound, faster nearer the goal. The script uses
        // the result (in 100 ms ticks) for its own timer; easier difficulties get
        // up to 4 more seconds.
        int stage = type - 0xF0, location = id & 0xFF;
        int t = location < 82 ? kCountdown[location][stage - 1] : 0;
        int seconds = t + 1 + 2 * (2 - arcadeDifficulty_);
        tickPeriod_ = uint32_t(64 * seconds);
        result = uint16_t(10 * seconds);
        if (tickSound_.empty())
            if (auto s = resources_.openFile("SOUND/TICK.RAW"))
                tickSound_ = s->readAll();
        break;
    }
    default:
        log::debug(1, "ACTIONSEQ %u: no such function", id);
        break;
    }
    setVariable(d.getArg(1), result);
}

} // namespace reluva::stharbinger
