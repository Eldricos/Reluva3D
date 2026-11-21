// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// lexicon.dic in stbof.res (docs/engines/botf/formats/lexicon.md): the words and
// phrases the interface shows (option values, race and star names, labels),
// by number. Which number is which comes from the screens; see lexicon.md.

namespace reluva::botf {

/** lexicon.dic's entries, by number. */
class Lexicon {
public:
    /** Reads lexicon.dic, replacing the entries; fails if the table or an entry is past the end. */
    Error load(const std::vector<uint8_t> &data);
    /** How many entries there are. */
    size_t size() const { return _entries.size(); }
    /** The entry as UTF-8, or empty past the end. */
    const std::string &operator[](size_t i) const;

private:
    std::vector<std::string> _entries;
};

/**
 * Entries the screens use, numbered as in the English lexicon.dic. Found by
 * the modding community by changing an entry and seeing which screen
 * changed (Ultimate Editor, ue/edit/res/stbof/files/dic/idx/LexDataIdx.java).
 */
namespace lex {
constexpr int kAdvanced = 4;
constexpr int kAutomatic = 52;
constexpr int kBeginning = 88;
constexpr int kDeveloped = 234;
constexpr int kDomination = 248;
constexpr int kEarly = 252;
constexpr int kEasy = 256;
constexpr int kElliptical = 264;
constexpr int kExpanded = 312;
constexpr int kFew = 358;
constexpr int kHard = 433;
constexpr int kImpossible = 482;
constexpr int kIrregular = 555;
constexpr int kLarge = 596;
constexpr int kManual = 621;
constexpr int kMany = 622;
constexpr int kMedium = 627;
constexpr int kNone = 872;
constexpr int kNormal = 873;
constexpr int kOff = 880;
constexpr int kOn = 882;
constexpr int kRing = 1056;
constexpr int kSimple = 1150;
constexpr int kSmall = 1158;
constexpr int kSome = 1161;
constexpr int kSpiral = 1172;
constexpr int kTeamPlay = 1253;
constexpr int kVendetta = 1407;
// The options screen's words, found by their text in the English
// lexicon.dic; which text box shows each is inferred
// (docs/engines/botf/systems/screens.md, Options).
constexpr int kMusic = 797;
constexpr int kNo = 828;
constexpr int kSoundEffects = 1113; // "SFX"
constexpr int kStrategicTimer = 1182;
constexpr int kTacticalTimer = 1247;
constexpr int kVideo = 1411;
constexpr int kVoice = 1413;
constexpr int kVolumeControl = 1416;
constexpr int kYes = 1453;
// A new save's description: "Ferengi 0 Medium Ring Galaxy". Found by their
// text; where two entries have it, which one the game uses isn't known.
constexpr int kCardassians = 125;
constexpr int kFederation = 320;
constexpr int kFerengi = 324;
constexpr int kGalaxy = 406;
constexpr int kKlingons = 563;
constexpr int kRomulans = 1061;
// The galaxy screen's words, found by their text.
constexpr int kAvoid = 652;
constexpr int kCreditsPerTurn = 194;
constexpr int kDilithium = 239;
constexpr int kEngage = 658;
constexpr int kEvent = 297;
constexpr int kEvents = 310;
constexpr int kNoEvents = 833;
constexpr int kTotalCredits = 1361;
constexpr int kTurn = 1370;
// The selected sector's strip under the map; a stellar object's description
// is the entry after its name's.
constexpr int kBlackHole = 94;
// The empires' adjectives, for a sector one claims: "Romulan Sector d1".
constexpr int kCardassian = 122;
constexpr int kFederationAdjective = 319;
constexpr int kFerengiAdjective = 323;
constexpr int kKlingon = 560;
constexpr int kRomulan = 1058;
constexpr int kContestedSector = 187; // "Contested Sector %s"
constexpr int kScanStrength = 1096;   // "Scan Strength %d"
constexpr int kSector = 1107;         // "Sector %s"
constexpr int kUnclaimedSector = 1382;
constexpr int kUnexploredSector = 1385;
constexpr int kDescription = 228;
constexpr int kGrowthRate = 425;
constexpr int kHumans = 445;
constexpr int kMaxPop = 626;
constexpr int kNeutronStar = 810;
constexpr int kOrbitals = 891;
constexpr int kPopulation = 919;
constexpr int kRadioPulsar = 968;
constexpr int kSystem = 1214;
constexpr int kTerraform = 1256;
constexpr int kUninhabited = 1388;
constexpr int kWormHole = 1447;
constexpr int kXRayPulsar = 1450;
} // namespace lex

} // namespace reluva::botf
