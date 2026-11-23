// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The menus' parts: the game's fonts as its text is drawn with them,
// buttons, their labels, shortcuts and clicks, the credits' lines, and the
// team selection's roster, mission and rules.

#include "engines/stawayteam/ui/briefing.h"
#include "engines/stawayteam/ui/debriefing.h"
#include "engines/stawayteam/ui/escape_menu.h"
#include "engines/stawayteam/ui/layout.h"
#include "engines/stawayteam/ui/save_menu.h"
#include "engines/stawayteam/game/campaign.h"
#include "engines/stawayteam/ui/button.h"
#include "engines/stawayteam/ui/credits.h"
#include "engines/stawayteam/ui/font.h"
#include "engines/stawayteam/ui/key_config.h"
#include "engines/stawayteam/ui/main_menu.h"
#include "engines/stawayteam/game/controls.h"
#include "engines/stawayteam/game/options.h"
#include "engines/stawayteam/game/team.h"
#include "engines/stawayteam/ui/team_selection.h"
#include "engines/stawayteam/ui/widgets.h"

#include "tests/engines/stawayteam/support.h"
#include "tests/support/test.h"

#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::stawayteam;

namespace {

void testFont() {
    MemoryArchive data;
    // A and B, 3 and 4 pixels wide: a font without lower case.
    data.files["Cache/Fonts/Test.seq16"] = fontFile({solidFrame(3, 5, 0xffff), solidFrame(4, 5, 0xffe0)}, 65);
    Font font;
    CHECK(font.load(data, "Missing"));
    CHECK(!font.load(data, "Test") && font.loaded());
    CHECK(font.width("AB") == 7);
    CHECK(font.width("ab") == 7); // drawn as capitals
    CHECK(font.width("A\x01"
                     "B") == 7);  // control characters left out
    CHECK(font.width("Z?") == 6); // characters it hasn't got drawn as its first
    CHECK(font.baseline() == 11);
    // Lines broken at spaces, at line breaks, and narrower beside a picture.
    CHECK((font.wrap("AB AB AB", 17) == std::vector<std::string>{"AB AB", "AB"}));
    CHECK((font.wrap("AB AB AB", 17, 5, 1) == std::vector<std::string>{"AB", "AB AB"}));
    CHECK((font.wrap("AB\n\nB", 0) == std::vector<std::string>{"AB", "", "B"}));
}

void testButtons() {
    MemoryArchive data;
    data.files["Cache/Interface/Screens/Menu/Button Default.frm16"] = solidFrame(40, 10, 0x001f);
    ButtonGroup group;
    group.setOrigin(100, 50);
    for (int i = 0; i < 2; ++i) {
        Button button(data, "Menu", "Button", 0, i * 20);
        button.setLabel(i == 0 ? "PLAY &NETWORK GAME" : "&QUIT", nullptr, 0, 0);
        group.add(std::move(button));
    }
    CHECK(group.buttons()[0].label() == "PLAY NETWORK GAME" && group.buttons()[0].shortcut() == 'n');
    CHECK(group.buttons()[1].width() == 40 && group.buttons()[1].shortcut() == 'q');

    // An enabled button's shortcut underlined in light blue a row under
    // the baseline, kept inside the button, 1 in from the character's
    // sides: B, 4 wide, after A, 3, the label centred in 40.
    data.files["Cache/Fonts/Test.seq16"] = fontFile({solidFrame(3, 5, 0xffff), solidFrame(4, 5, 0xffe0)}, 65);
    Font font;
    CHECK(!font.load(data, "Test"));
    Button underlined(data, "Menu", "Button", 0, 0);
    underlined.setLabel("A&B", &font, 0, 0);
    Surface surface;
    surface.create(40, 10, PixelFormat::kRGBA32);
    underlined.draw(surface, 0, 0, true, false);
    const auto lit = [&](int x) {
        const uint8_t *p = surface.row(9) + x * 4;
        return p[0] == 63 && p[1] == 154 && p[2] == 255;
    };
    CHECK(!lit(19) && lit(20) && lit(21) && !lit(22));
    underlined.setEnabled(false);
    surface.create(40, 10, PixelFormat::kRGBA32);
    underlined.draw(surface, 0, 0, false, false);
    CHECK(!lit(20));

    // Its label lit under the mouse and 0.66 bright away from it, a
    // quarter of its colour in the toolkit's steps; at 4 a second, the
    // label dims from one to the other in a quarter of a second.
    Button fading(data, "Menu", "Button", 0, 0);
    fading.setLabel("A&B", &font, 0, 0);
    fading.setFadeRate(4.0f);
    const auto label = [&](bool hover, uint64_t now) {
        surface.create(40, 10, PixelFormat::kRGBA32);
        fading.draw(surface, 0, 0, hover, false, now);
        return int(surface.row(2)[17 * 4]);
    };
    const int whole = label(true, 1000);
    CHECK(whole > 200);
    CHECK(label(false, 1100) == whole);
    CHECK(label(false, 1200) == whole * 128 / 256);
    CHECK(label(false, 1300) == whole * 64 / 256);
    CHECK(label(false, 2000) == whole * 64 / 256);

    // With the focus, the arrows and Tab move it round the buttons, and
    // Enter presses the one that has it.
    group.setFocus(0);
    Event arrow;
    arrow.type = EventType::kKeyDown;
    arrow.key = key::kDown;
    CHECK(group.handleEvent(arrow, nullptr) == -1 && group.focus() == 1);
    arrow.key = key::kTab;
    CHECK(group.handleEvent(arrow, nullptr) == -1 && group.focus() == 0);
    arrow.key = key::kUp;
    CHECK(group.handleEvent(arrow, nullptr) == -1 && group.focus() == 1);
    CHECK(group.pressFocused(0, nullptr) == 1);

    Event ev;
    ev.type = EventType::kKeyDown;
    ev.key = 'q';
    // A shortcut is taken only with Alt held, and not Ctrl.
    CHECK(group.handleEvent(ev, nullptr) == -1);
    ev.mods = keymod::kAlt;
    CHECK(group.handleEvent(ev, nullptr) == 1);
    ev.key = 'Q';
    CHECK(group.handleEvent(ev, nullptr) == 1);
    ev.mods = keymod::kAlt | keymod::kCtrl;
    CHECK(group.handleEvent(ev, nullptr) == -1);
    // A click: down and up on the same button chooses it; let go off it, nothing.
    Event mouse;
    mouse.button = MouseButton::kLeft;
    mouse.type = EventType::kMouseDown;
    mouse.mouseX = 110;
    mouse.mouseY = 55;
    CHECK(group.handleEvent(mouse, nullptr) == -1);
    mouse.type = EventType::kMouseUp;
    CHECK(group.handleEvent(mouse, nullptr) == 0);
    mouse.type = EventType::kMouseDown;
    CHECK(group.handleEvent(mouse, nullptr) == -1);
    mouse.type = EventType::kMouseUp;
    mouse.mouseY = 75;
    CHECK(group.handleEvent(mouse, nullptr) == -1);
}

void testCredits() {
    // Comments left out, blank lines kept, the line ends' CR dropped, and
    // a line cut at 255 characters.
    const Font font;
    const CreditsRoll roll("// comment\r\n_Heading\r\n\r\nRole: Name\n/also a comment\n" + std::string(300, 'x'), font);
    CHECK(roll.lines().size() == 4);
    CHECK(roll.lines()[0] == "_Heading" && roll.lines()[1].empty() && roll.lines()[2] == "Role: Name");
    CHECK(roll.lines()[3].size() == 255);

    // A heading's line fades as its text does: whole across the middle,
    // part see-through toward the edges.
    MemoryArchive data;
    data.files["Cache/Fonts/Test.seq16"] = fontFile({solidFrame(3, 5, 0xffff), solidFrame(4, 5, 0xffe0)}, 65);
    Font drawn;
    CHECK(!drawn.load(data, "Test"));
    std::string headings;
    for (int i = 0; i < 20; ++i)
        headings += "_AB\n";
    CreditsRoll heading(headings, drawn);
    bool whole = false;
    bool faded = false;
    for (uint64_t now = 0; now < 20000; now += 50) {
        Surface screen;
        screen.create(40, 60, PixelFormat::kRGBA32);
        heading.draw(screen, 0, 0, 40, 60, now);
        for (int y = 0; y < 60; ++y) {
            const uint8_t *p = screen.row(y) + 20 * 4;
            if (p[1] == 0 && p[2] == 0 && p[0] == 0x9c)
                whole = true;
            else if (p[1] == 0 && p[2] == 0 && p[0] > 0 && p[0] < 0x9c)
                faded = true;
        }
    }
    CHECK(whole && faded);
}

void testWidgets() {
    MemoryArchive data;
    data.files["Cache/Fonts/Test.seq16"] = fontFile({solidFrame(3, 5, 0xffff), solidFrame(4, 5, 0xffe0)}, 65);
    Font font;
    CHECK(!font.load(data, "Test"));
    image::Image picture;
    picture.w = 40;
    picture.h = 10;
    picture.rgba.assign(40 * 10 * 4, 200);
    // Dimmed, its colours scaled and its alpha kept.
    const image::Image dim = dimmed(picture, 0.5f);
    CHECK(dim.rgba[0] == 100 && dim.rgba[3] == 200);

    PictureButton button(picture, "CA&NCEL", &font);
    button.setPosition(10, 20);
    CHECK(button.label() == "CANCEL");
    Event ev;
    ev.type = EventType::kKeyDown;
    ev.key = 'n';
    ev.mods = keymod::kAlt;
    CHECK(button.handleEvent(ev, 100, 100, nullptr));
    Event mouse;
    mouse.button = MouseButton::kLeft;
    mouse.mouseX = 115;
    mouse.mouseY = 125;
    mouse.type = EventType::kMouseDown;
    CHECK(!button.handleEvent(mouse, 100, 100, nullptr));
    mouse.type = EventType::kMouseUp;
    CHECK(button.handleEvent(mouse, 100, 100, nullptr));

    // A toggle: its box, the label, and 1 between them.
    image::Image box;
    box.w = box.h = 5;
    box.rgba.assign(5 * 5 * 4, 255);
    Toggle toggle(box, box, "AB", &font);
    CHECK(toggle.width() == 5 + 7 + 1 && toggle.height() == 5);
    mouse.mouseX = 2;
    mouse.mouseY = 2;
    mouse.type = EventType::kMouseDown;
    CHECK(!toggle.handleEvent(mouse, 0, 0));
    mouse.type = EventType::kMouseUp;
    CHECK(toggle.handleEvent(mouse, 0, 0));
}

void testToolkit() {
    MemoryArchive data;
    // A font whose baseline is 11: 2 fifths.
    data.files["Cache/Fonts/Test.seq16"] = fontFile({solidFrame(3, 5, 0xffff), solidFrame(4, 5, 0xffe0)}, 65);
    Font font;
    CHECK(!font.load(data, "Test"));

    // A raised frame 2 thick: light on top and left, dark on the right
    // and bottom; inside it, as it was.
    Surface s;
    s.create(10, 10, PixelFormat::kRGBA32);
    drawBevel(s, 0, 0, 10, 10, 2, true);
    CHECK(s.row(0)[0] == 0xc8 && s.row(1)[1 * 4] == 0xc8 && s.row(5)[9 * 4] == 0x50 && s.row(9)[5 * 4] == 0x50);
    CHECK(s.row(5)[5 * 4] == 0);

    // A slider: as high as the baseline, 4 fifths and 2; dragged, its value
    // follows the mouse, kept to 0-99.
    Slider slider(&font, 100, {});
    slider.setWidth(112);
    slider.setPosition(0, 0);
    CHECK(slider.height() == 11 + 8 + 2);
    Event mouse;
    mouse.button = MouseButton::kLeft;
    mouse.type = EventType::kMouseDown;
    mouse.mouseX = 6 + 50;
    mouse.mouseY = 5;
    CHECK(slider.handleEvent(mouse, 0, 0) && slider.value() == 50);
    mouse.type = EventType::kMouseMove;
    mouse.mouseX = 500;
    CHECK(slider.handleEvent(mouse, 0, 0) && slider.value() == 99);
    mouse.type = EventType::kMouseUp;
    CHECK(!slider.handleEvent(mouse, 0, 0));
    mouse.type = EventType::kMouseMove;
    mouse.mouseX = 0;
    CHECK(!slider.handleEvent(mouse, 0, 0) && slider.value() == 99);
    // Clicked, it has the keys: Home to 0, Right a step up, Left a step
    // down, End to the top; Up and Down aren't its.
    Event press;
    press.type = EventType::kKeyDown;
    press.key = key::kHome;
    CHECK(slider.handleEvent(press, 0, 0) && slider.value() == 0);
    press.key = key::kRight;
    CHECK(slider.handleEvent(press, 0, 0) && slider.value() == 5);
    press.key = key::kUp;
    CHECK(!slider.handleEvent(press, 0, 0) && slider.value() == 5);
    press.key = key::kLeft;
    CHECK(slider.handleEvent(press, 0, 0) && slider.value() == 0);
    press.key = key::kEnd;
    CHECK(slider.handleEvent(press, 0, 0) && slider.value() == 99);

    // Its thumb's texture starts at the thumb's top left, not the screen's.
    image::Image fill;
    fill.w = 3;
    fill.h = 3;
    for (int i = 0; i < 9; ++i)
        fill.rgba.insert(fill.rgba.end(), {uint8_t(i * 20), 7, 9, 255});
    Slider textured(&font, 100, fill);
    textured.setWidth(112);
    textured.setPosition(4, 4);
    Surface screen;
    screen.create(130, 40, PixelFormat::kRGBA32);
    textured.draw(screen, 0, 0);
    const uint8_t *inside = screen.row(6) + 6 * 4;
    CHECK(inside[0] == 8 * 20 && inside[1] == 7);

    // A check box: its box the baseline and 2, then the label; a click or
    // its key turns it over.
    CheckBox box("&Pause", &font);
    CHECK(box.label() == "Pause" && box.height() == 13);
    Event key;
    key.type = EventType::kKeyDown;
    key.key = 'p';
    key.mods = keymod::kAlt;
    CHECK(box.handleEvent(key, 0, 0) && box.checked());
    CHECK(box.handleEvent(key, 0, 0) && !box.checked());
}

void testOptions(const std::filesystem::path &tmp) {
    // Without a file, the program's defaults; set, written and read back.
    const std::filesystem::path file = tmp / "saves" / "options.ini";
    GameOptions options;
    options.load(file);
    CHECK(options.scrollSpeed == 450.0f && options.scrollAccel == 15.0f && options.pauseWhenHit &&
          options.brightness == 1.0f);
    options.scrollSpeed = 900.0f;
    options.pauseWhenHit = false;
    options.brightness = 1.25f;
    options.save();
    GameOptions again;
    again.load(file);
    CHECK(again.scrollSpeed == 900.0f && !again.pauseWhenHit && again.brightness == 1.25f &&
          again.scrollAccel == 15.0f);

    // The sound's: defaults, then set and read back; on the mixer, music
    // -8 dB and effects whole, the master over both; muted, its least.
    CHECK(again.masterVolume == 0 && again.musicVolume == -800 && again.effectsVolume == 1.0f && again.stereo &&
          again.playVoiceovers && again.mixingChannels == 32);
    audio::Mixer mixer(22050);
    again.applySound(mixer);
    CHECK(mixer.typeVolume(audio::SoundType::kMusic) == 102 &&
          mixer.typeVolume(audio::SoundType::kEffects) == audio::kMaxVolume);
    again.masterVolume = -2000;
    again.musicMute = true;
    again.stereo = false;
    again.mixingChannels = 5;
    again.save();
    GameOptions third;
    third.load(file);
    CHECK(third.masterVolume == -2000 && third.musicMute && !third.stereo && third.mixingChannels == 5);
    third.applySound(mixer);
    CHECK(mixer.typeVolume(audio::SoundType::kEffects) == 26 && mixer.typeVolume(audio::SoundType::kMusic) == 1);

    // The brightness adds to each colour: at 1.25, 32 (red and blue in
    // 8s, green in 4s), kept to 255; under 1.0625, nothing.
    Surface screen;
    screen.create(2, 1, PixelFormat::kRGBA32);
    uint8_t *p = screen.row(0);
    p[0] = 10;
    p[1] = 20;
    p[2] = 240;
    brightenScreen(screen, 1.25f);
    CHECK(p[0] == 42 && p[1] == 52 && p[2] == 255);
    brightenScreen(screen, 1.05f);
    CHECK(p[0] == 42);
    brightenScreen(screen, 1.1f);
    CHECK(p[0] == 50 && p[1] == 64);
}

void testControls() {
    // The program's 48 actions, their default bindings shown as the
    // keyboard screen shows them.
    const auto &actions = gameActions();
    CHECK(actions.size() == 48 && std::string(actions[0].name) == "Walk");
    Controls controls;
    CHECK(controls.text(0) == "W");
    CHECK(controls.text(3) == "Left");
    CHECK(controls.text(21) == "], Wheel Down");
    CHECK(controls.text(25).empty());
    CHECK(keyName(0x4e) == "Num +" && scanCode('w') == 0x11 && scanCode(key::kKpPlus) == 0x4e &&
          scanCode(key::kLeft) == 0x14b);

    // Binding a key another action has takes it from that one.
    const auto lost = controls.bindKey(25, 0x11, 0);
    CHECK(lost.size() == 1 && lost[0] == "Walk" && controls.text(25) == "W" && controls.text(0).empty());
    CHECK(controls.bindKey(1, 0x13, 0x2a).empty() && controls.text(1) == "Shift+R");
    controls.clearKey(21);
    CHECK(controls.text(21) == "Wheel Down");

    // Kept as the program keeps them, and read back; reset, the defaults.
    Config file;
    controls.scheme = 1;
    controls.save(file);
    CHECK(file.get("NO JOYSTICK Control Configuration", "Movement Walk") == "0,0,0,0");
    Controls again;
    again.load(file);
    CHECK(again.text(25) == "W" && again.text(1) == "Shift+R" && again.scheme == 1);
    again.reset();
    CHECK(again.text(0) == "W" && again.text(25).empty());

    // The keyboard screen: a click on Walk waits for a key; Esc gives up,
    // the binding kept; Delete takes the key away; Enter presses EXIT.
    MemoryArchive data;
    data.files["Cache/Fonts/Trek Small.seq16"] = fontFile({solidFrame(3, 5, 0xffff)}, 65);
    TextObject rules;
    GameOptions options;
    KeyConfigScreen screen(data, rules, options);
    Event click;
    click.button = MouseButton::kLeft;
    click.mouseX = 10;
    click.mouseY = 2 + 0x18 + 5;
    const auto clickWalk = [&] {
        click.type = EventType::kMouseDown;
        (void)screen.handleEvent(click, nullptr, 0);
        click.type = EventType::kMouseUp;
        (void)screen.handleEvent(click, nullptr, 0);
    };
    Event press;
    press.type = EventType::kKeyDown;
    clickWalk();
    press.key = key::kEscape;
    CHECK(!screen.handleEvent(press, nullptr, 0) && options.controls.text(0) == "W");
    clickWalk();
    press.key = key::kDelete;
    CHECK(!screen.handleEvent(press, nullptr, 0) && options.controls.text(0).empty());
    // A key is bound as every key is let go, with the one held before it,
    // whatever it is; 1-6 are the program's and can't be bound.
    const auto key = [&](EventType type, uint32_t k) {
        press.type = type;
        press.key = k;
        (void)screen.handleEvent(press, nullptr, 0);
    };
    clickWalk();
    key(EventType::kKeyDown, key::kLShift);
    key(EventType::kKeyDown, 'r');
    key(EventType::kKeyUp, 'r');
    CHECK(options.controls.text(0).empty());
    key(EventType::kKeyUp, key::kLShift);
    CHECK(options.controls.text(0) == "Shift+R");
    clickWalk();
    key(EventType::kKeyDown, '1');
    key(EventType::kKeyDown, 't');
    key(EventType::kKeyUp, 't');
    key(EventType::kKeyUp, '1');
    CHECK(options.controls.text(0) == "T");
    press.type = EventType::kKeyDown;
    press.key = key::kReturn;
    CHECK(screen.handleEvent(press, nullptr, 0));
}

std::vector<uint8_t> bytesOf(const std::string &text) {
    return {text.begin(), text.end()};
}

// An officer of the roster, with an item or none.
std::string officer(const char *first, const char *last, const char *field, const char *item) {
    std::string out = std::string("CTeamMemberInfo\n{\n\tFirst Name=") + first + "\n\tLast Name=" + last +
                      "\n\tRank=Ensign\n\tField=" + field + "\n\tPortrait=" + first +
                      "\n\tIs Available=1\n\tAttribute=Array\n\t{\n\t\tItem Count=1\n"
                      "\t\tAttribute=CTeamMemberAttribute\n\t\t{\n\t\t\tName=Speed\n\t\t\tValue=40\n\t\t}\n\t}\n"
                      "\tInventory=Array\n\t{\n";
    if (*item)
        out +=
            std::string(
                "\t\tItem Count=1\n\t\tInventory Item=CStarTrekInventoryItemHolder\n\t\t{\n\t\t\tInventory Definition=") +
            item + "\n\t\t\tCount=1\n\t\t}\n";
    else
        out += "\t\tItem Count=0\n";
    return out + "\t}\n}\n";
}

void testTeamSelection() {
    MemoryArchive data;
    data.files["Character/Starting Roster/Slovaak.txt"] = bytesOf(officer("Slovaak", "", "Engineering", ""));
    data.files["Character/Starting Roster/Ann Bee.txt"] = bytesOf(officer("Ann", "Bee", "Command", "Tricorder"));
    data.files["Character/Starting Roster/Cy Dee.txt"] = bytesOf(officer("Cy", "Dee", "Command", ""));
    data.files["Character/Character Portraits/Ann.TXT"] =
        bytesOf("CStarTrekFrame\n{\n\tRace=Human\n\tGender=Female\n\tHead Icon=Ann Head\n}\n");
    data.files["Character/Field/Command.txt"] = bytesOf("CFieldDefinition\n{\n}\n");
    data.files["Character/Field/Engineering.txt"] = bytesOf("CFieldDefinition\n{\n}\n");
    data.files["Levels/Mission 02/Mission 02.MDS"] =
        bytesOf("CMissionDescription\n{\n\tMission Name=The Test\n\tShow Team Selection=1\n"
                "\tTransporter Background=Shuttle\n\tMax Members To Start=2\n"
                "\tWho Should Start=Array\n\t{\n\t\tItem Count=1\n\t\tWho Should Start=Slovaak\n\t}\n"
                "\tItems=Array\n\t{\n\t\tItem Count=1\n\t\tItems=Tricorder\n\t}\n"
                "\tRecommended Items=Array\n\t{\n\t\tItem Count=1\n\t\tRecommend=Hypospray\n\t}\n}\n");
    data.files["Inventory/Item Definitions/Tricorder.txt"] =
        bytesOf("CStarTrekInventoryItemDefinition\n{\n\tPrintable Item Name=A Tricorder\n\tIcon Image=Tricorder\n"
                "\tInventory Type=Gadget\n\tDisplay In Inventory=1\n\tBriefing Script=Items\n"
                "\tBriefing Text=Tricorder\n}\n");
    data.files["Inventory/Item Types/!None.txt"] = bytesOf("CInventoryTypeDefinition\n{\n}\n");
    data.files["Inventory/Item Types/Gadget.txt"] = bytesOf("CInventoryTypeDefinition\n{\n}\n");
    data.files["Inventory/Item Types/Primary.txt"] = bytesOf("CInventoryTypeDefinition\n{\n}\n");

    std::vector<TeamMember> roster;
    CHECK(!loadRoster(data, roster) && roster.size() == 3);
    // In their files' order; a portrait's definition gives race and icon.
    CHECK(roster[0].id == "Ann Bee" && roster[0].name() == "Ann Bee" && roster[0].race == "Human");
    CHECK(roster[0].headIcon == "Ann Head" && roster[0].hasItem("Tricorder") && !roster[2].hasItem("Tricorder"));
    CHECK(roster[0].attributes.size() == 1 && roster[0].attributes[0].value == 40);
    CHECK(roster[2].name() == "Slovaak");
    CHECK((loadFields(data) == std::vector<std::string>{"Command", "Engineering"}));
    Mission mission;
    CHECK(loadMission(data, 3, mission));
    CHECK(!loadMission(data, 2, mission) && mission.name == "The Test" && mission.showTeamSelection);
    CHECK(mission.maxMembers == 2 && mission.items == std::vector<std::string>{"Tricorder"});
    CHECK(mission.recommendedItems == std::vector<std::string>{"Hypospray"});
    // The items by their files' names; the types but !None.
    const auto items = loadItems(data);
    CHECK(items.size() == 1 && items.at("Tricorder").printableName == "A Tricorder");
    CHECK(items.at("Tricorder").type == "Gadget" && items.at("Tricorder").displayInInventory);
    CHECK(items.at("Tricorder").briefingScript == "Items" && items.at("Tricorder").iconImage == "Tricorder");
    CHECK((loadItemTypes(data) == std::vector<std::string>{"Gadget", "Primary"}));

    // The buttons by their shortcuts: the screen's layout gives only them.
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tMenu=Array\n\t\t{\n\t\t\tItem Count=1\n"
                          "\t\t\tMenu=CAnimatedMenuInfo\n\t\t\t{\n\t\t\t\tMenu Name=Team Selection\n"
                          "\t\t\t\tMenu Definition=CStarTrekTeamSelectionMenu\n\t\t\t\t{\n"
                          "\t\t\t\t\tExitButton Text=E&XIT\n\t\t\t\t\tContinueButton Text=&START MISSION\n"
                          "\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n",
                          rules));
    TeamSelectionScreen screen(data, rules, mission, roster);
    // Who the mission says starts on the transporter.
    CHECK(screen.team() == std::vector<size_t>{2});
    Event start;
    start.type = EventType::kKeyDown;
    start.key = 's';
    start.mods = keymod::kAlt;
    // Slovaak hasn't the tricorder the mission needs.
    CHECK(screen.handleEvent(start, nullptr, 0) == TeamSelectionScreen::Choice::kNone);
    roster[2].inventory.push_back({"Tricorder", 1});
    TeamSelectionScreen ready(data, rules, mission, roster);
    CHECK(ready.handleEvent(start, nullptr, 0) == TeamSelectionScreen::Choice::kStartMission);
    // EXIT asks first.
    Event exit = start;
    exit.key = 'x';
    CHECK(ready.handleEvent(exit, nullptr, 0) == TeamSelectionScreen::Choice::kNone);
    Event yes = start;
    yes.key = key::kEscape;
    CHECK(ready.handleEvent(yes, nullptr, 0) == TeamSelectionScreen::Choice::kNone);
    Surface surface;
    surface.create(640, 480, PixelFormat::kRGBA32);
    ready.draw(surface, 0);
}
void testBriefing() {
    MemoryArchive data;
    data.files["Levels/Mission 01/Mission 01.MDS"] = bytesOf(
        "CMissionDescription\n{\n\tMission Name=The Test\n\tShow Briefing=1\n\tBriefing Movie=Generic/None.bik\n"
        "\tLoop movie=1\n\tBriefing Music=Briefing.ogg\n\tScript=CBriefingScript\n\t{\n\t\tScript=Array\n\t\t{\n"
        "\t\t\tArray Count=2\n"
        "\t\t\tArray Item=CBriefingScriptLine\n\t\t\t{\n\t\t\t\tScript=Mission 01/Mission 01\n"
        "\t\t\t\tText=01 One\n\t\t\t\tWho is speaking=Marcus Refelian\n\t\t\t\tFrame to start=-1\n\t\t\t}\n"
        "\t\t\tArray Item=CBriefingScriptLine\n\t\t\t{\n\t\t\t\tScript=Mission 01/Mission 01\n"
        "\t\t\t\tText=01 Two\n\t\t\t\tWho is speaking=Data\n\t\t\t\tFrame to start=707\n\t\t\t}\n"
        "\t\t}\n\t}\n}\n");
    data.files["Levels/Mission 01/Mission 01.RDS"] =
        bytesOf(":01 One\nThe first line of the briefing.\n:01 Two\nThe second.\n");
    Mission mission;
    CHECK(!loadMission(data, 1, mission) && mission.showBriefing && mission.loopMovie);
    CHECK(mission.briefingMovie == "Generic/None.bik" && mission.briefingMusic == "Briefing.ogg");
    CHECK(mission.briefing.size() == 2 && mission.briefing[0].script == "Mission 01/Mission 01");
    CHECK(mission.briefing[1].text == "01 Two" && mission.briefing[1].speaker == "Data");
    CHECK(mission.briefing[0].frameToStart == -1 && mission.briefing[1].frameToStart == 707);

    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tMenu=Array\n\t\t{\n\t\t\tItem Count=1\n"
                          "\t\t\tMenu=CAnimatedMenuInfo\n\t\t\t{\n\t\t\t\tMenu Name=Briefing Screen\n"
                          "\t\t\t\tMenu Definition=CStarTrekBriefingMenu\n\t\t\t\t{\n"
                          "\t\t\t\t\tContinueButton Text=&CONTINUE\n\t\t\t\t\tEndBriefingButton Text=&END BRIEFING\n"
                          "\t\t\t\t\tTextDisplay Text Speed=10\n\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n",
                          rules));
    GameOptions options;
    BriefingScreen screen(data, "", rules, mission, options, nullptr, 0);
    Surface surface;
    surface.create(640, 480, PixelFormat::kRGBA32);
    // Typed out at Rules.txt's speed: 10 characters a second.
    screen.draw(surface, 1000);
    CHECK(screen.line() == 0 && !screen.typed());
    // Space shows the line whole; with no voice or movie to wait for, the
    // next comes by itself.
    Event space;
    space.type = EventType::kKeyDown;
    space.key = ' ';
    CHECK(screen.handleEvent(space, 1000) == BriefingScreen::Choice::kNone);
    screen.draw(surface, 1010);
    CHECK(screen.line() == 0 && screen.typed());
    screen.draw(surface, 1020);
    CHECK(screen.line() == 1 && !screen.typed());
    Event end = space;
    end.key = 'e';
    end.mods = keymod::kAlt;
    CHECK(screen.handleEvent(end, 1030) == BriefingScreen::Choice::kEndBriefing);
}
void testDebriefing() {
    MemoryArchive data;
    data.files["Levels/Mission 01/Mission 01.MDS"] =
        bytesOf("CMissionDescription\n{\n\tMission Name=The Test\n\tShow Debriefing=1\n"
                "\tDebriefing Music=Debriefing.ogg\n\tPrimary Song=Full.ogg\n\tRandom Song=Array\n\t{\n"
                "\t\tItem Count=2\n\t\tRandom Song=Sub 1.ogg\n\t\tRandom Song=Sub 2.ogg\n\t}\n"
                "\tDebrief Script=CBriefingScript\n\t{\n\t\tScript=Array\n\t\t{\n"
                "\t\t\tArray Count=2\n"
                "\t\t\tArray Item=CBriefingScriptLine\n\t\t\t{\n\t\t\t\tScript=Mission 01/Mission 01\n"
                "\t\t\t\tText=01 Debrief\n\t\t\t\tWho is speaking=Marcus Refelian\n\t\t\t}\n"
                "\t\t\tArray Item=CBriefingScriptLine\n\t\t\t{\n\t\t\t\tScript=Mission 01/Mission 01\n"
                "\t\t\t\tText=01 Debrief2\n\t\t\t\tWho is speaking=Data\n\t\t\t}\n"
                "\t\t}\n\t}\n}\n");
    data.files["Levels/Mission 01/Mission 01.RDS"] = bytesOf(":01 Debrief\nWell done.\n:01 Debrief2\nAgreed.\n");
    Mission mission;
    CHECK(!loadMission(data, 1, mission) && mission.showDebriefing && mission.debriefingMusic == "Debriefing.ogg");
    CHECK(mission.debriefing.size() == 2 && mission.debriefing[1].speaker == "Data" && mission.briefing.empty());
    CHECK(mission.primarySong == "Full.ogg" &&
          (mission.randomSongs == std::vector<std::string>{"Sub 1.ogg", "Sub 2.ogg"}));
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tMenu=Array\n\t\t{\n\t\t\tItem Count=1\n"
                          "\t\t\tMenu=CAnimatedMenuInfo\n\t\t\t{\n\t\t\t\tMenu Name=Debriefing Screen\n"
                          "\t\t\t\tMenu Definition=CStarTrekDebriefingMenu\n\t\t\t\t{\n"
                          "\t\t\t\t\tContinueButton Text=&CONTINUE\n\t\t\t\t\tTextDisplay Text Speed=10\n"
                          "\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n",
                          rules));
    GameOptions options;
    DebriefingScreen screen(data, rules, mission, options, nullptr, {{ObjectiveStatus::kComplete, "Done", true}},
                            125.0f, 0);
    Surface surface;
    surface.create(640, 480, PixelFormat::kRGBA32);
    // The panels fade in one after another: the mission's from 0.75
    // seconds, the objectives' from 1.25, the text's from 1.75, each over
    // 0.75; the lines are typed once theirs is in.
    screen.draw(surface, 1000);
    CHECK(screen.shown(0) > 0.3f && screen.shown(0) < 0.4f && screen.shown(1) == 0.0f && screen.shown(2) == 0.0f);
    screen.draw(surface, 2500);
    CHECK(screen.shown(2) == 1.0f && screen.line() == 0 && !screen.typed());
    // Space shows a line whole; without a voice-over the next then comes
    // by itself; CONTINUE after the last goes on.
    Event space;
    space.type = EventType::kKeyDown;
    space.key = ' ';
    CHECK(screen.handleEvent(space, 2500) == DebriefingScreen::Choice::kNone && screen.typed());
    screen.draw(surface, 2510);
    CHECK(screen.line() == 1);
    screen.draw(surface, 3500);
    CHECK(screen.typed());
    CHECK(screen.handleEvent(space, 3500) == DebriefingScreen::Choice::kContinue);
}
void testCampaign(const std::filesystem::path &tmp) {
    // A mission's score: its base, and the points of each objective done
    // that isn't required (0x50ca70).
    MissionObjectives mission;
    mission.baseScore = 100;
    std::vector<Objective> objectives(3);
    const auto set = [&](size_t i, ObjectiveStatus status, bool required, int points) {
        objectives[i].status = status;
        objectives[i].required = required;
        objectives[i].points = points;
    };
    set(0, ObjectiveStatus::kComplete, true, 50);
    set(1, ObjectiveStatus::kComplete, false, 25);
    set(2, ObjectiveStatus::kFailed, false, 30);
    CHECK(missionScore(mission, objectives) == 125);
    // Set to stun: a Weapon Setting of Stun, or the hand weapons' Weapon
    // Kills of 0 (the neck pinch's); by default an item kills.
    const auto setTo = [](const char *fields) {
        TextObject held;
        CHECK(!readTextObject(std::string("CStarTrekHandWeaponInventoryItem\n{\n") + fields + "}\n", held));
        return setToStun(held);
    };
    CHECK(setTo("\tWeapon Setting=Stun\n") && !setTo("\tWeapon Setting=Kill\n"));
    CHECK(setTo("\tWeapon Kills=0\n") && !setTo("\tWeapon Kills=1\n") && !setTo(""));
    // An officer's items as had: one not found yet left out; one overridden
    // by an item found since, that one, and on along those found.
    std::map<std::string, ItemDefinition> defs;
    defs["Phaser"].overriddenBy = "Phaser Damage Increase";
    defs["Phaser Damage Increase"].startsDiscovered = false;
    defs["Phaser Damage Increase"].overriddenBy = "Phaser Overload";
    defs["Phaser Overload"].startsDiscovered = false;
    defs["Mines"].startsDiscovered = false;
    CHECK(heldAs(defs, "Phaser", {}) == "Phaser" && heldAs(defs, "Mines", {}).empty());
    CHECK(heldAs(defs, "Phaser", {"Phaser Damage Increase"}) == "Phaser Damage Increase");
    CHECK(heldAs(defs, "Phaser", {"Phaser Damage Increase", "Phaser Overload"}) == "Phaser Overload");
    std::vector<TeamMember> officers(1);
    officers[0].inventory = {{"Phaser", -1}, {"Mines", 2}, {"Tricorder", -1}};
    resolveInventories(officers, defs, {"Phaser Damage Increase"});
    CHECK(officers[0].inventory.size() == 2 && officers[0].inventory[0].name == "Phaser Damage Increase" &&
          officers[0].inventory[1].name == "Tricorder");
    // Items found as the score reaches their points, once.
    std::map<std::string, ItemDefinition> items;
    items["Phaser"].startsDiscovered = true;
    items["Remote Mine"].startsDiscovered = false;
    items["Remote Mine"].pointsToDiscover = 250;
    items["Super Rifle"].startsDiscovered = false;
    items["Super Rifle"].pointsToDiscover = 900;
    items["Holographic Projection"].startsDiscovered = false;
    items["Holographic Projection"].pointsToDiscover = -1;
    Campaign campaign;
    campaign.score = 300;
    CHECK(foundByScore(items, campaign) == std::vector<std::string>{"Remote Mine"});
    campaign.discovered.push_back("Remote Mine");
    CHECK(foundByScore(items, campaign).empty());
    // Written and read back; listed newest first.
    SaveGame save;
    save.name = "Before the outpost";
    save.savedAt = 1000;
    save.missionName = "Mission 01: Klingon Outpost";
    save.campaign = campaign;
    save.campaign.mission = 2;
    save.campaign.toDescribe = {"Super Rifle"};
    const std::filesystem::path folder = tmp / "Save Games";
    CHECK(!writeSaveGame(folder / "a.sav", save));
    SaveGame back;
    CHECK(!readSaveGame(folder / "a.sav", back));
    CHECK(back.name == save.name && back.savedAt == 1000 && back.campaign.mission == 2 && back.campaign.score == 300 &&
          back.campaign.discovered == save.campaign.discovered && back.campaign.toDescribe == save.campaign.toDescribe);
    save.name = "Later";
    save.savedAt = 2000;
    CHECK(!writeSaveGame(folder / "b.sav", save));
    const auto saves = listSaveGames(folder);
    CHECK(saves.size() == 2 && saves[0].second.name == "Later");
}

