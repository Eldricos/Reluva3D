// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/main_menu.h"

#include "audio/decode.h"
#include "common/log.h"
#include "engines/stawayteam/ui/layout.h"
#include "engines/stawayteam/ui/movies.h"
#include "graphics/cursor.h"
#include "video/movie_player.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

namespace reluva::stawayteam {

/** A movie or a picture of the screen, and where it goes. */
struct MainMenu::Part {
    int x = 0;
    int y = 0;
    std::vector<uint8_t> movieData; // kept to start the movie again
    std::unique_ptr<video::MoviePlayer> movie;
    image::Image picture;
};

namespace {

const char *const kPointer = "Cache/Interface/Cursors/Pointer.seq16";

// The main menu's buttons, top to bottom, as the program names them
// (0x503657): Rules.txt only places them.
constexpr std::array<const char *, 6> kMainButtons = {"&START NEW GAME", "&LOAD SAVE GAME", "PLAY &NETWORK GAME",
                                                      "S&ETTINGS",       "&CREDITS",        "&QUIT"};
constexpr size_t kStartButton = 0;
constexpr size_t kLoadButton = 1;
constexpr size_t kNetworkButton = 2;
constexpr size_t kSettingsButton = 3;
constexpr size_t kCreditsButton = 4;
constexpr size_t kQuitButton = 5;
// The program's, not Rules.txt's Menu Font (0x50359a).
const char *const kMainFont = "Placar Condensed";
const char *const kMainButtonSound = "Interface/Main Screen Button.wav"; // 0x5036b9
// Reluva's own, as the countdown runs out: the missions themselves aren't
// played over the network yet.
const char *const kNoNetMissions = "Reluva can't play missions over the network yet.";
const char *const kQuitQuestion = "Are you sure you want to exit the game?"; // 0x640058
// The dialog's question is in this font, whatever Rules.txt's Menu Font
// says (0x4872b5).
const char *const kQuestionFont = "Placar Blue";

} // namespace

ConfirmDialog::ConfirmDialog(const Archive &data, const TextObject &rules, std::string question, int screenW,
                             int screenH)
    : _background(screenPicture(data, "Confirm Dialog/Background")), _question(std::move(question)),
      _x((screenW - _background.w) / 2), _y((screenH - _background.h) / 2) {
    const TextObject *def = menuDefinition(rules, "Confirm Dialog");
    int unused = 0;
    readPoint(def, "TextCenter Pos", unused, _questionY);
    (void)_questionFont.load(data, kQuestionFont);
    const std::array<const char *, 2> names = {"Yes", "No"};
    for (size_t i = 0; i < names.size(); ++i) {
        const std::string key = std::string(names[i]) + "Button";
        int x = 0;
        int y = 0;
        int textX = 0;
        int textY = 0;
        readPoint(def, key + " Pos", x, y);
        readPoint(def, key + " Text Pos", textX, textY);
        (void)_buttonFonts[i].load(data, readText(def, key + " Font"));
        Button button(data, "Confirm Dialog", names[i], x, y);
        button.setLabel(readText(def, key + " Text"), &_buttonFonts[i], textX, textY);
        button.setSound(gameSound(data, readText(def, key + " Press Sound")));
        _buttons.add(std::move(button));
    }
    _buttons.setOrigin(_x, _y);
}

std::optional<bool> ConfirmDialog::handleEvent(const Event &ev, audio::Mixer *mixer) {
    // With Yes and No, No is the dialog's default and cancel button
    // (0x4871dd).
    const int chosen = isEnter(ev) || isEscape(ev) ? _buttons.press(1, mixer) : _buttons.handleEvent(ev, mixer);
    if (chosen < 0)
        return std::nullopt;
    return chosen == 0;
}

void ConfirmDialog::draw(Surface &screen) const {
    // The question across the middle of the dialog, inside its frame (0x487304).
    image::blit(screen, _background, _x, _y);
    if (_questionFont.loaded())
        _questionFont.drawWrapped(screen, _x + _background.w / 2, _y + _questionY, _background.w - 128, _question,
                                  Font::Align::kCenter, 2);
    _buttons.draw(screen);
}

MainMenu::MainMenu(System &sys, Surface &screen, const Archive &data, const std::filesystem::path &install,
                   const TextObject &rules, GameOptions &options, audio::Mixer *mixer, int firstMission,
                   std::filesystem::path saves)
    : _sys(sys), _screen(screen), _data(data), _install(install), _rules(rules), _options(options), _mixer(mixer),
      _firstMission(firstMission), _saves(std::move(saves)) {
    (void)_pointer.loadFile(data, kPointer);
    const TextObject *menu = menuDefinition(rules, "Main Menu");
    const TextObject *nernies = menu ? menu->object("Nernie") : nullptr;
    for (const TextField *item : nernies ? nernies->items() : std::vector<const TextField *>{}) {
        if (!item->object)
            continue;
        const TextObject &nernie = *item->object;
        auto part = std::make_unique<Part>();
        readPoint(&nernie, "Position", part->x, part->y);
        const std::string at = " at " + std::to_string(part->x) + "," + std::to_string(part->y);
        if (nernie.className == "CMovieNernie") {
            // "Main Screen Background.bik": a movie of the install's
            std::string name = readText(&nernie, "Movie to Play");
            if (name.size() > 4 && name.ends_with(".bik"))
                name.resize(name.size() - 4);
            const auto file = movieFile(install, name);
            if (!file) {
                _contents.push_back("movie " + name + " missing");
                continue;
            }
            std::ifstream in(*file, std::ios::binary);
            part->movieData.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            _contents.push_back("movie " + name + at);
        } else if (nernie.className == "CImageNernie") {
            // "Main Menu/Reflexive Logo": a picture of DATA.dat's screens
            const std::string image = readText(&nernie, "Image");
            part->picture = screenPicture(data, image);
            if (!part->picture.w) {
                _contents.push_back("picture " + image + " missing");
                continue;
            }
            _contents.push_back("picture " + image + at);
        } else {
            _contents.push_back(nernie.className + ", not shown yet");
            continue;
        }
        _parts.push_back(std::move(part));
    }

    // The buttons, the first at Menu Pos, each the next under the one
    // before, Button Spacing between them (0x503d40).
    if (Error err = _font.load(data, kMainFont))
        _contents.push_back(std::string("font ") + kMainFont + " missing");
    int x = 0;
    int y = 0;
    int textX = 0;
    int textY = 0;
    readPoint(menu, "Menu Pos", x, y);
    readPoint(menu, "ButtonText Pos", textX, textY);
    const int spacing = menu ? int(menu->integer("Button Spacing").value_or(0)) : 0;
    const auto sound = gameSound(data, kMainButtonSound);
    for (const char *label : kMainButtons) {
        Button button(data, "Main Menu", "Main Button", x, y);
        button.setLabel(label, &_font, textX, textY);
        button.setSound(sound);
        _contents.push_back("button " + button.label() + " at " + std::to_string(x) + "," + std::to_string(y));
        y += button.height() + spacing;
        _buttons.add(std::move(button));
    }
    // Its buttons fade at 4 a second (0x50377e).
    _buttons.setFadeRate(4.0f);

    // The startup song, from Music/.
    const std::string song = readText(&rules, "Startup Song");
    std::vector<uint8_t> bytes;
    auto decoded = std::make_shared<audio::Sound>();
    if (!song.empty() && !data.read("Music/" + song, bytes) && !audio::decodeSound(bytes, *decoded)) {
        _song = std::move(decoded);
        _contents.push_back("song " + song);
    }
}

MainMenu::~MainMenu() = default;

bool MainMenu::run() {
    audio::SoundHandle song = audio::kNoSound;
    if (_mixer && _song)
        song = _mixer->play(_song, audio::SoundType::kMusic, audio::kMaxVolume, 0, true);
    const auto stopSong = [&] {
        if (_mixer && song != audio::kNoSound)
            _mixer->stop(song);
    };
    _sys.setMouseCursorVisible(true);
    for (;;) {
        Event ev;
        while (_sys.pollEvent(ev)) {
            if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher) {
                stopSong();
                return false;
            }
            if (_dialog) {
                const std::optional<bool> answer = _dialog->handleEvent(ev, _mixer);
                if (answer && *answer) {
                    stopSong();
                    return true;
                }
                if (answer)
                    _dialog.reset();
                continue;
            }
            if (_saveScreen) {
                const SaveGameScreen::Choice choice = _saveScreen->handleEvent(ev, _mixer);
                if (choice == SaveGameScreen::Choice::kLoad) {
                    const SaveGame save = _saveScreen->chosen();
                    _saveScreen.reset();
                    _sys.setTextInput(false);
                    loadGame(save);
                } else if (choice != SaveGameScreen::Choice::kNone) {
                    _saveScreen.reset();
                    _sys.setTextInput(false);
                }
                continue;
            }
            if (_netLobby) {
                if (_netLobby->handleEvent(ev, _mixer) == NetLobbyScreen::Choice::kExit) {
                    _netLobby.reset();
                    _sys.setTextInput(false);
                }
                continue;
            }
            if (_chooseNetGame) {
                const ChooseNetGameScreen::Choice choice = _chooseNetGame->handleEvent(ev, _mixer, _sys.getMillis());
                if (choice == ChooseNetGameScreen::Choice::kCancel) {
                    _options.netPlayerName = _chooseNetGame->name();
                    _options.save();
                    _chooseNetGame.reset();
                    _sys.setTextInput(false);
                }
                continue;
            }
            if (_difficultyWindow) {
                if (const auto closed = _difficultyWindow->handleEvent(ev, _mixer)) {
                    _difficultyWindow.reset();
                    if (*closed) {
                        _difficulty = **closed;
                        // The settings' window keeps the level; the main
                        // menu's starts the game with it.
                        if (!_settings)
                            startGame();
                    }
                }
                continue;
            }
            if (_keyConfig) {
                if (_keyConfig->handleEvent(ev, _mixer, _sys.getMillis()))
                    _keyConfig.reset();
                continue;
            }
            if (_soundOptions) {
                if (_soundOptions->handleEvent(ev, _mixer))
                    _soundOptions.reset();
                continue;
            }
            if (_settings) {
                const SettingsWindow::Choice choice = _settings->handleEvent(ev, _mixer);
                if (choice == SettingsWindow::Choice::kContinue)
                    _settings.reset();
                else if (choice == SettingsWindow::Choice::kKeyboard)
                    _keyConfig = std::make_unique<KeyConfigScreen>(_data, _rules, _options);
                else if (choice == SettingsWindow::Choice::kSound)
                    _soundOptions = std::make_unique<SoundOptionsWindow>(_data, _options, _screen.w, _screen.h);
                else if (choice == SettingsWindow::Choice::kDifficulty)
                    _difficultyWindow =
                        std::make_unique<DifficultyWindow>(_data, _screen.w, _screen.h, _difficulty, true);
                continue;
            }
            if (_mapScreen) {
                const MapScreen::Choice choice = _mapScreen->handleEvent(ev, _mixer, _sys.getMillis());
                moveOfficers();
                if (choice == MapScreen::Choice::kRestart) {
                    startMission(_missionTeam);
                } else if (choice == MapScreen::Choice::kSaveGame) {
                    openSaveScreen(SaveGameScreen::Mode::kSave);
                } else if (choice == MapScreen::Choice::kLoadGame) {
                    openSaveScreen(SaveGameScreen::Mode::kLoad);
                } else if (choice == MapScreen::Choice::kConfigure) {
                    // The escape menu's CONFIGURE: the settings over the map.
                    _settings = std::make_unique<SettingsWindow>(_data, _options, _screen.w, _screen.h);
                } else if (choice == MapScreen::Choice::kContinue) {
                    // Its debriefing, if it has one, then the mission the
                    // triggers named next ("Mission 02/Mission 02") from its
                    // briefing; without one, the main menu (inferred).
                    // Its score (0x50ca70) and time; the items the maps found,
                    // and those the score's now reached, for the next briefing.
                    const MissionProgress &progress = *_mapScreen->progress();
                    _campaign.score += missionScore(progress.mission, progress.objectives);
                    _campaign.time += progress.time;
                    for (const std::string &item : progress.discover)
                        _campaign.toDescribe.push_back(item);
                    for (const std::string &item : foundByScore(loadItems(_data), _campaign))
                        _campaign.toDescribe.push_back(item);
                    _nextMission = 0;
                    const std::string name = _mapScreen->nextMission();
                    if (std::sscanf(name.c_str(), "Mission %d", &_nextMission) != 1)
                        _nextMission = 0;
                    if (_mission.showDebriefing)
                        _debriefing = std::make_unique<DebriefingScreen>(_data, _rules, _mission, _options, _mixer,
                                                                         _mapScreen->objectiveLines(),
                                                                         _mapScreen->missionTime(), _sys.getMillis());
                    endMaps();
                    if (!_debriefing && _nextMission > 0)
                        beginMission(_nextMission);
                } else if (choice != MapScreen::Choice::kNone) {
                    endMaps();
                }
                continue;
            }
            if (_debriefing) {
                if (_debriefing->handleEvent(ev, _sys.getMillis()) == DebriefingScreen::Choice::kContinue) {
                    _debriefing.reset();
                    if (_nextMission > 0)
                        beginMission(_nextMission);
                }
                continue;
            }
            if (_briefing) {
                const BriefingScreen::Choice choice = _briefing->handleEvent(ev, _sys.getMillis());
                if (choice == BriefingScreen::Choice::kExit) {
                    _briefing.reset();
                    _teamSelection.reset();
                } else if (choice == BriefingScreen::Choice::kEndBriefing) {
                    _briefing.reset();
                    // On to the team selection; without one, the mission.
                    if (!_teamSelection && _mission.showTeamSelection)
                        showTeamSelection();
                    else if (!_teamSelection)
                        startMission(startingTeam());
                }
                continue;
            }
            if (_teamSelection) {
                const TeamSelectionScreen::Choice choice = _teamSelection->handleEvent(ev, _mixer, _sys.getMillis());
                if (choice == TeamSelectionScreen::Choice::kBriefing) {
                    _briefing = std::make_unique<BriefingScreen>(_data, _install, _rules, _mission, _options, _mixer,
                                                                 _sys.getMillis());
                } else if (choice == TeamSelectionScreen::Choice::kStartMission) {
                    std::vector<TeamMember> team = _teamSelection->chosenTeam();
                    _teamSelection.reset();
                    startMission(std::move(team));
                } else if (choice != TeamSelectionScreen::Choice::kNone) {
                    _teamSelection.reset();
                }
                continue;
            }
            if (_credits) {
                if (_credits->handleEvent(ev, _mixer))
                    _credits.reset();
                continue;
            }
            // START NEW GAME is the menu's default button, QUIT its cancel
            // button (0x503911).
            const int chosen = isEnter(ev)    ? _buttons.press(kStartButton, _mixer)
                               : isEscape(ev) ? _buttons.press(kQuitButton, _mixer)
                                              : _buttons.handleEvent(ev, _mixer);
            if (chosen == int(kQuitButton))
                _dialog = std::make_unique<ConfirmDialog>(_data, _rules, kQuitQuestion, _screen.w, _screen.h);
            else if (chosen == int(kLoadButton) && !_saves.empty())
                openSaveScreen(SaveGameScreen::Mode::kLoad);
            else if (chosen == int(kNetworkButton))
                openNetGame();
            else if (chosen == int(kStartButton))
                _difficultyWindow = std::make_unique<DifficultyWindow>(_data, _screen.w, _screen.h, _difficulty);
            else if (chosen == int(kSettingsButton))
                _settings = std::make_unique<SettingsWindow>(_data, _options, _screen.w, _screen.h);
            else if (chosen == int(kCreditsButton))
                _credits = std::make_unique<CreditsWindow>(_data, _rules, _screen.w, _screen.h, _sys.getMillis());
        }
        // The menu's song stops for the game's screens, which have their
        // own music, and comes back with the menu (inferred).
        const bool inGame = _briefing || _teamSelection || _mapScreen || _debriefing;
        if (inGame && song != audio::kNoSound) {
            stopSong();
            song = audio::kNoSound;
        } else if (!inGame && song == audio::kNoSound && _mixer && _song) {
            song = _mixer->play(_song, audio::SoundType::kMusic, audio::kMaxVolume, 0, true);
        }
        playMissionMusic();
        updateNetScreens(_sys.getMillis());
        draw();
        brighten();
        // The map's own pointer over it, else Interface/Cursors' Pointer
        // (0x4f566a).
        const bool windows = _settings || _soundOptions || _keyConfig || _difficultyWindow;
        showCursor(_mapScreen && !windows ? _mapScreen->cursor(_sys.getMillis()) : _pointer.at(_sys.getMillis()));
        _sys.updateScreen(_screen);
        _sys.delayMillis(10);
    }
}

