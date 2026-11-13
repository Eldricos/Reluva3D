// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "video/smacker.h"
#include "engines/stharbinger/formats/dialogue.h"
#include "engines/stharbinger/formats/font.h"
#include "engines/stharbinger/formats/graphics.h"
#include "engines/stharbinger/formats/resources.h"
#include "engines/stharbinger/game/arcade.h"
#include "engines/stharbinger/platform.h"

#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace reluva::stharbinger {

class Harbinger;

// Runs the game: the scene scripts with their hotspots, timers, animations and
// sounds. The behaviour follows DEEP.EXE (docs/engines/stharbinger/systems/script-reference.md, "Run-time
// behaviour").
//
// Script model: when a scene is entered its script runs from the top until
// RETURN; the script then idles and the player has control. Clicking an enabled
// hotspot, a timer running out or an animation reaching its ANIMEND point runs
// the script from the handler's label, again until RETURN. GOSUB and IF...SUBR
// push a return point that RETURN pops first. ANIMSUSPEND, the fades and leaving
// a scene make the script wait while animations keep playing.
class Game {
public:
    explicit Game(Harbinger* vm);
    ~Game();

    // `startScene` indexes Harbinger::getScenes(); -1 starts the game normally.
    int run(int startScene);

    // For tests and debugging: drive the game without the real-time loop.
    void start(int scene);
    void runSteps(int steps); // 20 ms each
    void click(int x, int y);
    int32_t getVariable(const std::string& name) const;
    // Animation state, for tests.
    bool isAnimVisible(const std::string& name) const;
    int getAnimFrame(const std::string& name) const;
    const std::string& getSceneName() const;
    bool isScriptRunning() const { return thread_.active; }
    // The conversation, for tests: whether one runs, and the menu labels when
    // the player is asked to choose.
    bool isConversationActive() const { return conv_.active; }
    bool isArcadeActive() const { return arcade_ != nullptr; }
    bool isMenuActive() const { return menu_.active; }
    bool hasMenuFont() const { return !font_.empty(); }
    bool isCreditsActive() const { return credits_.active; }
    bool isDeckMapOpen() const { return deckMap_; }
    void pressKey(uint32_t key);
    // For tests: press a menu button, answer a confirmation, type a save name.
    void pressMenuButton(int button) { menuButton(button); }
    void answerMenu(bool yes) { menuConfirm(yes); }
    void typeSaveName(const std::string& name);
    int getSaveCount() const { return int(menu_.saves.size()); }
    const Surface& getScreen() const { return screen_; }
    void moveMouse(int x, int y);
    // For tests: a right click (torpedo) at the given point.
    void rightClick(int x, int y);
    std::vector<std::string> getMenuLabels() const;
    void pickMenuEntry(int entry);
    // Plays DS9/<path> (e.g. "INTRO/VNM.SMK") on its own, for the developer start mode "video".
    int playVideoFile(const std::string& path);
    // Starts a conversation by dialogue name in a scene that hosts it (after
    // that scene's entry script). For testing the dialogue scripts.
    bool debugTalk(const std::string& dialogue);

private:
    // ---- Scene state ----------------------------------------------------

    struct Hotspot {
        bool defined = false;
        bool enabled = false; // hotspots start disabled; HOTON enables them
        int x = 0, y = 0, w = 0, h = 0;
        int label = -1;
        int cursor = 0;
    };

    enum TimerKind {
        kTimerScene = 0,
        kTimerGlobal = 1 // also runs during conversations
    };

    struct Timer {
        bool defined = false;
        bool active = false;
        int kind = kTimerScene;
        uint16_t reload = 0;
        uint16_t count = 0; // fires once when it counts down to 0
        int label = -1;
    };

    // Animation flags (DEEP.EXE animation record +8). ANIM and ANIMSEQ keywords
    // set or clear one bit each: keyword k touches bit k/2, odd k sets it.
    enum AnimFlags {
        kAnimFlagCycle = 1 << 0, // ping-pong at the ends (else loop)
        kAnimFlagBackward = 1 << 1,
        kAnimFlagStop = 1 << 2,  // no frame advance
        kAnimFlagToEnd = 1 << 3, // stop on the last frame
        kAnimFlagNoMove = 1 << 4,
        kAnimFlagOff = 1 << 5,     // not drawn
        kAnimFlagOneFrame = 1 << 6 // draw one frame, then turn off
    };

