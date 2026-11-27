// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stawayteam/game/team.h"

#include "engines/stawayteam/formats/text_object.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace reluva::stawayteam {

namespace {

Error readObject(const Archive &data, const std::string &path, TextObject &out) {
    std::vector<uint8_t> bytes;
    if (Error err = data.read(path, bytes))
        return err;
    if (Error err = readTextObject(std::string(bytes.begin(), bytes.end()), out))
        return Error::fail(path + ": " + err.message);
    return Error::success();
}

std::string text(const TextObject &object, const char *key) {
    const std::string *value = object.value(key);
    return value ? *value : std::string();
}

// The files of a folder of DATA.dat with an extension, by name; with
// `nested`, those of its folders too.
std::vector<std::string> filesIn(const Archive &data, const std::string &folder, const std::string &ext,
                                 bool nested = false) {
    std::vector<std::string> out;
    const std::string prefix = normalizeArchiveName(folder) + "/";
    for (const std::string &name : data.list()) {
        const std::string key = normalizeArchiveName(name);
        if (key.starts_with(prefix) && key.ends_with(ext) &&
            (nested || key.find('/', prefix.size()) == std::string::npos))
            out.push_back(name);
    }
    std::sort(out.begin(), out.end(), [](const std::string &a, const std::string &b) {
        return normalizeArchiveName(a) < normalizeArchiveName(b);
    });
    return out;
}

// The first object of class `className` in `o`, however deep.
const TextObject *findObject(const TextObject &o, const std::string &className) {
    if (o.className == className)
        return &o;
    for (const TextField &f : o.fields)
        if (f.object)
            if (const TextObject *found = findObject(*f.object, className))
                return found;
    return nullptr;
}

std::string stem(const std::string &path) {
    const size_t slash = path.find_last_of('/');
    std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
    const size_t dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

} // namespace

std::string TeamMember::name() const {
    return lastName.empty() ? firstName : firstName + " " + lastName;
}

bool TeamMember::hasItem(const std::string &item) const {
    return std::any_of(inventory.begin(), inventory.end(), [&](const Item &i) { return i.name == item; });
}

Error loadRoster(const Archive &data, std::vector<TeamMember> &out) {
    out.clear();
    for (const std::string &path : filesIn(data, "Character/Starting Roster", ".txt")) {
        TextObject info;
        if (Error err = readObject(data, path, info))
            return err;
        TeamMember m;
        m.id = stem(path);
        m.firstName = text(info, "First Name");
        m.lastName = text(info, "Last Name");
        m.rank = text(info, "Rank");
        m.field = text(info, "Field");
        m.portrait = text(info, "Portrait");
        m.available = info.integer("Is Available").value_or(0) != 0;
        m.characterScript = text(info, "Character Script");
        m.characterText = text(info, "Character Text");
        if (const TextObject *attributes = info.object("Attribute"))
            for (const TextField *item : attributes->items())
                if (item->object)
                    m.attributes.push_back(
                        {text(*item->object, "Name"), int(item->object->integer("Value").value_or(0))});
        if (const TextObject *inventory = info.object("Inventory"))
            for (const TextField *item : inventory->items())
                if (item->object)
                    m.inventory.push_back(
                        {text(*item->object, "Inventory Definition"), int(item->object->integer("Count").value_or(0))});
        // What the portrait's definition adds (0x5e4d70, 0x5e4e70).
        TextObject frame;
        if (!readObject(data, "Character/Character Portraits/" + m.portrait + ".TXT", frame)) {
            m.race = text(frame, "Race");
            m.gender = text(frame, "Gender");
            m.headIcon = text(frame, "Head Icon");
            m.bodyModel = text(frame, "Body Model");
            m.headModel = text(frame, "Head Model");
        }
        out.push_back(std::move(m));
    }
    return out.empty() ? Error::fail("DATA.dat has no Character/Starting Roster") : Error::success();
}

std::vector<std::string> loadFields(const Archive &data) {
    std::vector<std::string> out;
    for (const std::string &path : filesIn(data, "Character/Field", ".txt"))
        out.push_back(stem(path));
    return out;
}

bool setToStun(const TextObject &item) {
    // The setting's values: Stun 0, Kill 1 (0x649e60).
    return text(item, "Weapon Setting") == "Stun" || item.integer("Weapon Kills").value_or(1) == 0;
}

std::map<std::string, ItemDefinition> loadItems(const Archive &data) {
    std::map<std::string, ItemDefinition> out;
    // The enemies' are in folders of it, and named with them ("Enemy
    // Weapons/Vulcan Phaser").
    const std::string folder = "Inventory/Item Definitions";
    for (const std::string &path : filesIn(data, folder, ".txt", true)) {
        TextObject def;
        if (readObject(data, path, def))
            continue;
        ItemDefinition item;
        item.name = stem(path);
        item.printableName = text(def, "Printable Item Name");
        item.iconImage = text(def, "Icon Image");
        if (const std::string *format = def.value("Count Format String"))
            item.countFormat = *format;
        if (const std::string *empty = def.value("Empty String"))
            item.emptyText = *empty;
        item.type = text(def, "Inventory Type");
        item.displayInInventory = def.integer("Display In Inventory").value_or(0) != 0;
        item.briefingScript = text(def, "Briefing Script");
        item.briefingText = text(def, "Briefing Text");
        item.weaponDamage = float(def.number("Weapon Damage").value_or(0.0));
        item.reuseDelay = float(def.number("Reuse Delay").value_or(0.0));
        item.range = float(def.number("Item Range").value_or(0.0));
        item.maxShootAngle = float(def.number("Max Shoot Angle").value_or(0.0));
        item.dischargeAction = text(def, "Discharge Action");
        if (const TextObject *action = def.object("Discharge Action")) {
            item.shotNoise = float(action->number("Noise Radius").value_or(0.0));
            item.shotNoiseTime = float(action->number("Time").value_or(0.0));
            if (const TextObject *heal = action->object("Health action"))
                item.health = float(heal->number("Health").value_or(0.0));
            // Its Time To Enhance's default 60 (0x57ed8a).
            if (const TextObject *enhance = action->object("Action");
                enhance && enhance->className == "CEnhanceInventoryAction")
                item.enhanceTime = float(enhance->number("Time To Enhance").value_or(60.0));
            item.forward = float(action->number("Forward Position").value_or(0.0));
            item.lateral = float(action->number("Lateral Position").value_or(0.0));
            item.height = float(action->number("Height").value_or(0.0));
            if (action->className == "CShootEnergyWeaponAction") {
                item.beam = true;
                // The line's own default edges (205, 20, 25) where none's
                // given (0x499b66).
                item.beamOutside = {205, 20, 25};
                item.beamMiddle = {205, 20, 25};
                const auto colour = [&](const char *key, std::array<uint8_t, 3> &out) {
                    int r = 0;
                    int g = 0;
                    int b = 0;
                    if (std::sscanf(text(*action, key).c_str(), "%d,%d,%d", &r, &g, &b) == 3)
                        out = {uint8_t(r), uint8_t(g), uint8_t(b)};
                };
                colour("Outside Color", item.beamOutside);
                colour("Middle Color", item.beamMiddle);
            }
            if (const TextObject *gravity = action->object("Gravity AI")) {
                item.gravity = float(gravity->number("Gravity").value_or(0.0));
                item.bounceSound = text(*gravity, "Bounce Sound");
            }
            item.velocityFactor = float(action->number("Velocity Factor").value_or(1.0));
        }
        if (const TextObject *held = def.object("Default Inventory Item"))
            item.rounds = int(held->integer("Rounds").value_or(0));
        if (const TextObject *process = def.object("Process")) {
            item.processType = text(*process, "Process Type");
            item.processCursor = text(*process, "Process Cursor");
            item.cantUseCursor = text(*process, "Can't Use Cursor");
        }
        item.hoveringCursor = text(def, "Entity Hovering Cursor");
        item.obscuredCursor = text(def, "Obscured Cursor");
        item.showObscured = def.integer("Show Obscured Cursor").value_or(0) != 0;
        item.onlyIfHovering = def.integer("Only Execute If Hovering").value_or(0) != 0;
        item.canTargetPlayers = def.integer("Can Target Players").value_or(1) != 0;
        item.projectile = text(def, "Projectile");
        if (item.projectile == "!Unknown Model")
            item.projectile.clear();
        item.canUseOnSelf = def.integer("Can Use On Self").value_or(0) != 0;
        item.needLos = def.integer("Need LOS").value_or(0) != 0;
        item.useSound = text(def, "Use Sound");
        item.multipleSettings = def.integer("Has Multiple Settings").value_or(0) != 0;
        item.overloadable = def.integer("Is Overloadable").value_or(0) != 0;
        item.overloadProjectile = text(def, "Overload Projectile");
        if (item.overloadProjectile == "!Unknown Model")
            item.overloadProjectile.clear();
        item.overloadSequence = text(def, "Overload Discharge Sequence");
        if (const TextObject *action = def.object("Overload Discharge Action")) {
            if (const TextObject *gravity = action->object("Gravity AI"))
                item.overloadGravity = float(gravity->number("Gravity").value_or(0.0));
            item.overloadVelocityFactor = float(action->number("Velocity Factor").value_or(1.0));
        }
        if (const TextObject *process = def.object("Process"))
            if (const TextObject *drop = findObject(*process, "CDropItemAtTargetAction")) {
                item.deployModel = text(*drop, "Model to Create");
                item.deploySequence = text(*drop, "Deploy Sequence");
            }
        if (const TextObject *held = def.object("Default Inventory Item")) {
            item.remote = held->className == "CStarTrekRemoteMineInventoryItem";
            item.charged = held->className == "CStarTrekInvisBandInventoryItem";
            item.psyche = held->className == "CStarTrekPsycheDisplacementInventoryItem";
            item.failSound = text(*held, "Fail Sound");
            item.breakSound = text(*held, "Break Meld Sound");
            item.controllerItem = text(*held, "Controller Item");
        }
        item.energyUse = float(def.number("Energy Use").value_or(20.0));
        item.energyRecharge = float(def.number("Energy Recharge").value_or(2.5));
        for (TextField &f : def.fields)
            if (f.key == "Info Renderer" && f.object)
                item.infoRenderer = std::shared_ptr<const TextObject>(std::move(f.object));
        item.dischargeSequence = text(def, "Discharge Sequence");
        item.shotSound = text(def, "Shot Sound");
        item.shotFlash = text(def, "Shot Flash");
        item.hitFlash = text(def, "Hit Flash");
        item.shotDuration = float(def.number("Shot Duration").value_or(0.0));
        item.stunDuration = float(def.number("Stun Duration").value_or(0.0));
        if (const TextObject *held = def.object("Default Inventory Item"))
            item.stuns = setToStun(*held);
        item.strikeRadius = float(def.number("Strike Damage Radius").value_or(0.0));
        item.startsDiscovered = def.integer("Starts Discovered").value_or(1) != 0;
        item.pointsToDiscover = int(def.integer("Points to Discover").value_or(0));
        item.whoDiscovers = text(def, "Who Discovers");
        item.discoveryScript = text(def, "Script");
        item.discoveryText = text(def, "Discovery Text");
        item.overriddenBy = text(def, "Overridden By");
        if (item.overriddenBy == "!None")
            item.overriddenBy.clear();
        for (TextField &f : def.fields)
            if ((f.key == "Process" || f.key == "Discharge Action") && f.object)
                (f.key == "Process" ? item.process : item.discharge) =
                    std::shared_ptr<const TextObject>(std::move(f.object));
        const std::string key = path.substr(folder.size() + 1, path.size() - folder.size() - 5);
        out[key] = std::move(item);
    }
    return out;
}

std::string heldAs(const std::map<std::string, ItemDefinition> &items, const std::string &name,
                   const std::vector<std::string> &discovered) {
    const auto isDiscovered = [&](const std::string &item) {
        const auto it = items.find(item);
        return it == items.end() || it->second.startsDiscovered ||
               std::find(discovered.begin(), discovered.end(), item) != discovered.end();
    };
    if (!isDiscovered(name))
        return {};
    // Along the items overriding it while they're discovered; a loop in
    // the definitions stops where it began.
    std::string held = name;
    for (size_t step = 0; step < items.size(); ++step) {
        const auto it = items.find(held);
        if (it == items.end() || it->second.overriddenBy.empty() || !isDiscovered(it->second.overriddenBy))
            break;
        held = it->second.overriddenBy;
    }
    return held;
}

void resolveInventories(std::vector<TeamMember> &officers, const std::map<std::string, ItemDefinition> &items,
                        const std::vector<std::string> &discovered) {
    for (TeamMember &m : officers) {
        std::vector<TeamMember::Item> held;
        for (TeamMember::Item item : m.inventory) {
            item.name = heldAs(items, item.name, discovered);
            if (!item.name.empty())
                held.push_back(std::move(item));
        }
        m.inventory = std::move(held);
    }
}

std::vector<std::string> loadItemTypes(const Archive &data) {
    std::vector<std::string> out;
    for (const std::string &path : filesIn(data, "Inventory/Item Types", ".txt"))
        if (stem(path) != "!None")
            out.push_back(stem(path));
    // A type's index is its place in this order (0x41fa91, 0x5f2cf2).
    std::sort(out.begin(), out.end(), [](const std::string &a, const std::string &b) {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
            return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
        });
    });
    return out;
}

