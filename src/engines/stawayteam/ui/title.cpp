// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/title.h"

#include "audio/decode.h"
#include "engines/stawayteam/formats/frame.h"
#include "engines/stawayteam/ui/movies.h"
#include "video/movie_player.h"

#include <fstream>
#include <iterator>

namespace reluva::stawayteam {

/** A movie or a picture of the screen, and where it goes. */
struct TitleScreen::Part {
    int x = 0;
    int y = 0;
    std::vector<uint8_t> movieData; // kept to start the movie again
    std::unique_ptr<video::MoviePlayer> movie;
    image::Image picture;
};

namespace {

// A position as Rules.txt writes it: "x,y".
void position(const TextObject &nernie, int &x, int &y) {
    x = y = 0;
    const std::string *p = nernie.value("Position");
    if (!p)
        return;
    const size_t comma = p->find(',');
    x = std::atoi(p->c_str());
    y = comma == std::string::npos ? 0 : std::atoi(p->c_str() + comma + 1);
}

// The main menu's definition among Rules.txt's menus.
const TextObject *mainMenu(const TextObject &rules) {
    const TextObject *custom = rules.object("Custom Rules");
    const TextObject *menus = custom ? custom->object("Menu") : nullptr;
    if (!menus)
        return nullptr;
    for (const TextField *item : menus->items()) {
        const std::string *name = item->object ? item->object->value("Menu Name") : nullptr;
        if (name && *name == "Main Menu")
            return item->object->object("Menu Definition");
    }
    return nullptr;
}

} // namespace

TitleScreen::TitleScreen(System &sys, Surface &screen, const Archive &data, const std::filesystem::path &install,
                         const TextObject &rules, audio::Mixer *mixer)
    : _sys(sys), _screen(screen), _mixer(mixer) {
    const TextObject *menu = mainMenu(rules);
    const TextObject *nernies = menu ? menu->object("Nernie") : nullptr;
    for (const TextField *item : nernies ? nernies->items() : std::vector<const TextField *>{}) {
        if (!item->object)
            continue;
        const TextObject &nernie = *item->object;
        auto part = std::make_unique<Part>();
        position(nernie, part->x, part->y);
        const std::string at = " at " + std::to_string(part->x) + "," + std::to_string(part->y);
        if (nernie.className == "CMovieNernie") {
            // "Main Screen Background.bik": a movie of the install's
            const std::string *movie = nernie.value("Movie to Play");
            std::string name = movie ? *movie : "";
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
            const std::string *image = nernie.value("Image");
            std::vector<uint8_t> bytes;
            Frame frame;
            if (!image || data.read("Cache/Interface/Screens/" + *image + ".frm16", bytes) ||
                readFrame(bytes.data(), bytes.size(), frame)) {
                _contents.push_back("picture " + (image ? *image : std::string()) + " missing");
                continue;
            }
            part->picture = frameImage(frame);
            _contents.push_back("picture " + *image + at);
        } else {
            _contents.push_back(nernie.className + ", not shown yet");
            continue;
        }
        _parts.push_back(std::move(part));
    }
    // The startup song, from Music/.
    const std::string *song = rules.value("Startup Song");
    std::vector<uint8_t> bytes;
    auto sound = std::make_shared<audio::Sound>();
    if (song && !data.read("Music/" + *song, bytes) && !audio::decodeSound(bytes, *sound)) {
        _song = std::move(sound);
        _contents.push_back("song " + *song);
    }
}

TitleScreen::~TitleScreen() = default;

bool TitleScreen::run() {
    audio::SoundHandle song = audio::kNoSound;
    if (_mixer && _song)
        song = _mixer->play(_song, audio::SoundType::kMusic, audio::kMaxVolume, 0, true);
    const auto stopSong = [&] {
        if (_mixer && song != audio::kNoSound)
            _mixer->stop(song);
    };
    for (;;) {
        Event ev;
        while (_sys.pollEvent(ev)) {
            if (ev.type == EventType::kQuit || ev.type == EventType::kReturnToLauncher) {
                stopSong();
                return false;
            }
            if (ev.type == EventType::kKeyDown && ev.key == key::kEscape) {
                stopSong();
                return true;
            }
        }
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
        _sys.updateScreen(_screen);
        _sys.delayMillis(10);
    }
}

} // namespace reluva::stawayteam
