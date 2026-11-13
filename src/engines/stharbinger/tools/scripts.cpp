// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

// reluva-tool stharbinger script: the scene scripts (CC.ALL) and dialogue
// scripts (CCB.ALL) as listings, their arguments named through each scene's
// name lists (CA ... CV) and the linker's cross-references (CCS). A port of
// STDS9H's harbinger_script.py, with the same output, except that:
//   - it takes a game folder (a CD, both CDs side by side, an install, or
//     Reluva's import of them) and reads the archives through the engine's
//     Resources, rather than a folder SSE.ALL was unpacked into; so CC.ALL
//     needs its CC.LST (the Python could also walk a bare CC.ALL);
//   - it reads with the engine's own readers (formats/script.h, dialogue.h),
//     and names the scenes as the engine does (Resources::loadScenes: when
//     two script files name a scene's lists equally often, the first in
//     alphabetical order wins);
//   - a dialogue that can't be read is reported with the engine's message.
//
// Format, as read by DEEP.EXE (Load_Directive_List at 0x1DD32,
// Load_Dialogues 0x1CC1E): docs/engines/stharbinger/systems/script-reference.md.
//
// Argument notation in the listings:
//   @n  type 02 (variable n, or a plain number for commands that don't read
//       variables)      #n  type 40 constant      bN  type 01 byte
//   cN  type 04 byte    x/y/z lo.hi  types 08/10/18      dN  type 20
// Command names come from DEEP.EXE's own strings; a trailing ? marks a name
// inferred from behaviour, and op_XX means not yet identified.

#include "common/text.h"
#include "engines/stharbinger/detection.h"
#include "engines/stharbinger/formats/dialogue.h"
#include "engines/stharbinger/formats/resources.h"
#include "engines/stharbinger/formats/script.h"
#include "devtools/tool.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace reluva;
using namespace reluva::stharbinger;

// ---- Tables (as in harbinger_script.py) --------------------------------------

// The listing's names. They differ from the engine's getCommandName where
// later work named a command (DEATH, CDCHANGE, SAY, the fades the right way
// round, the dialogue commands EMPTY, DONEX, EMPTYX, ADDLINE).
const std::map<int, const char *> kNames = {
    // dialogue (directive-list interpreter, jump table 0x149B0)
    {0x32, "GOTO"},
    {0x33, "REPLY"},
    {0x34, "HOOK"},
    {0x35, "THOOK"},
    {0x36, "ON"},
    {0x37, "OFF"},
    {0x38, "SET"},
    {0x39, "EXIT"},
    {0x3A, "TALKHEAD"},
    {0x3B, "RUDE"},
    {0x3C, "DONE"},
    {0x3D, "ADDLINE"},
    {0x3E, "MOOD"},
    {0x3F, "UNSPEAK"},
    {0x40, "BACK"},
    {0x41, "EMPTY"},
    {0x42, "IFON"},
    {0x43, "IFOFF"},
    {0x44, "IFSET"},
    {0x45, "DONEX"},
    {0x46, "EMPTYX"},
    {0x47, "PROTAGONIST"},
    {0x48, "IFREPLY"},
    {0x49, "IFGOTO"},
    {0x4A, "TOFF"},
    {0x4B, "TON"},
    {0x4C, "IFTOFF"},
    {0x4D, "IFTON"},
    {0x4E, "COUNTCHOICES"},
    {0x4F, "SPEAK"},
    {0x50, "ANIMPHONEME"},
    {0x51, "TALKER"},
    {0x7C, "SAY"},
    {0x92, "SAY"},
    {0x93, "SAY"},
    {0x94, "SAY"},
    // scene interpreter (jump table 0x11000), most also valid in dialogues
    {0x64, "ANIM"},
    {0x65, "SOUND"},
    {0x66, "NOP"},
    {0x67, "LET"},
    {0x68, "SCENE"},
    {0x69, "BACKGRND"},
    {0x6A, "TIMER"},
    {0x6B, "TIMERON"},
    {0x6C, "TIMEROFF"},
    {0x6D, "HOTON"},
    {0x6E, "HOTOFF"},
    {0x6F, "RETURN"},
    {0x70, "CONVERSE"},
    {0x71, "IFE"},
    {0x72, "HOTSPOT"},
    {0x73, "NEWSCENE"},
    {0x74, "ADD"},
    {0x75, "SUB"},
    {0x76, "JUMP"},
    {0x77, "IFNE"},
    {0x78, "SWITCH"},
    {0x79, "DEATH"},
    {0x7A, "ANIMDEF"},
    {0x7B, "ACTIONSEQ"},
    {0x7D, "RANDOM"},
    {0x7E, "TIMERSET"},
    {0x7F, "TIMERGET"},
    {0x80, "TIMERLABEL"},
    {0x81, "TIMERLABEL2?"},
    {0x82, "TIMERRELOAD?"},
    {0x83, "RETURNVAL"},
    {0x84, "ANIMPOS"},
    {0x85, "ANIMSEQ"},
    {0x86, "ANIMRATE"},
    {0x87, "ANIMSPD"},
    {0x88, "ANIMDEST"},
    {0x89, "ANIMARRV"},
    {0x8A, "ANIMFRAM"},
    {0x8B, "ANIMEND"},
    {0x8C, "ANIMSUSPEND"},
    {0x8D, "CDCHANGE"},
    {0x8E, "ANIMUNSUS"},
    {0x8F, "HOTTYPE"},
    {0x90, "ANIMSOFF"},
    {0x91, "ANIMSETFRAME"},
    {0x95, "ANIMPRIO"},
    {0x96, "IFGT"},
    {0x97, "IFGE"},
    {0x98, "IFLT"},
    {0x99, "IFLE"},
    {0x9A, "SETRECT"},
    {0x9B, "GOSUB"},
    {0x9C, "ANIMDUMP"},
    {0x9D, "IFESUBR"},
    {0x9E, "IFNESUBR"},
    {0x9F, "IFGTSUBR"},
    {0xA0, "IFGESUBR"},
    {0xA1, "IFLTSUBR"},
    {0xA2, "IFLESUBR"},
    {0xA3, "MULTIPLY"},
    {0xA4, "DIVIDE"},
    {0xA5, "FADEOUT"},
    {0xA6, "FADEIN"},
    {0xA7, "MAPON"},
    {0xA8, "MAPOFF"},
    {0xA9, "ANIMCOMPOSIT"},
    {0xAA, "ANIMPRISTINE"},
    {0xC8, "LABEL"},
};

