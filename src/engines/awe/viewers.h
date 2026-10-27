// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/audio/mixer.h"
#include "common/events.h"
#include "common/gfx3d.h"
#include "common/image.h"
#include "common/system.h"
#include "common/types.h"
#include "common/video/bink_file.h"
#include "common/video/bink_video.h"
#include "engines/awe/ac2.h"
#include "engines/awe/actor.h"
#include "engines/awe/conditions.h"
#include "engines/awe/dialogue.h"
#include "engines/awe/particles.h"
#include "engines/awe/script.h"
#include "engines/awe/story.h"
#include "engines/awe/walk_grid.h"
#include "engines/awe/game_text.h"
#include "engines/awe/gr2.h"
#include "engines/awe/model.h"
#include "engines/awe/model_draw.h"
#include "engines/awe/movie_player.h"
#include "engines/awe/savegame.h"
#include "engines/awe/shadows.h"
#include "engines/awe/camera.h"

#include <array>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

// Data viewers used by the AWE engine until the game itself runs. Each is
// a page of the engine's 1024x768 window (the game's own size); Tab moves
// between them, and each names its keys at the bottom of the screen.

namespace reluva::awe {

constexpr int kScreenW = 1024;
constexpr int kScreenH = 768;

struct ViewerContext {
    System& sys;
    const Archive& files;
    audio::Mixer* mixer;  // null without an audio device
    Surface& screen;      // kScreenW x kScreenH, RGBA32
    std::filesystem::path savePath; // where screenshots go (F12)
    const GameFonts* fonts = nullptr;  // the game's fonts, if they loaded
};

class Page {
public:
    explicit Page(ViewerContext& ctx) : ctx_(ctx) {}
    virtual ~Page() = default;
    virtual const char* name() const = 0;
    virtual size_t count() const = 0;
    // Called when the page is shown, and before it's left.
    virtual void activate() = 0;
    virtual void deactivate() {}
    virtual void handleEvent(const Event& ev) = 0;
    // Each frame: 2D pages show the framebuffer again.
    virtual void update(uint64_t nowMs) {
        (void)nowMs;
        present();
        finishScreenshot(nullptr, &ctx_.screen);
    }
    // Whether Escape is the page's now (a movie skipped), not the way out.
    virtual bool wantsEscape() const { return false; }
    // Whether the page asks to go back to the launcher (the game's quit).
    virtual bool quitRequested() const { return false; }
    // Saves the next frame shown, as a TGA file (the window title says
    // where).
    void requestScreenshot(std::filesystem::path file) { shotFile_ = std::move(file); }

protected:
    ViewerContext& ctx_;
    std::filesystem::path shotFile_;

    // For pages that draw in 3D: before endFrame, asks the renderer to
    // keep the frame if a screenshot is due.
    void prepareScreenshot(gfx::Renderer3D* renderer) {
        if (renderer && !shotFile_.empty())
            renderer->captureNextFrame();
    }
    // After the frame: saves it (the 3D frame with `overlay` over it, or
    // `overlay` alone without a renderer) if a screenshot is due.
    void finishScreenshot(gfx::Renderer3D* renderer, const Surface* overlay);

    // 2D pages: the framebuffer, cleared, and the help line at the bottom.
    void begin2D();
    void clear(uint8_t r, uint8_t g, uint8_t b);
    void fill(int x, int y, int w, int h, Color c, uint8_t alpha = 255);
    void frame(int x, int y, int w, int h, Color c);
    // A movie's subtitles due at `seconds`, at the bottom of the screen.
    void drawSubtitles(const std::vector<Subtitle>& subtitles, double seconds);
    int text(int x, int y, const std::string& s, Color c = {230, 230, 230}, int scale = 1);
    // Text on a dark box, for reading over pictures.
    void label(int x, int y, const std::string& s, Color c = {230, 230, 230});
    void help(const std::string& keys);
    void present() { ctx_.sys.updateScreen(ctx_.screen); }
    // A list step from a key: -1/+1 for Left/Right, -10/+10 for
    // PageUp/PageDown, to the ends for Home/End; 0 for other keys.
    static int step(uint32_t key, int index, int count);

