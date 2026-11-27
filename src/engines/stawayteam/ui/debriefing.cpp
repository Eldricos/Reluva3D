// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/debriefing.h"

#include "audio/decode.h"
#include "engines/stawayteam/formats/frame.h"
#include "engines/stawayteam/formats/rds_script.h"
#include "engines/stawayteam/ui/layout.h"
#include "engines/stawayteam/ui/widgets.h"

#include <algorithm>
#include <cstdio>

namespace reluva::stawayteam {

namespace {

// The panels' fading in: each over 0.75 seconds, the mission's from 0.75,
// the objectives' from 1.25, the text's from 1.75 (0x48a898).
constexpr float kFadeTime = 0.75f;
constexpr std::array<float, 3> kFadeFrom = {0.75f, 1.25f, 1.75f};
const char *const kFadeSound = "Interface/Mission Debrief Fade.ogg"; // 0x4895ae
const char *const kAmbience = "Ambient/Bridge Rumble BG.wav";        // 0x48a7e6
// The mission panel's fonts and words (0x48a434...).
const char *const kTitleFont = "Placar Condensed";
const char *const kLineFont = "Placar Blue";
const char *const kSuccess = "MISSION SUCCESSFUL";
const char *const kObjectivesDone = "PRIMARY OBJECTIVES COMPLETE";
const char *const kTimeTaken = "TIME TAKEN: ";
// Its words centred this far left of the panel's middle, the time this
// far right of it (0x48a6a1, 0x48a74d).
constexpr int kMiddleGap = 2;
// The typing's speed until Rules.txt's is read, characters a second.
constexpr float kDefaultSpeed = 60.0f;
// After a line's voice-over, this long before the next comes on its own
// (0x48ad4b, 0x616fb0).
constexpr float kAfterVoice = 0.5f;
// The prompt under the lines (0x63aa24, 0x63bba8, 0x63bb84): centred on
// the panel, but its right end no further than 343 across it, and 6 above
// its foot (0x48a1eb).
const char *const kTypingPrompt = "Press 'Space' to display all text";
const char *const kNextPrompt = "Press 'Space' to continue debriefing";
const char *const kEndPrompt = "Press 'Continue' to end debriefing";
constexpr int kPromptRight = 343;
constexpr int kPromptAbove = 6;

} // namespace

DebriefingScreen::DebriefingScreen(const Archive &data, const TextObject &rules, const Mission &mission,
                                   const GameOptions &options, audio::Mixer *mixer,
                                   std::vector<ObjectiveLine> objectives, float seconds, uint64_t now)
    : _data(data), _def(menuDefinition(rules, "Debriefing Screen")), _mission(mission), _options(options),
      _mixer(mixer), _objectives(std::move(objectives)), _seconds(seconds),
      _background(screenPicture(data, "Debriefing/Background")),
      _missionPanel(screenPicture(data, "Debriefing/Mission Background")),
      _objectivePanel(screenPicture(data, "Debriefing/Objective Background")),
      _textPanel(screenPicture(data, "Debriefing/Debriefing Text Background")),
      _portraitBackground(screenPicture(data, "Debriefing/Portrait Background")), _list(data), _last(now) {
    (void)_titleFont.load(data, kTitleFont);
    (void)_lineFont.load(data, kLineFont);
    (void)_textFont.load(data, readText(_def, "Debriefing Text Font"));
    // CONTINUE and REPLAY, placed on the text's panel (0x4898c7).
    const std::array<const char *, 2> keys = {"ContinueButton", "ReplayButton"};
    const std::array<const char *, 2> pictures = {"Continue", "Replay"};
    for (size_t i = 0; i < keys.size(); ++i) {
        const std::string key = keys[i];
        int x = 0;
        int y = 0;
        int textX = 0;
        int textY = 0;
        readPoint(_def, key + " Pos", x, y);
        readPoint(_def, key + " Text Pos", textX, textY);
        (void)_buttonFonts[i].load(data, readText(_def, key + " Font"));
        Button button(data, "Debriefing", pictures[i], x, y);
        button.setLabel(readText(_def, key + " Text"), &_buttonFonts[i], textX, textY);
        button.setSound(gameSound(data, readText(_def, key + " Press Sound")));
        _buttons.add(std::move(button));
    }
    int wx = 0;
    int wy = 0;
    readPoint(_def, "TextDisplay Window Pos", wx, wy);
    _buttons.setOrigin(wx, wy);
    _fadeSound = gameSound(data, kFadeSound);
    // The bridge's rumble over and over (its loop bit set, 0x5ba02e); the
    // music's the menu's to queue.
    _ambience = gameSound(data, kAmbience);
    if (_mixer && _ambience)
        _ambienceHandle = _mixer->play(_ambience, audio::SoundType::kEffects, audio::kMaxVolume, 0, true);
    showLine(0);
}

DebriefingScreen::~DebriefingScreen() {
    stopSounds();
    if (_mixer && _ambienceHandle != audio::kNoSound)
        _mixer->stop(_ambienceHandle);
}

void DebriefingScreen::stopSounds() {
    if (_mixer && _voice != audio::kNoSound)
        _mixer->stop(_voice);
    _voice = audio::kNoSound;
}

float DebriefingScreen::shown(size_t panel) const {
    return std::clamp((_time - kFadeFrom[panel]) / kFadeTime, 0.0f, 1.0f);
}

void DebriefingScreen::showLine(size_t line) {
    stopSounds();
    _line = line;
    _text.clear();
    _portrait = image::Image();
    _typed = 0.0f;
    _hadVoice = false;
    _sinceVoice = 0.0f;
    if (line >= _mission.debriefing.size())
        return;
    const BriefingLine &l = _mission.debriefing[line];
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
    if (_options.playVoiceovers)
        if (auto voice = voiceOver(_data, l.text)) {
            _hadVoice = true;
            if (_mixer)
                _voice = _mixer->play(voice, audio::SoundType::kSpeech);
        }
}

void DebriefingScreen::update(uint64_t now) {
    const float dt = float(now - std::min(now, _last)) / 1000.0f;
    _last = now;
    const float before = _time;
    _time += dt;
    // Each panel's fade sound as it starts (0x4895ae).
    for (const float from : kFadeFrom)
        if (before < from && _time >= from && _mixer && _fadeSound)
            _mixer->play(_fadeSound, audio::SoundType::kEffects);
    // The lines are typed once their panel's in (inferred).
    if (shown(2) < 1.0f)
        return;
    const double speed = _def ? _def->number("TextDisplay Text Speed").value_or(kDefaultSpeed) : kDefaultSpeed;
    const float typing = std::min(dt, _time - (kFadeFrom[2] + kFadeTime));
    _typed = std::min(float(_text.size()), _typed + typing * float(speed));
    // A line typed goes on to the next by itself once its voice-over's
    // been over half a second, or at once without one (0x48ad32).
    const bool speaking = _mixer && _voice != audio::kNoSound && _mixer->isPlaying(_voice);
    if (_hadVoice && !speaking)
        _sinceVoice += dt;
    if (typed() && (!_hadVoice || (!speaking && _sinceVoice > kAfterVoice)) && _line + 1 < _mission.debriefing.size())
        showLine(_line + 1);
}

DebriefingScreen::Choice DebriefingScreen::handleEvent(const Event &ev, uint64_t /*now*/) {
    // CONTINUE, or Space, shows a line whole, then the next, and after the
    // last goes on, as its prompts say (0x48ad64); REPLAY starts the lines
    // again.
    int chosen = _buttons.handleEvent(ev, _mixer);
    if (ev.type == EventType::kKeyDown && ev.key == ' ')
        chosen = 0;
    if (chosen == 1) {
        showLine(0);
    } else if (chosen == 0) {
        if (!typed())
            _typed = float(_text.size());
        else if (_line + 1 < _mission.debriefing.size())
            showLine(_line + 1);
        else
            return Choice::kContinue;
    }
    return Choice::kNone;
}

void DebriefingScreen::draw(Surface &screen, uint64_t now) {
    update(now);
    image::blit(screen, _background, 0, 0);
    // Each panel faded in and out through the toolkit's brightness, in its
    // six steps (0x48977c).
    const auto bright = [&](size_t panel) { return toolkitScale(shown(panel)); };
    const auto blitPanel = [&](const image::Image &picture, int x, int y, size_t panel) {
        image::blit(screen, bright(panel) < 1.0f ? dimmed(picture, bright(panel)) : picture, x, y);
    };
    // The mission's panel: its success, its primary objectives complete, and
    // the time taken, each centred about its middle at its row (0x48a600).
    int px = 0;
    int py = 0;
    readPoint(_def, "Success Pos", px, py);
    if (shown(0) > 0.0f) {
        const float a = bright(0);
        blitPanel(_missionPanel, px, py, 0);
        const int middle = px + _missionPanel.w / 2 - kMiddleGap;
        const int successY = _def ? int(_def->integer("Mission Success Text").value_or(0)) : 0;
        const int doneY = _def ? int(_def->integer("Objective Complete Text").value_or(0)) : 0;
        const int timeY = _def ? int(_def->integer("Time Text").value_or(0)) : 0;
        if (_titleFont.loaded())
            _titleFont.draw(screen, middle - _titleFont.width(kSuccess) / 2, py + successY, kSuccess, 255, a);
        if (_lineFont.loaded()) {
            _lineFont.draw(screen, middle - _lineFont.width(kObjectivesDone) / 2, py + doneY, kObjectivesDone, 255, a);
            _lineFont.draw(screen, middle - _lineFont.width(kTimeTaken), py + timeY, kTimeTaken, 255, a);
            // Hours, minutes and seconds, two figures each (0x5c4700).
            const int s = int(_seconds) % 86400;
            char time[16];
            std::snprintf(time, sizeof time, "%02d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
            _lineFont.draw(screen, middle + 2 * kMiddleGap, py + timeY, time, 255, a);
        }
    }
    // The objectives' panel, the list in it (0x489630).
    readPoint(_def, "Objective Pos", px, py);
    if (shown(1) > 0.0f) {
        blitPanel(_objectivePanel, px, py, 1);
        int lx = 0;
        int ly = 0;
        int lw = 0;
        int lh = 0;
        readPoint(_def, "ObjectiveText Pos", lx, ly);
        readPoint(_def, "ObjectiveText Size", lw, lh);
        const int spacing = _def ? int(_def->integer("Objective Text Spacing").value_or(0)) : 0;
        _list.draw(screen, px + lx, py + ly, lw, spacing, _objectives, bright(1));
    }
    // The text's panel: the speaker's portrait, the lines typed beside it
    // as the briefing's, and the buttons (0x4897b0).
    readPoint(_def, "TextDisplay Window Pos", px, py);
    if (shown(2) > 0.0f) {
        blitPanel(_textPanel, px, py, 2);
        int tx = 0;
        int ty = 0;
        readPoint(_def, "TextDisplay Text Pos", tx, ty);
        const int x = px + tx;
        const int y = py + ty;
        int indent = 0;
        int narrow = 0;
        const int spacing = _def ? int(_def->integer("TextDisplay Text Spacing").value_or(0)) : 0;
        const int lineHeight = _textFont.height() + spacing;
        if (_portrait.w) {
            blitPanel(_portraitBackground, x, y, 2);
            blitPanel(_portrait, x + (_portraitBackground.w - _portrait.w) / 2,
                      y + (_portraitBackground.h - _portrait.h) / 2, 2);
            indent = _portraitBackground.w + 2;
            narrow = lineHeight > 0 ? _portraitBackground.h / lineHeight + 1 : 0;
        }
        // The words across the panel, its margin either side (0x48a1a1).
        const int width = _textPanel.w - 2 * tx;
        if (_textFont.loaded() && lineHeight > 0) {
            const auto lines = _textFont.wrap(std::string_view(_text).substr(0, size_t(_typed)), width, indent, narrow);
            const int rows = std::max(1, (_textPanel.h - 2 * ty) / lineHeight);
            const int first = std::max(0, int(lines.size()) - rows);
            for (int n = first; n < int(lines.size()); ++n)
                _textFont.draw(screen, x + (n < narrow ? indent : 0), y + (n - first) * lineHeight, lines[size_t(n)],
                               255, bright(2));
        }
        if (_textFont.loaded()) {
            const char *prompt = !typed()                                 ? kTypingPrompt
                                 : _line + 1 < _mission.debriefing.size() ? kNextPrompt
                                                                          : kEndPrompt;
            const int half = _textFont.width(prompt) / 2;
            const int middle = std::min(_textPanel.w / 2, kPromptRight - half);
            _textFont.draw(screen, px + middle - half, py + _textPanel.h - _textFont.height() - kPromptAbove, prompt,
                           255, bright(2));
        }
        if (shown(2) >= 1.0f)
            _buttons.draw(screen);
    }
}

} // namespace reluva::stawayteam