// What the less obvious commands do, from their code in DEEP.EXE.
const std::map<int, const char *> kNotes = {
    {0x39, "marks the dialogue finished"},
    {0x3A, "sets up a talking-head animation from the table at 0x58600"},
    {0x3D, "appends a value to a list at 0x71AF8 (purpose unknown)"},
    {0x41, "sets a per-line byte in the dialogue record (+0x41 or +0x73 by argument type)"},
    {0x45, "attaches a linked node to a choice group (submenu?)"},
    {0x4E, "var[arg2] = number of enabled, unused choices in group arg1"},
    {0x66, "does nothing in the scene interpreter; unused in the data"},
    {0x6A, "TIMER id, ticks, label, repeat: count = reload = ticks, active"},
    {0x6F, "pops the GOSUB stack, or ends the script if it is empty"},
    {0x73, "makes another scene current (switches the variable map)"},
    {0x76, "jumps to a LABEL"},
    {0x78, "SWITCH var, label0..label6: jumps to label[var]"},
    {0x79, "sets the dead flag, waits for one-shot sounds, opens the game menu (0x12DC4)"},
    {0x7B, "runs an action sequence (Smacker + .mus); result into a variable"},
    {0x7D, "RANDOM var, n: var = rand() % n (C library LCG)"},
    {0x83, "like RETURN, but evaluates its argument as a result"},
    {0x8D, "asks for CD n (0/1) if another disc is in the drive (0x12C93)"},
    {0x9A, "SETRECT n, x, y, w, h into the 300-entry table at 0x6F3D0"},
    {0x9B, "pushes a return address, then jumps (Subroutine nesting too deep)"},
    {0xA5, "fades the palette to black in n steps of 50 ms (0x12CCF)"},
    {0xA6, "fades the palette in from black in n steps of 50 ms (0x12D04)"},
    {0xA7, "enables the station map screen (maps/*.dat)"},
    {0xC8, "label definition, resolved when the script is loaded"},
};

std::string commandName(int op) {
    const auto it = kNames.find(op);
    return it != kNames.end() ? it->second : text::format("op_%02X", op);
}

const char *const kListKinds[kListKindCount] = {"CA", "CB", "CS", "CD", "CT", "CH", "CV"};
const char *const kKindTitles[kListKindCount] = {"animations", "backgrounds", "sounds",   "dialogues",
                                                 "timers",     "hotspots",    "variables"};

using Key = std::pair<int, int>; // (command, slot)
using Keywords = std::map<uint32_t, std::string>;

// Keyword values fixed by DEEP.EXE's tables (0x550D6 animation keywords,
// 0x5512E cursors, 0x10010 timer kinds). These win over names learned from
// the cross-references, which pair values and names wrongly in some scenes.
Keywords listKeywords(const std::vector<const char *> &names) {
    Keywords out;
    for (size_t i = 0; i < names.size(); i++)
        out[uint32_t(i)] = names[i];
    return out;
}

