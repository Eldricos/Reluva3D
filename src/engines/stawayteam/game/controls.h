// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/config.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reluva::stawayteam {

/**
 * What a game action is bound to, as the program keeps it (0x484f00): a
 * key by its PC scan code (an extended key's with 0x100), a key held with
 * it, a joystick's button, and the mouse's (bits 1, 2 and 4 its buttons,
 * 8 the wheel down, 16 up).
 */
struct Binding {
    int key = 0;
    int modifier = 0;
    int joystick = 0;
    int mouse = 0;

    bool operator==(const Binding &) const = default;
};

/** One of the game's actions, as the program registers it (0x5bd649...). */
struct Action {
    int id;
    const char *category;
    const char *name;
    Binding defaults;
};

/** The game's actions, in the program's order: their categories in the order they first come. */
const std::vector<Action> &gameActions();

/** A scan code's name, as Windows names the US keyboard's keys ("Num +", "Left"); empty if it has none. */
std::string keyName(int scanCode);

/** The scan code of one of Reluva's key codes (common/events.h); 0 if it has none. */
int scanCode(uint32_t key);

/**
 * The player's controls: each action's binding, kept with the game's
 * options, and how the mouse's buttons are used
 * (docs/engines/stawayteam/systems/menus.md#the-keyboard-screen).
 */
class Controls {
public:
    Controls();

    /** Reads the bindings from `file`; those it hasn't got keep their defaults. */
    void load(const Config &file);
    /** Writes them into `file`. */
    void save(Config &file) const;

    const Binding &binding(size_t action) const { return _bindings[action]; }
    /**
     * Binds `action` to `key` (a scan code) held with `modifier` (0: none);
     * any other action bound to the same keys loses them.
     * @return The names of the actions that lost them.
     */
    std::vector<std::string> bindKey(size_t action, int key, int modifier);
    /**
     * Binds `action` to the joystick's button `button` (from 1); any other
     * action bound to it loses it.
     * @return The names of the actions that lost it.
     */
    std::vector<std::string> bindJoystick(size_t action, int button);
    /** Takes `action`'s key away, keeping its mouse's and joystick's. */
    void clearKey(size_t action);
    /** Every action's default binding back (RESET). */
    void reset();
    /** A binding as the keyboard screen shows it ("], Wheel Down"). */
    std::string text(size_t action) const;

    /** The mouse's scheme: 0, or 1 with its buttons reversed (the "Control Scheme" option). */
    int scheme = 0;

private:
    std::vector<Binding> _bindings;
};

} // namespace reluva::stawayteam
