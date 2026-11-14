# And Then There Were None: how it was worked out

How the facts in these pages were found, so they can be checked again or
extended. The facts themselves are on their pages, each with its source.

## Ground rules

- **All of Reluva's code is new.** Nothing is taken from the game's code.
- **The game's code, text and assets stay in the player's copy.** Nothing
  of them is in this repository.
- See [CONTRIBUTING.md](../../../../../CONTRIBUTING.md) for what may and
  may not be used.

## Sources

- **The game's files:** the retail English release, two CDs, unpacked
  from their InstallShield cabinets.
- **The executable:** `ac.exe` (593,920 bytes), its strings and its import
  table so far; read as Orient Express's `motoe.exe` was
  ([../motoe/REVERSE_ENGINEERING.md](../motoe/REVERSE_ENGINEERING.md)).
- **The game's DLLs:** `granny2.dll` (2.5.0.10) and `binkw32.dll`, run in
  Reluva's x86 interpreter to check Reluva's decoders.

## Methods

The survey behind [PLAN.md](PLAN.md):

- **Unpacking:** `unshield x data1.cab` with `data1.hdr` and the three
  cabinets in one folder; the program, `ac.exe`, is in the `Txt Files`
  group, the rest in `Program Executable Files`.
- **The files' first bytes**, for what each extension is: JPEG, TGA and
  WAV under `.acg`, `.aca` and `.acv`; the scrambled text of `.act`,
  worked out by trying XOR keys on an event file (`wait .7` with 0x30),
  then the line counts and lengths checked on all 479 files.
- **The executable's strings and imports**, for its file names, paths,
  window title and registry key, and its file opening (`0x4028f0`, by
  the references to `"acx"`, `"act"` and `"txt"`).
- **Reluva's own readers** over the install: `reluva-tool awe files-check`
  (`-gr2 -oodle`, `-bik`) and `awe bink-check`.

## The tools

`reluva-tool help awe` lists the engine's commands
(`src/engines/awe/tools/`). Those used so far are in
[PLAN.md](PLAN.md#what-reluva-already-reads-checked).

## Working files not included

- **The unpacked install:** `unshield x data1.cab`, from your own CDs.
- **The decoded text:** each `.act` as plain text, by the rule in
  [PLAN.md](PLAN.md#what-the-survey-found).
