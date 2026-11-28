#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors
"""Decode Star Trek: Away Team's objects saved in binary, to check the schema.

The sequences (.seq16), models (.mdl16) and waypoint maps (.way) in DATA.dat
are objects saved by the game's binary writer
(docs/engines/stawayteam/research/binary-objects.md). This reads each of
them with the schema stat_classes.py writes, and the formats of the few
classes that save data of their own, and says whether each comes out to
its file's last byte, with every object's version its class's.

Reads (never writes) DATA.dat. Only standard-library Python 3.8+ is needed:

    python3 stat_objects.py classes.txt DATA.dat [part of a name] [--show]

--show prints the first file's objects, field by field.
"""

import argparse
import collections
import re
import struct
import sys

FORMAT_VERSION = 26.0
# The class at the top of each kind of file. A sequence may be one of its
# subclasses (CFont), told by its version.
TOP = {'mdl16': 'CModel', 'way': 'CWayPointMap', 'seq16': None}


def read_schema(path):
    classes = {}
    by_version = collections.defaultdict(list)
    current = None
    for line in open(path):
        if line.startswith('#') or not line.strip():
            continue
        if line.startswith('class '):
            fields = line.split()
            name = fields[1]
            attrs = dict(f.split('=', 1) for f in fields[2:])
            current = {'name': name, 'version': float(attrs['version']), 'parent': attrs['parent'],
                       'own': attrs['own-data'], 'props': [],
                       'size': int(attrs['size']) if attrs.get('size', 'None') != 'None' else None}
            classes[name] = current
            by_version[current['version']].append(name)
            continue
        m = re.match(r'\s+"(.*)" (.*)$', line)
        if m:
            current['props'].append((m.group(1), parse_kind(m.group(2).split())))
    return classes, by_version


def parse_kind(words):
    kind = {'kind': words[0]}
    rest = words[1:]
    while rest:
        w = rest.pop(0)
        if w.startswith('class='):
            kind['class'] = w[6:]
        elif w.startswith('size='):
            kind['size'] = int(w[5:])
        elif w.startswith('element='):
            kind['element'] = parse_kind([w[8:]] + rest)
            rest = []
        elif w == 'named':
            kind['named'] = True
    return kind


def inline_classes(texts):
    """The classes the game's text objects give their inline objects: for
    each (class, key), the classes seen. An inline property's class isn't in
    the binary file, nor in its registration; the text files name it."""
    seen = collections.defaultdict(set)
    for text in texts:
        lines = [l.strip('\t ') for l in text.split('\r\n')]
        lines = [l for l in lines if l]
        if len(lines) < 2 or lines[1] != '{':
            continue
        stack = [lines[0]]
        pending = None
        for l in lines[2:]:
            if l == '{':
                stack.append(pending or 'Array')
                pending = None
            elif l == '}':
                stack.pop()
            else:
                key, _, value = l.partition('=')
                pending = value
                if stack:
                    seen[(stack[-1], key)].add(value)
    return seen


class Bad(Exception):
    pass


