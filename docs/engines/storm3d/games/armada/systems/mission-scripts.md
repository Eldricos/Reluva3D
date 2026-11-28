# Star Trek: Armada: mission scripts

> **Status:** partial · **Code:** `src/engines/storm3d/play/mission_scripts.cpp` · **Checked against:** Federation 1's scripts started in Reluva's x86 emulator · **Confidence:** seen, not traced unless marked

## Overview

Each campaign mission's events (its cinematics, objectives, waves of
enemies, winning and losing) are compiled code: two Windows DLLs in
`missions/`, named for the mission, `<name>S.dsl` and `<name>R.drl`
(`Federation1S.dsl`, `Federation1R.drl`), 30 pairs. Reluva runs them
in its x86 emulator, as the original program loads them, standing in
for what they take from Armada.exe.

## How it works

- **The pair:** the `S` DLL holds the mission's variables and steps,
  all exported by name (`?Enterprise@@3HA`, a unit's handle;
  `?MISSION_BEGIN@@3Vhot_int@@A`; `?win_mission@@YAXXZ`). The `R` DLL
  imports them from it, so it's loaded second, and holds the rules:
  each a condition and an action, made through the world's rule engine
  (`IIIE::create_rule` on `WORLD`, its engine 0x38 into it) and tied to
  the variables it depends on (`hot_type::add_dependent_rule`)
  *(from the code)*.
- **Start-up:** neither exports a function to call; both do their work
  in their C runtime's start-up, as their global objects are built.
  Federation 1's builds 17 `hot_int`s and 12 `timer_int`s, gives each
  its first value, looks up 12 units by name (`GetHandle`), and makes
  34 rules tied to 52 variables, among them the program's own
  `ALWAYS_EVALUATE`, `isCineractiveDone` and `isCineractiveCancel`.
- **What they import:** Federation 1's pair takes 94 functions and
  variables from Armada.exe: the rule engine's classes (`hot_int`,
  `timer_int`, `dirty_int`, `rule`, `IIIE`, `script`, `character`,
  `linked_list`) and the game's functions (`Goto`, `Attack`,
  `BuildObject`, `CineractiveShot`, `ShowTitle`, `SucceedMission`...);
  all 30 missions, 178 functions besides the engine's. Their own C
  runtime is linked in: from Windows they take KERNEL32 alone.
- **Loading:** the map's `[AiMission]` object names the two DLLs, the
  script's first; the program loads each from the missions folder, else
  the AI folder, and frees them as the game ends, the rules first. All
  their work is done in their start-up and through what they import
  and export: nothing is looked up by name (0x4068a0, 0x406020,
  0x403280) *(from the code)*.
- **Variables** *(from the code)*: a `hot_int` is a value, -1 to start,
  that on a change is marked and put on a list of those changed
  (0x4052d0). A `timer_int` is one counting seconds down from `start`,
  0 to start; run out, it's set to 1 (0x406ad0, 0x406b70); `stop`
  leaves its value. `ALWAYS_EVALUATE` is a `dirty_int`, -1, never taken
  clean (0x404ed0). Each rule depends on some of them.
- **A pass** (0x4032b0), once a frame, with the frame's game seconds
  (its real time, at most 0.2, times the game speed over 3; real time in
  a cineractive) *(from the code)*:
  1. the running timers count down;
  2. each variable changed, newest first, queues the rules depending on
     it and is taken clean; a timer run out reads 2, then 0, so its rules
     see it true for that pass alone (0x406b50);
  3. the queued rules are tested, newest first; one joins the satisfied
     list when its result changes to true and leaves it when it changes
     to false; as the whole result is compared, a change from one true
     value to another adds it again (0x405770);
  4. the world fires every satisfied rule, newest first (0x405ad0).
     Firing doesn't change a rule: it fires every pass until tested
     false, and the missions' actions make their own conditions false;
  5. each script runs its next step, oldest first, and is gone after its
     last (0x403d10). A rule's action starts a script (`WORLD`'s
     `new_script`, `add_step`); Federation 1's are of one step but one.
- **The cineractive's flags,** `isCineractiveDone` and
  `isCineractiveCancel`, are set after each pass, for the next: done
  once the shot's moves are, and each `CineractiveShot` puts done back
  to 0 (0x4ecec0) *(from the code)*.
