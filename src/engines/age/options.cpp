// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/age/options.h"

#include "common/audio/mixer.h"
#include "common/events.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace reluva::age {

namespace {

GameOptions& current() {
    static GameOptions options;
    return options;
}

// Each control's name in mmlang.dll (the Customize Controls list), and
// its keys by default: the game's, as its help file lists them ("Driving
// with a Keyboard"). The chat's key isn't listed there; Y is Reluva's.
struct ControlInfo {
    uint32_t stringId;
    const char* name;
    uint32_t first, second;
};
constexpr ControlInfo kInfo[kControls] = {
    {280, "Throttle", key::Up, 0},
    {281, "Brakes", key::Down, 0},
    {283, "Steer Left", key::Left, 0},
    {284, "Steer Right", key::Right, 0},
    {307, "Handbrake", key::Space, 0},
    {279, "Horn", key::Return, 0},
    {291, "Shift Up", 'a', 0},
    {292, "Shift Down", 'z', 0},
    {293, "Reverse", 'r', 0},
    {278, "Transmission", 't', 0},
    {276, "Change Camera", 'c', 0},
    {299, "Map Zoom", 'e', 0},
    {298, "Full Screen Map", 'q', 0},
    {296, "Map Toggle", key::Tab, 0},
    {297, "HUD Toggle", 'h', 0},
    {305, "Rear View Mirror", key::Backspace, 0},
    {294, "Next Checkpoint", 's', 0},
    {295, "Prev. Checkpoint", 'x', 0},
    {277, "Thrill Cam", 'v', 0},
    {286, "Look Left", key::Kp0 + 4, 0},
    {285, "Look Right", key::Kp0 + 6, 0},
    {287, "Look Back", key::Kp0 + 2, 0},
    {288, "Look Forward", key::Kp0 + 8, 0},
    {289, "Wide Angle", 'w', 0},
    {290, "Dashboard On/Off", 'd', 0},
    {300, "Rotating Map", 'f', 0},
    {301, "Toggle CD Player", '2', 0},
    {302, "Start/Stop CD", '3', 0},
    {304, "Prev. CD Track", '4', 0},
    {303, "Next CD Track", '5', 0},
    {306, "Camera Pan", 0, 0}, // no key in the help's tables
    {308, "Opponent Position", 'i', 0},
    {309, "Enter Chat Msg", 'y', 0},
};

const char* kNames[kControls] = {
    "throttle",     "brakes",   "left",      "right",    "handbrake",   "horn", "shiftup",   "shiftdown", "reverse",
    "transmission", "camera",   "mapzoom",   "fullmap",  "maptoggle",   "hud",  "mirror",    "nextcheck", "prevcheck",
    "thrill",       "lookleft", "lookright", "lookback", "lookforward", "wide", "dashboard", "rotatemap", "cd",
    "cdplay",       "cdprev",   "cdnext",    "pan",      "opponent",    "chat"};

float number(const std::string& s, float lo, float hi) { return std::clamp(std::strtof(s.c_str(), nullptr), lo, hi); }

} // namespace

uint32_t controlStringId(Control c) { return kInfo[int(c)].stringId; }
const char* controlFallbackName(Control c) { return kInfo[int(c)].name; }

std::string keyName(uint32_t k) {
    switch (k) {
    case 0:
        return "-";
    case key::Up:
        return "Up";
    case key::Down:
        return "Down";
    case key::Left:
        return "Left";
    case key::Right:
        return "Right";
    case key::Space:
        return "Space";
    case key::Return:
        return "Enter";
    case key::Tab:
        return "Tab";
    case key::Backspace:
        return "Backspace";
    case key::Insert:
        return "Insert";
    case key::Delete:
        return "Delete";
    case key::Home:
        return "Home";
    case key::End:
        return "End";
    case key::PageUp:
        return "Page Up";
    case key::PageDown:
        return "Page Down";
    case key::LShift:
        return "Left Shift";
    case key::RShift:
        return "Right Shift";
    case key::LCtrl:
        return "Left Ctrl";
    case key::RCtrl:
        return "Right Ctrl";
    case key::LAlt:
        return "Left Alt";
    case key::RAlt:
        return "Right Alt";
    case key::KpEnter:
        return "Keypad Enter";
    case key::KpPeriod:
        return "Keypad .";
    default:
        break;
    }
    if (k >= key::F1 && k < key::F1 + 12)
        return "F" + std::to_string(k - key::F1 + 1);
    if (k >= key::Kp0 && k < key::Kp0 + 10)
        return "Keypad " + std::to_string(k - key::Kp0);
    if (k > 32 && k < 127)
        return std::string(1, char(std::toupper(int(k))));
    return "Key " + std::to_string(k);
}

const std::array<Resolution, 5>& resolutions() {
    static const std::array<Resolution, 5> r{{{640, 480}, {800, 600}, {1024, 768}, {1280, 960}, {1600, 1200}}};
    return r;
}

void GameOptions::resetAudio() {
    const GameOptions d;
    soundFx = d.soundFx;
    commentary = d.commentary;
    music = d.music;
    ambient = d.ambient;
    effectsVolume = d.effectsVolume;
    musicVolume = d.musicVolume;
    balance = d.balance;
}

void GameOptions::resetGraphics() {
    const GameOptions d;
    texturedSky = d.texturedSky;
    pedestrians = d.pedestrians;
    smartRendering = d.smartRendering;
    fullScreen = d.fullScreen;
    resolution = d.resolution;
    visibility = d.visibility;
    lightingQuality = d.lightingQuality;
    objectDetail = d.objectDetail;
    textureQuality = d.textureQuality;
}

