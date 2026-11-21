// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/ui/team_selection.h"

#include "engines/stawayteam/formats/binary_object.h"
#include "engines/stawayteam/formats/rds_script.h"
#include "engines/stawayteam/ui/layout.h"
#include "engines/stawayteam/ui/main_menu.h"
#include "engines/stawayteam/ui/widgets.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace reluva::stawayteam {

namespace {

const char *const kExitQuestion = "Are you sure you want to quit the game and exit to the main menu?"; // 0x63a9e0
const char *const kEmptyTeam = "Select at least one team member before continuing.";                   // 0x64ae70
const char *const kMissingItems = "The selected team is missing one or more required items.  Click on a RED "
                                  "required item to cycle through the team members with that item.";   // 0x64aea4
const char *const kRequired = "%s is a required team member for this mission, and cannot be removed."; // 0x64ae28
// A message's time on the screen: the 50 the program sets (0x5ebc1f),
// taken as seconds (inferred).
constexpr uint64_t kMessageMs = 50000;
// An attribute's value that fills its bar (inferred: the roster's are all
// under it).
constexpr int kAttributeMax = 100;

// The screen's buttons, in ButtonGroup order: Rules.txt's key and the
// pictures' name (0x5ea31e...).
struct ButtonSpec {
    const char *key;
    const char *picture;
    bool rightAligned;
};
constexpr std::array<ButtonSpec, 7> kButtons = {{{"ExitButton", "Exit", false},
                                                 {"BriefingButton", "Briefing", false},
                                                 {"ContinueButton", "Continue", true},
                                                 {"AddMemberButton", "Add Member", false},
                                                 {"RemoveMemberButton", "Remove Member", false},
                                                 {"InventoryButton", "Inventory", false},
                                                 {"CharacterInfoButton", "Character", false}}};
enum ButtonIndex : int { kExit, kBriefing, kStart, kAdd, kRemove, kInventory, kOfficers };

image::Image cachePicture(const Archive &data, const std::string &name) {
    std::vector<uint8_t> bytes;
    Frame frame;
    if (data.read("Cache/" + name + ".frm16", bytes) || readFrame(bytes.data(), bytes.size(), frame))
        return {};
    return frameImage(frame);
}

void blitBy(Surface &screen, const Animation::Picture *p, int x, int y, float brightness = 1.0f) {
    if (!p)
        return;
    if (brightness < 1.0f)
        image::blit(screen, dimmed(p->image, brightness), x - p->originX, y - p->originY);
    else
        image::blit(screen, p->image, x - p->originX, y - p->originY);
}

} // namespace

Error Animation::load(const Archive &data, const std::string &name, const std::string &sequence) {
    const std::string path = "Cache/" + std::string(sequence.empty() ? "Sequences/" : "Models/") + name +
                             (sequence.empty() ? ".seq16" : ".mdl16");
    std::vector<uint8_t> bytes;
    if (Error err = data.read(path, bytes))
        return err;
    BinaryObject object;
    if (Error err = readBinaryObject(bytes.data(), bytes.size(), "", object))
        return Error::fail(path + ": " + err.message);
    const BinaryObject *seq = &object;
    if (!sequence.empty()) {
        seq = nullptr;
        for (const NamedSequence &s : object.sequences)
            if (s.name == sequence && s.sequence)
                seq = s.sequence.get();
        if (!seq)
            return Error::fail(path + " has no " + sequence);
    }
    if (!seq->sequence)
        return Error::fail(path + ": not a sequence");
    fps = float(seq->object.number("Frames Per Second").value_or(15.0));
    frames.clear();
    for (const auto &frame : seq->sequence->frames)
        if (frame)
            frames.push_back({frameImage(*frame), frame->originX, frame->originY});
    return Error::success();
}

const Animation::Picture *Animation::at(uint64_t ms, int offset) const {
    if (frames.empty())
        return nullptr;
    const auto n = uint64_t(frames.size());
    return &frames[size_t((uint64_t(double(ms) * fps / 1000.0) + uint64_t(offset)) % n)];
}