class Reader:
    def __init__(self, data, classes, by_version, inline=None):
        self.inline = inline or {}
        self.d = data
        self.p = 0
        self.classes = classes
        self.by_version = by_version
        self.seen = collections.Counter()
        self.path = []

    def need(self, n):
        if self.p + n > len(self.d):
            raise Bad('runs past the end at %d' % self.p)

    def i32(self):
        self.need(4)
        v = struct.unpack_from('<i', self.d, self.p)[0]
        self.p += 4
        return v

    def f32(self):
        self.need(4)
        v = struct.unpack_from('<f', self.d, self.p)[0]
        self.p += 4
        return v

    def u8(self):
        self.need(1)
        v = self.d[self.p]
        self.p += 1
        return v

    def raw(self, n):
        self.need(n)
        v = self.d[self.p:self.p + n]
        self.p += n
        return v

    def string(self):
        n = self.i32()
        if not 0 <= n < 100000:
            raise Bad('a string of %d bytes at %d' % (n, self.p - 4))
        return self.raw(n).decode('latin1')

    def props(self, name):
        c = self.classes[name]
        inherited = self.props(c['parent']) if c['parent'] != '-' else []
        return inherited + c['props']

    def own_writers(self, name):
        """The own-data writer a class has, its own or its nearest parent's."""
        while name != '-':
            c = self.classes[name]
            if c['own'] != '-':
                return c['own']
            name = c['parent']
        return '-'

    def obj(self, name, out, size=None):
        """An object saved by 0x40dbd0, of class `name`, or of the class its
        version and size (`size`, if given) say."""
        start = self.p
        fmt = self.f32()
        if fmt != FORMAT_VERSION:
            raise Bad('format version %r at %d' % (fmt, start))
        version = self.f32()
        if name is None:
            names = self.by_version.get(version, [])
            if size is not None and len(names) > 1:
                names = [n for n in names if self.classes[n]['size'] == size]
            if len(names) != 1:
                raise Bad('version %r at %d: %s' % (version, start + 4, names or 'no class'))
            name = names[0]
        if version != self.classes[name]['version']:
            raise Bad('%s at %d: version %r, the class\'s %r' % (name, start, version, self.classes[name]['version']))
        self.seen[name] += 1
        fields = []
        for pname, kind in self.props(name):
            self.path.append('%s.%s' % (name, pname))
            if kind['kind'] == 'inline':
                kind = dict(kind, owner=name, key=pname.split('/')[-1])
            fields.append((pname, self.value(kind)))
            self.path.pop()
        own = self.own_data(name)
        out.append((name, fields, own))
        return name

    def value(self, kind):
        k = kind['kind']
        if k in ('int', 'ref'):
            return self.i32()
        if k == 'float':
            return self.f32()
        if k in ('bool', 'flag'):
            return self.u8()
        if k in ('point', 'fpoint', 'double'):
            return self.raw(8).hex()
        if k == 'colour':
            return self.raw(3).hex()
        if k in ('choice', 'name', 'string', 'text', 'ai'):
            return self.string()
        if k == 'object':
            cls = self.string()
            if not cls:
                return None
            if cls not in self.classes:
                raise Bad('an object of an unknown class %r at %d' % (cls, self.p))
            sub = []
            self.obj(cls, sub)
            return sub
        if k == 'inline':
            sub = []
            cls = kind.get('class')
            if cls is None:
                # the class the text files give it, if one; else by version
                named = [c for c in self.inline.get((kind['owner'], kind['key']), ()) if c in self.classes]
                if not named:
                    # an owner no text file has: the key's classes under any owner
                    named = sorted({c for (o, k), cs in self.inline.items() if k == kind['key']
                                    for c in cs if c in self.classes})
                if len(named) > 1 and self.p + 8 <= len(self.d):
                    version = struct.unpack_from('<f', self.d, self.p + 4)[0]
                    named = [c for c in named if self.classes[c]['version'] == version]
                if len(named) == 1:
                    cls = named[0]
            self.obj(cls, sub, kind.get('size'))
            return sub
        if k == 'objects':
            n = self.i32()
            sub = []
            for _ in range(n):
                self.obj(kind['class'], sub)
            return sub
        if k == 'array':
            n = self.i32()
            if not 0 <= n < 100000:
                raise Bad('an array of %d at %d' % (n, self.p - 4))
            items = []
            for _ in range(n):
                label = self.string() if kind.get('named') else None
                items.append((label, self.value(kind['element'])))
            return items
        if k == 'polygon':
            head = self.raw(12)
            if head[:7] != b'POLY2D\0':
                raise Bad('a polygon starting %r at %d' % (head[:8], self.p - 12))
            n = struct.unpack_from('<I', head, 8)[0]
            return self.raw(8 * n).hex()
        raise Bad('a property of an unknown kind %r at %d' % (k, self.p))

    def frame(self):
        """A frame (docs/engines/stawayteam/formats/frm16.md), by its sizes."""
        start = self.p
        head = self.raw(28)
        if head[0] != 0x2e:
            raise Bad('a frame starting %02x at %d' % (head[0], start))
        height = struct.unpack_from('<H', head, 8)[0]
        for size in struct.unpack_from('<III', head, 16):
            if size:
                self.raw(size + 4 * height)
        return (start, self.p)

    def own_data(self, name):
        w = self.own_writers(name)
        if w == '-':
            return None
        if w in ('0x55b640', '0x550ff0'):
            # CSequence (0x55b640): a box, then for each frame a byte, 1 if
            # it's there, and the frame; CRotationSequence (0x550ff0) adds 8
            # bytes for each frame. The count is the class's first property.
            box = struct.unpack('<4i', self.raw(16))
            count = self.out_count
            frames = []
            for _ in range(count):
                frames.append(self.frame() if self.u8() else None)
            extra = self.raw(8 * count).hex() if w == '0x550ff0' else None
            return {'box': box, 'frames': frames, 'extra': extra}
        if w == '0x50f6e0':
            # CModel: a count, a string, then for each a byte, 1 if it's
            # there, then its name, a 0 byte and a CRotationSequence.
            n = self.i32()
            label = self.string()
            seqs = []
            for _ in range(n):
                if self.u8():
                    seq_name = self.string()
                    self.u8()
                    sub = []
                    self.obj('CRotationSequence', sub)
                    seqs.append((seq_name, sub))
                else:
                    seqs.append(None)
            return {'label': label, 'sequences': seqs}
        if w == '0x52dad0':
            # CWayPointMap: its waypoints, then its lines.
            points = []
            for _ in range(self.i32()):
                self.obj('CPathWayPoint', points)
            lines = []
            for _ in range(self.i32()):
                self.obj('CPathLineSegment', lines)
            return {'points': len(points), 'lines': len(lines)}
        if w == '0x5305e0':
            # CPathWayPoint: the lines it's on, by number.
            return [self.i32() for _ in range(self.i32())]
        raise Bad('%s saves data of its own (%s), not known' % (name, w))