void testSaveScreen(const std::filesystem::path &tmp) {
    MemoryArchive data;
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n{\n\tMenu=Array\n\t{\n\t\tItem Count=2\n"
                          "\t\tMenu=CAnimatedMenuInfo\n\t\t{\n\t\t\tMenu Name=Save Game Screen\n"
                          "\t\t\tMenu Definition=CStarTrekSaveGameMenu\n\t\t\t{\n\t\t\t\tList Pos=57,96\n"
                          "\t\t\t\tList Size=362,352\n\t\t\t}\n\t\t}\n"
                          "\t\tMenu=CAnimatedMenuInfo\n\t\t{\n\t\t\tMenu Name=Load Game Screen\n"
                          "\t\t\tMenu Definition=CStarTrekLoadGameMenu\n\t\t\t{\n\t\t\t\tList Pos=57,96\n"
                          "\t\t\t\tList Size=362,352\n\t\t\t}\n\t\t}\n\t}\n}\n}\n",
                          rules));
    CHECK(menuDefinition(rules, "Load Game Screen") != nullptr);
    const std::filesystem::path folder = tmp / "Save Games";
    SaveGame toSave;
    toSave.name = "Mission 03: The";
    toSave.campaign.mission = 3;
    // Saving: the name offered, typed on, saved by Enter.
    SaveGameScreen save(data, rules, SaveGameScreen::Mode::kSave, folder, toSave);
    Event typed;
    typed.type = EventType::kTextInput;
    std::snprintf(typed.text, sizeof typed.text, " Tower");
    CHECK(save.handleEvent(typed, nullptr) == SaveGameScreen::Choice::kNone);
    Event enter;
    enter.type = EventType::kKeyDown;
    enter.key = key::kReturn;
    CHECK(save.handleEvent(enter, nullptr) == SaveGameScreen::Choice::kSaved);
    // Loading: it's listed and chosen.
    SaveGameScreen load(data, rules, SaveGameScreen::Mode::kLoad, folder);
    CHECK(load.saves().size() == 1 && load.saves()[0].second.name == "Mission 03: The Tower");
    CHECK(load.handleEvent(enter, nullptr) == SaveGameScreen::Choice::kLoad && load.chosen().campaign.mission == 3);
}

