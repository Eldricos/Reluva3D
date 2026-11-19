# Star Trek: Deep Space Nine - Harbinger: research notes

Working notes, kept as they were written: they don't follow the page
templates, and they may hold guesses since settled or corrected. What the
engine relies on is on its pages in [formats/](../formats/) and
[systems/](../systems/), each with its source. How the notes were made is
in [REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md).

This folder holds the notes from reading
DEEP.EXE that the engine is built from. Addresses are linear: the code object
starts at 0x10000 and the data object at 0x50000. Confidence marks: **H** read
directly from the code, **M** inferred with a small gap, **L** a guess.
[UNCONFIRMED.md](../UNCONFIRMED.md) collects the M and L points that matter to
the engine.

| File | Contents |
|---|---|
| [interpreter.md](interpreter.md) | Scene script interpreter: directives, variables, labels, GOSUB, timers, hotspots, animations and the renderer, backgrounds, palettes and fades |
| [dialogue.md](dialogue.md) | Conversations: dialogue records, turn logic, dialogue directives, speech archives, lip-sync and the reply menu |
| [menu-and-saves.md](menu-and-saves.md) | The game menu, saved games (field by field), dlas.ds9, death, the deck map |
| [arcade.md](arcade.md) | The action-sequence (arcade) engine: sprites, paths, shots, damage, music |
| [cursors-and-video.md](cursors-and-video.md) | Mouse cursors, palette cycling, Smacker video playback, the credits |
| [special.md](special.md) | The `ACTIONSEQ` special functions (phaser, enemy shots, countdown, map location, tricorder icon) |
| [functions.txt](functions.txt) | Functions named from their own debug messages |
| [handler-addresses.txt](handler-addresses.txt) | Names given to handler addresses (directive handlers and helpers); `exe-disasm` names them |
| [file-references.txt](file-references.txt) | File names in the executable and the code that uses them |
| [function-starts.txt](function-starts.txt) | The start address of each function found in the code; `fn.py` ([tools/stharbinger-re/](../../../../tools/stharbinger-re/README.md)) reads it |

The `.txt` files are plain lists, one entry a line, not pages:
`reluva-tool stharbinger exe-disasm` reads `handler-addresses.txt` as it is
(`<address> <name>` a line), so they keep their format.