    struct Anim {
        bool lazy = false;  // ANIMDEF onthefly/flyclear: loaded on first use
        bool clear = false; // background restored before each redraw
        bool loaded = false;
        bool defined = false; // ANIMDEF has run
        RleAnimation rle;

        uint8_t flags = kAnimFlagOff;
        int rate = 0; // a frame is shown for rate + 1 steps
        int countdown = 0;
        int frame = 0;              // -1: no current frame
        int drawnFrame = -1;        // the frame last drawn into the work buffer, -1: none
        int drawnX = 0, drawnY = 0; // and where it went
        bool suspended = false;     // the script waits for it (ANIMSUSPEND)
        int x = 0, y = 0;           // added to each frame's own position
        int priority = 0;           // 0..4, 4 on top
        int endLabel = -1;          // ANIMEND
    };

    // ---- Conversations (game_dialogue.cpp) -------------------------------

    static const int kMaxGroups = 50;

    // A reference to a dialogue item: group and position in the NPC or choice tree.
    struct ItemRef {
        bool npc = false;
        int group = -1, item = -1;
        bool valid() const { return group >= 0; }
        bool operator==(const ItemRef& o) const { return npc == o.npc && group == o.group && item == o.item; }
    };

    // What a dialogue remembers for the whole game (DEEP.EXE keeps the records
    // loaded and saves them with the game).
    struct DialogueState {
        Dialogue data;
        int cur = 0;              // current group
        bool replyMode = false;   // next turn: the menu (else an NPC line)
        uint8_t rudeGroup = 0xFF; // start group after an aborted conversation
        bool rude = false;
        uint8_t backGroup = 0xFF;       // start group after a finished one
        bool exit = false;              // ends the loop; afterwards "talked before"
        uint8_t done[kMaxGroups];       // DONE: group to go to when exhausted
        uint8_t emptyGoto[kMaxGroups];  // EMPTY: NPC group to go to when exhausted
        uint8_t emptyReply[kMaxGroups]; // EMPTY: menu to go to when exhausted
        struct Redirect {
            int target = -1; // -1: none
            ItemRef except;  // ignored when testing for "exhausted"
        };
        Redirect doneX[kMaxGroups], emptyXGoto[kMaxGroups], emptyXReply[kMaxGroups];
        std::vector<int> current; // per NPC group: the item it says next
        struct ItemFlags {
            bool on = true;
            bool said = false;
        };
        std::vector<std::vector<ItemFlags>> npcFlags, choiceFlags;
        struct Hook {
            int group;
            int state; // 0 HOOK, 1 THOOK, 2 THOOK used up
        };
        std::vector<Hook> hooks[kMaxGroups];

        DialogueState();
    };

    struct Talker {
        std::string code;          // speaker code from the clip names ("odo" ...)
        int mouth = -1, head = -1; // animation indexes
    };

    struct ConversationOp {
        enum Type {
            kRunList,   // run `list` from `pos`
            kSpeak,     // say `line`
            kMenu,      // let the player pick from `menu`
            kAfterPick, // after a choice: on to the next group
            kCloseMenu,
            kLoop // DONE checks, then the next turn
        } type;
        const DirectiveList* list = nullptr;
        size_t pos = 0;
        int line = -1;
    };

    struct Conversation {
        bool active = false;
        int dialogue = -1; // CCB index
        int hotspot = -1;  // the speaker; clicks on it don't abort
        int result = -1;   // hotspot to jump to afterwards, or -1
        std::deque<ConversationOp> ops;
        bool jumpTaken = false;        // GOTO/REPLY ran: skip the rest of the lists
        std::vector<ItemRef> lastPick; // per choice group, this conversation
        std::vector<int> protagonistLines;
        struct TempFlag {
            ItemRef item;
            bool saved;
        };
        std::vector<TempFlag> tempFlags; // TON/TOFF, undone at the next NPC line
        std::vector<Talker> talkers;

        // Waiting.
        int waitAnim = -1;
        bool waitFade = false;
        // Speech.
        bool speaking = false;
        uint32_t speechEnd = 0; // game time when the clip ends
        uint32_t pauseEnd = 0;  // then 250 ms of silence
        audio::SoundHandle speechHandle = audio::kNoSound;
        Phonemes phonemes;
        int mouth = -1;
        size_t phonemeNode = 0;
        uint32_t speechStart = 0, nodeStart = 0;
        // The menu.
        std::vector<ItemRef> menu;
        std::vector<int> menuWidths;
        int menuX = 0, menuY = 0, lineHeight = 0;
        int hover = -1;
        bool menuVisible = false;
        bool menuWaiting = false;
        int picked = -1;
    };

