// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "image/image.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

// A game's own window over its scenes, which the other game hasn't (And
// Then There Were None's journal): the shared scene page updates and
// draws it, gives it clicks first and asks it of verbs it doesn't know.

namespace reluva::awe {

struct GameState;
class GameFonts;

/** What a game's own window needs from the scene page around it. */
struct OverlayHost {
    GameState &state;
    const Archive &files;
    /** A picture of the game's, loaded once; an empty image if it hasn't one. */
    std::function<const image::Image &(const std::string &file)> picture;
    /** A sound of the game's played once (its path, "sound\interface\click1.wav"). */
    std::function<void(const std::string &file)> sound;
    /** interface.txt's text for a tag. */
    std::function<std::string(const std::string &tag)> text;
};

/** A game's own window over its scenes. */
class GameOverlay {
public:
    virtual ~GameOverlay() = default;
    /** Moves it on by `seconds`. */
    virtual void update(float seconds) = 0;
    /** Draws it over `dst` (the game's frame), when the interface is shown. */
    virtual void draw(Surface &dst, const GameFonts &fonts) = 0;
    /** A click at (x, y): true if it took it. */
    virtual bool click(int x, int y) = 0;
    /** A script verb of its own (`words[0]` lower case): true if it's one, done. */
    virtual bool verb(const std::vector<std::string> &words) = 0;
};

} // namespace reluva::awe
