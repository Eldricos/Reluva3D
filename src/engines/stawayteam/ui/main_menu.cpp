// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/main_menu.h"

#include "audio/decode.h"
#include "engines/stawayteam/ui/movies.h"
#include "video/movie_player.h"

#include <array>
#include <cstdlib>
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
constexpr size_t kQuitButton = 5;
// The program's, not Rules.txt's Menu Font (0x50359a).
const char *const kMainFont = "Placar Condensed";
const char *const kMainButtonSound = "Interface/Main Screen Button.wav";     // 0x5036b9
const char *const kQuitQuestion = "Are you sure you want to exit the game?"; // 0x640058
// The dialog's question is in this font, whatever Rules.txt's Menu Font
// says (0x4872b5).
const char *const kQuestionFont = "Placar Blue";

// A point as Rules.txt writes it: "x,y"; (0, 0) if it isn't there.
void point(const TextObject *object, const std::string &key, int &x, int &y) {
    x = y = 0;
    const std::string *p = object ? object->value(key) : nullptr;
    if (!p)
        return;
    const size_t comma = p->find(',');
    x = std::atoi(p->c_str());
    y = comma == std::string::npos ? 0 : std::atoi(p->c_str() + comma + 1);
}

std::string text(const TextObject *object, const std::string &key) {
    const std::string *value = object ? object->value(key) : nullptr;
    return value ? *value : std::string();
}

} // namespace

const TextObject *menuDefinition(const TextObject &rules, const std::string &name) {
    const TextObject *custom = rules.object("Custom Rules");
    const TextObject *menus = custom ? custom->object("Menu") : nullptr;
    if (!menus)
        return nullptr;
    for (const TextField *item : menus->items()) {
        const std::string *menuName = item->object ? item->object->value("Menu Name") : nullptr;
        if (menuName && *menuName == name)
            return item->object->object("Menu Definition");
    }
    return nullptr;
}

ConfirmDialog::ConfirmDialog(const Archive &data, const TextObject &rules, std::string question, int screenW,
                             int screenH)
    : _background(screenPicture(data, "Confirm Dialog/Background")), _question(std::move(question)),
      _x((screenW - _background.w) / 2), _y((screenH - _background.h) / 2) {
    const TextObject *def = menuDefinition(rules, "Confirm Dialog");
    int unused = 0;
    point(def, "TextCenter Pos", unused, _questionY);
    (void)_questionFont.load(data, kQuestionFont);
    const std::array<const char *, 2> names = {"Yes", "No"};
    for (size_t i = 0; i < names.size(); ++i) {
        const std::string key = std::string(names[i]) + "Button";
        int x = 0;
        int y = 0;
        int textX = 0;
        int textY = 0;
        point(def, key + " Pos", x, y);
        point(def, key + " Text Pos", textX, textY);
        (void)_buttonFonts[i].load(data, text(def, key + " Font"));
        Button button(data, "Confirm Dialog", names[i], x, y);
        button.setLabel(text(def, key + " Text"), &_buttonFonts[i], textX, textY);
        button.setSound(gameSound(data, text(def, key + " Press Sound")));
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
        _questionFont.drawWrapped(screen, _x + _background.w / 2, _y + _questionY, _background.w - 128, _question);
    _buttons.draw(screen);
}

MainMenu::MainMenu(System &sys, Surface &screen, const Archive &data, const std::filesystem::path &install,
                   const TextObject &rules, audio::Mixer *mixer)
    : _sys(sys), _screen(screen), _data(data), _rules(rules), _mixer(mixer) {
    const TextObject *menu = menuDefinition(rules, "Main Menu");
    const TextObject *nernies = menu ? menu->object("Nernie") : nullptr;
    for (const TextField *item : nernies ? nernies->items() : std::vector<const TextField *>{}) {
        if (!item->object)
            continue;
        const TextObject &nernie = *item->object;
        auto part = std::make_unique<Part>();
        point(&nernie, "Position", part->x, part->y);
        const std::string at = " at " + std::to_string(part->x) + "," + std::to_string(part->y);
        if (nernie.className == "CMovieNernie") {
            // "Main Screen Background.bik": a movie of the install's
            std::string name = text(&nernie, "Movie to Play");
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
            const std::string image = text(&nernie, "Image");
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
    point(menu, "Menu Pos", x, y);
    point(menu, "ButtonText Pos", textX, textY);
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
    const std::string song = text(&rules, "Startup Song");
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
            // Esc asks to quit, as the Quit button does.
            const bool escape = ev.type == EventType::kKeyDown && ev.key == key::kEscape;
            if (escape || _buttons.handleEvent(ev, _mixer) == int(kQuitButton))
                _dialog = std::make_unique<ConfirmDialog>(_data, _rules, kQuitQuestion, _screen.w, _screen.h);
        }
        draw();
        _sys.updateScreen(_screen);
        _sys.delayMillis(10);
    }
}

void MainMenu::draw() {
    const uint64_t now = _sys.getMillis();
    clearScreen(_screen);
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
    if (_dialog)
        _dialog->draw(_screen);
}

} // namespace reluva::stawayteam
