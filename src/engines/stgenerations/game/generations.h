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

/** A saved game's first four bytes. */
const uint32_t kSaveMagic = tagBE('S', 'T', 'G', 'S');
/**
 * The version saved games are written in. 1: the data viewer's place; 2:
 * adds a mission in progress; 3: doors, lifts, lights and the rest the
 * other missions need; 4: the date saved and the campaign; 5: the bridge;
 * 6: low gravity, safe flats and the walker's step-down; 7: actors' guard
 * targets. Missions saved by an older version than 3 can't be read.
 */
const uint32_t kSaveVersion = 7;
/** The first versions that hold a mission, the date and the bridge. */
///@{
const uint32_t kFirstMissionSaveVersion = 3;
const uint32_t kFirstDatedSaveVersion = 4;
const uint32_t kFirstBridgeSaveVersion = 5;
///@}

/**
 * How the game runs: its folders and the launcher's settings for it
 * (stgenerations.cpp lists them), or reluva-tool's options.
 */
struct Options {
    std::filesystem::path gameDir; // the installed game (or an import of it, discs in disc1/ and disc2/)
    // the discs' folders, when not in the game's
    std::filesystem::path cd1;
    std::filesystem::path cd2;
    std::string description;     // the release, as detection names it
    bool unknownVariant = false; // found by its executable alone, not by kVariants: warned about
    // Developer: start the story at one of its sections, or straight into
    // space combat n.
    std::string story;
    int combat = 0;
    // The settings' defaults, when the game's own settings file has none
    // (its menu keeps them there): 0..256 volumes, difficulty 0..2.
    int musicVolume = 192;
    int effectsVolume = 192;
    int speechVolume = 192;
    int difficulty = -1; // as the game's menu starts it
    // For tests: the date saved games are given, in place of the clock's,
    // so runs are the same whenever they're made.
    std::string fixedSaveDate;
    // -1: the game's own default
    int subtitles = -1;
    int largeWindow = -1;
    int lookspring = -1;
};

/**
 * Star Trek: Generations (MicroProse, 1997): the game, from its title to
 * the credits (the front end, the story, the bridge, away missions and
 * space combat). The data viewer (pages/resource_viewer.h, in reluva-viewer
 * only) is the same class without the front end: what it changes is the
 * virtual methods below.
 */
class Generations {
public:
    /** The game on `system`, which it doesn't own; with no mixer there, no sound. */
    Generations(Platform *system, Options options);
    virtual ~Generations();

    /**
     * Opens the game's files and runs the game until the player quits; an
     * Error if start() gives one.
     */
    Error run();

    /** Loads a saved game from a slot; an Error saying why it can't be. */
    Error loadGameState(int slot);
    /**
     * Saves the game in a slot; an Error saying why it can't be (a mission
     * waiting for PROCEED can't be saved).
     */
    Error saveGameState(int slot, const std::string &description);

    /** The game's random numbers. */
    RandomSource &getRandom() { return _random; }
    /** Whether the player has quit, and quitting. */
    ///@{
    bool shouldQuit() const { return _system->shouldQuit(); }
    void quitGame() { _system->quitGame(); }
    ///@}
    /**
     * Pauses the game, or lets it go on: pauses nest, and the sound is
     * paused at the first and resumed at the last.
     */
    void pauseEngine(bool pause);
    /** Whether the game is paused. */
    bool isPaused() const { return _pauseLevel > 0; }
    /** Applies the volumes kept by the game's menu to the mixer. */
    void syncSoundSettings();

    /** The game's own settings file (its menu's choices), in its folder. */
    static constexpr const char *kSettingsFile = "settings.ini";
    /** A save slot's file, in the game's folder: "stg.s00" and on. */
    static std::string getSaveFileName(int slot);

protected:
    // The palette with nothing of the game's up: black, a grey ramp (the
    // viewer's stars), a 6x6x6 colour cube, the frame's orange and white.
    static constexpr uint8_t kColorBlack = 0;
    static constexpr uint8_t kColorStarFirst = 1;
    static constexpr uint8_t kColorStarLast = 32;
    static constexpr uint8_t kColorCubeFirst = 33; // to 248
    static constexpr uint8_t kColorFrame = 254;
    static constexpr uint8_t kColorWhite = 255;
    // The status and subtitle lines under a page.
    static constexpr int kTextPanelHeight = 60;
    // A level's pages: its floor plan, the 3D view, then its textures.
    static constexpr int kLevelPageMap = 0;
    static constexpr int kLevelPage3D = 1;
    static constexpr int kLevelPageTextures = 2;

