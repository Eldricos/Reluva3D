// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stharbinger/formats/script.h"

#include "common/bytes.h"
#include "common/log.h"
#include "common/text.h"

namespace reluva::stharbinger {

namespace {

const int8_t kArgCounts[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 1,  1,  2,  2,  1,  1,  1,  0,
    6,  1,  2,  1,  1,  1,  1,  3,  3,  3,  3,  3,  4,  1,  3,  3,  1,  1,  3,  3,  2,  1,  2,  3,  -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 5,  2,  3,  2,  1,  2,  4,  1,  1,  8,  8,  0,  2,  3,  7,  3,
    2,  2,  1,  3,  8,  0,  2,  2,  3,  2,  2,  2,  2,  2,  1,  1,  3,  8,  8,  4,  4,  2,  3,  2,  1,  1,  1,  2,  0,
    2,  3,  3,  3,  2,  3,  3,  3,  3,  5,  1,  8,  3,  3,  3,  3,  3,  3,  2,  2,  1,  1,  0,  0,  1,  1,  -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 1,  -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

const char* const kListNames[kListKindCount] = {"CA", "CB", "CS", "CD", "CT", "CH", "CV"};
const char* const kListTitles[kListKindCount] = {"animations", "backgrounds", "sounds",   "dialogues",
                                                 "timers",     "hotspots",    "variables"};

} // namespace

const Argument* Directive::getArg(int slot) const {
    for (const Argument& a : args)
        if (a.slot == slot)
            return &a;
    return nullptr;
}

int getArgCount(uint8_t command) { return kArgCounts[command]; }

int getArgSize(uint8_t type) {
    switch (type) {
    case kArgByte:
    case kArgByte4: return 1;
    case kArgSymbol:
    case kArgPair08:
    case kArgPair10:
    case kArgPair18:
    case kArgConst: return 2;
    case kArgDouble: return 4;
    default: return 0;
    }
}

std::string getCommandName(uint8_t command) {
    switch (command) {
    case 0x32: return "GOTO";
    case 0x33: return "REPLY";
    case 0x34: return "HOOK";
    case 0x35: return "THOOK";
    case 0x36: return "ON";
    case 0x37: return "OFF";
    case 0x38: return "SET";
    case 0x39: return "EXIT";
    case 0x3A: return "TALKHEAD?";
    case 0x3B: return "RUDE";
    case 0x3C: return "DONE";
    case 0x3E: return "MOOD";
    case 0x3F: return "UNSPEAK";
    case 0x40: return "BACK";
    case 0x41: return "LINEATTR?";
    case 0x42: return "IFON";
    case 0x43: return "IFOFF";
    case 0x44: return "IFSET";
    case 0x45: return "SUBMENU?";
    case 0x46: return "SUBMENU4?";
    case 0x47: return "PROTAGONIST?";
    case 0x48: return "IFREPLY";
    case 0x49: return "IFGOTO";
    case 0x4A: return "TOFF";
    case 0x4B: return "TON";
    case 0x4C: return "IFTOFF";
    case 0x4D: return "IFTON";
    case 0x4E: return "COUNTCHOICES";
    case 0x4F: return "SPEAK";
    case 0x50: return "ANIMPHONEME";
    case 0x51: return "TALKER";
    case 0x64: return "ANIM";
    case 0x65: return "SOUND";
    case 0x66: return "NOP";
    case 0x67: return "LET";
    case 0x68: return "SCENE";
    case 0x69: return "BACKGRND";
    case 0x6A: return "TIMER";
    case 0x6B: return "TIMERON";
    case 0x6C: return "TIMEROFF";
    case 0x6D: return "HOTON";
    case 0x6E: return "HOTOFF";
    case 0x6F: return "RETURN";
    case 0x70: return "CONVERSE";
    case 0x71: return "IFE";
    case 0x72: return "HOTSPOT";
    case 0x73: return "NEWSCENE";
    case 0x74: return "ADD";
    case 0x75: return "SUB";
    case 0x76: return "JUMP";
    case 0x77: return "IFNE";
    case 0x78: return "SWITCH";
    case 0x79: return "SOUNDSOFF?";
    case 0x7A: return "ANIMDEF";
    case 0x7B: return "ACTIONSEQ";
    case 0x7C: return "LIPSYNC?";
    case 0x7D: return "RANDOM";
    case 0x7E: return "TIMERSET";
    case 0x7F: return "TIMERGET";
    case 0x80: return "TIMERLABEL";
    case 0x81: return "TIMERLABEL2?";
    case 0x82: return "TIMERRELOAD?";
    case 0x83: return "RETURNVAL";
    case 0x84: return "ANIMPOS";
    case 0x85: return "ANIMSEQ";
    case 0x86: return "ANIMRATE";
    case 0x87: return "ANIMSPD";
    case 0x88: return "ANIMDEST";
    case 0x89: return "ANIMARRV";
    case 0x8A: return "ANIMFRAM";
    case 0x8B: return "ANIMEND";
    case 0x8C: return "ANIMSUSPEND";
    case 0x8D: return "AMBIENT?";
    case 0x8E: return "ANIMUNSUS";
    case 0x8F: return "HOTTYPE";
    case 0x90: return "ANIMSOFF";
    case 0x91: return "ANIMSETFRAME";
    case 0x92: return "LIPSYNC_17?";
    case 0x93: return "LIPSYNC_18?";
    case 0x94: return "LIPSYNC_19?";
    case 0x95: return "ANIMPRIO";
    case 0x96: return "IFGT";
    case 0x97: return "IFGE";
    case 0x98: return "IFLT";
    case 0x99: return "IFLE";
    case 0x9A: return "SETRECT";
    case 0x9B: return "GOSUB";
    case 0x9C: return "ANIMDUMP";
    case 0x9D: return "IFESUBR";
    case 0x9E: return "IFNESUBR";
    case 0x9F: return "IFGTSUBR";
    case 0xA0: return "IFGESUBR";
    case 0xA1: return "IFLTSUBR";
    case 0xA2: return "IFLESUBR";
    case 0xA3: return "MULTIPLY";
    case 0xA4: return "DIVIDE";
    case 0xA5: return "FADEIN?";
    case 0xA6: return "FADEOUT?";
    case 0xA7: return "MAPON";
    case 0xA8: return "MAPOFF";
    case 0xA9: return "ANIMCOMPOSIT";
    case 0xAA: return "ANIMPRISTINE";
    case 0xC8: return "LABEL";
    default: return text::format("op_%02X", command);
    }
}

bool readDirectiveList(const uint8_t* data, uint32_t size, uint32_t& pos, DirectiveList& out, std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error)
            *error = msg;
        return false;
    };
    if (pos + 2 > size)
        return fail("directive list header past the end");
    uint32_t count = readLE16(data + pos);
    uint32_t p = pos + 2;
    out.clear();
    out.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        if (p >= size)
            return fail(text::format("directive %u past the end", i));
        Directive d;
        d.offset = p;
        d.command = data[p++];
        int argc = getArgCount(d.command);
        if (argc < 0)
            return fail(text::format("illegal command %02X at offset %X", d.command, d.offset));
        for (int slot = 0; slot < argc; slot++) {
            if (p >= size)
                return fail(text::format("arguments of %02X at offset %X past the end", d.command, d.offset));
            uint8_t type = data[p++];
            int n = getArgSize(type);
            if (p + n > size)
                return fail(text::format("argument data at offset %X past the end", p));
            if (n) {
                Argument a;
                a.slot = uint8_t(slot);
                a.type = type;
                a.value = n == 1 ? data[p] : readLE16(data + p);
                d.args.push_back(a);
            }
            p += n;
        }
        out.push_back(std::move(d));
    }
    pos = p;
    return true;
}

