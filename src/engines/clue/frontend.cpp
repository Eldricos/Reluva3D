// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Which button leads where is the game's, as recordings of the original
// show it and the dialogs' own words say (docs/engines/clue.md); the
// dialogs, their controls and their numbers are dialogInfo.res's.

#include "engines/clue/frontend.h"

#include "engines/clue/viewers.h"

#include "common/audio/decode.h"
#include "common/fs.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>

namespace reluva::clue {

namespace {

constexpr int kW = 640, kH = 480;
// The game's bar: the view of the mansion is above it.
constexpr int kBarTop = 413;
// The windows over the game (231 x 395): Clue.exe 0x435C7F places its exit
// window (dialog 44) at 320 less half its width across and in the middle
// between the top frame (5) and the bar (413) down: (205, 11).
constexpr int kOverGameX = kW / 2 - 231 / 2, kOverGameY = 5 + (kBarTop - 5 - 395) / 2;
// The Host Game window's row of boxes for the number of players, in its
// picture (menu:Ihostgmbg): covered when the window joins by address, by
// the Exit window's picture (menu:Iexitbgnd), the same circle there
// without them (and the same around them).
constexpr int kCoverX = 218, kCoverY = 269, kCoverW = 203, kCoverH = 29;

// dialogInfo.res's dialogs, by number.
enum Dialog {
    kMainMenu = 1, kStartChoice = 2, kLoadGame = 3, kRulesMenu = 4, kRules = 5, kCredits = 6, kExit = 7,
    kConnection = 9, kNetGame = 10, kCreditsText = 14, kVariants = 15, kPicker = 17, kSetupFirst = 18,
    kPrefs = 24, kLoading = 26, kGameMenu = 35, kSaveGame = 36, kGamePrefs = 37, kGameRulesMenu = 38, kGameRules = 39,
    kExitConfirm = 44,
};
// Controls most dialogs share.
constexpr int kReturn = 10000, kOk = 10001;

const char* const kLevelName[3] = {"Novice CPU", "Normal CPU", "Expert CPU"};
// The rules' topics, in the How to Play menu's order, and their files.
const char* const kRulesFile[7] = {"Setup.txt", "Character Movement.txt", "Entering or Leaving a Room.txt",
                                   "Making a Suggestion.txt", "Disproving a Suggestion.txt",
                                   "Making an Accusation.txt", "Game Variants.txt"};
const char* const kPrefKey[Preferences::kSwitches] = {"suggest_movies", "lighting",        "translucency",
                                                      "auto_notes",     "butler_voice",    "walk_animations",
                                                      "mansion_walls"};

std::vector<uint8_t> readFile(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Copies the image's (x, y, w, h) to the same place on the screen.
void copyRect(Surface& s, const image::Image& img, int x, int y, int w, int h) {
    for (int py = std::max(y, 0); py < std::min({y + h, img.h, s.h}); ++py)
        for (int px = std::max(x, 0); px < std::min({x + w, img.w, s.w}); ++px)
            std::memcpy(s.pixels.data() + size_t(py) * size_t(s.pitch) + size_t(px) * 4, img.pixel(px, py), 4);
}

} // namespace

void Preferences::load(const std::filesystem::path& file) {
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = line.substr(0, eq);
        const int value = std::atoi(line.c_str() + eq + 1);
        for (int k = 0; k < kSwitches; ++k)
            if (key == kPrefKey[k])
                on[size_t(k)] = value != 0;
        if (key == "music_volume")
            musicVolume = std::clamp(value, 0, 100);
        if (key == "master_volume")
            masterVolume = std::clamp(value, 0, 100);
    }
}

bool Preferences::loadOriginal(const std::vector<uint8_t>& b) {
    // As Clue.exe reads and writes it (its preferences' constructor at
    // 0x42FA30 in 2.2, the volumes made the mixer's by 127 / 10 at 0x415E63).
    if (b.size() != 16)
        return false;
    for (int k = 0; k < kSwitches; ++k)
        on[size_t(k)] = b[size_t(k)] != 0;
    const auto volume = [&](size_t at) {
        const int v = int(b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (uint32_t(b[at + 3]) << 24));
        return std::clamp(v, 0, 10) * 10;
    };
    musicVolume = volume(8);
    masterVolume = volume(12);
    return true;
}

void Preferences::save(const std::filesystem::path& file) const {
    if (file.empty())
        return;
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file);
    for (int k = 0; k < kSwitches; ++k)
        out << kPrefKey[k] << '=' << (on[size_t(k)] ? 1 : 0) << '\n';
    out << "music_volume=" << musicVolume << "\nmaster_volume=" << masterVolume << '\n';
}

FrontEnd::FrontEnd(System& sys, Surface& screen, const GameData& data, audio::Mixer* mixer,
                   std::filesystem::path prefsFile)
    : sys_(sys), screen_(screen), data_(data), mixer_(mixer), prefsFile_(std::move(prefsFile)) {}

FrontEnd::~FrontEnd() {
    stopMovie();
    stopMusic();
}

Error FrontEnd::open() {
    const std::filesystem::path* resi = data_.find("dialogInfo.res");
    if (!resi)
        return Error::fail("no dialogInfo.res");
    if (Error err = parseResi(readFile(*resi), resi_))
        return Error::fail("dialogInfo.res: " + err.message);
    fonts_ = std::make_unique<Fonts>(data_, resi_);
    if (!prefsFile_.empty())
        prefs_.load(prefsFile_);
    // The first time, the original's own preferences, if it has any.
    std::error_code ec;
    if (prefsFile_.empty() || !std::filesystem::exists(prefsFile_, ec))
        if (const std::filesystem::path* original = data_.find("Prefs.ini"))
            prefs_.loadOriginal(readFile(*original));
    applyVolumes();
    sys_.setWindowTitle("Clue: Murder at Boddy Mansion");
    sys_.setMouseCursorVisible(false);
    go(Screen::Intro);
    return Error::success();
}