    // ---- Videos and cursors (game_video.cpp) ------------------------------

    struct VideoPlayback {
        bool active = false;
        std::unique_ptr<smacker::Decoder> decoder;
        int x = 0, y = 0;
        uint32_t startMillis = 0;
        int framesShown = 0;
        bool trans = false;       // TRANS videos stop the ambient sounds
        bool thenCredits = false; // the epilogue
        RleAnimation staticNoise; // shown before the video (arcade deaths)
        uint32_t staticUntil = 0;
        uint16_t result = 0; // for ACTIONSEQ: 0 played, 1 skipped
        std::string resultVariable;
        std::shared_ptr<audio::QueuingStream> audio; // fed while the mixer plays it
        audio::SoundHandle audioHandle = audio::kNoSound;
    };

    struct CursorImage {
        int w = 0, h = 0;
        int hotX = 0, hotY = 0;
        uint8_t transparent = 0;
        std::vector<uint8_t> pixels;
    };

    // ---- The game menu (game_menu.cpp) -----------------------------------

    struct GameMenu {
        bool active = false;
        bool first = false; // at start-up: no Save, no Resume
        RleAnimation buttons;
        Surface art;        // art\s\e\setup.cmp
        Palette artPalette; // palettes\setup.pal
        std::vector<std::string> saves;
        int selected = 0;
        bool infoValid = false;
        std::string description;
        std::vector<uint8_t> thumbnail; // 320x240
        uint8_t thumbnailMap[256] = {}; // its colours in the menu art's palette
        Palette savePalette;
        enum Mode { kMain, kConfirm, kNameEntry } mode = kMain;
        int confirmButton = -1;
        std::string confirmText;
        std::string name;
        bool typed = false;
        int hover = -1;
        Surface frame; // the menu picture, without the cursor
    };

    // ---- Credits and deck map (game_extras.cpp) ----------------------------

    struct CreditsScreen {
        bool active = false;
        bool fromMenu = false;
        enum Phase { kBlank, kFadeIn, kScroll, kFadeOut } phase = kFadeIn;
        uint32_t phaseStart = 0;
        int offset = 0; // pixels scrolled
        Surface blank, background;
        Palette blankPalette, palette;
    };

    // ---- Script thread --------------------------------------------------

    enum WaitType {
        kWaitNone,
        kWaitAnim,         // until animation `waitArg` is no longer suspended
        kWaitFade,         // until the fade is done
        kWaitRestart,      // ran off the end of the script: restart next step
        kWaitConversation, // until the conversation ends
        kWaitVideo,        // until the video ends
        kWaitArcade,       // until the action sequence ends
        kWaitCredits       // until the credits end
    };

    struct Thread {
        bool active = false; // false: idle at RETURN
        uint32_t pc = 0;     // directive index in the scene script
        std::vector<uint32_t> callStack;
        WaitType wait = kWaitNone;
        int32_t waitArg = 0;
        bool nested = false;           // a timer handler run from a conversation
        uint16_t returnValue = 0x8000; // its RETURNVAL
    };

    struct Fade {
        bool active = false;
        bool in = false; // fade in from black, else out to black
        int steps = 0;
        uint32_t startMillis = 0;
    };

    // game.cpp
    void enterScene(int index);
    void leaveScene();
    void switchScenes();
    bool soundsPlaying();
    void handleEvent(const InputEvent& event);
    void step();
    void tickTimers();
    void updateFade();
    void applyPalette();
    void drawScreen();
    int findHotspotAt(int x, int y) const;
    void updateCaption();

    // game_script.cpp
    void runThread();
    void jumpToLabel(int label);
    // Executes one directive. Returns false when the thread stops or waits.
    bool executeDirective(const Directive& d);
    bool isWaitOver();

    uint16_t getValue(const Argument* arg) const;
    uint16_t getVariable(const Argument* arg) const;
    void setVariable(const Argument* arg, uint16_t value);
    std::string variableName(const Argument* arg) const;
    bool jumpTo(const Argument* arg);
    bool gosub(const Argument* arg);
    void buildLabels();

