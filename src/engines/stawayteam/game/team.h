// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"

#include <map>
#include <string>
#include <vector>

namespace reluva::stawayteam {

/**
 * One of the Away Team's officers, as `Character/Starting Roster/<name>.txt`
 * describes them (a `CTeamMemberInfo`), with their race, gender and icon
 * from their portrait's definition (`Character/Character Portraits`, a
 * `CStarTrekFrame`) (docs/engines/stawayteam/systems/team-selection.md).
 */
struct TeamMember {
    std::string id; // the roster file's name: how missions name them ("Chad Verax")
    std::string firstName;
    std::string lastName;
    std::string rank;
    std::string field;
    std::string portrait;
    bool available = false;
    std::string characterScript; // a script of Levels/, and the entry describing them
    std::string characterText;
    struct Attribute {
        std::string name;
        int value = 0;
    };
    std::vector<Attribute> attributes;
    struct Item {
        std::string name;
        int count = 0; // -1: as many as wanted
    };
    std::vector<Item> inventory;
    std::string race;
    std::string gender;
    std::string headIcon;
    std::string bodyModel; // of Cache/Models: the body, and the head drawn over it
    std::string headModel;

    /** "First Last", or the first alone: the name shown, and of their sequences. */
    std::string name() const;
    bool hasItem(const std::string &item) const;
};

/** The officers of the game's roster, in its files' order. */
Error loadRoster(const Archive &data, std::vector<TeamMember> &out);

/** The officers' fields, as `Character/Field` names them, in its files' order. */
std::vector<std::string> loadFields(const Archive &data);

/**
 * A line of a mission's briefing (`CBriefingScriptLine`): its text, an
 * entry of a script of `Levels/`, and who says it.
 */
struct BriefingLine {
    std::string script;    // "Mission 02/Mission 02": Levels/Mission 02/Mission 02.RDS
    std::string text;      // the entry's label, and its voice-over's name
    std::string speaker;   // their portrait's name
    int frameToStart = -1; // the movie's frame it waits for; -1: none
};

/**
 * A mission's description (`Levels/Mission NN/Mission NN.MDS`, a
 * `CMissionDescription`), as far as the screens before it use it
 * (docs/engines/stawayteam/systems/briefing.md).
 */
struct Mission {
    int number = 0;
    std::string name;
    bool showBriefing = false;
    bool showTeamSelection = false;
    std::string transporterBackground;
    std::string mapName; // of the mission's folder in Levels/: "01 Klingon Outpost.zax"
    int maxMembers = 0;
    std::vector<std::string> whoShouldStart; // roster ids; these can't be left behind
    std::vector<std::string> items;          // each needed by one of the team
    std::vector<std::string> recommendedItems;
    std::string briefingMovie; // of DATA/Movies, "Generic/Starfield.bik"
    bool loopMovie = false;
    std::string briefingMusic; // of Music/
    std::vector<BriefingLine> briefing;
};

/**
 * An inventory item's definition, `Inventory/Item Definitions/<name>.txt`
 * (a `CStarTrekInventoryItemDefinition`), as far as the screens before a
 * mission use it.
 */
struct ItemDefinition {
    std::string name;          // the file's name: how officers and missions name it
    std::string printableName; // the name shown
    std::string iconImage;     // Cache/Inventory/Icon Frames/<name>.frm16
    std::string type;          // of Inventory/Item Types
    bool displayInInventory = false;
    std::string briefingScript; // a script of Levels/, and its entry describing the item
    std::string briefingText;
    // A weapon's (Weapon Damage 0 for anything else).
    float weaponDamage = 0.0f;
    float reuseDelay = 0.0f;       // seconds between shots
    float range = 0.0f;            // Item Range, pixels
    float maxShootAngle = 0.0f;    // degrees off its aim it still shoots
    std::string dischargeAction;   // what using it does: CShootEnergyWeaponAction...
    std::string dischargeSequence; // the shooter's ("Shoot1")
    std::string shotSound;
    std::string shotFlash; // models, of Cache/Models
    std::string hitFlash;
    float shotDuration = 0.0f; // seconds the shot's shown
    float stunDuration = 0.0f; // seconds one stunned by it stays down
    bool stuns = false;        // set to stun (Weapon Setting), not kill
};

/**
 * The item definitions, by name, those in Item Definitions' folders by
 * their path in it ("Enemy Weapons/Vulcan Phaser"); one that won't read is
 * left out.
 */
std::map<std::string, ItemDefinition> loadItems(const Archive &data);

/** The inventory's item types (`Inventory/Item Types`), in their files' order, `!None` left out. */
std::vector<std::string> loadItemTypes(const Archive &data);

/** Reads mission `number` (1 to 18). */
Error loadMission(const Archive &data, int number, Mission &out);

} // namespace reluva::stawayteam
