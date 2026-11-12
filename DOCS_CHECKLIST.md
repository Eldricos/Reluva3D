# Docs checklist: status lines and empty sections

What the docs can't fill in without someone who knows how each part was checked.
Tick a box once its page is updated; `python3 tools/check-docs.py` checks that
what you write uses the standard's words.

Paths are under `docs/engines/`.

## How to fill a status line

- **Status**, format pages: `decoded`, `partial` or `unknown`. System pages:
  `done`, `partial` or `not started`.
- **Checked on** (format pages): the release, and how many of its files the
  reader was checked on. **Checked against** (system pages): the original
  running, a recording, or the code only.
- If a check changes the default Confidence, change that too (the words are in
  `docs/DOCUMENTATION_STANDARD.md`, "Sources and confidence").

## 1. Status lines missing a field (item 7)

### awe (Murder on the Orient Express)

- [ ] `awe/games/motoe/systems/characters.md`: **Status**
- [ ] `awe/games/motoe/systems/dialogue.md`: **Status**
- [ ] `awe/games/motoe/systems/interface.md`: **Status**
- [ ] `awe/games/motoe/systems/movies.md`: **Status** and **Checked against**
- [ ] `awe/games/motoe/systems/options.md`: **Status**
- [ ] `awe/games/motoe/systems/puzzles.md`: **Status**
- [ ] `awe/games/motoe/systems/saves.md`: **Status**
- [ ] `awe/games/motoe/systems/script.md`: **Status**
- [ ] `awe/games/motoe/systems/shadows.md`: **Status**

### clue

- [ ] `clue/formats/resi.md`: **Checked on**
- [ ] `clue/formats/videos.md`: **Status** and **Checked on**

### idtech3 (Elite Force)

- [ ] `idtech3/formats/aas.md`: **Status** and **Checked on**
- [ ] `idtech3/formats/mdr.md`: **Status** and **Checked on**
- [ ] `idtech3/formats/text.md`: **Checked on**
- [ ] `idtech3/games/stvef/systems/cinematics.md`: **Status** and **Checked against**
- [ ] `idtech3/games/stvef/systems/input.md`: **Status**
- [ ] `idtech3/games/stvef/systems/single-player.md`: **Status** and **Checked against**
- [ ] `idtech3/games/stvef/systems/sp-interface.md`: **Status** and **Checked against**
- [ ] `idtech3/games/stvef/systems/syscalls.md`: **Status**

### netimmerse (LEGO Alpha Team)

- [ ] `netimmerse/games/legoalphateam/systems/input.md`: **Checked against**
- [ ] `netimmerse/games/legoalphateam/systems/options.md`: **Checked against**
- [ ] `netimmerse/games/legoalphateam/systems/running.md`: **Checked against**
- [ ] `netimmerse/games/legoalphateam/systems/saves.md`: **Checked against**
- [ ] `netimmerse/games/legoalphateam/systems/sound.md`: **Checked against**
- [ ] `netimmerse/games/legoalphateam/systems/training.md`: **Checked against**

### stgenerations

- [ ] `stgenerations/formats/bridge.md`: **Status** and **Checked on**.
  Suggested from the docs: TESTING.md: the bridge played, with the crew's subtitled speech.
- [ ] `stgenerations/formats/combat.md`: **Status** and **Checked on**.
  Suggested from the docs: TESTING.md: space combat's briefing and console played.
- [ ] `stgenerations/formats/containers.md`: **Status** and **Checked on**.
  Suggested from the docs: TESTING.md: "all 18 archives (8175 files)" read on the 2 CD English release.
- [ ] `stgenerations/formats/fonts.md`: **Checked on**.
  Suggested from the docs: TESTING.md: "5 fonts" read on the 2 CD English release.
- [ ] `stgenerations/formats/levels.md`: **Checked on**.
  Suggested from the docs: TESTING.md: "all 12 levels and their scripts" read on the 2 CD English release.
- [ ] `stgenerations/formats/menu.md`: **Status** and **Checked on**.
  Suggested from the docs: TESTING.md: the captain's menu played (`stgenerations play`).
- [ ] `stgenerations/formats/scripts.md`: **Checked on**.
  Suggested from the docs: TESTING.md: "all 12 levels and their scripts" read on the 2 CD English release.
- [ ] `stgenerations/formats/story.md`: **Checked on**.
  Suggested from the docs: TESTING.md: New and the prologue's movie played.

### stharbinger

- [ ] `stharbinger/formats/action-sequences.md`: **Checked on**.
  Suggested from the docs: TESTING.md: action sequences checked on the real discs with STDS9H, before the port.
- [ ] `stharbinger/formats/animations.md`: **Checked on**
- [ ] `stharbinger/formats/archives.md`: **Checked on**
- [ ] `stharbinger/formats/audio.md`: **Checked on**
- [ ] `stharbinger/formats/backgrounds.md`: **Checked on**
- [ ] `stharbinger/formats/cursors.md`: **Checked on**.
  Suggested from the docs: TESTING.md: cursors checked on the real discs with STDS9H, before the port.
