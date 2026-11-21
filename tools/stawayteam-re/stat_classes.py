#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors
"""List Star Trek: Away Team's classes and their properties, from Star Trek.exe.

The game saves its objects by their classes' lists of properties, in text
and in binary (docs/engines/stawayteam/research/binary-objects.md). Each
class registers its properties as the program starts; this reads those
registrations out of the program's disassembly and writes a schema: each
class with its version, its parent and its properties in order, each with
the kind of value it saves.

Reads (never writes) Star Trek.exe and a disassembly of it made by GNU
objdump. Only standard-library Python 3.8+ is needed:

    objdump -d -M intel --no-show-raw-insn "Star Trek.exe" > star-trek.asm
    python3 stat_classes.py "Star Trek.exe" star-trek.asm --data DATA.dat -o classes.txt

With --data, the class of each object held in place on its own (an
"inline" property) is taken from the game's text files in DATA.dat, which
name it: the program's registration doesn't.

What's read from the program, and where: see the research note.
"""

import argparse
import re
import struct
import sys

# The functions the registrations call, in the US English retail
# Star Trek.exe (2,437,120 bytes).
NEW = 0x6073a7            # operator new
MAKE_CLASS = 0x40c8a0     # a class's description: (name, version, parent, size)
MAKE_CLASS_ALL = 0x411370 # the same, through a helper: (&global, name, version, parent, size, ...)
MAKE_CLASS_ALL_CALL = 0x411389  # the helper's own call of MAKE_CLASS
ADD_PROPERTY = 0x40ccb0   # appends a property to a class's list
NO_OWN_DATA = 0x401040    # an object's own writer that writes nothing
EMPTY_NAME = 0x651000     # an empty string: properties with no name

# What each kind of property saves, by its binary writer (its vtable's
# +0x34): see the research note for each one's code.
KINDS = {
    0x548280: 'int',      # 4 bytes
    0x51b4b0: 'ref',      # 4 bytes: an object of the same file, by number
    0x4134a0: 'float',    # 4 bytes
    0x548740: 'bool',     # 1 byte
    0x548920: 'flag',     # 1 byte, 0 or 1: one bit of a flags word
    0x4b2cd0: 'point',    # 8 bytes
    0x43b210: 'double',   # 8 bytes
    0x483860: 'colour',   # 3 bytes
    0x548b70: 'choice',   # a string: the chosen item's name
    0x547f50: 'name',     # a string
    0x40d3c0: 'string',   # a string
    0x40c720: 'text',     # a string: the value as text
    0x40ec20: 'ai',       # a string: an AI's class name
    0x40eff0: 'object',   # a class name, then that object
    0x548e80: 'inline',   # an object of a set class, without its name
    0x40e630: 'objects',  # a count, then objects of a set class
    0x40f920: 'array',    # a count, then items of the element property
    0x53df30: 'polygon',  # 12 bytes, then 8 bytes for each point
}
REG = r'(e[a-d]x|e[sd]i|ebp)'


class Program:
    """Star Trek.exe's sections and its disassembly."""

    def __init__(self, exe, asm):
        self.data = open(exe, 'rb').read()
        pe = struct.unpack_from('<I', self.data, 0x3c)[0]
        count = struct.unpack_from('<H', self.data, pe + 6)[0]
        optional = struct.unpack_from('<H', self.data, pe + 20)[0]
        self.sections = []
        for i in range(count):
            o = pe + 24 + optional + 40 * i
            _, va, size, raw = struct.unpack_from('<IIII', self.data, o + 8)
            self.sections.append((0x400000 + va, size, raw))
        line = re.compile(r'^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$')
        self.ins = []
        for text in open(asm):
            m = line.match(text)
            if m:
                self.ins.append((int(m.group(1), 16), m.group(2), m.group(3).strip()))
        self.at = {a: i for i, (a, _, _) in enumerate(self.ins)}

    def read(self, va, n):
        for base, size, raw in self.sections:
            if base <= va < base + size:
                return self.data[raw + va - base: raw + va - base + n]
        return None

    def u32(self, va):
        b = self.read(va, 4)
        return struct.unpack('<I', b)[0] if b and len(b) == 4 else None

    def string(self, va):
        b = self.read(va, 256)
        if b is None:
            return None
        s = b.split(b'\0')[0]
        return s.decode('latin1') if all(32 <= c < 127 for c in s) else None


def num(s):
    return int(s, 16) if re.fullmatch(r'0x[0-9a-f]+', s) else None


def f32(v):
    return struct.unpack('<f', struct.pack('<I', v & 0xffffffff))[0]