    /**
     * What run() starts once the game's files are open: the front end,
     * or an Error saying what of it is missing.
     */
    virtual Error start();
    /**
     * A key pressed with no menu, bridge, combat or walking to take it:
     * Esc opens the menu.
     */
    virtual void keyDown(const KeyState &kbd);
    /** A left click off the 3D view, with nothing else to take it. */
    virtual void click(int x, int y);
    /**
     * A frame with no 3D view, menu, bridge or combat up; false if it
     * put the last frame back on screen unchanged (no text panel drawn).
     */
    virtual bool drawPage(uint32_t delta);
    /** The away mission being played is over. */
    virtual void missionOver();
    /** The level put away (a mission left): nothing on screen. */
    virtual void clearView();
    /**
     * A saved game with neither the story nor a mission in it: the place
     * the data viewer saved (its image and sound, -1 for none).
     */
    virtual Error restorePlace(int imageIndex, int soundIndex);
    /** Frees the level and everything running on it. */
    void unloadLevel();

    /** Sets the palette with nothing of the game's up (kColorBlack and the rest). */
    void initPalette();
    /** Sets the arrow pointer, and shows it. */
    void initCursor();
    /** Loads the text font (COMMON.DAT's ACTION.FNT) and the palette its colours are in. */
    void loadFont();
    /** Hands an event to whatever is up: the menu, combat, the bridge, the briefing or the 3D view. */
    void handleEvent(const Event &event);
    /** Draws a frame of whatever is up, `delta` ms after the last. */
    void drawFrame(uint32_t delta);
    /** The status and subtitle lines under a page, in the font's own colours. */
    void drawTextPanel();
    /** The 3D page with the away-team console (HudScreen). */
    void drawMissionScreen(uint32_t delta);
    /** Blends the screen effects and any transition into the level's palette. */
    void applyScreenEffects();

    /** Loads a level; with run false its scripts are set up but not started (a saved game follows). */
    void showLevel(const ResourceRef &ref, bool run = true);
    /** Page 0 is the map, page 1 the 3D view, page n + 1 the n-th texture; it wraps round. */
    void showLevelPage(int page);
    /**
     * Handles a key on the 3D view, through the player's key map and then
     * the engine's own keys; false if neither has a use for it.
     */
    bool handleWalkKey(const KeyState &state, bool down);
    /** An away action from the key map (KeyMap::AwayAction) pressed or released; false if it has no use here. */
    bool walkAction(int action, bool down);
    /** A mouse button (virtual key 1, 2 or 4) pressed or released over the 3D view. */
    void viewButton(int vk, bool down);
    /** Lets go of every walking key and button (0x43F552). */
    void releaseWalkKeys();
    /** While Mouse Look is held: the turn (radians a second) and look (pitch a second) the mouse asks for. */
    void mouseLookRates(double &turn, double &look) const;
    /** Moves the camera by the keys held, over delta ms, keeping it on the floor. */
    void updateWalk(uint32_t delta);
    /** Sets the status line to the 3D view's readout (in developer builds; else clears it). */
    void updateWalkStatus();
    /** Sets up the level's actors, loading their sprite sets. */
    void loadActors(const ResourceRef &levelRef);
    /**
     * Finds a sound file (or the subtitle next to it) by file name,
     * preferring archives whose name starts with archivePrefix.
     */
    bool findSoundFile(const std::string &name, const std::string &archivePrefix, ResourceRef &ref) const;
    /** Loads the level's GAME.BIN, if there is one, and starts its mission (unless run is false). */
    void startMission(const ResourceRef &levelRef, bool run = true);
    /** The briefing and its PROCEED button, until it is clicked. */
    void drawBriefing(Canvas &screen, bool withProceed, bool pressed);
    /** Whether (x, y) is on the PROCEED button. */
    bool isOverProceed(int x, int y) const;
    /** PROCEED: its sound, then the mission (event 6). */
    void proceed();
    /** A mission transition's blends over the screen (0x43E13A). */
    void drawTransition(const Mission::TransitionView &transition, Palette &palette);
    /** Starts an away mission from the list, in the 3D view. */
    void playMission(int index);
    /** Back to the list, saying why. */
    void leaveMission(const std::string &message);
    /** Before rendering a w x h view: where to aim, the item held up, what to pick. */
    void prepareAim(int w, int h);
    /** After rendering it: the gun held up, or the punch and a crosshair at the centre. */
    void drawHands(Canvas &view, uint32_t delta);
    /** The scripts' full-view overlay (SHOW_VIEW_OVERLAY), if any. */
    void drawViewOverlay(Canvas &view);
    /** The mouse cursor: a crosshair to aim with, or an arrow. */
    void setCursor(bool target);
    /** The player attacks: fires the weapon in use, or else punches. */
    void attack();
    /** Runs the punch animation by delta ms and draws it over view. */
    void updatePunch(uint32_t delta, Canvas &view);
    /**
     * Plays one of the movies (e.g. "MOVIE/MOV_01.AVI") full screen, with
     * its sound, until it ends or a key or click skips it. False if it
     * can't be found or read.
     */
    bool playMovie(const std::string &name);

