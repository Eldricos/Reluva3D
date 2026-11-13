# LEGO Alpha Team: Mission One, and the camera

> **Status:** done · **Checked against:** a recording of the original · **Confidence:** confirmed unless marked

## Overview

Mission One (M1010) played to the end, and how the game's camera chooses
its shots there.

## How it works

- Done: Mission One (M1010) played to the end, from a saved Adventure at
  it. The view at each mission's start is a camera behind Dash; NextCam
  (412,428) five times shows the room from above. Dash runs from the far
  platform into a fenced pen, where a Direction Tile turns him right; the
  second tile, by the robot, is turned once clockwise (click it: a menu of
  rotate left, rotate right and two hints, "Hints left: 2"; rotate right
  is 16 left of the tile and 75 above it), so he leaves by the gap in the
  near wall and reaches the Plunger Lock. As `at-run` options after the
  Adventure route (the first slot chosen, with `P0Space  M1010` saved):

    ```
    -click 2400 -click 2500 -point 3046@412,428 -click 3050 -click 3100
    -click 3150 -click 3200 -click 3250 -point 3296@450,262 -click 3300
    -point 3336@434,187 -click 3340 -point 3376@150,470 -click 3380
    -point 3416@572,434 -click 3420
    ```

  - The camera at Mission One's end matches the original's, settled with
    a recording the game owner made for it: the original played twice
    with the camera input known (the first run as our route: the
    rightmost camera button five times, the second tile turned right, Go,
    then hands off). Side by side, half a second apart from Go, ours
    shows the same shots in the same order: the high view over the
    room (held 2.5 s), the pen and the walkway as Dash runs, the lock
    from beside the skull, the skull door opening (framed with its eyes
    and teeth), a moment's wide view, the reward close-up of Dash and
    "We did it!". Ours runs up to half a second behind, which is within
    how Go was lined up (the Go button turning red).
    - The earlier recording of the playthrough doesn't show the door:
      that player turned the camera (its view changes faster than the
      camera ever holds a base), and the camera before Go decides the
      bases used afterwards.
    - What was wrong in ours was the FPU's precision (below): at 53 bits
      the camera pushed into the skull until its texture filled the
      screen; with Direct3D's single precision it frames the skull as
      the original does.
    - How the camera chooses, as traced on the way (`at-run -watch`
      from frame 3400 with `-from`):
    - DDIcam (LoadComp) films the action of the highest priority: Dash's
      (30, `t0011.dll`), then the plunger lock's (220, `og0019b.dll`),
      then the door's (230, `og0019a.dll`, as it starts to open), each
      from the camera base FindNewBase chooses; `IsObscured` tests a
      segment to the actor with `DDSpace::CollideSegment`, the actor
      left out, so a base beside the door isn't obscured.
    - The interface behaviour (PE0300, `PE0300.DLL` in `Bhvr.pac`: the
      behaviour DLL that runs the game's screens and a mission's start and
      end) then waits `GoalAnimTime`
      seconds, plays `OgelRant` if the space has the option, waits two
      more, and while the music source still plays
      (`Sound_IsPlaying`), and only then turns the camera to
      `REWARD_CAM` (`DDIcam::ChangeManeuver(7)`), the close-up of Dash.
      `GoalAnimTime` (and `RewardAnimTime`) come from the space's
      options, else from `Config.txt` (LoadComp's `ConfigState`), else
      5 (and 3) seconds.
    - Not a setting. The keys the program reads from `Config.txt`:
      ActorBaseDir, AllVideoModes, AudioDir, AutoLoadPuzzle,
      CategoryImageFile, CategoryUnitFile, ClickAndRelease, DiscPath,
      GoalAnimTime, Hints, MusicBuffer, MusicSupport, Nav_InOut_Yaw,
      PortalExitDelay, PuzzleEditor, RewardAnimTime, TextureDir,
      ToolBoxLayout, UIFile, UseEnvLights. An install's `Config.txt`,
      after the game has run once, has only `DiscPath`,
      `DisplayDescription` and `DisplayGUID`, and its `Game.txt` only a
      header, so `GoalAnimTime` is 5 there too. The recording's 11 s
      from the lock to "We did it!" fits 5 + 2 seconds and the music.
      (With `GoalAnimTime  0.5` ours goes to Dash before the door's shot
      shows, which only hides the difference.)
    - Where ours turns: `FindNewBase` (LoadComp) first tests the
      space's main base (`DDIcam` 0xab3f0, here the one beside the
      skull at (-602.7, -50.4, 295.4)), then every base whose
      `Intelligent` attribute isn't `fixed_pan`. A base's line of sight
      to the actions (`IsObscured`, segments of radius 2 from the base)
      must be clear. Then it scores each base and takes the highest.
      Within 1.4 s of a change (0xab720 against 0xab558) it keeps the
      current base (0xab4a0). As Dash walks up to the lock, the
      score of the walkway base at (-123.9, -86.0, 182.6) falls from 0.23 to
      0.053 and the main base's rises to 0.055. They cross at
      `DDIcam` time 6.57 s, so ours turns there, before the lock's
      action (220) or the door's (230) is added.
    - Checked and ruled out: the frame rate (with `-clock-rate` at 3x,
      5x and 10x the frames a second, ours turns at the same 6.57 s);
      the door's bound (the merged spheres of OG0019A's shapes give
      about 200, as ours does); and the collision volumes.
      `DDSpace::CollectBoundingVolumes` adds every actor with a
      collision volume to the space's map (11 in M1010: the room, the
      door, the lock, Dash and others), and `CollideSegment` tests
      each one. The test is a capsule against the actor's bound
      sphere, then its volumes. The door's volume (on DOOR: a box cut
      by two half-spaces) is in world space at (-665.2, -50.5, 255.4),
      half-sizes 36 by 130.7 by 138. It lies just behind the main base,
      on the far side from Dash, so the segment rightly misses it.
    - The score is the square of the tangent of the angle between the
      line from the base to the shot and the camera's current view axis
      (`UCam`'s rotation). So it depends on where the camera points at
      that moment, which follows from the camera's smoothing so far.
    - What the recording shows from 77.5 s isn't the reward close-up:
      ours (`REWARD_CAM`, from Dash's left, the lock to his right) frames
      him differently from the original (straight on, the lock behind
      him). So the original films Dash from a walkway base the whole
      time. `IsObscured` also counts an actor whose centre isn't on
      screen from the current camera as obscured, so from the walkway
      the door's shot would find no base and the view stays. The whole
      difference comes down to ours turning to the main base at 6.57 s.
    - With Direct3D's single precision (below) ours still turns away from
      the walkway and shows the door, from a further base. So the
      precision isn't the cause either.
    - So ours runs the game's own camera code to a consistent result.
      There was only ever one release, so the difference has to be in
      what that code is given.
    - Compared from Go (69.6 s in the recording; Go is when the button
      turns from green to red), the two differ from the first second,
      not just at the lock:
      - The camera held right after Go is the view the player had
        chosen. A new base is held at least 1.4 s, and the main base
        2.5 s. Our route's five NextCam presses leave a high base at
        (-470.9, -59.5, 514.2), so ours shows the room from there for
        2.5 s. Half a second after Go the original is already low
        behind Dash.
      - The original player's view before Go is none of our eight
        NextCam views, so they used the rotate buttons too. With view 4
        (seven more presses after the tile clicks, Go at frame 3680) or
        view 7 (two more, Go at 3480), ours keeps to the walkway base
        and never turns to the skull-side base. The pre-Go camera does
        steer the rest. It still cuts to the door when it opens, and
        then to `REWARD_CAM`. The mission is won with each of these views.
      - From 72 to 75 s the original's view changes about every half
        second and stays close to Dash. That is quicker than the 1.4 s
        any base is held, so either the player was moving the camera
        or it was following Dash. Ours only uses fixed bases
        (`FIXED_PAN`) the whole time.
    - At the lock, ours does what the original does apart from the
      camera. The lock plays `plunger_in` and `plunger_out`; Dash's
      behaviour (T0011) calls `AssignZoomActor(Dash, "T0011")` (the
      actor `REWARD_CAM` films, kept only if none is set yet) and starts
      `victory`; the door starts `opening`. The original films that
      `victory` from the front, close, until "We did it!" (77.5 to
      89 s). Its view changes a little at about 85 s, where the chain's
      5 + 2 seconds and the music would bring `REWARD_CAM`. Ours films
      the door opening first.
  - Found on the way: Direct3D 7 puts the FPU in single precision
    (24 bits) when it makes a device, and on its calls, unless the
    program passed `DDSCL_FPUPRESERVE`. LEGO Alpha Team passes
    `MULTITHREADED | EXCLUSIVE | ALLOWREBOOT | FULLSCREEN`, so on a real
    machine its sums run in single precision. Ours left the FPU at 53
    bits; now `CreateDevice` and `BeginScene` set it as Direct3D does.
    It changes the run's details (which base the camera takes for the
    door); Mission One still plays through to "We did it!" and saves,
    and all 54 missions still load and run past Go to frame 3700.
  - Found on the way: `DDCaps::CalcProcSpeed` (LoadComp) times the
    processor with RDTSC across `Sleep(500)`, and recommends shadows,
    environment animation and other detail (`MakeRecs`) above 250 and
    350 million cycles a second. Our RDTSC counted instructions, which
    don't move during a Sleep, so it found nearly nothing. RDTSC now
    reads a 1 GHz counter going by the process's clock
    (`Cpu::timeStamp`).
  - "We did it!" offers Next Mission, Instant Replay! and Return to HQ, and
    the game saves on its own: `HighSpace  M1010` (the furthest mission
    any Adventure has finished, which Single Missions now offers) and
    `P0Space  M1020`, with CR-LF line ends.
  - In a mission, a click given with a position (`-click frame@x,y`) moves
    the pointer by relative motion, which the game doesn't follow all the
    way; `-point` first puts it where it's meant to be. A menu that's open
    takes the next click to close it.

## Sources and checks

- **Where the facts come from:** the game's own code, traced with
  `at-run -watch` (above).
- **How it was checked:** side by side with a recording the game owner
  made of the original played with the camera input known (above).
