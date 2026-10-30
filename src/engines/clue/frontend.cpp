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
#include <fstream>
#include <iterator>
#include <sstream>

namespace reluva::clue {

namespace {

constexpr int kW = 640, kH = 480;
// The game's bar: the view of the mansion is above it.
constexpr int kBarTop = 413;

// dialogInfo.res's dialogs, by number.
enum Dialog {
    kMainMenu = 1, kStartChoice = 2, kLoadGame = 3, kRulesMenu = 4, kRules = 5, kCredits = 6, kExit = 7,
    kConnection = 9, kNetGame = 10, kCreditsText = 14, kVariants = 15, kPicker = 17, kSetupFirst = 18,
    kPrefs = 24, kLoading = 26, kGameMenu = 35, kSaveGame = 36,
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
    applyVolumes();
    sys_.setWindowTitle("Clue: Murder at Boddy Mansion");
    sys_.setMouseCursorVisible(false);
    go(Screen::Intro);
    return Error::success();
}

void FrontEnd::leave() {
    if (play_)
        play_->stopSounds();
    stopMovie();
    stopMusic();
    sys_.setMouseCursorVisible(true);
}

void FrontEnd::resume() {
    sys_.setWindowTitle("Clue: Murder at Boddy Mansion");
    sys_.setMouseCursorVisible(false);
    if (at_ == Screen::Game || at_ == Screen::GameMenu || at_ == Screen::SaveGame)
        playMusic(true);
    else if (at_ != Screen::Intro && at_ != Screen::Deal)
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
    const int master = prefs_.masterVolume * audio::kMaxVolume / 100;
    mixer_->setTypeVolume(audio::SoundType::Music, master * prefs_.musicVolume / 100);
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
    switch (s) {
    case Screen::Intro: startMovie("Intro.avi", Screen::MainMenu); break;
    case Screen::MainMenu:
        mansion_.reset();
        playMusic(false);
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
        // The saved games, by their names; the empty slots can't be chosen.
        for (int slot = 0; slot < kSaveSlots; ++slot) {
            const std::string name = savedName(slot);
            dialog_->setText(slot, name);
            dialog_->setEnabled(slot, !name.empty());
        }
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
        refreshPrefs();
        break;
    case Screen::Loading:
        stopMusic();
        showDialog(kLoading);
        break;
    case Screen::Deal: startMovie("deal.avi", Screen::Game); break;
    case Screen::Game:
        dialog_.reset();
        gameMenu_.reset();
        if (!play_ && mansion_ && loadSlot_ >= 0) {
            // A saved game, in place of a new one.
            const int slot = loadSlot_;
            loadSlot_ = -1;
            play_ = std::make_unique<Play>(sys_, screen_, data_, mixer_, resi_, *fonts_, *mansion_, level_);
            play_->setLook(playLook());
            if (loadGame(slot)) {
                play_.reset();
                go(Screen::MainMenu);
                return;
            }
        }
        if (!play_ && mansion_) {
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
        playMusic(true);
        break;
    case Screen::GameMenu:
        if (const ResiDialog* d = resi_.dialog(kGameMenu)) {
            // Over the game, in the middle of the view.
            gameMenu_ = std::make_unique<DialogView>(data_, *fonts_, *d, (kW - 231) / 2, (kBarTop - 395) / 2);
            // Save: when it's a human's turn and nothing is going on.
            gameMenu_->setEnabled(0, play_ && play_->canSave() && !savesFolder().empty());
        }
        break;
    case Screen::SaveGame:
        if (const ResiDialog* d = resi_.dialog(kSaveGame)) {
            gameMenu_ = std::make_unique<DialogView>(data_, *fonts_, *d, (kW - 231) / 2, (kBarTop - 395) / 2);
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

void FrontEnd::refreshPrefs() {
    // Each setting has two radio buttons, on and off, each shown lit or not
    // by a pair of images in the same place.
    for (int k = 0; k < Preferences::kSwitches; ++k) {
        const bool on = prefs_.on[size_t(k)];
        dialog_->setVisible(4 * k, on);
        dialog_->setVisible(4 * k + 1, !on);
        dialog_->setVisible(4 * k + 2, !on);
        dialog_->setVisible(4 * k + 3, on);
    }
    dialog_->setValue(28, prefs_.musicVolume);
    dialog_->setValue(29, prefs_.masterVolume);
}

void FrontEnd::scrollText(int textId, int knobId, int lines) {
    dialog_->setScroll(textId, dialog_->scroll(textId) + lines);
    const int most = std::max(1, dialog_->lineCount(textId) - dialog_->visibleLines(textId));
    dialog_->setValue(knobId, dialog_->scroll(textId) * 100 / most);
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
    // "Mansion Walls" off, no walls at all; "Translucency" off, the near
    // walls gone; on, the near walls see-through.
    look.mansion.walls = !prefs_.on[Preferences::MansionWalls] ? MansionLook::Walls::None
                         : prefs_.on[Preferences::Translucency] ? MansionLook::Walls::SeeThrough
                                                                 : MansionLook::Walls::Hidden;
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
            scrollText(11, 10, -1);
        else if (id == 9)
            scrollText(11, 10, 1);
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
            scrollText(3, 2, -1);
        else if (id == 1)
            scrollText(3, 2, 1);
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
            refreshPrefs();
        } else if (id == 28 || id == 29) {
            (id == 28 ? prefs_.musicVolume : prefs_.masterVolume) = dialog_->value(id);
            applyVolumes();
        } else if (id == kReturn) {
            prefs_.save(prefsFile_);
            go(prefsReturn_ == Screen::GameMenu ? Screen::Game : Screen::MainMenu);
        }
        break;
    case Screen::GameMenu:
        if (id == 4)
            go(Screen::Game);
        else if (id == 0)
            go(Screen::SaveGame);
        else if (id == 3) {
            stopMusic();
            if (play_)
                play_->stopSounds();
            play_.reset();
            if (net_)
                netLeave();
            else
                go(Screen::MainMenu);
        } else if (id == 1) {
            prefsReturn_ = Screen::GameMenu;
            go(Screen::Prefs);
        } else if (id == 2) {
            rulesReturn_ = Screen::GameMenu;
            go(Screen::RulesMenu);
        }
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
        } else if (id == 1 && saveSlot_ >= 0) {
            sys_.setTextInput(false);
            saveGame(saveSlot_, gameMenu_->text(10 + saveSlot_));
            go(Screen::Game);
        } else if (id == 0) {
            sys_.setTextInput(false);
            go(Screen::GameMenu);
        }
        break;
    case Screen::LoadGame:
        if (id >= 0 && id < kSaveSlots) {
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
    if (at_ == Screen::GameMenu || at_ == Screen::SaveGame) {
        if (ev.type == EventType::KeyDown && ev.key == key::Escape) {
            if (at_ == Screen::SaveGame)
                sys_.setTextInput(false);
            go(at_ == Screen::SaveGame ? Screen::GameMenu : Screen::Game);
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
    if (id >= 0)
        onClick(id);
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
    // The CD's track 5 loops alone (in the menus, it seems); tracks 2, 3
    // and 4 follow one another (in the game). See docs/engines/clue.md.
    if (!mixer_ || (music_ != audio::kNoSound && mixer_->isPlaying(music_) && musicInGame_ == inGame))
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
    // storm over them. Their places measured on a recording of the original.
    struct Panel {
        const char* animation;
        int x, y;
    };
    static const Panel kPanels[] = {{"main_anim:Alt1anim", 13, 239}, {"main_anim:Alt2anim", 13, 317},
                                    {"main_anim:Art1anim", 421, 239}, {"main_anim:Art2anim", 396, 317}};
    for (const Panel& p : kPanels) {
        const Animation* a = data_.animation(p.animation);
        if (!a || a->layers.empty() || a->frameCount() == 0)
            continue;
        const size_t f = size_t(now_ * uint64_t(a->framesPerSecond) / 1000) % a->frameCount();
        for (const auto& layer : a->layers) {
            const Animation::Part& part = layer[f];
            if (part.image >= a->images.size())
                continue;
            if (const Image* img = data_.image(a->images[part.image]))
                image::blit(screen_, img->pixels, p.x + part.x + img->offsetX, p.y + part.y + img->offsetY);
        }
    }
}

void FrontEnd::menuThunder() {
    // The storm's thunder now and then (Clue.exe's mnThndr1.wav and
    // mnThndr2.wav); when isn't known, so every 12-30 s.
    if (!mixer_)
        return;
    if (nextThunder_ == 0 || nextThunder_ > now_ + 60000)
        nextThunder_ = now_ + 8000;
    if (now_ < nextThunder_)
        return;
    const char* file = (now_ / 7) % 2 ? "mnThndr1.wav" : "mnThndr2.wav";
    if (const std::filesystem::path* p = data_.find(file)) {
        auto s = std::make_shared<audio::Sound>();
        if (!audio::decodeSound(readFile(*p), *s))
            mixer_->play(s, audio::SoundType::Effects);
    }
    nextThunder_ = now_ + 12000 + (now_ * 2654435761u) % 18000;
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
    // The game's music goes on to its next track: 2, 3, 4, 2 ...
    if (musicInGame_ && mixer_ && music_ != audio::kNoSound && !mixer_->isPlaying(music_) &&
        (at_ == Screen::Game || at_ == Screen::GameMenu || at_ == Screen::SaveGame))
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
    if ((at_ == Screen::Game || at_ == Screen::GameMenu || at_ == Screen::SaveGame) && play_) {
        play_->setLook(playLook());
        if (at_ == Screen::Game)
            play_->update(nowMs);
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
        if ((at_ == Screen::GameMenu || at_ == Screen::SaveGame) && gameMenu_)
            gameMenu_->draw(screen_);
    } else {
        fillRect(screen_, 0, 0, kW, kH, {0, 0, 0});
        if (dialog_) {
            // The storm goes between the menu's background and its controls.
            dialog_->drawBackground(screen_);
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