TeamSelectionScreen::TeamSelectionScreen(const Archive &data, const TextObject &rules, const Mission &mission,
                                         std::vector<TeamMember> roster)
    : _data(data), _rules(rules), _def(menuDefinition(rules, "Team Selection")), _mission(mission),
      _roster(std::move(roster)), _background(screenPicture(data, "Team Selection/Background")) {
    _pictures.resize(_roster.size());
    _fields = loadFields(data);
    // The menu's font (Rules.txt's Menu Font), and the red for what's missing.
    (void)_font.load(data, "Trek Small");
    (void)_redFont.load(data, "Trek Small Red");
    _texts = readStaticTexts(data, _def);

    _buttonFonts.resize(kButtons.size());
    for (size_t i = 0; i < kButtons.size(); ++i) {
        const std::string key = kButtons[i].key;
        int x = 0;
        int y = 0;
        readPoint(_def, key + " Pos", x, y);
        Button button(data, "Team Selection", kButtons[i].picture, x, y);
        int textX = 0;
        int textY = 0;
        readPoint(_def, key + " Text Pos", textX, textY);
        (void)_buttonFonts[i].load(data, readText(_def, key + " Font"));
        button.setLabel(readText(_def, key + " Text"), &_buttonFonts[i], textX, textY);
        // START MISSION's label is on its right (0x5eac5d).
        if (kButtons[i].rightAligned)
            button.alignLabelRight();
        button.setSound(gameSound(data, readText(_def, key + " Press Sound")));
        _buttons.add(std::move(button));
    }
    _buttons.button(kOfficers).setChecked(true);

    _characterBackground = screenPicture(data, "Team Selection/Character Background");
    _inventoryBackground = screenPicture(data, "Team Selection/Inventory Background");
    _portraitBackground = screenPicture(data, "Team Selection/Portrait Background");
    _attributeBar = screenPicture(data, "Team Selection/Attribute Bar");
    _rosterBackground = screenPicture(data, "Roster Window/Team Roster Background");
    _iconBackground = screenPicture(data, "Roster Window/Icon Background");
    _iconSelected = screenPicture(data, "Roster Window/Icon Select Background");

    // The roster's field tabs, Class Button Spacing apart (0x54f429).
    int fx = 0;
    int fy = 0;
    readPoint(_def, "ClassButton Pos", fx, fy);
    const int spacing = _def ? int(_def->integer("Class Button Spacing").value_or(0)) : 0;
    for (size_t i = 0; i < _fields.size(); ++i) {
        Button tab(data, "Roster Window", _fields[i], fx + int(i) * spacing, fy);
        tab.setSound(gameSound(data, "Interface/Roster Window/" + _fields[i] + " Tab.wav"));
        tab.setChecked(i == 0);
        _fieldButtons.add(std::move(tab));
    }
    int rx = 0;
    int ry = 0;
    readPoint(_def, "RosterDisplay Pos", rx, ry);
    _fieldButtons.setOrigin(rx, ry);

    // The transporter: its background's sequence, places and texts
    // (Transporter Backgrounds/<name>.txt), and the selection's marker
    // (Interface/Selection, 0x5e6c73).
    std::vector<uint8_t> bytes;
    TextObject background;
    if (!data.read("Transporter Backgrounds/" + _mission.transporterBackground + ".txt", bytes) &&
        !readTextObject(std::string(bytes.begin(), bytes.end()), background)) {
        (void)_transporter.load(data, readText(&background, "Background Sequence"));
        if (const TextObject *places = background.object("Position"))
            for (const TextField *item : places->items()) {
                const size_t comma = item->value.find(',');
                _places.emplace_back(std::atoi(item->value.c_str()),
                                     comma == std::string::npos ? 0 : std::atoi(item->value.c_str() + comma + 1));
            }
        _transporterTexts = readStaticTexts(data, &background);
    }
    (void)_marker.load(data, "Interface/Selection", "Idle");

    // The team to start with: who the mission says (0x5ea2f0).
    for (const std::string &id : _mission.whoShouldStart)
        for (size_t i = 0; i < _roster.size(); ++i)
            if (_roster[i].id == id && int(_team.size()) < _mission.maxMembers)
                _team.push_back(i);
    updateButtons();
}

TeamSelectionScreen::~TeamSelectionScreen() = default;

TeamSelectionScreen::Pictures &TeamSelectionScreen::pictures(size_t member) {
    Pictures &p = _pictures[member];
    if (!p.loaded) {
        p.loaded = true;
        const TeamMember &m = _roster[member];
        p.portrait = cachePicture(_data, "Character/Character Portraits/" + m.portrait);
        p.icon = cachePicture(_data, "Character/Character Icons/" + m.headIcon);
        (void)p.figure.load(_data, "Characters/" + m.name());
    }
    return p;
}

