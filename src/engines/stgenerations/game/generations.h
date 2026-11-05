// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "engines/stgenerations/audio.h"
#include "common/bytes.h"
#include "engines/stgenerations/platform.h"
#include "engines/stgenerations/game/random.h"
#include "engines/stgenerations/game/bridge.h"
#include "engines/stgenerations/game/campaign.h"
#include "engines/stgenerations/game/combat.h"
#include "engines/stgenerations/formats/font.h"
#include "engines/stgenerations/game/hudscreen.h"
#include "engines/stgenerations/formats/level.h"
#include "engines/stgenerations/game/mission.h"
#include "engines/stgenerations/game/optionsmenu.h"
#include "engines/stgenerations/game/helditem.h"
#include "engines/stgenerations/formats/overlay.h"
#include "engines/stgenerations/game/render3d.h"
#include "engines/stgenerations/formats/resources.h"
#include "engines/stgenerations/game/screen.h"
#include "engines/stgenerations/formats/sprite.h"
#include "engines/stgenerations/game/walker.h"

namespace reluva::stgenerations {

const uint32_t kSaveMagic = tagBE('S', 'T', 'G', 'S');
// 1: the viewer's place; 2: adds a mission in progress; 3: doors, lifts,
// lights and the rest the other missions need; 4: the date saved and the
// campaign; 5: the bridge. Missions saved by an older version can't be read.
const uint32_t kSaveVersion = 6;
const uint32_t kFirstMissionSaveVersion = 3;
const uint32_t kFirstDatedSaveVersion = 4;
const uint32_t kFirstBridgeSaveVersion = 5;

// How the game runs: its folders and the launcher's settings for it
// (stgenerations.cpp lists them), or reluva-tool's options.
struct Options {
    std::filesystem::path gameDir;  // the installed game (or an import of it, discs in disc1/ and disc2/)
    std::filesystem::path cd1, cd2; // the discs' folders, when not in the game's
    std::string description;        // the release, as detection names it
    bool unknownVariant = false;     // found by its executable alone, not by kVariants: warned about
    // Developer: the resource viewer instead of the game; start the story
    // at one of its sections; or straight into space combat n.
    bool viewer = false;
    std::string story;
    int combat = 0;
    // The settings' defaults, when the game's own settings file has none
    // (its menu keeps them there): 0..256 volumes, difficulty 0..2.
    int musicVolume = 192, effectsVolume = 192, speechVolume = 192;
    int difficulty = -1; // as the game's menu starts it
    // For tests: the date saved games are given, in place of the clock's,
    // so runs are the same whenever they're made.
    std::string fixedSaveDate;
    int subtitles = -1, largeWindow = -1, lookspring = -1; // -1: the game's own default
};

// Star Trek: Generations (MicroProse, 1997): the game, from its title to
// the credits (the front end, the story, the bridge, away missions and
// space combat), and the resource viewer over its data.
class Generations {
public:
    // `mixer` may be null (no sound).
    Generations(Platform* system, Options options);
    ~Generations();

    // Runs the game until the player quits.
    Error run();

    Error loadGameState(int slot);
    Error saveGameState(int slot, const std::string& description);

    RandomSource& getRandom() { return _random; }
    bool shouldQuit() const { return _system->shouldQuit(); }
    void quitGame() { _system->quitGame(); }
    void pauseEngine(bool pause);
    bool isPaused() const { return _pauseLevel > 0; }
    // The volumes kept by the game's menu, applied to the mixer.
    void syncSoundSettings();

    // The game's own settings file (its menu's choices), in its folder.
    static constexpr const char* kSettingsFile = "settings.ini";
    // A save slot's file, in the game's folder.
    static std::string getSaveFileName(int slot);

private:
    struct Star {
        int32_t x, y, z;
    };

    void initPalette();
    void initCursor();
    void loadFont();
    void handleEvent(const Event& event);
    void drawFrame(uint32_t delta);
    void drawStarfield(uint32_t delta);
    void drawTextPanel();
    // The 3D page with the away-team console (HudScreen).
    void drawMissionScreen(uint32_t delta);
    // Blend the screen effects into the level's palette.
    void applyScreenEffects();