Error loadMission(const Archive &data, int number, Mission &out) {
    char folder[32];
    std::snprintf(folder, sizeof folder, "Mission %02d", number);
    TextObject mds;
    if (Error err = readObject(data, std::string("Levels/") + folder + "/" + folder + ".MDS", mds))
        return err;
    out = Mission{};
    out.number = number;
    out.name = text(mds, "Mission Name");
    out.showBriefing = mds.integer("Show Briefing").value_or(0) != 0;
    out.showTeamSelection = mds.integer("Show Team Selection").value_or(0) != 0;
    out.transporterBackground = text(mds, "Transporter Background");
    out.mapName = text(mds, "Map Name");
    out.maxMembers = int(mds.integer("Max Members To Start").value_or(4));
    if (const TextObject *who = mds.object("Who Should Start"))
        for (const TextField *item : who->items())
            out.whoShouldStart.push_back(item->value);
    if (const TextObject *items = mds.object("Items"))
        for (const TextField *item : items->items())
            out.items.push_back(item->value);
    if (const TextObject *items = mds.object("Recommended Items"))
        for (const TextField *item : items->items())
            out.recommendedItems.push_back(item->value);
    out.briefingMovie = text(mds, "Briefing Movie");
    out.loopMovie = mds.integer("Loop movie").value_or(0) != 0;
    out.briefingMusic = text(mds, "Briefing Music");
    const auto readScript = [&](const char *key, std::vector<BriefingLine> &lines) {
        const TextObject *script = mds.object(key);
        if (const TextObject *list = script ? script->object("Script") : nullptr)
            for (const TextField *item : list->items())
                if (item->object)
                    lines.push_back({text(*item->object, "Script"), text(*item->object, "Text"),
                                     text(*item->object, "Who is speaking"),
                                     int(item->object->integer("Frame to start").value_or(-1))});
    };
    readScript("Script", out.briefing);
    readScript("Debrief Script", out.debriefing);
    out.showDebriefing = mds.integer("Show Debriefing").value_or(0) != 0;
    out.endGameAfterMission = mds.integer("End Game After Mission").value_or(0) != 0;
    out.debriefingMusic = text(mds, "Debriefing Music");
    out.primarySong = text(mds, "Primary Song");
    if (const TextObject *songs = mds.object("Random Song"))
        for (const TextField *item : songs->items())
            out.randomSongs.push_back(item->value);
    out.availableInNetGame = mds.integer("Available in Net Game").value_or(0) != 0;
    out.netLobbyScript = text(mds, "Net Lobby Script");
    out.netLobbyText = text(mds, "Net Lobby Text");
    for (TextField &f : mds.fields)
        if (f.key == "Objective" && f.object)
            out.objectives = std::shared_ptr<const TextObject>(std::move(f.object));
    return Error::success();
}

} // namespace reluva::stawayteam
