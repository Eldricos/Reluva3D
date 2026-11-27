// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/briefing.h"

#include "audio/decode.h"
#include "engines/stawayteam/formats/frame.h"
#include "engines/stawayteam/formats/rds_script.h"
#include "engines/stawayteam/ui/layout.h"
#include "engines/stawayteam/ui/main_menu.h"
#include "engines/stawayteam/ui/movies.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace reluva::stawayteam {

namespace {

const char *const kExitQuestion = "Are you sure you want to quit the game and exit to the main menu?"; // 0x63a9e0
const char *const kTypingPrompt = "Press 'Space' to display all text";                                 // 0x63aa24
const char *const kNextPrompt = "Press 'Space' or 'Continue' to continue briefing";                    // 0x63aa6c
const char *const kEndPrompt = "Press 'End Briefing' to continue";                                     // 0x63aa48
const char *const kSpaceSound = "Interface/Space Bar Briefing.wav";                                    // 0x63abc0
// The title's and the prompt's fonts, the program's (0x4748ea, 0x4747d0).
const char *const kTitleFont = "Placar Blue";
const char *const kPromptFont = "Trek Small";
// A voice-over's end, then this long before the next line comes on its
// own (0x47437a).
constexpr float kAfterVoice = 1.0f;
// The typing's speed until Rules.txt's is read (0x568d68), in characters
// a second.
constexpr float kDefaultSpeed = 90.0f;

// The buttons, in ButtonGroup order: Rules.txt's key, the pictures' name
// (0x4739c4...).
struct ButtonSpec {
    const char *key;
    const char *picture;
};
constexpr std::array<ButtonSpec, 4> kButtons = {{{"ExitButton", "Exit"},
                                                 {"ReplayButton", "Replay"},
                                                 {"ContinueButton", "Continue"},
                                                 {"EndBriefingButton", "End Briefing"}}};
enum ButtonIndex : int { kExit, kReplay, kContinue, kEndBriefing };

std::string withoutExtension(std::string name) {
    const size_t dot = name.rfind('.');
    if (dot != std::string::npos && name.find('/', dot) == std::string::npos)
        name.resize(dot);
    return name;
}

} // namespace