std::vector<size_t> TeamSelectionScreen::rosterShown() const {
    std::vector<size_t> out;
    if (_field >= _fields.size())
        return out;
    for (size_t i = 0; i < _roster.size(); ++i)
        if (_roster[i].available && _roster[i].field == _fields[_field] &&
            std::find(_team.begin(), _team.end(), i) == _team.end())
            out.push_back(i);
    return out;
}

bool TeamSelectionScreen::required(size_t member) const {
    return std::find(_mission.whoShouldStart.begin(), _mission.whoShouldStart.end(), _roster[member].id) !=
           _mission.whoShouldStart.end();
}

void TeamSelectionScreen::showMessage(const std::string &text, uint64_t now) {
    _message = text;
    _messageUntil = now + kMessageMs;
}

void TeamSelectionScreen::updateButtons() {
    const bool chosenFree =
        _rosterChosen >= 0 && std::find(_team.begin(), _team.end(), size_t(_rosterChosen)) == _team.end();
    _buttons.button(kAdd).setEnabled(chosenFree && int(_team.size()) < _mission.maxMembers);
    _buttons.button(kRemove).setEnabled(_placeChosen >= 0 && _placeChosen < int(_team.size()));
}

int TeamSelectionScreen::rosterAt(int x, int y) const {
    int rx = 0;
    int ry = 0;
    int ix = 0;
    int iy = 0;
    int ox = 0;
    int oy = 0;
    readPoint(_def, "RosterDisplay Pos", rx, ry);
    readPoint(_def, "RosterIcon Pos", ix, iy);
    readPoint(_def, "RosterIconOffset Pos", ox, oy);
    const int step = _def ? int(_def->integer("Roster Icon Y Spacing").value_or(0)) : 0;
    const int perRow = std::max(1, _def ? int(_def->integer("Roster Icon Width").value_or(1)) : 1);
    const auto shown = rosterShown();
    for (size_t i = 0; i < shown.size(); ++i) {
        const int cx = rx + ix + ox + int(i) % perRow * step;
        const int cy = ry + iy + oy + int(i) / perRow * step;
        if (x >= cx && y >= cy && x < cx + _iconBackground.w && y < cy + _iconBackground.h)
            return int(shown[i]);
    }
    return -1;
}

int TeamSelectionScreen::placeAt(int x, int y) const {
    int tx = 0;
    int ty = 0;
    int mx = 0;
    int my = 0;
    readPoint(_def, "TeamDisplay Pos", tx, ty);
    readPoint(_def, "TeamSelectionMemberOffset Pos", mx, my);
    // A place answers within its marker's reach, about it (inferred).
    const int limit = std::min(_mission.maxMembers, int(_places.size()));
    for (int k = 0; k < limit; ++k) {
        const int px = tx + _places[size_t(k)].first + mx;
        const int py = ty + _places[size_t(k)].second + my;
        if (std::abs(x - px) <= 14 && y >= py - 40 && y <= py + 24)
            return k;
    }
    return -1;
}