void MainMenu::showCursor(const Animation::Picture *p) {
    if (p == _cursorShown)
        return;
    _cursorShown = p;
    if (!p || !p->image.w) {
        _sys.setMouseCursor(nullptr);
        return;
    }
    // Its origin's the mouse's place; the picture's widened to take it in
    // when it's outside (the edges' arrows are drawn off it).
    const int x0 = std::min(0, p->originX);
    const int y0 = std::min(0, p->originY);
    const int x1 = std::max(p->image.w, p->originX + 1);
    const int y1 = std::max(p->image.h, p->originY + 1);
    Cursor c;
    c.w = x1 - x0;
    c.h = y1 - y0;
    c.hotX = p->originX - x0;
    c.hotY = p->originY - y0;
    c.rgba.assign(size_t(c.w) * size_t(c.h) * 4, 0);
    for (int y = 0; y < p->image.h; ++y)
        std::memcpy(c.pixel(-x0, y - y0), p->image.pixel(0, y), size_t(p->image.w) * 4);
    _sys.setMouseCursor(&c);
}

void MainMenu::playMissionMusic() {
    // On a mission's maps, its Primary Song as they start (0x5c3258); as
    // each song ends, the next of its Random Songs, shuffled, all of them
    // before they're shuffled again (0x507460); without them, the last
    // song again (0x5bcbeb). The maps' Map Song isn't played. It stops as
    // the maps are left.
    if (!_mixer)
        return;
    if (!_mapScreen) {
        if (_missionSong != audio::kNoSound)
            _mixer->stop(_missionSong);
        _missionSong = audio::kNoSound;
        _songsPlayed = 0;
        _playlist.clear();
        _playlistNext = 0;
        _lastSong.clear();
        return;
    }
    if (_missionSong != audio::kNoSound && _mixer->isPlaying(_missionSong))
        return;
    std::string name;
    if (_songsPlayed == 0) {
        name = _mission.primarySong;
    } else if (!_mission.randomSongs.empty()) {
        if (_playlistNext >= _playlist.size()) {
            // Each in turn swapped with one chosen by chance; one that
            // would come straight after itself goes last instead (0x507380).
            const std::string previous = _playlist.empty() ? std::string() : _playlist.back();
            _playlist = _mission.randomSongs;
            for (size_t i = 0; i < _playlist.size(); ++i) {
                _songRandom = _songRandom * 1103515245u + 12345u;
                std::swap(_playlist[i], _playlist[(_songRandom >> 16) % _playlist.size()]);
            }
            if (!previous.empty() && _playlist.front() == previous)
                std::swap(_playlist.front(), _playlist.back());
            _playlistNext = 0;
        }
        name = _playlist[_playlistNext++];
    } else {
        name = _lastSong;
    }
    _lastSong = name;
    _missionSong = audio::kNoSound;
    ++_songsPlayed;
    std::vector<uint8_t> bytes;
    auto sound = std::make_shared<audio::Sound>();
    if (name.empty() || name == "!None.ogg" || _data.read("Music/" + name, bytes) || audio::decodeSound(bytes, *sound))
        return;
    _missionMusic = std::move(sound);
    _missionSong = _mixer->play(_missionMusic, audio::SoundType::kMusic, audio::kMaxVolume, 0, false);
}