const char* getCursorName(int cursor) {
    static const char* const kNames[] = {"normal",   "forward",  "back",     "left", "right", "turn180",
                                         "bearleft", "bearrite", "up",       "down", "talk",  "door",
                                         "zoomin",   "touch",    "gunsight", "off",  "shoot"};
    return cursor >= 0 && cursor < int(sizeof(kNames) / sizeof(kNames[0])) ? kNames[cursor] : "?";
}

const char* getListArchiveName(ListKind kind) { return kListNames[kind]; }

const char* getListTitle(ListKind kind) { return kListTitles[kind]; }

namespace {

// Reads a NUL-terminated string at `p`, moving `p` past the NUL.
bool readString(const uint8_t* data, uint32_t size, uint32_t& p, std::string& out) {
    uint32_t start = p;
    while (p < size && data[p])
        p++;
    if (p >= size)
        return false;
    out.assign(reinterpret_cast<const char*>(data + start), p - start);
    p++;
    return true;
}

} // namespace

std::vector<NameEntry> readNameList(const uint8_t* data, uint32_t size) {
    std::vector<NameEntry> out;
    uint32_t p = 0;
    while (p < size) {
        NameEntry e;
        if (!readString(data, size, p, e.name) || !readString(data, size, p, e.source))
            break;
        e.flag = p < size ? data[p] : 0;
        p++;
        out.push_back(std::move(e));
    }
    return out;
}

std::vector<XrefEntry> readXrefList(const uint8_t* data, uint32_t size) {
    std::vector<XrefEntry> out;
    uint32_t p = 0;
    while (p < size) {
        XrefEntry e;
        if (!readString(data, size, p, e.symbol) || p + 2 > size)
            break;
        e.command = data[p];
        e.slot = data[p + 1];
        p += 2;
        out.push_back(std::move(e));
    }
    return out;
}

std::string getResourcePath(ListKind kind, const std::string& name) {
    std::string n = text::toUpper(name);
    switch (kind) {
    case kListBackgrounds:
        if (n.size() < 2)
            return std::string();
        return "ART/" + n.substr(0, 1) + "/" + n.substr(1, 1) + "/" + n + ".CMP";
    case kListAnims: return "ANIMS/" + n.substr(0, 4) + "/" + n + ".RLE";
    case kListSounds: return "SOUND/" + n + ".RAW";
    default: return std::string();
    }
}

} // namespace reluva::stharbinger