TeamSelectionScreen::Choice TeamSelectionScreen::handleEvent(const Event &ev, audio::Mixer *mixer, uint64_t now) {
    if (_confirm) {
        const std::optional<bool> answer = _confirm->handleEvent(ev, mixer);
        if (answer && *answer)
            return Choice::kExit;
        if (answer)
            _confirm.reset();
        return Choice::kNone;
    }
    if (ev.type == EventType::kMouseMove) {
        _rosterHover = rosterAt(ev.mouseX, ev.mouseY);
        _placeHover = placeAt(ev.mouseX, ev.mouseY);
    }
    if (ev.type == EventType::kMouseDown && ev.button == MouseButton::kLeft) {
        if (const int member = rosterAt(ev.mouseX, ev.mouseY); member >= 0) {
            _rosterChosen = member;
            _placeChosen = -1;
        } else if (const int place = placeAt(ev.mouseX, ev.mouseY); place >= 0) {
            _placeChosen = place;
            _rosterChosen = -1;
        }
        updateButtons();
    }
    if (const int tab = _fieldButtons.handleEvent(ev, mixer); tab >= 0) {
        _field = size_t(tab);
        for (size_t i = 0; i < _fields.size(); ++i)
            _fieldButtons.button(i).setChecked(i == _field);
        _rosterChosen = -1;
        updateButtons();
    }
    const int chosen = _buttons.handleEvent(ev, mixer);
    switch (chosen) {
    case kExit:
        _confirm = std::make_unique<ConfirmDialog>(_data, _rules, kExitQuestion, _background.w, _background.h);
        break;
    case kStart: {
        // No one, or an item the mission needs that none of them has,
        // keeps the team here (0x5ebad6).
        if (_team.empty()) {
            showMessage(kEmptyTeam, now);
            break;
        }
        for (const std::string &item : _mission.items)
            if (std::none_of(_team.begin(), _team.end(), [&](size_t m) { return _roster[m].hasItem(item); })) {
                showMessage(kMissingItems, now);
                return Choice::kNone;
            }
        return Choice::kStartMission;
    }
    case kAdd:
        if (_rosterChosen >= 0 && int(_team.size()) < _mission.maxMembers) {
            _team.push_back(size_t(_rosterChosen));
            _placeChosen = int(_team.size()) - 1;
            _rosterChosen = -1;
        }
        break;
    case kRemove:
        if (_placeChosen >= 0 && _placeChosen < int(_team.size())) {
            const size_t member = _team[size_t(_placeChosen)];
            if (required(member)) {
                char text[256];
                std::snprintf(text, sizeof text, kRequired, _roster[member].name().c_str());
                showMessage(text, now);
            } else {
                _team.erase(_team.begin() + _placeChosen);
                _placeChosen = -1;
            }
        }
        break;
    case kInventory:
    case kOfficers:
        // The two tabs, one panel shown at a time (0x5eb9ea).
        _inventoryShown = chosen == kInventory;
        _buttons.button(kInventory).setChecked(_inventoryShown);
        _buttons.button(kOfficers).setChecked(!_inventoryShown);
        break;
    default:
        break;
    }
    updateButtons();
    return Choice::kNone;
}

void TeamSelectionScreen::drawCharacter(Surface &screen, size_t member) {
    int cx = 0;
    int cy = 0;
    readPoint(_def, "CharacterDisplay Pos", cx, cy);
    const TeamMember &m = _roster[member];
    Pictures &p = pictures(member);
    // The portrait in the middle of its background (0x5e86a2).
    int px = 0;
    int py = 0;
    readPoint(_def, "PortraitDisplay Pos", px, py);
    image::blit(screen, _portraitBackground, cx + px, cy + py);
    image::blit(screen, p.portrait, cx + px + (_portraitBackground.w - p.portrait.w) / 2,
                cy + py + (_portraitBackground.h - p.portrait.h) / 2);
    if (!_font.loaded())
        return;
    // A line each, its value after its label (0x5e8756).
    int ix = 0;
    int iy = 0;
    readPoint(_def, "InfoDisplay Pos", ix, iy);
    const std::array<std::pair<const char *, std::string>, 5> info = {
        {{"Name: ", m.name()}, {"Rank: ", m.rank}, {"Race: ", m.race}, {"Gender: ", m.gender}, {"Field: ", m.field}}};
    for (size_t i = 0; i < info.size(); ++i) {
        const int y = cy + iy + int(i) * _font.height();
        _font.draw(screen, cx + ix, y, info[i].first);
        _font.draw(screen, cx + ix + _font.width(info[i].first), y, info[i].second);
    }
    // The attributes: the bar as long as the value, its name on it and the
    // value at its end (0x5e8b40).
    int ax = 0;
    int ay = 0;
    readPoint(_def, "AttributeDisplay Pos", ax, ay);
    const int width = std::min(_def ? int(_def->integer("AttributeDisplay Width").value_or(0)) : 0, _attributeBar.w);
    const int gap = _def ? int(_def->integer("AttributeDisplay Spacing").value_or(0)) : 0;
    int y = cy + ay + (_attributeBar.h - _font.height()) / 2;
    for (const TeamMember::Attribute &a : m.attributes) {
        const image::Image &bar = _attributeBar;
        const int shown = std::clamp(width * a.value / kAttributeMax, 0, bar.w);
        image::Image cut;
        cut.w = shown;
        cut.h = bar.h;
        cut.rgba.resize(size_t(shown) * size_t(bar.h) * 4);
        for (int r = 0; r < bar.h; ++r)
            std::copy_n(bar.pixel(0, r), size_t(shown) * 4, cut.pixel(0, r));
        image::blit(screen, cut, cx + ax, y);
        _font.draw(screen, cx + ax + 2, y, a.name);
        const std::string value = std::to_string(a.value);
        _font.draw(screen, cx + ax + width - _font.width(value), y, value);
        y += _attributeBar.h + gap;
    }
    // The description, the character's script's entry (0x5e8cc8).
    std::vector<uint8_t> bytes;
    if (!_data.read("Levels/" + m.characterScript + ".RDS", bytes)) {
        const RdsScript script = readRdsScript(std::string(bytes.begin(), bytes.end()));
        if (const std::string *text = script.find(m.characterText)) {
            int dx = 0;
            int dy = 0;
            int dw = 0;
            int dh = 0;
            readPoint(_def, "CharacterDescription Pos", dx, dy);
            readPoint(_def, "CharacterDescription Size", dw, dh);
            _font.drawWrapped(screen, cx + dx, cy + dy, dw, *text, Font::Align::kLeft, 2);
        }
    }
}