    // A picture from the game's files (cached; an empty image if it can't
    // be read).
    const image::Image& picture(const std::string& name);
    std::map<std::string, image::Image> pictures_;
};

// Every picture in the game: backgrounds, cutouts, interface, inventory.
class PicturePage : public Page {
public:
    explicit PicturePage(ViewerContext& ctx);
    const char* name() const override { return "Pictures"; }
    size_t count() const override { return names_.size(); }
    void activate() override;
    void handleEvent(const Event& ev) override;

private:
    void show();
    std::vector<std::string> names_;
    int index_ = 0;
};

// Scenes (Locations/**/*.ac2): their pictures where the scene places
// them, hotspots, and what else the scene holds.
// The game's settings, as its options screen sets them (0x490048..):
// volumes and brightness 0..10, the rest on (1) or off.
struct GameOptions {
    enum { Music, Sound, Gamma, Captions, Shadows, Water, Fog, Rain, AntiAlias, kCount };
    int values[kCount];
    GameOptions();
    int get(int i) const { return values[i]; }
};

class ScenePage : public Page, public ScriptHost, public DialogueHost {
public:
    explicit ScenePage(ViewerContext& ctx);
    ~ScenePage() override;
    const char* name() const override { return "Scenes"; }
    size_t count() const override { return names_.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;
    void update(uint64_t nowMs) override;
    bool wantsEscape() const override { return movie_ != nullptr || playing_; }
    bool quitRequested() const override { return quit_; }

    // For tools (reluva-tool awe-acts; scene_autoplay.cpp): a game started
    // without the menus at an act ("04a": its PreLoad, location and Post
    // Load) or from a developer's save ("slot7"); every line of the script
    // log passed on; and a player who only waits: movies skipped, the
    // first choice taken, lines cut short (what it did, "" if nothing).
    void startAt(const std::string& where);
    // A script run as the scenes' are (a tool's: "startpass michel").
    void runCommands(const std::string& script) { runner_->run(script); }
    std::function<void(const std::string& line)> onLog;
    std::string autoplay();
    // Nothing drawn (a tool that only follows the scripts).
    void setDrawing(bool on) { drawing_ = on; }
    struct GameStatus {
        std::string scene, act, event;
        bool idle = true, talking = false, movie = false, loading = false, puzzle = false, fading = false;
    };
    GameStatus status() const;
    // The hotspots and characters that could be clicked now, and a click
    // on one (its script run, as a click runs it).
    std::vector<std::string> clickables() const;
    bool click(size_t i);
    // Where a player's mouse would click the clickable i: a point where
    // the game's pick finds it (0x435eb0) and clicks are taken, its
    // rectangle tried first; false if there's none on the screen.
    bool pointFor(size_t i, int& x, int& y) const;
    // An item taken up into the hand, as the inventory's click takes it.
    void takeUp(const std::string& item) { holdItem(item); }
    bool clickRuns(size_t i) const; // whether it has a script (Antoinette may not)
    // The inventory (scene_inventory.cpp): an item used on an object, as
    // the game does when one in hand is clicked on it (0x41c050: its
    // conditional scripts asking for it, else uselist.txt for a
    // character, else "can't"); items combined (combinelist.txt). The
    // items the clickable i asks for, that Poirot has, and one used on it.
    bool useItem(const std::string& item, const Ac2Record& target);
    bool combineItems(const std::vector<std::string>& parts);
    std::vector<std::string> itemsFor(size_t i);
    // Of those, the ones the cursor shows would be taken (0x419e90).
    std::vector<std::string> usableItems(size_t i);
    bool useOn(const std::string& item, size_t i);
    // The parts of each combinelist.txt row that could be combined now.
    std::vector<std::vector<std::string>> combinations();
    // The items a combinelist.txt row lets be taken apart now, and one
    // taken apart (the combine page's right-hand box, then the gears).
    std::vector<std::string> decombinable();
    bool decombineItem(const std::string& item);
    // For a player that searches (reluva-tool awe-acts -solve): the story
    // state; the game saved to memory and loaded from it as the save and
    // load screens do; who picks the dialogue's choices (the index, or -1
    // for the tool's own: the one taken least); the items whose back has
    // an Examine Event and that back looked at; the bar's cars that can
    // be travelled to now and the travel; whether clicks are taken now
    // (nothing runs, or the runner is unlocked).
    const GameState& gameState() const { return state_; }
    // Every clause of a condition the game tests is told (GameState::onTest);
    // and whether a condition holds now, untold.
    void watchConditions(std::function<void(std::string_view, bool)> f) { state_.onTest = std::move(f); }
    bool holds(const std::string& condition) {
        auto watch = std::move(state_.onTest);
        state_.onTest = nullptr;
        const bool h = testCondition(condition, state_);
        state_.onTest = std::move(watch);
        return h;
    }
    std::vector<uint8_t> saveToMemory() const;
    bool loadFromMemory(const std::vector<uint8_t>& data);
    void setLastScene(const std::string& scene) { state_.lastScene = scene; }
    std::function<int(const std::vector<std::string>& choices)> chooser;
    std::vector<std::string> flippableItems();
    bool flipItem(const std::string& item);
    std::vector<int> travelCars();
    bool travel(int car);
    // The fingerprint cards the magnifier would match (a person's print
    // and a thing's whose Flipped Description names it), not compared yet;
    // a pair compared, as the cards page does with them shown.
    std::vector<std::pair<std::string, std::string>> matchingPrints();
    bool comparePrintCards(const std::string& person, const std::string& thing);
    bool clickable() const { return !runner_->locked() && !dialogue_->active() && !movie_; }
    // The puzzle screen open ("tornletter", "safe", "puzzlebox"; "" none),
    // whether it's solved, and the next of a player's inputs that solve it,
    // worked out from the puzzle's own checks and made as the mouse makes
    // them (false if there's none to make now).
    std::string puzzleOpen() const;
    bool puzzleSolved() const;
    bool puzzleStep();

private:
    // A character placed in the scene, animated as the game animates
    // them (Actor).
    struct Character {
        std::string name;
        std::shared_ptr<GpuModel> model;
        std::unique_ptr<Actor> actor;
        gfx::Mat4 placement{};
        // Where it stands and faces; while walking, the points still to
        // reach (world x, z).
        float x = 0, y = 0, z = 0, rotY = 0;
        float home = 0; // its heading when placed, in degrees ("rotate home")
        bool player = false;
        std::string who;                  // "antoinette": the model's folder
        const Ac2Record* record = nullptr; // its object in the scene
        std::vector<std::pair<float, float>> path;
        float walkSpeed = 0; // world units a second, from the walk itself
        bool follower = false; // follows the player (FLLW, its COND holding)
        bool shadow = true;    // casts a shadow (MODL shadow, hideshadow/showshadow)
        float walkIn = 0;      // walking in: the distance left
        // A script waiting on it (one at a time, as the game's +0xb5c):
        // for "walk", "run", "rotate" or "anim" (that animation, by the
        // name asked and the name played).
        ScriptRunner* waiting = nullptr;
        std::string waitVerb, waitAnim, waitAnimAs;
        // Its own routine (the IDLE list), and whether "pause" stopped it.
        std::unique_ptr<ScriptRunner> routine;
        bool paused = false;
        // Props attached ("attach clipboard"), posed by its animations.
        std::vector<std::pair<std::string, std::shared_ptr<GpuModel>>> props;
        // Outdoors (scene_outdoors.cpp): the footprints it has left, the
        // time into its stride (+0x14f4) and the foot next down (+0x14f8),
        // and its breath in the cold.
        struct Footprint {
            std::shared_ptr<GpuModel> model;
            float x = 0, z = 0, rotY = 0, age = 0;
            size_t serial = 0; // its place after the scene's objects
        };
        std::vector<Footprint> footprints;
        float stride = 0;
        int foot = 0;
        std::unique_ptr<ParticleSystem> breath;
    };
    // What's drawn in 3D among the pictures, by depth: a character, or one
    // of its footprints.
    struct ModelSlot {
        size_t character = 0;
        int footprint = -1;
    };
    // A video loop the scene shows (the countryside going by a window),
    // by the object that places it.
    struct Loop {
        std::unique_ptr<video::BinkFile> file;
        std::unique_ptr<video::BinkVideoDecoder> video;
        image::Image frame;
        size_t next = 0; // the frame to decode next
        float start = 0; // when it started (shown), in the page's seconds
    };
    void load();
    void loadLoops();
    // Draws a particle system's particles through the scene's camera.
    void drawParticles(const ParticleSystem& system, bool keepAlpha);
    // Advances the loops to `time` seconds; whether any frame changed.
    bool advanceLoops(float time);
    void loadCharacters();
    void draw();
    void releaseGpu();
    void line(int x0, int y0, int x1, int y1, Color c);
    // A convex polygon (screen points), blended over the screen.
    void fillPolygon(const std::vector<std::pair<float, float>>& points, Color c, uint8_t alpha);
    void drawGrid();
    // Walks the player's character to where the screen point is on the
    // floor, along a path through the grid.
    void walkTo(int sx, int sy);
    // Scripts: a click on a hotspot runs its script (or the first of its
    // conditional ones whose condition holds); the page does what they ask.
    bool start(ScriptRunner& runner, const ScriptEvent& e, bool wait) override;
    // The dialogue's host: voices, event files, the scene's name.
    bool playVoice(const std::string& file) override;
    bool voicePlaying() override;
    void stopVoice() override;
    std::string readText(const std::string& file) override;
    std::string sceneName() override;
    void turnHead(const std::string& speaker, const std::string& target) override;
    void loadDialogue();
    bool startTalk(ScriptRunner& runner, const ScriptEvent& e);
    const Ac2Record* characterAt(int sx, int sy) const;
    std::string scriptText(const std::string& name) override;
    const Ac2Record* hotspotAt(int sx, int sy) const;
    void runScript(const Ac2Record& object);
    Character* scriptCharacter(const ScriptRunner& runner);
    // Walks a character to a cell along a path; false if there's none.
    bool walkCharacter(Character& c, WalkGrid::Cell goal, bool toPoint, bool run = false);
    // "scene <name> [wp] [flag]": the act's variant of it, the player at
    // walk point wp (-1: where they were when it was last left).
    bool changeScene(const std::string& name, int entry, bool walkIn = false);
    // A follower goes after the player (0x4400a0).
    void follow(Character& f);
    // A scene shown in a game (N): its visit noted, its music and its
    // first event whose condition holds (0x4362b0).
    void enterScene();
    void playMusic();
    void newGame();

    // Saved games (scene_saves.cpp; docs/formats/awe/saves.md). The
    // game's own format: Saved.Games/slot<N>.ac2 and .bmp, quick.ac2,
    // kept in Reluva's save folder for the game.
    std::filesystem::path savesFolder() const;
    SaveGame captureSave(const std::string& description) const;
    // "preload": the story state from a save; "load" then goes there.
    bool preload(const std::filesystem::path& file, bool fromGame);
    bool preloadData(const std::vector<uint8_t>& data, const std::string& what);
    void finishRestore();
    // "menu <name>", "previous [scene]": the menus and back.
    void openMenu(const std::string& name);
    void previous(bool toScene);
    // A game key: the in-game, save or load menu (the view kept for the
    // thumbnail), F2 quick save, F3 quick load. False if it isn't one.
    bool gameKey(uint32_t key);
    bool quickSave();
    // The screen's message (0x4168c0): "Save Game" on the placard for 1.5
    // s after F2, put away early by a click or a key; and the caret's
    // blink, on and off every 0.33 s (0x414400).
    std::string message_;
    float messageTime_ = 0;
    float caretTime_ = 0.33f;
    bool caretOn_ = false;
    void drawMessage();
    // The save and load screens (a scene's PUZZ save/load: 0x4347a0,
    // 0x424260).
    struct SlotMenu {
        bool save = false;
        int count = 0;          // slots there are
        int page = 0, row = 0;  // first slot shown (a multiple of 3), the row picked
        std::string name;       // save: the name typed
        struct Row {
            bool exists = false;
            std::string description, date, time;
            image::Image thumbnail;
        } rows[3];
        int lastRow = -1;
        float sinceClick = 10;
    };
    std::unique_ptr<SlotMenu> slots_;
    // The options (scene_options.cpp): kept in Reluva's folder for the
    // game; the screen's staged values (music, sound, brightness,
    // captions) and the toggles as they were (Cancel), the slider dragged.
    GameOptions options_;
    struct OptionsMenu {
        int staged[4] = {0, 0, 0, 0};
        int original[5] = {0, 0, 0, 0, 0};
        int dragging = 0; // 1..3
    };
    std::unique_ptr<OptionsMenu> optionsMenu_;
    // The puzzles (scene_puzzles.cpp; docs/formats/awe/puzzles.md): the
    // torn letter, the baggage car's safe, the puzzle box. Pictures they
    // move and turn are drawn by overrides.
    struct PictureOverride {
        float x = 0, y = 0, depth = 0, roll = 0, cx = 0, cy = 0;
        bool tint = false; // picked: blue x 0.75
    };
    std::map<const Ac2Record*, PictureOverride> overrides_;
    struct TornLetter {
        std::vector<const Ac2Record*> pieces;
        int selected = -1;
        bool dragging = false, solved = false;
        float topz = 0;
        int dragX = 0, dragY = 0;
    };
    struct Safe {
        const Ac2Record* dial = nullptr;
        int spinning = 0; // 1 right, 2 left
        float timer = 0, interval = 0.25f;
        int number = 0, entered = 0, expected = 2;
        bool opening = false; // a tool's step has pulled the handle
    };
    struct PuzzleBox {
        int step = 0, selected = 0, misses = 0;
        int state[5] = {0, 0, 0, 0, 0}; // by piece: 1 upper end, 2 lower end, 3 side, 4 lid
        bool solved = false;
    };
    std::unique_ptr<TornLetter> torn_;
    std::unique_ptr<Safe> safe_;
    std::unique_ptr<PuzzleBox> box_;
    unsigned puzzleSeed_ = 1; // the game's rand()
    bool puzzleActive() const { return torn_ || safe_ || box_; }
    void openPuzzle(const std::string& kind);
    void closePuzzle();
    void puzzleClick(int x, int y);
    void puzzleRelease(int x, int y);
    void puzzleUpdate(float seconds);
    void puzzleAction(const std::string& word);
    bool puzzleCondition(const std::string& word) const;
    bool puzzleDraws(const Ac2Record& o) const;
    void drawPuzzleBox();
    // Torn letter.
    void saveTornPiece(int i);
    void snapTornPiece(int i);
    bool checkTornLetter();
    // Safe.
    void turnDial(int direction);
    void writeSafeTags();
    // Puzzle box.
    const Ac2Record* puzzleObject(const std::string& file, int n = 0) const;
    std::string boxEndPicture() const;
    std::string boxSidePicture() const;
    bool hitPicture(const Ac2Record* o, float x, float y, int px, int py) const;
    int boxArrowAt(int x, int y) const;
    bool boxMovePossible(int piece, int arrow) const;
    bool boxMoveCorrect(int piece, int arrow) const;
    int boxPieceAt(int x, int y) const; // 1 the end's upper half, 2 its lower, 3 the side, 4 the lid; 0 none
    const Ac2Record* tornPick(int x, int y) const; // the letter's front-most picture there
    void writeBoxTags();
    int lastMouseX_ = 0, lastMouseY_ = 0;
    std::filesystem::path optionsFile() const;
    void loadOptions();
    void saveOptions() const;
    void applyVolumes(int music, int sound);
    void applyBrightness();
    // Anti-Aliasing: the 3D drawn with 4x multisampling, else 2x, if on.
    void applyAntiAliasing();
    int brightnessLevel() const;
    bool fogged(const Ac2Record& o) const;
    bool frozen(const Ac2Record& o) const;
    void openOptions();
    int thumbX(int slider) const;
    void optionsAction(const std::string& word);
    void optionsRelease();
    void drawOptions();
    void openSlots(bool save);
    void refreshSlots();
    void slotAction(const std::string& word, const std::string& second);
    bool slotCondition(const std::string& word) const;
    bool slotClick(int x, int y);
    void slotKey(uint32_t key);
    void drawSlots();
    // The yes/no box (0x432930): its question and its kind (0 quit the
    // program, 1 quit the game, 2 overwrite a save); the choice's rows.
    struct YesNo {
        std::string text;
        int kind = 0;
        std::vector<std::pair<int, int>> rows;
    };
    std::unique_ptr<YesNo> yesNo_;
    void answer(bool yes);
    std::string interfaceText(const std::string& tag);
    // "movie <name> [inscene] [silent]" (0x40a725); false if there's no
    // such movie. endMovie: it's over (or skipped).
    bool startMovie(ScriptRunner& runner, const ScriptEvent& e, bool wait);
    void endMovie();
    // "musica <name>": sound\music\<name>.mp3, looping.
    void playLooping(const std::string& file, bool music);
    void playSound(const std::string& name);
    // A sound effect ("sound"), looping or not; kNoSound if there's none.
    audio::SoundHandle startSound(const std::string& name, bool loop);
    std::vector<std::pair<std::string, audio::SoundHandle>> sounds_; // by name, for "stop"
    ScriptRunner* soundRunner_ = nullptr; // waiting on a "sound"
    audio::SoundHandle soundWaited_ = audio::kNoSound;
    bool interfaceOff_ = false;
    // The letterbox (0x416ef0): black bars along the top and the bottom,
    // `height` pixels each; mode 0 off, 1 on, 2 sliding in, 3 sliding out
    // (the runner waiting for those).
    struct Letterbox {
        int mode = 0;
        float height = 0;
        ScriptRunner* runner = nullptr;
    } letterbox_;
    bool drawing_ = true;
    // The interface (scene_interface.cpp; docs/formats/awe/interface.md):
    // the bar along the top, the inventory window and its pages, the item
    // in hand, the passport or fingerprint card held up.
    struct Interface {
        // The bar: its alpha (0..255) and whether it's fully shown.
        float barAlpha = 0;
        bool barShown = false;
        // The window: open; its page (0 items, 1 examine, 2 combine, 3 the
        // scrapbook, 4 passports, 5 fingerprint cards) and the page it's
        // fading from; the fade (0 none, 1 in, 2 out, 3 page to page) and
        // its alpha; the button under the mouse (-1 none).
        bool open = false;
        int page = 0, fromPage = 0, fade = 0, hover = -1;
        float alpha = 0;
        bool fromInventory = false; // a script it started runs (its talks leave it open)
        std::string freshItem;      // the item just taken, which blinks for 5 s of the window open
        float freshTime = 0;
        int dragging = 0;           // the blip dragged: 1 the top line's, 2 the second's
        std::string hovered;        // the item under the mouse
        // Examine: the item, its back shown.
        std::string examined;
        bool back = false;
        // Combine: the four boxes and the result's, the message.
        std::string boxes[5];
        std::string message;
        // Passports: the page, how many, the one shown (by its place).
        int passPage = 0, passPages = 1, passAt = 0;
        // Fingerprint cards: the card shown in each row.
        int printAt[2] = {0, 0};
        // The scrapbook: the section (0 the contents), the level (0 the
        // contents, 1 a section's list, 2 an entry), each level's pages and
        // the page shown, the entry, the picture shown full screen.
        int section = 0, level = 0, pages[3] = {1, 1, 1}, at[3] = {0, 0, 0};
        std::string entry, zoom;
        // A card held up: the step (0 none, 1 the screen darkening, 2 the
        // card fading in, 3 shown, 4 fading out, 5 the screen brightening),
        // passport (1) or fingerprint card (2), whose; the darkness (0..128)
        // and the pictures' alphas, their clocks; the runner waiting.
        int card = 0, cardKind = 0;
        std::string cardWho;
        int cardRaise = 0; // 160 with the dialogue box up: the card 80 higher
        float dim = 0, cardAlpha = 0, printAlpha = 0, dimClock = 0, cardClock = 0, printClock = 0;
        int cardFade = 0, printFade = 0; // 1 out, 2 in
        ScriptRunner* cardRunner = nullptr;
        std::string cardVerb;
    } ui_;
    bool interfaceOn() const;
    bool canAct() const;
    bool interfaceEvent(const Event& ev);
    bool interfaceUpdate(float dt);
    void drawInterface();
    int barSection(int x, int y);
    bool carOn(int car);
    bool inCar(int car) const;
    std::string carScene(int car) const; // "salon_01 1": where the car's section goes
    void drawCards();
    void toggleInventory();
    void closeInventory();
    void showPage(int page);
    // The screen's text (0x414720): font and colour by their place in
    // fonts.txt, broken at spaces to the rectangle's width, placed by
    // flags (1 centred, 2 right, 4 centred down, 8 at the foot, 0x1000
    // struck through); returns its height.
    int uiText(int font, int colour, int l, int t, int r, int b, int flags, const std::string& text, int alpha);
    int textHeight(int font, int width, const std::string& text, int* lines = nullptr);
    // Items: their itemlist.txt row's columns, pictures and places.
    struct ItemInfo {
        std::string description, event, examine, flipped;
    };
    ItemInfo itemInfo(const std::string& item) const;
    std::string itemPicture(const std::string& item, const char* kind) const;
    int printRow(const std::string& item) const;
    std::vector<std::string> bagItems(); // in the bag, just taken or in hand, in slot order
    std::string itemInSlot(int slot);
    int itemPages();
    void holdItem(const std::string& item);
    void releaseHeld();
    void endFresh();
    void playItemSound(const std::string& item);
    void runFromInventory(const std::string& script, bool fromInventory);
    // The pages' clicks and drawing.
    bool pageClick(int x, int y);
    int pageButton(int x, int y, int* bx = nullptr, int* by = nullptr);
    void drawPage(int page, int alpha);
    void drawBlip(int row, int count, int at, int alpha);
    bool blipClick(int x, int y, int row, int count, int& at);
    bool combine();
    void comparePrints();
    // The scrapbook (its texts read on each change).
    struct ScrapRow {
        std::string heading, condition, tick, entry, entry2;
    };
    std::vector<ScrapRow> scrapRows();
    int drawScrapbook(int alpha, bool drawIt, int clickX = -1, int clickY = -1, std::string* clicked = nullptr);
    void scrapbookBack();
    // A card held up ("startpass", "startprint", their "end"s): false if
    // the event is over at once.
    bool startCard(ScriptRunner& runner, const ScriptEvent& e);
    // A conversation starting (0x43a649): the window closed unless it
    // started it, the item in hand put back unless it's a gen_ or com_.
    void interfaceTalk(const std::string& name);
    void logScript(const std::string& line);
    bool hidden(const Ac2Record& o) const;
    bool shown(const Ac2Record& o) const;
    void showObject(const std::string& name, bool show);
    static std::vector<std::string> objectKeys(const Ac2Record& o);
    // Starts the character's walk (or run) along its path: false if it
    // has no such animation.
    bool startWalk(Character& c, bool run = false);
    void placeCharacter(Character& c) const;
    // Shadows (docs/formats/awe/shadows.md): a model's meshes into the
    // stencil, each its shadow (if it casts one) then its body (`ref`); a
    // mask drawn as a coloured quad; the player's white-out in the snow.
    void stencilModel(const gfx::Mat4& viewProjection, const GpuModel& model, const Pose& pose,
                      const gfx::Mat4& placement, bool shadow, uint8_t ref, ShadowStencil& stencil) const;
    void drawMask(const CoverageMask& mask, Color colour, uint8_t alpha);
    void drawWhiteOut();
    // Whether the characters are drawn now.
    bool charactersShown() const {
        return renderer_ && showCharacters_ && scene_.camera() && !movie_ && sceneShown_ && !load_.screen;
    }
    // The character verbs (turn, look, rotate, anim...): true if the
    // script waits.
    bool startCharacterVerb(ScriptRunner& runner, Character& c, const ScriptEvent& e, bool wait, bool& handled);
    // Where a scene object or character is (x, z); false if there's none.
    bool objectAt(const std::string& name, float& x, float& z) const;
    Character* characterNamed(const std::string& who);
    // A prop's model (models\objects\...), loaded once; null if none.
    std::shared_ptr<GpuModel> propModel(const std::string& file);
    // A character's routine: its first IDLE entry whose condition holds.
    void queueRoutine(Character& c);
    void lineStarted(const std::string& speaker) override;
    void lineEnded(const std::string& speaker) override;
    void mouth(const std::string& speaker, bool on) override;
    int voiceLevel() override;

    std::vector<std::string> names_;
    int index_ = 0;
    Ac2Scene scene_;
    std::string base_; // the base scene an act's variant is shown over
    std::string error_;
    int selected_ = -1; // object highlighted, -1 none
    bool hotspots_ = true, info_ = true, showCharacters_ = true;
    // What the scene's conditions test: the train moving or not (M), the
    // weather (W), the act of the scene's variant.
    GameState state_;
    std::map<const Ac2Record*, Loop> loops_;
    std::map<const Ac2Record*, ParticleSystem> particles_;
    // Scrolling pictures (SPRT scroll): where each is now, and the part
    // of a pixel it has gone beyond that.
    struct Scroll {
        float x = 0, y = 0, part = 0;
    };
    std::map<const Ac2Record*, Scroll> scrolls_;
    bool scrollPictures(float dt); // whether any moved
    // The camera's walk grid (G shows it), the cell under the mouse and
    // the last path found.
    WalkGrid grid_;
    bool showGrid_ = false;
    WalkGrid::Cell hover_;
    std::vector<WalkGrid::Cell> path_;
    // The scripts' state: tags, items and the act are state_'s.
    std::unique_ptr<ScriptRunner> runner_;
    std::unique_ptr<DialogueManager> dialogue_;
    std::map<std::string, int> autoChoices_; // how often the tool's player took each choice
    std::vector<const Ac2Record*> clickableObjects() const;
    static bool scripted(const Ac2Record& o); // a script or conditional ones
    mutable std::map<std::string, Table> itemTables_; // uselist.txt, combinelist.txt, itemlist.txt
    const Table& itemTable(const char* file) const;
    // A game played (N): the acts, the music playing (by file), where the
    // player was in each scene left, the screen's fade (0 clear, 1 black)
    // and the runner waiting on it.
    std::unique_ptr<Story> story_;
    bool playing_ = false;
    std::map<std::string, audio::SoundHandle> music_;
    std::map<std::string, std::array<float, 3>> positions_; // the player's x, z and heading, by scene file left
    float fade_ = 0, fadeTarget_ = 0;
    // "movie": the one playing over everything, its subtitles, the runner
    // waiting on it; and whether a scene is shown (one not "inscene" is
    // left for the movie: black until the next scene).
    // The game's load state machine (0x422a90) for a location: an act's,
    // or a scene's in another place (after a fade out). Its scene files
    // are read one an update behind the loading screen; then the act's
    // Post Load runs, or the scene is shown and fades in.
    struct LocationLoad {
        int state = 0; // 0 none, 1 fading out, 2 loading, 3 fading the loading screen out, 4 fading the scene in
        bool forAct = false;
        std::string location;
        std::vector<std::string> files;
        size_t next = 0;
        bool screen = false, fadedIn = false; // the loading screen shown; faded in (+0x10f0)
        int wheel = 0;
        std::string scene, verb; // a scene's: its name, and the verb waiting ("scene", "gotoscene")
        int entry = 1;
        ScriptRunner* runner = nullptr;
        bool restore = false; // a saved game's
        bool walkIn = true;   // a scene's: its flag
    } load_;
    std::string location_; // the current scene's folder ("sirkeci.market")
    void beginLoad(const std::string& location, bool forAct);
    void updateLoad();
    // The location's scene files the game would load (0x423830): variants
    // for the act in place of their base; LOAD 0 ones are read on demand.
    std::vector<std::string> locationFiles(const std::string& location) const;
    std::unique_ptr<MoviePlayer> movie_;
    std::string movieName_;
    std::vector<Subtitle> movieSubtitles_;
    ScriptRunner* movieRunner_ = nullptr;
    bool sceneShown_ = true;
    // Saved games: the save being restored (between "preload" and "load"),
    // NPCs' positions by scene file and model file (the game keeps its
    // scenes, so they stay where they were), the game scene and the menu
    // the menus return to, the view grabbed for a thumbnail.
    std::unique_ptr<SaveGame> restoring_;
    std::map<std::string, SaveGame::Placement> npcPositions_;
    std::string gameScene_, previousMenu_;
    image::Image grab_;
    bool grabDue_ = false, quit_ = false;
    std::map<std::string, std::string> interfaceTexts_;
    ScriptRunner* fadeRunner_ = nullptr;
    std::string dialogueAct_ = "-";   // the act whose table is loaded
    std::map<std::string, Table> dialogueTables_; // by file, as read
    audio::SoundHandle voice_ = audio::kNoSound;
    std::shared_ptr<audio::Sound> voiceSound_; // for its level: the lips move
    const Ac2Record* scriptObject_ = nullptr; // the hotspot a script is "this" of
    const Ac2Record* resumeAfter_ = nullptr;  // the character clicked, its routine resumed after
    int entryPoint_ = -1; // the walk point the player arrives at, after "scene"
    bool entryWalk_ = false; // and whether they walk in
    WalkGrid::Cell leadGoal_; // where the player last set off for (followers follow)
    std::set<std::string> hiddenNames_; // objects hidden by scripts, by name or file
    std::set<std::string> shownNames_;  // and shown, whatever their condition
    std::deque<std::string> scriptLog_;
    std::map<std::string, std::string> soundFiles_; // sound name -> file
    float time_ = 0;
    image::Image dot_; // stands in for a particle texture that's missing
    uint64_t lastMs_ = 0;
    bool charactersDue_ = false; // to load, once the page has stayed on the scene
    uint64_t charactersAt_ = 0;
    // With 3D: the pictures behind the characters as one texture, the
    // characters, and the models and animations loaded so far.
    gfx::Renderer3D* renderer_ = nullptr;
    // The pictures in layers between the characters, back to front:
    // layer k comes after the k-th character in depth order, layer 0
    // (opaque) before them all. Each is a texture, and where its pictures
    // take the shadows off (a byte a pixel; none for layer 0).
    struct PictureLayer {
        gfx::TextureId texture = 0;
        std::vector<uint8_t> resets;
    };
    std::vector<PictureLayer> layers_;
    std::vector<ModelSlot> characterOrder_; // characters_ and footprints in depth order, as draw() found it
    gfx::MeshId quad_ = 0;
    std::vector<Character> characters_;
    std::map<std::string, std::shared_ptr<GpuModel>> models_;
    std::map<std::string, std::shared_ptr<Animation>> animations_;
    std::vector<gfx::MeshId> frameMeshes_;
    std::vector<gfx::TextureId> frameTextures_;
    std::unique_ptr<CoverageMask> shadowMask_;
    std::unique_ptr<ShadowStencil> stencil_;
    uint64_t startMs_ = 0;

    // The mouse (scene_cursor.cpp): the game's pick of the object under a
    // point (0x435eb0), and the cursor drawn there (0x415650).
    bool objectShown(const Ac2Record& o) const;
    bool clickable(const Ac2Record& o) const;
    int cursorOf(const Ac2Record& o) const;
    // Its rectangle on the screen now; false if it has none.
    bool objectRect(const Ac2Record& o, int& left, int& top, int& right, int& bottom) const;
    bool hitObject(const Ac2Record& o, int sx, int sy) const;
    bool hitBones(const Ac2Record& o, int sx, int sy) const;
    const Ac2Record* pickAt(int sx, int sy) const;
    const std::string& itemInHand() const;
    bool canUseOn(const std::string& item, const Ac2Record& o);
    bool interfaceShown() const;
    bool cursorShown() const;
    bool pickAllowed(int sy) const;
    int cursorUnder(int sx, int sy, const Ac2Record** under) const;
    void drawCursor();
    bool cursorMoved_ = false; // to draw again
    // The hotspots the mouse is over (their pictures shown, their sounds
    // played), in the scene shown.
    void updateHotspots();
    void hotspotPressed(const Ac2Record& o);
    void stopSound(const std::string& name);
    std::set<const Ac2Record*> over_;
    std::string overScene_;
    bool systemCursor_ = true; // the system's cursor shown (the game hides it)
    // Footprints and breath (scene_outdoors.cpp): a character's, moved on
    // by dt seconds, `walking` or not; a footprint drawn.
    void updateOutdoors(Character& c, float dt, bool walking);
    void drawFootprint(const Character::Footprint& f, const SceneLighting& lighting, float brightness);
    size_t footprintSerial_ = 0;
    // Escape or Enter: the hotspot with that key pressed; false if none.
    bool hotspotKey(uint32_t key);
};

// Characters and objects (Models/**/*_model.gr2), with their animations.
class ModelPage : public Page {
public:
    explicit ModelPage(ViewerContext& ctx);
    ~ModelPage() override;
    const char* name() const override { return "Models"; }
    size_t count() const override { return models_.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;
    void update(uint64_t nowMs) override;

private:
    struct Entry {
        std::string model;
        std::vector<std::string> animations;
    };
    void load();
    void loadAnimation();
    void releaseGpu();

    std::vector<Entry> models_;
    int index_ = 0, animation_ = 0; // 0: rest pose, else animations[animation_ - 1]
    std::unique_ptr<GpuModel> gpu_;
    Animation anim_;
    bool hasAnim_ = false;
    std::string error_;
    gfx::Renderer3D* renderer_ = nullptr;
    std::vector<gfx::MeshId> meshes_; // this frame's
    Surface overlay_;
    float yaw_ = -1.5708f, pitch_ = 0.15f, distance_ = 400;
    float center_[3] = {0, 0, 0};
    bool playing_ = true, helpers_ = false, dragging_ = false;
    int dragX_ = 0, dragY_ = 0;
    float time_ = 0;
    uint64_t lastMs_ = 0;
};

// Movies (Movies/*.bik) and the scenes' video loops (Elements/**/*.bik,
// cutouts'), with their sound.
class MoviePage : public Page {
public:
    explicit MoviePage(ViewerContext& ctx);
    ~MoviePage() override;
    const char* name() const override { return "Movies"; }
    size_t count() const override { return names_.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;
    void update(uint64_t nowMs) override;

private:
    void open();
    std::vector<std::string> names_;
    int index_ = 0;
    MoviePlayer player_;
    std::vector<Subtitle> subtitles_;
    std::string info_;
};

// Text files: tables, dialogue, subtitles.
class TextPage : public Page {
public:
    explicit TextPage(ViewerContext& ctx);
    const char* name() const override { return "Text"; }
    size_t count() const override { return names_.size(); }
    void activate() override;
    void handleEvent(const Event& ev) override;

private:
    void load();
    void draw();
    std::vector<std::string> names_;
    int index_ = 0, scroll_ = 0;
    std::vector<std::string> lines_;
};

// Sounds: voices, effects and music.
class SoundPage : public Page {
public:
    explicit SoundPage(ViewerContext& ctx);
    const char* name() const override { return "Sounds"; }
    size_t count() const override { return names_.size(); }
    void activate() override;
    void deactivate() override;
    void handleEvent(const Event& ev) override;

private:
    void draw();
    void play();
    std::vector<std::string> names_;
    int index_ = 0;
    std::string info_;
    audio::SoundHandle playing_ = audio::kNoSound;
};

} // namespace reluva::awe