void MainMenu::openSaveScreen(SaveGameScreen::Mode mode) {
    // What's saved is the mission as it began: loaded, it starts again
    // from its briefing (Reluva's own saves; the game's are of the map as
    // it stands).
    SaveGame save;
    char name[160];
    std::snprintf(name, sizeof name, "Mission %02d: %s", _mission.number, _mission.name.c_str());
    save.name = name;
    save.missionName = name;
    save.campaign = _missionStart;
    _saveScreen = std::make_unique<SaveGameScreen>(_data, _rules, mode, _saves, std::move(save));
    _sys.setTextInput(mode == SaveGameScreen::Mode::kSave);
}

void MainMenu::loadGame(const SaveGame &save) {
    endMaps();
    _briefing.reset();
    _teamSelection.reset();
    _debriefing.reset();
    _campaign = save.campaign;
    _difficulty = save.campaign.difficulty == int(Difficulty::kEasy) ? Difficulty::kEasy : Difficulty::kNormal;
    beginMission(_campaign.mission);
}

void MainMenu::startGame() {
    // A new game: no score, nothing found but what starts so; begun at a
    // later mission, the score's 100 for each before it (0x5c3070), and
    // the items that's reached are found as its mission starts (0x5c319d,
    // 0x5b8fe0), described in its briefing as any found (inferred).
    _campaign = Campaign{};
    _campaign.score = 100 * (_firstMission - 1);
    _campaign.difficulty = int(_difficulty);
    _campaign.toDescribe = foundByScore(loadItems(_data), _campaign);
    beginMission(_firstMission);
}