    void setBackground(int index, bool loadPalette);
    void playSound(int index, const Argument* mode);
    void actionSequence(const Directive& d);
    // Beams drawn by ACTIONSEQ 0xC0..0xC2 (0x258BC): drawn on the screen at
    // once, then kept by the renderer for `beamLife_` renders.
    struct Beam {
        bool enemy = false;
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        uint8_t color = 0;
        int left = 0, top = 0, w = 0, h = 0; // the box between the end points
    };
    void drawBeam(Surface& dst, const Beam& b);
    void drawBeams(Surface& dst);
    void addBeam(Beam b);
    // Copies the rectangle (x, y, w, h) of the background into the work buffer, clipped.
    void restoreRect(int x, int y, int w, int h);
    void updateTick();
    int randomNumber(int n);
    uint16_t evalReturnValue(const Argument* a) const;
    std::string listName(ListKind kind, int index) const;
    std::string describe(const Directive& d) const;

    // game_dialogue.cpp
    void loadFont();
    void startConversation(int cdIndex, int hotspot);
    DialogueState* getDialogueState(int index);
    void updateConversation();
    void processConversationOps();
    bool runDialogueList(ConversationOp& op);
    bool executeDialogueDirective(const Directive& d);
    void dialogueTurn();
    void npcTurn(DialogueState& st);
    void menuTurn(DialogueState& st);
    void pickChoice(int entry);
    void abortConversation(bool rude, int hotspot);
    void finishConversation();
    void restoreTempFlags();
    bool isExhausted(const DialogueState& st, int group) const;
    bool isExhausted(const DialogueState& st, int group, const ItemRef& except) const;
    int applyRedirect(const DialogueState& st, int group, const uint8_t* table,
                      const DialogueState::Redirect* redirects) const;
    std::vector<ItemRef> availableChoices(DialogueState& st, int group, bool consumeHooks);
    DialogueState::ItemFlags* getItemFlags(DialogueState& st, const ItemRef& ref);
    ItemRef getItemRef(const Argument* arg) const;
    void setItemOn(DialogueState& st, const ItemRef& ref, bool on, bool temporary);
    void registerTalker(int slot, const std::string& code, int mouth, int head);
    void startSpeech(int line);
    void updateSpeech();
    int phonemeFrame(int anim, int code) const;
    void layoutMenu(DialogueState& st);
    void drawMenu(Surface& dst);
    int menuEntryAt(int x, int y) const;
    bool handleConversationClick(int x, int y);
    void tickConversationTimers();
    uint16_t runNestedHandler(int label);

    // game_video.cpp
    // Starts DS9/<dir>/<name>.SMK at (x, y). Returns false if it is missing.
    // `staticNoise` (if it has frames) is shown first, for kStaticMillis.
    bool startVideo(const std::string& dir, const std::string& name, int x, int y, bool keepScreen,
                    RleAnimation* staticNoise = nullptr);
    void updateVideo();
    void finishVideo(bool skipped);
    bool playActionSequenceVideo(int type, const Argument* resultVar);
    void startArcade(int type, const Argument* resultVar);
    void updateArcade();
    void finishArcade();
    void loadCursors();
    int currentCursor() const;
    Point cursorHitPoint(bool menu) const;
    void drawCursor(Surface& dst);
    void cyclePalette();

    // game_save.cpp
    struct SaveInfo {
        std::string description;
        std::vector<uint8_t> screenshot; // 640x480
        uint8_t palette[768] = {};       // 8 bits per channel
    };
    std::string getSaveDirectory() const;
    // Writes the game as DEEP.EXE does (savegame\NAME.ds9); `resumePc` is the
    // directive the script continues from.
    bool saveGame(const std::string& fileName, const std::string& description, uint32_t resumePc);
    bool loadGame(const std::string& fileName);
    static bool readSaveInfo(const std::string& path, SaveInfo& info);
    uint32_t getResumePc() const;
    void startNewGame();
    void loadSettings();
    void saveSettings();
    void autosave(int type);

    // game_menu.cpp
    void openMenu(bool first);
    void closeMenu();
    void scanSaves();
    void selectSave(int index);
    void drawMenuScreen();
    int menuButtonAt(int x, int y) const;
    void handleMenuEvent(const InputEvent& event);
    void menuButton(int button);
    void menuConfirm(bool yes);
    void finishNameEntry();
    void requestMenu();

    // game_extras.cpp: the credits and the deck map
    void startCredits(bool withBlank, bool fromMenu);
    void updateCredits();
    void drawCredits();
    void finishCredits();
    bool openDeckMap();

