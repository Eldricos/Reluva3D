#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors
"""Writes small synthetic Smacker files for the decoder tests.

    python3 make_smacker.py OUT_DIR

Creates test2.smk (SMK2, packed 8-bit mono audio), test4.smk (SMK4, packed
16-bit stereo audio) and long.smk (40 frames, for the action sequence tests). Every block type, the escape (recent value) leaves of the
Huffman trees, palette set/keep/copy commands and both audio paths are used.
The expected frames and audio come from an independent decoder (FFmpeg):

    ffmpeg -i test2.smk -f rawvideo -pix_fmt rgb24 test2.rgb
    ffmpeg -i test2.smk -f s16le -acodec pcm_s16le test2.pcm
"""

import random
import struct
import sys
from pathlib import Path

RUNS = list(range(1, 60)) + [128, 256, 512, 1024, 2048]


class BitWriter:
    def __init__(self):
        self.bits = []

    def put(self, value, n):
        for i in range(n):
            self.bits.append((value >> i) & 1)

    def bytes(self):
        out = bytearray((len(self.bits) + 7) // 8)
        for i, b in enumerate(self.bits):
            out[i >> 3] |= b << (i & 7)
        return bytes(out)


def balanced(leaves):
    """A balanced binary tree over the leaves: ('leaf', x) or ('node', l, r)."""
    if len(leaves) == 1:
        return ("leaf", leaves[0])
    mid = len(leaves) // 2
    return ("node", balanced(leaves[:mid]), balanced(leaves[mid:]))


def codes(tree, prefix=()):
    if tree[0] == "leaf":
        return {tree[1]: prefix}
    out = codes(tree[1], prefix + (0,))
    out.update(codes(tree[2], prefix + (1,)))
    return out


class SmallTree:
    def __init__(self, values):
        self.values = sorted(set(values))
        self.tree = balanced(self.values)
        self.codes = codes(self.tree)

    def write(self, bw):
        bw.put(1, 1)                       # present
        self._write(bw, self.tree)
        bw.put(0, 1)                       # end

    def _write(self, bw, t):
        if t[0] == "leaf":
            bw.put(0, 1)
            bw.put(t[1], 8)
        else:
            bw.put(1, 1)
            self._write(bw, t[1])
            self._write(bw, t[2])

    def encode(self, bw, v):
        for b in self.codes[v]:
            bw.put(b, 1)


ESCAPES = (0xFFF0, 0xFFF1, 0xFFF2)


class BigTree:
    """Values are emitted as ('val', v) or ('esc', i) (the i-th recent value)."""

    def __init__(self):
        self.cache = [0, 0, 0]
        self.emits = []        # per frame: list of symbols
        self.values = set()

    def new_frame(self):
        self.cache = [0, 0, 0]
        self.emits.append([])

    def emit(self, v, rng):
        # Use an escape leaf when the value is in the cache, half of the time.
        sym = ("val", v)
        for i in range(3):
            if self.cache[i] == v and rng.random() < 0.5:
                sym = ("esc", i)
                break
        if sym[0] == "val":
            self.values.add(v)
        self.emits[-1].append(sym)
        if v != self.cache[0]:
            self.cache = [v, self.cache[0], self.cache[1]]

    def build(self):
        leaves = [("val", v) for v in sorted(self.values)] + [("esc", i) for i in range(3)]
        self.tree = balanced(leaves)
        self.codes = codes(self.tree)
        raw = [v for v in self.values] + list(ESCAPES)
        self.lo = SmallTree([v & 0xFF for v in raw])
        self.hi = SmallTree([v >> 8 for v in raw])
        self.nodes = 2 * len(leaves)

    def write(self, bw):
        bw.put(1, 1)
        self.lo.write(bw)
        self.hi.write(bw)
        for e in ESCAPES:
            bw.put(e, 16)
        self._write(bw, self.tree)
        bw.put(0, 1)

    def _write(self, bw, t):
        if t[0] == "leaf":
            bw.put(0, 1)
            kind, x = t[1]
            v = x if kind == "val" else ESCAPES[x]
            self.lo.encode(bw, v & 0xFF)
            self.hi.encode(bw, v >> 8)
        else:
            bw.put(1, 1)
            self._write(bw, t[1])
            self._write(bw, t[2])

    def encode(self, bw, sym):
        for b in self.codes[sym]:
            bw.put(b, 1)


def palette_chunk(commands):
    data = bytearray(commands)
    size = len(data) + 1
    size = (size + 3) & ~3
    data += bytes(size - 1 - len(data))
    return bytes([size // 4]) + bytes(data)


def pack_audio(samples, stereo, bits16):
    """Packed audio chunk for interleaved samples (u8 values or s16)."""
    channels = 2 if stereo else 1
    deltas = [[] for _ in range(4)]
    pred = list(samples[:channels])
    stream = []
    for i in range(channels, len(samples)):
        c = i % channels
        if bits16:
            d = (samples[i] - pred[c]) & 0xFFFF
            stream.append((c * 2, d & 0xFF))
            stream.append((c * 2 + 1, d >> 8))
        else:
            d = (samples[i] - pred[c]) & 0xFF
            stream.append((c, d))
        pred[c] = samples[i]
    ntrees = 1 << ((1 if stereo else 0) + (1 if bits16 else 0))
    for t, v in stream:
        deltas[t].append(v)
    trees = [SmallTree(deltas[t] or [0]) for t in range(ntrees)]
    bw = BitWriter()
    bw.put(1, 1)
    bw.put(1 if stereo else 0, 1)
    bw.put(1 if bits16 else 0, 1)
    for t in trees:
        t.write(bw)
    for c in reversed(range(channels)):
        if bits16:
            v = samples[c] & 0xFFFF
            bw.put(((v & 0xFF) << 8) | (v >> 8), 16)
        else:
            bw.put(samples[c], 8)
    for t, v in stream:
        trees[t].encode(bw, v)
    unpacked = len(samples) * (2 if bits16 else 1)
    body = struct.pack("<I", unpacked) + bw.bytes()
    return struct.pack("<I", len(body) + 4) + body


def make(path, smk4, stereo, bits16, seed, nframes=4):
    rng = random.Random(seed)
    w, h = 32, 16
    blocks = (w // 4) * (h // 4)
    names = ("mmap", "mclr", "full", "type")
    trees = {k: BigTree() for k in names}

    # Choose each frame's runs of blocks and the values they code.
    frames = []
    for f in range(nframes):
        for t in trees.values():
            t.new_frame()
        runs = []
        blk = 0
        while blk < blocks:
            # The first frame has no skip blocks (nothing to keep yet).
            kind = rng.choice([0, 1, 3] if f == 0 else [0, 1, 2, 3])
            run_index = rng.randrange(0, min(blocks - blk, 6))
            run = RUNS[run_index]
            color = rng.randrange(256) if kind == 3 else 0
            trees["type"].emit(kind | (run_index << 2) | (color << 8), rng)
            mode = rng.choice([0, 1, 2]) if (smk4 and kind == 1) else 0
            for _ in range(run):
                if kind == 0:
                    trees["mclr"].emit(rng.choice([0x1020, 0x3040, rng.randrange(65536)]), rng)
                    trees["mmap"].emit(rng.randrange(65536), rng)
                elif kind == 1:
                    for _ in range(8 if mode == 0 else (2 if mode == 1 else 4)):
                        trees["full"].emit(rng.choice([0x0101, 0x0202, rng.randrange(65536)]), rng)
            runs.append((kind, mode, run))
            blk += run
        frames.append(runs)
    for t in trees.values():
        t.build()

    tb = BitWriter()
    for k in names:
        trees[k].write(tb)
    tree_bytes = tb.bytes()

    rate = 22050
    audio_flags = rate | 0x80000000 | 0x40000000
    if stereo:
        audio_flags |= 0x10000000
    if bits16:
        audio_flags |= 0x20000000
    channels = 2 if stereo else 1

    payloads, types = [], []
    for f in range(nframes):
        data = bytearray()
        ftype = 2                          # audio track 0
        if f in (0, 2):
            ftype |= 1
            if f == 0:
                cmds = []
                for i in range(256):
                    cmds += [i & 0x3F, (i * 3) & 0x3F, (i * 7) & 0x3F]
            else:
                # Keep 10, copy 5 from entry 100, one new colour, keep the rest.
                cmds = [0x80 | 9, 0x40 | 4, 100, 63, 0, 31, 0x80 | 127, 0x80 | 111]
            data += palette_chunk(cmds)
        n = 300
        if bits16:
            samples = [int(8000 * ((i * 37 + f * 11) % 200 - 100) / 100) for i in range(n * channels)]
        else:
            samples = [(128 + (i * 13 + f * 5) % 90 - 45) & 0xFF for i in range(n * channels)]
        data += pack_audio(samples, stereo, bits16)

        vb = BitWriter()
        pos = {k: 0 for k in names}

        def put(k):
            trees[k].encode(vb, trees[k].emits[f][pos[k]])
            pos[k] += 1

        for kind, mode, run in frames[f]:
            put("type")
            if kind == 1 and smk4:
                vb.put({0: 0b00, 1: 0b1, 2: 0b10}[mode], {0: 2, 1: 1, 2: 2}[mode])
            for _ in range(run):
                if kind == 0:
                    put("mclr")
                    put("mmap")
                elif kind == 1:
                    for _ in range(8 if mode == 0 else (2 if mode == 1 else 4)):
                        put("full")
        data += vb.bytes()
        data += bytes((-len(data)) % 4)
        payloads.append(bytes(data))
        types.append(ftype)

    header = b"SMK4" if smk4 else b"SMK2"
    header += struct.pack("<IIIiI", w, h, nframes, 100, 0)
    header += struct.pack("<7I", *([max(len(p) for p in payloads)] + [0] * 6))
    header += struct.pack("<I", len(tree_bytes))
    for k in names:
        header += struct.pack("<I", trees[k].nodes * 4 + 64)
    header += struct.pack("<7I", audio_flags, 0, 0, 0, 0, 0, 0)
    header += struct.pack("<I", 0)
    assert len(header) == 104
    out = bytearray(header)
    for i, p in enumerate(payloads):
        out += struct.pack("<I", len(p) | (1 if i == 0 else 0))
    out += bytes(types)
    out += tree_bytes
    for p in payloads:
        out += p
    Path(path).write_bytes(bytes(out))


if __name__ == "__main__":
    out = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    out.mkdir(parents=True, exist_ok=True)
    make(out / "test2.smk", False, False, False, 1)
    make(out / "test4.smk", True, True, True, 2)
    # A longer video for the action sequence tests.
    make(out / "long.smk", False, False, False, 3, nframes=40)