void MainMenu::beginMission(int number) {
    if (Error err = loadMission(_data, number, _mission)) {
        log::warning("Mission %d: %s", number, err.message.c_str());
        return;
    }
    _campaign.mission = number;
    _missionStart = _campaign;
    // After its lines, the briefing describes the items found since the
    // last, each by who found it (0x474639), and they're then had
    // (0x474b2d; inferred: here as the mission begins).
    const std::map<std::string, ItemDefinition> items = loadItems(_data);
    for (const std::string &name : _campaign.toDescribe) {
        const auto it = items.find(name);
        if (it == items.end() || it->second.discoveryText.empty() || it->second.discoveryText == "!None")
            continue;
        _mission.briefing.push_back({"Discovery Items", it->second.discoveryText, it->second.whoDiscovers, -1});
    }
    for (const std::string &name : _campaign.toDescribe)
        if (std::find(_campaign.discovered.begin(), _campaign.discovered.end(), name) == _campaign.discovered.end())
            _campaign.discovered.push_back(name);
    _campaign.toDescribe.clear();
    if (_mission.showBriefing)
        _briefing =
            std::make_unique<BriefingScreen>(_data, _install, _rules, _mission, _options, _mixer, _sys.getMillis());
    else if (_mission.showTeamSelection)
        showTeamSelection();
    else
        startMission(startingTeam());
}