- [ ] `stharbinger/formats/deck-maps.md`: **Checked on**.
  Suggested from the docs: TESTING.md: the deck map checked on the real discs with STDS9H, before the port.
- [ ] `stharbinger/formats/discs.md`: **Checked on**.
  Suggested from the docs: TESTING.md: disc detection checked on the real discs with STDS9H, before the port.
- [ ] `stharbinger/formats/executable.md`: **Status** and **Checked on**
- [ ] `stharbinger/formats/saves.md`: **Checked on**.
  Suggested from the docs: TESTING.md: save, load and delete checked on the real discs with STDS9H, before the port.
- [ ] `stharbinger/formats/scripts.md`: **Checked on**.
  Suggested from the docs: TESTING.md: scene scripts and conversations checked on the real discs with STDS9H, before the port.
- [ ] `stharbinger/formats/videos.md`: **Checked on**.
  Suggested from the docs: TESTING.md: the intro videos checked on the real discs with STDS9H, before the port.

## 2. Sections that say "None yet" (item 8)

Each needs either content or confirmation that "None yet" is right.

### awe (Murder on the Orient Express)

- [ ] `awe/games/motoe/KNOWN_DIFFERENCES.md`, "Bugs fixed": the original's own bugs Reluva doesn't reproduce, if any
- [ ] `awe/games/motoe/KNOWN_DIFFERENCES.md`, "Additions outside the game": what Reluva adds around the game: viewers, tools, settings
- [ ] `awe/games/motoe/REVERSE_ENGINEERING.md`, "Working files not included": working files the notes mention but the repository leaves out, and how to make them again
- [ ] `awe/games/motoe/TESTING.md`, "Last checked": a row per check: date, release, platform, what was checked
- [ ] `awe/games/motoe/UNCONFIRMED.md`, "Guessed": working assumptions

### clue

- [ ] `clue/KNOWN_DIFFERENCES.md`, "Bugs fixed": the original's own bugs Reluva doesn't reproduce, if any
- [ ] `clue/KNOWN_DIFFERENCES.md`, "Additions outside the game": what Reluva adds around the game: viewers, tools, settings
- [ ] `clue/TESTING.md`, "Last checked": a row per check: date, release, platform, what was checked
- [ ] `clue/UNCONFIRMED.md`, "Guessed": working assumptions
- [ ] `clue/UNCONFIRMED.md`, "Settled": items confirmed or corrected since, with the date and how

### idtech3 (Elite Force)

- [ ] `idtech3/games/stvef/KNOWN_DIFFERENCES.md`, "Bugs fixed": the original's own bugs Reluva doesn't reproduce, if any
- [ ] `idtech3/games/stvef/REVERSE_ENGINEERING.md`, "Working files not included": working files the notes mention but the repository leaves out, and how to make them again
- [ ] `idtech3/games/stvef/UNCONFIRMED.md`, "Settled": items confirmed or corrected since, with the date and how

### netimmerse (LEGO Alpha Team)

- [ ] `netimmerse/games/legoalphateam/KNOWN_DIFFERENCES.md`, "Bugs fixed": the original's own bugs Reluva doesn't reproduce, if any
- [ ] `netimmerse/games/legoalphateam/REVERSE_ENGINEERING.md`, "Working files not included": working files the notes mention but the repository leaves out, and how to make them again
- [ ] `netimmerse/games/legoalphateam/TESTING.md`, "Last checked": a row per check: date, release, platform, what was checked

### stgenerations

- [ ] `stgenerations/KNOWN_DIFFERENCES.md`, "Bugs fixed": the original's own bugs Reluva doesn't reproduce, if any
- [ ] `stgenerations/REVERSE_ENGINEERING.md`, "Working files not included": working files the notes mention but the repository leaves out, and how to make them again
- [ ] `stgenerations/UNCONFIRMED.md`, "Seen running, not traced in the code": behaviour seen in the original but not found in its code
- [ ] `stgenerations/UNCONFIRMED.md`, "Settled": items confirmed or corrected since, with the date and how

### stharbinger

- [ ] `stharbinger/KNOWN_DIFFERENCES.md`, "Not yet as the original": known gaps
- [ ] `stharbinger/TESTING.md`, "Last checked": a row per check: date, release, platform, what was checked
- [ ] `stharbinger/UNCONFIRMED.md`, "Seen running, not traced in the code": behaviour seen in the original but not found in its code
- [ ] `stharbinger/UNCONFIRMED.md`, "Settled": items confirmed or corrected since, with the date and how

## Candidates noted during the clean-up

- [ ] `idtech3/games/stvef/KNOWN_DIFFERENCES.md`, "Bugs fixed": two items under
  "By choice" may belong here: keeping 1.x and Expansion Pack saves apart, and
  leaving out the original's cap of 5 copies of one sound per entity.
- [ ] `clue/UNCONFIRMED.md`: "Read from the code, not seen running" holds the
  sections "Code and recordings disagree" and "Not traced, no visible effect
  known"; check each is under the heading that fits.
- [ ] awe's `systems/dialogue.md` has 10 *(inferred)* items. If any matters to
  how Reluva behaves, list it in `awe/games/motoe/UNCONFIRMED.md` under
  "Inferred".