void GameOptions::resetControls() {
    autoReverse = true;
    startInCar = false;
    steering = 0.5f;
    for (int i = 0; i < kControls; ++i)
        keys[size_t(i)] = {kInfo[i].first, kInfo[i].second};
}

Control GameOptions::controlOf(uint32_t key) const {
    if (key == 0)
        return Control::Count;
    for (int i = 0; i < kControls; ++i)
        if (keys[size_t(i)][0] == key || keys[size_t(i)][1] == key)
            return Control(i);
    return Control::Count;
}

Control GameOptions::otherWith(uint32_t key, Control except) const {
    if (key == 0)
        return Control::Count;
    for (int i = 0; i < kControls; ++i)
        if (Control(i) != except && (keys[size_t(i)][0] == key || keys[size_t(i)][1] == key))
            return Control(i);
    return Control::Count;
}

std::string GameOptions::text() const {
    std::ostringstream o;
    o << "# Reluva's options for Midtown Madness 2\n";
    o << "soundfx " << soundFx << "\ncommentary " << commentary << "\nmusic " << music << "\nambient " << ambient
      << "\neffectsvolume " << effectsVolume << "\nmusicvolume " << musicVolume << "\nbalance " << balance << "\nsky "
      << texturedSky << "\npedestrians " << pedestrians << "\nsmartrendering " << smartRendering << "\nfullscreen "
      << fullScreen << "\nresolution " << resolution << "\nvisibility " << visibility << "\nlighting "
      << lightingQuality << "\nobjectdetail " << objectDetail << "\ntexturequality " << textureQuality
      << "\nautoreverse " << autoReverse << "\nstartincar " << startInCar << "\nsteering " << steering << "\n";
    for (int i = 0; i < kControls; ++i)
        o << "key " << kNames[i] << " " << keys[size_t(i)][0] << " " << keys[size_t(i)][1] << "\n";
    return o.str();
}

Error GameOptions::parse(const std::string& text) {
    *this = GameOptions();
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream l(line);
        std::string name, value;
        if (!(l >> name) || name[0] == '#' || !(l >> value))
            continue;
        const bool on = value == "1";
        if (name == "soundfx")
            soundFx = on;
        else if (name == "commentary")
            commentary = on;
        else if (name == "music")
            music = on;
        else if (name == "ambient")
            ambient = on;
        else if (name == "effectsvolume")
            effectsVolume = number(value, 0, 1);
        else if (name == "musicvolume")
            musicVolume = number(value, 0, 1);
        else if (name == "balance")
            balance = number(value, -1, 1);
        else if (name == "sky")
            texturedSky = on;
        else if (name == "pedestrians")
            pedestrians = on;
        else if (name == "smartrendering")
            smartRendering = on;
        else if (name == "fullscreen")
            fullScreen = on;
        else if (name == "resolution")
            resolution = int(number(value, 0, float(resolutions().size() - 1)));
        else if (name == "visibility")
            visibility = number(value, 0, 1);
        else if (name == "lighting")
            lightingQuality = number(value, 0, 1);
        else if (name == "objectdetail")
            objectDetail = int(number(value, 0, 3));
        else if (name == "texturequality")
            textureQuality = int(number(value, 0, 3));
        else if (name == "autoreverse")
            autoReverse = on;
        else if (name == "startincar")
            startInCar = on;
        else if (name == "steering")
            steering = number(value, 0, 1);
        else if (name == "key") {
            std::string second;
            l >> second;
            for (int i = 0; i < kControls; ++i)
                if (value == kNames[i])
                    keys[size_t(i)] = {uint32_t(std::strtoul(second.c_str(), nullptr, 10)), 0};
            uint32_t other = 0;
            if (l >> other)
                for (int i = 0; i < kControls; ++i)
                    if (value == kNames[i])
                        keys[size_t(i)][1] = other;
        }
    }
    return Error::success();
}

Error GameOptions::load(const std::filesystem::path& folder) {
    if (folder.empty())
        return Error::success();
    std::ifstream in(folder / kFileName, std::ios::binary);
    if (!in)
        return Error::success(); // none yet: the defaults
    std::ostringstream s;
    s << in.rdbuf();
    return parse(s.str());
}

Error GameOptions::save(const std::filesystem::path& folder) const {
    if (folder.empty())
        return Error::fail("no save folder");
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    std::ofstream out(folder / kFileName, std::ios::binary | std::ios::trunc);
    if (!out)
        return Error::fail("can't write " + (folder / kFileName).string());
    out << text();
    return out ? Error::success() : Error::fail("can't write " + (folder / kFileName).string());
}

void applySoundOptions(const GameOptions& o, audio::Mixer* mixer) {
    if (!mixer)
        return;
    auto level = [](bool on, float v) { return on ? int(std::clamp(v, 0.0f, 1.0f) * audio::kMaxVolume) : 0; };
    mixer->setTypeVolume(audio::SoundType::Effects, level(o.soundFx, o.effectsVolume));
    mixer->setTypeVolume(audio::SoundType::Music, level(o.music, o.musicVolume));
    mixer->setTypeVolume(audio::SoundType::Speech, level(o.commentary, 1));
    mixer->setBalance(int(o.balance * audio::kMaxPan));
}

GameOptions loadOptions(const std::filesystem::path& folder) {
    GameOptions o;
    o.load(folder);
    setCurrentOptions(o);
    return o;
}

const GameOptions& currentOptions() { return current(); }
void setCurrentOptions(const GameOptions& options) { current() = options; }

} // namespace reluva::age