std::vector<TeamMember> MainMenu::startingTeam() const {
    // Without a team selection, who the mission says starts (inferred).
    std::vector<TeamMember> roster;
    std::vector<TeamMember> team;
    if (loadRoster(_data, roster))
        return team;
    resolveInventories(roster, loadItems(_data), _campaign.discovered);
    for (const std::string &id : _mission.whoShouldStart)
        for (const TeamMember &m : roster)
            if (m.id == id && int(team.size()) < _mission.maxMembers)
                team.push_back(m);
    return team;
}

void MainMenu::startMission(std::vector<TeamMember> team) {
    _missionTeam = std::move(team);
    endMaps();
    char folder[32];
    std::snprintf(folder, sizeof folder, "Mission %02d", _mission.number);
    _mapScreen = openMap(std::string(folder) + "/" + _mission.mapName, false);
    if (_mapScreen) {
        _mapScreen->setObjectives(readMissionObjectives(_mission.objectives));
        _mapScreen->setDiscovered(_campaign.discovered);
    }
}

void MainMenu::endMaps() {
    _mapScreen = nullptr;
    _maps.clear();
}

MapScreen *MainMenu::openMap(const std::string &name, bool arriving) {
    // A mission's maps by their names, as its actions give them ("Mission
    // 02/02 Security deck.zax"), whatever their case.
    // One named without its .zax (Mission 09's) is that (inferred).
    const std::string file = name.ends_with(".zax") || name.ends_with(".ZAX") ? name : name + ".zax";
    const std::string key = normalizeArchiveName(file);
    if (const auto it = _maps.find(key); it != _maps.end())
        return it->second.get();
    const std::string base = "Levels/" + file;
    GameMap map;
    if (Error err = loadMap(_data, base, map)) {
        log::warning("Mission %d's map %s: %s", _mission.number, file.c_str(), err.message.c_str());
        return nullptr;
    }
    // Its waypoints, the map's name with .way (formats/binary-object.md).
    WayPoints wayPoints;
    const size_t dot = base.rfind('.');
    if (Error err = loadWayPoints(_data, base.substr(0, dot) + ".way", wayPoints))
        log::warning("Mission %d's waypoints: %s", _mission.number, err.message.c_str());
    auto screen = std::make_unique<MapScreen>(_data, _rules, std::move(map), _options, _sys.getMillis(), _missionTeam,
                                              std::move(wayPoints), _mixer, arriving);
    // The mission's progress is the same on all its maps.
    if (!_maps.empty())
        screen->shareProgress(_maps.begin()->second->progress());
    MapScreen *opened = screen.get();
    _maps.emplace(key, std::move(screen));
    return opened;
}

