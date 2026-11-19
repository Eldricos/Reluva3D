# Harbinger reverse-engineering tools

The working tools used to find out how *Star Trek: Deep Space Nine –
Harbinger* works, kept so the findings in
[docs/engines/stharbinger/](../../docs/engines/stharbinger/) can be checked
and extended. They came from STDS9H, where the engine was first written. How
they were used:
[REVERSE_ENGINEERING.md](../../docs/engines/stharbinger/REVERSE_ENGINEERING.md).

They are research scripts, not part of Reluva: not built by CMake, not
tested, kept as they were used. Day to day, `reluva-tool stharbinger` does
the same work (the table below says which command); these stay for what it
doesn't do, and as the record of how the formats were worked out. They read
a copy of the game you own; nothing of the game is in this folder, and
nothing they write (extracted pictures and sounds, listings, string dumps,
disassembly) belongs in the repository.

## Setting up

- Python 3.8 or later. Only `harbinger_disasm.py` needs more than the
  standard library: `capstone` (`pip install capstone`).
- Run them with `-I` (`python3 -I py/harbinger_script.py ...`). The scripts
  that use another one find it in their own folder.

## The tools (`py/`)

| Tool | What it does | In `reluva-tool stharbinger` |
|---|---|---|
| `harbinger_extract.py extract OUT DISC...` | Unpacks every archive of the discs given and converts the assets (backgrounds, animations, sounds, palettes, deck maps), with a report and CSVs | `extract` |
| `harbinger_extract.py probe-raw`, `map-palettes` | One sound in several formats and rates; each deck map with every palette | `probe-raw`, `map-palettes` |
| `harbinger_script.py SSE_FOLDER OUT` | The scene and dialogue scripts as listings, their arguments named | `script` |
| `harbinger_dialogue.py CCQ.ALL CCF.ALL OUT` | The dialogue text as a script and a CSV | `dialogue` |
| `harbinger_strings.py DEEP.EXE OUT` | The executable's map: strings, who uses them, functions named from their messages, string tables | `exe-strings` |
| `harbinger_disasm.py DEEP.EXE START END [NAMES]` | A range of the code disassembled with fixups, strings and handler names | `exe-disasm` |
| `fn.py ADDRESS...` | Which function each address is in, from [function-starts.txt](../../docs/engines/stharbinger/research/function-starts.txt) and the handler names | — |
| `cvtab.py DEEP.EXE START END` | A range of the executable as data: each pointer with the string it points to | — |
| `raw.py CC.ALL COMMAND,SLOT...` | Every value one argument of one command takes across the scene scripts, with counts | — |

Addresses are hex, linear: code at `0x10000`, data at `0x50000`.
`harbinger_disasm.py` was `hd.py` in the notes.

## What only these do

- **`harbinger_extract.py`'s `rle_frames.csv`** has, for each frame, the
  measured width and height of its rows and the candidate header fields
  (read little- and big-endian) that were compared to find the header's
  layout; `reluva-tool`'s has the decoded headers only.
- **`harbinger_extract.py --jobs N`** extracts on several processes;
  `reluva-tool` uses one thread.
- **`harbinger_script.py`** reads a folder `SSE.ALL` was unpacked into, and
  walks a bare `CC.ALL` without its `CC.LST`.
- **`raw.py`, `cvtab.py`, `fn.py`** have no `reluva-tool` command.

## Known difference

`harbinger_script.py` names a talker's speaker code (`TALKHEAD`'s fourth
argument, `TALKER`'s third) from the symbols' order, which gives the wrong
speaker in some dialogues (`ras` for Quark in QUARK010). `reluva-tool
stharbinger script` takes it from the scene's `CCS` list as the game does
([UNCONFIRMED.md](../../docs/engines/stharbinger/UNCONFIRMED.md#settled)).
