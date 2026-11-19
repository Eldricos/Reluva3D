#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

"""Script disassembler for Star Trek: DS9 Harbinger (CC.ALL scenes, CCB.ALL dialogues).

    python3 harbinger_script.py SSE_DIR OUT_DIR

SSE_DIR is the folder the extractor unpacked SSE.ALL into (CC.ALL, CCB.ALL, CCB.LST,
CCQ.ALL, CCF.ALL ...). Writes one listing per scene and per dialogue, plus summary.txt.

Format, as read by DEEP.EXE (Load_Directive_List at 0x1DD32, Load_Dialogues 0x1CC1E):

  directive list:  u16 count, then count directives
  directive:       u8 command (0x32..0xC8), then ARGC[command] argument slots
  argument slot:   u8 type, then 0, 1, 2 or 4 bytes depending on the type:
                     00            unused slot, no data
                     01, 04        1 byte
                     02            u16; a VARIABLE number for commands that read values
                                   (IF*, SUB, MULTIPLY, ...), via a per-scene map of 300
                                   slots into the global variable array
                     40            u16 constant
                     08, 10, 18    two bytes, lo then hi (for 08 the hi byte is a choice)
                     20            two u16; only the first is kept

  CC.ALL           31 directive lists back to back, one per scene. The first command of
                   each is SCENE n (0x68, a no-op at run time); n indexes the scene table.
  CCB.ALL/.LST     72 dialogue records (offsets in CCB.LST), one per dialogue name:
                     list A, list B,
                     u8 n, tree of NPC lines     (n non-empty groups)
                     u8 n, tree of player choices
                   tree group: u8 k, then k items of
                     u16 line, u16 choice (FFFF for NPC lines), directive list,
                     u8 m, then m sub-items: u16, u16, directive list
                   Empty groups (k = 0) do not count toward n.

Argument notation in the listings:
  @n  type 02 (variable n, or a plain number for commands that don't read variables)
  #n  type 40 constant      bN  type 01 byte      cN  type 04 byte
  x/y/z lo.hi  types 08/10/18     dN  type 20      tXX:N  any other type
Command names come from DEEP.EXE's own strings; a trailing ? marks a name inferred
from behaviour, and op_XX means not yet identified.
"""

import collections
import re
import struct
import sys
from pathlib import Path

# Maximum argument slots per command: DEEP.EXE data at 0x55140, indexed by command byte.
ARGC = {0x32: 1, 0x33: 1, 0x34: 2, 0x35: 2, 0x36: 1, 0x37: 1, 0x38: 1, 0x39: 0, 0x3A: 6, 0x3B: 1,
        0x3C: 2, 0x3D: 1, 0x3E: 1, 0x3F: 1, 0x40: 1, 0x41: 3, 0x42: 3, 0x43: 3, 0x44: 3, 0x45: 3,
        0x46: 4, 0x47: 1, 0x48: 3, 0x49: 3, 0x4A: 1, 0x4B: 1, 0x4C: 3, 0x4D: 3, 0x4E: 2, 0x4F: 1,
        0x50: 2, 0x51: 3, 0x64: 5, 0x65: 2, 0x66: 3, 0x67: 2, 0x68: 1, 0x69: 2, 0x6A: 4, 0x6B: 1,
        0x6C: 1, 0x6D: 8, 0x6E: 8, 0x6F: 0, 0x70: 2, 0x71: 3, 0x72: 7, 0x73: 3, 0x74: 2, 0x75: 2,
        0x76: 1, 0x77: 3, 0x78: 8, 0x79: 0, 0x7A: 2, 0x7B: 2, 0x7C: 3, 0x7D: 2, 0x7E: 2, 0x7F: 2,
        0x80: 2, 0x81: 2, 0x82: 1, 0x83: 1, 0x84: 3, 0x85: 8, 0x86: 8, 0x87: 4, 0x88: 4, 0x89: 2,
        0x8A: 3, 0x8B: 2, 0x8C: 1, 0x8D: 1, 0x8E: 1, 0x8F: 2, 0x90: 0, 0x91: 2, 0x92: 3, 0x93: 3,
        0x94: 3, 0x95: 2, 0x96: 3, 0x97: 3, 0x98: 3, 0x99: 3, 0x9A: 5, 0x9B: 1, 0x9C: 8, 0x9D: 3,
        0x9E: 3, 0x9F: 3, 0xA0: 3, 0xA1: 3, 0xA2: 3, 0xA3: 2, 0xA4: 2, 0xA5: 1, 0xA6: 1, 0xA7: 0,
        0xA8: 0, 0xA9: 1, 0xAA: 1, 0xC8: 1}

