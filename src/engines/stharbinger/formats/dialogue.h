// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/stharbinger/formats/script.h"

#include <string>
#include <vector>

namespace reluva::stharbinger {

// Number of dialogues in CCB.ALL, CCQ.ALL, CCF.ALL and CCR.ALL.
const int kNumDialogues = 72;

// The dialogue names in CCB.ALL order (DEEP.EXE table at 0x5DDB0). Scenes name
// their dialogues in their CD lists; the speech of dialogue NAME is in
// DIALOGUE/NAME/NAME.ALL.
const char *getDialogueName(int index);
// Index of a dialogue name (ignoring case), or -1.
int findDialogue(const std::string &name);

// A dialogue script (CCB.ALL entry):
//   directive list A, directive list B,
//   u8 n, tree of NPC lines, u8 n, tree of player choices.
// A tree is groups of items; empty groups do not count toward n.
struct DialogueItem {
    uint16_t line = 0;        // index into the dialogue's lines (CCQ/CCF)
    uint16_t choice = 0xFFFF; // choice number; FFFF for NPC lines
    DirectiveList script;

    struct SubItem {
        uint16_t a = 0;
        uint16_t b = 0;
        DirectiveList script;
    };
    std::vector<SubItem> subItems;
};

typedef std::vector<DialogueItem> DialogueGroup;

struct Dialogue {
    int index = -1;
    std::string name;
    DirectiveList listA;
    DirectiveList listB;
    std::vector<DialogueGroup> npcLines;
    std::vector<DialogueGroup> choices;

    // Per line: text (CCQ, with the menu [label] and {recording notes}) and
    // the voice clip name (CCF).
    std::vector<std::string> text;
    std::vector<std::string> clips;
    std::vector<std::string> menuLabels; // CCR

    // The text of a line without the [label] and {notes}.
    std::string getSpokenText(int line) const;
    // The [label] of a player choice line, or an empty string.
    std::string getMenuLabel(int line) const;
};

// Lip-sync data for one line (PHONEMES/NAME/NAME.ALL entry):
//   u32 frameSamples, then nodes {u8 code (FF ends), u8 duration, word NUL}.
// Each node shows mouth shape `code` for (unit * duration) / 64 ms, or one unit
// when the duration is 0, where unit = frameSamples / 22.05 ms.
struct Phonemes {
    struct Node {
        uint8_t code = 0;
        uint8_t duration = 0;
    };
    uint32_t unitMillis = 0;
    std::vector<Node> nodes;

    uint32_t getNodeMillis(size_t i) const;
};

bool readPhonemes(const uint8_t *data, uint32_t size, Phonemes &out);

// Parses a CCB.ALL entry. On failure `error` says why.
bool readDialogue(const uint8_t *data, uint32_t size, Dialogue &out, std::string *error = nullptr);

// Splits CCQ/CCR entries (NUL-separated) and CCF entries (CRLF-separated).
std::vector<std::string> splitStrings(const uint8_t *data, uint32_t size, char separator);

} // namespace reluva::stharbinger