def to_f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


class Extractor:
    def __init__(self, prog):
        self.p = prog
        self.ins = prog.ins
        self.classes = {}

    # What a register holds at instruction k: a number, a global's value
    # ('global', address) or a call's result ('call', function).
    def resolve(self, k, reg):
        # ebx, esi, edi and ebp outlive calls (the compiler's convention);
        # eax holds a call's result, ecx and edx are lost
        for j in range(k - 1, max(k - (20000 if reg in ('ebx', 'esi', 'edi', 'ebp') else 60), 0), -1):
            a, mn, op = self.ins[j]
            if mn == 'ret':
                return None
            if mn == 'call':
                if reg == 'eax':
                    return ('call', num(op))
                if reg in ('ecx', 'edx'):
                    return None
                continue
            if mn in ('mov', 'xor') and op.startswith(reg + ','):
                src = op.split(',', 1)[1]
                if mn == 'xor' and src == reg:
                    return 0
                v = num(src)
                if v is not None:
                    return v
                m = re.fullmatch(r'(?:DWORD PTR )?ds:(0x[0-9a-f]+)', src)
                return ('global', int(m.group(1), 16)) if m else None
        return None

    # The pushes for the call at i, first argument first: (instruction, value).
    def pushes(self, i, count):
        out = []
        k = i - 1
        while k >= 0 and len(out) < count and i - k < 50:
            a, mn, op = self.ins[k]
            # a getter's result, pushed a few instructions after it, is an
            # argument; any other call ends them
            if mn == 'call' and not any(self.ins[k + d][1:] == ('push', 'eax') for d in (1, 2, 3)):
                break
            if mn == 'push':
                v = num(op)
                if v is None and re.fullmatch(REG, op):
                    v = self.resolve(k, op)
                out.append((k, v if v is not None else op))
            k -= 1
        return out

    def args(self, i, count):
        return [v for _, v in self.pushes(i, count)]

    # The last vtable a function stores in its object: a constructor's.
    def function_vtable(self, fn):
        i = self.p.at.get(fn)
        if i is None:
            return None
        vt = None
        for j in range(i, min(i + 400, len(self.ins))):
            a, mn, op = self.ins[j]
            if mn == 'ret':
                break
            m = re.fullmatch(r'DWORD PTR \[' + REG + r'\],(0x6[0-9a-f]{5})', op)
            if mn == 'mov' and m:
                vt = int(m.group(2), 16)
        return vt

    # The class global a getter returns; 0 for a getter of no class. Read
    # from its bytes, not the disassembly: objdump's sweep can lose its way
    # after a jump table and miss a function's start.
    def getter_class(self, fn):
        if fn is None:
            return None
        b = self.p.read(fn, 5)
        if not b:
            return None
        if b[:2] == b'\x33\xc0':      # xor eax,eax
            return 0
        if b[0] == 0xa1:               # mov eax,[global]
            return struct.unpack_from('<I', b, 1)[0]
        return None

    def class_ref(self, v):
        if isinstance(v, tuple) and v[0] == 'call':
            return self.getter_class(v[1])
        if isinstance(v, tuple) and v[0] == 'global':
            return v[1]
        return v

    def find_classes(self):
        for i, (a, mn, op) in enumerate(self.ins):
            if mn != 'call':
                continue
            target = num(op)
            if target == MAKE_CLASS and a != MAKE_CLASS_ALL_CALL:
                v = self.args(i, 4)
                if len(v) < 4 or not isinstance(v[0], int) or not self.p.string(v[0]):
                    continue
                g = None
                for j in range(i + 1, i + 8):
                    m = re.fullmatch(r'(?:DWORD PTR )?ds:(0x[0-9a-f]+),' + REG, self.ins[j][2])
                    if self.ins[j][1] == 'mov' and m:
                        g = int(m.group(1), 16)
                        break
                if g:
                    self.add_class(g, self.p.string(v[0]), v[1], v[2], v[3])
            elif target == MAKE_CLASS_ALL:
                v = self.args(i, 9)
                if len(v) >= 5 and isinstance(v[0], int) and isinstance(v[1], int) and self.p.string(v[1]):
                    self.add_class(v[0], self.p.string(v[1]), v[2], v[3], v[4])

    def add_class(self, g, name, version, parent, size):
        if g not in self.classes:
            self.classes[g] = {'name': name, 'version': f32(version) if isinstance(version, int) else None,
                               'parent': self.class_ref(parent), 'props': [],
                               'size': size if isinstance(size, int) else None}

    # A property object built at or before instruction k into `reg`:
    # (constructor, its arguments' pushes, vtable).
    def constructed(self, k, reg):
        j = k - 1
        want = reg
        while j > max(k - 250, 0):
            a, mn, op = self.ins[j]
            if mn == 'xor' and op == want + ',' + want and self.ins[j - 1][1] == 'jmp':
                j -= 1
                continue  # the branch where the allocation failed
            if mn == 'jmp':
                j -= 1
                continue
            if mn == 'call' and want == 'eax' and num(op) != NEW:
                ctor = num(op)
                return ctor, self.pushes(j, 14), self.function_vtable(ctor)
            if mn == 'mov' and op.startswith(want + ','):
                src = op.split(',', 1)[1]
                # the call whose result this is, perhaps with its arguments
                # popped between them
                c = j - 2 if self.ins[j - 1][1] == 'add' and self.ins[j - 1][2].startswith('esp,') else j - 1
                if src == 'eax' and self.ins[c][1] == 'call':
                    call = num(self.ins[c][2])
                    if call != NEW:
                        return call, self.pushes(c, 14), self.function_vtable(call)
                    # built in place: its base constructor, then its own vtable
                    ctor = pushes = vtable = None
                    for m in range(j + 1, k):
                        a2, m2, o2 = self.ins[m]
                        if m2 == 'call' and ctor is None:
                            ctor = num(o2)
                            pushes = self.pushes(m, 14)
                            vtable = self.function_vtable(ctor)
                        mv = re.fullmatch(r'DWORD PTR \[' + want + r'\],(0x6[0-9a-f]{5})', o2)
                        if m2 == 'mov' and mv and ctor:
                            vtable = int(mv.group(1), 16)
                            break
                    return ctor, pushes, vtable
                if re.fullmatch(REG, src):
                    want = src
                    j -= 1
                    continue
                return None
            j -= 1
        return None

    def describe(self, built):
        """A property from what built it: its name, kind and what that needs."""
        if not built:
            return {'name': None, 'kind': None, 'vtable': None, 'ctor': None, 'args': ()}
        ctor, pushes, vtable = built
        values = [v for _, v in pushes or []]
        name = None
        for v in values[:2]:
            if isinstance(v, int) and v == EMPTY_NAME:
                name = ''
                break
            if isinstance(v, int) and self.p.string(v):
                name = self.p.string(v)
                break
        writer = self.p.u32(vtable + 0x34) if vtable else None
        prop = {'name': name, 'kind': KINDS.get(writer, 'unknown:%s' % (hex(writer) if writer else None)),
                'vtable': vtable, 'ctor': ctor, 'args': tuple(map(str, values))}
        if prop['kind'] == 'inline' and len(values) > 2 and isinstance(values[2], int):
            # its class is its owner's to make; its size is given (0x548d50)
            prop['size'] = values[2]
        if prop['kind'] == 'objects':
            for v in values:
                g = self.class_ref(v)
                if isinstance(g, int) and g in self.classes:
                    prop['class'] = g
                    break
        # Arrays: (name, its offsets in threes, element, ?, names), with three
        # offsets' threes for 0x40f420, two for 0x40f300.
        at = {0x40f420: 10, 0x40f300: 7}.get(ctor)
        if prop['kind'] == 'array' and at is not None and len(pushes) > at:
            k, element = pushes[at]
            op = self.ins[k][2]
            prop['element'] = self.describe(self.constructed(k, op)) if re.fullmatch(REG, op) else None
            names = pushes[at + 2][1] if len(pushes) > at + 2 else 0
            prop['named'] = names != 0  # unresolved counts as named, to be looked at
        return prop

    def find_helpers(self):
        """Functions that add the property they're given to one class (its
        global), making the class first if need be: the registration code
        calls them for each property. Their own call of ADD_PROPERTY passes
        their argument, which is no property of its own."""
        self.helpers = {}
        self.helper_calls = set()
        for i, (a, mn, op) in enumerate(self.ins):
            if not (mn == 'call' and num(op) == ADD_PROPERTY):
                continue
            m = re.fullmatch(r'ecx,(?:DWORD PTR )?ds:(0x[0-9a-f]+)', self.ins[i - 2][2]) or \
                re.fullmatch(r'ecx,(?:DWORD PTR )?ds:(0x[0-9a-f]+)', self.ins[i - 1][2])
            push = self.ins[i - 1] if self.ins[i - 1][1] == 'push' else None
            if not m or not push:
                continue
            src = None
            for j in range(i - 2, i - 6, -1):
                if self.ins[j][1] == 'mov' and self.ins[j][2].startswith(push[2] + ','):
                    src = self.ins[j][2].split(',', 1)[1]
                    break
            if src and re.fullmatch(r'DWORD PTR \[esp\+0x[0-9a-f]+\]', src):
                # the function it's in: back to the last ret
                k = i
                while k > 0 and self.ins[k - 1][1] != 'ret':
                    k -= 1
                while self.ins[k][1] == 'nop':
                    k += 1
                self.helpers[self.ins[k][0]] = int(m.group(1), 16)
                self.helper_calls.add(a)

    def find_properties(self):
        self.find_helpers()
        self.unplaced = 0
        for i, (a, mn, op) in enumerate(self.ins):
            if mn == 'call' and num(op) in self.helpers:
                if self.ins[i - 1][1] == 'push':
                    g = self.helpers[num(op)]
                    if g in self.classes:
                        prop = self.describe(self.constructed(i - 1, self.ins[i - 1][2]))
                        prop['at'] = a
                        self.classes[g]['props'].append(prop)
                continue
            if not (mn == 'call' and num(op) == ADD_PROPERTY) or a in self.helper_calls:
                continue
            g = reg = None
            start = i
            if self.ins[i - 1][1:] == ('mov', 'ecx,eax') and self.ins[i - 2][1] == 'call':
                # the class from its getter, the property pushed before it
                g = self.getter_class(num(self.ins[i - 2][2]))
                if self.ins[i - 3][1] == 'push':
                    reg, start = self.ins[i - 3][2], i - 3
            else:
                for j in range(i - 1, i - 4, -1):
                    m = re.fullmatch(r'ecx,(?:DWORD PTR )?ds:(0x[0-9a-f]+)', self.ins[j][2])
                    if self.ins[j][1] == 'mov' and m and g is None:
                        g = int(m.group(1), 16)
                    if self.ins[j][1] == 'push' and reg is None:
                        reg, start = self.ins[j][2], j
            if g not in self.classes or not reg:
                self.unplaced += 1
                continue
            prop = self.describe(self.constructed(start, reg))
            prop['at'] = a
            self.classes[g]['props'].append(prop)
        # The compiler copied some classes' creation, registrations and all,
        # into each place that uses them; only the first to run registers. A
        # copy's properties have the same names, so each name is kept once.
        self.copies = 0
        for c in self.classes.values():
            kept, seen = [], set()
            for p in sorted(c['props'], key=lambda p: p['at']):
                key = p['name'] if p['name'] else (p['kind'], p['vtable'], p['ctor'], p['args'])
                if key in seen:
                    self.copies += 1
                    continue
                seen.add(key)
                kept.append(p)
            c['props'] = kept

    # Each class's object vtable, from its create function, for its own
    # writer (+0x34): kept at the class's +0x30 where it's made, or the
    # helper's seventh argument.
    def find_own_writers(self):
        creates = {}
        for i, (a, mn, op) in enumerate(self.ins):
            if mn == 'mov':
                m = re.fullmatch(r'(?:DWORD PTR )?ds:(0x[0-9a-f]+),' + REG, op)
                if m and int(m.group(1), 16) in self.classes:
                    g = int(m.group(1), 16)
                    for j in range(i - 3, i + 10):
                        m2 = re.fullmatch(r'DWORD PTR \[' + REG + r'\+0x30\],(0x[0-9a-f]+|' + REG + ')', self.ins[j][2])
                        if self.ins[j][1] == 'mov' and m2:
                            v = num(m2.group(2))
                            if v is None:
                                v = self.resolve(j, m2.group(2))
                            if isinstance(v, int):
                                creates.setdefault(g, v)
                            break
            elif mn == 'call' and num(op) == MAKE_CLASS_ALL:
                v = self.args(i, 9)
                if v and len(v) >= 9 and isinstance(v[0], int) and isinstance(v[6], int):
                    creates.setdefault(v[0], v[6])
        for g, fn in creates.items():
            vt = self.object_vtable(fn)
            self.classes[g]['own'] = self.p.u32(vt + 0x34) if vt else None

    def object_vtable(self, fn):
        i = self.p.at.get(fn)
        if i is None:
            return None
        vt = None
        for j in range(i, min(i + 150, len(self.ins))):
            a, mn, op = self.ins[j]
            if mn == 'ret':
                break
            m = re.fullmatch(r'DWORD PTR \[' + REG + r'\],(0x6[0-9a-f]{5})', op)
            if mn == 'mov' and m:
                vt = int(m.group(2), 16)
            if mn in ('call', 'jmp') and num(op) and num(op) not in (0x546000, NEW):
                v2 = self.function_vtable(num(op))
                if v2:
                    vt = v2
        return vt

    def all_props(self, g, seen=()):
        c = self.classes.get(g)
        if not c or g in seen:
            return []
        p = c['parent']
        inherited = self.all_props(p, seen + (g,)) if p in self.classes else []
        return inherited + c['props']

    def version(self, g, seen=()):
        """The version the game works out for a class (0x40cd30): its own,
        47 for each property it adds, and 51 times its parent's."""
        c = self.classes[g]
        # Rounded to a 32-bit float where the game stores it (0x40cd65, 0x40cd75).
        v = to_f32((c['version'] or 0) + 47 * len(c['props']))
        if c['parent'] in self.classes and g not in seen:
            v = to_f32(v + 51 * self.version(c['parent'], seen + (g,)))
        return v