NAMES = {
    # dialogue (directive-list interpreter, jump table 0x149B0)
    0x32: "GOTO", 0x33: "REPLY", 0x34: "HOOK", 0x35: "THOOK", 0x36: "ON", 0x37: "OFF",
    0x38: "SET", 0x39: "EXIT", 0x3A: "TALKHEAD", 0x3B: "RUDE", 0x3C: "DONE", 0x3D: "ADDLINE", 0x3E: "MOOD",
    0x3F: "UNSPEAK", 0x40: "BACK", 0x41: "EMPTY", 0x42: "IFON", 0x43: "IFOFF", 0x44: "IFSET",
    0x45: "DONEX", 0x46: "EMPTYX", 0x47: "PROTAGONIST", 0x48: "IFREPLY", 0x49: "IFGOTO",
    0x4A: "TOFF", 0x4B: "TON", 0x4C: "IFTOFF", 0x4D: "IFTON", 0x4E: "COUNTCHOICES",
    0x4F: "SPEAK", 0x50: "ANIMPHONEME", 0x51: "TALKER",
    0x7C: "SAY", 0x92: "SAY", 0x93: "SAY", 0x94: "SAY",
    # scene interpreter (jump table 0x11000), most also valid in dialogues
    0x64: "ANIM", 0x65: "SOUND", 0x66: "NOP", 0x67: "LET", 0x68: "SCENE", 0x69: "BACKGRND",
    0x6A: "TIMER", 0x6B: "TIMERON", 0x6C: "TIMEROFF", 0x6D: "HOTON", 0x6E: "HOTOFF",
    0x6F: "RETURN", 0x70: "CONVERSE", 0x71: "IFE", 0x72: "HOTSPOT", 0x73: "NEWSCENE",
    0x74: "ADD", 0x75: "SUB", 0x76: "JUMP", 0x77: "IFNE", 0x78: "SWITCH", 0x79: "DEATH",
    0x7A: "ANIMDEF", 0x7B: "ACTIONSEQ", 0x7D: "RANDOM", 0x7E: "TIMERSET", 0x7F: "TIMERGET",
    0x80: "TIMERLABEL", 0x81: "TIMERLABEL2?", 0x82: "TIMERRELOAD?", 0x83: "RETURNVAL",
    0x84: "ANIMPOS", 0x85: "ANIMSEQ", 0x86: "ANIMRATE", 0x87: "ANIMSPD", 0x88: "ANIMDEST",
    0x89: "ANIMARRV", 0x8A: "ANIMFRAM", 0x8B: "ANIMEND", 0x8C: "ANIMSUSPEND", 0x8D: "CDCHANGE",
    0x8E: "ANIMUNSUS", 0x8F: "HOTTYPE", 0x90: "ANIMSOFF", 0x91: "ANIMSETFRAME", 0x95: "ANIMPRIO",
    0x96: "IFGT", 0x97: "IFGE", 0x98: "IFLT", 0x99: "IFLE", 0x9A: "SETRECT", 0x9B: "GOSUB",
    0x9C: "ANIMDUMP", 0x9D: "IFESUBR", 0x9E: "IFNESUBR", 0x9F: "IFGTSUBR", 0xA0: "IFGESUBR",
    0xA1: "IFLTSUBR", 0xA2: "IFLESUBR", 0xA3: "MULTIPLY", 0xA4: "DIVIDE", 0xA5: "FADEOUT",
    0xA6: "FADEIN", 0xA7: "MAPON", 0xA8: "MAPOFF", 0xA9: "ANIMCOMPOSIT", 0xAA: "ANIMPRISTINE",
    0xC8: "LABEL",
}

