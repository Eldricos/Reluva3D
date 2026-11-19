// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/cvars.h"

#include "common/events.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace reluva::idtech3 {

// Quake III's key numbers, which the game code (menus, bindings) uses,
// Elite Force's taken to be the same: ASCII for printable keys, then its
// own list from 128.
namespace q3key {
constexpr int32_t kTab = 9;
constexpr int32_t kEnter = 13;
constexpr int32_t kEscape = 27;
constexpr int32_t kSpace = 32;
constexpr int32_t kBackspace = 127;
constexpr int32_t kPause = 131;
constexpr int32_t kUp = 132;
constexpr int32_t kDown = 133;
constexpr int32_t kLeft = 134;
constexpr int32_t kRight = 135;
constexpr int32_t kAlt = 136;
constexpr int32_t kCtrl = 137;
constexpr int32_t kShift = 138;
constexpr int32_t kInsert = 139;
constexpr int32_t kDelete = 140;
constexpr int32_t kPageDown = 141;
constexpr int32_t kPageUp = 142;
constexpr int32_t kHome = 143;
constexpr int32_t kEnd = 144;
constexpr int32_t kF1 = 145;
constexpr int32_t kKpEnter = 169;
// Added to a character's code for typed text, as against keys.
constexpr int32_t kCharFlag = 1024;
constexpr int32_t kMouse1 = 178;
constexpr int32_t kMouse2 = 179;
constexpr int32_t kMouse3 = 180;
constexpr int32_t kMouse4 = 181;
constexpr int32_t kMouse5 = 182;
constexpr int32_t kWheelDown = 183;
constexpr int32_t kWheelUp = 184;
constexpr int32_t kCount = 256;
} // namespace q3key

// The game's number for a key or mouse button (0 for none).
int32_t toQ3Key(uint32_t key);
int32_t toQ3Button(MouseButton button);

// What each key does: the console command it runs ("+forward", "weapon
// 3"...), as the original's bind command sets and its config files keep.
class KeyBindings {
public:
    // A key's name in config files and the menus ("w", "SPACE", "MOUSE2"),
    // and back (-1 for an unknown name; case doesn't matter).
    static std::string keyName(int32_t key);
    static int32_t keyNumber(std::string_view name);

    const std::string &get(int32_t key) const;
    void set(int32_t key, std::string command);
    void clear();
    bool empty() const;

    // Runs a config file's text: bind, unbind and unbindall here, set,
    // seta (and sets, setu) into `cvars`; anything else is left out
    // (counted in `ignored` if given). exec runs another file, read by
    // `readFile` if given.
    void execute(std::string_view text, CvarStore &cvars, int *ignored = nullptr,
                 const std::function<bool(const std::string &, std::string &)> &readFile = nullptr, int depth = 0);
    // The bindings as a config file's lines (unbindall first).
    std::string write() const;

    // Whether set() or clear() has changed anything since last cleared.
    bool changed = false;

private:
    std::array<std::string, q3key::kCount> _commands;
};

// A command line split into words as the original's console does: at
// white space, a quoted string as one word, // ending it.
std::vector<std::string> splitCommand(std::string_view line);
// A config file's text split into commands: at new lines and at
// semicolons outside quotes.
std::vector<std::string> splitCommands(std::string_view text);

} // namespace reluva::idtech3