const std::map<Key, Keywords> &fixedKeywords() {
    static const std::map<Key, Keywords> table = [] {
        const Keywords anim = listKeywords({"loop", "cycle", "forward", "backward", "start", "stop", "nonstop", "toend",
                                            "move", "nomove", "animon", "animoff", "allframe", "oneframe"});
        const Keywords cursors =
            listKeywords({"normal", "forward", "back", "left", "right", "turn180", "bearleft", "bearrite", "up", "down",
                          "talk", "door", "zoomin", "touch", "gunsight", "off", "loading"});
        std::map<Key, Keywords> t = {
            {{0x65, 1}, {{0, "ambient"}, {1, "asneeded"}}},
            {{0x69, 1}, {{0, "palette"}}},
            {{0x6A, 3}, {{0, "scene"}, {1, "global"}, {2, "confused"}, {3, "lost"}}},
            {{0x7A, 1}, {{0, "onthefly"}, {1, "flyclear"}, {2, "clear"}}},
            {{0x64, 1}, anim},
            {{0x72, 6}, cursors},
            {{0x8F, 1}, cursors},
        };
        for (int slot = 1; slot < 8; slot++)
            t[{0x85, slot}] = anim;
        for (int slot = 2; slot < 8; slot++)
            t[{0x86, slot}] = anim;
        return t;
    }();
    return table;
}

// Slots the cross-reference vote gets wrong, because labels and dialogues
// are usually named after the hotspot or timer they belong to. Checked
// against the command code.
const std::map<Key, int> kForcedKinds = {
    {{0x70, 0}, kListDialogues}, {{0x70, 1}, kListHotspots}, {{0x3A, 0}, kListDialogues}, {{0x7F, 0}, kListTimers}};

// A count per key in the order the keys were first seen, as Python's
// Counter keeps them (so ties sort as they do there).
template<class K>
struct Counter {
    std::vector<std::pair<K, int>> items;
    void add(const K &k, int n = 1) {
        for (auto &[key, count] : items)
            if (key == k) {
                count += n;
                return;
            }
        items.push_back({k, n});
    }
    int get(const K &k) const {
        for (const auto &[key, count] : items)
            if (key == k)
                return count;
        return 0;
    }
    int total() const {
        int n = 0;
        for (const auto &item : items)
            n += item.second;
        return n;
    }
    std::vector<std::pair<K, int>> mostCommon() const {
        auto out = items;
        std::stable_sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
        return out;
    }
};

// ---- Text --------------------------------------------------------------------

// The game's text is Latin-1; the listings are UTF-8.
std::string latin1(std::string_view s) {
    std::string out;
    for (unsigned char c : s) {
        if (c < 0x80) {
            out += char(c);
        } else {
            out += char(0xC0 | (c >> 6));
            out += char(0x80 | (c & 0x3F));
        }
    }
    return out;
}

// Case as Python's str.lower() and upper() change it, on Latin-1 text.
std::string lower(std::string s) {
    for (char &ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7))
            ch = char(c + 0x20);
    }
    return s;
}

std::string upper(std::string s) {
    for (char &ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7))
            ch = char(c - 0x20);
    }
    return s;
}

// Python's str % formatting of "%-13s" and the like, with UTF-8 counted as
// one character per Latin-1 byte.
std::string padRight(const std::string &s, size_t width) {
    size_t chars = 0;
    for (unsigned char c : s)
        chars += (c & 0xC0) != 0x80;
    return chars >= width ? s : s + std::string(width - chars, ' ');
}

std::string padLeft(const std::string &s, size_t width) {
    return s.size() >= width ? s : std::string(width - s.size(), ' ') + s;
}

std::string withoutNotes(const std::string &t) {
    // re.sub(r"\{[^}]*\}", "", text)
    std::string out;
    for (size_t i = 0; i < t.size(); i++) {
        if (t[i] == '{') {
            const size_t close = t.find('}', i + 1);
            if (close != std::string::npos) {
                i = close;
                continue;
            }
        }
        out += t[i];
    }
    return out;
}

// ---- Arguments -------------------------------------------------------------

std::string formatArg(const Argument &a) {
    const uint32_t v = a.value;
    switch (a.type) {
    case kArgSymbol:
        return text::format("@%u", v);
    case kArgConst:
        return text::format("#%u", v);
    case kArgByte:
        return text::format("b%u", v);
    case kArgByte4:
        return text::format("c%u", v);
    case kArgPair08:
    case kArgPair10:
    case kArgPair18: {
        const char tag = a.type == kArgPair08 ? 'x' : a.type == kArgPair10 ? 'y' : 'z';
        return text::format("%c%u.%u", tag, v & 0xFF, (v >> 8) & 0xFF);
    }
    case kArgDouble:
        return text::format("d%u", v & 0xFFFF);
    default:
        return text::format("t%02X:%u", a.type, v);
    }
}

const char *typeTag(uint8_t t) {
    switch (t) {
    case kArgSymbol:
        return "sym";
    case kArgConst:
        return "const";
    case kArgByte:
        return "byte";
    case kArgByte4:
        return "byte4";
    case kArgPair08:
        return "pair08";
    case kArgPair10:
        return "pair10";
    case kArgPair18:
        return "pair18";
    case kArgDouble:
        return "dword";
    default:
        return nullptr;
    }
}

struct Use {
    int op, slot;
    uint32_t value;
};