    // The front end (frontend.cpp).

    /** A line of rolling text: its gap above, and whether to centre it. */
    struct RollLine {
        std::string text;
        int gap;
        bool centred;
    };
    /**
     * The title, then the menu, and the story from STATES.INI; an Error
     * saying what's missing if the data for it isn't there.
     */
    Error startFrontEnd();
    /** For testing: the story from one of its sections, as if a new game had got there. */
    void startStoryAt(const std::string &section);
    /**
     * Reads the menu's settings from kSettingsFile, the launcher's
     * settings for those it hasn't set, and applies the volumes.
     */
    void loadSettings();
    /** Keeps the settings and applies the volumes. */
    void saveSettings();
    /** Runs the story until a mission, combat or the bridge takes over, or the game ends. */
    void runCampaign();
    /** Starts the away mission of a game class (Location::type); false if it isn't installed. */
    bool startCampaignMission(int type);
    /** The campaign's mission is over: on to whatever comes next. */
    void campaignMissionEnded();
    /** Opens the menu; with canReturn, it can go back to what it was opened over. */
    void openMenu(bool canReturn);
    /** Closes the menu; with resume, back to what it was opened over (the bridge, say). */
    void closeMenu(bool resume = true);
    /** Shows the bridge (true colour), or puts it away. */
    void setBridgeMode(bool on);
    /** A frame of the bridge, with a command of the script. */
    void updateBridge(uint32_t delta);
    /** Starts space combat n; false if it can't be (the story goes on, the fight taken as won). */
    bool playSpaceCombat(int number);
    /** Switches the screen to true colour (the bridge, combat) or back. */
    void setTrueColour(bool on);
    /** A frame of space combat. */
    void updateCombat(uint32_t delta);
    /** Hands an event to the menu, and acts on what the player chose there. */
    void handleMenuEvent(const Event &event);
    /** Plays a menu sound from OPTIONS.DAT: M_`id`.WAV from 2000 on, X_`id`.WAV below. */
    void playMenuSound(int id);
    /** The save slots for the menu, the automatic save's last. */
    std::vector<OptionsMenu::Slot> listSaves();
    /** A slot's description and date (empty before version 4); false if it holds no saved game. */
    bool readSaveHeader(int slot, std::string &description, std::string &date);
    /** When a game is saved, as the menu shows it ("11/05 21:52"). */
    std::string saveDate() const;
    /** Polls events; true on a key or click (or quitting). */
    bool waitForSkip();
    /** Shows a bitmap full screen in its own palette; false if it isn't an 8-bit one that can be read. */
    bool showPicture(const ResourceRef &ref, BitmapDecoder &decoder);
    /** The title (INTRO1.BMP) for five seconds, or until skipped. */
    void showIntro();
    /** Plays a track from MUSIC.1 or MUSIC.2; false if it isn't there. */
    bool playMusic(const std::string &name);
    /** Rolls lines of text up over a background while the music plays. */
    void rollText(const BitmapDecoder &background, const Font &font, const std::vector<RollLine> &lines,
                  const std::vector<std::string> &music);
    /** The ending texts, TEXTn.TXT. */
    void showScrollText(int number);
    /** Reads the credits from their table in SGENS.EXE; false if it isn't there or can't be read. */
    bool loadCredits(std::vector<RollLine> &lines);
    /** Rolls the credits, with their music. */
    void showCredits();