void FrontEnd::leave() {
    if (mixer_ && stormSound_ != audio::kNoSound)
        mixer_->stop(stormSound_);
    stormSound_ = audio::kNoSound;
    if (play_)
        play_->stopSounds();
    stopMovie();
    stopMusic();
    sys_.setMouseCursorVisible(true);
}

void FrontEnd::resume() {
    sys_.setWindowTitle("Clue: Murder at Boddy Mansion");
    sys_.setMouseCursorVisible(false);
    if (at_ == Screen::Game || overGame())
        playMusic(true);
    else if (at_ == Screen::Picker || at_ == Screen::Setup || at_ == Screen::NetLobby || at_ == Screen::NetSetup ||
             at_ == Screen::Prefs)
        playMusic(false);
    if (at_ == Screen::Intro || at_ == Screen::Deal)
        go(afterMovie_);
}

std::string FrontEnd::readText(const std::string& file) const {
    const std::filesystem::path* p = data_.find(file);
    if (!p)
        return {};
    const std::vector<uint8_t> b = readFile(*p);
    return {b.begin(), b.end()}; // Windows-1252, as the fonts are
}

void FrontEnd::applyVolumes() {
    if (!mixer_)
        return;
    // As Clue.exe's preferences set them (0x415E6D): the master volume
    // Miles' for the sounds and voices (AIL_set_digital_master_volume), the
    // music's the CD's own (AIL_redbook_set_volume), the one not under the
    // other.
    const int master = prefs_.masterVolume * audio::kMaxVolume / 100;
    mixer_->setTypeVolume(audio::SoundType::Music, prefs_.musicVolume * audio::kMaxVolume / 100);
    mixer_->setTypeVolume(audio::SoundType::Speech, master);
    mixer_->setTypeVolume(audio::SoundType::Effects, master);
}

void FrontEnd::showDialog(int id, int x, int y) {
    dialog_.reset();
    if (const ResiDialog* d = resi_.dialog(id))
        dialog_ = std::make_unique<DialogView>(data_, *fonts_, *d, x, y);
    // So that the control under the pointer shows as it.
    Event move;
    move.type = EventType::MouseMove;
    move.mouseX = mouseX_;
    move.mouseY = mouseY_;
    if (dialog_)
        dialog_->handleEvent(move);
}