std::vector<Use> symbolUses(const std::vector<const DirectiveList *> &lists) {
    std::vector<Use> out;
    for (const DirectiveList *list : lists)
        for (const Directive &d : *list)
            for (const Argument &a : d.args)
                if (a.type == kArgSymbol)
                    out.push_back({d.command, a.slot, a.value});
    return out;
}

std::vector<const DirectiveList *> dialogueLists(const Dialogue &dlg) {
    std::vector<const DirectiveList *> lists = {&dlg.listA, &dlg.listB};
    for (const auto *tree : {&dlg.npcLines, &dlg.choices})
        for (const DialogueGroup &group : *tree)
            for (const DialogueItem &item : group) {
                lists.push_back(&item.script);
                for (const DialogueItem::SubItem &sub : item.subItems)
                    lists.push_back(&sub.script);
            }
    return lists;
}

// ---- Symbols: per-scene name lists (CA..CV) and the linker cross-reference -
// A type-02 argument is an index into the list that its (command, slot)
// uses, a label number (labels are numbered in definition order), or a
// keyword.

struct SlotKinds {
    std::map<Key, int> kinds; // (command, slot) -> the list it indexes
    std::set<Key> labels;     // the slots that hold labels
    bool isLabel(const Key &k) const { return labels.count(k) != 0; }
    const int *kind(const Key &k) const {
        const auto it = kinds.find(k);
        return it == kinds.end() ? nullptr : &it->second;
    }
};

struct SceneSymbols {
    const Scene *scene = nullptr;
    std::string name; // "" when its lists come from no script file
    std::set<std::string> lowerNames[kListKindCount];
    std::map<uint32_t, std::string> labels;
    std::map<Key, Keywords> keywords;

    std::vector<int> kindsOf(const std::string &sym) const {
        std::vector<int> out;
        for (int k = 0; k < kListKindCount; k++)
            if (lowerNames[k].count(lower(sym)))
                out.push_back(k);
        return out;
    }

    void learn(const std::vector<const DirectiveList *> &scriptLists, const SlotKinds &slots) {
        const std::vector<Use> uses = symbolUses(scriptLists);
        std::vector<uint32_t> labelValues;
        for (const Use &u : uses)
            if (u.op == kCmdLabel && std::find(labelValues.begin(), labelValues.end(), u.value) == labelValues.end())
                labelValues.push_back(u.value);
        std::vector<std::string> labelNames;
        for (const XrefEntry &x : scene->xref)
            if (x.command == kCmdLabel)
                labelNames.push_back(x.symbol);
        labels.clear();
        for (size_t i = 0; i < labelValues.size() && i < labelNames.size(); i++)
            labels[labelValues[i]] = labelNames[i];
        std::vector<std::pair<Key, std::vector<uint32_t>>> bySlot;
        for (const Use &u : uses) {
            const Key key{u.op, u.slot};
            if (slots.kind(key) || slots.isLabel(key) || u.op == kCmdLabel)
                continue;
            auto it = std::find_if(bySlot.begin(), bySlot.end(), [&](const auto &e) { return e.first == key; });
            if (it == bySlot.end()) {
                bySlot.push_back({key, {}});
                it = bySlot.end() - 1;
            }
            if (std::find(it->second.begin(), it->second.end(), u.value) == it->second.end())
                it->second.push_back(u.value);
        }
        for (const auto &[key, values] : bySlot) {
            std::vector<std::string> syms;
            for (const XrefEntry &x : scene->xref)
                if (Key{x.command, x.slot} == key && std::find(syms.begin(), syms.end(), x.symbol) == syms.end())
                    syms.push_back(x.symbol);
            if (syms.size() == values.size()) {
                Keywords &kw = keywords[key];
                for (size_t i = 0; i < syms.size(); i++)
                    kw[values[i]] = syms[i];
            }
        }
    }

    // The name a type-02 value stands for, or "" if none is known.
    std::string resolve(int op, int slot, uint32_t value, const SlotKinds &slots) const {
        const Key key{op, slot};
        // A talker's speaker code (TALKHEAD's slot 3, TALKER's slot 2) is the
        // scene's CCS symbol with that index, as the game reads it (0x58600).
        if (((op == 0x3A && slot == 3) || (op == 0x51 && slot == 2)) && value < scene->xref.size())
            return scene->xref[value].symbol;
        if (const int *kind = slots.kind(key)) {
            const auto &list = scene->lists[*kind];
            if (value < list.size())
                return (*kind == kListVariables ? "$" : "") + list[value].name;
        }
        if (slots.isLabel(key) || op == kCmdLabel) {
            const auto it = labels.find(value);
            if (it != labels.end())
                return ">" + it->second;
        }
        const auto fixed = fixedKeywords().find(key);
        if (fixed != fixedKeywords().end()) {
            const auto it = fixed->second.find(value);
            return it != fixed->second.end() ? it->second : std::string();
        }
        const auto learned = keywords.find(key);
        if (learned != keywords.end()) {
            const auto it = learned->second.find(value);
            if (it != learned->second.end())
                return lower(it->second);
        }
        return std::string();
    }
};