    Platform *_system;
    Platform *_eventMan; // the same Platform: the events and what's held
    std::unique_ptr<Mixer> _mixerWrapper;
    Mixer *_mixer = nullptr;
    Options _options;
    int _pauseLevel = 0;
    // The menu's settings and the volumes, kept in kSettingsFile.
    std::map<std::string, std::string> _storedSettings;
    RandomSource _random;
    std::unique_ptr<Screen> _screen;
    ResourceManager _resources;

    Font _font;
    Palette _fontPalette;
    bool _hasFontPalette = false;
    // The mission's BRIEFING.BMP, for its opening transition.
    BitmapDecoder _briefing;
    bool _briefingLoaded = false;
    // The briefing waits for its PROCEED button (COMMON.DAT's PROCEED.BMP,
    // the normal look below the pressed one) before the mission starts.
    BitmapDecoder _proceed;
    bool _proceedLoaded = false;
    int _proceedX = 0;
    int _proceedY = 0;
    bool _awaitingProceed = false;
    bool _proceedPressed = false;
    // The game as the first phase of a transition last showed it.
    Canvas _transitionFrom;
    RandomSource _dissolveRandom;
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
    /** The walking keys held, indexing _walkKeys. */
    enum WalkKey {
        kWalkForward,
        kWalkBack,
        kWalkTurnLeft,
        kWalkTurnRight,
        kWalkStrafeLeft,
        kWalkStrafeRight,
        kWalkLookUp,
        kWalkLookDown,
        kWalkCrouch,
        kWalkJump,
        kWalkRun,
        kWalkKeyCount
    };
    bool _walkKeys[kWalkKeyCount] = {};
    // A jump asked for, taken at the next step; and the low gravity jump's wait (ms).
    bool _jumpPressed = false;
    uint32_t _jumpWait = 0;
    // The mouse, where the weapon aims (in the view), and the view's size.
    int _mouseX = -1;
    int _mouseY = -1;
    int _aimX = 0;
    int _aimY = 0;
    int _viewW = 1;
    int _viewH = 1;
    bool _targetCursor = false;
    HeldItem _heldItem;
    // How much longer the gun shows its muzzle flash, in ms.
    uint32_t _flashLeft = 0;
    // Set while a fire key is held: one attack per press.
    bool _fireHeld = false;
    // Mouse Look held (game+0xE8): the mouse turns the view.
    bool _mouseLook = false;
    // The mouse buttons pressed over the view, by virtual key.
    int _viewButtons = 0;
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
    // The data viewer's place, kept in saved games (-1 in the game's).
    int _imageIndex = -1;
    int _soundIndex = -1;
    // The data viewer's sound, whose subtitle shows while it plays.
    SoundHandle _soundHandle;
    std::string _status;   // first line of the text panel
    std::string _subtitle; // shown while the current sound plays
    // The subtitle is a mission's, shown until _subtitleUntil (level time).
    bool _missionSubtitle = false;
    uint32_t _subtitleUntil = 0;
    // The away missions installed, and the one chosen or being played.
    /** An away mission installed: its folder's name and its level. */
    struct MissionEntry {
        std::string name;
        ResourceRef level;
    };
    std::vector<MissionEntry> _missions;
    int _missionChoice = 0;
    bool _playingMission = false;
    std::string _missionMessage;
    // GET_ARRAY/SET_ARRAY's words, carried from one mission to the next.
    std::vector<int16_t> _carriedArray;
    bool _dirty = true;

    // Playing the game (the front end started), not the data viewer.
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
