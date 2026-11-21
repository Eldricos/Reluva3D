// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/main_menu.h"

#include "audio/decode.h"
#include "common/log.h"
#include "engines/stawayteam/ui/layout.h"
#include "engines/stawayteam/ui/movies.h"
#include "video/movie_player.h"

#include <array>
#include <cmath>
#include <cstdio>
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

// The main menu's buttons, top to bottom, as the program names them
// (0x503657): Rules.txt only places them.
constexpr std::array<const char *, 6> kMainButtons = {"&START NEW GAME", "&LOAD SAVE GAME", "PLAY &NETWORK GAME",
                                                      "S&ETTINGS",       "&CREDITS",        "&QUIT"};
constexpr size_t kStartButton = 0;
constexpr size_t kSettingsButton = 3;
constexpr size_t kCreditsButton = 4;
constexpr size_t kQuitButton = 5;
// The program's, not Rules.txt's Menu Font (0x50359a).
const char *const kMainFont = "Placar Condensed";
const char *const kMainButtonSound = "Interface/Main Screen Button.wav";     // 0x5036b9
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
    if (ev.type == EventType::kKeyDown && ev.key == key::kEscape)
        return false;
    const int chosen = _buttons.handleEvent(ev, mixer);
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
                   const TextObject &rules, GameOptions &options, audio::Mixer *mixer, int firstMission)
    : _sys(sys), _screen(screen), _data(data), _install(install), _rules(rules), _options(options), _mixer(mixer),
      _firstMission(firstMission) {
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
            if (_mapScreen) {
                if (_mapScreen->handleEvent(ev, _mixer, _sys.getMillis()) != MapScreen::Choice::kNone)
                    _mapScreen.reset();
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
            if (_credits) {
                if (_credits->handleEvent(ev, _mixer))
                    _credits.reset();
                continue;
            }
            // Esc asks to quit, as the Quit button does.
            const bool escape = ev.type == EventType::kKeyDown && ev.key == key::kEscape;
            const int chosen = escape ? int(kQuitButton) : _buttons.handleEvent(ev, _mixer);
            if (chosen == int(kQuitButton))
                _dialog = std::make_unique<ConfirmDialog>(_data, _rules, kQuitQuestion, _screen.w, _screen.h);
            else if (chosen == int(kStartButton))
                _difficultyWindow = std::make_unique<DifficultyWindow>(_data, _screen.w, _screen.h, _difficulty);
            else if (chosen == int(kSettingsButton))
                _settings = std::make_unique<SettingsWindow>(_data, _options, _screen.w, _screen.h);
            else if (chosen == int(kCreditsButton))
                _credits = std::make_unique<CreditsWindow>(_data, _rules, _screen.w, _screen.h, _sys.getMillis());
        }
        // The menu's song stops for the game's screens, which have their
        // own music, and comes back with the menu (inferred).
        const bool inGame = _briefing || _teamSelection || _mapScreen;
        if (inGame && song != audio::kNoSound) {
            stopSong();
            song = audio::kNoSound;
        } else if (!inGame && song == audio::kNoSound && _mixer && _song) {
            song = _mixer->play(_song, audio::SoundType::kMusic, audio::kMaxVolume, 0, true);
        }
        draw();
        brighten();
        _sys.updateScreen(_screen);
        _sys.delayMillis(10);
    }
}

void MainMenu::startGame() {
    if (Error err = loadMission(_data, _firstMission, _mission)) {
        log::warning("Mission %d: %s", _firstMission, err.message.c_str());
        return;
    }
    if (_mission.showBriefing)
        _briefing =
            std::make_unique<BriefingScreen>(_data, _install, _rules, _mission, _options, _mixer, _sys.getMillis());
    else if (_mission.showTeamSelection)
        showTeamSelection();
}

std::vector<TeamMember> MainMenu::startingTeam() const {
    // Without a team selection, who the mission says starts (inferred).
    std::vector<TeamMember> roster;
    std::vector<TeamMember> team;
    if (loadRoster(_data, roster))
        return team;
    for (const std::string &id : _mission.whoShouldStart)
        for (const TeamMember &m : roster)
            if (m.id == id && int(team.size()) < _mission.maxMembers)
                team.push_back(m);
    return team;
}

void MainMenu::startMission(std::vector<TeamMember> team) {
    char folder[32];
    std::snprintf(folder, sizeof folder, "Mission %02d", _mission.number);
    const std::string base = std::string("Levels/") + folder + "/" + _mission.mapName;
    GameMap map;
    if (Error err = loadMap(_data, base, map)) {
        log::warning("Mission %d's map: %s", _mission.number, err.message.c_str());
        return;
    }
    // Its waypoints, the map's name with .way (formats/binary-object.md).
    WayPoints wayPoints;
    const size_t dot = base.rfind('.');
    if (Error err = loadWayPoints(_data, base.substr(0, dot) + ".way", wayPoints))
        log::warning("Mission %d's waypoints: %s", _mission.number, err.message.c_str());
    _mapScreen = std::make_unique<MapScreen>(_data, _rules, std::move(map), _options, _sys.getMillis(), std::move(team),
                                             std::move(wayPoints));
}

void MainMenu::showTeamSelection() {
    std::vector<TeamMember> roster;
    if (Error err = loadRoster(_data, roster)) {
        log::warning("The roster: %s", err.message.c_str());
        return;
    }
    _teamSelection = std::make_unique<TeamSelectionScreen>(_data, _rules, _mission, std::move(roster));
}

void MainMenu::draw() {
    const uint64_t now = _sys.getMillis();
    clearScreen(_screen);
    if (_mapScreen) {
        _mapScreen->draw(_screen, now);
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
    _buttons.draw(_screen);
    if (_credits)
        _credits->draw(_screen, now);
    if (_settings)
        _settings->draw(_screen, now);
    if (_soundOptions)
        _soundOptions->draw(_screen, now);
    if (_keyConfig)
        _keyConfig->draw(_screen, now);
    if (_difficultyWindow)
        _difficultyWindow->draw(_screen, now);
    if (_dialog)
        _dialog->draw(_screen);
}

void MainMenu::brighten() {
    const float gamma = _options.brightness;
    if (gamma <= GameOptions::kBrightnessMin)
        return;
    // The display's gamma, as the game sets it (0x558fc0), taken as
    // brightening the darker colours (inferred).
    std::array<uint8_t, 256> table{};
    for (size_t i = 0; i < table.size(); ++i)
        table[i] = uint8_t(std::lround(255.0 * std::pow(double(i) / 255.0, 1.0 / double(gamma))));
    for (int y = 0; y < _screen.h; ++y) {
        uint8_t *p = _screen.row(y);
        for (int x = 0; x < _screen.w; ++x, p += 4) {
            p[0] = table[p[0]];
            p[1] = table[p[1]];
            p[2] = table[p[2]];
        }
    }
}

} // namespace reluva::stawayteam
