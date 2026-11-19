// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Animation commands and playback, after DEEP.EXE's renderer (0x22AE3).
//
// The scene is drawn into a work buffer that persists from step to step. Each
// step (20 ms) erases what has to go, draws every animation that is not off
// and advances it (renderAnims): a frame stays up for rate + 1 steps. At the end of a pass the animation
// releases a waiting script, jumps the script to its ANIMEND label if it has one,
// and then stops (TOEND), turns round (CYCLE) or wraps (LOOP).

#include "engines/stharbinger/game/game.h"

#include "common/log.h"
#include "common/text.h"
#include "engines/stharbinger/harbinger.h"

#include <algorithm>

namespace reluva::stharbinger {

namespace {

const int kNumPriorities = 5;
const int kDefaultRate = 10;
// Animations loaded on first use are drawn at this priority (0x1C155).
const int kLazyPriority = 3;

bool isSymbol(const Argument *a) {
    return a && a->type == kArgSymbol;
}

} // namespace

void Game::setupAnims() {
    // The ANIMDEF prepass (0x10AC4): onthefly and flyclear animations are loaded
    // on first use, the others when the scene loads.
    anims_.assign(scene_->lists[kListAnims].size(), Anim());
    for (const Directive &d : scene_->script) {
        const Argument *a0 = d.getArg(0), *a1 = d.getArg(1);
        if (d.command != kCmdAnimDef || !a0 || a0->value >= anims_.size())
            continue;
        Anim &a = anims_[a0->value];
        int mode = a1 ? int(a1->value) : -1;
        a.lazy = mode == kAnimModeOnTheFly || mode == kAnimModeFlyClear;
        a.clear = mode == kAnimModeFlyClear || mode == kAnimModeClear;
    }
    for (int i = 0; i < int(anims_.size()); i++)
        if (!anims_[i].lazy)
            loadAnim(i, false);
}

void Game::loadAnim(int index, bool lazy) {
    Anim &a = anims_[index];
    if (a.loaded)
        return;
    a.loaded = true;
    a.flags = kAnimFlagOff;
    a.rate = 0;
    a.priority = lazy ? kLazyPriority : 0;
    a.frame = 0;
    if (!resources_.loadAnimation(listName(kListAnims, index), a.rle))
        log::debug(1, "Animation %s not found", listName(kListAnims, index).c_str());
}

void Game::unloadAnim(int index) {
    Anim &a = anims_[index];
    a.loaded = false;
    a.rle = RleAnimation();
    a.flags = kAnimFlagOff;
    a.drawnFrame = -1;
    a.suspended = false;
}

// Any animation command loads an on-the-fly animation first.
Game::Anim *Game::useAnim(const Argument *arg) {
    if (!arg || arg->value >= anims_.size())
        return nullptr;
    if (!anims_[arg->value].loaded)
        loadAnim(int(arg->value), true);
    return &anims_[arg->value];
}

void Game::applyAnimKeyword(Anim &a, uint16_t keyword) {
    if (keyword > kAnimOneFrame)
        return;
    uint8_t bit = uint8_t(1 << (keyword / 2));
    if (keyword & 1)
        a.flags |= bit;
    else
        a.flags &= uint8_t(~bit);
}

void Game::restartAnim(Anim &a) {
    a.frame = 0;
    a.countdown = 0;
}

bool Game::executeAnimCommand(const Directive &d) {
    const Argument *a0 = d.getArg(0);
    const Argument *a1 = d.getArg(1);

    switch (d.command) {
    case kCmdAnimDef: {
        // Only animations that are already loaded get the defaults; on-the-fly
        // ones keep the loader's (0x11719).
        if (!a0 || a0->value >= anims_.size())
            return true;
        Anim &a = anims_[a0->value];
        if (a.defined || !a.loaded)
            return true;
        a.defined = true;
        a.rate = kDefaultRate;
        a.flags = kAnimFlagOff;
        a.priority = 0;
        a.x = a.y = 0;
        restartAnim(a);
        return true;
    }
    case kCmdAnim: {
        Anim *a = useAnim(a0);
        if (!a)
            return true;
        if (isSymbol(a1) && a1->value == kAnimOff && (a->flags & kAnimFlagOff))
            return true;
        // The keyword replaces all flags (START plays looping, forward, visible).
        if (isSymbol(a1)) {
            a->flags = 0;
            applyAnimKeyword(*a, a1->value);
        }
        if (d.getArg(2))
            a->rate = int(d.getArg(2)->value);
        if (d.getArg(3))
            a->x = int(int16_t(d.getArg(3)->value));
        if (d.getArg(4))
            a->y = int(int16_t(d.getArg(4)->value));
        restartAnim(*a);
        return true;
    }
    case kCmdAnimSeq:
    case kCmdAnimRate: {
        Anim *a = useAnim(a0);
        if (!a)
            return true;
        int first = 1;
        if (d.command == kCmdAnimRate) {
            if (a1)
                a->rate = int(a1->value);
            first = 2;
        }
        for (const Argument &arg : d.args)
            if (arg.slot >= first && arg.type == kArgSymbol)
                applyAnimKeyword(*a, uint16_t(arg.value));
        return true;
    }
    case kCmdAnimPos:
        if (Anim *a = useAnim(a0)) {
            a->x = int(int16_t(getValue(a1)));
            a->y = int(int16_t(getValue(d.getArg(2))));
        }
        return true;
    case kCmdAnimSetFrame:
        if (Anim *a = useAnim(a0)) {
            uint16_t n = getValue(a1);
            int count = a->rle.frameCount();
            a->frame = n == 0xFFFF ? -1 : (count ? n % count : 0);
        }
        return true;
    case kCmdAnimPrio:
        if (Anim *a = useAnim(a0))
            a->priority = std::clamp<int>(getValue(a1), 0, kNumPriorities - 1);
        return true;
    case kCmdAnimEnd:
        if (Anim *a = useAnim(a0))
            a->endLabel = a1 ? int(a1->value) : -1;
        return true;
    case kCmdAnimSuspend: {
        // Waits only where the animation will come to an end by itself (0x141F0).
        Anim *a = useAnim(a0);
        if (!a || !a->rle.frameCount())
            return true;
        bool off = (a->flags & kAnimFlagOff) != 0;
        if ((a->flags & kAnimFlagOneFrame) || (off && a->drawnFrame >= 0) ||
            (!(a->flags & kAnimFlagStop) && (a->flags & kAnimFlagToEnd))) {
            a->suspended = true;
            thread_.wait = kWaitAnim;
            thread_.waitArg = int(a0->value);
        }
        return true;
    }
    case kCmdAnimUnsus:
        if (Anim *a = useAnim(a0))
            a->suspended = false;
        return true;
    case kCmdAnimsOff:
        // Everything off, on-the-fly animations unloaded, and the screen is not
        // updated until the next BACKGRND (0x114D5).
        for (int i = 0; i < int(anims_.size()); i++) {
            if (anims_[i].lazy)
                unloadAnim(i);
            anims_[i].flags |= kAnimFlagOff;
        }
        screenFrozen_ = true;
        return true;
    case kCmdAnimDump:
        for (const Argument &arg : d.args)
            if (arg.type == kArgSymbol && arg.value < anims_.size())
                unloadAnim(int(arg.value));
        return true;
    case kCmdAnimComposit:
        // Copies what is on screen under the animation's first frame (the
        // background with every animation as last drawn) into the background
        // (0x1429D).
        if (Anim *a = useAnim(a0)) {
            if (a->rle.frameCount() && !draw::empty(background_)) {
                const RleAnimation::Frame &f = a->rle.getFrame(0);
                bool current = !draw::empty(workBuffer_) && !workBufferStale_;
                const Surface &src = current ? workBuffer_ : background_;
                int x0 = std::max(0, a->x + f.x), y0 = std::max(0, a->y + f.y);
                int x1 = std::min(background_.w, a->x + f.x + f.w);
                int y1 = std::min(background_.h, a->y + f.y + f.h);
                for (int y = y0; y < y1; y++)
                    for (int x = x0; x < x1; x++)
                        *draw::at(background_, x, y) = *draw::at(src, x, y);
            }
        }
        return true;
    case kCmdAnimPristine:
        // Draws the animation's current frame into the background (0x1432F).
        if (Anim *a = useAnim(a0)) {
            if (a->rle.frameCount() && !draw::empty(background_))
                a->rle.drawFrame(std::max(a->frame, 0), background_, a->x, a->y);
        }
        return true;
    case kCmdAnimSpd:
    case kCmdAnimDest:
    case kCmdAnimArrv:
    case kCmdAnimFram:
        // The game stores these but never uses them.
        return true;
    default:
        return false;
    }
}

void Game::advanceAnim(Anim &a) {
    int count = a.rle.frameCount();
    bool backward = (a.flags & kAnimFlagBackward) != 0;
    bool atEnd = backward ? a.frame <= 0 : a.frame >= count - 1;
    if (!atEnd) {
        a.frame += backward ? -1 : 1;
        return;
    }
    // End of a pass.
    a.suspended = false;
    if (a.endLabel >= 0) {
        log::debug(2, "Animation end: running label %d", a.endLabel);
        jumpToLabel(a.endLabel);
    }
    if (a.flags & kAnimFlagToEnd)
        a.flags |= kAnimFlagStop;
    if (a.flags & kAnimFlagCycle) {
        a.flags ^= kAnimFlagBackward;
        if (count > 1)
            a.frame += backward ? 1 : -1;
    } else if (!(a.flags & kAnimFlagStop)) {
        a.frame = backward ? count - 1 : 0;
    }
}

void Game::restoreRect(int x, int y, int w, int h) {
    int x0 = std::max(0, x), y0 = std::max(0, y);
    int x1 = std::min({workBuffer_.w, background_.w, x + w});
    int y1 = std::min({workBuffer_.h, background_.h, y + h});
    for (int py = y0; py < y1; py++)
        for (int px = x0; px < x1; px++)
            *draw::at(workBuffer_, px, py) = *draw::at(background_, px, py);
}

void Game::renderAnims(bool advance) {
    if (draw::empty(workBuffer_) || workBufferStale_) {
        draw::create(workBuffer_, kScreenWidth, kScreenHeight);
        draw::blit(workBuffer_, background_, 0, 0);
        workBufferStale_ = false;
    }
    // Beams last three renders, BACKGRND's included: redrawn on the first, left
    // on the second and wiped with the background on the third (0x22BD4).
    if (beamLife_ > 0 && --beamLife_ == 0) {
        for (const Beam &b : beams_)
            restoreRect(b.left - 3, b.top, b.w + 6, b.h);
        beams_.clear();
    }

    // Erase: an animation that is off, or a clear one that is not stopped, gives
    // back the background under the frame it last drew. Nothing else is ever
    // erased, so a moving animation without "clear" leaves a trail. While a
    // talker's line plays the game renders from the lip-sync loop (dl = 2),
    // which erases stopped clear animations too: the mouth changes shape
    // without moving its frames on.
    bool lipSync = conv_.active && conv_.speaking && conv_.mouth >= 0;
    for (int i = int(anims_.size()) - 1; i >= 0; i--) {
        Anim &a = anims_[i];
        if (!a.rle.frameCount())
            continue;
        bool off = (a.flags & kAnimFlagOff) != 0;
        if (!off && !(a.clear && (lipSync || !(a.flags & kAnimFlagStop))))
            continue;
        if (a.drawnFrame >= 0 && a.drawnFrame < a.rle.frameCount()) {
            const RleAnimation::Frame &f = a.rle.getFrame(a.drawnFrame);
            restoreRect(a.drawnX + f.x, a.drawnY + f.y, f.w, f.h);
        }
        if (off) {
            // A script waiting for it carries on.
            a.drawnFrame = -1;
            a.suspended = false;
        }
    }

    // Draw: priority 0 first, 4 on top; within a priority the lowest index is
    // on top. Beams from ACTIONSEQ 0xC0..0xC2 go under priority 4.
    for (int prio = 0; prio < kNumPriorities; prio++) {
        if (prio == 4 && beamLife_ > 1)
            drawBeams(workBuffer_);
        for (int i = int(anims_.size()) - 1; i >= 0; i--) {
            Anim &a = anims_[i];
            if (a.priority != prio || !a.rle.frameCount() || a.frame < 0 || (a.flags & kAnimFlagOff))
                continue;
            int frame = std::min(a.frame, a.rle.frameCount() - 1);
            a.rle.drawFrame(frame, workBuffer_, a.x, a.y);
            a.drawnFrame = frame;
            a.drawnX = a.x;
            a.drawnY = a.y;
            if (!advance)
                continue;
            if (a.flags & kAnimFlagOneFrame) {
                // Shown once, then off without being erased: it stays on screen
                // until something draws over it.
                a.flags = uint8_t((a.flags | kAnimFlagOff) & ~kAnimFlagOneFrame);
                a.suspended = false;
                a.drawnFrame = -1;
                continue;
            }
            if (a.flags & kAnimFlagStop)
                continue;
            if (a.countdown > 0) {
                // Faster while a talker's lips move (speedstep 3, 0x71518).
                a.countdown = std::max(0, a.countdown - speedStep_);
                continue;
            }
            a.countdown = a.rate;
            advanceAnim(a);
        }
    }
}

bool Game::isAnimVisible(const std::string &name) const {
    const auto &list = scene_->lists[kListAnims];
    for (size_t i = 0; i < list.size() && i < anims_.size(); i++)
        if (text::equalsIgnoreCase(list[i].name, name))
            return !(anims_[i].flags & kAnimFlagOff);
    return false;
}

int Game::getAnimFrame(const std::string &name) const {
    const auto &list = scene_->lists[kListAnims];
    for (size_t i = 0; i < list.size() && i < anims_.size(); i++)
        if (text::equalsIgnoreCase(list[i].name, name))
            return anims_[i].frame;
    return -1;
}

} // namespace reluva::stharbinger