- **The cut scenes** ("cineractives"), from the program's camera
  manager (0x661588) *(from the code)*:
  - `CineractiveStart` takes the input (mode 2): the interface is
    hidden, but for the fade; the world runs on at real speed, whatever
    the game speed; the music stops. `CineractiveEnd` gives it back and
    puts every unit's speeds back to its definition's.
  - Four controllers: the eye, what it looks at, and an offset to each.
    Each is a fixed point, a unit followed (its origin and heading, each
    frame, without lag), or a curve along a path: a Hermite curve through
    every point, each segment a second at speed 1 whatever its length,
    stopping at the last point (0x4ee520). An offset's path starts at its
    second point and is less its first, a pivot; with its flag, it's
    turned into the eye's own axes (0x4ed930).
  - The camera each frame: the eye plus its offset, looking at the
    target plus its offset; it never rolls, up from the world's (0x5534f0);
    the field of view and the far plane are play's: `ART_CFG.h`'s
    `FAR_CLIPPING_PLANE`, 1400, so nothing whose bounding sphere is wholly
    past 1400 along the view is drawn (0x582570). The background and the
    starfield are drawn out to `STAR_FAR_CLIP_PLANE` (80000), the grid to
    `GRID_FAR_CLIP_PLANE` (10000), and the planets and suns with them: in
    the original's draws of the opening's first shot, Barisa's vertices
    are 1,480 to 1,815 deep under a far plane of about 80,000, as are the
    sun's sprite (2,725) and the planet's glow (1,908); the rest of that
    frame is the background and the title's letters *(confirmed)*. The `DETAIL_<n>_` clipping planes
    of `ART_CFG.h` aren't read. A nebula is its model's sprite nodes,
    drawn added on, as any object's: the map's at 3,000 to 7,000 from the
    opening's camera aren't drawn, as the original's frames show none. A shot cuts at once, and during a cut
    scene puts both offsets back to none, so a script sets the offset
    after it.
  - The shots by their arguments: two units, the eye following the first
    looking at the second; a path's point and a unit; two paths' points;
    along a path at a speed, at a path's point; a path's point, looking
    along a path. Shots along a heading, and a tracking shot, aren't used
    by Federation 1.
  - `isCineractiveDone` is all four controllers done: a fixed or
    followed one at once, a path at its end; `isCineractiveCancel` is
    every human player having pressed Space (`switch_camera_shot`).
  - `FadeOut` and `FadeIn` (seconds, delay, colour): after the delay the
    screen goes to the colour, or from it, over the seconds, straight;
    the colour black, or white if the third isn't 0. A fade out stays
    until a fade in. The fade is drawn only during a cut scene.
- **Titles and subtitles** (`ShowTitle`, `ShowSubtitle`, `RemoveTitle`;
  0x437190, 0x494330, 0x4a5090) *(from the code)*: boxes in one list,
  their ids from one count, drawn only during a cut scene, under its
  fade. A title is blue (0, 0.5, 1) in `title.spr`, the font sheet
  `font256.tga` at twice the size; a subtitle white in the default font.
  The text, lines broken only at `\n`, is centred in the box less a pixel
  each way as the default font measures it, so a title sits right of
  centre: "'PREMONITIONS'" in (0, 0, 200, 70) from x 65 to 185, as the
  original's frames show it. One shown fading comes in over a second,
  one removed fading goes out over a second, darkening; removed without,
  it's gone at once. Their text passes through the strings file, and is
  shown as given if it isn't a key there.
- **Voice messages** (`AudioMessage`; 0x505620, 0x59e730) *(from the
  code)*: the file found by its name wherever the game's index has it
  (the cut scenes' voices in `sounds/voiceover/cineractive`, music in
  `sounds/music`), streamed once at the voice volume, any number at
  once, the music left as it is. Its id reads done
  (`IsAudioMessageDone`) once it has ended or been stopped, or at once
  if it couldn't be played.
- **The objectives box** (`ObjectivesDisplay_...`; 0x499bf0, 0x49a6b0)
  *(from the code)*: `gui_glob.cfg`'s "Objectives Pop-Up", a panel of 27
  pieces at (128, 32), 384 × 288, its text from a file of `objtxt.zfs`
  (`F1OB1.txt`: plain lines, the title centred by spaces) drawn from the
  top left of its area in orange (1, 0.47, 0) in the default font, and
  an OK button that puts it away. The script shows and hides it; the
  game menu's restate button shows it again. Never drawn during a cut
  scene. While it's up in a campaign the world stands still; the
  interface and sound run on.
