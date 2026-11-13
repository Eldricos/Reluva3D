// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/newworlds/briefing.h"

#include <algorithm>
#include <cctype>

namespace reluva::newworlds {

namespace {

using V = BriefingView;

struct Site {
    int a = -1, b = -1; // a map position (eighths of a block, row first); -1 the whole map
};

struct Step {
    const char *clip; // the clip's number after the mission (and side) prefix; "" for none
    uint32_t page;
    V view;
    Site site = {};
    const char *model = ""; // for a model view
};

// The models the recordings' model views showed, matched by shape and
// colour against the archives' models (forbuild, foractor, fedbuild,
// kliactor).
constexpr const char *kTemple = "geo/bad/tt.geo";       // the Taubat temple
constexpr const char *kHubrinCraft = "geo/bad/h.geo";   // a red and white winged craft
constexpr const char *kMetarDevice = "geo/bad/mad.geo"; // a blue faceted crystal, J8's anomaly device
constexpr const char *kPillar = "geo/bad/hcp.geo";      // the containment pillar
constexpr const char *kMetarCore = "geo/bad/mch.geo";   // a Metar spire
constexpr const char *kShieldGen = "geo/fed/fsg.geo";   // the Federation shield generator
constexpr const char *kDisruptorTank = "geo/kln/kdt1.geo";
constexpr const char *kDerelict = "geo/bad/odv.geo";      // F2's derelict (its metamodel: an Orion disruptor tank)
constexpr const char *kMetarPortal = "geo/bad/mport.geo"; // the Metar portal, a vehicle dock, all blue-streaked wings
constexpr const char *kMetarNak = "geo/bad/mn_a.geo";     // a Metar nak: long-nosed, a four-pointed star head-on
constexpr const char *kVehicleYard = "geo/fed/fvpy_a.geo";
constexpr const char *kTaubatHangar = "geo/bad/tw.geo";  // the Taubat triangle ("Taubat hangar" in the panel)
constexpr const char *kRomScience = "geo/rom/rsb_a.geo"; // a low faceted dome: the Romulan science block (a guess)
constexpr const char *kRomShield = "geo/rom/rcss.geo";   // a four-winged pyramid (a guess)
constexpr const char *kRomVehicleYard = "geo/rom/rvy.geo";

struct Script {
    const char *mission;
    char side;
    std::vector<Step> steps;
};

// Places the recordings' messages give positions for (map positions,
// eighths of a block, row first). The recordings' map views can't be
// placed exactly (their position readout follows the mouse), so each map
// step looks at the place its page or view is about: the colony (taken
// from the mines it reported exhausted early, which lie beside it), or a
// site a message named. Steps about places no message located, and the
// turning overviews, show the whole map.
constexpr Site kF1Colony{407, 209}, kF1Temple{332, 152}; // the start's readout; the energy source
constexpr Site kK1Colony{244, 92}, kK1Temple{372, 107};  // mine; the Taubat temple
constexpr Site kK2Colony{300, 284}, kK2Ally{279, 26};    // between the two mines; the allied base
constexpr Site kJ1Colony{52, 148}, kJ2Colony{92, 404}, kJ2Alien{112, 260};
constexpr Site kJ3Colony{204, 172}, kJ4Colony{340, 180}, kK12Colony{84, 252}, kK14Colony{100, 292};
// The Romulans' (play-r*.md, play-j*r.md): J1's from the start's readout,
// J2's from the readout it kept returning to, the rest from mines.
constexpr Site kR1Rival{165, 323}, kR2Colony{260, 356}, kJ1RColony{111, 442}, kJ2RColony{166, 184};
constexpr Site kJ3RColony{103, 311}, kJ4RColony{276, 380}, kR9Colony{308, 428}, kJ6RColony{92, 196};
constexpr Site kJ7RColony{200, 324}, kR12Colony{260, 264}, kR14Colony{260, 272};

// The briefings as recorded play showed them (docs/engines/newworlds/play-*.md):
// which page and view went with each clip. Where the recording didn't show
// a step (a clip not heard), the step is a guess from its neighbours.
const std::vector<Script> &recordedScripts() {
    static const std::vector<Script> kScripts = {
        {"F1",
         'F',
         {{"", 3, V::kEmblem},
          {"005", 2, V::kGalaxy},
          {"006", 4, V::kStarSystem},
          {"007", 0, V::kGround, kF1Temple},
          {"008", 0, V::kGround, kF1Colony},
          {"009", 6, V::kGround, kF1Colony},
          {"010", 7, V::kRelief, kF1Temple},
          {"011", 8, V::kModel, {}, kTemple},
          {"012", 9, V::kRelief, kF1Colony},
          {"013", 10, V::kGround, kF1Temple},
          {"014", 11, V::kGround, kF1Temple},
          {"015", 0, V::kEmblem}}},
        {"F2",
         'F',
         {{"", 3, V::kEmblem},
          {"005", 2, V::kGalaxy},
          {"006", 4, V::kStarSystem},
          {"007", 6, V::kGround},
          {"008", 7, V::kRelief},
          {"009", 8, V::kRelief},
          {"010", 9, V::kModel, {}, kDerelict},
          {"011", 10, V::kEmblem},
          {"012", 11, V::kEmblem}}},
        {"F3",
         'F',
         {{"", 10, V::kEmblem},
          {"005", 11, V::kGalaxy},
          {"006", 12, V::kStarSystem},
          {"007", 2, V::kGround},
          {"008", 3, V::kGround},
          {"009", 4, V::kGround},
          {"010", 5, V::kRelief},
          {"011", 7, V::kRelief},
          {"012", 6, V::kGround},
          {"013", 8, V::kGround},
          {"014", 9, V::kGround},
          {"015", 0, V::kEmblem},
          {"016", 0, V::kEmblem},
          {"017", 0, V::kEmblem}}},
        // Pages past PLANETARY DATA in table order (not read off the recording).
        {"J1",
         'F',
         {{"", 18, V::kEmblem},
          {"005", 3, V::kGalaxy},
          {"006", 4, V::kStarSystem},
          {"011", 5, V::kGround},
          {"012", 6, V::kModel, {}, "geo/fed/fch.geo"},
          {"013", 7, V::kRelief},
          {"014", 8, V::kGround},
          {"015", 9, V::kGround},
          {"016", 10, V::kRelief},
          {"017", 11, V::kRelief},
          {"018", 12, V::kGround},
          {"019", 13, V::kRelief},
          {"020", 14, V::kRelief},
          {"021", 15, V::kGround}}},
        // Pages past PLANETARY DATA in table order (not read off the recording).
        {"J3",
         'F',
         {{"", 10, V::kEmblem},
          {"005", 3, V::kGalaxy},
          {"006", 4, V::kStarSystem},
          {"007", 5, V::kStarSystem},
          {"008", 6, V::kRelief},
          {"009", 7, V::kGround},
          {"010", 8, V::kGround},
          {"011", 9, V::kGround},
          {"012", 0, V::kGround},
          {"013", 0, V::kEmblem}}},
        // 006 wasn't heard (commentary); the ship model isn't identified.
        {"J4",
         'F',
         {{"", 4, V::kEmblem},
          {"005", 3, V::kGalaxy},
          {"006", 0, V::kGalaxy},
          {"007", 0, V::kModel, {}, kMetarPortal},
          {"008", 0, V::kModel, {}, kMetarNak},
          {"009", 5, V::kStarSystem},
          {"010", 0, V::kRelief},
          {"011", 7, V::kGround},
          {"012", 8, V::kGround},
          {"013", 9, V::kModel, {}, "geo/kln/kss_a.geo"},
          {"014", 0, V::kEmblem}}},
        // 005 wasn't heard (commentary).
        {"F8",
         'F',
         {{"", 10, V::kEmblem},
          {"005", 2, V::kGalaxy},
          {"006", 3, V::kStarSystem},
          {"007", 4, V::kGround},
          {"008", 5, V::kModel, {}, kMetarDevice},
          {"009", 6, V::kRelief},
          {"010", 0, V::kEmblem},
          {"011", 0, V::kEmblem},
          {"012", 0, V::kEmblem},
          {"013", 0, V::kEmblem},
          {"014", 0, V::kEmblem}}},
        {"J6",
         'F',
         {{"", 8, V::kEmblem},
          {"005", 3, V::kGalaxy},
          {"006", 4, V::kStarSystem},
          {"007", 5, V::kGround},
          {"008", 6, V::kRelief},
          {"009", 7, V::kModel, {}, kMetarDevice},
          {"010", 0, V::kEmblem},
          {"011", 0, V::kEmblem},
          {"012", 0, V::kEmblem},
          {"013", 0, V::kEmblem},
          {"014", 0, V::kEmblem}}},
        // 005, 006, 009, 014 weren't heard (commentary).
        {"J7",
         'F',
         {{"", 7, V::kEmblem},
          {"005", 3, V::kGalaxy},
          {"006", 4, V::kStarSystem},
          {"007", 5, V::kRelief},
          {"008", 6, V::kModel, {}, kHubrinCraft},
          {"009", 0, V::kModel, {}, kHubrinCraft},
          {"010", 0, V::kGround},
          {"011", 0, V::kGround},
          {"012", 0, V::kEmblem},
          {"013", 0, V::kEmblem},
          {"014", 0, V::kEmblem}}},
        // 005 and 009 weren't heard (commentary).
        {"F12",
         'F',
         {{"", 10, V::kEmblem},
          {"005", 2, V::kGalaxy},
          {"006", 3, V::kStarSystem},
          {"007", 4, V::kGround},
          {"008", 5, V::kModel, {}, kPillar},
          {"009", 6, V::kRelief},
          {"010", 0, V::kEmblem}}},
        {"J2",
         'F',
         {{"", 6, V::kEmblem},
          {"005", 3, V::kGalaxy},
          {"006", 4, V::kStarSystem},
          {"007", 5, V::kGround},
          {"008", 0, V::kGround},
          {"009", 0, V::kEmblem}}},
        {"J8",
         'F',
         {{"", 7, V::kEmblem},
          {"005", 3, V::kGalaxy},
          {"006", 4, V::kStarSystem},
          {"007", 5, V::kModel, {}, kMetarDevice},
          {"008", 6, V::kGround},
          {"009", 0, V::kModel, {}, kVehicleYard},
          {"010", 0, V::kRelief},
          {"011", 0, V::kEmblem},
          {"012", 0, V::kEmblem},
          {"013", 0, V::kEmblem},
          {"014", 0, V::kEmblem}}},
        // 013 wasn't heard (commentary).
        {"F14",
         'F',
         {{"", 10, V::kEmblem},
          {"005", 2, V::kGalaxy},
          {"006", 3, V::kStarSystem},
          {"007", 4, V::kModel, {}, kPillar},
          {"008", 5, V::kGround},
          {"009", 6, V::kGround},
          {"010", 0, V::kRelief},
          {"011", 0, V::kModel, {}, kMetarCore},
          {"012", 0, V::kEmblem},
          {"013", 0, V::kEmblem}}},
        {"J9",
         'F',
         {{"", 7, V::kEmblem},
          {"005", 3, V::kModel, {}, kHubrinCraft},
          {"006", 4, V::kGalaxy},
          {"007", 5, V::kStarSystem},
          {"008", 6, V::kModel, {}, kMetarCore},
          {"009", 0, V::kModel, {}, kMetarCore},
          {"010", 0, V::kGround},
          {"011", 0, V::kRelief},
          {"012", 0, V::kModel, {}, kMetarCore},
          {"013", 0, V::kGalaxy},
          {"014", 0, V::kEmblem}}},
        {"C9", 'F', {{"", 59, V::kEmblem}}},
        {"K1",
         'K',
         {{"005", 2, V::kGalaxy},
          {"006", 3, V::kStarSystem},
          {"007", 4, V::kRelief},
          {"008", 5, V::kRelief, kK1Colony},
          {"009", 6, V::kGround, kK1Colony},
          {"010", 7, V::kGround, kK1Temple},
          {"011", 8, V::kGround, kK1Colony},
          {"012", 9, V::kGround, kK1Temple},
          {"013", 10, V::kEmblem}}},
        {"K2",
         'K',
         {{"005", 4, V::kGalaxy},
          {"006", 5, V::kStarSystem},
          {"007", 6, V::kGround, kK2Colony},
          {"008", 7, V::kModel, {}, kTemple},
          {"009", 8, V::kRelief},
          {"010", 9, V::kGround, kK2Ally},
          {"011", 10, V::kGround, kK2Colony},
          {"012", 0, V::kEmblem}}},
        {"J1",
         'K',
         {{"005", 3, V::kGalaxy},
          {"006", 4, V::kStarSystem},
          {"007", 5, V::kRelief},
          {"008", 6, V::kModel, {}, kTemple},
          {"009", 7, V::kRelief},
          {"010", 8, V::kGround, kJ1Colony},
          {"011", 9, V::kGround},
          {"012", 10, V::kRelief},
          {"013", 11, V::kRelief},
          {"014", 12, V::kEmblem}}},
        {"J2",
         'K',
         {{"005", 7, V::kGalaxy},
          {"006", 8, V::kStarSystem},
          {"007", 5, V::kGround, kJ2Colony},
          {"008", 0, V::kRelief, kJ2Alien},
          {"009", 0, V::kGround, kJ2Colony},
          {"010", 0, V::kEmblem},
          {"011", 0, V::kEmblem}}},
        {"J3",
         'K',
         {{"005", 10, V::kGalaxy},
          {"006", 9, V::kGalaxy},
          {"007", 3, V::kStarSystem},
          {"008", 0, V::kModel, {}, kTemple},
          {"009", 0, V::kModel, {}, kTemple},
          {"010", 5, V::kGround},
          {"011", 5, V::kGround},
          {"012", 0, V::kRelief},
          {"013", 6, V::kGround, kJ3Colony},
          {"014", 7, V::kEmblem}}},
        {"K7",
         'K',
         {{"005", 2, V::kGalaxy},
          {"006", 3, V::kStarSystem},
          {"007", 4, V::kGround},
          {"008", 0, V::kEmblem},
          {"009", 0, V::kEmblem},
          {"010", 0, V::kEmblem},
          {"011", 0, V::kEmblem},
          {"012", 0, V::kEmblem},
          {"013", 0, V::kEmblem}}},
        {"J4",
         'K',
         {{"005", 5, V::kGalaxy},
          {"006", 6, V::kStarSystem},
          {"007", 0, V::kModel, {}, kMetarDevice},
          {"008", 0, V::kRelief, kJ4Colony},
          {"009", 0, V::kGround, kJ4Colony},
          {"010", 7, V::kGround, kJ4Colony},
          {"011", 8, V::kGround, kJ4Colony},
          {"012", 9, V::kGround, kJ4Colony},
          {"013", 10, V::kGround, kJ4Colony},
          {"014", 11, V::kGround},
          {"015", 12, V::kEmblem}}},
        // 006-009 weren't heard in the recording (skipped); guessed.
        {"K8",
         'K',
         {{"005", 2, V::kGalaxy},
          {"006", 3, V::kStarSystem},
          {"007", 0, V::kStarSystem},
          {"008", 0, V::kStarSystem},
          {"009", 0, V::kStarSystem},
          {"010", 4, V::kGround},
          {"011", 5, V::kGround},
          {"012", 0, V::kGround},
          {"013", 0, V::kModel, {}, kDisruptorTank},
          {"014", 0, V::kEmblem},
          {"015", 0, V::kEmblem}}},
        {"J6",
         'K',
         {{"005", 3, V::kGalaxy},
          {"006", 4, V::kStarSystem},
          {"007", 5, V::kGround},
          {"008", 6, V::kModel, {}, kShieldGen},
          {"009", 7, V::kModel, {}, kMetarCore},
          {"010", 0, V::kEmblem},
          {"011", 0, V::kEmblem},
          {"012", 0, V::kEmblem},
          {"013", 0, V::kEmblem},
          {"014", 0, V::kEmblem}}},
        {"J7",
         'K',
         {{"005", 3, V::kGalaxy},
          {"006", 4, V::kStarSystem},
          {"007", 5, V::kGround},
          {"008", 6, V::kModel, {}, kHubrinCraft},
          {"009", 0, V::kEmblem},
          {"010", 0, V::kEmblem},
          {"011", 0, V::kEmblem},
          {"012", 0, V::kEmblem},
          {"013", 0, V::kEmblem},
          {"014", 0, V::kEmblem}}},
        {"K12",
         'K',
         {{"005", 2, V::kGalaxy},
          {"006", 3, V::kGalaxy},
          {"007", 4, V::kStarSystem},
          {"008", 5, V::kGround, kK12Colony},
          {"009", 6, V::kModel, {}, kPillar},
          {"009A", 0, V::kGround},
          {"010", 0, V::kGround, kK12Colony},
          {"011", 0, V::kRelief, kK12Colony},
          {"012", 0, V::kEmblem},
          {"013", 0, V::kEmblem}}},
        {"J8",
         'K',
         {{"", 7, V::kEmblem},
          {"005", 3, V::kGalaxy},
          {"006", 4, V::kStarSystem},
          {"007", 5, V::kModel, {}, kMetarDevice},
          {"008", 6, V::kGround},
          {"009", 0, V::kModel, {}, kHubrinCraft},
          {"010", 0, V::kGround},
          {"011", 0, V::kGround}}},
        // 007 and 009 weren't heard (commentary); their pages are guessed.
        {"K14",
         'K',
         {{"005", 2, V::kGalaxy},
          {"006", 3, V::kStarSystem},
          {"007", 4, V::kGround},
          {"008", 5, V::kModel, {}, kHubrinCraft},
          {"009", 6, V::kGround},
          {"010", 0, V::kRelief, kK14Colony},
          {"011", 0, V::kEmblem}}},
        {"J9",
         'K',
         {{"005", 3, V::kGalaxy},
          {"006", 4, V::kRelief},
          {"007", 5, V::kRelief},
          {"008", 6, V::kRelief},
          {"009", 0, V::kRelief}}},
        {"C10", 'K', {{"", 74, V::kEmblem}}},
        // The Romulans' (play-r1.md ...): the transmission page silent over
        // the emblem first, as the Federation's.
        {"R1",
         'R',
         {{"", 10, V::kEmblem},
          {"005", 22, V::kGalaxy},
          {"006", 2, V::kStarSystem},
          {"007", 0, V::kGround},
          {"008", 4, V::kModel, {}, kMetarPortal},
          {"009", 5, V::kModel, {}, kRomShield},
          {"010", 6, V::kRelief},
          {"011", 7, V::kGround},
          {"012", 8, V::kRelief, kR1Rival},
          {"013", 9, V::kEmblem}}},
        {"R2",
         'R',
         {{"", 3, V::kEmblem},
          {"005", 5, V::kGalaxy},
          {"006", 6, V::kStarSystem},
          {"007", 7, V::kGround},
          {"008", 8, V::kGround},
          {"009", 0, V::kGround, kR2Colony},
          {"010", 0, V::kGround, kR2Colony},
          {"011", 9, V::kModel, {}, kDerelict},
          {"013", 0, V::kModel, {}, kTaubatHangar},
          {"014", 10, V::kModel, {}, kTemple},
          {"015", 0, V::kEmblem},
          {"016", 0, V::kEmblem}}},
        {"J1",
         'R',
         {{"", 18, V::kEmblem},
          {"005", 3, V::kGalaxy},
          {"006", 4, V::kStarSystem},
          {"007", 5, V::kGround, kJ1RColony},
          {"008", 6, V::kGround},
          {"009", 7, V::kGround},
          {"010", 8, V::kRelief},
          {"011", 9, V::kEmblem}}},
        {"J2",
         'R',
         {{"", 6, V::kEmblem},
          {"005", 3, V::kGalaxy},
          {"006", 4, V::kStarSystem},
          {"007", 5, V::kGround, kJ2RColony},
          {"008", 0, V::kRelief},
          {"009", 0, V::kModel, {}, kRomScience},
          {"010", 0, V::kGround, kJ2RColony},
          {"011", 0, V::kEmblem}}},
        // 005 wasn't heard (commentary).
        {"J3",
         'R',
         {{"", 10, V::kEmblem},
          {"005", 3, V::kGalaxy},
          {"006", 4, V::kStarSystem},
          {"007", 5, V::kGround},
          {"008", 6, V::kGround},
          {"009", 7, V::kGround},
          {"010", 0, V::kGround, kJ3RColony},
          {"011", 8, V::kRelief},
          {"012", 0, V::kGround, kJ3RColony},
          {"013", 0, V::kEmblem}}},
        // 011 was heard twice and 014 not at all; the star system step has
        // no model of its own (a yellow haze in the recording).
        {"J4",
         'R',
         {{"", 3, V::kEmblem},
          {"005", 5, V::kGalaxy},
          {"006", 6, V::kStarSystem},
          {"007", 0, V::kRelief},
          {"008", 0, V::kGround},
          {"009", 0, V::kGround},
          {"010", 0, V::kGround},
          {"011", 0, V::kRelief},
          {"012", 7, V::kGround},
          {"013", 0, V::kGround, kJ4RColony},
          {"014", 0, V::kRelief},
          {"015", 0, V::kEmblem}}},
        {"R9",
         'R',
         {{"", 8, V::kEmblem},
          {"005", 2, V::kGalaxy},
          {"006", 3, V::kStarSystem},
          {"007", 4, V::kRelief},
          {"008", 5, V::kGround, kR9Colony},
          {"009", 6, V::kModel, {}, kMetarDevice},
          {"010", 7, V::kRelief},
          {"011", 0, V::kGround, kR9Colony},
          {"012", 0, V::kRelief},
          {"013", 0, V::kEmblem},
          {"014", 0, V::kEmblem}}},
        // The recording's views ran a step behind the pages (005 over the
        // emblem), as J7's.
        {"J6",
         'R',
         {{"", 8, V::kEmblem},
          {"005", 0, V::kEmblem},
          {"006", 4, V::kGalaxy},
          {"007", 5, V::kStarSystem},
          {"008", 6, V::kGround},
          {"009", 7, V::kRelief},
          {"010", 0, V::kModel, {}, kMetarDevice},
          {"011", 0, V::kGround},
          {"012", 0, V::kRelief},
          {"013", 0, V::kEmblem}}},
        {"J7",
         'R',
         {{"", 7, V::kEmblem},
          {"005", 3, V::kEmblem},
          {"006", 4, V::kEmblem},
          {"007", 5, V::kGalaxy},
          {"008", 6, V::kGalaxy},
          {"009", 0, V::kGalaxy},
          {"010", 0, V::kStarSystem},
          {"011", 0, V::kRelief, kJ7RColony},
          {"012", 0, V::kGround},
          {"013", 0, V::kEmblem}}},
        // 005 wasn't heard (commentary).
        {"R12",
         'R',
         {{"", 7, V::kEmblem},
          {"005", 2, V::kGalaxy},
          {"006", 3, V::kStarSystem},
          {"007", 4, V::kGround, kR12Colony},
          {"008", 5, V::kModel, {}, kPillar},
          {"009", 6, V::kRelief, kR12Colony}}},
        {"J8",
         'R',
         {{"", 7, V::kEmblem},
          {"005", 3, V::kGalaxy},
          {"006", 4, V::kStarSystem},
          {"007", 5, V::kModel, {}, kMetarDevice},
          {"008", 6, V::kModel, {}, kRomVehicleYard},
          {"009", 0, V::kEmblem}}},
        {"R14",
         'R',
         {{"", 7, V::kEmblem},
          {"005", 2, V::kGalaxy},
          {"006", 3, V::kStarSystem},
          {"007", 4, V::kModel, {}, kPillar},
          {"008", 5, V::kRelief},
          {"009", 6, V::kGround, kR14Colony},
          {"010", 0, V::kEmblem}}},
        {"C11", 'R', {{"", 80, V::kEmblem}}},
        {"J9",
         'R',
         {{"", 7, V::kEmblem},
          {"005", 3, V::kGalaxy},
          {"006", 4, V::kGround},
          {"007", 5, V::kModel, {}, kMetarDevice},
          {"008", 6, V::kModel, {}, kMetarCore},
          {"009", 0, V::kEmblem}}},
    };
    return kScripts;
}

bool sameName(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
           });
}

