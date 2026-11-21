// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/game/controls.h"

#include "common/events.h"

#include <cstdio>
#include <cstdlib>
#include <map>

namespace reluva::stawayteam {

namespace {

// The section the program keeps the bindings in, with no joystick
// (0x63b7a4, 0x63b788), and the options' for the mouse's scheme (0x63f218).
const char *const kSection = "NO JOYSTICK Control Configuration";
const char *const kOptions = "Options";
const char *const kScheme = "Control Scheme";
constexpr int kWheelDown = 8;
constexpr int kWheelUp = 16;

} // namespace

const std::vector<Action> &gameActions() {
    // Each action's id, category, name and default binding, as the program
    // registers them (0x5bd649 to 0x5bdea9).
    static const std::vector<Action> kActions = {
        {0xd, "Movement", "Walk", {0x11, 0, 0, 0}},
        {0xb, "Movement", "Run", {0x13, 0, 0, 0}},
        {0xc, "Movement", "Crouch", {0x2e, 0, 0, 0}},
        {0x4, "Movement", "Scroll Map Left", {0x14b, 0, 0, 0}},
        {0x5, "Movement", "Scroll Map Right", {0x14d, 0, 0, 0}},
        {0x6, "Movement", "Scroll Map Up", {0x148, 0, 0, 0}},
        {0x7, "Movement", "Scroll Map Down", {0x150, 0, 0, 0}},
        {0x0, "General", "Pause", {0x1c, 0, 0, 0}},
        {0xe, "General", "Toggle Vision Cones", {0x2f, 0, 0, 0}},
        {0xf, "General", "Toggle Sound Cones", {0x1f, 0, 0, 0}},
        {0x10, "General", "Cycle Vision Cones", {0xf, 0, 0, 0}},
        {0x9, "General", "Cycle Movement", {0x2d, 0, 0, 0}},
        {0x8, "General", "Cancel Orders", {0xe, 0, 0, 0}},
        {0x2a, "General", "Chat (Network Only)", {0x14, 0, 0, 0}},
        {0x3, "General", "Select Entire Team", {0x1e, 0, 0, 0}},
        {0xa, "General", "Skip In-game Movie", {0x39, 0, 0, 0}},
        {0x2b, "General", "Display Objectives", {0x18, 0, 0, 0}},
        {0x2c, "General", "Toggle Minimap", {0x32, 0, 0, 0}},
        {0x2d, "General", "Zoom In", {0x4e, 0, 0, 0}},
        {0x2e, "General", "Zoom Out", {0x4a, 0, 0, 0}},
        {0x2f, "General", "Zoom Reset", {0x37, 0, 0, 0}},
        {0x1, "Inventory", "Inventory Next", {0x1b, 0, 0, kWheelDown}},
        {0x2, "Inventory", "Inventory Prev", {0x1a, 0, 0, kWheelUp}},
        {0x14, "Inventory", "Hypospray", {0x23, 0, 0, 0}},
        {0x17, "Inventory", "Enhanced Mind Meld", {0x20, 0, 0, 0}},
        {0x19, "Inventory", "Cloaking Band", {}},
        {0x1b, "Inventory", "Tricorder", {}},
        {0x1a, "Inventory", "Medical Tricorder", {}},
        {0x1e, "Inventory", "Extractor", {}},
        {0x20, "Inventory", "Herbal Healing", {}},
        {0x21, "Inventory", "Stasis Field Generator", {}},
        {0x22, "Inventory", "Teleportation Brig", {}},
        {0x23, "Inventory", "Brig Controller", {}},
        {0x27, "Inventory", "Engineering Tech Kit", {}},
        {0x28, "Inventory", "Computer Systems", {}},
        {0x29, "Inventory", "Security Systems", {}},
        {0x11, "Weapons", "Phaser", {0x19, 0, 0, 0}},
        {0x15, "Weapons", "Sniper Rifle", {0x21, 0, 0, 0}},
        {0x12, "Weapons", "Concussion Grenade", {0x22, 0, 0, 0}},
        {0x18, "Weapons", "EMP Grenade", {0x12, 0, 0, 0}},
        {0x13, "Weapons", "Neural Disruptor", {0x31, 0, 0, 0}},
        {0x24, "Weapons", "Phaser Rifle", {}},
        {0x25, "Weapons", "Proximity Mines", {}},
        {0x26, "Weapons", "Remote Mines", {}},
        {0x1c, "Weapons", "Audio Decoy", {}},
        {0x1d, "Weapons", "Holographic Projector", {}},
        {0x16, "Weapons", "Vulcan Nerve Pinch", {0x25, 0, 0, 0}},
        {0x1f, "Weapons", "Herbal Paralysis", {}},
    };
    return kActions;
}

std::string keyName(int code) {
    // The game asks Windows (GetKeyNameTextA); these are the US keyboard's
    // keys under the names it gives them.
    static const std::map<int, const char *> kNames = {
        {0x01, "Esc"},         {0x02, "1"},       {0x03, "2"},          {0x04, "3"},           {0x05, "4"},
        {0x06, "5"},           {0x07, "6"},       {0x08, "7"},          {0x09, "8"},           {0x0a, "9"},
        {0x0b, "0"},           {0x0c, "-"},       {0x0d, "="},          {0x0e, "Backspace"},   {0x0f, "Tab"},
        {0x10, "Q"},           {0x11, "W"},       {0x12, "E"},          {0x13, "R"},           {0x14, "T"},
        {0x15, "Y"},           {0x16, "U"},       {0x17, "I"},          {0x18, "O"},           {0x19, "P"},
        {0x1a, "["},           {0x1b, "]"},       {0x1c, "Enter"},      {0x1d, "Ctrl"},        {0x1e, "A"},
        {0x1f, "S"},           {0x20, "D"},       {0x21, "F"},          {0x22, "G"},           {0x23, "H"},
        {0x24, "J"},           {0x25, "K"},       {0x26, "L"},          {0x27, ";"},           {0x28, "'"},
        {0x29, "`"},           {0x2a, "Shift"},   {0x2b, "\\"},         {0x2c, "Z"},           {0x2d, "X"},
        {0x2e, "C"},           {0x2f, "V"},       {0x30, "B"},          {0x31, "N"},           {0x32, "M"},
        {0x33, ","},           {0x34, "."},       {0x35, "/"},          {0x36, "Right Shift"}, {0x37, "Num *"},
        {0x38, "Alt"},         {0x39, "Space"},   {0x3a, "Caps Lock"},  {0x3b, "F1"},          {0x3c, "F2"},
        {0x3d, "F3"},          {0x3e, "F4"},      {0x3f, "F5"},         {0x40, "F6"},          {0x41, "F7"},
        {0x42, "F8"},          {0x43, "F9"},      {0x44, "F10"},        {0x46, "Scroll Lock"}, {0x47, "Num 7"},
        {0x48, "Num 8"},       {0x49, "Num 9"},   {0x4a, "Num -"},      {0x4b, "Num 4"},       {0x4c, "Num 5"},
        {0x4d, "Num 6"},       {0x4e, "Num +"},   {0x4f, "Num 1"},      {0x50, "Num 2"},       {0x51, "Num 3"},
        {0x52, "Num 0"},       {0x53, "Num Del"}, {0x57, "F11"},        {0x58, "F12"},         {0x11c, "Num Enter"},
        {0x11d, "Right Ctrl"}, {0x135, "Num /"},  {0x138, "Right Alt"}, {0x147, "Home"},       {0x148, "Up"},
        {0x149, "Page Up"},    {0x14b, "Left"},   {0x14d, "Right"},     {0x14f, "End"},        {0x150, "Down"},
        {0x151, "Page Down"},  {0x152, "Insert"}, {0x153, "Delete"},
    };
    const auto it = kNames.find(code);
    return it == kNames.end() ? std::string() : it->second;
}

int scanCode(uint32_t k) {
    static const std::map<uint32_t, int> kCodes = [] {
        std::map<uint32_t, int> m;
        const char *rows[] = {"1234567890-=", "qwertyuiop[]", "asdfghjkl;'`", "\\zxcvbnm,./"};
        const int starts[] = {0x02, 0x10, 0x1e, 0x2b};
        for (int r = 0; r < 4; ++r)
            for (int i = 0; rows[r][i]; ++i)
                m[uint32_t(static_cast<unsigned char>(rows[r][i]))] = starts[r] + i;
        m[key::kEscape] = 0x01;
        m[key::kBackspace] = 0x0e;
        m[key::kTab] = 0x0f;
        m[key::kReturn] = 0x1c;
        m[key::kSpace] = 0x39;
        m[key::kLCtrl] = 0x1d;
        m[key::kLShift] = 0x2a;
        m[key::kRShift] = 0x36;
        m[key::kLAlt] = 0x38;
        m[key::kRCtrl] = 0x11d;
        m[key::kRAlt] = 0x138;
        for (int i = 0; i < 10; ++i)
            m[key::kF1 + uint32_t(i)] = 0x3b + i;
        m[key::kF1 + 10] = 0x57;
        m[key::kF1 + 11] = 0x58;
        const int keypad[] = {0x52, 0x4f, 0x50, 0x51, 0x4b, 0x4c, 0x4d, 0x47, 0x48, 0x49};
        for (int i = 0; i < 10; ++i)
            m[key::kKp0 + uint32_t(i)] = keypad[i];
        m[key::kKpPeriod] = 0x53;
        m[key::kKpEnter] = 0x11c;
        m[key::kKpPlus] = 0x4e;
        m[key::kKpMinus] = 0x4a;
        m[key::kKpMultiply] = 0x37;
        m[key::kKpDivide] = 0x135;
        m[key::kHome] = 0x147;
        m[key::kUp] = 0x148;
        m[key::kPageUp] = 0x149;
        m[key::kLeft] = 0x14b;
        m[key::kRight] = 0x14d;
        m[key::kEnd] = 0x14f;
        m[key::kDown] = 0x150;
        m[key::kPageDown] = 0x151;
        m[key::kInsert] = 0x152;
        m[key::kDelete] = 0x153;
        return m;
    }();
    const auto it = kCodes.find(k);
    return it == kCodes.end() ? 0 : it->second;
}

Controls::Controls() {
    reset();
}

void Controls::reset() {
    _bindings.clear();
    for (const Action &action : gameActions())
        _bindings.push_back(action.defaults);
}

void Controls::load(const Config &file) {
    const auto &actions = gameActions();
    for (size_t i = 0; i < actions.size(); ++i) {
        // "Category Name" = "key,modifier,joystick,mouse" (0x4846b6).
        const std::string value = file.get(kSection, std::string(actions[i].category) + " " + actions[i].name);
        Binding b;
        if (std::sscanf(value.c_str(), "%d,%d,%d,%d", &b.key, &b.modifier, &b.joystick, &b.mouse) == 4)
            _bindings[i] = b;
    }
    scheme = std::atoi(file.get(kOptions, kScheme, "0").c_str()) == 1 ? 1 : 0;
}

void Controls::save(Config &file) const {
    const auto &actions = gameActions();
    for (size_t i = 0; i < actions.size(); ++i) {
        const Binding &b = _bindings[i];
        char value[64];
        std::snprintf(value, sizeof value, "%d,%d,%d,%d", b.key, b.modifier, b.joystick, b.mouse);
        file.set(kSection, std::string(actions[i].category) + " " + actions[i].name, value);
    }
    file.set(kOptions, kScheme, std::to_string(scheme));
}

std::vector<std::string> Controls::bindKey(size_t action, int key, int modifier) {
    std::vector<std::string> lost;
    for (size_t i = 0; i < _bindings.size(); ++i)
        if (i != action && _bindings[i].key == key && _bindings[i].modifier == modifier) {
            _bindings[i].key = 0;
            _bindings[i].modifier = 0;
            lost.emplace_back(gameActions()[i].name);
        }
    _bindings[action].key = key;
    _bindings[action].modifier = modifier;
    return lost;
}

void Controls::clearKey(size_t action) {
    _bindings[action].key = 0;
    _bindings[action].modifier = 0;
}

std::string Controls::text(size_t action) const {
    // The key (after the key held with it and "+"), the joystick's button,
    // then the mouse's, joined by "+", all apart by ", " (0x484f00).
    const Binding &b = _bindings[action];
    std::vector<std::string> parts;
    if (b.key) {
        std::string k;
        if (b.modifier)
            k = keyName(b.modifier) + "+";
        parts.push_back(k + keyName(b.key));
    }
    if (b.joystick)
        parts.push_back("Joystick Button " + std::to_string(b.joystick));
    std::string mouse;
    for (int bit = 0; bit < 5; ++bit) {
        const int mask = 1 << bit;
        if (!(b.mouse & mask))
            continue;
        if (!mouse.empty())
            mouse += "+";
        mouse += mask == kWheelDown ? "Wheel Down"
                 : mask == kWheelUp ? "Wheel Up"
                                    : "Mouse Button " + std::to_string(bit + 1);
    }
    if (!mouse.empty())
        parts.push_back(mouse);
    std::string out;
    for (const std::string &p : parts)
        out += (out.empty() ? "" : ", ") + p;
    return out;
}

} // namespace reluva::stawayteam
