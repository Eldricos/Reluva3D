# The Fallen: the AI's paths and hearing

> **Status:** partial · **Code:** `src/engines/unreal/vm/vm_paths.cpp`, `vm/vm_level.cpp` · **Checked against:** the code (Engine.dll), and every map run · **Confidence:** from the code unless marked

## Overview

How the AI finds its way along the level's paths, and hears noises.

## How it works

### The AI's paths

`vm_paths.cpp` finds the AI its way across the level's navigation points
(path nodes, patrol points, cover spots...). This is Reluva's own
search over the paths the editor stored with the map.
- **The paths:** the Level object holds, after its actor list, its URL
  and its model, a reach list: for each reach, its length, its start and
  end points, the biggest collision radius and height that fit along
  it, how it's travelled (walking 1, flying 2, swimming 4, jumping 8,
  doors 16, special 32, the player only 64) and whether it was pruned.
  Each point's `Paths[16]` are the reaches from it. Checked: on every map
  the reaches resolve to the level's points and each starts at the point
  listing it; all of The Fallen's are walking ones (M05_KiraL1: 348
  points, 2293 reaches, 817 listed).
- **The networks are islands.** On M03_SiskoL1A the biggest set of
  points that reach each other is 33 of 79, and on M05_KiraL1 247 of
  348: rooms joined by doors and lifts, ledges with no way down (there
  are no jumps or drops among the reaches). Ladders are the player's
  alone.
- **Matched against Engine.dll** (`APawn::findPathToward`,
  `breadthPathFrom`, `pointReachable`, `actorReachable`,
  `LineOfSightTo`, the path iterators): Reluva's own code, written from
  what the routines were read to do.
- **The order of the points** is the level's: LevelInfo's
  `NavigationPointList` and each point's `nextNavigationPoint`, as saved
  with the map; the iterators and the searches follow it, so ties go the
  same way.
- **Reaching a place** (`pointReachable`, and what the search asks):
  within 800 across, not water the pawn can't swim, seen from its eyes
  (`BaseEyeHeight`) unless already known, the place nudged to where the
  pawn fits. Then a walk test: steps of the collision radius (at least
  128) along the floor, each a test move (as `MoveActor`, without events)
  up a step when blocked and down onto a floor (0.7 or flatter) within
  `MaxStepHeight` + 2; a fall, too steep a climb or 101 steps and it
  can't. Flyers and swimmers test a straight move. The pawn is put back.
- **Reaching an actor** (`actorReachable`): The Fallen's native takes a
  navigation point (or an item's marker) by the network alone when the
  pawn is at a point and fits the level's biggest; else the engine's
  test: within 800 (a pawn at any range), seen unless known, near enough
  by both sizes.
- **`FindPathToward(Actor, bSinglePath, bClearPaths)`, `FindPathTo(Vector,
  ...)`:** `bClearPaths` (on unless told otherwise) first sets each
  point's cost back (its `ExtraCost`, or what its `SpecialCost` event
  says). Then **one** start point: of the nearest 32 within 800, the
  nearest the pawn sees and can reach (or the one it stands on); a
  falling pawn looks from where it will land. **One** end point: of
  those near the goal, the nearest it can be reached from. The search
  goes back from the end over each point's `upstreamPaths` (the reaches
  into it) the pawn fits (its size in whole units; walking, flying,
  swimming, jumping, doors, special and player-only against its
  abilities) and a mover doesn't bar (one it can't open), cheapest first,
  each step costing its length and the point's cost, until a start is
  reached. A start further along (nearer the goal, or behind the pawn
  against the way it was going) is preferred over the nearest. The way
  goes in `RouteCache`; a point with its own handling (a lift, a door)
  names where to go instead (`SpecialHandling`, the pawn's
  `SpecialGoal`). None if there's no way.
- **The iterators:** `AllPathNodes` (by class and tag, in the level's
  order); `RadiusPathNodes` and `RadiusVisiblePathNodes`: of the **8**
  nearest points within the radius (the engine keeps one nearest-points
  list for everyone, reused while asked near where it was made), nearest
  first, those of the class, and for the visible one those that can see
  the place.
- **`LineOfSightTo(Other)`:** from the eyes. Its enemy: its middle.
  Within 1000: its chest (0.8 of its height up) and, for a pawn within
  500, its sides. Further, a pawn's middle only, and for the AI only
  half the time and not past half its sight range.
- **In play:** over all maps' first 20 seconds, 1,620 ways were straight, 2,598 along paths and 1,460 none. Many of the
  failures are faithful: on M05_WorfL2 the patrol points a guard is sent
  to are an island in the map's own data (no reach in or out either
  way), so the original fails there too.
- **Not done:** jumping and dropping reaches (none of The Fallen's maps
  has them), and the editor's path building (the maps' own reaches are
  used).

### Noise

The AI hears (`ScriptLevel::makeNoise`). A noise is `MakeNoise(Loudness)`
or a sound played without `bNoHearNoise` (as loud as its volume). Only a
pawn's noise is heard: its own, its weapon's or shot's, or what it set
off (its `Instigator`), as UE1's `MakeNoise` asks; the level's ambient
emitters aren't (they kept knocking guards out of their states). Each
other pawn hears it if Loudness x 800 over the distance (halved with the
world between) reaches its `HearingThreshold` (1 by default: a noise of
loudness 1 carries 800 units in the open). Then its `HearNoise(Loudness,
the maker)`: The Fallen's AI searches toward it, or attacks if it can
see the maker. One noise a pawn every fifth of a second. A guard with no
enemy, hearing the player, says its FHE line ("Did you hear that?").
UE1's own rule isn't in the scripts; this is Reluva's.

## In Reluva

`vm/vm_paths.cpp` finds the ways; `ScriptLevel::makeNoise` makes the
noises.

## Sources and checks

- **Where the facts come from:** the paths the editor stored with the
  maps, matched against Engine.dll (above). The noise rule is Reluva's
  own *(inferred)*.
- **How it was checked:** `reluva-tool unreal run` over all maps' first
  20 seconds (above).

## Open questions

- Jumping and dropping reaches (above).
