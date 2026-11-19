// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/botf/ui/main_menu.h"

#include <array>
#include <utility>

namespace reluva::botf {

namespace {

// opening.wdf's buttons, matched to the manual's by their pictures (mccont,
// mcnspg, mcnmpg, mcload, mcsave, mcoptns, mcfame, mcret, mcquit) and their
// order on the screen, as the recording shows them.
constexpr int32_t kContinue = 139;
constexpr int32_t kNewSinglePlayer = 140;
constexpr int32_t kNewMultiplayer = 141;
constexpr int32_t kLoadGame = 142;
constexpr int32_t kSaveGame = 355;
constexpr int32_t kOptions = 356;
constexpr int32_t kHallOfFame = 144;
constexpr int32_t kRetire = 9600;
constexpr int32_t kQuit = 143;

using Choice = MainMenu::Choice;

constexpr std::array<std::pair<int32_t, Choice>, 9> kButtons = {{
    {kContinue, Choice::kContinue},
    {kNewSinglePlayer, Choice::kNewSinglePlayer},
    {kNewMultiplayer, Choice::kNewMultiplayer},
    {kLoadGame, Choice::kLoadGame},
    {kSaveGame, Choice::kSaveGame},
    {kOptions, Choice::kOptions},
    {kHallOfFame, Choice::kHallOfFame},
    {kRetire, Choice::kRetire},
    {kQuit, Choice::kQuit},
}};

} // namespace

Error MainMenu::load() {
    if (Error err = loadLayouts(
            "opening.wdf",
            {kContinue, kNewSinglePlayer, kNewMultiplayer, kLoadGame, kSaveGame, kOptions, kHallOfFame, kRetire, kQuit},
            false))
        return err;
    setGameInProgress(false);
    return Error::success();
}

void MainMenu::setGameInProgress(bool inProgress) {
    // The recording, at the start with no game: these three are dimmed.
    // Continue loads the last game played (the manual), which a first start
    // hasn't got either.
    _disabled.clear();
    if (!inProgress)
        _disabled = {kContinue, kSaveGame, kRetire};
}

MainMenu::Choice MainMenu::handle(const Event &e) {
    trackPointer(e);
    if (e.button != MouseButton::kLeft)
        return Choice::kNone;
    if (e.type == EventType::kMouseDown) {
        _pressed = buttonAt(e.mouseX, e.mouseY);
    } else if (e.type == EventType::kMouseUp) {
        const int32_t id = _pressed && buttonAt(e.mouseX, e.mouseY) == _pressed ? _pressed : 0;
        _pressed = 0;
        for (const auto &[button, choice] : kButtons)
            if (id == button)
                return choice;
    }
    return Choice::kNone;
}

} // namespace reluva::botf
