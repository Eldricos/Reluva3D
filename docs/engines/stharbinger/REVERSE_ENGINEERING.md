# How Reluva's Harbinger support was worked out

Reluva runs Star Trek: Deep Space Nine - Harbinger from the player's own
copy, with none of the original's code in it. This page says how the facts
behind that were found, so they can be checked again or extended. The facts
themselves are in [formats/](formats/), [research/](research/)
and this engine's [README](README.md), each with the address or file it came
from. The engine came to Reluva from STDS9H, where this work was done.

## Ground rules

- **All of Reluva's code is new.** The game's file formats and behaviour were
  worked out from the game itself; nothing is taken from its code.
- **The retail game is studied as a legally owned copy, for
  interoperability.** `DEEP.EXE` (the DOS/4GW executable) was disassembled
  and its data files read, to learn what an engine must do to run the game's
  own data. The game's code, text and assets stay in the player's copy:
  nothing of them is in this repository, and the working files that would
  reproduce them are left out (below).
- **Published format write-ups are read for facts only.** Smacker and PKWARE
  DCL, shared with other games, follow the open descriptions named in
  [docs/common/smacker.md](../../common/smacker.md) and
  [docs/common/dcl.md](../../common/dcl.md).
- See [CONTRIBUTING.md](../../../CONTRIBUTING.md) for what may and may not be
  used.

## The notes

[research/](research/) holds the notes from reading
DEEP.EXE that the engine is built from. Addresses are linear: the code object
starts at 0x10000 and the data object at 0x50000. Confidence marks: **H** read
directly from the code, **M** inferred with a small gap, **L** a guess.
[UNCONFIRMED.md](UNCONFIRMED.md) collects the M and L points that matter to
the engine.

| File | Contents |
|---|---|
| [interpreter.md](research/interpreter.md) | Scene script interpreter: directives, variables, labels, GOSUB, timers, hotspots, animations and the renderer, backgrounds, palettes and fades |
| [dialogue.md](research/dialogue.md) | Conversations: dialogue records, turn logic, dialogue directives, speech archives, lip-sync and the reply menu |
| [menu-and-saves.md](research/menu-and-saves.md) | The game menu, saved games (field by field), dlas.ds9, death, the deck map |
| [arcade.md](research/arcade.md) | The action-sequence (arcade) engine: sprites, paths, shots, damage, music |
| [cursors-and-video.md](research/cursors-and-video.md) | Mouse cursors, palette cycling, Smacker video playback, the credits |
| [special.md](research/special.md) | The `ACTIONSEQ` special functions (phaser, enemy shots, countdown, map location, tricorder icon) |
| [functions.txt](research/functions.txt) | Functions named from their own debug messages |
| [handler-addresses.txt](research/handler-addresses.txt) | Names given to handler addresses (directive handlers and helpers); `exe-disasm` names them |
| [file-references.txt](research/file-references.txt) | File names in the executable and the code that uses them |

## The tools

`reluva-tool stharbinger <command>` does what STDS9H's Python tools did
(`reluva-tool help stharbinger` lists them):

| Command | What it does | Was |
|---|---|---|
| `extract <out> <game>` | Unpacks every archive and converts the assets (backgrounds, animations, sounds, palettes, deck maps) to .png and .wav, with a report | `harbinger_extract.py extract` |
| `probe-raw <out> <file.raw>` | One sound in several formats and rates, to tell them apart by ear | `harbinger_extract.py probe-raw` |
| `map-palettes <out> <game>` | Each deck map with every palette, as contact sheets | `harbinger_extract.py map-palettes` |
| `script <game> <out>` | The scene and dialogue scripts disassembled, their arguments named from the name lists and the linker's cross-references | `harbinger_script.py` |
| `dialogue <game> <out>` | The dialogue text as a readable script and a CSV: speaker, clip, menu label, recording notes | `harbinger_dialogue.py` |
| `exe-strings <DEEP.EXE> <out>` | The executable's map: its LE objects and fixups, every string with the code that uses it, functions named from their debug messages, string tables | `harbinger_strings.py` |
| `exe-disasm <DEEP.EXE> <start> <end> [names]` | A range of the code disassembled, with fixup targets, strings and handler names | `harbinger_disasm.py` (no longer needs capstone) |
| `files`, `info`, `conversations` | The game's parts and files; what its data holds; every conversation played through | (`--info`, and a test) |

`<game>` is anything the launcher takes: a CD, both CDs side by side, an
install. `DEEP.EXE` is inside disc 1's `SSE/SSE.ALL`: `extract` unpacks it
(to `disc1/SSE/SSE/DEEP.EXE`).

## Working files that are not included

The notes mention working files that are left out of the repository because
they reproduce the game's code or text:

- `*.asm`: disassembly dumps of address ranges of DEEP.EXE. Make any of them
  again from your own copy with `reluva-tool stharbinger exe-disasm DEEP.EXE
  START END` (the notes give each dump's range; `hd.py` in the notes is this
  command's forerunner).
- Script listings (`listings2/scenes`, `listings2/dialogues`): made with
  `reluva-tool stharbinger script` from the game's `CC.ALL` and `CCB.ALL`.
- The string dumps and string tables of the executable (they include the
  credits): made with `reluva-tool stharbinger exe-strings`.
- `fn.py`, `cvtab.py`, `raw.py`: small one-off helpers (find the enclosing
  function, dump a table of dwords with fixups, decode raw `CC.ALL` data).