void MainMenu::showMap(MapScreen *map) {
    if (!map)
        return;
    _mapScreen = map;
    for (auto &[name, each] : _maps)
        each->setActive(each.get() == map);
}

void MainMenu::moveOfficers() {
    // Officers sent to another map leave theirs and arrive there, each at
    // its place: the next of a list ("space1,space2"), or numbered as they
    // go ("to lower bay 1" to "4", as the maps have them), or side by side
    // about the place itself (inferred); without a map named, on the same
    // one. The map's shown if the action says.
    // One who's joined the team on one map is on the others' team bars
    // too, there elsewhere.
    const MapScreen *largest = nullptr;
    for (auto &[name, map] : _maps)
        if (!largest || map->officers().size() > largest->officers().size())
            largest = map.get();
    for (auto &[name, map] : _maps)
        while (largest && map->officers().size() < largest->officers().size())
            map->addMember(largest->team()[largest->officers()[map->officers().size()].member], false, 0.0f, 0.0f);
    std::vector<std::pair<MapScreen *, Relocation>> moves;
    for (auto &[name, map] : _maps)
        for (Relocation &r : map->takeRelocations())
            moves.emplace_back(map.get(), std::move(r));
    for (auto &[from, r] : moves) {
        MapScreen *to = r.map.empty() ? from : openMap(r.map, true);
        if (!to)
            continue;
        std::vector<std::string> places;
        for (size_t at = 0; at <= r.location.size();) {
            const size_t comma = std::min(r.location.find(',', at), r.location.size());
            places.push_back(r.location.substr(at, comma - at));
            at = comma + 1;
        }
        float x = to->cameraX();
        float y = to->cameraY();
        if (!to->partPosition(places[0], x, y))
            (void)to->partPosition(places[0] + " 1", x, y);
        constexpr float kSpacing = 24.0f;
        float firstX = x;
        float firstY = y;
        for (size_t k = 0; k < r.officers.size(); ++k) {
            float ox = x + kSpacing * float(k);
            float oy = y;
            if (places.size() > 1)
                (void)to->partPosition(places[std::min(k, places.size() - 1)], ox, oy);
            else
                (void)to->partPosition(places[0] + " " + std::to_string(k + 1), ox, oy);
            if (k == 0) {
                firstX = ox;
                firstY = oy;
            }
            const size_t officer = size_t(r.officers[k]);
            const MapScreen::Officer state = from->officers()[officer];
            if (from != to)
                from->depart(officer);
            to->arrive(officer, state, ox, oy);
        }
        if (r.show) {
            showMap(to);
            for (size_t k = 0; k < r.officers.size(); ++k)
                to->choose(r.officers[k], k > 0);
            to->moveCamera(firstX, firstY);
        }
    }
    // An officer on another map chosen on the team bar: that map shown.
    for (auto &[name, map] : _maps) {
        const int officer = map->takeSwitch();
        if (officer < 0)
            continue;
        for (auto &[other, each] : _maps)
            if (size_t(officer) < each->officers().size() && each->officers()[size_t(officer)].present) {
                showMap(each.get());
                each->choose(officer);
                each->moveCamera(each->officers()[size_t(officer)].x, each->officers()[size_t(officer)].y);
                break;
            }
        break;
    }
}