# What the less obvious commands do, from their code in DEEP.EXE.
NOTES = {
    0x39: "marks the dialogue finished",
    0x3A: "sets up a talking-head animation from the table at 0x58600",
    0x3D: "appends a value to a list at 0x71AF8 (purpose unknown)",
    0x41: "sets a per-line byte in the dialogue record (+0x41 or +0x73 by argument type)",
    0x45: "attaches a linked node to a choice group (submenu?)",
    0x4E: "var[arg2] = number of enabled, unused choices in group arg1",
    0x66: "does nothing in the scene interpreter; unused in the data",
    0x6A: "TIMER id, ticks, label, repeat: count = reload = ticks, active",
    0x6F: "pops the GOSUB stack, or ends the script if it is empty",
    0x73: "makes another scene current (switches the variable map)",
    0x76: "jumps to a LABEL",
    0x78: "SWITCH var, label0..label6: jumps to label[var]",
    0x79: "sets the dead flag, waits for one-shot sounds, opens the game menu (0x12DC4)",
    0x7B: "runs an action sequence (Smacker + .mus); result into a variable",
    0x7D: "RANDOM var, n: var = rand() % n (C library LCG)",
    0x83: "like RETURN, but evaluates its argument as a result",
    0x8D: "asks for CD n (0/1) if another disc is in the drive (0x12C93)",
    0x9A: "SETRECT n, x, y, w, h into the 300-entry table at 0x6F3D0",
    0x9B: "pushes a return address, then jumps (Subroutine nesting too deep)",
    0xA5: "fades the palette to black in n steps of 50 ms (0x12CCF)",
    0xA6: "fades the palette in from black in n steps of 50 ms (0x12D04)",
    0xA7: "enables the station map screen (maps/*.dat)",
    0xC8: "label definition, resolved when the script is loaded",
}

SCENES = ["BRIDGE", "BRIDGE2", "CHPPUZ", "COLPUZ", "COMM", "CONTROL", "CRASH", "DEFIANT",
          "DISPLAY", "HABCOR1", "HABCOR2", "HABITAT", "HABRM1", "HABRM2", "HOLO", "HOLOSUIT",
          "LCOR", "LREACTOR", "MAZEA1", "MAZEA2", "MAZEA3", "MAZEB1", "MAZEC1", "MEMORY",
          "OPENING", "POWER", "PROMBAR", "RCOR", "RREACTOR", "TUBPUZ", "TURBO"]
DIALOGUES = ("COM010 COM020 COM02X COM02Y COM030 COM050 COM051 COM060 COM070 COM080 COM090 "
             "DAX000 DAX010 DAX011 DAX020 DAX021 DAX030 DAX05X DAX060 DAX070 HAVIK010 HAVIK020 "
             "JAN010 JAN020 JAN030 KARLOGS KARPUZ01 KEEL010 KIRA010 KIRA050 KIRA060 KIRA070 "
             "ODO010 ODO020 ODO030 ODO050 ODO051 ODO052 ODO060 ODO080 QUARK010 QUARK011 "
             "QUARK020 QUARK060 QUARK070 QUARK080 QUARK090 QUARKX1 RASMS010 RASMS020 SCYTH020 "
             "SET010 SIS010 SIS020 SIS030 SIS070 SYR010 VOINVIS1 VOINVIS2 VOINVIS3 VOKMSG01 "
             "VOODO010 VOODO020 WEY010 XMIDARC5 XMIDARC9 XODO010 XOQ010 XQD010 XSDK030 YAR010 "
             "YAR020").split()

SIZE = {0x01: 1, 0x04: 1, 0x02: 2, 0x08: 2, 0x10: 2, 0x18: 2, 0x40: 2, 0x20: 4}


class ScriptError(Exception):
    pass


def fmt_arg(t, raw):
    v = int.from_bytes(raw, "little")
    if t == 0x02:
        return "@%d" % v
    if t == 0x40:
        return "#%d" % v
    if t == 0x01:
        return "b%d" % v
    if t == 0x04:
        return "c%d" % v
    if t in (0x08, 0x10, 0x18):
        return "%s%d.%d" % ({0x08: "x", 0x10: "y", 0x18: "z"}[t], raw[0], raw[1])
    if t == 0x20:
        return "d%d" % (v & 0xFFFF)
    return "t%02X:%d" % (t, v)


