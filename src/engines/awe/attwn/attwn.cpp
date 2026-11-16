// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/attwn/attwn.h"

#include "engines/awe/attwn/acts.h"
#include "engines/awe/attwn/files.h"
#include "engines/awe/attwn/inventory.h"
#include "engines/awe/attwn/overlay.h"
#include "engines/awe/attwn/script.h"

#include <string>

namespace reluva::awe::attwn {

namespace {

// Narracott's "nothing to say" (0x4325a0): narracottgen's part 5 for the
// women, 6 for the men, "I have nothing more to say to her" or "him". The
// list's test is a "contains", so before anyone's been talked to it's "her".
std::string noTalk(const std::string &generic, int &part) {
    const std::string women = "brentgen claythornegen erogersgen mothergen";
    part = women.find(generic) != std::string::npos ? 0x68 : 0x69;
    return "narracottgen";
}

} // namespace

const GameInfo &game() {
    static const GameInfo kGame{
        .id = "attwn",
        // ac.exe's own, given to CreateWindowExA at 0x401980 (through
        // [0x47c0c0]).
        .title = "Agatha Christie - And Then There Were None",
        // Built with `reluva --hash`, as Orient Express's: the executable and
        // the item list.
        .variants = {{"attwn",
                      "Agatha Christie: And Then There Were None",
                      "en",
                      "windows",
                      {{"ac.exe", "24bc962a1f2acfb788132d0ec198063d", 593920},
                       {"Text/itemlist.act", "b9f27db08f64b7340ccbaaef131bf9b8", 12306}}}},
        .fallback = {{"attwn",
                      "Agatha Christie: And Then There Were None (unknown release)",
                      "",
                      "windows",
                      {{"ac.exe", "", -1}, {"Text/itemlist.act", "", -1}, {"granny2.dll", "", -1}}}},
        .playable = true, // played from a new game to the credits (TESTING.md)
        .openFiles = openFiles,
        // Its window's client area (CreateWindowExA at 0x401980: 0x320 by
        // 0x258); the backgrounds and cameras are drawn at it.
        .width = 800,
        .height = 600,
        .condition = condition,
        .verb = verb,
        .clock = true,
        .movie = movie,
        // The table loader (0x431160) reads 14 columns: the 7th, Tone (notes
        // for the voices: "sleeping noises"), it drops; Duration and Location
        // it doesn't read; Line Translation it keeps apart (none in the
        // English release). Lines past 750 pixels wrap (0x431cba).
        .dialogue = {.target = false,
                     .duration = false,
                     .optional = false,
                     .translation = false,
                     .wrap = 750,
                     // At an act's start the manager is emptied, then has
                     // Narracott's and the act's (0x4174b0, 0x402c78), the
                     // act's characters' as it loads (0x403154), and a
                     // place's as it's entered (0x4033f4).
                     .always = {"dialogue/general/narracott.txt"},
                     .acts = "dialogue/acts/act",
                     .characters = "dialogue/general/%s.txt",
                     .places = "dialogue/locations/%s.txt",
                     // Tried in turn (0x43357a); only Acts/Events is in the install.
                     .events = {"dialogue/acts/events/", "dialogue/location/events/", "dialogue/general/events/"}},
        // Its dialogue manager, against Orient Express's:
        // docs/engines/awe/games/attwn/systems/dialogue.md.
        .talk = {.player = "narracott",
                 .generic = "%sgen",   // 0x4323d0
                 .genericExact = true, // 0x4324f4
                 .goodbyes = "%sexit", // 0x432ad0
                 .goodbyesElse = nullptr,
                 .finale = nullptr,
                 .regreet = false, // 0x4328fa
                 .noTalk = noTalk,
                 .noTalkAlways = true, // 0x40d74f
                 .talkOptions = 1,     // 0x40d6af
                 .greetAndPlus = false,
                 .loneChoicePlays = false, // 0x432ec1
                 .repeatsStayOffered = false,
                 .alternativeKept = false, // 0x431f48
                 .offerLooksOn = false,    // 0x433040
                 .extrasHeard = true,      // 0x433c49 saves, 0x432300 reads back
                 .advance = false,
                 .keepBox = "talk ", // 0x4339ff
                 .dropBox = "fade ",
                 .leaveBoxWins = true,
                 .goodbyeLeft = true, // 0x432dc0
                 .textRate = 10,      // 0x4333f7
                 .textMin = 2,
                 .playerResetsColour = true, // 0x433254
                 .box = 90,
                 .finaleBox = 90,
                 .boxByRows = {90, 115, 141}, // 0x4328b6
                 .closesInventory = false,
                 .speakerElse = {{"brent", "brentknit"}, {"brent", "brentwig"}}}, // 0x433364
        // The camera's constructor (0x408510), the light's (0x41d5a0), and
        // the light's night test (0x41d760).
        .lighting = {.cameraAmbient = 0.5f,
                     .lightDiffuse = 1,
                     .att0 = 0,
                     .att1 = 0.0001f,
                     .nightSparesLights = true,
                     .nightPictures = {0.5078125f, 0.625f, 0.78125f}, // 0x4010f0, used at 0x42e89c
                     .nightFiles = true},                             // 0x4080f9
        // Its fonts, made in its code (0x4151fc on); the dialogue's is the
        // second (0x431caa). The rest's uses aren't read yet.
        .fonts = {{"font 0", "interface/fonts/arial.ttf", 12, true},
                  {"dialogue", "interface/fonts/arialn.ttf", 12, false},
                  {"font 2", "interface/fonts/innswfte.ttf", 38, false},
                  {"font 3", "interface/fonts/arial.ttf", 10, true},
                  {"font 4", "interface/fonts/rulyrrg_.ttf", 14, false},
                  {"font 5", "interface/fonts/rulyrrg_.ttf", 14, true},
                  {"font 6", "interface/fonts/arialn.ttf", 10, false},
                  {"font 7", "interface/fonts/innswfte.ttf", 18, false},
                  {"font 8", "interface/fonts/innswfte.ttf", 24, false}},
        // The box's draw (0x434220, the rows 0x434bb0): three windows by the
        // choices' rows, the choices from 25 across and 10 down, none drawn
        // from y 585; its colours its own (0x433220, 0x430af6).
        .dialogueBox = {.windows = {"interface/dialoguewin01.tga", "interface/dialoguewin02.tga",
                                    "interface/dialoguewin03.tga"},
                        .bullet = "interface/dialoguewin04.tga",
                        .bulletOn = "interface/dialoguewin05.tga",
                        .left = 25,
                        .first = 10,
                        .below = 585,
                        .colours = {0xffd2aa6e, 0xfff2e7bd, 0xffd2aa6e, 0xffc08a6f, 0xff8f9cb0}},
        // Saved Games\slot%d.ac1 and .bmp (0x41e917, 0x41ebb4), written by
        // 0x412e9f.
        // Its screens' rows (0x41eca0, 0x41ef80).
        .saves = {.layout = {.narrow = true, .moving = false, .act = false, .timers = false, .shortCodes = true},
                  .extension = ".ac1",
                  .screen = {.rows = {57, 222, 389},
                             .left = 521,
                             .right = 721,
                             .height = 150,
                             .thumbnail = 522,
                             .name = 404,
                             .date = 526,
                             .timeEnd = 716,
                             .font = "font 7",
                             .picked = 0xffffffff,
                             .other = 0xff808080,
                             .glowSave = "interface/scenes/cutouts/glow.tga",
                             .glowLoad = "interface/scenes/cutouts/glow.tga",
                             .empty = "EMPTY"},
                  // Written by "savelast" (0x40cdf9), asked of by
                  // "lastgame" (0x40a56d); nothing loads it.
                  .last = "last"},
        // Its start (0x40279b), its chapters, its scenes by place and act
        // (0x403310, 0x4035d0), and its act's music when a scene has none
        // (0x42d742).
        .flow = {.start = "movie adventure, movie awe, movie agathalogo, movie soundblaster, scene mainmenu01",
                 .scenes = ".ac1",
                 .readActs = readChapters,
                 .placeOf = placeOf,
                 .actFolders = true,
                 .actMusic = true,
                 .musica = "sound\\%s.wav", // 0x40c42a
                 // interface\scenes\<name>.ac1 (0x403990); Escape alone
                 // (0x416f0c).
                 .menu = "%s",
                 .mainMenu = "mainmenu",
                 .letterKeys = false,
                 .newGame = "newover",
                 // The act's loading step (0x416370, 0x4164f0, drawn at
                 // 0x416390): interface\loading.jpg, fonts 2 and 8.
                 .actLoading = "interface/loading.jpg",
                 .actLoadingFont = "font 2",
                 .actLoadingWaitFont = "font 8",
                 .actLoadingRows = {225, 305, 345},
                 // 0x40ba29: 0.025 s a step for an object, 0.01 the screen.
                 .objectFade = 0.025f},
        // The options screen's slider (0x4244f0) and labels (0x424290).
        .options = {.thumbLeft = 515,
                    .thumbStep = 18,
                    .snap = 9,
                    .font = "font 7",
                    .colour = 0xffffffff,
                    .sliderLabels = 512,
                    .sliderRows = {80, 145, 210},
                    .boxLabels = 645,
                    .boxRows = {255, 305, 355, 405, 455, 505}},
        // Its items: no passports or cards; pictures in Inventory\<item>;
        // the characters' table, the line for nothing, the take's sound
        // (0x418bf6, 0x4194f0, 0x419380, 0x40d56e); its states 1 just
        // taken, 3 in the bag, 4 in hand, 5 used, 12 a page (0x412e9f).
        .inventory = {.cards = false,
                      .cursor = "inventory/%s/cur%s.tga",
                      .cursorAt = {22, 23}, // 0x4162ce
                      .characters = "charlist.txt",
                      .readCharacters = readCharacters,
                      .combinations = "uselist.txt",
                      .cantUse = "talk narracottgen 2",
                      .takeSound = "interface\\inventory",
                      .saveStates = {0, 1, 3, 4, 5, 5, 5, 5},
                      .perPage = 12},
        // The safe room's safe (0x42b020): its two dials, the number
        // kept, 28 11 49 (0x42b0ec).
        .safe = {.kind = "safecombo",
                 .combination = {28, 11, 49},
                 .dials = {"largedial.tga", "smalldial.tga"},
                 .again = "talk saferoom 10",
                 .opened = "talk narracottgen 122",
                 .kept = true,
                 .solved = "safecombodone"},
        .overlay = makeOverlay, // the journal and the inventory
        .exe = "ac.exe",
        // Orient Express's path-finder, the same code (24 functions) but
        // for a call of its debug log, an empty function here (0x407af0):
        // docs/engines/awe/games/attwn/formats/ac1.md.
        .pathFinder = {.statics = {0x426600, {0xdd, 0x05, 0x18, 0x4b, 0x47, 0x00, 0xdc, 0x25}},
                       .constructor = {0x426620, {0x8b, 0xc1, 0xb1, 0xff, 0xc7, 0x00, 0x20, 0x4b}},
                       .buildTables = {0x4266e0, {0x56, 0x8b, 0xf1, 0x57, 0xb9, 0x00, 0x04, 0x00}},
                       .search = {0x426d20, {0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8, 0x83, 0xec}},
                       .operatorNew = {0x46622c, {0x6a, 0x01, 0xff, 0x74, 0x24, 0x08, 0xe8, 0xec}},
                       .operatorDelete = {0x4661d2, {0xff, 0x74, 0x24, 0x04, 0xe8, 0xcd, 0x14, 0x00}},
                       .objectSize = 0x4d4600},
    };
    return kGame;
}

} // namespace reluva::awe::attwn
