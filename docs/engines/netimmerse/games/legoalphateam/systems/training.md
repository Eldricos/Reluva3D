# LEGO Alpha Team: into a mission, and the training tour

> **Status:** done · **Checked against:** the original running in Reluva · **Confidence:** confirmed unless marked

## Overview

From the menus into a mission, and the Alpha Team Training Area (M0010),
the guided tour at the Adventure's start.

## How it works

- Into a mission, from the menus. The `at-run` options that help:
  - `-fixed-clock`: the clock follows the instruction count (40,000 to a
    millisecond) and the main thread's sleeps, so runs repeat exactly.
  - `-quit <frame>`; `-limit 0` for no instruction limit.
  - `-point <frame>@x,y`: puts the game's own pointer on the screen with
    `DDMouse::SetPosition`. Relative moves land unevenly, so this is the
    reliable way to aim.
  - `-click <frame>` clicks where the pointer is.
  - `-key <frame>@vk` presses a key.
  - `-watch dll!export`: each frame, the calls to a function of the
    program's, with their first arguments and what they returned.
  - Tracing (`-log`, `-trace`) only when asked, so the instruction cache
    stays in use.
  - `-profile <from>-<to>`: where the program is, sampled every
    millisecond between those frames, then the share of samples in each
    function (the nearest export of its module, or the stand-in it's
    in). With `-profile-addresses`, also the busiest 64-byte stretches of
    code, each with its function.

  The route (`-fixed-clock`): click on frame 25 (past the demo); the main
  menu is up by frame 1500. Then Single Missions (`-point 1510@320,184
  -click 1512`), "Alpha Team Training Area" (`-point 1560@321,128 -click
  1562`) and the check mark (`-point 1600@134,315 -click 1602`), each
  where `-buttons` puts it. The mission loads (by frame 1800) and is
  drawn correctly: the camera flies over the board, and the goal text says
  to click to begin. A click (1900) starts the exercise: the text bar
  becomes the camera controls and the go button. (Frame numbers depend on
  how many instructions frames take: before the cycle counter's stand-in
  ([speed.md](../research/speed.md)), the menu was up by frame 100 and the mission began at 420. The
  frames quoted in the investigation that follows are from then.)

  Then it goes no further: Dash stands still and the go button does
  nothing. Traced (with `-watch`, `-calls` and `-dump`, below), the cause
  is sound:
  - Input works. The stop button (bottom left) opens the in-game menu
    (Keep Playing, Start Over, Skip Training Mission...).
  - The go button (`GoStop`) is locked on purpose. Clicks reach
    `DDUIManager::ProcessMouseClick`, which only hits buttons in its 4x4
    grid of screen cells (`ScreenButtons`). A button is added there only
    once it's clickable (`DDUIButton::SetClickable`: enabled at `+0x50`,
    clickable at `+0x51`, rectangle at `+0x40..+0x4c`, 0..1 from the
    bottom left).
  - The begin click enters a sub-machine of the interface's behaviour (in
    `PE0300.DLL`) whose entry code makes go unclickable. Its exit code
    makes go clickable again. The transition out is triggered by event
    `0xea7d`, which a state's entry code sends.
  - In the Adventure, "Skip Training Mission" leads to Mission One. There
    that state is entered when the mission loads, go unlocks, and the
    mission plays: go starts Dash running, and the others move. So
    simulation, input and interface are fine.
  - The Training Area (M0010) is driven by voice lines: `Locale_en.pac`
    has `M0010_010`, `M0010_020A`/`B`, ... up to `110`. Each step waits for
    its line. But every `DDStreamingAudioManager::AddSource` returns null
    (no A3D, so `NiA3dSoundSystem::Startup` fails), including the two the
    begin click starts. So the first line never "finishes", and nothing
    ever ends the lock.
  - So the fix was the A3D stand-in (step 5 of
    [PLAN.md](../PLAN.md#phases)): streaming sources that
    exist, play and end. With it, the begin click plays `M0010_010`
    (10.3 seconds) and then `M0010_020A` (1 second), each for its full
    length. The training then highlights a tile on the board and waits
    for the player. That needed real wave events: the streaming manager
    refills a source's buffer each time play passes the event at its
    middle (`DDStreamingAudioManager::UpdateBuffers`, `InitStreamData`).
    With every event read as set, it raced through the file, and the
    first line ended after about 3 seconds.
  - From there the training is a guided tour, and it now plays to the
    end. Each step plays a line, then waits for the player to do one
    thing. Played with the fixed clock (`-fixed-clock`, above: time
    follows the instructions run, so each run repeats) and the route below, Dash runs on
    frame 3050, reaches the top of the tower, and the game returns to
    headquarters on frame 3200.
  - The tour is the interface behaviour's training machine (PE0300,
    behaviour `3c3140`, machine `3c3580`). A choice point picks one of
    four tour composites; M0010's is a chain of about 50 states. A step's
    state waits (`DDState::registerEvts`) for one event in
    `0xea60`..`0xea7d`, each sent only in training (the level's `Training`
    option, read by `DDSpace::GetOptionValue`) by the code for one player
    action. Interface buttons raise their own events first: a table in
    PE0300 gives each button's (`Menu` `c358`, `NextCam` `c38f`, `GoStop`
    `c350`, `RotateCW` `c35b`, ...), and the in-game interface machine
    (`3c3c40`, in state `3c3f08` while playing) turns them into the
    tour's. The steps, in order:

    | Step | Event | What the player does |
    |---|---|---|
    | 1 | `ea61` | click the trainer TV (`T0001`, "teevee", a unit); later it repeats the current line |
    | 2 | `ea62` | open the menu (stop button); sent once the menu is fully open |
    | 3 | `ea63` | Keep Playing |
    | 4, 5 | `ea64` | next camera (right arrow) |
    | 6 | `ea65` | previous camera (left arrow) |
    | 7 | `ea66` | Nav_Pivot |
    | 8, 9 | `ea67` | Nav_InOut |
    | 10–13 | `ea68`, `ea69`, `ea6a`, `ea6b` | scroll by holding the pointer at the left, right, top and bottom edges (not over a button) |
    | 14 | `ea6c` | go |
    | 15 | `ea6d` | a team member's button |
    | 16 | `ea6e` | ICam |
    | 17 | `ea6f` | SmartCam |
    | 18 | `ea70` | stop |
    | 19 | `ea71` | drag the highlighted direction tile onto the blue square |
    | 20 | `ea72` | select it (click it) |
    | 21, 22 | `ea73` | rotate it clockwise; the second step waits until it faces 90 degrees |
    | 23 | `ea74` | Info |
    | 24 | `ea72` | select it again |
    | 25 | `ea75` | close its panel (a click beside it) |
    | 26 | `ea6c` | go: Dash runs |
    | 27 | `5a` | Dash's run ends; the tour is over |

    Some steps are entered only after a long line (one of about 400
    frames, before step 14).
  - The go button unlocks at `0xea7d`. The in-game interface machine
    sends it when it enters state `3c3ee8` in training (its entry code,
    PE0300 `0x10052310`). The training machine leaves the tour on it. In
    Mission One that state is entered as the level loads, which is why go
    works there.
  - Reading a step: `-graph` dumps the machines (states, their
    transitions, the events each waits for, the targets); `-watch` on
    `registerEvent` shows the steps as they're entered and on
    `??0DDEvent@@QAE@H@Z` the events raised. The sender of an event is
    found in PE0300 (`push $0xea..`), and its vtable says which state or
    transition it belongs to. PE0300 is mapped at `0x730000` in these
    runs, so its code can be watched by address.
  - The route, as `at-run` options after the menu route above (through
    `-click 1900`):

    ```
    -step ea61+10@click:193,92  -step ea62@click:65,435  -step ea63+20@click:320,98
    -step ea64@click:412,428  -step ea64@click:412,428  -step ea65@click:214,428
    -step ea66@click:348,428  -step ea67@click:282,425  -step ea67@click:282,425
    -step ea68@point:4,240  -step ea69@point:636,240  -step ea6a@point:320,3
    -step ea6b@point:320,477  -step ea6c@click:572,434  -step ea6d+10@click:419,428
    -step ea6e+10@click:470,428  -step ea6f+10@click:534,433  -step ea70+10@click:572,434
    -step ea71+10@drag:488,262,405,215  -step ea72+10@click:405,215
    -step ea73+10@click:389,140  -step ea73+10@click:389,140*4
    -step ea74+10@click:428,140  -step ea72+10@click:405,215
    -step ea75+10@click:432,182  -step ea6c+10@click:572,434
    ```

  The tracing options:
  - `-watch` takes `dll!export` or a raw `0x` address (for code in the
    behaviour DLLs, which `DDPackedDirFileLib` maps itself). For each
    frame it shows the calls, `this` (ECX), the first arguments (as text
    where they point to some), the caller, EAX and the x87 top of stack.
  - `-calls <frame>@<function>` prints the tree of calls one call makes.
    Functions are named as module!nearest-export+offset; for DLLs the
    program mapped itself, from their headers in memory.
  - `-dump <frame>@<address>,<bytes>` prints memory as words and floats.
  - `-graph <frame>@<node>[,<node>...][,<lo>-<hi>]` walks behaviour
    state machines through BehaviorComp's virtual functions, from the
    nodes given and every node found between `lo` and `hi`.
  - `-buttons <frame>` lists LoadComp's interface buttons: name, whether
    enabled and clickable, and where on the screen.
  - `-step <event>[+<delay>]@<action>` acts when a state starts waiting
    for the event (`DDBehavior::registerEvent`): `click:x,y` (with `*n`,
    clicked again every 40 frames until the training moves on),
    `point:x,y`, `key:vk`, `drag:x,y,x2,y2`, or `buttons`.

## Sources and checks

- **Where the facts come from:** the game's own code, traced as it ran
  with `at-run` (`-watch`, `-calls`, `-dump`, `-graph`), above.
- **How it was checked:** the tour played to its end with the fixed clock
  and the route above.
