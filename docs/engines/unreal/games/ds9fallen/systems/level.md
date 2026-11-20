# The Fallen: a level's actors and ticks

> **Status:** done · **Code:** `src/engines/unreal/vm/vm_level.cpp`, `vm/vm_travel.cpp` · **Checked against:** the code (Engine.dll), and every map run · **Confidence:** from the code unless marked

## Overview

How a level starts, how its actors are made, tick and go, where they
are, and how the player joins it and travels to the next.

## How it works

`ScriptLevel` makes a map's actors from the level's actor list (the
Level object's: a count, then compact references; the first is the
LevelInfo), each with its class's defaults then its saved properties,
their `Level`, `XLevel` and `Outer` set. Then `beginPlay`, in
`UGameEngine::LoadMap`'s order as read from Engine.dll (the code is
Reluva's own):

1. **No events yet.** The engine passes an actor's events to its
   script only once `LevelInfo.bBegunPlay` is set (`AActor::ProcessEvent`),
   so what happens before that is heard by no script (`Vm::holdEvents`).
2. `bHighDetailMode` set. The game type (the LevelInfo's
   `DefaultGameType`, else the .ini's `[Engine.Engine] DefaultGame`:
   `DS9.DS9_GameInfo`) spawned: in silence, so it never gets `Spawned`.
   `TimeSeconds` and `NoPauseTimeSeconds` 0.
3. **The ghosts set aside** before anything starts: each actor placed
   with The Fallen's `bIsGhost` (a guard who arrives later, an effect for
   a scene) goes to the ghost list. Every actor's `Touching` cleared.
4. `bBegunPlay` and `bStartup` set, then the game type's `InitGame`.
5. **Six passes** over every actor, the GameInfo and LevelInfo too,
   each reaching actors spawned during it: `SetInitialState` (its
   initial or auto state); `PreBeginPlay`; `BeginPlay`; its zone (the
   one it's in: `ZoneChange` if that's a ZoneInfo's, nothing to the
   zones); `PostBeginPlay`; its base (with an `AttachTag`, the last actor
   so tagged; else a decoration, item or pawn colliding with the world,
   standing still and with none, takes what's 8 units under it).
6. `bStartup` cleared.

Spawned actors get `SetInitialState`, `Spawned`, `PreBeginPlay`,
`BeginPlay`, their zone, then `PostBeginPlay` (and `bHasBegunPlay`).
Destroyed ones get `Destroyed` (and their owner `LostChild`); `bStatic`
and `bNoDelete` actors refuse to go.

**Ghosts** (`MakeGhost`, `GhostActors`): made a ghost, an actor leaves
the level as if destroyed, without being destroyed: `EndState`, no base
and nothing based on it, what it owns without an owner, `UnTouch` both
ways, its owner's `LostChild`. Brought back, one placed as a ghost
starts then, as if spawned; one that had begun play only gets its zone
and base again. `GhostActors` with no tag, asked to bring them back,
does nothing (the engine refuses to revive a whole class at once).

**A tick** (`ULevel::Tick`, `AActor::Tick`): the time scaled by
`TimeDilation` and counted (`TimeSeconds` unless paused,
`NoPauseTimeSeconds` always), then held within 0.005 to 0.4 seconds as
the actors' `DeltaTime`. Each actor that isn't static, in the level's
order (those spawned during the tick too): its animation (`AnimEnd`,
notifications); the player its input, `PlayerInput` and `PlayerTick`
(no `Tick`), any other its `Tick`; its state code; its timer (at most
once a tick); its `LifeSpan` (at 0, `Expired` and gone, with no
physics); its physics; a player's `UpdateEyeHeight`; a pawn's
`PainTime` counted down (a full tick only), at 0 `PainTimer`. Paused,
the player only reads its input (`PlayerInput`, its axes 0) and only
`bAlwaysTick` actors tick; the dialogue goes on. The Fallen's
generators (sparks, the Jerrado bug swarm: one every 5 ms, each to live
0.4 s) count on the life span: without it, M03_KiraL2's swarm would
make 17,500 actors by 20 seconds in. Any `LifeSpan` but 0 counts down (one
below 0 goes at once); a timer that isn't looping keeps what's over the
rate counted.

- **The owner first.** The level has a flag it flips each tick, each
  actor its own (`bTicked`); one spawned is set to "not yet", one that
  ticks to the level's. An actor whose `Owner` hasn't ticked yet this
  time is put off; once the pass is done, those put off are ticked in
  another (and another, while the passes get anywhere). As the engine
  has it, an owner that never ticks (static, asleep) leaves its flag
  standing, so what it owns ticks only every other frame. (`Base` isn't
  looked at.)
- **The pain timer.** The engine only counts down; the scripts do the
  rest: in water (the head), `PainTime` is `UnderWaterTime`; in a pain
  zone (the feet), 0.01; `PainTimer` drowns (5 `Drowned`, again in 2
  seconds) or does the zone's harm (`DamagePerSec` by how deep, again in
  1). M01_KiraL1B starts Kira in a flooded tunnel, its ceiling 46 over
  her head: at 20 seconds she starts to drown (100, 85, then 5 every 2
  seconds) unless she's swum out.