void TeamSelectionScreen::drawRoster(Surface &screen) {
    int rx = 0;
    int ry = 0;
    readPoint(_def, "RosterDisplay Pos", rx, ry);
    image::blit(screen, _rosterBackground, rx, ry);
    _fieldButtons.draw(screen);
    int ix = 0;
    int iy = 0;
    int ox = 0;
    int oy = 0;
    int tx = 0;
    int ty = 0;
    readPoint(_def, "RosterIcon Pos", ix, iy);
    readPoint(_def, "RosterIconOffset Pos", ox, oy);
    readPoint(_def, "RosterTitle Pos", tx, ty);
    ix += rx;
    iy += ry;
    const int step = _def ? int(_def->integer("Roster Icon Y Spacing").value_or(0)) : 0;
    const int perRow = std::max(1, _def ? int(_def->integer("Roster Icon Width").value_or(1)) : 1);
    // The name of the officer under the mouse, or chosen, centred on its
    // place (0x5e6080).
    const int named = _rosterHover >= 0 ? _rosterHover : _rosterChosen;
    if (named >= 0 && _font.loaded()) {
        const std::string name = _roster[size_t(named)].name();
        _font.draw(screen, ix + tx - _font.width(name) / 2, iy + ty, name);
    }
    // Each icon on its background, in the middle of it (0x5e60e6).
    const auto shown = rosterShown();
    for (size_t i = 0; i < shown.size(); ++i) {
        const int cx = ix + ox + int(i) % perRow * step;
        const int cy = iy + oy + int(i) / perRow * step;
        const image::Image &bg = int(shown[i]) == _rosterChosen ? _iconSelected : _iconBackground;
        image::blit(screen, bg, cx, cy);
        const image::Image &icon = pictures(shown[i]).icon;
        image::blit(screen, icon, cx + (bg.w - icon.w) / 2, cy + (bg.h - icon.h) / 2);
    }
    // The field's name, at the end of the third column and second row
    // (0x5e61e9).
    if (_font.loaded() && _field < _fields.size())
        _font.draw(screen, ix + ox + 3 * step - _font.width(_fields[_field]), iy + oy + 2 * step - 4 - _font.height(),
                   _fields[_field]);
}