// Which name list each (command, slot) indexes, and which slots hold labels.
SlotKinds learnSlotKinds(const std::vector<SceneSymbols> &scenes) {
    std::vector<std::pair<Key, Counter<int>>> votes; // kind, or -1 for "?"
    std::vector<std::pair<Key, Counter<bool>>> labelVotes;
    auto entry = [](auto &list, const Key &key) -> auto & {
        for (auto &e : list)
            if (e.first == key)
                return e.second;
        list.push_back({key, {}});
        return list.back().second;
    };
    for (const SceneSymbols &sym : scenes) {
        std::set<std::string> labelNames;
        for (const XrefEntry &x : sym.scene->xref)
            if (x.command == kCmdLabel)
                labelNames.insert(lower(x.symbol));
        for (const XrefEntry &x : sym.scene->xref) {
            const Key key{x.command, x.slot};
            const std::vector<int> kinds = sym.kindsOf(x.symbol);
            entry(votes, key).add(kinds.size() == 1 ? kinds[0] : -1);
            entry(labelVotes, key).add(labelNames.count(lower(x.symbol)) != 0);
        }
    }
    SlotKinds out;
    for (const auto &[key, c] : votes) {
        int best = -1;
        int n = 0;
        for (const auto &[kind, count] : c.items)
            if (kind >= 0 && count > n) {
                best = kind;
                n = count;
            }
        if (best >= 0 && n >= 0.5 * c.total())
            out.kinds[key] = best;
    }
    // Labels are often named after the hotspot or timer they handle, so a
    // slot whose symbols are (nearly) all label names is a label slot even
    // if the names match a list.
    for (const auto &[key, c] : labelVotes)
        if (c.get(true) >= 0.9 * c.total()) {
            out.labels.insert(key);
            out.kinds.erase(key);
        }
    for (const auto &[key, kind] : kForcedKinds) {
        out.kinds[key] = kind;
        out.labels.erase(key);
    }
    return out;
}

std::string resourcePath(int kind, const std::string &name) {
    const std::string n = upper(name);
    if (kind == kListBackgrounds && n.size() >= 2)
        return text::format("ART\\%c\\%c\\%s.CMP", n[0], n[1], n.c_str());
    if (kind == kListAnims)
        return "ANIMS\\" + n.substr(0, 4) + "\\" + n + ".RLE";
    if (kind == kListSounds)
        return "SOUND\\" + n + ".RAW";
    return std::string();
}

// ---- Output ----------------------------------------------------------------

using Block = std::vector<std::pair<std::string, std::string>>; // (clip, text), Latin-1

// The archive's entries one after another: the whole .ALL.
std::string wholeArchive(Resources &res, const char *name, bool &found) {
    auto archive = res.getArchive(name);
    found = archive != nullptr;
    std::string out;
    if (archive)
        for (uint32_t i = 0; i < archive->size(); i++) {
            auto e = archive->getEntry(i);
            out.append(reinterpret_cast<const char *>(e->data()), size_t(e->size()));
        }
    return out;
}

std::vector<std::string> splitOn(const std::string &s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t at = s.find(sep, start);
        if (at == std::string::npos) {
            out.push_back(s.substr(start));
            return out;
        }
        out.push_back(s.substr(start, at - start));
        start = at + 1;
    }
}

std::string stripAscii(const std::string &s) {
    const char *ws = " \t\n\r\x0b\x0c";
    const size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos)
        return std::string();
    return s.substr(b, s.find_last_not_of(ws) - b + 1);
}

// The global CCQ/CCF line list split into per-dialogue blocks, in CCB order.
bool dialogueBlocks(Resources &res, std::vector<Block> &blocks) {
    bool haveQ = false;
    bool haveF = false;
    const std::string ccq = wholeArchive(res, "CCQ", haveQ);
    const std::string ccf = wholeArchive(res, "CCF", haveF);
    if (!haveQ || !haveF)
        return false;
    const std::vector<std::string> texts = splitOn(ccq, '\0');
    std::vector<std::string> clips;
    for (const std::string &c : splitOn(ccf, '\n'))
        if (!stripAscii(c).empty())
            clips.push_back(stripAscii(c));
    Block cur;
    for (size_t i = 0; i < clips.size() && i < texts.size(); i++) {
        const std::string &c = clips[i];
        if (!cur.empty() &&
            (lower(c.substr(0, 3)) != lower(cur[0].first.substr(0, 3)) || lower(c) == lower(cur[0].first))) {
            blocks.push_back(cur);
            cur.clear();
        }
        cur.push_back({c, texts[i]});
    }
    if (!cur.empty())
        blocks.push_back(cur);
    return true;
}

std::string lineNote(const Block *block, size_t n) {
    if (!block || n >= block->size())
        return std::string();
    const auto &[clip, raw] = (*block)[n];
    std::string t = withoutNotes(raw);
    // Python's str.strip(), for the ASCII the text holds.
    const char *ws = " \t\n\r\x0b\x0c\x1c\x1d\x1e\x1f\x85\xa0";
    const size_t b = t.find_first_not_of(ws);
    t = b == std::string::npos ? std::string() : t.substr(b, t.find_last_not_of(ws) - b + 1);
    if (t.size() > 90)
        t = t.substr(0, 90) + "...";
    return "  ; " + latin1(clip) + ": " + latin1(t);
}