void FrontEnd::go(Screen s) {
    at_ = s;
    flashClick_ = -1; // a new screen hides the lightning (Clue.exe 0x40C420)
    // The screens without the storm stop its thunder: the character picker
    // and the network's player setup as they open (Clue.exe 0x412F30,
    // 0x411CB0), the game as it loads (0x404AC2).
    const bool trackFive = s == Screen::Picker || s == Screen::Setup || s == Screen::NetLobby || s == Screen::NetSetup;
    if (mixer_ && stormSound_ != audio::kNoSound &&
        (trackFive || s == Screen::Intro || s == Screen::Loading || s == Screen::Deal || s == Screen::Game)) {
        mixer_->stop(stormSound_);
        stormSound_ = audio::kNoSound;
    }
    if (s != Screen::Loading && mixer_ && loadingSound_ != audio::kNoSound) {
        mixer_->stop(loadingSound_); // loaded (0x404E44)
        loadingSound_ = audio::kNoSound;
    }
    // Those, and Preferences (0x415810), start the CD's track 5 if it
    // isn't playing anything; the menus with the storm have none.
    if (trackFive || s == Screen::Prefs)
        playMusic(false);
    switch (s) {
    case Screen::Intro: startMovie("Intro.avi", Screen::MainMenu); break;
    case Screen::MainMenu:
        mansion_.reset();
        showDialog(kMainMenu);
        break;
    case Screen::StartChoice: showDialog(kStartChoice); break;
    case Screen::Connection:
        showDialog(kConnection);
        // The Internet Game Zone is gone: games by address instead.
        dialog_->setText(0, "Internet|by address");
        break;
    case Screen::NetGame: showDialog(kNetGame); break;
    case Screen::NetHost:
    case Screen::NetJoin:
    case Screen::NetAddress:
    case Screen::NetLobby:
    case Screen::NetSetup:
    case Screen::NetMessage: netShow(s); break;
    case Screen::LoadGame:
        showDialog(kLoadGame);
        // The saved games, by their names; the empty slots light up under
        // the pointer all the same, as the original's do, but do nothing.
        for (int slot = 0; slot < kSaveSlots; ++slot)
            dialog_->setText(slot, savedName(slot));
        break;
    case Screen::RulesMenu: showDialog(kRulesMenu); break;
    case Screen::Rules:
        showDialog(kRules);
        dialog_->setVisible(rulesTopic_, true);
        dialog_->setText(11, readText(kRulesFile[rulesTopic_]));
        break;
    case Screen::Credits: showDialog(kCredits); break;
    case Screen::CreditsText: break; // shown by the Credits buttons
    case Screen::Exit: showDialog(kExit); break;
    case Screen::Picker:
        showDialog(kPicker);
        refreshPicker();
        break;
    case Screen::Setup:
        showDialog(kSetupFirst + setupSuspect_);
        refreshSetup();
        break;
    case Screen::Variants:
        showDialog(kVariants);
        dialog_->setChosen(0, !noDieRoll_);
        dialog_->setChosen(1, noDieRoll_);
        break;
    case Screen::Prefs:
        showDialog(kPrefs);
        refreshPrefs(*dialog_, 0, 28);
        break;
    case Screen::Loading:
        // Clue.exe 0x404AC2: the CD stopped, the loading's own loop.
        stopMusic();
        if (mixer_ && loadingSound_ == audio::kNoSound)
            if (const std::filesystem::path* p = data_.find("load music loop22khz.wav")) {
                auto loop = std::make_shared<audio::Sound>();
                if (!audio::decodeSound(readFile(*p), *loop))
                    loadingSound_ = mixer_->play(loop, audio::SoundType::Effects, kClueSoundVolume, 0, true);
            }
        showDialog(kLoading);
        break;
    case Screen::Deal: startMovie("deal.avi", Screen::Game); break;
    case Screen::Game: {
        bool started = false;
        dialog_.reset();
        gameMenu_.reset();
        if (!play_ && mansion_ && loadSlot_ >= 0) {
            // A saved game, in place of a new one.
            const int slot = loadSlot_;
            loadSlot_ = -1;
            started = true;
            play_ = std::make_unique<Play>(sys_, screen_, data_, mixer_, resi_, *fonts_, *mansion_, level_);
            play_->setLook(playLook());
            if (loadGame(slot)) {
                play_.reset();
                go(Screen::MainMenu);
                return;
            }
        }
        if (!play_ && mansion_) {
            started = true;
            play_ = std::make_unique<Play>(sys_, screen_, data_, mixer_, resi_, *fonts_, *mansion_, level_);
            play_->setLook(playLook());
            const Error err = net_ ? play_->startNetwork(*net_)
                                   : play_->start(players_, noDieRoll_, uint32_t(sys_.getMillis() * 2654435761u));
            if (err) {
                play_.reset();
                go(Screen::MainMenu);
                return;
            }
        }
        // A game starting, new or saved, starts on track 2 (0x404E53); back
        // from the Esc menu, the music is as it was.
        if (started) {
            musicInGame_ = true;
            playTrack(2);
        }
        break;
    }
    case Screen::GameMenu:
        if (const ResiDialog* d = resi_.dialog(kGameMenu)) {
            // Over the game, in the middle of the view.
            gameMenu_ = std::make_unique<DialogView>(data_, *fonts_, *d, kOverGameX, kOverGameY);
            // Save: when it's a human's turn and nothing is going on.
            gameMenu_->setEnabled(0, play_ && play_->canSave() && !savesFolder().empty());
        }
        break;
    case Screen::ExitConfirm:
        // "Are you sure you want to exit the current game?" Yes, No.
        if (const ResiDialog* d = resi_.dialog(kExitConfirm)) {
            gameMenu_ = std::make_unique<DialogView>(data_, *fonts_, *d, kOverGameX, kOverGameY);
            gameMenu_->setVisible(1, false); // its other question: "Would you like to save ...?"
        }
        break;
    // The Esc menu's Preferences and How to Play: windows of their own over
    // the game, where the original has them (dialogs 37-39; recordings show
    // them there), not the menus' screens.
    case Screen::GamePrefs:
        if (const ResiDialog* d = resi_.dialog(kGamePrefs)) {
            gameMenu_ = std::make_unique<DialogView>(data_, *fonts_, *d, kOverGameX, kOverGameY);
            refreshPrefs(*gameMenu_, 1, 29);
        }
        break;
    case Screen::GameRulesMenu:
        if (const ResiDialog* d = resi_.dialog(kGameRulesMenu))
            gameMenu_ = std::make_unique<DialogView>(data_, *fonts_, *d, kOverGameX, kOverGameY);
        break;
    case Screen::GameRules:
        if (const ResiDialog* d = resi_.dialog(kGameRules)) {
            gameMenu_ = std::make_unique<DialogView>(data_, *fonts_, *d, kOverGameX, kOverGameY);
            gameMenu_->setText(3, readText(kRulesFile[rulesTopic_]));
        }
        break;
    case Screen::SaveGame:
        if (const ResiDialog* d = resi_.dialog(kSaveGame)) {
            gameMenu_ = std::make_unique<DialogView>(data_, *fonts_, *d, kOverGameX, kOverGameY);
            saveSlot_ = -1;
            for (int slot = 0; slot < kSaveSlots; ++slot)
                gameMenu_->setText(2 + slot, savedName(slot));
            gameMenu_->setEnabled(1, false);
        }
        break;
    }
}

void FrontEnd::refreshPicker() {
    int count = 0;
    for (const Player& p : players_)
        count += p.kind != Player::None;
    for (int k = 0; k < kSuspects; ++k) {
        const Player& p = players_[size_t(k)];
        const bool taken = p.kind != Player::None;
        dialog_->setVisible(5 + k, !taken);
        dialog_->setVisible(11 + k, taken);
        dialog_->setVisible(23 + k, taken);
        if (taken)
            dialog_->setText(23 + k, p.kind == Player::Human ? "is|" + (p.name.empty() ? "Human" : p.name)
                                                             : std::string("is|") + kLevelName[p.level]);
    }
    dialog_->setVisible(0, count == 0);
    dialog_->setVisible(1, count > 0);
    dialog_->setVisible(2, count < 3);
    dialog_->setVisible(3, count < 3);
    dialog_->setVisible(4, count >= 3);
    dialog_->setEnabled(29, count >= 3);
}

void FrontEnd::refreshSetup() {
    const Player& p = setupDraft_;
    dialog_->setChosen(0, p.kind == Player::Human);
    dialog_->setChosen(1, p.kind == Player::Computer);
    dialog_->setChosen(2, p.kind == Player::None);
    for (int id : {3, 4, 5, 6, 7})
        dialog_->setVisible(id, p.kind == Player::Computer);
    for (int level = 0; level < 3; ++level)
        dialog_->setChosen(5 + level, p.level == level);
    for (int id : {8, 9, 10})
        dialog_->setVisible(id, p.kind == Player::Human);
    if (p.kind == Player::Human) {
        dialog_->setText(10, p.name);
        dialog_->focus(10);
        sys_.setTextInput(true);
    } else {
        dialog_->focus(-1);
        sys_.setTextInput(false);
    }
    int count = 0;
    for (int k = 0; k < kSuspects; ++k)
        count += (k == setupSuspect_ ? p.kind : players_[size_t(k)].kind) != Player::None;
    dialog_->setEnabled(12, count >= 3);
}