- **Sleep** (The Fallen's shutdown, before all the rest): an actor not
  seen for 5 seconds is asked `ShutDown`, and asleep (`bForceStasis`)
  has no tick at all, no animation, `Tick`, state code, timer or
  physics, until it's seen in the last 5 seconds again
  (`DisableShutdownForSomeTime` puts `LastRenderTime` forward); one
  that won't sleep is asked again in 2 seconds, and one asleep 6 to 7
  seconds is asked again. Never one with `bMustNotShutDown` (movers,
  dispatchers, the player, effects, projectiles, flashlights...), a
  life span, or in zone 0. "Seen": its own `LastRenderTime`, which
  drawing sets; for one that shows nothing of its own (hidden,
  `bWakeWhenSeeZone`, drawn as nothing but a decal, a mesh or sprite
  with none) when its zone was last drawn (the renderer's zones, the
  sky's too), or now with the view in zone 0. `ShutDown` asks
  `CanIShutYouDown` with `bAskMeBeforeShuttingDown`; with
  `bWakeWhenSeePlayer` (not by its zone) it won't while its zone was
  drawn in the last 5 seconds and nothing of the world is between the
  player and it. With `bDestroyOnShutDown` asleep is gone. Nothing
  sleeps until a renderer has drawn (`unreal run`, which draws nothing);
  `unreal play`'s `view` says how many are asleep (M01_KiraL1A, 194 of its
  actors at 21 seconds), nor while a cut scene is skipped (the engine's
  `bFastForward`, which `FastForward(true)` sets on Escape in one). A
  `MultiMesh` whose list is empty (its `Next` None) counts as showing
  nothing too, whatever it's drawn as; a decal drawn as nothing goes on
  to the other tests.

UnrealEd keeps its viewports' cameras in the level as `Camera` actors
(292 of them over The Fallen's maps). `Camera` is a `PlayerPawn`, and in
single player `GameInfo.Login` takes over any `PlayerPawn` in the level
that has no player (meant for players travelling between levels), so
they're left out; the original must drop them too, or every login would
become an editor camera.

### Where actors are

The engine keeps each actor's `Region` (its zone, BSP leaf and zone
number) as it's spawned and moves; scripts read it
(`Level.Player.Region.Zone.bWaterZone`). `ScriptLevel::regionAt` walks
the level's BSP from the root to the point, taking the zone and leaf on
its side of the last node. Against the regions saved with The Fallen's
placed actors (four maps, 2,808 actors with one): the same zone for all
but 8, the same leaf too for 2,738. Actors saved with no region (the
LevelInfo, brushes: zone 0, leaf -1) aren't in the world, and keep
none.

A point in no ZoneInfo's zone (zone 0, or one with none) is in the
LevelInfo's: it's the default zone, as in UE1. Left as None, the
player's `ViewFlash`, `UnderWaterSounds` and `HandleWalking` read None
150,000 times in all maps' first 20 seconds.

A pawn's feet, chest (The Fallen's `ChestHeight` up its height, 0.65)
and eyes are in zones too: `FootRegion`, `ChestRegion` and
`HeadRegion`, kept as it moves, and `FootZoneChange`, `ChestZoneChange`
and `HeadZoneChange` when one changes (wading, swimming, breath).

### The player logging in

After `beginPlay`, `login` does the engine's side of a player joining,
as `ULevel::SpawnPlayActor` does:

1. The character: The Fallen's starts (`CTVE_PlayerStart`) name it in
   their `Player` property (M01_KiraL1A's: `PLYR.DS9_Kira`). Of the
   starts that name one, enabled or tagged as the portal arrived by:
   with no portal the first, with one the one so tagged, else the last.
   Else the URL's class, else `PLYR.DS9_Sisko`. (The engine doesn't ask
   `FindPlayerStart` for this.)
2. The game type's `Login` with options `?Name=Player?Class=...`: it
   finds the start itself and spawns the pawn there (whose
   `PreBeginPlay` sets The Fallen's `Level.Player`).
3. The engine's own objects for it: a viewport (`Viewport` has no script
   class, it's C++ only: made up as a kind of `Player`, `Vm::defineIntrinsic`,
   so `Viewport(Player)` casts work) holding the .ini's console
   (`UMenu.DS9_Console`).