def read_list(d, p):
    """Returns (end offset, [(offset, command, [(slot, type, raw bytes)])])."""
    n = struct.unpack_from("<H", d, p)[0]
    p += 2
    out = []
    for _ in range(n):
        at, op = p, d[p]
        p += 1
        if op not in ARGC:
            raise ScriptError("illegal command %02X at offset %X" % (op, at))
        args = []
        for slot in range(ARGC[op]):
            t = d[p]
            size = SIZE.get(t, 0)
            if size:
                args.append((slot, t, d[p + 1:p + 1 + size]))
            p += 1 + size
        out.append((at, op, args))
    return p, out


def read_tree(d, p, n):
    groups, done = [], 0
    while done < n:
        k = d[p]
        p += 1
        items = []
        for _ in range(k):
            line, choice = struct.unpack_from("<HH", d, p)
            p, lst = read_list(d, p + 4)
            m = d[p]
            p += 1
            subs = []
            for _ in range(m):
                a, b = struct.unpack_from("<HH", d, p)
                p, sl = read_list(d, p + 4)
                subs.append((a, b, sl))
            items.append((line, choice, lst, subs))
        groups.append(items)
        if k:
            done += 1
    return p, groups


def read_dialogue(d, p):
    p, a = read_list(d, p)
    p, b = read_list(d, p)
    p, npc = read_tree(d, p + 1, d[p])
    p, choices = read_tree(d, p + 1, d[p])
    return p, a, b, npc, choices


def dialogue_lists(dlg):
    _, a, b, npc, choices = dlg
    lists = [a, b]
    for tree in (npc, choices):
        for group in tree:
            for _, _, lst, subs in group:
                lists.append(lst)
                lists.extend(sl for _, _, sl in subs)
    return lists


# --------------------------------------------------------------------------
# Symbols: per-scene name lists (CA..CV) and the linker cross-reference (CCS)
#   name list entry:  name NUL, source file NUL, flag byte
#   CCS entry:        symbol NUL, command byte, argument slot
# A type-02 argument is an index into the list that its (command, slot) uses,
# a label number (labels are numbered in definition order), or a keyword.
# --------------------------------------------------------------------------

LIST_KINDS = ("CA", "CB", "CS", "CD", "CT", "CH", "CV")
KIND_TITLE = {"CA": "animations", "CB": "backgrounds", "CS": "sounds", "CD": "dialogues",
              "CT": "timers", "CH": "hotspots", "CV": "variables"}


def find(dirpath, name):
    for p in Path(dirpath).iterdir():
        if p.name.upper() == name:
            return p
    return None


