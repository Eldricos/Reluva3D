// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// Which button leads where is the game's, as recordings of the original
// show it and the dialogs' own words say (docs/engines/clue/README.md); the
// dialogs, their controls and their numbers are dialogInfo.res's.

#include "engines/clue/ui/frontend.h"

#include "engines/clue/pages/viewers.h"

#include "audio/decode.h"
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
static_assert(kOverGameX == kW / 2 - 231 / 2 && kOverGameY == 5 + (kBarTop - 5 - 395) / 2);
// The Host Game window's row of boxes for the number of players, in its
// picture (menu:Ihostgmbg): covered when the window joins by address, by
// the Exit window's picture (menu:Iexitbgnd), the same circle there
// without them (and the same around them).
constexpr int kCoverX = 218, kCoverY = 269, kCoverW = 203, kCoverH = 29;

// dialogInfo.res's dialogs, by number.
enum Dialog {
    kMainMenu = 1,
    kStartChoice = 2,
    kLoadGame = 3,
    kRulesMenu = 4,
    kRules = 5,
    kCredits = 6,
    kExit = 7,
    kConnection = 9,
    kNetGame = 10,
    kCreditsText = 14,
    kVariants = 15,
    kPicker = 17,
    kSetupFirst = 18,
    kPrefs = 24,
    kLoading = 26,
    kGameMenu = 35,
    kSaveGame = 36,
    kGamePrefs = 37,
    kGameRulesMenu = 38,
    kGameRules = 39,
    kExitConfirm = 44,
};
// Controls most dialogs share.
constexpr int kReturn = 10000, kOk = 10001;

const char *const kLevelName[3] = {"Novice CPU", "Normal CPU", "Expert CPU"};
// The rules' topics, in the How to Play menu's order, and their files.
const char *const kRulesFile[7] = {"Setup.txt",
                                   "Character Movement.txt",
                                   "Entering or Leaving a Room.txt",
                                   "Making a Suggestion.txt",
                                   "Disproving a Suggestion.txt",
                                   "Making an Accusation.txt",
                                   "Game Variants.txt"};
const char *const kPrefKey[Preferences::kSwitches] = {"suggest_movies", "lighting",        "translucency", "auto_notes",
                                                      "butler_voice",   "walk_animations", "mansion_walls"};

std::vector<uint8_t> readFile(const std::filesystem::path &p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// Copies the image's (x, y, w, h) to the same place on the screen.
void copyRect(Surface &s, const image::Image &img, int x, int y, int w, int h) {
    for (int py = std::max(y, 0); py < std::min({y + h, img.h, s.h}); ++py)
        for (int px = std::max(x, 0); px < std::min({x + w, img.w, s.w}); ++px)
            std::memcpy(s.pixels.data() + size_t(py) * size_t(s.pitch) + size_t(px) * 4, img.pixel(px, py), 4);
}

} // namespace

