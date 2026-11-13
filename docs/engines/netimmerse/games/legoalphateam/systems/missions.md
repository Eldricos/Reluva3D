# LEGO Alpha Team: every mission

> **Status:** done · **Checked against:** recordings of the original · **Confidence:** confirmed unless marked

## Overview

Every mission, loaded, run past Go and solved by the game's own solutions.

## How it works

- Done: every mission loads and runs. With a saved Adventure at each
  mission (`P0Space`), all 54 (the three later training missions, M0020
  to M0040, and the Adventure's 50, M1010 to M4070) load and show their
  fly-over and goal, by frame 2300 of a fixed-clock run (about two
  minutes each). Then each was run on past its fly-over with Go pressed
  and nothing placed, to frame 3700: all ran without a failure, the crew
  moving as the room has them, most ending in "Team Member Down! Try
  Again / Return to HQ", as an unsolved room does.
  - `reluva-tool netimmerse at-sweep <game folder> <out folder>` does this second
    run for every mission (or those named), as many at once as there
    are processors (`-jobs`), and reports which didn't run to the frame
    (`-quit`, 3700). It shows frames late, as the player does. Each mission's log and save (and with `-shots`, a
    frame every 100) go in its own folder. All 54 take about 50 minutes
    on 4 processors, most of it the menus and loading before each Go.
    With the sound, gamma and late-presenting changes, all 54 still ran
    to frame 3700 (5,056 s on 4 processors, against 5,298 s showing each
    frame at its own flip).
- Done: **every Adventure mission solved**, by the game's own solutions.
  Most missions ship a `<space>_SOLVED.PUZ` beside their puzzle: the same
  room with the movable parts where they solve it (the four training
  missions have none).
  - `at-run -solved <space>` makes the game read that file in place of
    `<space>.PUZ`. It writes a copy of `Etc.pac` whose directory entry for
    the puzzle points at the solved one's data, and the run reads that
    copy. `-until-advance` stops the run once `Game.txt` shows the first
    Adventure on another mission: the game saves the next mission as
    soon as one is won.
  - `at-sweep -solved` runs every Adventure mission so, through the menus
    and Go, up to frame 12000.
  - Result, on 4 processors in 5,106 s: 50 of the 51 moved on, each
    between frames 3225 and 3600 (Go is pressed at 3200), in the game's
    own order (M2090 leads to the training mission M0020, M2160 to
    M0030, M3050 to M0040). Without the solved puzzles, none moves on.
  - The 51st, M4070, is the last mission. Solved, it goes straight to the
    ending, "The Victory Party" and the credits, and saves nothing new.
    That was checked from its frames.
  - Spot checks of M1010 and M1085 show the crew running to the goal and
    "We did it!" with Next Mission, Instant Replay and Return to HQ.
  - So every behaviour DLL a solution needs works as shipped, in the
    order the game's own solution runs it.
  - `AutoLoadPuzzle <space>` in `Config.txt` would skip the menus, but the
    screen stays black. Traced (`at-run -watch`) to the game itself:
    - The interface behaviour (`PE0300.DLL`) reads the key, keeps the
      space's name and takes the transition `To_FirstSpace` instead of
      `To_ShowLogos`. That skips the logo, menu and loading states.
    - Those states are what load a space into the director. In a normal
      run, each space loaded calls `DDDirector::SetBackdrop` and
      `SetExterior`: at frames 45 and 73 for the logos' spaces, and
      1665–1667 for the mission's. The logos' `FadeIn` follows, and the
      mission's start calls `ClearCameraBases`, after which the space
      updates every frame (`DDSpace::UpdateActivities`).
    - With `AutoLoadPuzzle`, none of those calls ever happens. The space
      updates twice, at frames 3 and 162. The director's camera clicks
      every frame but stays at its default place, at the origin and
      unrotated, with nothing in view. No overlay camera is inserted for
      the interface, so nothing is drawn at all: the game runs about
      180,000 instructions a frame against 2.6 million in a mission.
      Calling `FadeIn` and `SelectCameraBase` into it changes nothing.
    - Reluva's stand-ins have no part in that choice. The key is a
      leftover the shipped game doesn't support. Making it work would
      mean doing the menus' work for the game rather than running it, so
      the sweeps go through the menus.
- Single Missions lists only the missions an Adventure has reached ("Select
  a Mission to Revisit").

## Sources and checks

### Against recordings of the original

- Recordings of the original: a playthrough (the tutorial and all 51
  Adventure missions, 960x720, about eight hours), given by the game
  owner, kept outside the repository. Compared with ours:
  - Each mission's opening (its fly-over, the goal text and the panel)
    looks the same as ours: the rooms, textures, lighting and text.
  - The Adventure brings in the later training missions as it goes:
    Electric Cords and Sockets (M0020) before Hectic Electric (M2100),
    Belts and Belt Wheels (M0030) before Trouble Sub Docking Bay (M3010),
    and Ropes and Rope Loops (M0040) before Tug Team (M3060). Each
    compound starts with a text of its own on black ("All aboard!..."
    before M2010, another before M4010), as M1010's does.
  - In the training mission, the mission menu has Skip Training Mission
    too (Keep Playing, Start Over, Skip Training Mission, Options, Return
    to HQ, Exit Game).
  - Mission One's end looks different from ours because that player
    turned the camera; with the camera input matched, it's the same
    ([camera.md](camera.md)).
  - Each recording shows that mission solved, which is what playing them
    through needs.

## Open questions

### Still to do

- every mission played start to finish by hand, placing the parts (each
  is solved by the game's own `_SOLVED.PUZ`, above, and Mission One by
  hand, [camera.md](camera.md));
- checked against the original running on a period machine or a
  virtual machine: screenshots and recordings side by side.