BriefingScreen::BriefingScreen(const Archive &data, const std::filesystem::path &install, const TextObject &rules,
                               const Mission &mission, const GameOptions &options, audio::Mixer *mixer, uint64_t now)
    : _data(data), _rules(rules), _def(menuDefinition(rules, "Briefing Screen")), _mission(mission), _options(options),
      _mixer(mixer), _background(screenPicture(data, "Briefing/Background")),
      _portraitBackground(screenPicture(data, "Briefing/Portrait Background")), _last(now) {
    // The scroll bar's pictures (0x4727e8).
    _bar.background = screenPicture(data, "Briefing/Scroll Background");
    _bar.upDefault = screenPicture(data, "Briefing/Scroll Up Default");
    _bar.upPress = screenPicture(data, "Briefing/Scroll Up Press");
    _bar.downDefault = screenPicture(data, "Briefing/Scroll Down Default");
    _bar.downPress = screenPicture(data, "Briefing/Scroll Down Press");
    _bar.thumb = screenPicture(data, "Briefing/Scroll Thumb");
    _texts = readStaticTexts(data, _def);
    _numbers = readRandomNumbers(data, _def, 1);
    (void)_textFont.load(data, readText(_def, "Briefing Text Font"));
    (void)_titleFont.load(data, kTitleFont);
    (void)_promptFont.load(data, kPromptFont);
    _buttonFonts.resize(kButtons.size());
    for (size_t i = 0; i < kButtons.size(); ++i) {
        const std::string key = kButtons[i].key;
        int x = 0;
        int y = 0;
        int textX = 0;
        int textY = 0;
        readPoint(_def, key + " Pos", x, y);
        readPoint(_def, key + " Text Pos", textX, textY);
        Button button(data, "Briefing", kButtons[i].picture, x, y);
        (void)_buttonFonts[i].load(data, readText(_def, key + " Font"));
        button.setLabel(readText(_def, key + " Text"), &_buttonFonts[i], textX, textY);
        button.setSound(gameSound(data, readText(_def, key + " Press Sound")));
        _buttons.add(std::move(button));
    }
    _spaceSound = gameSound(data, kSpaceSound);

    // "Mission 02: The Leyte Gulf" (0x473d96).
    char title[160];
    std::snprintf(title, sizeof title, "%s %02d: %s", "Mission", _mission.number, _mission.name.c_str());
    _title = title;

    // The movie, its sound only with voice-overs on (0x47358a).
    if (const auto file = movieFile(install, withoutExtension(_mission.briefingMovie))) {
        std::ifstream in(*file, std::ios::binary);
        _movieData.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    replay(now);
}

BriefingScreen::~BriefingScreen() {
    stopSounds();
}

void BriefingScreen::stopSounds() {
    if (_mixer && _voice != audio::kNoSound)
        _mixer->stop(_voice);
    _voice = audio::kNoSound;
}

bool BriefingScreen::typed() const {
    return _typed >= float(_text.size());
}

void BriefingScreen::replay(uint64_t now) {
    _movie.reset();
    if (!_movieData.empty()) {
        _movie = std::make_unique<video::MoviePlayer>(_mixer);
        if (_movie->open(_movieData, now, _options.playVoiceovers))
            _movie.reset();
    }
    _buttons.button(kContinue).setEnabled(true);
    showLine(0);
}

void BriefingScreen::showLine(size_t line) {
    stopSounds();
    _line = line;
    _text.clear();
    _portrait = image::Image();
    _typed = 0.0f;
    _scroll = 0;
    _hadVoice = false;
    _sinceVoice = 0.0f;
    if (line >= _mission.briefing.size())
        return;
    const BriefingLine &l = _mission.briefing[line];
    std::vector<uint8_t> bytes;
    if (!_data.read("Levels/" + l.script + ".RDS", bytes)) {
        const RdsScript script = readRdsScript(std::string(bytes.begin(), bytes.end()));
        if (const std::string *text = script.find(l.text))
            _text = *text;
    }
    Frame frame;
    if (!l.speaker.empty() && !_data.read("Cache/Character/Character Portraits/" + l.speaker + ".frm16", bytes) &&
        !readFrame(bytes.data(), bytes.size(), frame))
        _portrait = frameImage(frame);
    // The voice-over named as the entry, when the game's options play them
    // (0x472ce1).
    if (_options.playVoiceovers) {
        if (auto voice = voiceOver(_data, l.text)) {
            _hadVoice = true;
            if (_mixer)
                _voice = _mixer->play(voice, audio::SoundType::kSpeech);
        }
    }
}

void BriefingScreen::update(uint64_t now) {
    const float dt = float(now - std::min(now, _last)) / 1000.0f;
    _last = now;
    for (RandomNumbers &n : _numbers)
        n.update(dt);
    const double speed = _def ? _def->number("TextDisplay Text Speed").value_or(kDefaultSpeed) : kDefaultSpeed;
    _typed = std::min(float(_text.size()), _typed + dt * float(speed));
    const bool voicePlaying = _mixer && _voice != audio::kNoSound && _mixer->isPlaying(_voice);
    if (_hadVoice && !voicePlaying)
        _sinceVoice += dt;
    if (_movie) {
        if (_mission.loopMovie && _movie->done(now))
            (void)_movie->open(_movieData, now, _options.playVoiceovers);
        _movie->update(now);
    }
    const bool hasNext = _line + 1 < _mission.briefing.size();
    // The next line's frame reached: this one shown whole, and on to it
    // (0x4742ca).
    if (_movie && hasNext && _mission.briefing[_line + 1].frameToStart >= 0 &&
        _movie->next() >= size_t(_mission.briefing[_line + 1].frameToStart)) {
        _typed = float(_text.size());
        _advance = true;
    }
    if (typed()) {
        if (hasNext) {
            // On by itself once the voice-over has been over a second, and
            // the movie, unless it loops, is over (0x47435b).
            const bool voiceDone = !_hadVoice || (!voicePlaying && _sinceVoice > kAfterVoice);
            const bool movieDone = !_movie || _mission.loopMovie || _movie->done(now);
            if (voiceDone && movieDone)
                _advance = true;
            _prompt = kNextPrompt;
        } else if (_buttons.buttons()[kContinue].enabled()) {
            _prompt = kEndPrompt;
            _buttons.button(kContinue).setEnabled(false);
        }
    } else {
        _buttons.button(kContinue).setEnabled(true);
        _prompt = kTypingPrompt;
    }
    if (!_advance)
        return;
    _advance = false;
    // A line half typed is shown whole; one shown goes on to the next
    // (0x474482).
    if (!typed()) {
        _typed = float(_text.size());
    } else if (hasNext) {
        showLine(_line + 1);
        const int frame = _mission.briefing[_line].frameToStart;
        if (_movie && frame >= 0)
            _movie->skipTo(size_t(frame), now);
    } else {
        return;
    }
    if (_mixer && _spaceSound)
        _mixer->play(_spaceSound, audio::SoundType::kEffects);
}

BriefingScreen::Choice BriefingScreen::handleEvent(const Event &ev, uint64_t now) {
    if (_confirm) {
        const std::optional<bool> answer = _confirm->handleEvent(ev, _mixer);
        if (answer && *answer)
            return Choice::kExit;
        if (answer)
            _confirm.reset();
        return Choice::kNone;
    }
    // Space as CONTINUE (0x474dde).
    if (ev.type == EventType::kKeyDown && ev.key == ' ') {
        _advance = true;
        return Choice::kNone;
    }
    if (ev.type == EventType::kMouseWheel)
        _scroll = std::max(0, _scroll - ev.wheelY);
    if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft && clickBar(ev.mouseX, ev.mouseY))
        return Choice::kNone;
    if (ev.type == EventType::kMouseUp)
        _bar.pressed = 0;
    switch (_buttons.handleEvent(ev, _mixer)) {
    case kContinue:
        _advance = true;
        break;
    case kExit:
        _confirm = std::make_unique<ConfirmDialog>(_data, _rules, kExitQuestion, _background.w, _background.h);
        break;
    case kEndBriefing:
        return Choice::kEndBriefing;
    case kReplay:
        replay(now);
        break;
    default:
        break;
    }
    return Choice::kNone;
}

