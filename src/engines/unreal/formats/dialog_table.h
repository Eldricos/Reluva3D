// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

// The Fallen's dialogue tables: one tab-separated text file per mission
// (int/M01_Dialogue.txt...), each line a row: who says it, to whom, in
// which missions, of which type, its text, what follows it, what it
// needs and what it sets off. Their voices are sounds of the same name
// in the matching .uax. See docs/formats/unreal/dialogue.md.

#include "common/types.h"

#include <filesystem>
#include <string>
#include <vector>

namespace reluva::unreal {

/** One row of a dialogue table: a line someone says. */
struct DialogLine {
    std::string table;   // its file's name, less the extension ("M01_Dialogue"), and its sound package's
    std::string record;  // its sound's name: the speaker's code and a number ("BFC002")
    std::string mission; // "K01" (Kira's mission 1), "M01", "G04", "GEN", "KOP" (Kira in Ops)...
    std::string type;    // "HAL" (hailing someone), "CIN" (a cut scene), "AMB" (ambient)...
    // speakers' codes: "KRA", "SKO", "BFC"...
    std::string speaker;
    std::string listener;
    std::string lineId; // "000", "010"...
    std::string text;
    std::string direction;
    float pause = 0; // seconds after it
    // the keys of the line that follows, and of the one to go back to (may hold '?')
    std::string next;
    std::string back;
    std::string tag;
    std::string event;
    std::string condition;
    std::string camera;
    std::string speakerAnim;
    std::string listenerAnim;
    std::string speakerFace;
    std::string listenerFace;
    int row = 0; // its row among its file's rows that aren't blank, the header too (from 0)
    // its LINE and CONDITION as the file has them (trailing spaces kept)
    std::string rawText;
    std::string rawCondition;

    /** How other lines name it: mission, type, speaker and line ID ("K01AMBBFC000"). */
    std::string key() const { return mission + type + speaker + lineId; }
};

/**
 * Every line of every *_Dialogue.txt in `folder`, in file then row order;
 * fails if there's none.
 */
Error readDialogTables(const std::filesystem::path &folder, std::vector<DialogLine> &out);
/** One table's rows (`table` its name), added to `out`. */
void parseDialogTable(const std::string &text, const std::string &table, std::vector<DialogLine> &out);

/**
 * Whether a key with '?' standing for any one character matches another
 * of the same length, ignoring case.
 */
bool dialogKeyMatches(const std::string &pattern, const std::string &key);

} // namespace reluva::unreal
