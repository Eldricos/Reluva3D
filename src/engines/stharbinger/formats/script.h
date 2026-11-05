// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/types.h"

#include <string>
#include <vector>

namespace reluva::stharbinger {

// Argument slot types (the byte before each argument's data).
enum ArgType {
    kArgNone = 0x00,   // unused slot, no data
    kArgByte = 0x01,   // 1 byte
    kArgSymbol = 0x02, // u16: a name-list index, label number, keyword or variable
    kArgByte4 = 0x04,  // 1 byte
    kArgPair08 = 0x08, // two bytes, lo then hi
    kArgPair10 = 0x10,
    kArgPair18 = 0x18,
    kArgDouble = 0x20, // two u16; only the first is used
    kArgConst = 0x40   // u16 constant
};

struct Argument {
    uint8_t slot = 0;
    uint8_t type = kArgNone;
    uint32_t value = 0; // little-endian value of the data (first u16 for type 20)
};

struct Directive {
    uint32_t offset = 0; // in the directive list's data
    uint8_t command = 0;
    std::vector<Argument> args; // only the slots that hold data

    const Argument* getArg(int slot) const;
};

typedef std::vector<Directive> DirectiveList;

// Scene commands, after DEEP.EXE's own names (a ? in docs/SCRIPT_REFERENCE.md
// marks names inferred from behaviour).
enum Command {
    kCmdAnim = 0x64,
    kCmdSound = 0x65,
    kCmdNop = 0x66,
    kCmdLet = 0x67,
    kCmdScene = 0x68,
    kCmdBackground = 0x69,
    kCmdTimer = 0x6A,
    kCmdTimerOn = 0x6B,
    kCmdTimerOff = 0x6C,
    kCmdHotOn = 0x6D,
    kCmdHotOff = 0x6E,
    kCmdReturn = 0x6F,
    kCmdConverse = 0x70,
    kCmdIfE = 0x71,
    kCmdHotspot = 0x72,
    kCmdNewScene = 0x73,
    kCmdAdd = 0x74,
    kCmdSub = 0x75,
    kCmdJump = 0x76,
    kCmdIfNE = 0x77,
    kCmdSwitch = 0x78,
    kCmdDeath = 0x79, // listed as SOUNDSOFF? in older listings
    kCmdAnimDef = 0x7A,
    kCmdActionSeq = 0x7B,
    kCmdRandom = 0x7D,
    kCmdTimerSet = 0x7E,
    kCmdTimerGet = 0x7F,
    kCmdTimerLabel = 0x80,
    kCmdTimerLabel2 = 0x81,
    kCmdTimerReload = 0x82,
    kCmdReturnVal = 0x83,
    kCmdAnimPos = 0x84,
    kCmdAnimSeq = 0x85,
    kCmdAnimRate = 0x86,
    kCmdAnimSpd = 0x87,
    kCmdAnimDest = 0x88,
    kCmdAnimArrv = 0x89,
    kCmdAnimFram = 0x8A,
    kCmdAnimEnd = 0x8B,
    kCmdAnimSuspend = 0x8C,
    kCmdRequireDisc = 0x8D, // listed as AMBIENT?
    kCmdAnimUnsus = 0x8E,
    kCmdHotType = 0x8F,
    kCmdAnimsOff = 0x90,
    kCmdAnimSetFrame = 0x91,
    kCmdAnimPrio = 0x95,
    kCmdIfGT = 0x96,
    kCmdIfGE = 0x97,
    kCmdIfLT = 0x98,
    kCmdIfLE = 0x99,
    kCmdSetRect = 0x9A,
    kCmdGosub = 0x9B,
    kCmdAnimDump = 0x9C,
    kCmdIfESubr = 0x9D,
    kCmdIfNESubr = 0x9E,
    kCmdIfGTSubr = 0x9F,
    kCmdIfGESubr = 0xA0,
    kCmdIfLTSubr = 0xA1,
    kCmdIfLESubr = 0xA2,
    kCmdMultiply = 0xA3,
    kCmdDivide = 0xA4,
    kCmdFadeOut = 0xA5, // to black (listed as FADEIN?)
    kCmdFadeIn = 0xA6,  // from black (listed as FADEOUT?)
    kCmdMapOn = 0xA7,
    kCmdMapOff = 0xA8,
    kCmdAnimComposit = 0xA9,
    kCmdAnimPristine = 0xAA,
    kCmdLabel = 0xC8
};

// Keywords of ANIM and ANIMSEQ (DEEP.EXE table at 0x550D6).
enum AnimKeyword {
    kAnimLoop = 0,
    kAnimCycle = 1,
    kAnimForward = 2,
    kAnimBackward = 3,
    kAnimStart = 4,
    kAnimStop = 5,
    kAnimNonStop = 6,
    kAnimToEnd = 7,
    kAnimMove = 8,
    kAnimNoMove = 9,
    kAnimOn = 10,
    kAnimOff = 11,
    kAnimAllFrame = 12,
    kAnimOneFrame = 13
};

// ANIMDEF modes.
enum AnimMode { kAnimModeOnTheFly = 0, kAnimModeFlyClear = 1, kAnimModeClear = 2 };

// Hotspot cursors (HOTSPOT slot 6, HOTTYPE slot 1).
enum Cursor {
    kCursorNormal = 0,
    kCursorForward = 1,
    kCursorBack = 2,
    kCursorLeft = 3,
    kCursorRight = 4,
    kCursorTurn180 = 5,
    kCursorBearLeft = 6,
    kCursorBearRight = 7,
    kCursorUp = 8,
    kCursorDown = 9,
    kCursorTalk = 10,
    kCursorDoor = 11,
    kCursorZoomIn = 12,
    kCursorTouch = 13,
    kCursorGunsight = 14,
    kCursorOff = 15,
    kCursorShoot = 16
};

const char* getCursorName(int cursor);

// Number of argument slots of a command, or -1 if the byte is not a command
// (DEEP.EXE table at 0x55140).
int getArgCount(uint8_t command);
// Number of data bytes after a slot type byte.
int getArgSize(uint8_t type);
// The command's name, or "op_XX".
std::string getCommandName(uint8_t command);

// Parses a directive list (u16 count, then the directives) starting at `pos`.
// On success `pos` is just past the list. On failure `error` says why.
bool readDirectiveList(const uint8_t* data, uint32_t size, uint32_t& pos, DirectiveList& out,
                       std::string* error = nullptr);

// Name-list kinds, in the order of the archives CA ... CV.
enum ListKind {
    kListAnims,       // CA
    kListBackgrounds, // CB
    kListSounds,      // CS
    kListDialogues,   // CD
    kListTimers,      // CT
    kListHotspots,    // CH
    kListVariables,   // CV
    kListKindCount
};

const char* getListArchiveName(ListKind kind); // "CA" ...
const char* getListTitle(ListKind kind);       // "animations" ...

struct NameEntry {
    std::string name;
    std::string source; // the script file that defined it, e.g. BRIDGE.CCG
    uint8_t flag = 0;
};

// Name list entries: name NUL, source NUL, flag byte.
std::vector<NameEntry> readNameList(const uint8_t* data, uint32_t size);

struct XrefEntry {
    std::string symbol;
    uint8_t command = 0;
    uint8_t slot = 0;
};

// CCS entries: symbol NUL, command byte, slot byte.
std::vector<XrefEntry> readXrefList(const uint8_t* data, uint32_t size);

// The file a resource is stored in, relative to the DS9 directory:
// ART/X/Y/NAME.CMP, ANIMS/XXXX/NAME.RLE or SOUND/NAME.RAW.
std::string getResourcePath(ListKind kind, const std::string& name);

} // namespace reluva::stharbinger
