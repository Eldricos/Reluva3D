// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// The menus' parts: the game's fonts as its text is drawn with them,
// buttons, their labels, shortcuts and clicks, the credits' lines, and the
// team selection's roster, mission and rules.

#include "engines/stawayteam/ui/briefing.h"
#include "engines/stawayteam/ui/button.h"
#include "engines/stawayteam/ui/credits.h"
#include "engines/stawayteam/ui/font.h"
#include "engines/stawayteam/game/controls.h"
#include "engines/stawayteam/game/options.h"
#include "engines/stawayteam/game/team.h"
#include "engines/stawayteam/ui/team_selection.h"
#include "engines/stawayteam/ui/widgets.h"

#include "tests/engines/stawayteam/support.h"
#include "tests/support/test.h"

#include <map>
#include <string>
#include <vector>

using namespace reluva;
using namespace reluva::stawayteam;

namespace {

// Files in memory, by name as given.
class MemoryArchive : public Archive {
public:
    std::map<std::string, std::vector<uint8_t>, std::less<>> files;

    std::vector<std::string> list() const override {
        std::vector<std::string> names;
        for (const auto &[name, data] : files)
            names.push_back(name);
        return names;
    }
    bool has(std::string_view name) const override { return files.find(name) != files.end(); }
    Error read(std::string_view name, std::vector<uint8_t> &out) const override {
        const auto it = files.find(name);
        if (it == files.end())
            return Error::fail(std::string(name) + " isn't there");
        out = it->second;
        return Error::success();
    }
    std::string describe() const override { return "memory"; }
};

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

    Event ev;
    ev.type = EventType::kKeyDown;
    ev.key = 'q';
    CHECK(group.handleEvent(ev, nullptr) == 1);
    ev.mods = keymod::kCtrl;
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

    // A check box: its box the baseline and 2, then the label; a click or
    // its key turns it over.
    CheckBox box("&Pause", &font);
    CHECK(box.label() == "Pause" && box.height() == 13);
    Event key;
    key.type = EventType::kKeyDown;
    key.key = 'p';
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
                "\tItems=Array\n\t{\n\t\tItem Count=1\n\t\tItems=Tricorder\n\t}\n}\n");

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
    CHECK(screen.handleEvent(end, 1030) == BriefingScreen::Choice::kEndBriefing);
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
                                    });
