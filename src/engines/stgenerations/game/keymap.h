// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <cstdint>
#include <string>

namespace reluva::stgenerations {

// A key map as the original's SGENS.CFG holds two of them (cfg+0x138 for
// away missions, cfg+0x238 for space combat): 256 bytes indexed by a
// Windows virtual-key code, each the action that key does, 0xFF for none.
// Virtual keys 1, 2 and 4 are the left, right and middle mouse buttons.
class KeyMap {
public:
    static const int kKeys = 256;
    static const uint8_t kNone = 0xFF;

    // The away actions (the switch in 0x43E8FB; action 0 does nothing).
    enum AwayAction {
        kAwayForward = 1,
        kAwayBackward = 2,
        kAwaySlideLeft = 3,
        kAwaySlideRight = 4,
        kAwayLeft = 6,
        kAwayRight = 7,
        kAwayLookUp = 8,
        kAwayLookDown = 9,
        kAwayJump = 10,
        kAwayCrouch = 11,
        kAwayRun = 12,
        kAwayLookCenter = 18,
        kAwayAttack = 19,
        kAwayMouseLook = 52
    };
    // The space combat actions (0x4A6976).
    enum StarshipAction {
        kShipClimb = 0,
        kShipDive = 1,
        kShipRollLeft = 2,
        kShipRollRight = 3,
        kShipLeft = 4,
        kShipRight = 5,
        kShipFaster = 6,
        kShipSlower = 7,
        kShipPhasers = 9,
        kShipPhotons = 10,
        // The helm orders, Close to Maintain Status, are 0x10..0x16.
        kShipHelm = 0x10
    };

    // The Controls panel's actions, in its order (tables 0x548B40 and 0x548B50).
    static const int kAwayActions = 14, kStarshipActions = 17;
    static const uint8_t kAwayPanel[kAwayActions];
    static const uint8_t kStarshipPanel[kStarshipActions];

    KeyMap() { clear(); }
    void clear();
    // The defaults, as 0x488977 and 0x488A23 write them when SGENS.CFG is missing.
    static KeyMap awayDefaults();
    static KeyMap starshipDefaults();

    // The action of virtual key vk, or kNone.
    uint8_t get(int vk) const { return vk >= 0 && vk < kKeys ? _map[vk] : kNone; }
    void set(int vk, uint8_t action) {
        if (vk >= 0 && vk < kKeys)
            _map[vk] = action;
    }
    // The first two keys bound to action, as the Controls panel shows them
    // (0x485CF0): with one, it is first and second is -1; with two, the
    // higher key comes first. -1 for none.
    void findKeys(uint8_t action, int &first, int &second) const;

    // As text for the configuration: "vk:action" pairs in hex, by commas.
    std::string toString() const;
    // Read toString()'s text; false (and the map unchanged) if it isn't valid.
    bool fromString(const std::string &text);

    bool operator==(const KeyMap &o) const;
    bool operator!=(const KeyMap &o) const { return !(*this == o); }

    // The virtual key of a key, as Windows numbers it, or -1.
    static int virtualKey(uint32_t key);
    // The Controls panel's name for a virtual key (0x48639D).
    static std::string keyName(int vk);

private:
    uint8_t _map[kKeys];
};

} // namespace reluva::stgenerations