void MainMenu::showTeamSelection() {
    std::vector<TeamMember> roster;
    if (Error err = loadRoster(_data, roster)) {
        log::warning("The roster: %s", err.message.c_str());
        return;
    }
    // The officers' items, and those the mission needs, as they're had:
    // those not found yet left out, and those found since in place of the
    // ones they override (0x5ec26d, 0x5ebf7b).
    const std::map<std::string, ItemDefinition> items = loadItems(_data);
    resolveInventories(roster, items, _campaign.discovered);
    Mission mission = _mission;
    for (std::vector<std::string> *list : {&mission.items, &mission.recommendedItems})
        for (std::string &item : *list)
            if (std::string held = heldAs(items, item, _campaign.discovered); !held.empty())
                item = held;
    _teamSelection = std::make_unique<TeamSelectionScreen>(_data, _rules, mission, std::move(roster));
}

void MainMenu::drawWindows(uint64_t now) {
    if (_saveScreen)
        _saveScreen->draw(_screen, now);
    if (_settings)
        _settings->draw(_screen, now);
    if (_soundOptions)
        _soundOptions->draw(_screen, now);
    if (_keyConfig)
        _keyConfig->draw(_screen, now);
    if (_difficultyWindow)
        _difficultyWindow->draw(_screen, now);
}