void BriefingScreen::drawText(Surface &screen) {
    if (!_textFont.loaded())
        return;
    int wx = 0;
    int wy = 0;
    int tx = 0;
    int ty = 0;
    int tw = 0;
    int th = 0;
    readPoint(_def, "TextDisplay Window Pos", wx, wy);
    readPoint(_def, "TextDisplay Pos", tx, ty);
    readPoint(_def, "TextDisplay Size", tw, th);
    const int x = wx + tx;
    const int y = wy + ty;
    const int spacing = _def ? int(_def->integer("TextDisplay Text Spacing").value_or(0)) : 0;
    const int lineHeight = _textFont.height() + spacing;
    // The speaker's portrait at the top left, in the middle of its
    // background, and the lines beside it until they're past it
    // (0x472f46).
    int indent = 0;
    int narrow = 0;
    if (_portrait.w) {
        image::blit(screen, _portraitBackground, x, y);
        image::blit(screen, _portrait, x + (_portraitBackground.w - _portrait.w) / 2,
                    y + (_portraitBackground.h - _portrait.h) / 2);
        indent = _portraitBackground.w + 2;
        narrow = lineHeight > 0 ? _portraitBackground.h / lineHeight + 1 : 0;
    }
    // What's been typed, broken into lines as it goes (0x568e60); with the
    // scroll bar, short of it (inferred).
    const int shown = lineHeight > 0 ? std::max(1, (th + spacing) / lineHeight) : 1;
    const std::string_view typed = std::string_view(_text).substr(0, size_t(_typed));
    std::vector<std::string> lines = _textFont.wrap(typed, tw, indent, narrow);
    if (int(lines.size()) > shown && _bar.background.w > 0)
        lines = _textFont.wrap(typed, tw - _bar.background.w, indent, narrow);
    _scroll = std::clamp(_scroll, 0, std::max(0, int(lines.size()) - shown));
    for (int i = 0; i < shown && _scroll + i < int(lines.size()); ++i) {
        const int n = _scroll + i;
        _textFont.draw(screen, x + (n < narrow ? indent : 0), y + i * lineHeight, lines[size_t(n)]);
    }
    // The scroll bar, at Scroll Pos in the text window, only while there's
    // more text than shows (0x47331c): its up arrow at its top, its down
    // arrow at its bottom, the thumb between them by how far it's
    // scrolled (inferred: its layout, 0x4e72a0, isn't traced through).
    _bar.shown = int(lines.size()) > shown && _bar.background.h > 0;
    if (!_bar.shown)
        return;
    int sx = 0;
    int sy = 0;
    readPoint(_def, "Scroll Pos", sx, sy);
    _bar.x = wx + sx;
    _bar.y = wy + sy;
    _bar.most = int(lines.size()) - shown;
    _bar.page = shown;
    image::blit(screen, _bar.background, _bar.x, _bar.y);
    image::blit(screen, _bar.pressed < 0 ? _bar.upPress : _bar.upDefault, _bar.x, _bar.y);
    const image::Image &down = _bar.pressed > 0 ? _bar.downPress : _bar.downDefault;
    image::blit(screen, down, _bar.x, _bar.y + _bar.background.h - down.h);
    const int top = _bar.y + _bar.upDefault.h;
    const int track = _bar.background.h - _bar.upDefault.h - _bar.downDefault.h - _bar.thumb.h;
    image::blit(screen, _bar.thumb, _bar.x, top + (_bar.most > 0 ? track * _scroll / _bar.most : 0));
}