- **The music in play** (`JukeBox`, 0x6bb1d0) *(from the code)*: the
  player's race's `track0`, `track1`... at random, never the same twice
  running, at the music volume; a cut scene stops it, and its end starts
  another.
- **The bridge** (0x648180, drawn in a cut scene under its titles and
  fade, 0x48f9b0) *(from the code)*: `SetBridgeSceneRace` picks its race,
  1 to 4 the Federation's, Klingons', Romulans' and Borg's (`f_`, `k_`,
  `r_`, `b_` in `gui_glob.cfg`), another a default (`d_`); each a
  picture over the whole screen, in six tiles, its view screen clear:
  400 × 200 at (220, 10) for the Federation's. Over it seven console
  lights, flipbooks of `TEX_ANIM_gui.SPR` (its frames over its seconds,
  stepped), begun again by `ShowBridgeScene`. While the bridge is up the
  world isn't drawn whole: only into the view screen, unless
  `ShowBridgeOnScreen` gives the screen to the movie.
- **The bridge's movie** (`PlayBridgeMovie`, 0x47a5f0) *(from the
  code)*: a Bink of `animations/`, in the view screen, its own sound at
  the voice volume, at its own rate, played its count of times (0 or
  under, until `StopMovie`); one already open, nothing. Stopped, its last
  frame stays while the bridge and its screen are up. Federation 1 shows
  Deming's call (`F1DEM1.bik`, 9.3 seconds) after the Premonition's cut
  scene, and `F1DEM2.bik` (50 seconds) before the end.
- **Saving:** the script registers a function to write and one to read
  its variables in the program's `save_scripts` and `load_scripts`
  lists; running scripts aren't saved, and loading clears them
  (0x403540, 0x403390) *(from the code)*.
- **Calls:** the names are Visual C++'s decorated ones, which give each
  function's arguments and how it's called: `@@YA`, a function the
  caller pops the arguments of (cdecl); `@@QAE` and `@@UAE`, a member,
  `this` in ECX, popping its own (thiscall); `@@3`, a variable.

## In Reluva

- **`MissionScripts`** loads the pair into an emulated process
  (`Armada.exe`, 64 MB), the `S` DLL first, and runs their start-up.
  Each Armada.exe import is stood in for by what its decorated name
  says of its arguments: a function goes to the `game` callback by its
  plain name (`GetHandle`, `hot_int::set`), its arguments read as the
  script passed them; a variable gets room of its own. Their C
  runtime's thread-local storage is a table of slots: they run on one
  thread.
- **The rule engine** is Reluva's, as above: the variables, timers,
  rules and scripts kept by the address the scripts know them by, their
  values written where the program keeps them too. `tick` runs a pass;
  `setCineractive` sets the flags for the next. A script started during
  a pass waits for the next to run its first step, a choice of Reluva's:
  the program's walk by index would shift.
