// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/archive.h"
#include "common/events.h"
#include "common/system.h"
#include "engines/stawayteam/formats/text_object.h"
#include "engines/stawayteam/ui/font.h"
#include "engines/stawayteam/ui/widgets.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace reluva::stawayteam {

class ConfirmDialog;

/**
 * The window the escape button and Esc open on a map (the "Esc Menu",
 * 0x5ba9bf): CONFIGURE for the settings, SAVE GAME, LOAD GAME, RESTART
 * MISSION and END GAME, each of the last two asked first, and RESUME
 * (docs/engines/stawayteam/systems/maps.md#the-interface).
 */
class EscapeWindow {
public:
    /** What the player chose. */
    enum class Choice { kNone, kConfigure, kSaveGame, kLoadGame, kRestart, kEndGame, kResume };

    /** On a screen `screenW` x `screenH`; `data` and `rules` must outlive it. */
    EscapeWindow(const Archive &data, const TextObject &rules, int screenW, int screenH);
    ~EscapeWindow();

    Choice handleEvent(const Event &ev, audio::Mixer *mixer);
    void draw(Surface &screen, uint64_t now);

    /** Its buttons, in order: CONFIGURE, SAVE GAME, LOAD GAME, RESTART MISSION, END GAME, RESUME. */
    const std::vector<PictureButton> &buttons() const { return _buttons; }

private:
    const Archive &_data;
    const TextObject &_rules;
    image::Image _background;
    Font _font;
    std::vector<PictureButton> _buttons;
    std::unique_ptr<ConfirmDialog> _confirm;
    Choice _asking = Choice::kNone; // what the dialog's asking about
    int _x;
    int _y;
};

} // namespace reluva::stawayteam
