// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/raven/fonts.h"
#include "engines/idtech3/vm/qvm.h"
#include "engines/idtech3/vm/vm_services.h"

#include "common/archive.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace reluva::idtech3::jk2 {

/**
 * A letter of text as 1.03's and 1.04's renderer reads it (jk2mp.exe 1.03,
 * 0x4771b0): two bytes in Korean (sp_language 4), traditional Chinese (5)
 * and Japanese (8) where its first and second fall in the language's
 * ranges, else one.
 * @param text the letter's first byte and the next (0 at the text's end)
 * @param[out] advance how many bytes it took
 * @param[out] trailingPunctuation whether it's punctuation a line may end
 *             on, but not start with
 * @return the letter: its byte, or both (the first high)
 */
int32_t readPatchedChar(uint8_t first, uint8_t second, int32_t language, int32_t &advance, bool &trailingPunctuation);

/**
 * Jedi Outcast's text calls, as its multiplayer renderer answers them for
 * the menus and the client game alike (each numbers them its own way):
 * fonts registered by name (fonts/<name>.fontdat, its picture
 * fonts/<name>), text measured and drawn as pictures, a letter limit
 * counting letters and spaces, the handle's high bits a drop shadow
 * (0x80000000) or blinking (0x40000000). See
 * docs/engines/idtech3/games/jk2/systems/syscalls.md (Text).
 */
class FontSet {
public:
    enum Call {
        kRegister,        // (name) -> handle, 0 if there's none
        kStrLenPixels,    // (text, font, scale) -> width
        kStrLenChars,     // (text) -> letters, colour codes and line breaks left out
        kStrHeightPixels, // (font, scale) -> height
        kDrawString,      // (x, y, text, rgba, font, letters at most or -1, scale)
        kReadChar,        // (char **) -> the letter there, the pointer moved past it
        // 1.03's: (text, int *advance, qboolean *trailing punctuation or 0)
        // -> the letter there, how many bytes it took.
        kReadCharAdvance,
    };
    // A piece of text to draw: a part of the font's picture over a
    // rectangle, in the 640 x 480 the modules draw in.
    using Sink = std::function<void(float x, float y, float w, float h, float s1, float t1, float s2, float t2,
                                    int32_t shader, const std::array<float, 4> &color)>;

    // `shaders`: where each font's picture is registered.
    FontSet(const SearchSet &files, HandleTable &shaders) : _files(files), _shaders(shaders) {}

    // One of the calls; `timeMs` is the clock blinking text goes by.
    int32_t call(Qvm &vm, Call which, const int32_t *args, int32_t timeMs, const Sink &draw);
    // sp_language, which 1.03's kReadCharAdvance reads text by.
    int32_t language = 0;

    int32_t registerFont(const std::string &name);
    const raven::Font *font(int32_t handle) const;
    // `limit`: letters at most, as multiplayer's engine takes it, or with
    // `pixels` the width in pixels at most, as single player's (-1: none).
    void drawString(int32_t x, int32_t y, const std::string &text, const std::array<float, 4> &color, int32_t handle,
                    int32_t limit, float scale, int32_t timeMs, const Sink &draw, bool pixels = false) const;

private:
    struct Loaded {
        raven::Font font;
        int32_t shader = 0;
    };
    const SearchSet &_files;
    HandleTable &_shaders;
    std::vector<Loaded> _fonts; // handle - 1
    std::map<std::string, int32_t> _byName;
};

} // namespace reluva::idtech3::jk2