void FrontEnd::refreshPrefs(DialogView& view, int first, int musicId) {
    // Each setting has two radio buttons, on and off, each shown lit or not
    // by a pair of images in the same place: first + 4k on (lit, unlit),
    // first + 4k + 2 off (lit, unlit). The menus' window numbers them from
    // 0, the one over the game (dialog 37) from 1.
    for (int k = 0; k < Preferences::kSwitches; ++k) {
        const bool on = prefs_.on[size_t(k)];
        const int b = first + 4 * k;
        view.setVisible(b, on);
        view.setVisible(b + 1, !on);
        view.setVisible(b + 2, !on);
        view.setVisible(b + 3, on);
    }
    view.setValue(musicId, prefs_.musicVolume);
    view.setValue(musicId + 1, prefs_.masterVolume);
}

void FrontEnd::scrollText(DialogView& view, int textId, int knobId, int lines) {
    view.setScroll(textId, view.scroll(textId) + lines);
    const int most = std::max(1, view.lineCount(textId) - view.visibleLines(textId));
    view.setValue(knobId, view.scroll(textId) * 100 / most);
}

void FrontEnd::startGame() {
    for (Player& p : players_)
        if (p.kind == Player::Human && p.name.empty())
            p.name = "Human";
    play_.reset();
    go(Screen::Loading);
}

PlayLook FrontEnd::playLook() const {
    PlayLook look;
    look.suggestMovies = prefs_.on[Preferences::SuggestMovies];
    look.autoNotes = prefs_.on[Preferences::AutoNotes];
    look.butlerVoice = prefs_.on[Preferences::ButlerVoice];
    look.walkAnimations = prefs_.on[Preferences::WalkAnimations];
    look.mansion.lighting = prefs_.on[Preferences::Lighting];
    // As a recording of the original shows them (docs/engines/clue.md):
    // "Mansion Walls" off, no walls; "Translucency": what stands in front of
    // the suspect whose turn it is see-through, or (off) left out.
    look.mansion.walls = prefs_.on[Preferences::MansionWalls];
    look.mansion.inFront = prefs_.on[Preferences::Translucency] ? MansionLook::InFront::SeeThrough
                                                                : MansionLook::InFront::Hidden;
    return look;
}