void MainMenu::draw() {
    const uint64_t now = _sys.getMillis();
    clearScreen(_screen);
    if (_mapScreen) {
        // The mission's other maps go on meanwhile (inferred).
        for (auto &[name, map] : _maps)
            if (map.get() != _mapScreen)
                map->update(now);
        _mapScreen->draw(_screen, now);
        moveOfficers();
        drawWindows(now);
        return;
    }
    if (_debriefing) {
        _debriefing->draw(_screen, now);
        return;
    }
    if (_briefing) {
        _briefing->draw(_screen, now);
        return;
    }
    if (_teamSelection) {
        _teamSelection->draw(_screen, now);
        return;
    }
    if (_netLobby) {
        _netLobby->draw(_screen, now);
        return;
    }
    for (auto &part : _parts) {
        if (!part->movieData.empty()) {
            // Started again as it ends: the screen's movies loop.
            if (!part->movie || part->movie->done(now)) {
                part->movie = std::make_unique<video::MoviePlayer>(nullptr);
                if (part->movie->open(part->movieData, now)) {
                    part->movieData.clear();
                    continue;
                }
            }
            part->movie->update(now);
            if (part->movie->frame().w > 0)
                image::blit(_screen, part->movie->frame(), part->x, part->y);
        } else {
            image::blit(_screen, part->picture, part->x, part->y);
        }
    }
    _buttons.draw(_screen, now);
    if (_credits)
        _credits->draw(_screen, now);
    drawWindows(now);
    if (_chooseNetGame)
        _chooseNetGame->draw(_screen, now);
    if (_dialog)
        _dialog->draw(_screen);
}

void MainMenu::openNetGame() {
    NetAccess network;
    network.open = [this](uint16_t port) {
        std::unique_ptr<net::UdpSocket> socket;
        if (_sys.openUdp(port, socket))
            return std::unique_ptr<net::UdpSocket>();
        return socket;
    };
    // The local network's broadcasts, and this machine, for a game made by
    // another copy on it.
    network.askAt.push_back(net::Address::broadcast(netplay::kDefaultPort));
    for (uint32_t ip : _sys.broadcastAddresses())
        network.askAt.push_back({ip, netplay::kDefaultPort});
    network.askAt.push_back({0x7f000001u, netplay::kDefaultPort});
    _chooseNetGame = std::make_unique<ChooseNetGameScreen>(_data, _rules, std::move(network), _options.netPlayerName,
                                                           _screen.w, _screen.h);
    _sys.setTextInput(true);
}

void MainMenu::updateNetScreens(uint64_t now) {
    if (_chooseNetGame && _chooseNetGame->update(now) == ChooseNetGameScreen::Choice::kLobby) {
        _options.netPlayerName = _chooseNetGame->name();
        _options.save();
        _netLobby =
            std::make_unique<NetLobbyScreen>(_data, _rules, loadNetMissions(_data), _chooseNetGame->takeSession());
        _chooseNetGame.reset();
    }
    if (!_netLobby)
        return;
    const NetLobbyScreen::Choice choice = _netLobby->update(now);
    if (choice == NetLobbyScreen::Choice::kExit) {
        _netLobby.reset();
        _sys.setTextInput(false);
    } else if (choice == NetLobbyScreen::Choice::kStart) {
        _netLobby->notice(kNoNetMissions);
    }
}

void brightenScreen(Surface &screen, float gamma) {
    // Truncated, as the program's float to int is (0x6076f8).
    const int amount = std::min(int((gamma - 1.0f) * 128.0f), 255);
    if (amount < 8)
        return;
    const int redBlue = amount >> 3 << 3;
    const int green = amount >> 2 << 2;
    for (int y = 0; y < screen.h; ++y) {
        uint8_t *p = screen.row(y);
        for (int x = 0; x < screen.w; ++x, p += 4) {
            p[0] = uint8_t(std::min(255, p[0] + redBlue));
            p[1] = uint8_t(std::min(255, p[1] + green));
            p[2] = uint8_t(std::min(255, p[2] + redBlue));
        }
    }
}

void MainMenu::brighten() {
    brightenScreen(_screen, _options.brightness);
}

} // namespace reluva::stawayteam