- **In play** (`MissionView`, the game functions in
  `play/script_calls.cpp`): a campaign mission's objects on a team are
  its units, the player's team 1, and its scripts run a pass a frame,
  before the computer players think. A unit's handle is its place in
  play, from 1; the map's other objects (its planets) have handles past
  0x100000, for their names. Done as the notes on the program's code
  say *(from the code)* unless marked:
  - **names and paths:** `GetHandle` finds the first unit, then map
    object, of the map's name, exactly; paths are the map's, by name;
  - **orders,** only to a unit the script has taken
    (`SetSpecialForcesFlag`), which the computer player then leaves
    alone *(guessed)*: `Goto` a path, along its points *(inferred)*,
    or a unit, through it if it's a wormhole
    ([wormholes.md](wormholes.md)); `Stop`; `Attack` and `AI_Attack`, the same here
    *(guessed)*, how the second differs not known. `SetPosition` puts
    a unit at a path's first point, `PointAt` turns it, `BuildObject`
    makes one at a path's point for a player, `RemoveObject` takes one
    away without an explosion *(inferred)*;
  - **the unit's state:** `CraftCannotDie` keeps it from being
    destroyed; `DisableWeapons`, `DisableEngines` and
    `DisableShieldGenerator` put the system out until turned on again;
    `DestroyWeapons` and `DestroyLifeSupport` take its hit points;
    `DestroyCritical` destroys it *(inferred)*; `SetShieldPercent` takes a
    share from 0 to 1, whatever its name; `SetDilithium` sets the
    player's; `DisableAllAITeams` stops the computer players;
  - **questions:** `IsValid` is whether the handle isn't 0, not whether
    the unit lives; `CountUnits` and `CountUnitsNearObject` count living
    units that aren't derelict, by player and definition, within the
    radius across the plane; `GetDistance` to a path's point and
    `GetNearestEnemy` (in a square of the range) are across the plane
    too; `InAttackMode` is having a target; `InQueue` being in a
    wormhole's queue, going in or coming out; `LinkWormHoles` pairs two
    wormholes; `IsCompromised` gone, derelict or another's;
  - **the end:** `SucceedMission` and `FailMission` end the mission when
    the game time reaches the time they give.

  - **the grid,** shown or not by `GridVisible`, as the player's grid
    key does; `SetWarpSpeed` sets a ship's top speed, its definition's
    `warpSpeed`, and `CineractiveEnd` puts it back;
  - **the cut scenes** as above (`Cineractive`, `play/cineractive.h`),
    run after the rules each frame, their flags for the next; Space
    skips one, and Esc still opens the menu;
  - **titles, subtitles, voices and the objectives box** as above, the
    voices timed by their length, so a run without sound finishes them
    when the original would; clicks on the box go to it alone.

  - **the player's counters** from the map's saved ones
    ([formats/bzn.md](../formats/bzn.md#after-the-objects)): the
    original's "Premonitions" starts its player with 3,500 dilithium
    (then the script's 6,000), 4,000 crew, and 19 of 400 officers, its
    memory showed, and so does Reluva's;
  - **the music in play** as above, the race's tracks from its file
    (`fed.odf`'s `mfed300.wav`, `mfed115.wav`, `mfed100.wav`), on at the
    third frame unless a cut scene is; the shell's music stops as the
    mission starts.

  - **the game menu** a campaign's, as the original builds it
    (0x5374fc-0x537880): Restate Objectives at y 72, which closes it and
    shows the objectives box again, Save Game at 112 (not during a cut
    scene) and Load Game at 152, then the settings, Abort Mission and
    Return to Game, without a skirmish's Surrender. Save Game and Load
    Game do nothing yet.

  - **the bridge** as above, its world view through `MapScene`'s inset.

  - **a bridge movie** still playing after its cut scene is skipped
    without `StopMovie`, as Federation 1's skip does, is drawn over play
    in its view screen's place until it ends, under the interface, as the
    original's player draws an open movie in every mode (0x47a410).

  - **the minimap's messages** (`MinimapMessage`): a white ring at the
    unit or the path's point, `n` pixels about it, one pixel wider as
    the program draws it, a circle of 20 lines kept to the minimap,
    shrinking a pixel each `f` seconds till it's gone (0x481c90,
    0x483050); its colour, white, set at 0x575240. Federation 1's, (5,
    0.2) every 2 seconds, shows a second of each two. The game's own
    rings for its alerts (`events.odf`'s `Marker`) aren't made yet.
  - **`ObstacleAvoidence(on)`** sets whether ships steer round each
    other, which the program's steering (0x42d570) tests first: off from
    the start of Federation 1's opening and its Premonition scene till
    they end; on unless a script turns it off.

- **The other missions' calls** (`play/campaign_calls.cpp`), from the
  notes on the program's code *(from the code)* unless marked:
  - **players:** `SetTeamCrew`, `AddCrew`, `SetOfficers`, `AddOfficers`
    (to the officers' limit) and `AddDilithium` set or add to a player's
    counters and give back the new value; `SetRelation(player, other,
    stance)` sets one side's stance, 0 enemy, 1 neutral, 2 ally, and
    two players are at war only while the lower of their two stances
    is enemy, which units' choice of targets, the computer players and
    `GetNearestEnemy` follow; with none set, every other player is an
    enemy;
  - **computer players:** `AI_Team_Load_AIP(team, file)` turns its AI on
    with the `.aip`'s personality; `EnableTeam` and `DisableTeam` turn
    one on and off; `SetIsAITeam` makes a player a computer one, or not;
  - **units:** `GetRealTeam`, `SetRealTeam` (a unit given to another
    player, in its colour) and `SetPerceivedTeam` (the player it looks
    like); `IsAlive`, `Exists`, `DoesNotExist`; `IsDerelict`, true for a
    bad handle; `GetShieldPercent` a share from 0 to 1 and -33 for a bad
    handle, `GetRealShieldPercent` the shields a disruptor holds back
    when they're down; `SetMaxShields` the most and the shields both;
    `SetSpecialEnergy`, `GetCrew` (-1 for a bad handle), `SetCrew`
    (within its most; a unit that can't die keeps 10 when it'd have
    none); `DestroyShieldGenerator`, `DestroyEngines` and
    `DestroySensors`; `SetImpulseSpeed`, and `ResetWarpSpeed` undoing
    `SetWarpSpeed`; `UnderAttack(unit, seconds)`, hit by another
    player's within them; `GetDilithiumOnFreighter`;
    `IsThereARomulanSpyOnShip`; `GetAttackLocation`, the attacker's rim
    towards the target, returned in the place the script passes first;
    `InStopMode` going nowhere and fighting nothing, and
    `OutOfSpecialAttackMode` having an order, both *(inferred)*: the
    program reads the unit's current order, which Reluva doesn't keep;
  - **orders:** `RecruitSpecialForces` takes the first idle unit of the
    player's of that definition for the script and writes its handle;
    `StopAllShips` stops those its filters leave; `Patrol` flies a path
    round and round *(inferred)*; `Build` puts the item in the unit's
    queue *(inferred)*; `Special_Attack` fires the unit's last special
    weapon of that definition, matched on 8 letters, at the target;
    `Cloak`, `ImmediateCloak` and `Decloak` press its cloak's button;
    `SearchAndDestroy` sends it after the nearest unit it's at war with
    and `AttackMainEnemyBase` after the nearest such starbase
    *(guessed)*; `Mine` sets a freighter working for its nearest mining
    station; `RemoveAllCraft`, `RemoveAllObjects` and `ReplaceObject`
    (the new unit in the old one's place, for its player, nothing else
    kept);
  - **finding and counting:** `CountUnitsNearPoint`, `CountVisibleUnits`
    (the one seeing first), `CountVisibleUnitsNearObject` (seen by the
    unit's own player), `IsVisibleToTeam`, and `GetNearestObjectNear
    Position` and `...NearMe`, across the plane, units and the map's
    objects, a radius under 0 no limit. The computer players' sight isn't
    kept, so they're taken to see everything, a choice of Reluva's own;
  - **the people aboard ships:** `CreateSpecialFlag` puts a named
    person on a unit and gives its id; `TransferFlag` moves one carried
    to another unit, whatever its restrictions; `RestrictSpecialFlag`
    and `ClearRestrictions` keep its bits and lists; `GetLocationOfFlag`,
    `GetRealTeamOfFlag` and `GetPercievedTeamOfFlag` its carrier, its
    player, or the player it looks like (-1 for none);
  - **attacks:** `Attack` and `AI_Attack` to a point (a `Vector3`
    passed whole, three words) send the unit there, fighting what it
    meets *(inferred)*; `AI_Attack` with no target sends it after the
    nearest unit it's at war with *(guessed)*;
  - **`SetPosition` on a map object** moves it, drawn and on the
    minimap: Federation 5 moves its nebulae about every 3 seconds;
  - **`Special_Attack` at a point** jumps a transwarp there (Finale 6's
    escape); `GetLocation(path, point)` gives a path's point;
  - **the decoration:** a function returning a class by value
    (`GetAttackLocation`'s `Vector3`) is given the place for it ahead of
    its arguments, and gives it back;
    a point passed whole takes three words, and the arguments after it
    are counted on past them;
  - **the world:** `PauseSimulation` holds the world still while the
    scripts and the camera run on, `UnpauseSimulation` lets it go;
    `CenterCamera` on a path's point, or the map's middle without one;
    `SetWormholeTeam` keeps a wormhole for one player's ships;
  - **the rule engine's `hot_float`:** from -1, changed only by a value
    that differs, a NaN never stored; the program's own
    `strength_team_1` to `8`, each player's units' most shields added up
    every 200th pass, from 0.

  - **an ally's attacks:** `GetAllyShipAttacks(p, q)` and
    `GetAllyStationAttacks` count the attack orders player q gave on
    player p's ships, or stations, while p doesn't take q for an enemy,
    at most once a second on ships and each 0.05 seconds on stations
    (0x426092, 0x446210); the computer's own attack order (`AI_Attack`)
    isn't counted;
  - **the transporter's glow:** `AddTransporterEffect` puts
    `xtransport` over the unit till `RemoveBillboardEffect` takes it off:
    `weapon.spr`'s 4 × 4 sheet, 32 frames a second round and round,
    added on, a square facing the camera its radius each side, moved its
    radius towards the camera (0x469be0, 0x5829c0) *(from the code)*.
    A unit sending a boarding party, and the one it's sending to, show it
    too while the transport lasts (0x463630);
  - **alert levels:** `SetRedAlert`, `SetYellowAlert` and
    `SetGreenAlert` set the unit's ([combat.md](combat.md));
  - **transwarp gates:** `GetTranswarpExitWormhole(gate)` gives the far
    end of the gate's open conduit, none while it's shut;
    `GetNearestTranswarpGate` the nearest unit whose definition says
    `isTranswarpGate`;
  - **a unit exploding:** Reluva takes a unit away at once when it's
    destroyed, where the program lets it explode first; the scripts see
    it as exploding for 2 seconds after *(guessed)*: `IsExploding` and
    `Exists` hold, `IsAlive` doesn't, and a person aboard is still there,
    lost with it after (0x5597d0). Romulan 2 notices Oldan's death so;
  - **the sky:** `Starfield_Load_Background_Geometry` swaps the model
    drawn behind everything (`MbgBorg.sod`, `Mbgomega.sod`), as Borg 1's
    scenes move from one system to another.

  - **the eye-only shots** (`CineractiveShot(unit, flag)`, `(path,
    speed, flag)`, `(path, point, flag)`): the camera looks along the
    eye's own way, the unit's heading, the path's tangent or +z, or
    along the camera offset's path with the flag (0x4ec400, 0x4ec990,
    0x4ebc50); none of the campaign's missions uses them.

  The program never hands a person aboard to another ship of itself:
  only the script's `TransferFlag` does, or the player's Special
  Transport order, which its own tooltip calls obsolete (0x425a07,
  0x46d8e0) *(from the code)*.

  Not done yet: saving and loading a mission; the tracking shot
  (`CineractiveShotTrackLeft`, unused by the missions);
  `Load_Action_Scripts` and `Add_Team_To_Group` (a skirmish AI's script
  borrowed by Romulan 1); the credits.
- **`reluva-tool storm3d mission-scripts <game folder> <mission> [-run
  seconds]`** loads a mission's scripts, runs them 30 passes a second
  with each shot taken as done at once, and lists each call they make
  of the game, with its arguments.

## Sources and checks

- **Where the facts come from:** the DLLs' import and export tables,
  their disassembly (the `R` DLL's rule set-up at 0x100025a0 in
  `Federation1R.drl`), and Armada.exe's exports.
- **How it was checked:** `mission-scripts` on Federation 1: both
  DLLs start without a fault, the calls listed above; run, the opening
  cineractive's calls (`CineractiveStart`, `FadeIn`, the shot
  `ENTFLYBY`, `AudioMessage("Mfed25.wav")`) follow, and with no units
  (every `GetHandle` 0) the rule for a lost construction ship fires.
  `engine-shot` starting "Premonitions" from the single-player screen:
  the opening's calls in order, the ships flown to and then put at
  their `...START` paths as in the original's first frame of play, the
  title "'PREMONITIONS'" and the Dominion patrols sent off; the
  dilithium 6,000, as the script sets it. The opening runs as long as the
  original's, about 57 seconds of the script's clock: Picard's log
  (`F1PIC1.wav`, 26.8 seconds) over the pan round the Enterprise, which
  the rule ending it waits on, then the fade, the zoom-out and play;
  frame for frame the same shots at the same moments as the original's
  run with DirectSound's position notifications. Before those were made
  in the emulator, the original's voices all ended at once and its
  opening ran 27 seconds.
- **Federation 1 played through** with `engine-shot`, the player's
  clicks given: the starbase and mining station built, the Dominion's
  reinforcements and the second objectives; the Premonition's scene,
  the Premonition and the Borg brought through the temporal gates
  ([wormholes.md](wormholes.md)); Deming's call on the bridge and the
  third objectives. Left alone, the Borg destroy the Premonition and
  the mission is lost ("THE PREMONITION HAS BEEN DESTROYED", then
  `FailMission`); with the Enterprise and its two escorts sent to it,
  the Borg are beaten, the second bridge movie (`F1DEM2.bik`) plays,
  and `SucceedMission` brings up the debriefing.

## Open questions

- What `AI_Attack`'s order 7 does that `Attack`'s 5 doesn't, and how
  `Goto` flies a path.
- The team colours of a campaign mission's other sides.
- What sets a script's hold flag (its 0x18), which stops its steps.
