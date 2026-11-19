// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/game/keymap.h"

#include <cstdlib>
#include <cstring>

#include "common/events.h"
#include "common/text.h"

namespace reluva::stgenerations {

const uint8_t KeyMap::kAwayPanel[kAwayActions] = {
    kAwayAttack, kAwayForward, kAwayLeft, kAwayRight, kAwayBackward, kAwaySlideLeft, kAwaySlideRight,
    kAwayJump, kAwayCrouch, kAwayRun, kAwayLookUp, kAwayLookDown, kAwayLookCenter, kAwayMouseLook
};

const uint8_t KeyMap::kStarshipPanel[kStarshipActions] = {
    kShipPhasers, kShipPhotons, kShipFaster, kShipSlower, kShipLeft, kShipRight, kShipRollLeft, kShipRollRight,
    kShipClimb, kShipDive, kShipHelm + 0, kShipHelm + 1, kShipHelm + 2, kShipHelm + 3, kShipHelm + 4,
    kShipHelm + 5, kShipHelm + 6
};

void KeyMap::clear() {
    memset(_map, kNone, sizeof(_map));
}

KeyMap KeyMap::awayDefaults() {
    // 0x488977.
    KeyMap m;
    m.set(0x26, kAwayForward);    // Up
    m.set(0x28, kAwayBackward);   // Down
    m.set(0x25, kAwayLeft);       // Left
    m.set(0x27, kAwayRight);      // Right
    m.set(0x21, kAwayLookUp);     // Page Up
    m.set(0x22, kAwayLookDown);   // Page Down
    m.set(0x0D, kAwayJump);       // Enter
    m.set(0x11, kAwayCrouch);     // Ctrl
    m.set(0x2D, kAwayAttack);     // Insert
    m.set(0x01, kAwayAttack);     // left button
    m.set(0x02, kAwayMouseLook);  // right button
    m.set(0x24, kAwayLookCenter); // Home
    m.set(0x10, kAwayRun);        // Shift
    m.set(0x6A, kAwaySlideRight); // Num *
    m.set(0x6F, kAwaySlideLeft);  // Num /
    return m;
}

KeyMap KeyMap::starshipDefaults() {
    // 0x488A23.
    KeyMap m;
    m.set(0x26, kShipDive);      // Up
    m.set(0x68, kShipDive);      // Num 8
    m.set(0x28, kShipClimb);     // Down
    m.set(0x62, kShipClimb);     // Num 2
    m.set(0x25, kShipLeft);      // Left
    m.set(0x64, kShipLeft);      // Num 4
    m.set(0x27, kShipRight);     // Right
    m.set(0x66, kShipRight);     // Num 6
    m.set(0x11, kShipRollLeft);  // Ctrl
    m.set(0x60, kShipRollRight); // Num 0
    m.set(0x10, kShipPhasers);   // Shift
    m.set(0x0D, kShipPhotons);   // Enter
    m.set(0x6B, kShipFaster);    // Num +
    m.set(0x6D, kShipSlower);    // Num -
    for (int i = 0; i < 7; i++)
        m.set('1' + i, kShipHelm + i);
    m.set(0x65, kShipHelm + 3); // Num 5: Full Stop
    return m;
}

void KeyMap::findKeys(uint8_t action, int& first, int& second) const {
    // 0x485CF0 keeps the first key found in the low uint8_t until a second
    // shifts it up; the panel shows the low uint8_t first.
    int found[2] = { -1, -1 };
    int count = 0;
    for (int vk = 0; vk < kKeys && count < 2; vk++) {
        if (_map[vk] == action)
            found[count++] = vk;
    }
    first = count == 2 ? found[1] : found[0];
    second = count == 2 ? found[0] : -1;
}

std::string KeyMap::toString() const {
    std::string out;
    for (int vk = 0; vk < kKeys; vk++) {
        if (_map[vk] == kNone)
            continue;
        if (!out.empty())
            out += ",";
        out += text::format("%x:%x", vk, _map[vk]);
    }
    return out;
}

bool KeyMap::fromString(const std::string& input) {
    KeyMap m;
    for (const std::string& pair : text::split(input, ',')) {
        std::string item = text::trim(pair);
        if (item.empty())
            continue;
        size_t colon = item.find(':');
        if (colon == std::string::npos)
            return false;
        char* end;
        long vk = strtol(item.c_str(), &end, 16);
        if (end != item.c_str() + colon || vk < 0 || vk >= kKeys)
            return false;
        long action = strtol(item.c_str() + colon + 1, &end, 16);
        if (*end || end == item.c_str() + colon + 1 || action < 0 || action > 0xFF)
            return false;
        m._map[vk] = (uint8_t)action;
    }
    *this = m;
    return true;
}

bool KeyMap::operator==(const KeyMap& o) const {
    return memcmp(_map, o._map, sizeof(_map)) == 0;
}

int KeyMap::virtualKey(uint32_t key) {
    if (key >= 'a' && key <= 'z')
        return 'A' + (key - 'a');
    if (key >= '0' && key <= '9')
        return key;
    if (key >= reluva::key::Kp0 && key <= (reluva::key::Kp0 + 9))
        return 0x60 + (key - reluva::key::Kp0);
    if (key >= reluva::key::F1 && key <= (reluva::key::F1 + 11))
        return 0x70 + (key - reluva::key::F1);
    switch (key) {
    case reluva::key::Backspace:
        return 0x08;
    case reluva::key::Tab:
        return 0x09;
    case reluva::key::Return:
    case reluva::key::KpEnter:
        return 0x0D;
    case reluva::key::LShift:
    case reluva::key::RShift:
        return 0x10;
    case reluva::key::LCtrl:
    case reluva::key::RCtrl:
        return 0x11;
    case reluva::key::LAlt:
    case reluva::key::RAlt:
        return 0x12;
    case reluva::key::Escape:
        return 0x1B;
    case reluva::key::Space:
        return 0x20;
    case reluva::key::PageUp:
        return 0x21;
    case reluva::key::PageDown:
        return 0x22;
    case reluva::key::End:
        return 0x23;
    case reluva::key::Home:
        return 0x24;
    case reluva::key::Left:
        return 0x25;
    case reluva::key::Up:
        return 0x26;
    case reluva::key::Right:
        return 0x27;
    case reluva::key::Down:
        return 0x28;
    case reluva::key::Insert:
        return 0x2D;
    case reluva::key::Delete:
        return 0x2E;
    case reluva::key::KpMultiply:
        return 0x6A;
    case reluva::key::KpPlus:
        return 0x6B;
    case reluva::key::KpMinus:
        return 0x6D;
    case reluva::key::KpPeriod:
        return 0x6E;
    case reluva::key::KpDivide:
        return 0x6F;
    case ';':
        return 0xBA;
    case '=':
        return 0xBB;
    case ',':
        return 0xBC;
    case '-':
        return 0xBD;
    case '.':
        return 0xBE;
    case '/':
        return 0xBF;
    case '`':
        return 0xC0;
    case '[':
        return 0xDB;
    case '\\':
        return 0xDC;
    case ']':
        return 0xDD;
    case '\'':
        return 0xDE;
    default:
        return -1;
    }
}

std::string KeyMap::keyName(int vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
        return std::string(1, (char)vk);
    if (vk >= 0x60 && vk <= 0x69)
        return text::format("num %d", vk - 0x60);
    if (vk >= 0x70 && vk <= 0x7B)
        return text::format("F%d", vk - 0x6F);
    switch (vk) {
    case 0x01:
        return "mb 1";
    case 0x02:
        return "mb 2";
    case 0x04:
        return "mb 3";
    case 0x08:
        return "bs";
    case 0x09:
        return "tab";
    case 0x0D:
        return "enter";
    case 0x10:
        return "shift";
    case 0x11:
        return "ctrl";
    case 0x12:
        return "alt";
    case 0x13:
        return "pause";
    case 0x20:
        return "space";
    case 0x21:
        return "pgup";
    case 0x22:
        return "pgdn";
    case 0x23:
        return "end";
    case 0x24:
        return "home";
    case 0x25:
        return "left";
    case 0x26:
        return "up";
    case 0x27:
        return "right";
    case 0x28:
        return "down";
    case 0x2D:
        return "ins";
    case 0x2E:
        return "del";
    case 0x6A:
        return "num *";
    case 0x6B:
        return "num +";
    case 0x6D:
        return "num -";
    case 0x6E:
        return "num .";
    case 0x6F:
        return "num /";
    case 0x90:
        return "numlk";
    case 0x91:
        return "scrlk";
    case 0xBA:
        return ";";
    case 0xBB:
        return "=";
    case 0xBC:
        return ",";
    case 0xBD:
        return "-";
    case 0xBE:
        return ".";
    case 0xBF:
        return "/";
    case 0xC0:
        return "`";
    case 0xDB:
        return "[";
    case 0xDC:
        return "\\";
    case 0xDD:
        return "]";
    case 0xDE:
        return "'";
    default:
        return "?!?";
    }
}

} // namespace reluva::stgenerations