    // game_anim.cpp
    void setupAnims();
    bool executeAnimCommand(const Directive& d);
    Anim* useAnim(const Argument* arg);
    void loadAnim(int index, bool lazy);
    void unloadAnim(int index);
    void applyAnimKeyword(Anim& a, uint16_t keyword);
    void restartAnim(Anim& a);
    void advanceAnim(Anim& a);
    // One pass of the renderer over the work buffer: erase, draw and, if
    // `advance`, step the animations.
    void renderAnims(bool advance);

    Harbinger* vm_;
    Platform* system_;
    Resources& resources_;
    const std::vector<Scene>& scenes_;
    bool quit_ = false;

    // Variables are global: one 16-bit value per name, shared by all scenes
    // (DEEP.EXE maps each scene's variable list into one array by name).
    std::map<std::string, uint16_t> variables_;

    int sceneIndex_ = -1;
    int pendingScene_ = -1;
    const Scene* scene_ = nullptr;
    std::map<uint32_t, uint32_t> labels_; // label number -> directive index

    std::vector<Hotspot> hotspots_; // by CH index
    std::vector<Timer> timers_;     // by CT index
    std::vector<Anim> anims_;       // by CA index

    Thread thread_;
    GameMenu menu_;
    CreditsScreen credits_;
    std::vector<std::string> creditLines_;
    Font creditsFont_;
    bool creditsLoaded_ = false;
    bool deckMap_ = false;
    Surface deckMapFrame_;
    Surface menuGameScreen_; // the game's picture when the menu opened
    Palette menuGamePalette_;
    Surface cleanScreen_;         // the last frame without the cursor
    Surface workBuffer_;          // the last drawn scene: background and animations
    bool workBufferStale_ = true; // to be refilled from the background
    VideoPlayback video_;
    std::unique_ptr<Arcade> arcade_;
    std::string arcadeResultVariable_;
    uint32_t arcadeStartMillis_ = 0;
    bool arcadeLeft_ = false, arcadeRight_ = false;
    int arcadeDifficulty_ = 1; // 0..2; the game menu sets it (default 1)
    int lastArcadeDamage_ = 0; // carried into some later sequences
    std::map<int, ArcadeGroupState> arcadeGroups_;
    std::vector<CursorImage> cursors_;
    bool cursorsLoaded_ = false;
    uint32_t nextCycleMillis_ = 0;
    uint8_t videoPalette_[768] = {};
    std::vector<int> ambientSounds_;
    std::vector<Beam> beams_;
    int beamLife_ = 0;
    int setRectHotspot_ = 0; // the hotspot SETRECT set last
    // The countdown's tick (ACTIONSEQ 0xF0..0xF5): tick.raw every `period` ms.
    uint32_t tickPeriod_ = 0xFFFF; // 0xFFFF: off
    uint32_t tickPhase_ = 0;
    std::vector<uint8_t> tickSound_;
    // State that saved games keep.
    int disc_ = 0;                // CDCHANGE
    int mapLocation_ = 0xFF;      // for the deck map (ACTIONSEQ 0xA0xx)
    int paletteBackground_ = 299; // the background whose palette is set (299: none)
    uint32_t idlePc_ = 0;         // the RETURN the script idles on
    uint32_t actionSeqPc_ = 0;    // the ACTIONSEQ being run (for autosaves)
    bool autosave_ = true;
    int lastSave_ = 0; // the save the menu showed last         // sound indexes, to restart after TRANS videos
    Conversation conv_;
    std::map<int, DialogueState> dialogues_; // by CCB index, for the whole game
    Font font_;
    bool fontLoaded_ = false;
    int speedStep_ = 1; // animation countdown step (3 while lips move)
    uint32_t stepCount_ = 0;
    uint32_t nextStepTime_ = 0;
    uint32_t millis_ = 0; // game time, advanced by step()

    int backgroundIndex_ = -1;
    Surface background_;
    Surface screen_;
    Palette palette_;
    Fade fade_;
    int fadeLevel_ = 256;       // 0 black .. 256 full palette
    bool screenFrozen_ = false; // ANIMSOFF until the next BACKGRND
    bool mapEnabled_ = true;
    bool tricorderIcon_ = false;
    bool dead_ = false;
    RleAnimation tricorderRle_;

    std::vector<audio::SoundHandle> ambients_; // looping, at most 2
    std::vector<audio::SoundHandle> oneShots_;

    Point mouse_;
    int hoverHotspot_ = -1;
    bool showHotspots_ = false;
    std::string status_; // shown in the caption
};

} // namespace reluva::stharbinger