4. `Possess`; `bAdmin` (playing alone); `TravelPreAccept`; the game
   type's `AcceptInventory`; `TravelPostAccept`; the game type's
   `PostLogin`. With what travelled in (below) put back after the
   player's `TravelPreAccept`: each of those told `TravelPreAccept`,
   last first; then `AcceptInventory`; each `TravelPostAccept`, last
   first; the player's own; `PostLogin`.

**Travelling with the player's things** (`vm_travel.cpp`, as Engine.dll's
travel and `SpawnPlayActor`). `ClientTravel(URL, Type, bItems)` with
`bItems` writes the player's pawn and each actor along its `Inventory`
chain that has `bTravel` as text, a block each (`Class=PLYR.DS9_Kira
Name=DS9_Kira0`, then `{`, a `Property=value` line for each `travel`
property that differs from its class's default, `}`; an actor reference
as the actor's name). On `?RESTART` what the level was entered with goes
again; otherwise nothing. On arrival the pawn's block goes onto the pawn
logged in, whatever class it was, and each other block's class is
spawned where the pawn is, owned by it; their properties put back by
name, an actor reference to the travelled actor that had that name.
The scripts rebuild the inventory (`Inventory.TravelPreAccept`:
`GiveTo`). The engine also carries the player's `GamePlayedTime` over
any travel. In The Fallen only `CTVE_EndLevel` with `bLinksLevelParts`
travels with items (it sets `bPreserveInventory`, a travel property,
and the weapon slot to take out again); `EquipInventory` then keeps
them, else gives the start's. Tried on M04_KiraL1 travelling to itself
with items and `bPreserveInventory`: the phaser rifle (`AmmoType` its
travelled ammo), the tricorder's 5 charges, the flashlight selected all
arrive, and 2 takes the rifle out. `unreal play`'s `travel <url> items`
does such a travel.

On M01_KiraL1A this plays the opening as the scripts have it: Kira's
`InitPlayerStart` gives her the start's weapons and items, spawns her
HUD (`DS9_Hud`), triggers the opening camera director, and freezes and
hides her for the start's 12 seconds; then `CTV_Human`'s `Frozen` timer
beams her in (`CTV_TransporterBeamIn`), which puts her in `PlayerWalking`
about 17 seconds in.

`reluva-tool unreal run <game> <map | -all> [seconds]` runs it and reports
what scripts did: their log (repeats counted), errors, natives not
written yet by calls, None accessed by place, and the player as it ends
up (its state and HUD), and how things moved. On The Fallen (October
2026), all 74 maps, 20 seconds each, in 6 minutes: 185 million calls. A
player logs in on every map, as twelve characters (DS9_Sisko and DS9_Worf
on 15 maps each, DS9_Kira, their EVA, ES, Beaten and Kira's Cardassian
disguise on the rest), all with their HUD but on Entry (the menu's level,
whose start isn't The Fallen's). 20 seconds in:
- 65 are walking.
- 4 are still frozen in their map's opening cut scene; cut scenes
  waiting on `FinishAnim` end when their animations do.
- 4 started in water and swim.
- On CM_Endcredits, the credits, the player falls: there's nothing under
  its start.

The movement: 187,000 moves, 6,837 landings, 8,341 touches.
- 9,908 placings refused: scripts putting things where there's no room,
  mostly SR3_Raptor and DS9_SmallFishSchool on M05_SiskoL1b, again and
  again at one spot.
- 367 walks to a goal given up: SR3_Leech mostly.

`unreal run`'s "drawn as nothing" and "lights at brightness 0" are the
game's own: over all maps, the actors with no DrawType are trigger
lights (489), exploding walls and breaking glass waiting to break,
decals (drawn as decals), the HUD's portrait actors, triggers and sound
actors; the dark lights are TriggerLights that start off (1,300) and
plain Lights placed with brightness 0 (226: TrainingRoom's, the
Promenade's and Ops' 30 each).

**Placed ghosts.** An actor saved in the map with The Fallen's
`bIsGhost` is set aside once the level has started it (its
`PreBeginPlay` to `SetInitialState`: a friendly AI takes its phaser
there), out of the level's list until an event brings it back through
`GhostActors`, which every `Trigger` and player start sends its event
through as well as to the live actors. Over all maps that's 1,306
actors: mostly the enemies of later waves (179 Jem'Hadar, 126 Bajoran
fanatics, 90 Grigari drones...), creatures, and a scene's lights,
sounds, music and speech. Left in, they would play from the start: on
M01_WorfL1 an ensign who arrives in a later Grigari attack would be
fighting already, with no phaser. Set aside, an all-maps run makes 178
million calls, and None is accessed 3,860 times.

**What None accessed is left** (`unreal run` lists it by place in the
code, the context expression's offset as `unreal script` lists it). It all
reads as the game's own:
- `CTV_AiPawn.HearNoise` and `DS9_FriendlyAI.HearNoise` (about 900 of
  each place): `Instigator.Owner.IsA('HUD')` with no `None` test, when
  the noise's maker has no owner, as a pawn placed or spawned in the
  level hasn't (here as in UE1).
- `CTVE_PlayerActivate.AutomaticActivate` (841): `Level.Player`, which
  the player's own script sets once it has joined, read when a mover's
  `BeginPlay` touches the trigger, before any player has.
- `CTVE_PlayerPawn.PlayerInput` (600): `CTVE_Hud(myHUD).IsLetterBoxOn()`
  before the player's HUD is made, in its first ticks. The HUD is made
  by `InitPlayerStart` or the first `PreRender`; `unreal run` draws no
  frames, so it's a few ticks later there than in play.

What's left before play, by what `unreal run` reports:

- **Natives not written**: none that a map calls in its first seconds.
  The Fallen's own physics modes are Reluva's reading of the scripts
  (see [movement.md](movement.md) and [effects.md](effects.md)).
- **Speed**: the biggest map, M03_KiraL2 (4,263 actors), runs 20 seconds
  of play in about 5.7 s without drawing; all 74 maps take 371 s
  together, the slowest (M10_WorfL1A) 11.5 (October 2026; November's
  figures, and the rest of what's done for speed, are under Speed in
  [playing.md](playing.md#in-reluva)). Among what's done for it:
  - Only actors that may be seen are looked at to draw: not hidden, in a
    zone seen through the portals (`LevelPlay::mayBeSeen`). The rest
    aren't drawn, so they don't get `C3DPreRender` or `LastRenderTime`
    either, as in UE1 (whose scripts read `LastRenderTime` to know
    they're seen).
  - A script function that only returns, with nothing to give back
    (Actor's `C3DPreRender` and other empty events), isn't run
    (`VmFunction::doesNothing`).
  - `Value`'s readings of numbers and objects are inline; what an actor
    looks like and its ambient sound are read through each class's
    slots, not by name.
  - A character posed each frame is uploaded with its triangles' layout
    worked out once (`MeshTopology`), only its vertices new.
  - Lightmaps that changing lights reach are redrawn only while their
    zone is seen (once seen again if they changed meanwhile), and a light
    effect's pattern takes what it needs of the time once a frame
    (`PatternNow`) and of each texel once at load (`PatternPoint`).
- **The game's own mistakes**: `DS9_GrigariSentryDrone` (and its kin)
  `goto 'DetermineAction'` in their `DefensiveStance` state, where it's
  a function, not a label, `DS9_Grigari` `goto 'CheckCloak'` in
  `MeleeAttack` and `DS9_FriendlyAI` `goto 'AlarmRunner'` in
  `Acquisition`, which have no such labels (reported, and the code
  carries on); `CTV_ElevatorMover.InitRamp` and `CTV_TriggerLight.Tick`
  divide by an elevator's move time or a light's change time, 0 in some
  (floats divide to infinity, as they do: the light's fade then ends at
  once; reported). `DS9_JerradoBugStream.SetUpMultiMesh` divides by its
  owner's speed, 0 when it stands still (each piece of the swarm made
  then: reported). `SR3_Raptor` `goto 'Wait'` in its `Dying` state,
  which has only `Begin` and `FinishedDeath` (a `GotoState('Wait')`
  meant, it may be; reported).

## In Reluva

### In the Maps page

Loading a map runs its start, the player's login and a second of play
(`runLevelStart`),
then compares each placed actor with how it was saved: destroyed,
hidden or shown, drawn another way, its light's type or brightness. The
lightmaps, sprites and meshes use the result: lights the scripts turned
off light nothing (trigger lights not initially on, which the original
starts dark), editor icons hide themselves (`CTV_TriggerLight` sets
`DrawType` to none), actors hidden until something happens stay
hidden. The info shows what changed; P turns the scripts off (the level
as saved). On M01_KiraL1A: 64 lights changed (37 triggered lights off),
10 Bajoran fanatics hidden until their ambush, 9 exploding walls' icons
gone.

## Sources and checks

- **Where the facts come from:** Engine.dll's `UGameEngine::LoadMap`,
  `ULevel::Tick`, `AActor::Tick`, `AActor::ProcessEvent`,
  `ULevel::SpawnPlayActor` and its travel, read for what they do; The
  Fallen's scripts.
- **How it was checked:** `reluva-tool unreal run` over all 74 maps
  (above); the regions against those saved with the placed actors.