void TeamSelectionScreen::drawTransporter(Surface &screen, uint64_t now) {
    int tx = 0;
    int ty = 0;
    readPoint(_def, "TeamDisplay Pos", tx, ty);
    int bx = 0;
    int by = 0;
    readPoint(_def, "TeamTransporter Pos", bx, by);
    blitBy(screen, _transporter.at(now), tx + bx, ty + by);
    for (const StaticText &t : _transporterTexts)
        t.draw(screen, tx, ty);
    // The name of the officer under the mouse, or chosen, over the picture.
    int titleX = 0;
    int titleY = 0;
    readPoint(_def, "Team Selection Title Pos", titleX, titleY);
    const int named = _placeHover >= 0 && _placeHover < int(_team.size())     ? int(_team[size_t(_placeHover)])
                      : _placeChosen >= 0 && _placeChosen < int(_team.size()) ? int(_team[size_t(_placeChosen)])
                                                                              : -1;
    if (named >= 0 && _font.loaded()) {
        const std::string name = _roster[size_t(named)].name();
        _font.draw(screen, tx + titleX - _font.width(name) / 2, ty + titleY, name);
    }
    int mx = 0;
    int my = 0;
    int sx = 0;
    int sy = 0;
    readPoint(_def, "TeamSelectionMemberOffset Pos", mx, my);
    readPoint(_def, "TeamSelectionMemberSelectOffset Pos", sx, sy);
    // Every place's marker at half brightness but the chosen one's
    // (0x5e7c0f); then the team, from the last, the chosen place's marker
    // whole, each officer's figure on their place (0x5e7d37).
    const int limit = std::min(_mission.maxMembers, int(_places.size()));
    for (int k = 0; k < limit; ++k)
        if (k != _placeChosen)
            blitBy(screen, _marker.at(now), tx + _places[size_t(k)].first + mx + sx,
                   ty + _places[size_t(k)].second + my + sy, 0.5f);
    for (int k = std::min(int(_team.size()), limit) - 1; k >= 0; --k) {
        const int px = tx + _places[size_t(k)].first + mx;
        const int py = ty + _places[size_t(k)].second + my;
        if (k == _placeChosen)
            blitBy(screen, _marker.at(now), px + sx, py + sy);
        // Each a frame on from the one before, so they don't move as one
        // (inferred, 0x5e797d).
        blitBy(screen, pictures(_team[size_t(k)]).figure.at(now, 2 * k), px, py);
    }
    if (_placeChosen >= 0 && _placeChosen >= int(_team.size()) && _placeChosen < limit)
        blitBy(screen, _marker.at(now), tx + _places[size_t(_placeChosen)].first + mx + sx,
               ty + _places[size_t(_placeChosen)].second + my + sy);
}

void TeamSelectionScreen::drawMission(Surface &screen) {
    if (!_font.loaded())
        return;
    // "Mission NN" and its name, centred one over the other (0x5ea1d0).
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    readPoint(_def, "MissionName Pos", x, y);
    readPoint(_def, "MissionName Size", w, h);
    char number[32];
    std::snprintf(number, sizeof number, "%s %02d", "Mission", _mission.number);
    _font.draw(screen, x + (w - _font.width(number)) / 2, y, number);
    _font.draw(screen, x + (w - _font.width(_mission.name)) / 2, y + _font.height(), _mission.name);
    // The items the mission needs, the missing ones in red (inferred).
    int sx = 0;
    int sy = 0;
    readPoint(_def, "Skill Pos", sx, sy);
    if (_mission.items.empty())
        return;
    _font.draw(screen, sx, sy, "Required Items for Mission");
    int line = sy + _font.height() + 2;
    for (const std::string &item : _mission.items) {
        const bool had = std::any_of(_team.begin(), _team.end(), [&](size_t m) { return _roster[m].hasItem(item); });
        const Font &f = had || !_redFont.loaded() ? _font : _redFont;
        f.draw(screen, sx, line, had ? item : item + " - NEEDED");
        line += _font.height();
    }
}

void TeamSelectionScreen::draw(Surface &screen, uint64_t now) {
    image::blit(screen, _background, 0, 0);
    for (const StaticText &t : _texts)
        t.draw(screen, 0, 0);
    int cx = 0;
    int cy = 0;
    if (_inventoryShown) {
        readPoint(_def, "Inventory Pos", cx, cy);
        image::blit(screen, _inventoryBackground, cx, cy);
    } else {
        readPoint(_def, "CharacterDisplay Pos", cx, cy);
        image::blit(screen, _characterBackground, cx, cy);
        const int shown = _rosterChosen >= 0                                      ? _rosterChosen
                          : _placeChosen >= 0 && _placeChosen < int(_team.size()) ? int(_team[size_t(_placeChosen)])
                                                                                  : -1;
        if (shown >= 0)
            drawCharacter(screen, size_t(shown));
    }
    drawRoster(screen);
    drawTransporter(screen, now);
    drawMission(screen);
    if (!_message.empty() && now < _messageUntil && _font.loaded()) {
        int mx = 0;
        int my = 0;
        int mw = 0;
        int mh = 0;
        readPoint(_def, "Message Pos", mx, my);
        readPoint(_def, "Message Size", mw, mh);
        _font.drawWrapped(screen, mx + mw / 2, my, mw, _message, Font::Align::kCenter, 2);
    }
    _buttons.draw(screen);
    if (_confirm)
        _confirm->draw(screen);
}

} // namespace reluva::stawayteam
