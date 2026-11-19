// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include <vector>

#include "common/stream.h"
#include "common/text.h"
#include "common/types.h"

namespace reluva::stgenerations {

// A compiled location script (GAME.BIN).
//
// Layout, from verid1/GAME.BIN and the AMARGOS1 header:
//
//   char[4]  "SGEN"
//   uint16   22 (version)
//   uint16   2
//   uint32   file size - 12
//   uint32   code size in bytes
//   uint32   size of each of six data sections
//   uint16   code[code size / 2]    the program, see below
//   uint8    sections[6][...]       section 0: (int16, int16) pairs ending
//                                   in -1, passed to the game at load;
//                                   section 1: 12-byte resource records,
//                                   one per name; section 2: sound cues;
//                                   section 3: the sound table (see
//                                   sound.h); section 4: projectiles
//   char[]   resource names, NUL separated (COMBADGE.BMP, KIRKHIT.OVR...)
//
// The program is a list of script objects, each a state machine. From the
// object factory at 0x43519F in sgens.exe:
//
//   program := object* 0x1000
//   object  := type param state+ 0x2000     (param becomes the object id)
//   state   := (0x8000 | id) block+ 0x3000
//   block   := event next-state instruction* 0x0000
//
// When an object receives an event, the block for (event, current state)
// runs; afterwards the object moves to the state set by GOTO_STATE, or
// else to the block's next state (-1 = stay). Event 2 is "enter state" and
// event 3 "leave state". See opcodes.h for the instruction set.
struct ScriptBlock {
    uint16_t event;
    int16_t nextState;
    // Word index of the first instruction in ScriptFile::getCode().
    uint32_t codeStart;
};

struct ScriptState {
    uint16_t id;
    std::vector<ScriptBlock> blocks;
};

struct ScriptObject {
    uint16_t type;
    uint16_t id;
    std::vector<ScriptState> states;
};

class ScriptFile {
public:
    static const int kSectionCount = 6;
    static const uint16_t kEndOfProgram = 0x1000;
    static const uint16_t kEndOfObject = 0x2000;
    static const uint16_t kEndOfState = 0x3000;
    static const uint16_t kStateMarker = 0x8000;

    bool load(SeekableReadStream& stream);

    uint16_t getVersion() const { return _version; }
    const std::vector<uint16_t>& getCode() const { return _code; }
    const std::vector<uint8_t>& getSection(int index) const { return _sections[index]; }
    const std::vector<std::string>& getResourceNames() const { return _resourceNames; }
    const std::vector<ScriptObject>& getObjects() const { return _objects; }

private:
    bool parseProgram();

    uint16_t _version = 0;
    std::vector<uint16_t> _code;
    std::vector<uint8_t> _sections[kSectionCount];
    std::vector<std::string> _resourceNames;
    std::vector<ScriptObject> _objects;
};

} // namespace reluva::stgenerations