bool BriefingScreen::clickBar(int x, int y) {
    // An arrow a line up or down; above or below the thumb, a window's
    // worth (inferred).
    if (!_bar.shown || x < _bar.x || x >= _bar.x + _bar.background.w || y < _bar.y || y >= _bar.y + _bar.background.h)
        return false;
    const int bottom = _bar.y + _bar.background.h;
    if (y < _bar.y + _bar.upDefault.h) {
        _bar.pressed = -1;
        _scroll = std::max(0, _scroll - 1);
    } else if (y >= bottom - _bar.downDefault.h) {
        _bar.pressed = 1;
        _scroll = std::min(_bar.most, _scroll + 1);
    } else {
        const int track = _bar.background.h - _bar.upDefault.h - _bar.downDefault.h - _bar.thumb.h;
        const int thumbY = _bar.y + _bar.upDefault.h + (_bar.most > 0 ? track * _scroll / _bar.most : 0);
        if (y < thumbY)
            _scroll = std::max(0, _scroll - _bar.page);
        else if (y >= thumbY + _bar.thumb.h)
            _scroll = std::min(_bar.most, _scroll + _bar.page);
    }
    return true;
}

void BriefingScreen::draw(Surface &screen, uint64_t now) {
    update(now);
    image::blit(screen, _background, 0, 0);
    for (const StaticText &t : _texts)
        t.draw(screen, 0, 0);
    for (const RandomNumbers &n : _numbers)
        n.draw(screen, 0, 0);
    // The movie while it plays, its top left at MovieDisplay Pos (0x473710).
    if (_movie && _movie->frame().w && (_mission.loopMovie || !_movie->done(now))) {
        int mx = 0;
        int my = 0;
        readPoint(_def, "MovieDisplay Pos", mx, my);
        image::blit(screen, _movie->frame(), mx, my);
    }
    drawText(screen);
    // The title and the prompt, each centred on its place (0x474780).
    int x = 0;
    int y = 0;
    readPoint(_def, "Mission Name Pos", x, y);
    if (_titleFont.loaded())
        _titleFont.draw(screen, x - _titleFont.width(_title) / 2, y, _title);
    readPoint(_def, "HelpText Pos", x, y);
    if (_promptFont.loaded())
        _promptFont.draw(screen, x - _promptFont.width(_prompt) / 2, y, _prompt);
    _buttons.draw(screen);
    if (_confirm)
        _confirm->draw(screen);
}

} // namespace reluva::stawayteam
