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
#include "engines/stawayteam/ui/animation.h"
#include "engines/stawayteam/ui/button.h"
#include "engines/stawayteam/ui/font.h"
#include "engines/stawayteam/ui/static_text.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace reluva::stawayteam {

class ConfirmDialog;

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
    enum class Choice { kNone, kStartMission, kBriefing, kExit };

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
    /** The team's officers, in their places' order. */
    std::vector<TeamMember> chosenTeam() const {
        std::vector<TeamMember> out;
        for (size_t i : _team)
            out.push_back(_roster[i]);
        return out;
    }

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

    // The officer whose details and inventory show: the one chosen in the
    // roster or on the transporter, or -1.
    int shownMember() const;
    // Chooses `member`, on the transporter if they're on the team, else in
    // the roster at their field (0x5ec018).
    void choose(size_t member);
    // On to the next available officer, after the one shown, who has
    // `item` (0x5ebf10).
    void cycleTo(const std::string &item);

    // The shown officer's items, a column for each item type (0x5ec1e0).
    std::vector<std::vector<const ItemDefinition *>> inventoryColumns() const;
    // The item at (x, y) in the inventory panel, or nullptr.
    const ItemDefinition *inventoryItemAt(int x, int y) const;
    const image::Image &itemIcon(const std::string &name);
    // The mission's required items, then its recommended ones, as their
    // lists show them (0x5e9620).
    struct ItemRow {
        enum class Kind { kTitle, kNone, kItem } kind;
        bool required;           // of the required items' list
        const std::string *item; // for kItem
        int y;                   // from the panel's top
    };
    std::vector<ItemRow> itemRows() const;
    bool teamHas(const std::string &item) const;
    void describe(const ItemDefinition &item, uint64_t now);

    void drawCharacter(Surface &screen, size_t member);
    void drawInventory(Surface &screen);
    void drawRoster(Surface &screen);
    void drawTransporter(Surface &screen, uint64_t now);
    void drawMission(Surface &screen);
    void drawDescription(Surface &screen, uint64_t now);

    const Archive &_data;
    const TextObject &_rules;
    const TextObject *_def;
    Mission _mission;
    std::vector<TeamMember> _roster;
    std::vector<Pictures> _pictures;
    std::vector<std::string> _fields;
    std::vector<size_t> _team;
    image::Image _background;
    Font _font;     // Trek Small, the menu's
    Font _redFont;  // Trek Small Red: what's missing
    Font _goldFont; // Trek Small Gold: the item lists' titles
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
    std::map<std::string, ItemDefinition> _items;
    std::vector<std::string> _itemTypes;
    std::map<std::string, image::Image> _icons;
    image::Image _itemFrame;
    image::Image _itemSelected;
    std::string _itemHover; // the inventory's item under the mouse
    // An item's description, over the screen (0x5e8d60).
    bool _describing = false;
    std::string _descriptionTitle;
    std::string _descriptionText;
    std::string _descriptionIcon;
    float _descriptionTyped = 0.0f;
    uint64_t _descriptionLast = 0;
    image::Image _descriptionBackground;
    image::Image _descriptionIconBackground;
    Font _descriptionButtonFont;
    ButtonGroup _descriptionButtons;
    std::shared_ptr<audio::Sound> _beep;
    std::string _message;
    uint64_t _messageUntil = 0;
    std::unique_ptr<ConfirmDialog> _confirm;
};

} // namespace reluva::stawayteam