const Script *findScript(std::string_view shortName, char side) {
    for (const Script &s : recordedScripts())
        if (sameName(s.mission, shortName) && std::toupper(static_cast<unsigned char>(side)) == s.side)
            return &s;
    return nullptr;
}

// A clip's number and letter: "K12009A" -> (9, 'A'); the number is the
// last three digits before an optional letter.
std::pair<int, char> clipOrder(const std::string &stem) {
    size_t end = stem.size();
    char letter = 0;
    if (end && std::isalpha(static_cast<unsigned char>(stem[end - 1])))
        letter = char(std::toupper(static_cast<unsigned char>(stem[--end])));
    int n = 0;
    for (size_t i = end >= 3 ? end - 3 : 0; i < end; ++i)
        n = n * 10 + (std::isdigit(static_cast<unsigned char>(stem[i])) ? stem[i] - '0' : 0);
    return {n, letter};
}

// "~HEADING~text" -> "HEADING"; empty if the string isn't a page.
std::string headingOf(const std::string &text) {
    if (text.size() < 2 || text[0] != '~')
        return {};
    const size_t close = text.find('~', 1);
    return close == std::string::npos ? std::string() : text.substr(1, close - 1);
}

// Pages the generic rule shows: headed, with text, not a transmission or
// the pass and fail pages.
bool briefingPage(const std::string &text) {
    std::string heading = headingOf(text);
    if (heading.empty())
        return false;
    for (char &c : heading)
        c = char(std::toupper(static_cast<unsigned char>(c)));
    if (heading.find("TRANSMISSION") != std::string::npos || heading.find("RECEIVING") != std::string::npos ||
        heading.rfind("MISSION PASS", 0) == 0 || heading.rfind("MISSION FAIL", 0) == 0)
        return false;
    const std::string body = text.substr(heading.size() + 2);
    return body.find_first_not_of(" \t\r\n") != std::string::npos;
}

} // namespace

