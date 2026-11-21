// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/events.h"
#include "common/system.h"
#include "engines/stawayteam/formats/text_object.h"
#include "engines/stawayteam/game/team.h"
#include "engines/stawayteam/ui/font.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace reluva::stawayteam {

/**
 * The interface around the game's view, laid out by Rules.txt's Custom
 * Rules: the team bar down the left, the button box, the info bar with the
 * minimap that pops up under the mouse, the inventory and the pause button
 * (docs/engines/stawayteam/systems/maps.md).
 */
class GameHud {
public:
    /** What an event on it asks of the game. */
    struct Result {
        bool used = false; // it was over the interface, not the map
        int select = -1;   // an officer chosen on the team bar
        bool moveCamera = false;
        float cameraX = 0.0f; // where, for moveCamera: the view's middle on the map
        float cameraY = 0.0f;
    };

    /**
     * The interface for `team`, its minimap `minimap` (of Cache/Levels) for
     * a map `mapW` x `mapH`; `data` must outlive it.
     */
    GameHud(const Archive &data, const TextObject &rules, std::vector<TeamMember> team, const std::string &minimap,
            int mapW, int mapH);

    Result handleEvent(const Event &ev, uint64_t now);
    /**
     * Draws it, officer `selected` lit, the view's place on the minimap from
     * the camera (the view's middle) and the view's size.
     */
    void draw(Surface &screen, uint64_t now, int selected, float cameraX, float cameraY, int viewW, int viewH);

private:
    // The info bar's top on the screen at `now`: rising as it opens.
    int infoTop(uint64_t now);
    bool overInfoBar(int x, int y, uint64_t now);
    const image::Image &picture(const std::string &name);

    const Archive &_data;
    const TextObject *_custom;
    std::vector<TeamMember> _team;
    std::vector<image::Image> _heads;
    std::map<std::string, ItemDefinition> _items;
    std::map<std::string, image::Image> _pictures; // of Interface/Screens
    std::map<std::string, image::Image> _icons;    // the items', by name
    image::Image _minimap;
    int _mapW = 0;
    int _mapH = 0;
    Font _font;
    std::map<std::string, Font> _fieldFonts;
    // The info bar: how open it is (0 to 1) and since when it's moving.
    float _open = 0.0f;
    bool _opening = false;
    uint64_t _last = 0;
    int _mouseX = -1;
    int _mouseY = -1;
};

} // namespace reluva::stawayteam
