// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "engines/stgenerations/platform.h"
#include "engines/stgenerations/formats/font.h"
#include "engines/stgenerations/formats/framedimage.h"
#include "engines/stgenerations/game/keymap.h"
#include "engines/stgenerations/formats/resources.h"

namespace reluva::stgenerations {

// The player's settings, as the original's SGENS.CFG holds them.
struct GameSettings {
    static const int kMaxVolume = 29;
    int musicVolume = 20;
    int effectsVolume = 20;
    // The configuration's +0x340, which the scripts read: the menu shows 0 as Captain, 1 Lieutenant, 2 Ensign.
    int difficulty = 1;
    bool largeWindow = false;
    bool subtitles = true;
    bool lookspring = false;
    KeyMap awayKeys = KeyMap::awayDefaults();
    KeyMap starshipKeys = KeyMap::starshipDefaults();

    bool operator==(const GameSettings &o) const {
        return musicVolume == o.musicVolume && effectsVolume == o.effectsVolume && difficulty == o.difficulty &&
               largeWindow == o.largeWindow && subtitles == o.subtitles && lookspring == o.lookspring &&
               awayKeys == o.awayKeys && starshipKeys == o.starshipKeys;
    }
    bool operator!=(const GameSettings &o) const { return !(*this == o); }
};

// The options screen (sgens.exe 0x47DCBB, run by 0x481221): CAPTAINS.BMP
// with Load, Save, New, Quit, Settings and Controls down the left, the
// picture area to the right where each opens its 320 x 360 panel, and
// Return to Game, or Accept and Cancel while a panel is open.
//
// Every bitmap comes from OPTIONS.DAT. Buttons are strips of frames counted
// from the bottom: 0 dim, 1 normal, 2 lit.
//
// The engine runs it: it passes the mouse and keys in, draws it, plays
// the sounds it asks for and acts on its result.
class OptionsMenu {
public:
    enum Result {
        kNone,
        kReturn,  // back to the game
        kNewGame, // start again
        kLoad,    // load getSlot()
        kSave,    // save the game in getSlot() as getSaveName()
        kQuit     // credits, then quit
    };
    enum Panel {
        kPanelMain = 0,
        kPanelLoad = 1,
        kPanelSave = 2,
        kPanelSettings = 3,
        kPanelQuit = 4,
        kPanelNew = 5,
        kPanelControls = 6
    };
    // A saved-game slot as the Load and Save panels list it.
    struct Slot {
        bool used = false;
        std::string name;
        std::string date;
    };
    static const int kSlots = 9;
    // The automatic save: the Load panel's bottom row.
    static const int kAutoSaveSlot = 9;

    // Sounds by the original's numbers: X_1000..1003, M_2000..2002.
    typedef std::function<void(int id)> SoundCallback;

    explicit OptionsMenu(const ResourceManager &resources);

    // Load the screen; false if OPTIONS.DAT doesn't have it.
    bool load();
    const Palette &getPalette() const { return _palette; }

    // Show the main panel. canReturn: there is a game to go back to;
    // canSave: it can be saved; canChangeDifficulty: no mission is running
    // (the original allows it on the bridge only).
    void open(const GameSettings &settings, bool canReturn, bool canSave, bool canChangeDifficulty);
    // Saving now saves the start of a space combat: the Save panel says so.
    void setEncounter(bool encounter) { _encounter = encounter; }
    // The saved games, kSlots of them and then the automatic one.
    void setSlots(const std::vector<Slot> &slots) { _slots = slots; }
    void setSoundCallback(SoundCallback callback) { _sound = std::move(callback); }

    // button: the virtual key of the mouse button, 1 left, 2 right, 4 middle.
    void mouseDown(int x, int y, int button = 1);
    void mouseMove(int x, int y);
    void mouseUp(int x, int y);
    void keyDown(const KeyState &key);

    void draw(Canvas &screen) const;

    Panel getPanel() const { return _panel; }
    // What the player chose, once; kNone until then.
    Result takeResult();
    int getSlot() const { return _selected; }
    const std::string &getSaveName() const { return _editName; }
    // A saved game's name is being typed in.
    bool isTyping() const { return _panel == kPanelSave && _selected >= 0; }
    // The settings, as last accepted.
    const GameSettings &getSettings() const { return _settings; }
    // True once after the settings were changed.
    bool takeSettingsChanged();

private:
    enum Widget {
        kLoadButton,
        kSaveButton,
        kNewButton,
        kQuitButton,
        kSettingsButton,
        kControlsButton,
        kReturnButton,
        kAcceptButton,
        kCancelButton,
        kAutoSaveButton,
        kDifficultyLabel,
        kDifficultyValue,
        kWindowLabel,
        kWindowValue,
        kSubtitlesLabel,
        kSubtitlesValue,
        kStarshipTab,
        kAwayTab,
        kLookspringLabel,
        kLookspringValue,
        kWidgetCount
    };
    struct WidgetInfo {
        const char *image;
        int frames;
        // On the screen, or within the panel for panel widgets.
        Rect rect;
        bool inPanel;
    };
    static const WidgetInfo kWidgets[kWidgetCount];

    const FramedImage *image(const std::string &name, int frames = 1);
    const FramedImage *find(const std::string &name) const;
    // The widget under (x, y) that can be clicked now, or -1.
    int hit(int x, int y) const;
    bool isShown(int widget) const;
    bool isEnabled(int widget) const;
    Rect screenRect(int widget) const;
    void drawWidget(Canvas &screen, int widget, int frame) const;
    void drawText(Canvas &screen, const std::string &text, int x, int y) const;
    void drawSlider(Canvas &screen, const Rect &rect, int value) const;
    void click(int widget);
    void openPanel(Panel panel);
    void accept();
    void cancel();
    // A click on a slot row, or the sliders.
    bool clickPanel(int x, int y, bool drag);
    // The Controls panel's key cell under (x, y), or -1.
    int hitCell(int x, int y) const;
    // The cell's rectangle within the panel (0x48074F: 32 x 26, two to an action).
    static Rect cellRect(int cell);
    // Fill the cells from the working key maps, as the panel opens (the loop at 0x4852A0).
    void fillCells();
    // Give the captured cell's action the key vk (0x483CAB).
    void assignKey(int vk);
    void play(int id);

    const ResourceManager &_resources;
    Palette _palette;
    // Each colour at two fifths of its brightness, for options that do nothing here.
    uint8_t _dim[256] = {};
    std::map<std::string, std::unique_ptr<FramedImage>> _images;
    Font _font;

    GameSettings _settings;
    GameSettings _working;
    bool _settingsChanged = false;
    bool _canReturn = false;
    bool _canSave = false;
    bool _canChangeDifficulty = true;
    Panel _panel = kPanelMain;
    bool _starshipPage = false;
    bool _encounter = false;
    // The keys the cells show, away then starship, -1 for none.
    int _cellKeys[2][KeyMap::kStarshipActions * 2];
    // The cell waiting for a key, or -1.
    int _captureCell = -1;
    std::vector<Slot> _slots;
    int _selected = -1;
    std::string _editName;
    int _pressed = -1;
    bool _dragging = false;
    Result _result = kNone;
    SoundCallback _sound;
};

} // namespace reluva::stgenerations