void FrontEnd::onClick(int id) {
    switch (at_) {
    case Screen::MainMenu:
        if (id == 1)
            go(Screen::StartChoice);
        else if (id == 3)
            go(Screen::LoadGame);
        else if (id == 2) {
            prefsReturn_ = Screen::MainMenu;
            go(Screen::Prefs);
        } else if (id == 0) {
            rulesReturn_ = Screen::MainMenu;
            go(Screen::RulesMenu);
        } else if (id == 5)
            go(Screen::Credits);
        else if (id == 7)
            go(Screen::Exit);
        break;
    case Screen::StartChoice:
        if (id == 0)
            go(Screen::Picker);
        else if (id == 1)
            go(Screen::Connection);
        else if (id == kReturn)
            go(Screen::MainMenu);
        break;
    case Screen::Connection:
        if (id == kReturn)
            go(Screen::StartChoice);
        else
            netClick(id);
        break;
    case Screen::RulesMenu:
        if (id >= 0 && id <= 6) {
            rulesTopic_ = id;
            go(Screen::Rules);
        } else if (id == kReturn)
            go(rulesReturn_ == Screen::GameMenu ? Screen::Game : Screen::MainMenu);
        break;
    case Screen::Rules:
        if (id == 8)
            scrollText(*dialog_, 11, 10, -1);
        else if (id == 9)
            scrollText(*dialog_, 11, 10, 1);
        else if (id == 10) {
            const int most = std::max(0, dialog_->lineCount(11) - dialog_->visibleLines(11));
            dialog_->setScroll(11, dialog_->value(10) * most / 100);
        } else if (id == 7)
            go(Screen::RulesMenu);
        else if (id == kReturn)
            go(rulesReturn_ == Screen::GameMenu ? Screen::Game : Screen::MainMenu);
        break;
    case Screen::Credits:
        if (id == 0 || id == 1) {
            showDialog(kCreditsText);
            dialog_->setText(3, readText(id == 0 ? "clue Credits.txt" : "EAI_credits.txt"));
            at_ = Screen::CreditsText;
        } else if (id == kReturn)
            go(Screen::MainMenu);
        break;
    case Screen::CreditsText:
        if (id == 0)
            scrollText(*dialog_, 3, 2, -1);
        else if (id == 1)
            scrollText(*dialog_, 3, 2, 1);
        else if (id == 2) {
            const int most = std::max(0, dialog_->lineCount(3) - dialog_->visibleLines(3));
            dialog_->setScroll(3, dialog_->value(2) * most / 100);
        } else if (id == kReturn)
            go(Screen::Credits);
        break;
    case Screen::Exit:
        if (id == kOk)
            quit_ = true;
        else if (id == kReturn || id == 0)
            go(Screen::MainMenu);
        break;
    case Screen::Picker:
        if (id >= 5 && id <= 16) {
            setupSuspect_ = (id - 5) % kSuspects;
            setupDraft_ = players_[size_t(setupSuspect_)];
            go(Screen::Setup);
        } else if (id == 29)
            startGame();
        else if (id == 30)
            go(Screen::MainMenu);
        break;
    case Screen::Setup:
        if (dialog_ && setupDraft_.kind == Player::Human)
            setupDraft_.name = dialog_->text(10);
        if (id == 0)
            setupDraft_.kind = Player::Human;
        else if (id == 1)
            setupDraft_.kind = Player::Computer;
        else if (id == 2)
            setupDraft_.kind = Player::None;
        else if (id >= 5 && id <= 7)
            setupDraft_.level = id - 5;
        if (id <= 7) {
            refreshSetup();
            break;
        }
        sys_.setTextInput(false);
        if (id == kOk || id == 12)
            players_[size_t(setupSuspect_)] = setupDraft_;
        if (id == kOk || id == kReturn)
            go(Screen::Picker);
        else if (id == 11)
            go(Screen::Variants);
        else if (id == 12)
            startGame();
        break;
    case Screen::Variants:
        if (id == 0 || id == 1)
            noDieRoll_ = id == 1;
        if (id == 0 || id == 1 || id == kReturn)
            go(Screen::Setup);
        break;
    case Screen::Prefs:
        if (id >= 0 && id < 4 * Preferences::kSwitches) {
            // The on column's buttons are 4k and 4k + 1, the off column's 4k + 2 and 4k + 3.
            prefs_.on[size_t(id / 4)] = id % 4 < 2;
            refreshPrefs(*dialog_, 0, 28);
        } else if (id == 28 || id == 29) {
            (id == 28 ? prefs_.musicVolume : prefs_.masterVolume) = dialog_->value(id);
            applyVolumes();
        } else if (id == kReturn) {
            // Clue.exe 0x41650C: leaving stops the CD, in the menus or the
            // game alike.
            stopMusic();
            prefs_.save(prefsFile_);
            go(prefsReturn_ == Screen::GameMenu ? Screen::Game : Screen::MainMenu);
        }
        break;
    case Screen::GameMenu:
        if (id == 4)
            go(Screen::Game);
        else if (id == 0)
            go(Screen::SaveGame);
        else if (id == 3)
            go(Screen::ExitConfirm);
        else if (id == 1)
            go(Screen::GamePrefs);
        else if (id == 2)
            go(Screen::GameRulesMenu);
        break;
    case Screen::GamePrefs:
        // As the menus' (Clue.exe 0x43F0B0 sets the volumes the same way),
        // but leaving it doesn't touch the music; Continue, back to the game.
        if (id >= 1 && id <= 4 * Preferences::kSwitches) {
            prefs_.on[size_t((id - 1) / 4)] = (id - 1) % 4 < 2;
            refreshPrefs(*gameMenu_, 1, 29);
            if (play_)
                play_->setLook(playLook());
        } else if (id == 29 || id == 30) {
            (id == 29 ? prefs_.musicVolume : prefs_.masterVolume) = gameMenu_->value(id);
            applyVolumes();
        } else if (id == 0) {
            prefs_.save(prefsFile_);
            go(Screen::Game);
        }
        break;
    case Screen::GameRulesMenu:
        if (id >= 0 && id <= 6) {
            rulesTopic_ = id;
            go(Screen::GameRules);
        } else if (id == 7)
            go(Screen::Game);
        break;
    case Screen::GameRules:
        if (id == 0)
            scrollText(*gameMenu_, 3, 2, -1);
        else if (id == 1)
            scrollText(*gameMenu_, 3, 2, 1);
        else if (id == 2) {
            const int most = std::max(0, gameMenu_->lineCount(3) - gameMenu_->visibleLines(3));
            gameMenu_->setScroll(3, gameMenu_->value(2) * most / 100);
        } else if (id == 5)
            go(Screen::GameRulesMenu);
        else if (id == 4)
            go(Screen::Game);
        break;
    case Screen::ExitConfirm:
        if (id == kOk) {
            stopMusic();
            if (play_)
                play_->stopSounds();
            play_.reset();
            if (net_)
                netLeave();
            else
                go(Screen::MainMenu);
        } else if (id == kReturn)
            go(Screen::GameMenu);
        break;
    case Screen::SaveGame:
        if (id >= 2 && id < 2 + kSaveSlots) {
            // A slot chosen: its name to type, over its button.
            if (saveSlot_ >= 0) {
                gameMenu_->setVisible(10 + saveSlot_, false);
                gameMenu_->setVisible(2 + saveSlot_, true);
            }
            saveSlot_ = id - 2;
            // Its name so far, to type over; an empty slot's to type afresh.
            const std::string name = savedName(saveSlot_);
            gameMenu_->setVisible(id, false);
            gameMenu_->setVisible(10 + saveSlot_, true);
            gameMenu_->setText(10 + saveSlot_, name);
            gameMenu_->focus(10 + saveSlot_);
            gameMenu_->setEnabled(1, true);
            sys_.setTextInput(true);
        } else if (id == 1 && saveSlot_ >= 0 && play_ && play_->canSave()) {
            sys_.setTextInput(false);
            saveGame(saveSlot_, gameMenu_->text(10 + saveSlot_));
            go(Screen::Game);
        } else if (id == 0) {
            sys_.setTextInput(false);
            go(Screen::GameMenu);
        }
        break;
    case Screen::LoadGame:
        if (id >= 0 && id < kSaveSlots && !savedName(id).empty()) {
            loadSlot_ = id;
            play_.reset();
            go(Screen::Loading);
        } else if (id == kReturn)
            go(Screen::MainMenu);
        break;
    default: netClick(id); break;
    }
}

std::filesystem::path FrontEnd::savesFolder() const {
    return prefsFile_.empty() ? std::filesystem::path() : prefsFile_.parent_path() / "saves";
}