bool clipBefore(const std::string &a, const std::string &b) {
    return clipOrder(a) < clipOrder(b);
}

bool hasRecordedBriefing(std::string_view shortName, char side) {
    return findScript(shortName, side) != nullptr;
}

std::vector<BriefingStep> briefingScript(std::string_view shortName, char side, const StringTable &table,
                                         const std::vector<std::string> &clips) {
    std::vector<BriefingStep> out;
    std::string prefix(shortName);
    if (!prefix.empty() && std::toupper(static_cast<unsigned char>(prefix[0])) == 'J')
        prefix += char(std::toupper(static_cast<unsigned char>(side)));
    for (char &c : prefix)
        c = char(std::toupper(static_cast<unsigned char>(c)));
    if (const Script *s = findScript(shortName, side)) {
        for (const Step &st : s->steps)
            out.push_back(
                {*st.clip ? prefix + st.clip : std::string(), st.page, st.view, st.site.a, st.site.b, st.model});
        return out;
    }
    std::vector<std::string> sorted = clips;
    std::sort(sorted.begin(), sorted.end(), clipBefore);
    std::vector<uint32_t> pages;
    for (const StringTable::String &s : table.strings())
        if (briefingPage(s.text))
            pages.push_back(s.id);
    const size_t n = std::max(pages.size(), sorted.size());
    for (size_t i = 0; i < n; ++i) {
        BriefingStep step;
        step.clip = i < sorted.size() ? sorted[i] : std::string();
        step.page = i < pages.size() ? pages[i] : 0;
        step.view = i >= pages.size() ? V::kEmblem : i == 0 ? V::kGalaxy : i == 1 ? V::kStarSystem : V::kGround;
        out.push_back(std::move(step));
    }
    return out;
}

} // namespace reluva::newworlds