def inline_classes_from_text(ex, data_path):
    """For each inline property, the one class the text files give it, under
    its class or any subclass (stat_objects.inline_classes reads them)."""
    import os
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import stat_objects
    texts = [d.decode('latin1') for n, d in stat_objects.entries(data_path)
             if n.rpartition('.')[2].lower() in ('txt', 'zax', 'mds', 'sty')]
    seen = stat_objects.inline_classes(texts)
    by_name = {c['name']: g for g, c in ex.classes.items()}

    def is_sub(name, ancestor):
        g = by_name.get(name)
        while g in ex.classes:
            if g == ancestor:
                return True
            g = ex.classes[g]['parent']
        return False

    found = 0
    for g, c in ex.classes.items():
        for p in c['props']:
            if p['kind'] != 'inline' or not p['name']:
                continue
            key = p['name'].split('/')[-1]
            named = {x for (owner, k), xs in seen.items() if k == key and is_sub(owner, g)
                     for x in xs if x in by_name}
            if len(named) == 1:
                p['class'] = by_name[named.pop()]
                found += 1
    return found


def kind_text(ex, p):
    text = p['kind'] or 'unknown'
    if p.get('class') is not None:
        text += ' class=' + ex.classes[p['class']]['name']
    if p.get('size') is not None:
        text += ' size=%d' % p['size']
    if p['kind'] == 'array':
        e = p.get('element') or {}
        text += ' element=' + kind_text(ex, e) if e.get('kind') else ' element=unknown'
        if p.get('named'):
            text += ' named'
    return text


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('exe', help='Star Trek.exe')
    ap.add_argument('asm', help="objdump's disassembly of it")
    ap.add_argument('--data', help="DATA.dat, for inline objects' classes from its text files")
    ap.add_argument('-o', '--output', default='-', help='where to write the schema (default: standard output)')
    a = ap.parse_args()
    ex = Extractor(Program(a.exe, a.asm))
    ex.find_classes()
    ex.find_properties()
    ex.find_own_writers()
    from_text = inline_classes_from_text(ex, a.data) if a.data else 0
    out = sys.stdout if a.output == '-' else open(a.output, 'w', newline='\n')
    props = sum(len(c['props']) for c in ex.classes.values())
    out.write('# Star Trek: Away Team: classes and their properties, read from Star Trek.exe by\n'
              '# tools/stawayteam-re/stat_classes.py: %d classes, %d properties (%d copies of\n'
              '# registrations left out, %d not placed). Inherited properties come first and\n'
              '# are not repeated. %d inline objects\' classes are from the text files in\n'
              '# DATA.dat. See docs/engines/stawayteam/research/binary-objects.md.\n'
              % (len(ex.classes), props, ex.copies, ex.unplaced, from_text))
    for g in sorted(ex.classes, key=lambda g: ex.classes[g]['name'].lower()):
        c = ex.classes[g]
        parent = ex.classes[c['parent']]['name'] if c['parent'] in ex.classes else '-'
        own = c.get('own')
        own_text = '-' if own in (None, NO_OWN_DATA) else hex(own)
        out.write('class %s version=%r parent=%s own-data=%s size=%s global=%s\n'
                  % (c['name'], ex.version(g), parent, own_text, c['size'], hex(g)))
        for p in c['props']:
            out.write('  "%s" %s\n' % (p['name'] if p['name'] is not None else '?', kind_text(ex, p)))
    if out is not sys.stdout:
        out.close()
    print('%d classes, %d properties, %d copies left out, %d not placed'
          % (len(ex.classes), props, ex.copies, ex.unplaced), file=sys.stderr)


if __name__ == '__main__':
    main()