std::filesystem::path FrontEnd::saveFile(int slot) const {
    return savesFolder() / ("slot" + std::to_string(slot + 1) + ".sav");
}

std::string FrontEnd::savedName(int slot) const {
    if (savesFolder().empty())
        return {};
    std::ifstream in(saveFile(slot), std::ios::binary);
    std::string tag, name;
    int version = 0;
    if (!(in >> tag >> version) || tag != "reluva-clue-save" || in.get() != '\n' || !saveio::getString(in, name))
        return {};
    return name.empty() ? "Game " + std::to_string(slot + 1) : name;
}

Error FrontEnd::saveGame(int slot, const std::string& name) {
    if (!play_ || savesFolder().empty())
        return Error::fail("nothing to save");
    std::error_code ec;
    std::filesystem::create_directories(savesFolder(), ec);
    // Written whole beside it, then put in its place.
    const std::filesystem::path file = saveFile(slot), part = file.string() + ".part";
    {
        std::ofstream out(part, std::ios::binary);
        out << "reluva-clue-save 1\n";
        saveio::putString(out, name);
        out << '\n';
        play_->save(out);
        if (!out)
            return Error::fail("couldn't write " + part.string());
    }
    std::filesystem::rename(part, file, ec);
    return ec ? Error::fail("couldn't write " + file.string()) : Error::success();
}

Error FrontEnd::loadGame(int slot) {
    std::ifstream in(saveFile(slot), std::ios::binary);
    std::string tag, name;
    int version = 0;
    if (!(in >> tag >> version) || tag != "reluva-clue-save" || version != 1 || in.get() != '\n' ||
        !saveio::getString(in, name))
        return Error::fail("not a saved game");
    return play_->load(in);
}

void FrontEnd::handleEvent(const Event& ev) {
    if (ev.type == EventType::MouseMove || ev.type == EventType::MouseDown || ev.type == EventType::MouseUp) {
        mouseX_ = ev.mouseX;
        mouseY_ = ev.mouseY;
    }
    // Videos: a key or a click skips them.
    if (movie_) {
        if (ev.type == EventType::KeyDown || ev.type == EventType::MouseDown) {
            const Screen next = afterMovie_;
            stopMovie();
            go(next);
        }
        return;
    }
    if (at_ == Screen::Game) {
        if (ev.type == EventType::KeyDown && ev.key == key::Escape)
            go(Screen::GameMenu);
        else if (play_)
            play_->handleEvent(ev);
        return;
    }
    if (overGame()) {
        if (ev.type == EventType::KeyDown && ev.key == key::Escape) {
            if (at_ == Screen::SaveGame)
                sys_.setTextInput(false);
            go(at_ == Screen::GameMenu ? Screen::Game : Screen::GameMenu);
            return;
        }
        if (ev.type == EventType::KeyDown && ev.key == key::Return && at_ == Screen::SaveGame) {
            onClick(1);
            return;
        }
        if (gameMenu_) {
            const int id = gameMenu_->handleEvent(ev);
            if (id >= 0)
                onClick(id);
        }
        return;
    }
    if (flashClick_ >= 0) {
        // The lightning's second keeps the screen; what is pressed meanwhile
        // waits, as Windows queues it, and is handled afterwards.
        heldEvents_.push_back(ev);
        return;
    }
    if (!dialog_)
        return;
    // The network screens: Esc goes back (a lobby is left), Enter sends.
    const bool netScreen = at_ == Screen::NetHost || at_ == Screen::NetAddress || at_ == Screen::NetJoin ||
                           at_ == Screen::NetLobby || at_ == Screen::NetSetup || at_ == Screen::NetMessage;
    if (netScreen && ev.type == EventType::KeyDown && (ev.key == key::Escape || ev.key == key::Return)) {
        const bool esc = ev.key == key::Escape;
        if (at_ == Screen::NetLobby)
            esc ? netLeave() : netClick(60);
        else if (at_ == Screen::NetMessage)
            netClick(kOk);
        else if (at_ == Screen::NetHost || at_ == Screen::NetAddress)
            netClick(esc ? kReturn : 2);
        else if (esc)
            netClick(kReturn);
        return;
    }
    // Esc as each screen's way back.
    if (ev.type == EventType::KeyDown && ev.key == key::Escape) {
        if (at_ == Screen::MainMenu)
            go(Screen::Exit);
        else if (at_ == Screen::Setup || at_ == Screen::Exit || at_ == Screen::Variants || at_ == Screen::Prefs ||
                 at_ == Screen::Credits || at_ == Screen::CreditsText || at_ == Screen::RulesMenu ||
                 at_ == Screen::Rules || at_ == Screen::StartChoice || at_ == Screen::Connection ||
                 at_ == Screen::NetGame || at_ == Screen::LoadGame)
            onClick(at_ == Screen::Exit ? 0 : kReturn);
        else if (at_ == Screen::Picker)
            onClick(30);
        return;
    }
    if (ev.type == EventType::KeyDown && ev.key == key::Return && at_ == Screen::Setup) {
        onClick(kOk);
        return;
    }
    const int id = dialog_->handleEvent(ev);
    if (id >= 0) {
        if (stormScreen())
            startFlash(id);
        else
            onClick(id);
    }
}

void FrontEnd::startMovie(const std::string& file, Screen after) {
    stopMovie();
    stopMusic();
    afterMovie_ = after;
    const std::filesystem::path* p = data_.find(file);
    auto player = std::make_unique<video::VideoPlayer>();
    if (!p || player->open(readFile(*p))) {
        go(after); // without the video, on to what follows it
        return;
    }
    if (mixer_ && player->file().hasAudio()) {
        auto sound = std::make_shared<audio::Sound>();
        if (!player->file().audio(*sound))
            movieSound_ = mixer_->play(sound, audio::SoundType::Speech);
    }
    movie_ = std::move(player);
    movieStart_ = sys_.getMillis();
}