void testEscapeMenu() {
    MemoryArchive data;
    TextObject rules;
    CHECK(!readTextObject("CRules\n{\n\tCustom Rules=CCustomRules\n\t{\n\t\tMenu=Array\n\t\t{\n\t\t\tItem Count=1\n"
                          "\t\t\tMenu=CAnimatedMenuInfo\n\t\t\t{\n\t\t\t\tMenu Name=Confirm Dialog\n"
                          "\t\t\t\tMenu Definition=CStarTrekConfirmDialogMenu\n\t\t\t\t{\n"
                          "\t\t\t\t\tYesButton Text=&Yes\n\t\t\t\t\tNoButton Text=&No\n"
                          "\t\t\t\t}\n\t\t\t}\n\t\t}\n\t}\n}\n",
                          rules));
    EscapeWindow window(data, rules, 640, 480);
    // Its six buttons, all of them to use.
    const auto &buttons = window.buttons();
    CHECK(buttons.size() == 6 && buttons[0].label() == "CONFIGURE" && buttons[5].label() == "RESUME");
    CHECK(buttons[0].enabled() && buttons[1].enabled() && buttons[2].enabled() && buttons[3].enabled());
    // RESTART MISSION asks first: no keeps the window, yes restarts.
    Event key;
    key.type = EventType::kKeyDown;
    key.key = 'm';
    key.mods = keymod::kAlt;
    CHECK(window.handleEvent(key, nullptr) == EscapeWindow::Choice::kNone);
    key.key = 'n';
    CHECK(window.handleEvent(key, nullptr) == EscapeWindow::Choice::kNone);
    key.key = 'm';
    CHECK(window.handleEvent(key, nullptr) == EscapeWindow::Choice::kNone);
    key.key = 'y';
    CHECK(window.handleEvent(key, nullptr) == EscapeWindow::Choice::kRestart);
    key.key = 'c';
    CHECK(window.handleEvent(key, nullptr) == EscapeWindow::Choice::kConfigure);
    // In the dialog, Left gives Yes the focus, and Enter presses it.
    key.key = 'm';
    CHECK(window.handleEvent(key, nullptr) == EscapeWindow::Choice::kNone);
    key.key = key::kLeft;
    CHECK(window.handleEvent(key, nullptr) == EscapeWindow::Choice::kNone);
    key.key = key::kReturn;
    CHECK(window.handleEvent(key, nullptr) == EscapeWindow::Choice::kRestart);
    // Enter in the dialog presses No, its default button.
    key.key = 'm';
    CHECK(window.handleEvent(key, nullptr) == EscapeWindow::Choice::kNone);
    key.key = key::kReturn;
    CHECK(window.handleEvent(key, nullptr) == EscapeWindow::Choice::kNone);
    key.key = 'y';
    CHECK(window.handleEvent(key, nullptr) == EscapeWindow::Choice::kNone);
}
} // namespace

static const reluva::test::Suite
    kSuite("stawayteam.unit.menus", {
                                        {"font", [](const std::filesystem::path &) { testFont(); }},
                                        {"buttons", [](const std::filesystem::path &) { testButtons(); }},
                                        {"credits", [](const std::filesystem::path &) { testCredits(); }},
                                        {"widgets", [](const std::filesystem::path &) { testWidgets(); }},
                                        {"toolkit", [](const std::filesystem::path &) { testToolkit(); }},
                                        {"options", [](const std::filesystem::path &tmp) { testOptions(tmp); }},
                                        {"controls", [](const std::filesystem::path &) { testControls(); }},
                                        {"team selection", [](const std::filesystem::path &) { testTeamSelection(); }},
                                        {"briefing", [](const std::filesystem::path &) { testBriefing(); }},
                                        {"debriefing", [](const std::filesystem::path &) { testDebriefing(); }},
                                        {"escape menu", [](const std::filesystem::path &) { testEscapeMenu(); }},
                                        {"campaign", [](const std::filesystem::path &tmp) { testCampaign(tmp); }},
                                        {"save screen", [](const std::filesystem::path &tmp) { testSaveScreen(tmp); }},
                                    });
