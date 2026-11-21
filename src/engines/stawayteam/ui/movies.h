// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/system.h"

#include <filesystem>
#include <optional>
#include <string>

namespace reluva::stawayteam {

/** The movies the game plays as it starts, in order (Star Trek.exe 0x63f860). */
inline constexpr const char *kOpeningMovieNames[] = {"Activision", "Paramount", "Reflexive", "Intro"};

/** Fills an RGBA32 screen with black. */
void clearScreen(Surface &screen);

/** A movie's file in the install: DATA/Movies/<name>.bik, if it's there. */
std::optional<std::filesystem::path> movieFile(const std::filesystem::path &install, const std::string &name);

/**
 * Plays a Bink movie on the whole screen, its picture in the middle on
 * black and its sound through `mixer` (nullptr: none), until it ends or a
 * key or a click skips it, as the game's player does (0x4d4da0).
 * @return False if the window was closed (or the launcher asked for) meanwhile.
 */
bool playMovie(System &sys, Surface &screen, audio::Mixer *mixer, const std::filesystem::path &file);

} // namespace reluva::stawayteam
