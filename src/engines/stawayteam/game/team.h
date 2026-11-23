// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/types.h"
#include "engines/stawayteam/formats/text_object.h"

#include <array>
#include <map>
#include <memory>
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
    bool showDebriefing = false;
    std::vector<BriefingLine> debriefing; // its Debrief Script
    std::string debriefingMusic;
    std::string primarySong;                      // of Music/, played on its maps
    std::vector<std::string> randomSongs;         // played on them after it
    std::shared_ptr<const TextObject> objectives; // its CMissionObjectiveManager, if it has one
    bool availableInNetGame = false;
    std::string netLobbyScript; // a script of Levels/, and its entry the net lobby shows
    std::string netLobbyText;
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
    int rounds = 0;            // its Default Inventory Item's Rounds: uses of one, 0 for unlimited
    std::string processType;   // what using it does: "Shoot", "Hypospray Use", "Scan"...
    std::string projectile;    // a thrown one's model, of Cache/Models
    // Where its Discharge Action leaves the user: Forward Position along
    // their facing, Lateral Position across it, Height up from the ground
    // (a throw's hand, a shot's muzzle).
    float forward = 0.0f;
    float lateral = 0.0f;
    float height = 0.0f;
    // A beam weapon's (CShootEnergyWeaponAction's) colours, red, green,
    // blue: its edges' and its middle's; none for a weapon without one.
    bool beam = false;
    std::array<uint8_t, 3> beamOutside{};
    std::array<uint8_t, 3> beamMiddle{};
    // A thrown one's Gravity AI's Gravity and Bounce Sound.
    float gravity = 0.0f;
    std::string bounceSound;
    float health = 0.0f;      // a healer's (its Discharge Action's Health action's Health)
    float enhanceTime = 0.0f; // an enhancer's (its Discharge Action's CEnhanceInventoryAction's Time To Enhance), s
    bool canUseOnSelf = false;
    std::string useSound;
    bool multipleSettings = false; // Has Multiple Settings: the phaser's kill and stun
    // Is Overloadable: set to overload, it goes off as its Overload
    // Projectile where it is, or is thrown as it, by its Overload Discharge
    // Action's Gravity and Overload Discharge Sequence.
    bool overloadable = false;
    std::string overloadProjectile;
    float overloadGravity = 0.0f;
    std::string overloadSequence;
    std::string deployModel;    // what a mine leaves where it's put (its CDropItemAtTargetAction's)
    std::string deploySequence; // its sequence played once as it's put down: that action's Deploy Sequence
    bool remote = false;        // a mine set off from its panel, not by who comes near
    // Its panel on the info bar (its Info Renderer: a CPhaserInfoRenderer,
    // CGenericInfoRenderer...), kept as written.
    std::shared_ptr<const TextObject> infoRenderer;
    float shotNoise = 0.0f;     // its Discharge Action's Noise Radius, pixels
    float shotNoiseTime = 0.0f; // seconds it's heard for
    // Its cursors, of Interface/Game Cursors ("!None" for none): where it
    // can be used (its Process's Process Cursor) and can't, over what it's
    // used on (Entity Hovering Cursor), and over that out of sight
    // (Obscured Cursor, with Show Obscured Cursor).
    // A charged one's (the cloaking band's): its charge, out of 100,
    // spent Energy Use a second while it's on and back Energy Recharge a
    // second while it's off (0x5d2c50).
    // The Psychic Rack (a CStarTrekPsycheDisplacementInventoryItem): used
    // on an enemy, its holder takes it over while its charge lasts, with
    // its Fail Sound when it can't and its Break Meld Sound as it ends.
    bool psyche = false;
    std::string failSound;
    std::string breakSound;
    std::string controllerItem; // the item the enemy taken over is given: its own panel
    bool charged = false;
    float strikeRadius = 0.0f; // Strike Damage Radius: how near a strike reaches
    // Its Process (what using it does, with its Process Activity) and its
    // Discharge Action, kept as written.
    std::shared_ptr<const TextObject> process;
    std::shared_ptr<const TextObject> discharge;
    float energyUse = 0.0f;
    float energyRecharge = 0.0f;
    // How it's found (its Discovery): had from the start, or once the
    // score's its Points to Discover; then described in the next briefing
    // by its Who Discovers, its Discovery Text a line of its Script.
    bool startsDiscovered = true;
    int pointsToDiscover = 0;
    std::string overriddenBy; // the item had in its place once that's discovered (Discovery/Overridden By)
    std::string whoDiscovers;
    std::string discoveryScript;
    std::string discoveryText;
    std::string processCursor;
    std::string cantUseCursor;
    std::string hoveringCursor;
    std::string obscuredCursor;
};

/**
 * The item definitions, by name, those in Item Definitions' folders by
 * their path in it ("Enemy Weapons/Vulcan Phaser"); one that won't read is
 * left out.
 */
std::map<std::string, ItemDefinition> loadItems(const Archive &data);

/**
 * The item an officer has as `name` (0x5b7c90): none (empty) if it isn't
 * discovered, else the item it's overridden by if that's discovered, and
 * so on. An item that starts discovered, or is in `discovered`, is; one
 * without a definition is kept as it is.
 */
std::string heldAs(const std::map<std::string, ItemDefinition> &items, const std::string &name,
                   const std::vector<std::string> &discovered);

/**
 * The officers' inventories as they have them: each item as heldAs()
 * gives it, those not discovered taken out.
 */
void resolveInventories(std::vector<TeamMember> &officers, const std::map<std::string, ItemDefinition> &items,
                        const std::vector<std::string> &discovered);

/** The inventory's item types (`Inventory/Item Types`), in their files' order, `!None` left out. */
std::vector<std::string> loadItemTypes(const Archive &data);

/** Reads mission `number` (1 to 18). */
Error loadMission(const Archive &data, int number, Mission &out);

} // namespace reluva::stawayteam