class Writer {
public:
    Writer(const SceneSymbols *sym, const SlotKinds &slots) : sym_(sym), slots_(slots) {}

    std::string arg(int op, const Argument &a) {
        if (a.type == kArgSymbol && sym_) {
            total++;
            const std::string name = sym_->resolve(op, a.slot, a.value, slots_);
            if (!name.empty()) {
                resolved++;
                return latin1(name);
            }
        }
        return formatArg(a);
    }

    void write(std::vector<std::string> &out, const DirectiveList &list, const std::string &indent) {
        for (const Directive &d : list) {
            if (d.command == kCmdLabel && sym_) {
                std::string label = "?";
                if (!d.args.empty()) {
                    Argument first = d.args[0];
                    first.slot = 0; // read as the label's own slot, whichever holds it
                    label = arg(d.command, first);
                }
                label.erase(0, label.find_first_not_of('>'));
                out.push_back(indent + text::format("%06X  ", d.offset) + label + ":");
                continue;
            }
            std::string args;
            for (const Argument &a : d.args)
                args += (args.empty() ? "" : ", ") + arg(d.command, a);
            out.push_back(indent + text::format("%06X  ", d.offset) + padRight(commandName(d.command), 13) + " " +
                          args);
        }
    }

    int resolved = 0, total = 0;

private:
    const SceneSymbols *sym_;
    const SlotKinds &slots_;
};

std::vector<std::string> sceneHeader(const SceneSymbols &sym) {
    std::vector<std::string> out;
    for (int kind = 0; kind < kListKindCount; kind++) {
        const auto &items = sym.scene->lists[kind];
        if (items.empty())
            continue;
        out.push_back(text::format(";   %s (%s):", kKindTitles[kind], kListKinds[kind]));
        for (size_t i = 0; i < items.size(); i++) {
            const std::string src = upper(items[i].source);
            const bool ccg = src.size() >= 4 && src.compare(src.size() - 4, 4, ".CCG") == 0;
            out.push_back(text::format(";     %3d ", int(i)) + padRight(latin1(items[i].name), 14) + " " +
                          padRight(latin1(resourcePath(kind, items[i].name)), 26) + " " +
                          (ccg ? "" : "from " + latin1(items[i].source)));
        }
    }
    return out;
}

struct Owner {
    const SceneSymbols *sym;
    std::vector<const DirectiveList *> lists;
};

std::vector<std::string> argumentReference(const std::vector<Owner> &owners, const SlotKinds &slots) {
    std::map<Key, Counter<std::string>> types;
    std::map<Key, Counter<std::string>> examples;
    for (const Owner &o : owners)
        for (const DirectiveList *list : o.lists)
            for (const Directive &d : *list)
                for (const Argument &a : d.args) {
                    const Key key{d.command, a.slot};
                    const char *tag = typeTag(a.type);
                    types[key].add(tag ? tag : text::format("t%02X", a.type));
                    if (a.type == kArgConst) {
                        examples[key].add(text::format("#%u", a.value));
                    } else if (a.type == kArgSymbol && o.sym && !slots.kind(key) && !slots.isLabel(key)) {
                        const std::string name = o.sym->resolve(d.command, a.slot, a.value, slots);
                        if (!name.empty())
                            examples[key].add(latin1(name));
                    }
                }
    std::vector<std::string> out;
    std::string last;
    for (const auto &[key, counter] : types) {
        const std::string name = commandName(key.first);
        const int *kind = slots.kind(key);
        const std::string what = kind ? kKindTitles[*kind] : slots.isLabel(key) ? "label" : "";
        std::string ex;
        const auto common = examples[key].mostCommon();
        for (size_t i = 0; i < common.size() && i < 8; i++)
            ex += (i ? ", " : "") + common[i].first;
        std::string t;
        for (const auto &[tag, n] : counter.mostCommon())
            t += (t.empty() ? "" : "/") + tag;
        out.push_back("  " + padRight(name != last ? name : "", 13) + " " + std::to_string(key.second) + "  " +
                      padRight(t, 18) + " " + padRight(what, 11) + " " + ex);
        last = name;
    }
    return out;
}

bool writeLines(const std::filesystem::path &path, const std::vector<std::string> &lines) {
    std::ofstream out(path, std::ios::binary);
    for (const std::string &l : lines)
        out << l << '\n';
    return bool(out);
}