void Preferences::load(const std::filesystem::path &file) {
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

bool Preferences::loadOriginal(const std::vector<uint8_t> &b) {
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

void Preferences::save(const std::filesystem::path &file) const {
    if (file.empty())
        return;
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file);
    for (int k = 0; k < kSwitches; ++k)
        out << kPrefKey[k] << '=' << (on[size_t(k)] ? 1 : 0) << '\n';
    out << "music_volume=" << musicVolume << "\nmaster_volume=" << masterVolume << '\n';
}

FrontEnd::FrontEnd(System &sys, Surface &screen, const GameData &data, audio::Mixer *mixer,
                   std::filesystem::path prefsFile)
    : _sys(sys), _screen(screen), _data(data), _mixer(mixer), _prefsFile(std::move(prefsFile)) {
}

FrontEnd::~FrontEnd() {
    stopMovie();
    stopMusic();
}

Error FrontEnd::open() {
    const std::filesystem::path *resi = _data.find("dialogInfo.res");
    if (!resi)
        return Error::fail("no dialogInfo.res");
    if (Error err = parseResi(readFile(*resi), _resi))
        return Error::fail("dialogInfo.res: " + err.message);
    _fonts = std::make_unique<Fonts>(_data, _resi);
    if (!_prefsFile.empty())
        _prefs.load(_prefsFile);
    // The first time, the original's own preferences, if it has any.
    std::error_code ec;
    if (_prefsFile.empty() || !std::filesystem::exists(_prefsFile, ec))
        if (const std::filesystem::path *original = _data.find("Prefs.ini"))
            _prefs.loadOriginal(readFile(*original));
    applyVolumes();
    _sys.setWindowTitle(gameTitle(_data.language()));
    _sys.setMouseCursorVisible(false);
    go(Screen::kIntro);
    return Error::success();
}

void FrontEnd::leave() {
    if (_mixer && _stormSound != audio::kNoSound)
        _mixer->stop(_stormSound);
    _stormSound = audio::kNoSound;
    if (_play)
        _play->stopSounds();
    stopMovie();
    stopMusic();
    _sys.setMouseCursorVisible(true);
}

void FrontEnd::resume() {
    _sys.setWindowTitle(gameTitle(_data.language()));
    _sys.setMouseCursorVisible(false);
    if (_at == Screen::kGame || overGame())
        playMusic(true);
    else if (_at == Screen::kPicker || _at == Screen::kSetup || _at == Screen::kNetLobby || _at == Screen::kNetSetup ||
             _at == Screen::kPrefs)
        playMusic(false);
    if (_at == Screen::kIntro || _at == Screen::kDeal)
        go(_afterMovie);
}

std::string FrontEnd::readText(const std::string &file) const {
    const std::filesystem::path *p = _data.find(file);
    if (!p)
        return {};
    const std::vector<uint8_t> b = readFile(*p);
    return {b.begin(), b.end()}; // Windows-1252, as the fonts are
}

void FrontEnd::applyVolumes() {
    if (!_mixer)
        return;
    // As Clue.exe's preferences set them (0x415E6D): the master volume
    // Miles' for the sounds and voices (AIL_set_digital_master_volume), the
    // music's the CD's own (AIL_redbook_set_volume), the one not under the
    // other. The videos' sound neither: AviPlayer.cpp hands it to
    // DirectShow's own renderer (AddMediaStream with MSPID_PrimaryAudio and
    // AMMSF_ADDDEFAULTRENDERER) and never sets its volume. So here Effects
    // are all that Miles plays (sounds and voices), Speech the videos.
    const int master = _prefs.masterVolume * audio::kMaxVolume / 100;
    _mixer->setTypeVolume(audio::SoundType::kMusic, _prefs.musicVolume * audio::kMaxVolume / 100);
    _mixer->setTypeVolume(audio::SoundType::kSpeech, audio::kMaxVolume);
    _mixer->setTypeVolume(audio::SoundType::kEffects, master);
}

void FrontEnd::showDialog(int id, int x, int y) {
    _dialog.reset();
    if (const ResiDialog *d = _resi.dialog(id))
        _dialog = std::make_unique<DialogView>(_data, *_fonts, *d, x, y);
    // So that the control under the pointer shows as it.
    Event move;
    move.type = EventType::kMouseMove;
    move.mouseX = _mouseX;
    move.mouseY = _mouseY;
    if (_dialog)
        _dialog->handleEvent(move);
}

void FrontEnd::go(Screen s) {
    _at = s;
    _flashClick = -1; // a new screen hides the lightning (Clue.exe 0x40C420)
    // The screens without the storm stop its thunder: the character picker
    // and the network's player setup as they open (Clue.exe 0x412F30,
    // 0x411CB0), the game as it loads (0x404AC2).
    const bool trackFive =
        s == Screen::kPicker || s == Screen::kSetup || s == Screen::kNetLobby || s == Screen::kNetSetup;
    if (_mixer && _stormSound != audio::kNoSound &&
        (trackFive || s == Screen::kIntro || s == Screen::kLoading || s == Screen::kDeal || s == Screen::kGame)) {
        _mixer->stop(_stormSound);
        _stormSound = audio::kNoSound;
    }
    if (s != Screen::kLoading && _mixer && _loadingSound != audio::kNoSound) {
        _mixer->stop(_loadingSound); // loaded (0x404E44)
        _loadingSound = audio::kNoSound;
    }
    // Those, and Preferences (0x415810), start the CD's track 5 if it
    // isn't playing anything; the menus with the storm have none.
    if (trackFive || s == Screen::kPrefs)
        playMusic(false);
    switch (s) {
    case Screen::kIntro:
        startMovie("Intro.avi", Screen::kMainMenu);
        break;
    case Screen::kMainMenu:
        _mansion.reset();
        showDialog(kMainMenu);
        break;
    case Screen::kStartChoice:
        showDialog(kStartChoice);
        break;
    case Screen::kConnection:
        showDialog(kConnection);
        // The Internet Game Zone is gone: games by address instead.
        _dialog->setText(0, "Internet|by address");
        break;
    case Screen::kNetGame:
        showDialog(kNetGame);
        break;
    case Screen::kNetHost:
    case Screen::kNetJoin:
    case Screen::kNetAddress:
    case Screen::kNetLobby:
    case Screen::kNetSetup:
    case Screen::kNetMessage:
        netShow(s);
        break;
    case Screen::kLoadGame:
        showDialog(kLoadGame);
        // The saved games, by their names; the empty slots light up under
        // the pointer all the same, as the original's do, but do nothing.
        for (int slot = 0; slot < kSaveSlots; ++slot)
            _dialog->setText(slot, savedName(slot));
        break;
    case Screen::kRulesMenu:
        showDialog(kRulesMenu);
        break;
    case Screen::kRules:
        showDialog(kRules);
        _dialog->setVisible(_rulesTopic, true);
        _dialog->setText(11, readText(kRulesFile[_rulesTopic]));
        break;
    case Screen::kCredits:
        showDialog(kCredits);
        break;
    case Screen::kCreditsText:
        break; // shown by the Credits buttons
    case Screen::kExit:
        showDialog(kExit);
        break;
    case Screen::kPicker:
        showDialog(kPicker);
        refreshPicker();
        break;
    case Screen::kSetup:
        showDialog(kSetupFirst + _setupSuspect);
        refreshSetup();
        break;
    case Screen::kVariants:
        showDialog(kVariants);
        _dialog->setChosen(0, !_noDieRoll);
        _dialog->setChosen(1, _noDieRoll);
        break;
    case Screen::kPrefs:
        showDialog(kPrefs);
        refreshPrefs(*_dialog, 0, 28);
        break;
    case Screen::kLoading:
        // Clue.exe 0x404AC2: the CD stopped, the loading's own loop.
        stopMusic();
        if (_mixer && _loadingSound == audio::kNoSound)
            if (const std::filesystem::path *p = _data.find("load music loop22khz.wav")) {
                auto loop = std::make_shared<audio::Sound>();
                if (!audio::decodeSound(readFile(*p), *loop))
                    _loadingSound = _mixer->play(loop, audio::SoundType::kEffects, kClueSoundVolume, 0, true);
            }
        showDialog(kLoading);
        break;
    case Screen::kDeal:
        startMovie("deal.avi", Screen::kGame);
        break;
    case Screen::kGame: {
        bool started = false;
        _dialog.reset();
        _gameMenu.reset();
        if (!_play && _mansion && _loadSlot >= 0) {
            // A saved game, in place of a new one.
            const int slot = _loadSlot;
            _loadSlot = -1;
            started = true;
            _play = std::make_unique<Play>(_sys, _screen, _data, _mixer, _resi, *_fonts, *_mansion, _level);
            _play->setLook(playLook());
            if (loadGame(slot)) {
                _play.reset();
                go(Screen::kMainMenu);
                return;
            }
        }
        if (!_play && _mansion) {
            started = true;
            _play = std::make_unique<Play>(_sys, _screen, _data, _mixer, _resi, *_fonts, *_mansion, _level);
            _play->setLook(playLook());
            const Error err = _net ? _play->startNetwork(*_net)
                                   : _play->start(_players, _noDieRoll, uint32_t(_sys.getMillis() * 2654435761u));
            if (err) {
                _play.reset();
                go(Screen::kMainMenu);
                return;
            }
        }
        // A game starting, new or saved, starts on track 2 (0x404E53); back
        // from the Esc menu, the music is as it was.
        if (started) {
            _musicInGame = true;
            playTrack(2);
        }
        break;
    }
    case Screen::kGameMenu:
        if (const ResiDialog *d = _resi.dialog(kGameMenu)) {
            // Over the game, in the middle of the view.
            _gameMenu = std::make_unique<DialogView>(_data, *_fonts, *d, kOverGameX, kOverGameY);
            // Save: when it's a human's turn and nothing is going on.
            _gameMenu->setEnabled(0, _play && _play->canSave() && !savesFolder().empty());
        }
        break;
    case Screen::kExitConfirm:
        // "Are you sure you want to exit the current game?" Yes, No.
        if (const ResiDialog *d = _resi.dialog(kExitConfirm)) {
            _gameMenu = std::make_unique<DialogView>(_data, *_fonts, *d, kOverGameX, kOverGameY);
            _gameMenu->setVisible(1, false); // its other question: "Would you like to save ...?"
        }
        break;
    // The Esc menu's Preferences and How to Play: windows of their own over
    // the game, where the original has them (dialogs 37-39; recordings show
    // them there), not the menus' screens.
    case Screen::kGamePrefs:
        if (const ResiDialog *d = _resi.dialog(kGamePrefs)) {
            _gameMenu = std::make_unique<DialogView>(_data, *_fonts, *d, kOverGameX, kOverGameY);
            refreshPrefs(*_gameMenu, 1, 29);
        }
        break;
    case Screen::kGameRulesMenu:
        if (const ResiDialog *d = _resi.dialog(kGameRulesMenu))
            _gameMenu = std::make_unique<DialogView>(_data, *_fonts, *d, kOverGameX, kOverGameY);
        break;
    case Screen::kGameRules:
        if (const ResiDialog *d = _resi.dialog(kGameRules)) {
            _gameMenu = std::make_unique<DialogView>(_data, *_fonts, *d, kOverGameX, kOverGameY);
            _gameMenu->setText(3, readText(kRulesFile[_rulesTopic]));
        }
        break;
    case Screen::kSaveGame:
        if (const ResiDialog *d = _resi.dialog(kSaveGame)) {
            _gameMenu = std::make_unique<DialogView>(_data, *_fonts, *d, kOverGameX, kOverGameY);
            _saveSlot = -1;
            for (int slot = 0; slot < kSaveSlots; ++slot)
                _gameMenu->setText(2 + slot, savedName(slot));
            _gameMenu->setEnabled(1, false);
        }
        break;
    }
}

void FrontEnd::refreshPicker() {
    int count = 0;
    for (const Player &p : _players)
        count += p.kind != Player::kNone;
    for (int k = 0; k < kSuspects; ++k) {
        const Player &p = _players[size_t(k)];
        const bool taken = p.kind != Player::kNone;
        _dialog->setVisible(5 + k, !taken);
        _dialog->setVisible(11 + k, taken);
        _dialog->setVisible(23 + k, taken);
        if (taken)
            _dialog->setText(23 + k, p.kind == Player::kHuman ? "is|" + (p.name.empty() ? "Human" : p.name)
                                                              : std::string("is|") + kLevelName[p.level]);
    }
    _dialog->setVisible(0, count == 0);
    _dialog->setVisible(1, count > 0);
    _dialog->setVisible(2, count < 3);
    _dialog->setVisible(3, count < 3);
    _dialog->setVisible(4, count >= 3);
    _dialog->setEnabled(29, count >= 3);
}

void FrontEnd::refreshSetup() {
    const Player &p = _setupDraft;
    _dialog->setChosen(0, p.kind == Player::kHuman);
    _dialog->setChosen(1, p.kind == Player::kComputer);
    _dialog->setChosen(2, p.kind == Player::kNone);
    for (int id : {3, 4, 5, 6, 7})
        _dialog->setVisible(id, p.kind == Player::kComputer);
    for (int level = 0; level < 3; ++level)
        _dialog->setChosen(5 + level, p.level == level);
    for (int id : {8, 9, 10})
        _dialog->setVisible(id, p.kind == Player::kHuman);
    if (p.kind == Player::kHuman) {
        _dialog->setText(10, p.name);
        _dialog->focus(10);
        _sys.setTextInput(true);
    } else {
        _dialog->focus(-1);
        _sys.setTextInput(false);
    }
    int count = 0;
    for (int k = 0; k < kSuspects; ++k)
        count += (k == _setupSuspect ? p.kind : _players[size_t(k)].kind) != Player::kNone;
    _dialog->setEnabled(12, count >= 3);
}

void FrontEnd::refreshPrefs(DialogView &view, int first, int musicId) {
    // Each setting has two radio buttons, on and off, each shown lit or not
    // by a pair of images in the same place: first + 4k on (lit, unlit),
    // first + 4k + 2 off (lit, unlit). The menus' window numbers them from
    // 0, the one over the game (dialog 37) from 1.
    for (int k = 0; k < Preferences::kSwitches; ++k) {
        const bool on = _prefs.on[size_t(k)];
        const int b = first + 4 * k;
        view.setVisible(b, on);
        view.setVisible(b + 1, !on);
        view.setVisible(b + 2, !on);
        view.setVisible(b + 3, on);
    }
    view.setValue(musicId, _prefs.musicVolume);
    view.setValue(musicId + 1, _prefs.masterVolume);
}

void FrontEnd::scrollText(DialogView &view, int textId, int knobId, int lines) {
    view.setScroll(textId, view.scroll(textId) + lines);
    const int most = std::max(1, view.lineCount(textId) - view.visibleLines(textId));
    view.setValue(knobId, view.scroll(textId) * 100 / most);
}

void FrontEnd::startGame() {
    for (Player &p : _players)
        if (p.kind == Player::kHuman && p.name.empty())
            p.name = "Human";
    _play.reset();
    go(Screen::kLoading);
}

PlayLook FrontEnd::playLook() const {
    PlayLook look;
    look.suggestMovies = _prefs.on[Preferences::kSuggestMovies];
    look.autoNotes = _prefs.on[Preferences::kAutoNotes];
    look.butlerVoice = _prefs.on[Preferences::kButlerVoice];
    look.walkAnimations = _prefs.on[Preferences::kWalkAnimations];
    look.mansion.lighting = _prefs.on[Preferences::kLighting];
    // As a recording of the original shows them (docs/engines/clue/systems/playing.md):
    // "Mansion Walls" off, no walls; "Translucency": what stands in front of
    // the suspect whose turn it is see-through, or (off) left out.
    look.mansion.walls = _prefs.on[Preferences::kMansionWalls];
    look.mansion.inFront =
        _prefs.on[Preferences::kTranslucency] ? MansionLook::InFront::kSeeThrough : MansionLook::InFront::kHidden;
    return look;
}

void FrontEnd::onClick(int id) {
    switch (_at) {
    case Screen::kMainMenu:
        if (id == 1)
            go(Screen::kStartChoice);
        else if (id == 3)
            go(Screen::kLoadGame);
        else if (id == 2) {
            _prefsReturn = Screen::kMainMenu;
            go(Screen::kPrefs);
        } else if (id == 0) {
            _rulesReturn = Screen::kMainMenu;
            go(Screen::kRulesMenu);
        } else if (id == 5)
            go(Screen::kCredits);
        else if (id == 7)
            go(Screen::kExit);
        break;
    case Screen::kStartChoice:
        if (id == 0)
            go(Screen::kPicker);
        else if (id == 1)
            go(Screen::kConnection);
        else if (id == kReturn)
            go(Screen::kMainMenu);
        break;
    case Screen::kConnection:
        if (id == kReturn)
            go(Screen::kStartChoice);
        else
            netClick(id);
        break;
    case Screen::kRulesMenu:
        if (id >= 0 && id <= 6) {
            _rulesTopic = id;
            go(Screen::kRules);
        } else if (id == kReturn)
            go(_rulesReturn == Screen::kGameMenu ? Screen::kGame : Screen::kMainMenu);
        break;
    case Screen::kRules:
        if (id == 8)
            scrollText(*_dialog, 11, 10, -1);
        else if (id == 9)
            scrollText(*_dialog, 11, 10, 1);
        else if (id == 10) {
            const int most = std::max(0, _dialog->lineCount(11) - _dialog->visibleLines(11));
            _dialog->setScroll(11, _dialog->value(10) * most / 100);
        } else if (id == 7)
            go(Screen::kRulesMenu);
        else if (id == kReturn)
            go(_rulesReturn == Screen::kGameMenu ? Screen::kGame : Screen::kMainMenu);
        break;
    case Screen::kCredits:
        if (id == 0 || id == 1) {
            showDialog(kCreditsText);
            _dialog->setText(3, readText(id == 0 ? "clue Credits.txt" : "EAI_credits.txt"));
            _at = Screen::kCreditsText;
        } else if (id == kReturn)
            go(Screen::kMainMenu);
        break;
    case Screen::kCreditsText:
        if (id == 0)
            scrollText(*_dialog, 3, 2, -1);
        else if (id == 1)
            scrollText(*_dialog, 3, 2, 1);
        else if (id == 2) {
            const int most = std::max(0, _dialog->lineCount(3) - _dialog->visibleLines(3));
            _dialog->setScroll(3, _dialog->value(2) * most / 100);
        } else if (id == kReturn)
            go(Screen::kCredits);
        break;
    case Screen::kExit:
        if (id == kOk)
            _quit = true;
        else if (id == kReturn || id == 0)
            go(Screen::kMainMenu);
        break;
    case Screen::kPicker:
        if (id >= 5 && id <= 16) {
            _setupSuspect = (id - 5) % kSuspects;
            _setupDraft = _players[size_t(_setupSuspect)];
            go(Screen::kSetup);
        } else if (id == 29)
            startGame();
        else if (id == 30)
            go(Screen::kMainMenu);
        break;
    case Screen::kSetup:
        if (_dialog && _setupDraft.kind == Player::kHuman)
            _setupDraft.name = _dialog->text(10);
        if (id == 0)
            _setupDraft.kind = Player::kHuman;
        else if (id == 1)
            _setupDraft.kind = Player::kComputer;
        else if (id == 2)
            _setupDraft.kind = Player::kNone;
        else if (id >= 5 && id <= 7)
            _setupDraft.level = id - 5;
        if (id <= 7) {
            refreshSetup();
            break;
        }
        _sys.setTextInput(false);
        if (id == kOk || id == 12)
            _players[size_t(_setupSuspect)] = _setupDraft;
        if (id == kOk || id == kReturn)
            go(Screen::kPicker);
        else if (id == 11)
            go(Screen::kVariants);
        else if (id == 12)
            startGame();
        break;
    case Screen::kVariants:
        if (id == 0 || id == 1)
            _noDieRoll = id == 1;
        if (id == 0 || id == 1 || id == kReturn)
            go(Screen::kSetup);
        break;
    case Screen::kPrefs:
        if (id >= 0 && id < 4 * Preferences::kSwitches) {
            // The on column's buttons are 4k and 4k + 1, the off column's 4k + 2 and 4k + 3.
            _prefs.on[size_t(id / 4)] = id % 4 < 2;
            refreshPrefs(*_dialog, 0, 28);
        } else if (id == 28 || id == 29) {
            (id == 28 ? _prefs.musicVolume : _prefs.masterVolume) = _dialog->value(id);
            applyVolumes();
        } else if (id == kReturn) {
            // Clue.exe 0x41650C: leaving stops the CD, in the menus or the
            // game alike.
            stopMusic();
            _prefs.save(_prefsFile);
            go(_prefsReturn == Screen::kGameMenu ? Screen::kGame : Screen::kMainMenu);
        }
        break;
    case Screen::kGameMenu:
        if (id == 4)
            go(Screen::kGame);
        else if (id == 0)
            go(Screen::kSaveGame);
        else if (id == 3)
            go(Screen::kExitConfirm);
        else if (id == 1)
            go(Screen::kGamePrefs);
        else if (id == 2)
            go(Screen::kGameRulesMenu);
        break;
    case Screen::kGamePrefs:
        // As the menus' (Clue.exe 0x43F0B0 sets the volumes the same way),
        // but leaving it doesn't touch the music; Continue, back to the game.
        if (id >= 1 && id <= 4 * Preferences::kSwitches) {
            _prefs.on[size_t((id - 1) / 4)] = (id - 1) % 4 < 2;
            refreshPrefs(*_gameMenu, 1, 29);
            if (_play)
                _play->setLook(playLook());
        } else if (id == 29 || id == 30) {
            (id == 29 ? _prefs.musicVolume : _prefs.masterVolume) = _gameMenu->value(id);
            applyVolumes();
        } else if (id == 0) {
            _prefs.save(_prefsFile);
            go(Screen::kGame);
        }
        break;
    case Screen::kGameRulesMenu:
        if (id >= 0 && id <= 6) {
            _rulesTopic = id;
            go(Screen::kGameRules);
        } else if (id == 7)
            go(Screen::kGame);
        break;
    case Screen::kGameRules:
        if (id == 0)
            scrollText(*_gameMenu, 3, 2, -1);
        else if (id == 1)
            scrollText(*_gameMenu, 3, 2, 1);
        else if (id == 2) {
            const int most = std::max(0, _gameMenu->lineCount(3) - _gameMenu->visibleLines(3));
            _gameMenu->setScroll(3, _gameMenu->value(2) * most / 100);
        } else if (id == 5)
            go(Screen::kGameRulesMenu);
        else if (id == 4)
            go(Screen::kGame);
        break;
    case Screen::kExitConfirm:
        if (id == kOk) {
            stopMusic();
            if (_play)
                _play->stopSounds();
            _play.reset();
            if (_net)
                netLeave();
            else
                go(Screen::kMainMenu);
        } else if (id == kReturn)
            go(Screen::kGameMenu);
        break;
    case Screen::kSaveGame:
        if (id >= 2 && id < 2 + kSaveSlots) {
            // A slot chosen: its name to type, over its button.
            if (_saveSlot >= 0) {
                _gameMenu->setVisible(10 + _saveSlot, false);
                _gameMenu->setVisible(2 + _saveSlot, true);
            }
            _saveSlot = id - 2;
            // Its name so far, to type over; an empty slot's to type afresh.
            const std::string name = savedName(_saveSlot);
            _gameMenu->setVisible(id, false);
            _gameMenu->setVisible(10 + _saveSlot, true);
            _gameMenu->setText(10 + _saveSlot, name);
            _gameMenu->focus(10 + _saveSlot);
            _gameMenu->setEnabled(1, true);
            _sys.setTextInput(true);
        } else if (id == 1 && _saveSlot >= 0 && _play && _play->canSave()) {
            _sys.setTextInput(false);
            saveGame(_saveSlot, _gameMenu->text(10 + _saveSlot));
            go(Screen::kGame);
        } else if (id == 0) {
            _sys.setTextInput(false);
            go(Screen::kGameMenu);
        }
        break;
    case Screen::kLoadGame:
        if (id >= 0 && id < kSaveSlots && !savedName(id).empty()) {
            _loadSlot = id;
            _play.reset();
            go(Screen::kLoading);
        } else if (id == kReturn)
            go(Screen::kMainMenu);
        break;
    default:
        netClick(id);
        break;
    }
}

std::filesystem::path FrontEnd::savesFolder() const {
    return _prefsFile.empty() ? std::filesystem::path() : _prefsFile.parent_path() / "saves";
}

std::filesystem::path FrontEnd::saveFile(int slot) const {
    return savesFolder() / ("slot" + std::to_string(slot + 1) + ".sav");
}

std::string FrontEnd::savedName(int slot) const {
    if (savesFolder().empty())
        return {};
    std::ifstream in(saveFile(slot), std::ios::binary);
    std::string tag;
    std::string name;
    int version = 0;
    if (!(in >> tag >> version) || tag != "reluva-clue-save" || in.get() != '\n' || !saveio::getString(in, name))
        return {};
    return name.empty() ? "Game " + std::to_string(slot + 1) : name;
}

Error FrontEnd::saveGame(int slot, const std::string &name) {
    if (!_play || savesFolder().empty())
        return Error::fail("nothing to save");
    std::error_code ec;
    std::filesystem::create_directories(savesFolder(), ec);
    // Written whole beside it, then put in its place.
    const std::filesystem::path file = saveFile(slot);
    const std::filesystem::path part = file.string() + ".part";
    {
        std::ofstream out(part, std::ios::binary);
        out << "reluva-clue-save 1\n";
        saveio::putString(out, name);
        out << '\n';
        _play->save(out);
        if (!out)
            return Error::fail("couldn't write " + part.string());
    }
    std::filesystem::rename(part, file, ec);
    return ec ? Error::fail("couldn't write " + file.string()) : Error::success();
}

Error FrontEnd::loadGame(int slot) {
    std::ifstream in(saveFile(slot), std::ios::binary);
    std::string tag;
    std::string name;
    int version = 0;
    if (!(in >> tag >> version) || tag != "reluva-clue-save" || version != 1 || in.get() != '\n' ||
        !saveio::getString(in, name))
        return Error::fail("not a saved game");
    return _play->load(in);
}

void FrontEnd::handleEvent(const Event &ev) {
    if (ev.type == EventType::kMouseMove || ev.type == EventType::kMouseDown || ev.type == EventType::kMouseUp) {
        _mouseX = ev.mouseX;
        _mouseY = ev.mouseY;
    }
    // Videos: a key or a click skips them.
    if (_movie) {
        if (ev.type == EventType::kKeyDown || ev.type == EventType::kMouseDown) {
            const Screen next = _afterMovie;
            stopMovie();
            go(next);
        }
        return;
    }
    if (_at == Screen::kGame) {
        if (ev.type == EventType::kKeyDown && ev.key == key::kEscape)
            go(Screen::kGameMenu);
        else if (_play)
            _play->handleEvent(ev);
        return;
    }
    if (overGame()) {
        if (ev.type == EventType::kKeyDown && ev.key == key::kEscape) {
            if (_at == Screen::kSaveGame)
                _sys.setTextInput(false);
            go(_at == Screen::kGameMenu ? Screen::kGame : Screen::kGameMenu);
            return;
        }
        if (ev.type == EventType::kKeyDown && ev.key == key::kReturn && _at == Screen::kSaveGame) {
            onClick(1);
            return;
        }
        if (_gameMenu) {
            const int id = _gameMenu->handleEvent(ev);
            if (id >= 0)
                onClick(id);
        }
        return;
    }
    if (_flashClick >= 0) {
        // The lightning's second keeps the screen; what is pressed meanwhile
        // waits, as Windows queues it, and is handled afterwards.
        _heldEvents.push_back(ev);
        return;
    }
    if (!_dialog)
        return;
    // The network screens: Esc goes back (a lobby is left), Enter sends.
    const bool netScreen = _at == Screen::kNetHost || _at == Screen::kNetAddress || _at == Screen::kNetJoin ||
                           _at == Screen::kNetLobby || _at == Screen::kNetSetup || _at == Screen::kNetMessage;
    if (netScreen && ev.type == EventType::kKeyDown && (ev.key == key::kEscape || ev.key == key::kReturn)) {
        const bool esc = ev.key == key::kEscape;
        if (_at == Screen::kNetLobby)
            esc ? netLeave() : netClick(60);
        else if (_at == Screen::kNetMessage)
            netClick(kOk);
        else if (_at == Screen::kNetHost || _at == Screen::kNetAddress)
            netClick(esc ? kReturn : 2);
        else if (esc)
            netClick(kReturn);
        return;
    }
    // Esc as each screen's way back.
    if (ev.type == EventType::kKeyDown && ev.key == key::kEscape) {
        if (_at == Screen::kMainMenu)
            go(Screen::kExit);
        else if (_at == Screen::kSetup || _at == Screen::kExit || _at == Screen::kVariants || _at == Screen::kPrefs ||
                 _at == Screen::kCredits || _at == Screen::kCreditsText || _at == Screen::kRulesMenu ||
                 _at == Screen::kRules || _at == Screen::kStartChoice || _at == Screen::kConnection ||
                 _at == Screen::kNetGame || _at == Screen::kLoadGame)
            onClick(_at == Screen::kExit ? 0 : kReturn);
        else if (_at == Screen::kPicker)
            onClick(30);
        return;
    }
    if (ev.type == EventType::kKeyDown && ev.key == key::kReturn && _at == Screen::kSetup) {
        onClick(kOk);
        return;
    }
    const int id = _dialog->handleEvent(ev);
    if (id >= 0) {
        if (stormScreen())
            startFlash(id);
        else
            onClick(id);
    }
}

void FrontEnd::startMovie(const std::string &file, Screen after) {
    stopMovie();
    stopMusic();
    _afterMovie = after;
    const std::filesystem::path *p = _data.find(file);
    auto player = std::make_unique<Movie>();
    if (!p || player->open(readFile(*p))) {
        go(after); // without the video, on to what follows it
        return;
    }
    if (_mixer && player->hasAudio()) {
        auto sound = std::make_shared<audio::Sound>();
        if (!player->audio(*sound))
            _movieSound = _mixer->play(sound, audio::SoundType::kSpeech);
    }
    _movie = std::move(player);
    _movieStart = _sys.getMillis();
}

void FrontEnd::stopMovie() {
    if (_mixer && _movieSound != audio::kNoSound)
        _mixer->stop(_movieSound);
    _movieSound = audio::kNoSound;
    _movie.reset();
}

void FrontEnd::playMusic(bool inGame) {
    // The CD's track 5 loops alone (the picker, the player setup,
    // Preferences); tracks 2, 3 and 4 follow one another (the game). Either
    // only if the CD isn't playing already (Clue.exe 0x42F120). See
    // docs/engines/clue/systems/music.md.
    if (!_mixer || (_music != audio::kNoSound && _mixer->isPlaying(_music)))
        return;
    _musicInGame = inGame;
    playTrack(inGame ? 2 : 5);
}

void FrontEnd::playTrack(int track) {
    stopMusic();
    _musicTrack = track;
    auto it = _tracks.find(track);
    if (it == _tracks.end()) {
        // Ripped from the CD as trackNN.wav or .mp3 (reluva-tool cue-rip);
        // without them the game is silent, as the original without its CD.
        std::shared_ptr<audio::Sound> s;
        for (const char *ext : {".wav", ".mp3"}) {
            char name[16];
            std::snprintf(name, sizeof name, "track%02d%s", track, ext);
            if (const std::filesystem::path *p = _data.find(name)) {
                auto decoded = std::make_shared<audio::Sound>();
                if (!audio::decodeSound(readFile(*p), *decoded)) {
                    s = decoded;
                    break;
                }
            }
        }
        it = _tracks.emplace(track, s).first;
    }
    if (it->second)
        _music = _mixer->play(it->second, audio::SoundType::kMusic, audio::kMaxVolume, 0, !_musicInGame);
}

void FrontEnd::stopMusic() {
    if (_mixer && _music != audio::kNoSound)
        _mixer->stop(_music);
    _music = audio::kNoSound;
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
    for (const Panel &p : kPanels)
        for (const char *name : {p.animation, _flashClick >= 0 ? p.flash : nullptr}) {
            const Animation *a = name ? _data.animation(name) : nullptr;
            if (!a || a->layers.empty() || a->frameCount() == 0)
                continue;
            const size_t f = a->frameAt(_now, false);
            for (const auto &layer : a->layers) {
                const Animation::Part &part = layer[f];
                if (part.image >= a->images.size())
                    continue;
                if (const Image *img = _data.image(a->images[part.image]))
                    image::blit(_screen, img->pixels, p.x + part.x + img->offsetX, p.y + part.y + img->offsetY);
            }
        }
}

bool FrontEnd::stormScreen() const {
    return _dialog && !overGame() && _at != Screen::kGame && _at != Screen::kLoading && _at != Screen::kPicker &&
           _at != Screen::kSetup && _at != Screen::kNetLobby && _at != Screen::kNetSetup && _at != Screen::kNetMessage;
}

namespace {
std::shared_ptr<audio::Sound> loadSound(const GameData &data, const char *file) {
    const std::filesystem::path *p = data.find(file);
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
    if (!_mixer || (_stormSound != audio::kNoSound && _mixer->isPlaying(_stormSound)))
        return;
    if (auto s = loadSound(_data, "mnThndr1.wav"))
        _stormSound = _mixer->play(s, audio::SoundType::kEffects, kClueSoundVolume, 0, true);
}

void FrontEnd::startFlash(int id) {
    // Clue.exe 0x40C350, the storm screens' handler of a click (notify
    // code 0): mnThndr2.wav (sound 3) once, the four flash pictures
    // (controls 1004-1007) shown, 500 ms with the screen still and 500 ms
    // more with it going on; then the click is acted on, and the screen it
    // opens hides the flashes (0x40C420).
    if (_mixer)
        if (auto s = loadSound(_data, "mnThndr2.wav"))
            _mixer->play(s, audio::SoundType::kEffects, kClueSoundVolume);
    _flashUntil = _now + 1000;
    _flashClick = id;
}

void FrontEnd::drawCursor() {
    // The game's pointer, a gloved hand pointing up and to the left.
    const Animation *a = _data.animation("cursors:Apointcrsr");
    if (!a || a->layers.empty() || a->layers[0].empty() || a->layers[0][0].image >= a->images.size())
        return;
    const Image *img = _data.image(a->images[a->layers[0][0].image]);
    if (!img)
        return;
    // The pointer's images fill their 32x32 square, colour 0 (black) around
    // the hand: a colour key, as the recordings show no square. Elsewhere
    // colour 0 is drawn black (the piano in a recording's frame is).
    const image::Image &p = img->pixels;
    for (int y = 0; y < p.h; ++y)
        for (int x = 0; x < p.w; ++x) {
            const uint8_t *c = p.pixel(x, y);
            const int sx = _mouseX + x;
            const int sy = _mouseY + y;
            if (c[3] == 0 || (c[0] | c[1] | c[2]) == 0 || sx < 0 || sy < 0 || sx >= kW || sy >= kH)
                continue;
            uint8_t *d = _screen.row(sy) + sx * 4;
            d[0] = c[0];
            d[1] = c[1];
            d[2] = c[2];
        }
}

void FrontEnd::update(uint64_t nowMs) {
    _now = nowMs;
    netUpdate(); // the network's, whatever is on the screen
    if (_movie) {
        const double t = double(nowMs - _movieStart) / 1000.0;
        const bool soundOn = _mixer && _movieSound != audio::kNoSound && _mixer->isPlaying(_movieSound);
        if (_movie->seek(t) || (_movie->finished(t) && !soundOn)) {
            const Screen next = _afterMovie;
            stopMovie();
            go(next);
        } else {
            // Twice the size: 320x240 fills the screen.
            const image::Image &pic = _movie->picture();
            const int scale = std::max(1, std::min(kW / std::max(1, pic.w), kH / std::max(1, pic.h)));
            fillRect(_screen, 0, 0, kW, kH, {0, 0, 0});
            const int x0 = (kW - pic.w * scale) / 2;
            const int y0 = (kH - pic.h * scale) / 2;
            for (int y = 0; y < pic.h * scale; ++y) {
                uint8_t *d = _screen.row(y0 + y) + x0 * 4;
                for (int x = 0; x < pic.w * scale; ++x, d += 4) {
                    const uint8_t *p = pic.pixel(x / scale, y / scale);
                    d[0] = p[0];
                    d[1] = p[1];
                    d[2] = p[2];
                    d[3] = 255;
                }
            }
            return;
        }
    }
    if (_flashClick >= 0 && _now >= _flashUntil) {
        const int id = _flashClick;
        _flashClick = -1;
        onClick(id);
        std::vector<Event> held;
        held.swap(_heldEvents);
        for (size_t i = 0; i < held.size(); ++i) {
            handleEvent(held[i]);
            if (_flashClick >= 0) { // another flash: the rest wait for it
                _heldEvents.insert(_heldEvents.begin(), held.begin() + i + 1, held.end());
                break;
            }
        }
    }
    if (_play)
        if (const int cue = _play->takeMusicCue()) {
            if (cue < 0)
                stopMusic();
            else {
                _musicInGame = true;
                playTrack(cue);
            }
        }
    // The game's music goes on to its next track: 2, 3, 4, 2 ...
    if (_musicInGame && _mixer && _music != audio::kNoSound && !_mixer->isPlaying(_music) &&
        (_at == Screen::kGame || overGame()))
        playTrack(_musicTrack == 4 ? 2 : _musicTrack + 1);
    if (_at == Screen::kLoading) {
        // Shown while the mansion is put together, then the deal.
        fillRect(_screen, 0, 0, kW, kH, {0, 0, 0});
        if (_dialog)
            _dialog->draw(_screen);
        _sys.updateScreen(_screen);
        if (!_mansion) {
            _mansion = std::make_unique<Mansion>();
            const std::filesystem::path *lvl = _data.find("clue.lvl");
            if (!lvl || _levelFile.open(*lvl) || parseLevel(_levelFile, _level) || _mansion->build(_data, _level)) {
                _mansion.reset();
                go(Screen::kMainMenu);
                return;
            }
        }
        // A saved game goes straight on; a new one is dealt first.
        go(_loadSlot >= 0 ? Screen::kGame : Screen::kDeal);
        return;
    }
    // The game goes on behind the Esc menu and what it opens, as the
    // original's does (its computer players play on; recordings show it).
    const bool behind = overGame() || (_prefsReturn == Screen::kGameMenu && _at == Screen::kPrefs) ||
                        (_rulesReturn == Screen::kGameMenu && (_at == Screen::kRulesMenu || _at == Screen::kRules));
    if (_play && (_at == Screen::kGame || behind)) {
        _play->setLook(playLook());
        _play->update(nowMs);
    }
    // Save as the game stands now (it goes on behind the menu).
    if (_at == Screen::kGameMenu && _gameMenu && _play)
        _gameMenu->setEnabled(0, _play->canSave() && !savesFolder().empty());
    if ((_at == Screen::kGame || overGame()) && _play) {
        if (_play->finished()) {
            // The game over: back to the main menu, as the original.
            _play->stopSounds();
            _play.reset();
            if (_net)
                netLeave();
            else
                go(Screen::kMainMenu);
            drawCursor();
            return;
        }
        _play->draw();
        if (overGame() && _gameMenu)
            _gameMenu->draw(_screen);
    } else {
        fillRect(_screen, 0, 0, kW, kH, {0, 0, 0});
        if (_dialog) {
            // The storm goes between the menu's background and its controls.
            _dialog->drawBackground(_screen);
            if (_at == Screen::kNetAddress)
                if (const Image *cover = _data.image("menu:Iexitbgnd"))
                    copyRect(_screen, cover->pixels, kCoverX, kCoverY, kCoverW, kCoverH);
            if (_at != Screen::kPicker && _at != Screen::kSetup && _at != Screen::kNetLobby &&
                _at != Screen::kNetSetup && _at != Screen::kNetMessage) {
                drawStorm();
                menuThunder();
            }
            _dialog->drawControls(_screen);
        }
    }
    drawCursor();
}

} // namespace reluva::clue
