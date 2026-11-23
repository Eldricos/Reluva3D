# Star Trek: Armada: testing

## Automated checks

### Unit tests

`tests/engines/storm3d/unit/`, a file per topic, each a suite
`storm3d.unit.<topic>`, on data built in the tests (never the game's
files):

| Suite | What it checks |
|---|---|
| `storm3d.unit.detection` | An empty folder, and the executable alone, aren't the game; Armada's three files by name are found as an unknown release |
| `storm3d.unit.zfs` | ZFS archives over two blocks: names, a name filling its field, decoding with an unencoded tail; damaged archives refused |
| `storm3d.unit.odf` | Object definitions: includes overridden, lists over lines, sections, the value forms, a stray line, failing includes |
| `storm3d.unit.config` | Settings in C: variables, defines, expressions, structures grouped, strings over lines, what's skipped, failures |
| `storm3d.unit.ldl` | The `.ldl` encoding, with a key made in the test; no key from a file that isn't the game's |
| `storm3d.unit.images` | The Images page's order (each texture before its mipmap levels) and which files it shows |
| `storm3d.unit.input_map` | `Input.map`'s blocks and conditions, a stray line, a block left open |

### The basic engine test

`storm3d.engine` (`tests/engines/storm3d/engine.cpp`), without the
game's files: a small install of its own, taken through what playing
does, against its record (`data/engine-record.txt`). So far its install
is Armada's three files by name, found as an unknown release, and a few
textures made in the test, which it then shows in the data viewer, page
by page, with keys pressed at set times: the record keeps a checksum of
every screen and each window title.

### The game-files test

<!-- Once it has one: with your own copy of the game, what it runs and
what its records hold. See docs/DEVELOPMENT.md, "Engine tests". -->
Not yet.

### What the automated checks don't cover

What needs the game's own files, checked by hand with `reluva-tool
storm3d` ([Last checked](#last-checked)): recognising the English
release by its hashes, and reading every file of each kind
(`odf-check`, `config-check`, `image-check`, and `zfs-extract`, `ldl`
and `input-map` on each file).

## A playthrough by hand

1. Add the install in the launcher: it's found as "Star Trek: Armada".
2. Start it: an empty screen; Esc goes back to the launcher.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| November 2026 | English retail 1.0 | Linux | `reluva-tool detect` on the install finds it as the English release |
| November 2026 | English retail 1.0 | Linux | Both ZFS archives extract whole (596 files); all 488 object definitions, 94 settings files and 3 `Input.map`s read; all six `.ldl` files decode |
| November 2026 | English retail 1.0 | Linux | All 2,320 pictures decode (`image-check`); four looked at as PNGs |
