// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The player's options, as the game's Options menus set them: sound,
// graphics and the controls, the keys among them. Kept in Reluva's own
// text file (options.txt) in the game's save folder; the game's own
// settings are in the Windows registry. See docs/engines/age-menus.md.

#include "common/audio/mixer.h"
#include "common/types.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace reluva::age {

// What a key can be bound to (Customize Controls), named by mmlang.dll.
enum class Control : int {
    Throttle,
    Brakes,
    SteerLeft,
    SteerRight,
    Handbrake,
    Horn,
    ShiftUp,
    ShiftDown,
    Reverse,
    Transmission,
    ChangeCamera,
    MapZoom,
    FullScreenMap,
    Chat,
    Count
};
constexpr int kControls = int(Control::Count);
uint32_t controlStringId(Control c); // its name in mmlang.dll
const char* controlFallbackName(Control c);
// A key's name, as Customize Controls shows it ("Up", "A", "Space").
std::string keyName(uint32_t key);

struct GameOptions {
    static constexpr const char* kFileName = "options.txt";

    // Audio Options.
    bool soundFx = true, commentary = true, music = true, ambient = true;
    float effectsVolume = 1;  // Sound FX Volume, 0..1
    float musicVolume = 0.8f; // Music/City Volume
    float balance = 0;        // -1 left .. 1 right
    // Graphics Options.
    bool texturedSky = true, pedestrians = true, smartRendering = true;
    bool fullScreen = false;
    int resolution = 0;        // into resolutions()
    float visibility = 1;      // 0..1: how far the city is drawn
    float lightingQuality = 1; // 0..1: below half, no headlight pools
    int objectDetail = 3;      // 0 Low .. 3 Very High
    // Control Options.
    bool autoReverse = true; // the brake backs up when stopped
    bool startInCar = false; // the in-car view first (POV)
    float steering = 0.5f;   // Steering Sensitivity, 0..1
    // Two keys for each control (0: none).
    std::array<std::array<uint32_t, 2>, kControls> keys{};

    GameOptions() { resetControls(); }
    void resetAudio();
    void resetGraphics();
    void resetControls(); // the keys too

    Error load(const std::filesystem::path& folder);
    Error save(const std::filesystem::path& folder) const;
    Error parse(const std::string& text);
    std::string text() const;

    // The control a key is bound to, Control::Count if none.
    Control controlOf(uint32_t key) const;
    // Another control with this key, Control::Count if none (for the
    // "duplicate key" warning).
    Control otherWith(uint32_t key, Control except) const;

    // What the options mean in numbers.
    float farClip() const { return 800 + visibility * 3200; }
    float steerRate() const { return 0.5f + steering; }
};

struct Resolution {
    int w, h;
};
const std::array<Resolution, 5>& resolutions();

// The sound levels into the mixer: each kind's volume, on or off, and
// the balance.
void applySoundOptions(const GameOptions& options, audio::Mixer* mixer);
// The options file read (none: the defaults) and put in force.
GameOptions loadOptions(const std::filesystem::path& folder);

// The options in force, for what reads the keys and the sound levels;
// the Race page loads them and the menus change them.
const GameOptions& currentOptions();
void setCurrentOptions(const GameOptions& options);

} // namespace reluva::age
