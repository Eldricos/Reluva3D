// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/archive.h"
#include "common/system.h"
#include "engines/stawayteam/formats/text_object.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace reluva::stawayteam {

/**
 * The main menu's screen without its menu, until the menus are rebuilt:
 * what Rules.txt's main menu puts on it (its "nernies": movies, looping,
 * and pictures, each at its position), and the startup song.
 */
class TitleScreen {
public:
    /**
     * The screen as `rules` describes it, its files from `data` and the
     * install's movies; `sys`, `screen`, `data` and `mixer` (nullptr: no
     * sound) must outlive it.
     */
    TitleScreen(System &sys, Surface &screen, const Archive &data, const std::filesystem::path &install,
                const TextObject &rules, audio::Mixer *mixer);
    ~TitleScreen();

    /** Shown until Esc, or the window is closed (then false). */
    bool run();

    /** What it found to show: "movie <name> at x,y", "picture <name> at x,y". */
    const std::vector<std::string> &contents() const { return _contents; }

private:
    struct Part;

    System &_sys;
    Surface &_screen;
    audio::Mixer *_mixer;
    std::vector<std::unique_ptr<Part>> _parts;
    std::vector<std::string> _contents;
    std::shared_ptr<audio::Sound> _song;
};

} // namespace reluva::stawayteam
