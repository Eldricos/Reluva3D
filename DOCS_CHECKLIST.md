# Docs: work to look into later

Improvements found while cleaning up the docs that aren't urgent: none of
them blocks anything, and most need someone who knows how each part was
checked. Look into them when there's time; tick a box once its page is
updated. `python3 tools/check-docs.py` checks that what's written uses the
standard's words.

Paths are under `docs/engines/`.

## How to fill a status line

- **Status**, format pages: `decoded`, `partial` or `unknown`. System pages:
  `done`, `partial` or `not started`.
- **Checked on** (format pages): the release, and how many of its files the
  reader was checked on. **Checked against** (system pages): the original
  running, a recording, or the code only.
- If a check changes the default Confidence, change that too (the words are in
  `docs/DOCUMENTATION_STANDARD.md`, "Sources and confidence").

## 1. Status lines missing a field

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

- [x] `idtech3/formats/aas.md`: **Status** and **Checked on**
- [x] `idtech3/formats/mdr.md`: **Status** and **Checked on**
- [x] `idtech3/formats/text.md`: **Checked on**
- [x] `idtech3/games/stvef/systems/cinematics.md`: **Status** and **Checked against**
- [x] `idtech3/games/stvef/systems/input.md`: **Status**
- [x] `idtech3/games/stvef/systems/single-player.md`: **Status** and **Checked against**
- [x] `idtech3/games/stvef/systems/sp-interface.md`: **Status** and **Checked against**
- [x] `idtech3/games/stvef/systems/syscalls.md`: **Status**

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

## 2. Sections that say "None yet"

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

- [x] `idtech3/games/stvef/KNOWN_DIFFERENCES.md`, "Bugs fixed": the original's own bugs Reluva doesn't reproduce, if any
- [x] `idtech3/games/stvef/REVERSE_ENGINEERING.md`, "Working files not included": working files the notes mention but the repository leaves out, and how to make them again
- [x] `idtech3/games/stvef/UNCONFIRMED.md`, "Settled": items confirmed or corrected since, with the date and how

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

## 3. Terms used without explanation

Terms specific to a game or its engine that may be used without being
explained where a reader first meets them. For each, check the place
given (its first use, README first); if it isn't explained there, either
explain it in a few words at that point or add it to a short "Terms" list
in the game's README. If "Terms" lists are adopted, add the section to
`docs/templates/engine/README.md` and `docs/templates/game/README.md` too.

### idtech3 (Elite Force)