    void showImage(int index);
    void showOverlay(const ResourceRef& ref);
    void showSprites(const ResourceRef& ref);
    // Load a level; with run false its scripts are set up but not started (a saved game follows).
    void showLevel(const ResourceRef& ref, bool run = true);
    // Page 0 is the map, page 1 the 3D view, page n + 1 the n-th texture.
    void showLevelPage(int page);
    // Handle a key on the 3D view; returns false if it isn't a movement key.
    bool handleWalkKey(const KeyState& state, bool down);
    // Move the camera by the keys held, over delta ms, keeping it on the floor.
    void updateWalk(uint32_t delta);
    void updateWalkStatus();
    // Set up the level's actors, loading their sprite sets.
    void loadActors(const ResourceRef& levelRef);
    // Find a sound file (or the subtitle next to it) by file name,
    // preferring archives whose name starts with archivePrefix.
    bool findSoundFile(const std::string& name, const std::string& archivePrefix, ResourceRef& ref) const;
    // Load the level's GAME.BIN, if there is one, and start its mission.
    void startMission(const ResourceRef& levelRef, bool run = true);
    // The briefing and its PROCEED button, until it is clicked.
    void drawBriefing(Canvas& screen, bool withProceed, bool pressed);
    bool isOverProceed(int x, int y) const;
    // PROCEED: its sound, then the mission (event 6).
    void proceed();
    // A mission transition's blends over the screen (0x43E13A).
    void drawTransition(const Mission::TransitionView& transition, Palette& palette);
    // Jump to the first image of the next (direction 1) or previous (-1) archive.
    void jumpArchive(int direction);
    // Show the next (1) or previous (-1) 3D level, GAME.LV2.
    void jumpLevel(int direction);
    void showStarfield();
    // The starfield with nothing loaded over it, where the mission list is.
    bool isOnStarfield() const;
    void drawMissionList();
    // Start an away mission from the list, in the 3D view.
    void playMission(int index);
    // Back to the list, saying why.
    void leaveMission(const std::string& message);
    // Before rendering a w x h view: where to aim, the item held up, what to pick.
    void prepareAim(int w, int h);
    // After rendering it: the gun held up, or the punch and a crosshair at the centre.
    void drawHands(Canvas& view, uint32_t delta);
    // The scripts' full-view overlay (SHOW_VIEW_OVERLAY), if any.
    void drawViewOverlay(Canvas& view);
    // The mouse cursor: a crosshair to aim with, or an arrow.
    void setCursor(bool target);
    // The player attacks: fires the weapon in use, or else punches.
    void attack();
    // Run the punch animation by delta ms and draw it over view.
    void updatePunch(uint32_t delta, Canvas& view);
    void playSound(int index);
    void playBeep(int frequency);
    // Play one of the movies (e.g. "MOVIE/MOV_01.AVI") full screen, with
    // its sound, until it ends or a key or click skips it. False if it
    // can't be found or read.
    bool playMovie(const std::string& name);

    // The front end (frontend.cpp).
    // A line of rolling text: its gap above, and whether to centre it.
    struct RollLine {
        std::string text;
        int gap;
        bool centred;
    };
    // The title, then the menu, and the story from STATES.INI; false (and
    // the resource viewer) if the data for it isn't there.
    bool startFrontEnd();
    // For testing: the story from one of its sections, as if a new game had got there.
    void startStoryAt(const std::string& section);
    void loadSettings();
    // Keep the settings and apply the volumes.
    void saveSettings();
    // Run the story until a mission starts or the game ends.
    void runCampaign();
    bool startCampaignMission(int type);
    // The campaign's mission is over: on to whatever comes next.
    void campaignMissionEnded();
    void openMenu(bool canReturn);
    // resume: back to what the menu was opened over (the bridge, say).
    void closeMenu(bool resume = true);
    // Show the bridge (true colour), or put it away.
    void setBridgeMode(bool on);
    // A frame of the bridge, with a command of the script.
    void updateBridge(uint32_t delta);
    // Start space combat n; false if it can't be (the story goes on).
    bool playSpaceCombat(int number);
    // Switch the screen to true colour (the bridge, combat) or back.
    void setTrueColour(bool on);
    // A frame of space combat.
    void updateCombat(uint32_t delta);
    void handleMenuEvent(const Event& event);
    void playMenuSound(int id);
    std::vector<OptionsMenu::Slot> listSaves();
    bool readSaveHeader(int slot, std::string& description, std::string& date);
    // When a game is saved, as the menu shows it ("11/05 21:52").
    std::string saveDate() const;
    // Poll events; true on a key or click (or quitting).
    bool waitForSkip();
    // Show a bitmap full screen in its own palette.
    bool showPicture(const ResourceRef& ref, BitmapDecoder& decoder);
    void showIntro();
    // Play a track from MUSIC.1 or MUSIC.2.
    bool playMusic(const std::string& name);
    // Roll lines of text up over a background while the music plays.
    void rollText(const BitmapDecoder& background, const Font& font, const std::vector<RollLine>& lines,
                  const std::vector<std::string>& music);
    // The ending texts, TEXTn.TXT.
    void showScrollText(int number);
    bool loadCredits(std::vector<RollLine>& lines);
    void showCredits();


    Platform* _system;
    Platform* _eventMan; // the same Platform: the events and what's held
    std::unique_ptr<Mixer> _mixerWrapper;
    Mixer* _mixer = nullptr;
    Options _options;
    int _pauseLevel = 0;
    // The menu's settings and the volumes, kept in kSettingsFile.
    std::map<std::string, std::string> _storedSettings;
    RandomSource _random;
    std::unique_ptr<Screen> _screen;
    ResourceManager _resources;

