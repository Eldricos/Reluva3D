# The Fallen: effects

> **Status:** partial · **Code:** `src/engines/unreal/vm/vm_effects.cpp`, `vm/vm_effect_physics.cpp` · **Checked against:** the scripts, and recordings for some · **Confidence:** inferred unless marked

## Overview

The effects: particles, sparks, electric arcs, beams, rain and snow,
decals. Each kind of effect moves its particles.

## How it works

The Fallen draws most effects as copies of one actor, made by its scripts
(`CTVE_MultiMesh`: `NewMultiMesh` puts one on the actor's `MultiMesh`
list, `Delete` moves it to the list's end as spare). `vm_effects.cpp`
moves them and works the effects' natives. All of this is Reluva's
reading of what the scripts set and expect; the original code isn't
known.

- **Copies**: each tick, as the actor's physics mode has them (below).
  Most move by their `Velocity`, which their `Acceleration` changes,
  and are let go when their `LifeSpan` runs out (none: they last). An actor with a copy list is drawn as its copies only (even none
  yet), unless it asks to be drawn as well (`bMultiMeshRenderInitial`).
  Each copy is drawn as the actor's mesh or sprite, in its place, with
  its rotation, `DrawScale`, `AxisScale`, `ScaleGlow`, style and
  texture. `AxisScale` goes along the mesh's axes before its
  `RotOrigin` turns them, as a mesh's own Scale does: the transporter's
  strips (`CTV_Quad`, 800 long in X and turned a quarter by RotOrigin)
  are narrowed to a tenth of their length and grown upwards. An
  imported mesh's turn into the game's axes comes before it: the
  scripts scale their beams' sections along X as they're drawn.
- **A mesh with no skin of its own** (no MultiSkins, Skin, or texture
  in the mesh) takes the actor's `Texture`, as UE1 does: the
  transporter's strips (`Texture = StripTexture`), impact flares.
  Before, they were drawn plain white: big white slabs over a beam-down,
  a white square where a rifle shot hit.
- **Copies by physics mode** (`moveMultiMeshes`), as Engine.dll's
  `phys*` functions for The Fallen's modes do it (read November 2026).
  Each walks the copies from the base's `Next` itself. Most fade only
  in a copy's last second (or half second) of `LifeSpan`, not over its
  whole life. Some count a copy's life down only while it has one;
  the explosions', the fire's and the plasma jet's count it down
  always, so a copy given none is gone on its first tick.
  - Left alone: beams (`BeamSustain`), whose sections their scripts lay
    out with `UpdateBeamSections`, which works only while the beam is in
    that mode (0x10379196): once firing stops and the scripts set it to
    none, the sections stay as they were.
  - Spark (an arc's segments): laid on the arc's curve again when it's
    updated (the base copy holds it, below), then every tick each jolted
    a random way by its `Use1` (the two ends `Use2`), as bright as the
    arc, turned to the one before and stretched to it; `TipLocation` the
    last.
  - Particle: pushed by the generator's `Forces` and `WindForce`
    (whatever `bIsWindy` says, and on top of the `Acceleration` its
    scripts set, which `CTVE_Generator.Generate` makes `Forces`: so twice
    `Forces`, as physSmoke too adds the copy's `Acceleration` and the
    generator's `Forces` and wind), slowed by `DeltaSpeed`, growing by `DeltaSize`; in
    its last second shrinking (`bFadeSize`) and fading to nothing from
    `MaxScaleGlow` (`bFadeGlow`). Without a generator both fades.
  - Smoke: pushed by `Forces` and wind, fading in its last second
    (`ScaleGlow` = what's left). It doesn't grow.
  - SteamJet: out and slowing, growing by `DeltaSize`; with a second
    left, blown off (30 x a random way plus the jet's
    `MMEventLocation`, back along it) and fading over half a second
    from twice `MaxScaleGlow`.
  - HotSpark: facing the way it flies, kicked somewhere new 3 ticks in
    10 (500 a random way, falling; 200 out), flickering, fading in its
    last half second.
  - CTVE_Sparks: flickering in size (round its `Phase`, the size it
    was given) and brightness, jittered, dragged across to 100 a second,
    each leaving a trail of embers (a quarter its size, up to 0.6 s,
    Style translucent) every 0.01 to 0.03 s.
  - Bubble: rising at the speed it started with, the drift damped,
    shrinking and dimming in its last second (half bright before). At
    the surface (the generator's `SeedLocation.Z`) it becomes a pop (the
    generator's `SpecialTexture`) for 0.08 s, and the generator's
    `bMMEvent` is raised there; with no pop texture it's just gone.
  - GasCloudM4, BeamFadeTail: a life of a second or more is spent at
    once (the engine counts it down again until it's under a second);
    then size and brightness go down 0.2 a second.
  - FanParticle: along its line (`Phase`, slower in the middle) at the
    generator's `Acceleration.Z`, flaring out toward both ends (`Vect1`
    x (1 + 4 Phase^2) + `Vect2` x Phase), fading in the outer halves.
  - SpiralJet: out along the axis (its `Phase` the distance), round a
    circle widening with it at a turn a second, growing; fading in its
    last second.
  - PipePath: drawn toward its `Acceleration` (a point), pushed away
    from it at 1.5 to 2 and 3.5 to 4 s left, damped; started again at
    its end (`Vect1`, `Vect2`, `Use1`).
  - SwarmObject: by velocity and acceleration. With no textures it
    faces the way it flies; otherwise its texture steps a frame a tick
    through the generator's `AnimTexture` (the fireflies' 8: frames 0
    to 3, or 4 to 7 with `bUse`).
  - FishInTank: each fish swims at 5 to 30 (at most 10) to a point
    picked every 0.3 to 1.3 s, or half the time follows the fish before
    it; triggered (the base's `bUse`), they go up to the top to feed.
  - Fire_Gas, a fire's puffs (the fire's `DrawScale` S and `ScaleGlow`
    G; its `ColorFilter.X` the chance of smoke, `.Z` 1 for a normal
    fire, else plasma): stages (`Angle`) of `Phase` seconds, each
    setting the growth, brightening, push and colour change. A white
    flash up (0.2 s, 300 G up), yellow to orange or green (0.3 s), then
    smoke (the holder's `Tex[0]`) greying and sinking away (0.5 s each),
    or the flame shrinking out (0.5 s). A fire's `TweenRate`, if set,
    caps the brightness (a dying fire's).
  - EXP_Trail: fire's colour to smoke's over its life; its texture
    picked again each tick from the explosion's `Tex`, `FadeTex` (in the
    last 2 s, by what's left) or `TranslucentTex` (translucent ones).
    EXP_Smoke (growing by its `Phase`; falling faster than 40, drawn
    back up) and EXP_Column (growing by its `Phase`, at least 0.3) the
    same.
  - EXP_Cinder, EXP_Particle: flickering between the base's colours and
    in brightness, curling about their `Vect2` (the base's `Angle`: the
    axis crossed with the way it goes, kept to its speed; the order read
    from the opcodes, `fsubp st(1),st0`),
    each leaving a trail of embers; cinders jostled now and then.
  - PlasmaThrower: drawn onto the thrower's axis until 50 short of what
    it hits (`MMEventLocation`), then bounced off it (`WindDir`, the
    normal) and spread over it; green, growing.
  - Lava: falling until under the generator, then still, shrinking
    away 0.2 a second from the tick it lands.
  - DisintigrationPhaser: sinking at 30 a second, fading 0.5 a second.
  - ExhaustJet: orange to red to pale, pulled back by the jet's own
    `Velocity.Z`, growing and fading a unit a second.
  - EMPulseRifle, EMPulseRifleQuad: brightening to 1, dimming in the
    last half second; the rings grow by the shot's power (the actor's
    `Acceleration.Z`, at most 1) plus a half.
  - WaterRing: drifting, spreading by its `Angle` and fading by its
    `Phase` a second; gone when dark.
  - Weather (rain, snow): by their velocity and acceleration and the
    wind (a third of the weather's `DropVelocity` across; none with
    `bCheckBlocks`), brightening a unit a second up to its
    `MaxScaleGlow`, fading in their last half second.
  - SwarmObject and the rest: by their velocity and acceleration (the
    swarm's scripts steer each by its acceleration).
- **Arcs** (`CTVE_ChainSpark`), as Engine.dll's natives (read November
  2026):
  - `SpawnChain` makes `NumSegments` copies, each with `SpikeMag`
    (`Use1`) and `EndSpikeMag` (`Use2`) to jitter by, `SparkColor`,
    translucent, the arc's `AxisScale` (its `SetSparkThickness`), then
    lays it out.
  - Laying it out (`Update`, `ForceUpdate`): the base copy gets the
    curve, a cubic from `StartLoc` leaving along `startPath` and arriving
    at `FinishLoc` along `FinishPath`, and the step along it from one copy
    to the next (`Growth` over `NumSegments` - 1). With `bSinVariation` a
    wave across it: a random way (`Alter`, `Amplitude` long, across the
    world's Y and Z, as nothing sets `P1`-`P3`), `WaveLengths` x 6.28
    (`SinScale`). The arc goes to its middle.
  - `ChainSparkTick`: grows (`bGrows`, `GrowthRate` a second, then off at
    1), travels (`bTravels`: `StartLoc`, `FinishLoc` by `StartVelocity`,
    `FinishVelocity`), fades (`bFade`, its `LightBrightness` with it; gone
    below 0) or glows up and down (`bCycleFade`, turning at `MaxGlow` and
    `MinGlow`). Growing or travelling sets `bUpdate`; with it, it's laid
    out again. Its physics (Spark) then places the segments.
  - Before, Reluva laid the segments itself each tick, jolting them only
    on updates, so an arc held still between them.
- **Beams** (`UpdateBeamSections`, 0x103790f0): only while sustained
  (Physics 39) with its copies. Nothing is made or removed: the sections
  the scripts made, from `BeamSectionIndexFirst` up to
  `BeamSectionIndexLast`, are laid end to end.
  - Each starts at `BeamOrigin` + k × `BeamSectionAxesX` (k its place in
    the list), turned as `BeamRotation`, and takes
    `BeamSectionColorFilter`.
  - The beam spins about its length at 25600 a second, on from the first
    section's roll.
  - Each is `SectionAxisScale` long, the last too: the scripts cut it
    after, where the beam meets something.
  - Its thickness is `SectionAxisScale` plus a random share of
    `SectionAxisYZRandomFactor` (the same both ways, so only ever
    thicker), times a pulse 1 + `WaveAmplitude` × (1 + sin(k ×
    `WaveLength` × 803.84 + `WaveStartOffset`)) / 2. The pulse travels:
    `WaveStartOffset` goes down by `WaveTravelVelocity` a second. The
    phaser rifle's swells to twice as thick and back about every six
    sections, moving out along the beam.
  - Before, Reluva bent the beam sideways for its wave, a unit at most
    and slowly, sized the last section by how far the head had flown,
    jittered the thickness both ways and didn't spin the beam.
- **Sparks** (`CTVE_Sparks.SparkTick`): when its scripts' `BurstDelay`
  runs out, a burst of `MinSparksPerBurst` to `MaxSparksPerBurst` flies
  out along its X axis at `Speed`. They spread `RangeWidth` and
  `RangeHeight`, fall by the zone's gravity, and are `Size` and last
  `SparkLife` (each give or take its variance), fading as they go. The
  next burst is due in about `AverageBurstTime`, else at random up to
  `MaxWaitTime`. Its light (`saveLightType`) flashes for a tenth of a
  second.
- **Rain and snow** (`CTVE_Weather.WeatherTick`, 0x10349230;
  `InitialSetUp`, 0x103496e0). Its scripts keep `SeedLocation` upwind of
  the player and the weather itself at the player's feet.
  - **How hard:** `Intensity` comes up toward `MaxIntensity` at
    `DeltaIntensityRate` a second while `bActive` (the scripts hold it
    at 0.1), and down when not; at 0 it stops (`bShutDown`).
  - **The wind:** each tick, with a chance of `WindActivity`, a new one
    to turn to (`DestWind`: any way, `MaxLateralMove` across, falling at
    `FallRate`); `DropVelocity` goes straight toward it at
    `WindActivity` x 2500 a second.
  - **The blockers** (`Block`: a box or an upright cylinder) near the
    seed are found every 0.4 s (`CheckBlocks`, 0x10349e50: within half
    the larger spawn range, 1300 then, and `CheckBlockRange` of its
    footprint), and `bPlayerSheltered` set if one is over the player.
  - **New drops:** dt x `Intensity` x 2000 a tick, whole ones (about 200
    a second), none at 0.01 or under (`Generate`, 0x10349910). Each
    starts over a square `SpawnRange` across about the seed,
    `DropHeight` up and up to `DropHeightRange` more, at `DropVelocity`
    (slower by up to `FallRateVariance`), `Size` and up to
    `SizeVariance` more, rolled at random, and at `StartScaleGlow`,
    which nothing sets, so it fades in (its physics brings it up to
    `MaxScaleGlow` at 1 a second). It lasts until it's down to the
    weather's height (the player's feet): the world doesn't stop it.
    With blockers near, one born inside or under one is gone at once and
    one falling onto one stops there; with none, it sways sideways,
    `FallRate` x `Chaos` any way. With `bInvertWeatherBlockers` drops
    fall only inside the blockers near, straight down, and none with
    none near.
  - **The fill** (`InitialSetUp`, when the level starts and when the
    player jumps far) makes `Intensity` x 2000 at once from the
    weather's own place in still air, each moved on along its fall by a
    random share of its life. The drops already there stay.
  - Before, Reluva kept 4000 x `Intensity` alive, started them at full
    brightness, ended them where a trace met the world, and dropped
    those left far behind.
- **Decals** (`Decal.AttachDecal`, `DetachDecal`): stuck to the world
  surface within the trace distance behind them. They lie flat on it,
  their top along the direction given or their rotation's Z, and are
  drawn over it (modulated) with a depth bias.
- `SetSurfaceTexture`: the surface found by a trace takes another
  texture (the whole world's use of that texture, if asked), saved with
  the game. Its `XORPolyFlags` aren't applied.
- `GetWaterLine(Start, out End)`: `End` is brought back to where the line
  from `Start` crosses the water's surface, found by halving. It is
  `Start` if the line doesn't cross it (bubbles rising out of water).
- **Ghosts** (`MakeGhost`, `GhostActors`): an actor set aside, out of the
  level's list (no ticks, no collision, not drawn) until it's brought
  back. Those placed as ghosts start so ([level.md](level.md)).
- `ListActors`: an AI's known actors (`ActorList`), each with its entry.
- `ScriptTick`: one actor's tick on its own while the game's paused (the
  HUD's portrait).
- `ResizeActor`: a new collision size, kept only if there's room for it
  (on the floor or under the ceiling, as asked).
- `PickWallAdjust`: a step aside, left or right, that's clear to the
  goal.
- `GetDialogueCameraData`: the current line's camera from the dialogue
  table, in the world or from the speaker.
- `GetRendererFlags` gives 0. `Level.bHighDetailMode` is set from the
  ini's `LowDetailTextures`, so high detail by default: the effects that
  only run in high detail run.

## In Reluva

`vm/vm_effects.cpp` moves the copies and works the effects' natives;
`vm/vm_effect_physics.cpp` the effects' physics modes
([movement.md](movement.md)).

## Sources and checks

- **Where the facts come from:** each effect mode's function in
  Engine.dll (`AActor::performPhysics`, 0x10358a90, picks it by the
  mode; all of them read November 2026), and the scripts for what sets
  them up.
- **How it was checked:** in play, and some against the recordings
  ([research/playthrough.md](../research/playthrough.md)).

## Open questions

- Not compared side by side with the original running: the swarms,
  lava, water rings, scripted paths.