def load_archive(src, name):
    """Entries of NAME.ALL as split by NAME.LST, or None."""
    all_, lst = find(src, name + ".ALL"), find(src, name + ".LST")
    if not (all_ and lst):
        return None
    d, raw = all_.read_bytes(), lst.read_bytes()
    offs = list(struct.unpack("<%dI" % (len(raw) // 4), raw))
    if offs[-1] != len(d):
        offs.append(len(d))
    return [d[a:b] for a, b in zip(offs, offs[1:])]


def read_names(d):
    out, p = [], 0
    while p < len(d):
        e = d.index(b"\0", p)
        name = d[p:e].decode("latin-1")
        e2 = d.index(b"\0", e + 1)
        out.append((name, d[e + 1:e2].decode("latin-1")))
        p = e2 + 2
    return out


def read_xref(d):
    out, p = [], 0
    while p < len(d):
        e = d.index(b"\0", p)
        out.append((d[p:e].decode("latin-1"), d[e + 1], d[e + 2]))
        p = e + 3
    return out


def type02_uses(lists):
    for lst in lists:
        for _, op, args in lst:
            for slot, t, raw in args:
                if t == 0x02:
                    yield op, slot, int.from_bytes(raw, "little")


class SceneSymbols:
    def __init__(self, lists, xref):
        self.lists = lists                                   # kind -> [(name, source)]
        self.xref = xref
        self.lower = {k: {n.lower() for n, _ in v} for k, v in lists.items()}
        sources = collections.Counter(src.upper() for v in lists.values() for _, src in v
                                      if src.upper().endswith(".CCG"))
        self.name = sources.most_common(1)[0][0][:-4] if sources else None
        self.labels, self.keywords = {}, {}

    def kinds_of(self, sym):
        return [k for k in LIST_KINDS if sym.lower() in self.lower[k]]

    def learn(self, script_lists, kind_map, label_slots):
        uses = list(type02_uses(script_lists))
        label_vals = list(dict.fromkeys(v for op, slot, v in uses if op == 0xC8))
        label_names = [s for s, op, slot in self.xref if op == 0xC8]
        self.labels = dict(zip(label_vals, label_names))
        by_slot = collections.defaultdict(list)
        for op, slot, v in uses:
            if (op, slot) not in kind_map and (op, slot) not in label_slots and op != 0xC8:
                by_slot[(op, slot)].append(v)
        for key, vals in by_slot.items():
            syms = list(dict.fromkeys(s for s, op, slot in self.xref if (op, slot) == key))
            vals = list(dict.fromkeys(vals))
            if len(syms) == len(vals):
                self.keywords[key] = dict(zip(vals, syms))

    def resolve(self, op, slot, value, kind_map, label_slots):
        kind = kind_map.get((op, slot))
        if kind and value < len(self.lists[kind]):
            name = self.lists[kind][value][0]
            return "$" + name if kind == "CV" else name
        if (op, slot) in label_slots or op == 0xC8:
            if value in self.labels:
                return ">" + self.labels[value]
        if (op, slot) in FIXED_KEYWORDS:
            kw = FIXED_KEYWORDS[(op, slot)].get(value)
            return kw if kw else None
        kw = self.keywords.get((op, slot), {}).get(value)
        return kw.lower() if kw else None


# Keyword values fixed by DEEP.EXE's tables (0x550D6 animation keywords, 0x5512E
# cursors, 0x10010 timer kinds). These win over names learned from the
# cross-references, which pair values and names wrongly in some scenes.
ANIM_KEYWORDS = dict(enumerate("loop cycle forward backward start stop nonstop toend move nomove "
                               "animon animoff allframe oneframe".split()))
CURSORS = dict(enumerate("normal forward back left right turn180 bearleft bearrite up down talk door "
                         "zoomin touch gunsight off loading".split()))
FIXED_KEYWORDS = {
    (0x65, 1): {0: "ambient", 1: "asneeded"},
    (0x69, 1): {0: "palette"},
    (0x6A, 3): {0: "scene", 1: "global", 2: "confused", 3: "lost"},
    (0x7A, 1): {0: "onthefly", 1: "flyclear", 2: "clear"},
    (0x64, 1): ANIM_KEYWORDS,
    (0x72, 6): CURSORS,
    (0x8F, 1): CURSORS,
}
FIXED_KEYWORDS.update({(0x85, slot): ANIM_KEYWORDS for slot in range(1, 8)})
FIXED_KEYWORDS.update({(0x86, slot): ANIM_KEYWORDS for slot in range(2, 8)})


# Slots the cross-reference vote gets wrong, because labels and dialogues are usually
# named after the hotspot or timer they belong to. Checked against the command code.
FORCED_KINDS = {(0x70, 0): "CD", (0x70, 1): "CH", (0x3A, 0): "CD", (0x7F, 0): "CT"}


def learn_slot_kinds(scenes):
    """Which name list each (command, slot) indexes, and which slots hold labels."""
    votes, label_votes = collections.defaultdict(collections.Counter), collections.defaultdict(collections.Counter)
    for sym in scenes:
        label_names = {s.lower() for s, op, slot in sym.xref if op == 0xC8}
        for s, op, slot in sym.xref:
            kinds = sym.kinds_of(s)
            votes[(op, slot)][kinds[0] if len(kinds) == 1 else "?"] += 1
            label_votes[(op, slot)][s.lower() in label_names] += 1
    kind_map, label_slots = {}, set()
    for key, c in votes.items():
        best, n = max(((k, n) for k, n in c.items() if k != "?"), key=lambda kv: kv[1], default=(None, 0))
        if best and n >= 0.5 * sum(c.values()):
            kind_map[key] = best
    # Labels are often named after the hotspot or timer they handle, so a slot whose
    # symbols are (nearly) all label names is a label slot even if the names match a list.
    for key, c in label_votes.items():
        if c[True] >= 0.9 * sum(c.values()):
            label_slots.add(key)
            kind_map.pop(key, None)
    for key, kind in FORCED_KINDS.items():
        kind_map[key] = kind
        label_slots.discard(key)
    return kind_map, label_slots


def resource_paths(kind, name):
    n = name.upper()
    if kind == "CB" and len(n) >= 2:
        return "ART\\%s\\%s\\%s.CMP" % (n[0], n[1], n)
    if kind == "CA":
        return "ANIMS\\%s\\%s.RLE" % (n[:4], n)
    if kind == "CS":
        return "SOUND\\%s.RAW" % n
    return ""


# --------------------------------------------------------------------------
# Output
# --------------------------------------------------------------------------

def dialogue_blocks(ccq, ccf):
    """Split the global CCQ/CCF line list into per-dialogue blocks, in CCB order."""
    if not (ccq and ccf):
        return None
    texts = ccq.read_bytes().split(b"\x00")
    clips = [c.strip().decode("latin-1") for c in ccf.read_bytes().split(b"\n") if c.strip()]
    lines = [(c, t.decode("latin-1")) for c, t in zip(clips, texts)]
    blocks, cur = [], []
    for c, t in lines:
        if cur and (c[:3].lower() != cur[0][0][:3].lower() or c.lower() == cur[0][0].lower()):
            blocks.append(cur)
            cur = []
        cur.append((c, t))
    if cur:
        blocks.append(cur)
    return blocks


def line_note(block, n):
    if block is None or n >= len(block):
        return ""
    clip, text = block[n]
    text = re.sub(r"\{[^}]*\}", "", text).strip()
    return "  ; %s: %s" % (clip, (text[:90] + "...") if len(text) > 90 else text)


class Writer:
    def __init__(self, sym=None, kind_map=None, label_slots=None):
        self.sym, self.kind_map, self.label_slots = sym, kind_map or {}, label_slots or set()
        self.resolved = self.total = 0

    def arg(self, op, slot, t, raw):
        if t == 0x02 and self.sym:
            self.total += 1
            name = self.sym.resolve(op, slot, int.from_bytes(raw, "little"), self.kind_map, self.label_slots)
            if name:
                self.resolved += 1
                return name
        return fmt_arg(t, raw)

    def write(self, out, lst, indent):
        for at, op, args in lst:
            if op == 0xC8 and self.sym:
                label = self.arg(op, 0, args[0][1], args[0][2]) if args else "?"
                out.append("%s%06X  %s:" % (indent, at, label.lstrip(">")))
                continue
            text = ", ".join(self.arg(op, slot, t, raw) for slot, t, raw in args)
            out.append("%s%06X  %-13s %s" % (indent, at, NAMES.get(op, "op_%02X" % op), text))


def scene_header(sym):
    out = []
    for kind in LIST_KINDS:
        items = sym.lists.get(kind, [])
        if not items:
            continue
        out.append(";   %s (%s):" % (KIND_TITLE[kind], kind))
        for i, (name, src) in enumerate(items):
            path = resource_paths(kind, name)
            out.append(";     %3d %-14s %-26s %s" % (i, name, path, "" if src.upper().endswith(".CCG") else "from " + src))
    return out


TYPE_TAG = {0x02: "sym", 0x40: "const", 0x01: "byte", 0x04: "byte4", 0x08: "pair08",
            0x10: "pair10", 0x18: "pair18", 0x20: "dword"}


def argument_reference(scripts, dialogues, symbols, kind_map, label_slots):
    types = collections.defaultdict(collections.Counter)
    examples = collections.defaultdict(collections.Counter)
    owners = [(k, [lst]) for k, lst in scripts.items()]
    for k, dlg in dialogues.items():
        owners.append((None, dialogue_lists(dlg)))
    for k, lists in owners:
        sym = symbols.get(k) if k is not None else None
        for lst in lists:
            for _, op, args in lst:
                for slot, t, raw in args:
                    types[(op, slot)][TYPE_TAG.get(t, "t%02X" % t)] += 1
                    if t == 0x40:
                        examples[(op, slot)]["#%d" % int.from_bytes(raw, "little")] += 1
                    elif t == 0x02 and sym and (op, slot) not in kind_map and (op, slot) not in label_slots:
                        name = sym.resolve(op, slot, int.from_bytes(raw, "little"), kind_map, label_slots)
                        if name:
                            examples[(op, slot)][name] += 1
    out, last = [], None
    for (op, slot) in sorted(types):
        name = NAMES.get(op, "op_%02X" % op)
        what = KIND_TITLE.get(kind_map.get((op, slot)), "label" if (op, slot) in label_slots else "")
        ex = ", ".join(e for e, _ in examples[(op, slot)].most_common(8))
        t = "/".join("%s" % k for k, _ in types[(op, slot)].most_common())
        out.append("  %-13s %d  %-18s %-11s %s" % (name if name != last else "", slot, t, what, ex))
        last = name
    return out


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    (out / "scenes").mkdir(parents=True, exist_ok=True)
    (out / "dialogues").mkdir(parents=True, exist_ok=True)
    summary, usage = [], collections.Counter()

    # Scenes: CC.ALL entries via CC.LST when present (entry number = scene slot).
    cc_entries = load_archive(src, "CC")
    if cc_entries is None and find(src, "CC.ALL"):
        d, p, cc_entries = find(src, "CC.ALL").read_bytes(), 0, []
        while p < len(d):
            q, _ = read_list(d, p)
            cc_entries.append(d[p:q])
            p = q
    cc_entries = cc_entries or []
    scripts = {}
    for k, d in enumerate(cc_entries):
        if d:
            end, lst = read_list(d, 0)
            scripts[k] = lst

    # Symbol lists, one entry per scene slot.
    name_files = {kind: load_archive(src, kind) for kind in LIST_KINDS}
    xrefs = load_archive(src, "CCS")
    symbols = {}
    if xrefs and all(name_files.values()):
        for k in scripts:
            lists = {kind: read_names(name_files[kind][k]) if k < len(name_files[kind]) else []
                     for kind in LIST_KINDS}
            symbols[k] = SceneSymbols(lists, read_xref(xrefs[k]) if k < len(xrefs) else [])
    kind_map, label_slots = learn_slot_kinds(symbols.values()) if symbols else ({}, set())

    # Dialogues, so their scripts can be resolved with the lists of a scene that uses them.
    ccb = load_archive(src, "CCB")
    dialogues = {}
    for k, d in enumerate(ccb or []):
        try:
            dialogues[k] = read_dialogue(d, 0)
        except (ScriptError, IndexError, struct.error) as e:
            summary.append("dialogue %d FAILED: %s" % (k, e))
    dlg_scene = {}
    for k, sym in symbols.items():
        for name, _ in sym.lists["CD"]:
            dlg_scene.setdefault(name.lower(), k)
    for k, sym in symbols.items():
        extra = [l for dk, dlg in dialogues.items()
                 if dlg_scene.get(DIALOGUES[dk].lower() if dk < len(DIALOGUES) else "") == k
                 for l in dialogue_lists(dlg)]
        sym.learn(list(scripts[k].__iter__()) and [scripts[k]] + extra, kind_map, label_slots)

    summary.append("CC: %d scene slots, %d scripts" % (len(cc_entries), len(scripts)))
    resolved = total = 0
    for k, lst in sorted(scripts.items()):
        sym = symbols.get(k)
        name = (sym.name if sym and sym.name else "SCENE%02d" % k)
        w = Writer(sym, kind_map, label_slots)
        sid = lst[0][2][0][2] if lst and lst[0][1] == 0x68 and lst[0][2] else None
        body = ["; CC.ALL entry %d: %s (SCENE number %s), %d directives"
                % (k, name, int.from_bytes(sid, "little") if sid else "?", len(lst))]
        if sym:
            body += [";", "; resources (index, name, file):"] + scene_header(sym)
        body.append("")
        w.write(body, lst, "")
        usage.update(op for _, op, _ in lst)
        fname = "scene_%02d_%s.txt" % (k, name)
        (out / "scenes" / fname).write_text("\n".join(body) + "\n", encoding="utf-8")
        resolved, total = resolved + w.resolved, total + w.total
        summary.append("  slot %2d  %-9s %5d directives, %d/%d symbol arguments named  -> scenes/%s"
                       % (k, name, len(lst), w.resolved, w.total, fname))

    blocks = dialogue_blocks(find(src, "CCQ.ALL"), find(src, "CCF.ALL"))
    if blocks is not None and len(blocks) != len(dialogues):
        summary.append("note: %d line blocks in CCQ/CCF for %d dialogues; line text left out"
                       % (len(blocks), len(dialogues)))
        blocks = None
    summary.append("\nCCB: %d dialogues" % len(dialogues))
    for k, dlg in sorted(dialogues.items()):
        name = DIALOGUES[k] if k < len(DIALOGUES) else "DLG%02d" % k
        scene_k = dlg_scene.get(name.lower())
        w = Writer(symbols.get(scene_k), kind_map, label_slots)
        block = blocks[k] if blocks else None
        _, a, b, npc, choices = dlg
        body = ["; CCB.ALL dialogue %d (%s), symbols from scene %s" % (
            k, name, symbols[scene_k].name if scene_k in symbols else "(none found)"), ""]
        body.append("list A:")
        w.write(body, a, "    ")
        body.append("list B:")
        w.write(body, b, "    ")
        for title, tree in (("NPC lines", npc), ("player choices", choices)):
            body.append("\n%s:" % title)
            for gi, group in enumerate(tree):
                body.append("  group %d%s" % (gi, " (empty)" if not group else ""))
                for line, choice, lst, subs in group:
                    tag = "line %d" % line + ("" if choice == 0xFFFF else ", choice %d" % choice)
                    body.append("    [%s]%s" % (tag, line_note(block, line)))
                    w.write(body, lst, "        ")
                    for sa, sb, sl in subs:
                        body.append("      sub [%d, %d]%s" % (sa, sb, line_note(block, sa)))
                        w.write(body, sl, "          ")
        for lst in dialogue_lists(dlg):
            usage.update(op for _, op, _ in lst)
        (out / "dialogues" / ("%02d_%s.txt" % (k, name))).write_text("\n".join(body) + "\n", encoding="utf-8")
        resolved, total = resolved + w.resolved, total + w.total
        summary.append("  %2d %-9s %d/%d symbol arguments named" % (k, name, w.resolved, w.total))

    summary.append("\nsymbol arguments named: %d of %d (%.1f%%)" % (resolved, total, 100.0 * resolved / max(total, 1)))
    summary.append("\nwhat each type-02 argument refers to (learned from the CCS cross-references):")
    for (op, slot), kind in sorted(kind_map.items()):
        summary.append("  %02X %-13s slot %d -> %s" % (op, NAMES.get(op, "op_%02X" % op), slot, KIND_TITLE[kind]))
    for op, slot in sorted(label_slots):
        summary.append("  %02X %-13s slot %d -> label" % (op, NAMES.get(op, "op_%02X" % op), slot))
    summary.append("\nargument reference (per command and slot: types seen, what type-02 values name, examples):")
    for line in argument_reference(scripts, dialogues, symbols, kind_map, label_slots):
        summary.append(line)
    summary.append("\ncommand usage:")
    for op, n in sorted(usage.items()):
        summary.append("  %02X %-13s %6d  %s" % (op, NAMES.get(op, "op_%02X" % op), n, NOTES.get(op, "")))
    (out / "summary.txt").write_text("\n".join(summary) + "\n", encoding="utf-8")
    print("\n".join(summary[:36]))
    print("... full summary in", out / "summary.txt")


if __name__ == "__main__":
    main()