    std::vector<Star> _stars;
    Font _font;
    Palette _fontPalette;
    bool _hasFontPalette = false;
    BitmapDecoder _image;
    // The mission's BRIEFING.BMP, for its opening transition.
    BitmapDecoder _briefing;
    bool _briefingLoaded = false;
    // The briefing waits for its PROCEED button (COMMON.DAT's PROCEED.BMP,
    // the normal look below the pressed one) before the mission starts.
    BitmapDecoder _proceed;
    bool _proceedLoaded = false;
    int _proceedX = 0, _proceedY = 0;
    bool _awaitingProceed = false, _proceedPressed = false;
    // The game as the first phase of a transition last showed it.
    Canvas _transitionFrom;
    RandomSource _dissolveRandom;
    bool _imageLoaded = false;
    Overlay _overlay;
    bool _overlayLoaded = false;
    int _overlayFrameHeight = 0;
    int _overlayFrame = 0;
    uint32_t _overlayTime = 0; // also used to animate sprites
    SpriteSet _sprites;
    bool _spritesLoaded = false;
    std::unique_ptr<LevelFile> _level;
    std::string _levelName;
    ResourceRef _levelRef;
    std::vector<uint16_t> _levelTextures;
    int _levelPage = 0;
    std::unique_ptr<LevelRenderer> _levelRenderer;
    std::unique_ptr<Walker> _walker;
    std::unique_ptr<Actors> _actors;
    std::vector<SpriteDraw> _actorSprites;
    std::unique_ptr<ScriptFile> _levelScript;
    // Declared last of the level's parts, so it goes first.
    std::unique_ptr<Mission> _mission;
    // The away-team console, when the game's art for it is there.
    std::unique_ptr<HudScreen> _hudScreen;
    // Whether screen effects changed the palette last frame.
    bool _paletteBlended = false;
    Camera _camera;
    uint32_t _levelTime = 0;
    enum WalkKey { kWalkForward, kWalkBack, kWalkTurnLeft, kWalkTurnRight, kWalkStrafeLeft, kWalkStrafeRight,
                   kWalkLookUp, kWalkLookDown, kWalkCrouch, kWalkJump, kWalkKeyCount };
    bool _walkKeys[kWalkKeyCount] = {};
    // A jump asked for, taken at the next step; and the low gravity jump's wait (ms).
    bool _jumpPressed = false;
    uint32_t _jumpWait = 0;
    // The mouse, where the weapon aims (in the view), and the view's size.
    int _mouseX = -1, _mouseY = -1;
    int _aimX = 0, _aimY = 0, _viewW = 1, _viewH = 1;
    bool _targetCursor = false;
    HeldItem _heldItem;
    // How much longer the gun shows its muzzle flash, in ms.
    uint32_t _flashLeft = 0;
    // Set while a fire key is held: one attack per press.
    bool _fireHeld = false;
    Overlay _viewOverlay;
    std::string _viewOverlayImage;
    bool _viewOverlayLoaded = false;
    // The bare-hand attack's overlay, as the original's view.vt130 runs it.
    Overlay _punchOverlay;
    std::string _punchImage;
    int _punchFrameHeight = 0;
    bool _punching = false;
    bool _punchLanded = false;
    int _punchFrame = 0;
    uint32_t _punchTime = 0;
    int16_t _shownHealth = -1;
    int _imageIndex = -1;
    int _soundIndex = -1;
    SoundHandle _soundHandle;
    std::string _status;   // first line of the text panel
    std::string _subtitle; // shown while the current sound plays
    // The subtitle is a mission's, shown until _subtitleUntil (level time).
    bool _missionSubtitle = false;
    uint32_t _subtitleUntil = 0;
    // The away missions installed, and the one chosen or being played.
    struct MissionEntry {
        std::string name;
        ResourceRef level;
    };
    std::vector<MissionEntry> _missions;
    int _movieChoice = 0;
    int _missionChoice = 0;
    bool _playingMission = false;
    std::string _missionMessage;
    // GET_ARRAY/SET_ARRAY's words, carried from one mission to the next.
    std::vector<int16_t> _carriedArray;
    bool _dirty = true;

    // Playing the game, rather than viewing its files.
    bool _frontEnd = false;
    Campaign _campaign;
    std::unique_ptr<OptionsMenu> _menu;
    bool _menuOpen = false;
    // The palette under the menu.
    Palette _gamePalette;
    GameSettings _settings;
    SoundHandle _menuSound;
    std::unique_ptr<Bridge> _bridge;
    std::unique_ptr<SpaceCombat> _combat;
    bool _bridgeMode = false;
    bool _menuOverBridge = false;
    Canvas _bridgeSurface;
    SoundHandle _musicHandle;
};

} // namespace reluva::stgenerations

