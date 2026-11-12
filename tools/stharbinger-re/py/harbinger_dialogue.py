#!/usr/bin/env python3
"""Dump the dialogue script of Star Trek: DS9 Harbinger as readable text and CSV.

    python3 harbinger_dialogue.py CCQ.ALL CCF.ALL OUT_DIR

CCQ.ALL holds every spoken line as NUL-terminated text. CCF.ALL holds the voice clip
name for each line (CRLF separated), in the same order, so line i of one belongs to
line i of the other. Player choices start with their menu label in [brackets] (CCR.ALL
is just those labels collected), and recording notes from the voice sessions are kept
in {braces}.

Clip names are <conversation:3><speaker:3><index:2>, e.g. kdkdax42 = conversation kdk,
Dax, line 42. 'pro' is the protagonist (Ambassador Bannik).
"""

import collections
import csv
import re
import sys
from pathlib import Path

SPEAKERS = {
    "pro": "Bannik", "com": "Computer", "kir": "Kira", "dax": "Dax", "sis": "Sisko",
    "odo": "Odo", "qrk": "Quark", "jan": "Jani", "kar": "Karrig", "hvk": "Havik",
    "syr": "Syr", "kee": "Keel", "yar": "Yarrow", "ras": "Rasmus", "lyk": "Lykotian",
    "hav": "Havik",
}   # codes not listed here (set, wey, car, dev ...) are printed as-is


def parse_line(text):
    label = None
    m = re.match(r"\s*\[([^\]]*)\]", text)
    if m:
        label, text = m.group(1), text[m.end():]
    notes = re.findall(r"\{([^}]*)\}", text)
    spoken = re.sub(r"\{[^}]*\}", "", text).strip()
    return label, spoken, notes


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    ccq, ccf, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    texts = ccq.read_bytes().split(b"\x00")
    clips = [c.strip() for c in ccf.read_bytes().split(b"\n")]
    if texts and not texts[-1]:
        texts.pop()
    if clips and not clips[-1]:
        clips.pop()
    if len(texts) != len(clips):
        print("warning: %d text lines but %d clip names" % (len(texts), len(clips)))
    out.mkdir(parents=True, exist_ok=True)

    rows, script, last_conv = [], [], None
    speakers = collections.Counter()
    for i, (t, c) in enumerate(zip(texts, clips)):
        t, c = t.decode("latin-1"), c.decode("latin-1")
        conv, spk = c[:3].lower(), c[3:6].lower()
        who = SPEAKERS.get(spk, spk)
        speakers[who] += 1
        label, spoken, notes = parse_line(t)
        rows.append([i, c, conv, who, label or "", spoken, " | ".join(notes)])
        if conv != last_conv:
            script.append("\n=== conversation %s ===" % conv)
            last_conv = conv
        choice = "  [choice: %s]" % label if label else ""
        script.append("%5d %-9s %-8s %s%s" % (i, c, who + ":", spoken or "(no spoken text)", choice))

    with open(out / "dialogue.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["line", "clip", "conversation", "speaker", "menu_label", "text", "recording_notes"])
        w.writerows(rows)
    (out / "dialogue.txt").write_text("\n".join(script).lstrip() + "\n", encoding="utf-8")
    print("%d lines, %d conversations -> %s" % (len(rows), len({r[2] for r in rows}), out))
    print("lines per speaker:", ", ".join("%s %d" % kv for kv in speakers.most_common()))


if __name__ == "__main__":
    main()