- [x] Holomatch: first in `idtech3/games/stvef/README.md:18`
- [x] QVM: first in `idtech3/README.md:25`
- [x] cgame: first in `idtech3/games/stvef/README.md:25`
- [x] qagame: first in `idtech3/games/stvef/README.md:25`
- [x] cvar: first in `idtech3/README.md:64`
- [x] configstring: first in `idtech3/games/stvef/README.md:26`
- [x] ICARUS: first in `idtech3/games/stvef/formats/entities.md:93`
- [x] refEntity: first in `idtech3/README.md:56`
- [x] netchan: first in `idtech3/games/stvef/systems/network.md:28`
- [x] pure server: first in `idtech3/games/stvef/TESTING.md:106`
- [x] botlib: first in `idtech3/games/stvef/systems/syscalls.md:265`
- [x] AAS: first in `idtech3/games/stvef/UNCONFIRMED.md:148`

  Done with a "Terms" list in `idtech3/README.md` (the engine's words) and
  `idtech3/games/stvef/README.md` (Holomatch, ICARUS), a few words at the
  first use on the other pages, and the section added to both README
  templates.

### awe (Murder on the Orient Express)

- [ ] Granny: first in `awe/README.md:31`
- [ ] Oodle1: first in `awe/games/motoe/README.md:28`
- [ ] Povent: first in `awe/games/motoe/UNCONFIRMED.md:68`
- [ ] `SPRT`, `HOTS`, `BNKX` and the other scene records: first in `awe/games/motoe/UNCONFIRMED.md:55`
- [ ] track mask: first in `awe/games/motoe/README.md:105`
- [ ] walk grid: first in `awe/games/motoe/REVERSE_ENGINEERING.md:80`

### netimmerse (LEGO Alpha Team)

- [ ] NIF: first in `netimmerse/README.md:30`
- [ ] B1 (the option chosen): first in `netimmerse/games/legoalphateam/PLAN.md:3`
- [ ] behaviour DLL: first in `netimmerse/games/legoalphateam/PLAN.md:21`
- [ ] `at-run`: first in `netimmerse/games/legoalphateam/README.md:117`
- [ ] DDIcam: first in `netimmerse/games/legoalphateam/REVERSE_ENGINEERING.md:104`
- [ ] PE0300: first in `netimmerse/games/legoalphateam/systems/camera.md:56`
- [ ] fixed clock: first in `netimmerse/games/legoalphateam/systems/training.md:80`
- [ ] Dash and Ogel (the game's characters): first in `netimmerse/games/legoalphateam/formats/fin.md:312`

### clue

- [ ] EagleM: first in `clue/README.md:24`
- [ ] RESI: first in `clue/README.md:42`
- [ ] `PACK`, `DDRV` and `EIMG`: first in `clue/PLAN.md:14`
- [ ] No Die Roll: first in `clue/formats/resi.md:93`

### stharbinger

- [ ] STDS9H: first in `stharbinger/README.md:5`
- [ ] DOS/4GW: first in `stharbinger/REVERSE_ENGINEERING.md:15`
- [ ] LE (the executable's format): first in `stharbinger/REVERSE_ENGINEERING.md:53`
- [ ] directive: first in `stharbinger/formats/executable.md:17`
- [ ] ACTIONSEQ: first in `stharbinger/UNCONFIRMED.md:43`

### stgenerations

- [ ] SGEN: first in `stgenerations/formats/menu.md:19`
- [ ] LV2: first in `stgenerations/formats/README.md:14`

## 4. Candidates noted during the clean-up

- [x] `idtech3/games/stvef/KNOWN_DIFFERENCES.md`, "Bugs fixed": two items under
  "By choice" may belong here: keeping 1.x and Expansion Pack saves apart, and
  leaving out the original's cap of 5 copies of one sound per entity.
  The saves moved: the original lists saves its game can't load. The sound
  cap stays under "By choice": it's ioquake3's, and it was Reluva's own
  first version that went silent with it, not the original.
- [ ] `clue/UNCONFIRMED.md`: "Read from the code, not seen running" holds the
  sections "Code and recordings disagree" and "Not traced, no visible effect
  known"; check each is under the heading that fits.
- [ ] awe's `systems/dialogue.md` has 10 *(inferred)* items. If any matters to
  how Reluva behaves, list it in `awe/games/motoe/UNCONFIRMED.md` under
  "Inferred".
- [x] `idtech3/formats/md3.md` says "all 1156 retail Elite Force models".
  `model-check` counts 790 models in 1.2 and 917 with the Expansion Pack
  (919 `.md3`/`.mdr` files in the paks); 1156 matches neither. Recount, and
  give the release.
  Done: `md3.md` gives 790 for 1.2 and 917 with the Expansion Pack.
- [x] The Expansion Pack's `models/mapobjects/scavenger/k_chair1.md3`
  doesn't read in Reluva: its surface 1 has 0 frames, the model 1. Check
  whether the original draws it (and how) or never loads it, then make
  Reluva match and note it in `md3.md`.
  Done: the original's loader doesn't check a surface's frame count, so it
  loads the model; Reluva now reads it the same way (`md3.md`). Seeing the
  chairs drawn is in `idtech3/games/stvef/TESTING.md`.
- [x] `idtech3/games/stvef/REVERSE_ENGINEERING.md` says Elite Force has
  1872 shaders; `shader-check` counts 1891 in 1.2 (as the game's README
  says) and 2059 with the Expansion Pack. Give the release with the count.
  Done: 1872 is 1.0's count (1.1 has 1875). Each count now names its
  release, in `REVERSE_ENGINEERING.md`, `PLAN.md`, `shader.md` and the
  game's README; none of the releases uses a directive outside Quake III's.
