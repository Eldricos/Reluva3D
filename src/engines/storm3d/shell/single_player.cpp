// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/storm3d/shell/single_player.h"

#include "common/text.h"

#include <algorithm>

namespace reluva::storm3d {

namespace {

// Where the game's frames have them (the pictures matched against them).
constexpr int kTitleX = 256;
constexpr int kTitleY = 12;
constexpr int kPreviousX = 28;
constexpr int kPreviousY = 451;
constexpr int kLoadX = 507;
constexpr int kLoadY = 452;
// The mission panel, a dialog the game moves to (224, 46), and what's in it.
constexpr int kPanelX = 224;
constexpr int kPanelY = 46;
constexpr int kHeadingX = 243;
constexpr int kHeadingY = 75;
constexpr int kBarX = 234;
constexpr int kBarY = 146;
// The frames show one mission open: the next bars' spacing is guessed.
constexpr int kBarStep = 20;
constexpr int kOkX = 228;
constexpr int kCancelX = 328;
constexpr int kButtonsY = 386;
// Arial Bold, at sizes the game asks GDI for (12, 14, 16 and 20 pixels):
// which is which is fitted to the frames' text.
constexpr float kButtonText = 12;
constexpr float kTooltipText = 16;
constexpr Color kButtonColour{0, 0, 0};
constexpr Color kTooltipColour{255, 255, 255};
// Six map names a campaign in mshell.set.
constexpr size_t kCampaignLines = 6;

} // namespace

const std::vector<SinglePlayerScreen::Race> &SinglePlayerScreen::races() {
    // Each panel 280 x 190, its movie matched against the game's frame.
    static const std::vector<Race> kRaces = {
        {"Fed", "federation", 0, 30},
        {"Kling", "klingon", 360, 30},
        {"Rom", "romulan", 0, 250},
        {"Borg", "borg", 360, 250},
    };
    return kRaces;
}

int SinglePlayerScreen::raceAt(int x, int y) {
    constexpr int kWidth = 280;
    constexpr int kHeight = 190;
    for (size_t i = 0; i < races().size(); ++i) {
        const Race &r = races()[i];
        if (x >= r.x && y >= r.y && x < r.x + kWidth && y < r.y + kHeight)
            return int(i);
    }
    return -1;
}

std::vector<std::vector<std::string>> SinglePlayerScreen::campaignsOf(std::string_view mshell) {
    std::vector<std::vector<std::string>> campaigns;
    size_t line = 0;
    for (const std::string &raw : text::split(mshell, '\n')) {
        std::string name = raw;
        while (!name.empty() && (name.back() == '\r' || name.back() == ' ' || name.back() == '\t'))
            name.pop_back();
        if (name.empty())
            continue;
        if (line++ % kCampaignLines == 0)
            campaigns.emplace_back();
        if (!text::equalsIgnoreCase(name, "none"))
            campaigns.back().push_back(name);
    }
    return campaigns;
}

Error SinglePlayerScreen::load(const GameFiles &files, uint64_t millis) {
    if (Error err = loadPicture(files, "bitmaps/single/singlebk.bmp", _background))
        return err;
    for (const auto &[name, picture] :
         {std::pair<const char *, image::Image *>{"SPtitle", &_title}, {"SPpanel", &_panel}, {"SPselect", &_heading}})
        if (Error err = loadPicture(files, std::string("bitmaps/sp/") + name + ".bmp", *picture))
            return err;
    for (size_t i = 0; i < _bar.size(); ++i)
        if (Error err = loadPicture(files, "bitmaps/sp/SPgame" + std::to_string(i + 1) + ".bmp", _bar[i]))
            return err;
    if (Error err = _previous.load(files, "bitmaps/sp", "SPprev", kPreviousX, kPreviousY))
        return err;
    if (Error err = _load.load(files, "bitmaps/sp", "SPload", kLoadX, kLoadY))
        return err;
    if (Error err = _ok.load(files, "bitmaps/sp", "SPok", kOkX, kButtonsY))
        return err;
    if (Error err = _cancel.load(files, "bitmaps/sp", "SPok", kCancelX, kButtonsY))
        return err;
    std::vector<uint8_t> data;
    if (Error err = files.read("label.map", data))
        return err;
    if (Error err = _text.parse(std::string_view(reinterpret_cast<const char *>(data.data()), data.size())))
        return Error::fail("label.map: " + err.message);
    if (Error err = files.read("mshell.set", data))
        return err;
    _campaigns = campaignsOf(std::string_view(reinterpret_cast<const char *>(data.data()), data.size()));
    _rest.assign(races().size(), PanelMovie());
    _lit.assign(races().size(), PanelMovie());
    for (size_t i = 0; i < races().size(); ++i) {
        const std::string movie = std::string("animations/") + races()[i].movie;
        if (!_rest[i].open(files, movie + "01.bik"))
            _rest[i].restart(millis);
        if (!_lit[i].open(files, movie + "02.bik"))
            _lit[i].restart(millis);
    }
    _font.load();
    _hover = _pressed = Target{};
    _panelRace = -1;
    return Error::success();
}

std::vector<std::string> SinglePlayerScreen::openMissions(int race) const {
    if (race < 0 || size_t(race) >= _campaigns.size() || !open(race))
        return {};
    const std::vector<std::string> &all = _campaigns[size_t(race)];
    // A campaign finished has all its missions open.
    const size_t count = race < _campaignsDone ? all.size() : std::min(all.size(), size_t(std::max(_missionsOpen, 1)));
    return {all.begin(), all.begin() + long(count)};
}

int SinglePlayerScreen::barY(int i) const {
    return kBarY + i * kBarStep;
}

SinglePlayerScreen::Target SinglePlayerScreen::targetAt(int x, int y) const {
    auto inside = [x, y](int rx, int ry, const image::Image &p) {
        return x >= rx && y >= ry && x < rx + p.w && y < ry + p.h;
    };
    if (_panelRace >= 0) {
        // The panel is a dialog over the screen: nothing else answers.
        if (_ok.contains(x, y))
            return {Target::kOk, 0};
        if (_cancel.contains(x, y))
            return {Target::kCancel, 0};
        const std::vector<std::string> missions = openMissions(_panelRace);
        for (size_t i = 0; i < missions.size(); ++i)
            if (inside(kBarX, barY(int(i)), _bar[0]))
                return {Target::kMission, int(i)};
        return {};
    }
    if (_previous.contains(x, y))
        return {Target::kPrevious, 0};
    if (_load.contains(x, y))
        return {Target::kLoad, 0};
    if (const int race = raceAt(x, y); race >= 0)
        return {Target::kRace, race};
    return {};
}

bool SinglePlayerScreen::pointerAt(int x, int y, uint64_t millis) {
    const Target now = targetAt(x, y);
    if (now == _hover)
        return false;
    // An open race's panel plays its second movie under the pointer, as
    // the main menu's do (its emblem lit in the game's frame).
    if (now.kind == Target::kRace && open(now.index) && _lit[size_t(now.index)].isOpen())
        _lit[size_t(now.index)].restart(millis);
    if (_hover.kind == Target::kRace && _rest[size_t(_hover.index)].isOpen())
        _rest[size_t(_hover.index)].restart(millis);
    _hover = now;
    return now.kind != Target::kNone;
}

void SinglePlayerScreen::press(int x, int y) {
    _pressed = targetAt(x, y);
}

SingleChoice SinglePlayerScreen::release(int x, int y, uint64_t millis) {
    const Target at = targetAt(x, y);
    const Target pressed = _pressed;
    _pressed = Target{};
    if (at.kind == Target::kNone || !(at == pressed))
        return SingleChoice::kNone;
    switch (at.kind) {
    case Target::kPrevious:
        return SingleChoice::kPreviousMenu;
    case Target::kLoad:
        return SingleChoice::kLoadGame;
    case Target::kRace:
        if (open(at.index) && !openMissions(at.index).empty()) {
            _panelRace = at.index;
            _chosen = 0;
            _hover = Target{};
        }
        return SingleChoice::kNone;
    case Target::kMission:
        _chosen = at.index;
        return SingleChoice::kNone;
    case Target::kOk:
        _mission = openMissions(_panelRace)[size_t(_chosen)];
        _panelRace = -1;
        return SingleChoice::kStartMission;
    case Target::kCancel:
        _panelRace = -1;
        for (size_t i = 0; i < races().size(); ++i)
            if (_rest[i].isOpen())
                _rest[i].restart(millis);
        return SingleChoice::kNone;
    default:
        return SingleChoice::kNone;
    }
}

void SinglePlayerScreen::draw(Surface &screen, uint64_t millis) {
    drawPicture(screen, _background, 0, 0, false);
    for (size_t i = 0; i < races().size(); ++i) {
        // The race chosen keeps its lit movie, stopped, under the panel.
        const bool lit =
            (_hover.kind == Target::kRace && _hover.index == int(i) && open(int(i))) || (_panelRace == int(i));
        PanelMovie &movie = lit && _lit[i].isOpen() ? _lit[i] : _rest[i];
        // The races' movies stop while the mission panel is up.
        if (!movie.isOpen() || (_panelRace < 0 && !movie.update(millis)) || movie.rgba().empty())
            continue;
        drawPixels(screen, movie.rgba().data(), movie.width(), movie.height(), races()[i].x, races()[i].y, false);
    }
    drawPicture(screen, _title, kTitleX, kTitleY, false);
    _previous.draw(screen, _hover.kind == Target::kPrevious, _pressed.kind == Target::kPrevious, false);
    _load.draw(screen, _hover.kind == Target::kLoad, _pressed.kind == Target::kLoad, false);
    // A race not open yet says the first campaign there is to finish.
    if (_hover.kind == Target::kRace && !open(_hover.index) && _panelRace < 0) {
        const Race &r = races()[size_t(_campaignsDone)];
        const Race &under = races()[size_t(_hover.index)];
        const std::string tip = _text.text("single_player", std::string(r.name) + "_incomplete");
        _font.drawCentred(screen, under.x, under.y, 280, 190, tip, kTooltipText, kTooltipColour);
    }
    if (_panelRace < 0)
        return;
    drawPicture(screen, _panel, kPanelX, kPanelY, false);
    drawPicture(screen, _heading, kHeadingX, kHeadingY, false);
    const std::vector<std::string> missions = openMissions(_panelRace);
    for (size_t i = 0; i < missions.size(); ++i) {
        const bool lit = _hover.kind == Target::kMission && _hover.index == int(i);
        const image::Image &bar = int(i) == _chosen && missions.size() > 1 ? _bar[2] : lit ? _bar[1] : _bar[0];
        drawPicture(screen, bar, kBarX, barY(int(i)), false);
        const std::string name = _text.text("mission_select", missions[i], missions[i]);
        _font.drawCentred(screen, kBarX, barY(int(i)), bar.w, bar.h, name, kButtonText, kButtonColour);
    }
    for (const auto &[button, kind, key] :
         {std::tuple<ShellButton *, Target::Kind, const char *>{&_ok, Target::kOk, "ok_button"},
          {&_cancel, Target::kCancel, "cancel_button"}}) {
        button->draw(screen, _hover.kind == kind, _pressed.kind == kind, false);
        _font.drawCentred(screen, button->x, button->y, button->looks[0].w, button->looks[0].h,
                          _text.text("mission_select", key), kButtonText, kButtonColour);
    }
}

} // namespace reluva::storm3d