# The count of frames is the first property of CSequence and its kind.
_orig_obj = Reader.obj


def _obj(self, name, out, size=None):
    saved = len(out)
    start = self.p
    fmt = self.f32()
    version = self.f32()
    self.p = start
    cls = name
    if cls is None:
        names = self.by_version.get(version, [])
        cls = names[0] if len(names) == 1 else None
    if cls and self.own_writers(cls) in ('0x55b640', '0x550ff0'):
        # peek at the first property, the frame count, after the versions
        self.out_count = struct.unpack_from('<i', self.d, start + 8)[0]
    return _orig_obj(self, name, out, size)


Reader.obj = _obj


def entries(path):
    """DATA.dat's files: a ZIP of stored files (docs/engines/stawayteam/formats/data-dat.md)."""
    d = open(path, 'rb').read()
    p = 0
    while d[p:p + 4] == b'PK\x03\x04':
        _, _, method, _, _, _, size, _, nl, xl = struct.unpack_from('<HHHHHIIIHH', d, p + 4)
        name = d[p + 30:p + 30 + nl].decode('latin1')
        start = p + 30 + nl + xl
        yield name, d[start:start + size]
        p = start + size


def show(objs, depth=1):
    for name, fields, own in objs:
        print('%s%s' % ('  ' * depth, name))
        for fname, v in fields:
            if isinstance(v, list) and v and isinstance(v[0], tuple) and len(v[0]) == 3:
                print('%s  %s:' % ('  ' * depth, fname))
                show(v, depth + 2)
            else:
                print('%s  %s = %r' % ('  ' * depth, fname, v if not isinstance(v, str) or len(v) < 80 else v[:80]))
        if own:
            print('%s  (own data) %r' % ('  ' * depth, {k: (v if not isinstance(v, list) else '%d entries' % len(v)) for k, v in own.items()} if isinstance(own, dict) else own))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('schema')
    ap.add_argument('data')
    ap.add_argument('part', nargs='?', default='')
    ap.add_argument('--show', action='store_true')
    a = ap.parse_args()
    classes, by_version = read_schema(a.schema)
    texts = [d.decode('latin1') for n, d in entries(a.data)
             if n.rpartition('.')[2].lower() in ('txt', 'zax', 'mds', 'sty')]
    inline = inline_classes(texts)
    results = collections.Counter()
    seen = collections.Counter()
    problems = []
    shown = False
    for name, data in entries(a.data):
        ext = name.rpartition('.')[2].lower()
        if ext not in ('seq16', 'mdl16', 'way') or a.part.lower() not in name.lower():
            continue
        r = Reader(data, classes, by_version, inline)
        objs = []
        try:
            r.obj(TOP[ext], objs)
            if r.p != len(data):
                raise Bad('%d bytes left after the object' % (len(data) - r.p))
            results[ext + ' ok'] += 1
            seen.update(r.seen)
            if a.show and not shown:
                print(name)
                show(objs)
                shown = True
        except Bad as e:
            results[ext + ' failed'] += 1
            problems.append('%s: %s (in %s)' % (name, e, ' > '.join(r.path[-3:])))
    for p in problems[:20 if not a.part else None]:
        print(p)
    print(dict(results))
    print('objects of each class:', dict(seen.most_common()))


if __name__ == '__main__':
    main()
