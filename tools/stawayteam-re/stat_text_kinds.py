#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors
"""Check Reluva's rule for objects in text files against the game's classes.

Star Trek.exe reads a text field by its class's property (0x40db31): a
property holding an object reads the lines after it as that object. Reluva
takes a field for an object when a `{` line follows it. This reads every
text object under a folder of the game's extracted files and compares the
two by the property table Reluva is built with (formats/class_table.cpp):

    python3 stat_text_kinds.py <extracted files> \\
        --table ../../src/engines/stawayteam/formats/class_table.cpp

It prints how many fields agree, and each that doesn't. A field with an
empty value has no object either way. Keys a class doesn't list are counted
apart: those classes read their text themselves (`CSortList2D`'s `Level
Part`, `CLayerSaveData`, `CPlasmaTileMap`).
"""

import argparse
import collections
import os
import re

HOLDS_OBJECTS = {'Object', 'Objects', 'Inline'}


def read_table(path):
    """The properties, and the classes, from class_table.cpp."""
    text = open(path, encoding='utf-8').read()
    properties = [(m.group(1).split('/')[-1], m.group(2), m.group(3)) for m in re.finditer(
        r'\{"([^"]*)", Saved::k(\w+), Saved::k(\w+), (?:true|false), -?\d+, \d+\}', text)]
    classes = {}
    order = []
    for m in re.finditer(r'\{"(\w+)", [\d.e+]+f, (-?\d+), OwnData::k\w+, (\d+), (\d+), \d+\}', text):
        order.append((m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4))))
    for name, parent, first, count in order:
        classes[name] = (parent, first, count)
    return properties, order, classes


def properties_of(name, properties, order, classes):
    """A class's properties by key, its own before its parents'."""
    out = {}
    index = order.index(next(c for c in order if c[0] == name)) if name in classes else -1
    while index >= 0:
        _, parent, first, count = order[index]
        for key, saved, element in properties[first:first + count]:
            out.setdefault(key, (saved, element))
        index = parent
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('folder')
    parser.add_argument('--table', required=True)
    args = parser.parse_args()
    properties, order, classes = read_table(args.table)
    cache = {}
    counts = collections.Counter()
    differ = []

    def props(name):
        if name not in cache:
            cache[name] = properties_of(name, properties, order, classes)
        return cache[name]

    def block(lines, pos, cls, array_of, where):
        known = props(cls) if cls in classes else None
        while pos < len(lines):
            line = lines[pos].lstrip('\t \r\n')
            pos += 1
            if line.startswith('}'):
                return pos
            if '=' not in line:
                continue
            key, value = line.split('=', 1)
            braced = pos < len(lines) and lines[pos].lstrip('\t \r\n') == '{'
            if not braced and not value.strip():
                counts['empty'] += 1
                continue
            if braced:
                pos += 1
            kind = None
            if cls == 'Array':
                if key not in ('Item Count', 'Array Count') and array_of:
                    kind = (array_of[1], 'Unknown')
            elif known is not None and key in known:
                kind = known[key]
            elif known is not None:
                counts['not a property'] += 1
            else:
                counts['no class'] += 1
            if kind and kind[0] != 'Unknown':
                wants = kind[0] in HOLDS_OBJECTS or kind[0] == 'Array'
                if wants == braced:
                    counts['agree'] += 1
                else:
                    differ.append((where, cls, key, kind[0], braced))
            if braced:
                inner = kind if kind and kind[0] == 'Array' else None
                pos = block(lines, pos, value, inner, where)
        return pos

    for top, _, names in os.walk(args.folder):
        for name in names:
            path = os.path.join(top, name)
            data = open(path, 'rb').read()
            if b'\x00' in data[:200]:
                continue
            lines = data.decode('latin-1').replace('\r', '').split('\n')
            first = next((i for i, line in enumerate(lines) if line.strip()), None)
            if first is None or first + 1 >= len(lines) or lines[first + 1].strip() != '{':
                continue
            counts['files'] += 1
            block(lines, first + 2, lines[first].strip(), None, path)
    for what, n in sorted(counts.items()):
        print(f'{what}: {n}')
    print(f'differ: {len(differ)}')
    for d in differ:
        print(*d, sep='\t')


if __name__ == '__main__':
    main()
