// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/idtech3/client/bindings.h"

#include "common/fs.h"

#include <cstdio>
#include <cstdlib>

namespace reluva::idtech3 {

namespace {

// The names of keys that aren't printable characters, as config files
// write them, by key number.
struct KeyName {
    int32_t key;
    const char *name;
};
constexpr KeyName kKeyNames[] = {
    {q3key::kTab, "TAB"},
    {q3key::kEnter, "ENTER"},
    {q3key::kEscape, "ESCAPE"},
    {q3key::kSpace, "SPACE"},
    {q3key::kBackspace, "BACKSPACE"},
    {129, "CAPSLOCK"},
    {q3key::kPause, "PAUSE"},
    {q3key::kUp, "UPARROW"},
    {q3key::kDown, "DOWNARROW"},
    {q3key::kLeft, "LEFTARROW"},
    {q3key::kRight, "RIGHTARROW"},
    {q3key::kAlt, "ALT"},
    {q3key::kCtrl, "CTRL"},
    {q3key::kShift, "SHIFT"},
    {q3key::kInsert, "INS"},
    {q3key::kDelete, "DEL"},
    {q3key::kPageDown, "PGDN"},
    {q3key::kPageUp, "PGUP"},
    {q3key::kHome, "HOME"},
    {q3key::kEnd, "END"},
    {160, "KP_HOME"},
    {161, "KP_UPARROW"},
    {162, "KP_PGUP"},
    {163, "KP_LEFTARROW"},
    {164, "KP_5"},
    {165, "KP_RIGHTARROW"},
    {166, "KP_END"},
    {167, "KP_DOWNARROW"},
    {168, "KP_PGDN"},
    {q3key::kKpEnter, "KP_ENTER"},
    {170, "KP_INS"},
    {171, "KP_DEL"},
    {172, "KP_SLASH"},
    {173, "KP_MINUS"},
    {174, "KP_PLUS"},
    {175, "KP_NUMLOCK"},
    {176, "KP_STAR"},
    {177, "KP_EQUALS"},
    {q3key::kMouse1, "MOUSE1"},
    {q3key::kMouse2, "MOUSE2"},
    {q3key::kMouse3, "MOUSE3"},
    {q3key::kMouse4, "MOUSE4"},
    {q3key::kMouse5, "MOUSE5"},
    {q3key::kWheelDown, "MWHEELDOWN"},
    {q3key::kWheelUp, "MWHEELUP"},
    {';', "SEMICOLON"},
    // The joystick's buttons, and the original's names for 217 to 232
    // (nothing presses those).
    {185, "JOY1"},
    {186, "JOY2"},
    {187, "JOY3"},
    {188, "JOY4"},
    {189, "JOY5"},
    {190, "JOY6"},
    {191, "JOY7"},
    {192, "JOY8"},
    {193, "JOY9"},
    {194, "JOY10"},
    {195, "JOY11"},
    {196, "JOY12"},
    {197, "JOY13"},
    {198, "JOY14"},
    {199, "JOY15"},
    {200, "JOY16"},
    {201, "JOY17"},
    {202, "JOY18"},
    {203, "JOY19"},
    {204, "JOY20"},
    {205, "JOY21"},
    {206, "JOY22"},
    {207, "JOY23"},
    {208, "JOY24"},
    {209, "JOY25"},
    {210, "JOY26"},
    {211, "JOY27"},
    {212, "JOY28"},
    {213, "JOY29"},
    {214, "JOY30"},
    {215, "JOY31"},
    {216, "JOY32"},
    {217, "AUX1"},
    {218, "AUX2"},
    {219, "AUX3"},
    {220, "AUX4"},
    {221, "AUX5"},
    {222, "AUX6"},
    {223, "AUX7"},
    {224, "AUX8"},
    {225, "AUX9"},
    {226, "AUX10"},
    {227, "AUX11"},
    {228, "AUX12"},
    {229, "AUX13"},
    {230, "AUX14"},
    {231, "AUX15"},
    {232, "AUX16"},
};

} // namespace

int32_t toQ3Key(uint32_t k) {
    switch (k) {
    case key::kTab:
        return q3key::kTab;
    case key::kReturn:
        return q3key::kEnter;
    case key::kKpEnter:
        return q3key::kKpEnter;
    case key::kEscape:
        return q3key::kEscape;
    case key::kSpace:
        return q3key::kSpace;
    case key::kBackspace:
        return q3key::kBackspace;
    case key::kDelete:
        return q3key::kDelete;
    case key::kUp:
        return q3key::kUp;
    case key::kDown:
        return q3key::kDown;
    case key::kLeft:
        return q3key::kLeft;
    case key::kRight:
        return q3key::kRight;
    case key::kInsert:
        return q3key::kInsert;
    case key::kHome:
        return q3key::kHome;
    case key::kEnd:
        return q3key::kEnd;
    case key::kPageUp:
        return q3key::kPageUp;
    case key::kPageDown:
        return q3key::kPageDown;
    case key::kLShift:
    case key::kRShift:
        return q3key::kShift;
    case key::kLCtrl:
    case key::kRCtrl:
        return q3key::kCtrl;
    case key::kLAlt:
    case key::kRAlt:
        return q3key::kAlt;
    default:
        break;
    }
    if (k >= key::kF1 && k < key::kF1 + 12)
        return q3key::kF1 + int32_t(k - key::kF1);
    if (k > 32 && k < 127)
        return int32_t(k);
    return 0;
}

int32_t toQ3Button(MouseButton b) {
    switch (b) {
    case MouseButton::kLeft:
        return q3key::kMouse1;
    case MouseButton::kRight:
        return q3key::kMouse2;
    case MouseButton::kMiddle:
        return q3key::kMouse3;
    default:
        return 0;
    }
}

std::string KeyBindings::keyName(int32_t key) {
    for (const KeyName &n : kKeyNames)
        if (n.key == key)
            return n.name;
    if (key >= q3key::kF1 && key < q3key::kF1 + 15)
        return "F" + std::to_string(key - q3key::kF1 + 1);
    if (key > 32 && key < 127)
        return std::string(1, char(key));
    // Others by number, as the original writes them.
    char hex[8];
    std::snprintf(hex, sizeof hex, "0x%02x", unsigned(key & 0xff));
    return hex;
}

int32_t KeyBindings::keyNumber(std::string_view name) {
    if (name.empty())
        return -1;
    if (name.size() == 1) {
        const auto c = static_cast<unsigned char>(name[0]);
        // Letters are bound in lower case.
        return c >= 'A' && c <= 'Z' ? int32_t(c - 'A' + 'a') : int32_t(c);
    }
    const std::string lower = fs::toLower(name);
    for (const KeyName &n : kKeyNames)
        if (lower == fs::toLower(n.name))
            return n.key;
    if (lower.size() >= 2 && lower[0] == 'f') {
        const int f = std::atoi(lower.c_str() + 1);
        if (f >= 1 && f <= 15)
            return q3key::kF1 + f - 1;
    }
    if (lower.size() == 4 && lower[0] == '0' && lower[1] == 'x')
        return int32_t(std::strtol(lower.c_str() + 2, nullptr, 16));
    return -1;
}

const std::string &KeyBindings::get(int32_t key) const {
    static const std::string kNone;
    return key >= 0 && key < q3key::kCount ? _commands[size_t(key)] : kNone;
}

void KeyBindings::set(int32_t key, std::string command) {
    if (key < 0 || key >= q3key::kCount || _commands[size_t(key)] == command)
        return;
    _commands[size_t(key)] = std::move(command);
    changed = true;
}

void KeyBindings::clear() {
    for (std::string &c : _commands)
        if (!c.empty()) {
            c.clear();
            changed = true;
        }
}

bool KeyBindings::empty() const {
    for (const std::string &c : _commands)
        if (!c.empty())
            return false;
    return true;
}

std::vector<std::string> splitCommand(std::string_view line) {
    std::vector<std::string> words;
    size_t i = 0;
    while (true) {
        while (i < line.size() && static_cast<unsigned char>(line[i]) <= ' ')
            ++i;
        if (i >= line.size() || (line[i] == '/' && i + 1 < line.size() && line[i + 1] == '/'))
            return words;
        std::string word;
        if (line[i] == '"') {
            ++i;
            while (i < line.size() && line[i] != '"')
                word += line[i++];
            if (i < line.size())
                ++i;
        } else {
            while (i < line.size() && static_cast<unsigned char>(line[i]) > ' ')
                word += line[i++];
        }
        words.push_back(std::move(word));
    }
}

std::vector<std::string> splitCommands(std::string_view text) {
    std::vector<std::string> out;
    std::string current;
    bool quoted = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '"')
            quoted = !quoted;
        if (c == '/' && !quoted && i + 1 < text.size() && text[i + 1] == '/') {
            // A comment, to the end of the line.
            while (i + 1 < text.size() && text[i + 1] != '\n')
                ++i;
            continue;
        }
        if (c == '\n' || c == '\r' || (c == ';' && !quoted)) {
            if (c != ';')
                quoted = false;
            if (!splitCommand(current).empty())
                out.push_back(current);
            current.clear();
            continue;
        }
        current += c;
    }
    if (!splitCommand(current).empty())
        out.push_back(current);
    return out;
}

