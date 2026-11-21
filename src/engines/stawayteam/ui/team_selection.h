// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "audio/mixer.h"
#include "common/archive.h"
#include "common/events.h"
#include "common/system.h"
#include "engines/stawayteam/formats/frame.h"
#include "engines/stawayteam/formats/text_object.h"
#include "engines/stawayteam/game/team.h"
#include "engines/stawayteam/ui/button.h"
#include "engines/stawayteam/ui/font.h"
#include "engines/stawayteam/ui/static_text.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace reluva::stawayteam {

class ConfirmDialog;

/** A sequence's frames, as pictures with their origins, and its speed. */
struct Animation {
    struct Picture {
        image::Image image;
        int originX = 0;
        int originY = 0;
    };
    std::vector<Picture> frames;
    float fps = 0.0f;

    /** Reads `Cache/Sequences/<name>.seq16`, or a model's sequence `sequence` from `Cache/Models/<name>.mdl16`. */
    Error load(const Archive &data, const std::string &name, const std::string &sequence = "");
    /** The frame shown `ms` milliseconds in, the first `offset` frames passed over; nullptr if it has none. */
    const Picture *at(uint64_t ms, int offset = 0) const;
};

/**
 * The team selection screen (`CStarTrekTeamSelectionMenu`, 0x5ea240),
 * before a mission that has one: the roster by field, the officer chosen
 * (portrait, details, attributes, description), the team on the
 * transporter, the mission and the items it needs; ADD MEMBER and REMOVE
 * MEMBER, START MISSION and EXIT. Laid out by Rules.txt's `Team
 * Selection` (docs/engines/stawayteam/systems/team-selection.md).
 */
class TeamSelectionScreen {
public:
    /** What the player chose that leaves the screen. */
    enum class Choice { kNone, kStartMission, kExit };

    /**
     * The screen for `mission`, the officers of `roster`; `data` and
     * `rules` must outlive it.
     */
    TeamSelectionScreen(const Archive &data, const TextObject &rules, const Mission &mission,
                        std::vector<TeamMember> roster);
    ~TeamSelectionScreen();

    /** What `ev` does to it at `now` (milliseconds). */
    Choice handleEvent(const Event &ev, audio::Mixer *mixer, uint64_t now);
    void draw(Surface &screen, uint64_t now);

    /** The team, the roster's numbers, in their places' order. */
    const std::vector<size_t> &team() const { return _team; }

private:
    /** An officer's pictures, loaded as they're first shown. */
    struct Pictures {
        bool loaded = false;
        image::Image portrait;
        image::Image icon;
        Animation figure;
    };

    Pictures &pictures(size_t member);
    // The roster's officers shown for the field chosen: the available ones
    // not on the team (inferred).
    std::vector<size_t> rosterShown() const;
    void showMessage(const std::string &text, uint64_t now);
    void updateButtons();
    bool required(size_t member) const;
    // The member under (x, y) on the screen: in the roster's icons, or a
    // place on the transporter.
    int rosterAt(int x, int y) const;
    int placeAt(int x, int y) const;

    void drawCharacter(Surface &screen, size_t member);
    void drawRoster(Surface &screen);
    void drawTransporter(Surface &screen, uint64_t now);
    void drawMission(Surface &screen);

    const Archive &_data;
    const TextObject &_rules;
    const TextObject *_def;
    Mission _mission;
    std::vector<TeamMember> _roster;
    std::vector<Pictures> _pictures;
    std::vector<std::string> _fields;
    std::vector<size_t> _team;
    image::Image _background;
    Font _font;    // Trek Small, the menu's
    Font _redFont; // Trek Small Red: what's missing
    std::vector<StaticText> _texts;
    std::vector<Font> _buttonFonts;
    ButtonGroup _buttons;      // EXIT, BRIEFING, START MISSION, ADD, REMOVE, INVENTORY, OFFICERS
    ButtonGroup _fieldButtons; // the roster's, a field each
    size_t _field = 0;
    int _rosterChosen = -1; // the roster's number
    int _rosterHover = -1;
    int _placeChosen = -1; // a place on the transporter
    int _placeHover = -1;
    bool _inventoryShown = false;
    // The pictures of the panels.
    image::Image _characterBackground;
    image::Image _inventoryBackground;
    image::Image _portraitBackground;
    image::Image _attributeBar;
    image::Image _rosterBackground;
    image::Image _iconBackground;
    image::Image _iconSelected;
    Animation _transporter;
    Animation _marker;
    std::vector<std::pair<int, int>> _places;
    std::vector<StaticText> _transporterTexts;
    std::string _message;
    uint64_t _messageUntil = 0;
    std::unique_ptr<ConfirmDialog> _confirm;
};

} // namespace reluva::stawayteam
