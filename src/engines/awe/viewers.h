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
#include "engines/awe/game_paths.h"
#include "engines/awe/particles.h"
#include "engines/awe/script.h"
#include "engines/awe/story.h"
#include "engines/awe/walk_grid.h"
#include "engines/awe/game_text.h"
#include "engines/awe/gr2.h"
#include "engines/awe/granny_dll.h"
#include "engines/awe/model.h"
#include "engines/awe/model_draw.h"
#include "engines/awe/movie_player.h"
#include "engines/awe/savegame.h"

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
    GrannyDll* granny;    // null if granny2.dll couldn't be loaded
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
    };
    // A video loop the scene shows (the countryside going by a window),
    // by the object that places it.
    struct Loop {
        std::unique_ptr<video::BinkFile> file;
        std::unique_ptr<video::BinkVideoDecoder> video;
        image::Image frame;
        size_t next = 0; // the frame to decode next
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
    bool changeScene(const std::string& name, int entry);
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
    void finishRestore();
    // "menu <name>", "previous [scene]": the menus and back.
    void openMenu(const std::string& name);
    void previous(bool toScene);
    // A game key: the in-game, save or load menu (the view kept for the
    // thumbnail), F2 quick save, F3 quick load. False if it isn't one.
    bool gameKey(uint32_t key);
    bool quickSave();
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
    void logScript(const std::string& line);
    bool hidden(const Ac2Record& o) const;
    // Starts the character's walk (or run) along its path: false if it
    // has no such animation.
    bool startWalk(Character& c, bool run = false);
    void placeCharacter(Character& c) const;
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
    // The camera's walk grid (G shows it), the cell under the mouse and
    // the last path found.
    WalkGrid grid_;
    bool showGrid_ = false;
    WalkGrid::Cell hover_;
    std::vector<WalkGrid::Cell> path_;
    // The game's own path-finder, from motoe.exe (loaded on the first
    // walk); Reluva's A* if it can't be.
    std::unique_ptr<GamePathFinder> gamePaths_;
    bool triedGamePaths_ = false;
    std::string pathSource_;
    // The scripts' state: tags, items and the act are state_'s.
    std::unique_ptr<ScriptRunner> runner_;
    std::unique_ptr<DialogueManager> dialogue_;
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
    audio::SoundHandle voice_ = audio::kNoSound;
    std::shared_ptr<audio::Sound> voiceSound_; // for its level: the lips move
    const Ac2Record* scriptObject_ = nullptr; // the hotspot a script is "this" of
    const Ac2Record* resumeAfter_ = nullptr;  // the character clicked, its routine resumed after
    int entryPoint_ = -1; // the walk point the player arrives at, after "scene"
    std::set<std::string> hiddenNames_; // objects hidden by scripts, by name or file
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
    gfx::TextureId back_ = 0;
    gfx::MeshId quad_ = 0;
    std::vector<Character> characters_;
    std::map<std::string, std::shared_ptr<GpuModel>> models_;
    std::map<std::string, std::shared_ptr<Animation>> animations_;
    std::vector<gfx::MeshId> frameMeshes_;
    uint64_t startMs_ = 0;
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