void FrontEnd::stopMovie() {
    if (mixer_ && movieSound_ != audio::kNoSound)
        mixer_->stop(movieSound_);
    movieSound_ = audio::kNoSound;
    movie_.reset();
}

void FrontEnd::playMusic(bool inGame) {
    // The CD's track 5 loops alone (the picker, the player setup,
    // Preferences); tracks 2, 3 and 4 follow one another (the game). Either
    // only if the CD isn't playing already (Clue.exe 0x42F120). See
    // docs/engines/clue.md.
    if (!mixer_ || (music_ != audio::kNoSound && mixer_->isPlaying(music_)))
        return;
    musicInGame_ = inGame;
    playTrack(inGame ? 2 : 5);
}

void FrontEnd::playTrack(int track) {
    stopMusic();
    musicTrack_ = track;
    auto it = tracks_.find(track);
    if (it == tracks_.end()) {
        // Ripped from the CD as trackNN.wav or .mp3 (reluva-tool cue-rip);
        // without them the game is silent, as the original without its CD.
        std::shared_ptr<audio::Sound> s;
        for (const char* ext : {".wav", ".mp3"}) {
            char name[16];
            std::snprintf(name, sizeof name, "track%02d%s", track, ext);
            if (const std::filesystem::path* p = data_.find(name)) {
                auto decoded = std::make_shared<audio::Sound>();
                if (!audio::decodeSound(readFile(*p), *decoded)) {
                    s = decoded;
                    break;
                }
            }
        }
        it = tracks_.emplace(track, s).first;
    }
    if (it->second)
        music_ = mixer_->play(it->second, audio::SoundType::Music, audio::kMaxVolume, 0, !musicInGame_);
}

void FrontEnd::stopMusic() {
    if (mixer_ && music_ != audio::kNoSound)
        mixer_->stop(music_);
    music_ = audio::kNoSound;
}

void FrontEnd::drawStorm() {
    // The menus' lower corners: the mansion on its hill and the cliff, the
    // storm over them: controls 1000-1003, placed by Clue.exe 0x40C520; while
    // a click's lightning lasts, the flashes (1004-1007) at the same places.
    struct Panel {
        const char *animation, *flash;
        int x, y;
    };
    static const Panel kPanels[] = {{"main_anim:Alt1anim", "menu:Alt1flash", 13, 239},
                                    {"main_anim:Alt2anim", "menu:Alt2flash", 13, 317},
                                    {"main_anim:Art1anim", "menu:Art1flash", 421, 239},
                                    {"main_anim:Art2anim", "menu:Art2flash", 396, 317}};
    for (const Panel& p : kPanels)
        for (const char* name : {p.animation, flashClick_ >= 0 ? p.flash : nullptr}) {
            const Animation* a = name ? data_.animation(name) : nullptr;
            if (!a || a->layers.empty() || a->frameCount() == 0)
                continue;
            const size_t f = a->frameAt(now_, false);
            for (const auto& layer : a->layers) {
                const Animation::Part& part = layer[f];
                if (part.image >= a->images.size())
                    continue;
                if (const Image* img = data_.image(a->images[part.image]))
                    image::blit(screen_, img->pixels, p.x + part.x + img->offsetX, p.y + part.y + img->offsetY);
            }
        }
}

bool FrontEnd::stormScreen() const {
    return dialog_ && !overGame() && at_ != Screen::Game && at_ != Screen::Loading && at_ != Screen::Picker &&
           at_ != Screen::Setup && at_ != Screen::NetLobby && at_ != Screen::NetSetup && at_ != Screen::NetMessage;
}

namespace {
std::shared_ptr<audio::Sound> loadSound(const GameData& data, const char* file) {
    const std::filesystem::path* p = data.find(file);
    if (!p)
        return nullptr;
    auto s = std::make_shared<audio::Sound>();
    if (audio::decodeSound(readFile(*p), *s))
        return nullptr;
    playAsClueExe(*s);
    return s;
}
} // namespace

void FrontEnd::menuThunder() {
    // Clue.exe 0x40C4A0, run as each storm screen opens: mnThndr1.wav
    // (sound 2) looping, started again only if it isn't playing.
    if (!mixer_ || (stormSound_ != audio::kNoSound && mixer_->isPlaying(stormSound_)))
        return;
    if (auto s = loadSound(data_, "mnThndr1.wav"))
        stormSound_ = mixer_->play(s, audio::SoundType::Effects, kClueSoundVolume, 0, true);
}

void FrontEnd::startFlash(int id) {
    // Clue.exe 0x40C350, the storm screens' handler of a click (notify
    // code 0): mnThndr2.wav (sound 3) once, the four flash pictures
    // (controls 1004-1007) shown, 500 ms with the screen still and 500 ms
    // more with it going on; then the click is acted on, and the screen it
    // opens hides the flashes (0x40C420).
    if (mixer_)
        if (auto s = loadSound(data_, "mnThndr2.wav"))
            mixer_->play(s, audio::SoundType::Effects, kClueSoundVolume);
    flashUntil_ = now_ + 1000;
    flashClick_ = id;
}

