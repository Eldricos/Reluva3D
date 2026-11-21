// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The player's options, as the game's Options menus set them: sound,
// graphics and the controls, the keys among them. Kept in Reluva's own
// text file (options.txt) in the game's save folder; the game's own
// settings are in the Windows registry. See
// docs/engines/age/games/mm2/systems/options.md.

#include "audio/mixer.h"
#include "common/types.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace reluva::age {

/** What a key can be bound to (Customize Controls), named by mmlang.dll. */
enum class Control : int {
    kThrottle,
    kBrakes,
    kSteerLeft,
    kSteerRight,
    kHandbrake,
    kHorn,
    kShiftUp,
    kShiftDown,
    kReverse,
    kTransmission,
    kChangeCamera,
    kMapZoom,
    kFullScreenMap,
    kMapToggle,
    kHudToggle,
    kRearViewMirror,
    kNextCheckpoint,
    kPrevCheckpoint,
    kThrillCam,
    kLookLeft,
    kLookRight,
    kLookBack,
    kLookForward,
    kWideAngle,
    kDashboard,
    kRotatingMap,
    kCdToggle,
    kCdPlay,
    kCdPrev,
    kCdNext,
    kCameraPan,
    kOpponentPosition,
    kChat,
    kCount
};
constexpr int kControls = int(Control::kCount);
/** The id of a control's name in mmlang.dll. */
uint32_t controlStringId(Control c);
/** A control's name in English, for when mmlang.dll hasn't it. */
const char *controlFallbackName(Control c);
/** A key's name, as Customize Controls shows it ("Up", "A", "Space"). */
std::string keyName(uint32_t key);

/** The player's options, as options.txt keeps them. */
struct GameOptions {
    static constexpr const char *kFileName = "options.txt";

    // Audio Options.
    bool soundFx = true;
    bool commentary = true;
    bool music = true;
    bool ambient = true;
    float effectsVolume = 1;  // Sound FX Volume, 0..1
    float musicVolume = 0.8f; // Music/City Volume
    float balance = 0;        // -1 left .. 1 right
    // Graphics Options.
    bool texturedSky = true;
    bool pedestrians = true;
    bool smartRendering = true;
    bool vehicleReflections = true;
    bool fullScreen = false;
    int resolution = 0;        // into resolutions()
    float visibility = 1;      // 0..1: how far the city is drawn
    float lightingQuality = 1; // 0..1: below half, no headlight pools
    int objectDetail = 3;      // 0 Low .. 3 Very High
    int textureQuality = 3;    // the same
    int cloudShadows = 2;      // 0 None, 1 Low, 2 High
    // Control Options.
    bool autoReverse = true;   // the throttle held against a wall backs the car away
    bool startInCar = false;   // the in-car view first (POV)
    bool forceFeedback = true; // a game controller shakes when the car's knocked
    float steering = 0.5f;     // Steering Sensitivity, 0..1
    // CONTROLLERS: what drives, as mmlang.dll's 581-584 name it. A game
    // pad by default, so one plugged in works at once; the keyboard
    // always does.
    int controller = kGamePad;
    float deadZone = 0.3f; // Joystick Dead Zone, 0..1 of the bar: up to half the stick's travel
    // The network menus' meeting server ("host" or "host:port"), as last
    // typed; empty for none.
    std::string meetingServer;
    // Two keys for each control (0: none).
    std::array<std::array<uint32_t, 2>, kControls> keys{};

    /** What drives (controller). */
    enum : int { kKeyboardOnly = 0, kJoystick = 1, kGamePad = 2, kSteeringWheel = 3 };

    /** The defaults, the keys the game's own (its help file's). */
    GameOptions() { resetControls(); }
    /** The Audio Options back to their defaults. */
    void resetAudio();
    /** The Graphics Options back to their defaults. */
    void resetGraphics();
    /** The Control Options back to their defaults, the keys too. */
    void resetControls();

    /**
     * Reads options.txt from the save folder.
     * @return Success, keeping the defaults, when there's no folder or no file yet.
     */
    Error load(const std::filesystem::path &folder);
    /** Writes options.txt into the save folder, making the folder if need be; fails with no folder. */
    Error save(const std::filesystem::path &folder) const;
    /**
     * Reads options.txt's text over the defaults: numbers out of range are
     * clamped, and lines it doesn't know are skipped, so it never fails.
     */
    Error parse(const std::string &text);
    /** The options as options.txt holds them: a line a setting, then a line a control's keys. */
    std::string text() const;

    /** The control a key is bound to, Control::kCount if none (or the key is 0). */
    Control controlOf(uint32_t key) const;
    /**
     * Another control with this key, Control::kCount if none (for the
     * "duplicate key" warning).
     */
    Control otherWith(uint32_t key, Control except) const;

    /**
     * What the options mean in numbers: how far the city is drawn (metres),
     * and how fast the steering turns (CarSim::steerRate).
     */
    ///@{
    float farClip() const { return 800 + visibility * 3200; }
    /** How fast the wheels turn as steered: 0.5 to 1.5 times the car's own, by the sensitivity. */
    float steerRate() const { return 0.5f + steering; }
    ///@}
};

/** A screen size, in pixels. */
struct Resolution {
    int w;
    int h;
};
/** The Graphics Options' screen sizes, 640 x 480 to 1600 x 1200. */
const std::array<Resolution, 5> &resolutions();

/**
 * The sound levels into the mixer: each kind's volume, on or off, and
 * the balance.
 */
void applySoundOptions(const GameOptions &options, audio::Mixer *mixer);
/** The options file read (none: the defaults) and put in force. */
GameOptions loadOptions(const std::filesystem::path &folder);

/**
 * The options in force, for what reads the keys and the sound levels;
 * the race screen loads them and the menus change them.
 */
///@{
const GameOptions &currentOptions();
/** Puts @p options in force (they're copied). */
void setCurrentOptions(const GameOptions &options);
///@}

} // namespace reluva::age