void KeyBindings::execute(std::string_view text, CvarStore &cvars, int *ignored,
                          const std::function<bool(const std::string &, std::string &)> &readFile, int depth) {
    for (const std::string &line : splitCommands(text)) {
        const std::vector<std::string> w = splitCommand(line);
        const std::string verb = fs::toLower(w[0]);
        if (verb == "bind" && w.size() >= 3) {
            const int32_t key = keyNumber(w[1]);
            if (key >= 0) {
                // The rest of the line, unquoted words joined.
                std::string command = w[2];
                for (size_t i = 3; i < w.size(); ++i)
                    command += " " + w[i];
                set(key, command);
                continue;
            }
        } else if (verb == "unbind" && w.size() >= 2 && keyNumber(w[1]) >= 0) {
            set(keyNumber(w[1]), {});
            continue;
        } else if (verb == "unbindall") {
            clear();
            continue;
        } else if ((verb == "set" || verb == "seta" || verb == "sets" || verb == "setu") && w.size() >= 3) {
            const int32_t flags = verb == "seta"   ? CvarStore::kArchive
                                  : verb == "sets" ? CvarStore::kServerInfo
                                  : verb == "setu" ? CvarStore::kUserInfo
                                                   : 0;
            std::string value = w[2];
            for (size_t i = 3; i < w.size(); ++i)
                value += " " + w[i];
            cvars.create(w[1], value, flags);
            cvars.set(w[1], value);
            continue;
        } else if (verb == "exec" && w.size() >= 2 && readFile && depth < 8) {
            std::string name = w[1];
            std::string contents;
            if (name.find('.') == std::string::npos)
                name += ".cfg";
            if (readFile(name, contents))
                execute(contents, cvars, ignored, readFile, depth + 1);
            continue;
        }
        if (ignored)
            ++*ignored;
    }
}

std::string KeyBindings::write() const {
    std::string out = "unbindall\n";
    for (int32_t key = 0; key < q3key::kCount; ++key) {
        const std::string &c = _commands[size_t(key)];
        if (c.empty())
            continue;
        // The quote mark by number, as it can't be written quoted.
        const std::string name = key == '"' ? "0x22" : keyName(key);
        out += "bind " + name + " \"" + c + "\"\n";
    }
    return out;
}

} // namespace reluva::idtech3