void FrontEnd::drawCursor() {
    // The game's pointer, a gloved hand pointing up and to the left.
    const Animation* a = data_.animation("cursors:Apointcrsr");
    if (!a || a->layers.empty() || a->layers[0].empty() || a->layers[0][0].image >= a->images.size())
        return;
    const Image* img = data_.image(a->images[a->layers[0][0].image]);
    if (!img)
        return;
    // The pointer's images fill their 32x32 square, colour 0 (black) around
    // the hand: a colour key, as the recordings show no square. Elsewhere
    // colour 0 is drawn black (the piano in a recording's frame is).
    const image::Image& p = img->pixels;
    for (int y = 0; y < p.h; ++y)
        for (int x = 0; x < p.w; ++x) {
            const uint8_t* c = p.pixel(x, y);
            const int sx = mouseX_ + x, sy = mouseY_ + y;
            if (c[3] == 0 || (c[0] | c[1] | c[2]) == 0 || sx < 0 || sy < 0 || sx >= kW || sy >= kH)
                continue;
            uint8_t* d = screen_.row(sy) + sx * 4;
            d[0] = c[0];
            d[1] = c[1];
            d[2] = c[2];
        }
}

void FrontEnd::update(uint64_t nowMs) {
    now_ = nowMs;
    netUpdate(); // the network's, whatever is on the screen
    if (movie_) {
        const double t = double(nowMs - movieStart_) / 1000.0;
        const bool soundOn = mixer_ && movieSound_ != audio::kNoSound && mixer_->isPlaying(movieSound_);
        if (movie_->seek(t) || (movie_->finished(t) && !soundOn)) {
            const Screen next = afterMovie_;
            stopMovie();
            go(next);
        } else {
            // Twice the size: 320x240 fills the screen.
            const image::Image& pic = movie_->picture();
            const int scale = std::max(1, std::min(kW / std::max(1, pic.w), kH / std::max(1, pic.h)));
            fillRect(screen_, 0, 0, kW, kH, {0, 0, 0});
            const int x0 = (kW - pic.w * scale) / 2, y0 = (kH - pic.h * scale) / 2;
            for (int y = 0; y < pic.h * scale; ++y) {
                uint8_t* d = screen_.row(y0 + y) + x0 * 4;
                for (int x = 0; x < pic.w * scale; ++x, d += 4) {
                    const uint8_t* p = pic.pixel(x / scale, y / scale);
                    d[0] = p[0];
                    d[1] = p[1];
                    d[2] = p[2];
                    d[3] = 255;
                }
            }
            return;
        }
    }
    if (flashClick_ >= 0 && now_ >= flashUntil_) {
        const int id = flashClick_;
        flashClick_ = -1;
        onClick(id);
        std::vector<Event> held;
        held.swap(heldEvents_);
        for (size_t i = 0; i < held.size(); ++i) {
            handleEvent(held[i]);
            if (flashClick_ >= 0) { // another flash: the rest wait for it
                heldEvents_.insert(heldEvents_.begin(), held.begin() + i + 1, held.end());
                break;
            }
        }
    }
    if (play_)
        if (const int cue = play_->takeMusicCue()) {
            if (cue < 0)
                stopMusic();
            else {
                musicInGame_ = true;
                playTrack(cue);
            }
        }
    // The game's music goes on to its next track: 2, 3, 4, 2 ...
    if (musicInGame_ && mixer_ && music_ != audio::kNoSound && !mixer_->isPlaying(music_) &&
        (at_ == Screen::Game || overGame()))
        playTrack(musicTrack_ == 4 ? 2 : musicTrack_ + 1);
    if (at_ == Screen::Loading) {
        // Shown while the mansion is put together, then the deal.
        fillRect(screen_, 0, 0, kW, kH, {0, 0, 0});
        if (dialog_)
            dialog_->draw(screen_);
        sys_.updateScreen(screen_);
        if (!mansion_) {
            mansion_ = std::make_unique<Mansion>();
            const std::filesystem::path* lvl = data_.find("clue.lvl");
            if (!lvl || levelFile_.open(*lvl) || parseLevel(levelFile_, level_) || mansion_->build(data_, level_)) {
                mansion_.reset();
                go(Screen::MainMenu);
                return;
            }
        }
        // A saved game goes straight on; a new one is dealt first.
        go(loadSlot_ >= 0 ? Screen::Game : Screen::Deal);
        return;
    }
    // The game goes on behind the Esc menu and what it opens, as the
    // original's does (its computer players play on; recordings show it).
    const bool behind = overGame() || (prefsReturn_ == Screen::GameMenu && at_ == Screen::Prefs) ||
                        (rulesReturn_ == Screen::GameMenu && (at_ == Screen::RulesMenu || at_ == Screen::Rules));
    if (play_ && (at_ == Screen::Game || behind)) {
        play_->setLook(playLook());
        play_->update(nowMs);
    }
    // Save as the game stands now (it goes on behind the menu).
    if (at_ == Screen::GameMenu && gameMenu_ && play_)
        gameMenu_->setEnabled(0, play_->canSave() && !savesFolder().empty());
    if ((at_ == Screen::Game || overGame()) && play_) {
        if (play_->finished()) {
            // The game over: back to the main menu, as the original.
            play_->stopSounds();
            play_.reset();
            if (net_)
                netLeave();
            else
                go(Screen::MainMenu);
            drawCursor();
            return;
        }
        play_->draw();
        if (overGame() && gameMenu_)
            gameMenu_->draw(screen_);
    } else {
        fillRect(screen_, 0, 0, kW, kH, {0, 0, 0});
        if (dialog_) {
            // The storm goes between the menu's background and its controls.
            dialog_->drawBackground(screen_);
            if (at_ == Screen::NetAddress)
                if (const Image* cover = data_.image("menu:Iexitbgnd"))
                    copyRect(screen_, cover->pixels, kCoverX, kCoverY, kCoverW, kCoverH);
            if (at_ != Screen::Picker && at_ != Screen::Setup && at_ != Screen::NetLobby && at_ != Screen::NetSetup &&
                at_ != Screen::NetMessage) {
                drawStorm();
                menuThunder();
            }
            dialog_->drawControls(screen_);
        }
    }
    drawCursor();
}

} // namespace reluva::clue