int cmdScript(int argc, char **argv) {
    if (argc != 4) {
        std::fprintf(stderr, "usage: reluva-tool stharbinger script <game folder> <out folder>\n");
        return 2;
    }
    const auto parts = findGameParts(argv[2]);
    if (parts.empty()) {
        std::fprintf(stderr, "%s: no part of Harbinger found (a CD, both CDs or an install)\n", argv[2]);
        return 1;
    }
    Resources res;
    for (const GamePart &p : parts)
        res.addGamePath(p.folder.string());
    const std::filesystem::path out = argv[3];
    std::error_code ec;
    std::filesystem::create_directories(out / "scenes", ec);
    std::filesystem::create_directories(out / "dialogues", ec);
    std::vector<std::string> summary;
    std::map<int, int> usage;

    // Scenes: CC.ALL's entries (entry number = scene slot), named as the
    // engine names them.
    std::vector<Scene> scenes;
    std::string error;
    res.loadScenes(scenes, error);
    auto cc = res.getArchive("CC");
    const uint32_t ccSlots = cc ? cc->size() : 0;

    // Symbols, when every name list and the cross-references are there.
    bool haveSymbols = true;
    for (const char *kind : {"CA", "CB", "CS", "CD", "CT", "CH", "CV", "CCS"}) {
        auto a = res.getArchive(kind);
        haveSymbols = haveSymbols && a && a->size() > 0;
    }
    std::vector<SceneSymbols> symbols;
    std::map<int, size_t> symbolsBySlot;
    if (haveSymbols)
        for (const Scene &s : scenes) {
            SceneSymbols sym;
            sym.scene = &s;
            sym.name = s.name == text::format("SCENE%02u", unsigned(s.slot)) ? std::string() : s.name;
            for (int k = 0; k < kListKindCount; k++)
                for (const NameEntry &n : s.lists[k])
                    sym.lowerNames[k].insert(lower(n.name));
            symbolsBySlot[s.slot] = symbols.size();
            symbols.push_back(std::move(sym));
        }
    const SlotKinds slots = learnSlotKinds(symbols);
    auto symbolsOf = [&](int slot) -> SceneSymbols * {
        const auto it = symbolsBySlot.find(slot);
        return it == symbolsBySlot.end() ? nullptr : &symbols[it->second];
    };

    // Dialogues, so their scripts can be resolved with the lists of a scene
    // that uses them.
    std::map<int, Dialogue> dialogues;
    if (auto ccb = res.getArchive("CCB"))
        for (uint32_t k = 0; k < ccb->size(); k++) {
            auto e = ccb->getEntry(k);
            Dialogue d;
            std::string err;
            if (readDialogue(e->data(), uint32_t(e->size()), d, &err))
                dialogues[int(k)] = std::move(d);
            else
                summary.push_back(text::format("dialogue %u FAILED: %s", k, err.c_str()));
        }
    auto dialogueName = [](int k) {
        return k < kNumDialogues ? std::string(getDialogueName(k)) : text::format("DLG%02d", k);
    };
    std::map<std::string, int> dialogueScene;
    for (const SceneSymbols &sym : symbols)
        for (const NameEntry &n : sym.scene->lists[kListDialogues])
            dialogueScene.emplace(lower(n.name), sym.scene->slot);
    auto sceneOfDialogue = [&](int k) {
        const auto it = dialogueScene.find(lower(k < kNumDialogues ? getDialogueName(k) : ""));
        return it == dialogueScene.end() ? -1 : it->second;
    };
    for (SceneSymbols &sym : symbols) {
        std::vector<const DirectiveList *> lists;
        if (!sym.scene->script.empty()) {
            lists.push_back(&sym.scene->script);
            for (const auto &[k, dlg] : dialogues)
                if (sceneOfDialogue(k) == sym.scene->slot)
                    for (const DirectiveList *l : dialogueLists(dlg))
                        lists.push_back(l);
        }
        sym.learn(lists, slots);
    }

    summary.push_back(text::format("CC: %u scene slots, %u scripts", ccSlots, unsigned(scenes.size())));
    int resolved = 0;
    int total = 0;
    for (const Scene &s : scenes) {
        const SceneSymbols *sym = symbolsOf(s.slot);
        const std::string name = sym && !sym->name.empty() ? sym->name : text::format("SCENE%02d", s.slot);
        Writer w(sym, slots);
        std::vector<std::string> body = {
            text::format("; CC.ALL entry %d: %s (SCENE number %s), %u directives", s.slot, name.c_str(),
                         s.number >= 0 ? std::to_string(s.number).c_str() : "?", unsigned(s.script.size()))};
        if (sym) {
            body.push_back(";");
            body.push_back("; resources (index, name, file):");
            for (const std::string &l : sceneHeader(*sym))
                body.push_back(l);
        }
        body.push_back("");
        w.write(body, s.script, "");
        for (const Directive &d : s.script)
            usage[d.command]++;
        const std::string file = text::format("scene_%02d_%s.txt", s.slot, name.c_str());
        writeLines(out / "scenes" / file, body);
        resolved += w.resolved;
        total += w.total;
        summary.push_back(text::format("  slot %2d  ", s.slot) + padRight(name, 9) +
                          text::format(" %5u directives, %d/%d symbol arguments named  -> scenes/%s",
                                       unsigned(s.script.size()), w.resolved, w.total, file.c_str()));
    }

    std::vector<Block> blocks;
    bool haveBlocks = dialogueBlocks(res, blocks);
    if (haveBlocks && blocks.size() != dialogues.size()) {
        summary.push_back(text::format("note: %u line blocks in CCQ/CCF for %u dialogues; line text left out",
                                       unsigned(blocks.size()), unsigned(dialogues.size())));
        haveBlocks = false;
    }
    summary.push_back(text::format("\nCCB: %u dialogues", unsigned(dialogues.size())));
    for (const auto &[k, dlg] : dialogues) {
        const std::string name = dialogueName(k);
        const int sceneSlot = sceneOfDialogue(k);
        const SceneSymbols *sym = symbolsOf(sceneSlot);
        Writer w(sym, slots);
        const Block *block = haveBlocks && size_t(k) < blocks.size() ? &blocks[size_t(k)] : nullptr;
        std::vector<std::string> body = {
            text::format("; CCB.ALL dialogue %d (%s), symbols from scene %s", k, name.c_str(),
                         sym ? (sym->name.empty() ? "None" : sym->name.c_str()) : "(none found)"),
            ""};
        body.push_back("list A:");
        w.write(body, dlg.listA, "    ");
        body.push_back("list B:");
        w.write(body, dlg.listB, "    ");
        const std::pair<const char *, const std::vector<DialogueGroup> *> trees[] = {{"NPC lines", &dlg.npcLines},
                                                                                     {"player choices", &dlg.choices}};
        for (const auto &[title, tree] : trees) {
            body.push_back(std::string("\n") + title + ":");
            for (size_t gi = 0; gi < tree->size(); gi++) {
                const DialogueGroup &group = (*tree)[gi];
                body.push_back(text::format("  group %u%s", unsigned(gi), group.empty() ? " (empty)" : ""));
                for (const DialogueItem &item : group) {
                    std::string tag = text::format("line %u", item.line);
                    if (item.choice != 0xFFFF)
                        tag += text::format(", choice %u", item.choice);
                    body.push_back("    [" + tag + "]" + lineNote(block, item.line));
                    w.write(body, item.script, "        ");
                    for (const DialogueItem::SubItem &sub : item.subItems) {
                        body.push_back(text::format("      sub [%u, %u]", sub.a, sub.b) + lineNote(block, sub.a));
                        w.write(body, sub.script, "          ");
                    }
                }
            }
        }
        for (const DirectiveList *l : dialogueLists(dlg))
            for (const Directive &d : *l)
                usage[d.command]++;
        writeLines(out / "dialogues" / text::format("%02d_%s.txt", k, name.c_str()), body);
        resolved += w.resolved;
        total += w.total;
        summary.push_back(text::format("  %2d ", k) + padRight(name, 9) +
                          text::format(" %d/%d symbol arguments named", w.resolved, w.total));
    }

    summary.push_back(text::format("\nsymbol arguments named: %d of %d (%.1f%%)", resolved, total,
                                   100.0 * resolved / std::max(total, 1)));
    summary.push_back("\nwhat each type-02 argument refers to (learned from the CCS cross-references):");
    for (const auto &[key, kind] : slots.kinds)
        summary.push_back(text::format("  %02X ", key.first) + padRight(commandName(key.first), 13) +
                          text::format(" slot %d -> %s", key.second, kKindTitles[kind]));
    for (const Key &key : slots.labels)
        summary.push_back(text::format("  %02X ", key.first) + padRight(commandName(key.first), 13) +
                          text::format(" slot %d -> label", key.second));
    summary.push_back("\nargument reference (per command and slot: types seen, what type-02 values name, examples):");
    std::vector<Owner> owners;
    for (const Scene &s : scenes)
        owners.push_back({symbolsOf(s.slot), {&s.script}});
    for (const auto &[k, dlg] : dialogues)
        owners.push_back({nullptr, dialogueLists(dlg)});
    for (const std::string &line : argumentReference(owners, slots))
        summary.push_back(line);
    summary.push_back("\ncommand usage:");
    for (const auto &[op, n] : usage) {
        const auto note = kNotes.find(op);
        summary.push_back(text::format("  %02X ", op) + padRight(commandName(op), 13) + " " +
                          padLeft(std::to_string(n), 6) + "  " + (note != kNotes.end() ? note->second : ""));
    }
    writeLines(out / "summary.txt", summary);
    for (size_t i = 0; i < summary.size() && i < 36; i++)
        std::printf("%s\n", summary[i].c_str());
    std::printf("... full summary in %s\n", (out / "summary.txt").string().c_str());
    return 0;
}

const reluva::tool::Commands commands(
    "stharbinger",
    {
        {"script", "<game folder> <out folder>",
         "list the scene scripts (CC.ALL) and dialogue scripts (CCB.ALL) with their arguments named, a file each, "
         "and summary.txt: what each argument names, and how often each command is used",
         cmdScript},
    });

} // namespace
