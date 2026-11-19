# The Fallen: the UnrealScript virtual machine

> **Status:** done · **Code:** `src/engines/unreal/vm/vm.cpp`, `vm/vm_natives_core.cpp` · **Checked against:** the code (the scripts, and the natives' declarations and callers), and every map run · **Confidence:** from the code unless marked

## Overview

The Fallen's game is written in UnrealScript, compiled into its packages
([script.md](../formats/script.md)). Reluva runs it in a VM of its own.

It **runs The Fallen's levels with the player in them**: the
Maps page plays a level (Enter), its scripts live, the player moved by
the keyboard and mouse, seen from the game's own camera, the AI finding
its way along the level's paths and the characters' lines shown as
they're said, and its own HUD and menus drawn over it, the menus
travelling from map to map, games saved and loaded, and its sounds and
music heard. It isn't the
game yet: some of The Fallen's own engine functions are still missing
([level.md](level.md)). Every class of the game loads (`vm.cpp`), every default
property converts to its type, and all 74 maps start: their actors made,
the game type spawned, the start-up events sent, the player logged in as
the level's character, then twenty seconds of ticks, with no crash. The
script errors left are mistakes in the game's own scripts ([level.md](level.md)) and
None accessed where natives not written yet would have filled
something in. The Maps page draws each level as its scripts leave it
when play begins.

What the VM does in a level is on the pages for each part:
[the level's actors and ticks](level.md), [movement](movement.md) and
[climbing](climbing.md), [the AI](ai.md), [dialogue](dialogue.md),
[the HUD](hud.md), [the menus](menus.md), [sound](sound.md),
[effects](effects.md), [saved games](saves.md),
[documents and objectives](documents.md), [splines](splines.md),
[animation](animation.md), [bones and attachment points](bones.md) and
[playing a level](playing.md).

## How it works

### Values and objects

Values are kept by property, not in raw memory as the original does: an
object holds one `Value` per property element (`VmObject::values`, its
class's slots: inherited properties first), a struct value one per
field. A value is a byte, int, bool, float, name (interned for the whole
VM, compared without case), string, object (classes are objects too:
`VmObject::asClass`) or struct. Objects the VM doesn't run (textures,
sounds, meshes) are stand-ins holding where they are in their package.

Classes load from their compiled script on first use (`Vm::classAt`):
their parent first, then their fields (properties, functions, states,
structs, enums), then their defaults over the parent's. A property a
subclass declares again (The Fallen redeclares `SoundVolume`, `SoundPitch`,
`SoundRadius`, `Acceleration` with other types) is found most derived
first. Struct values in default properties are stored as their fields
one after another, each as a property of its type is (bytes 1 byte, ints
and floats 4, names and objects compact indices, strings as strings,
bools 1 byte); every one of The Fallen's reads to its end. Objects' own
built-in properties (`Outer`, `Name`, `Class`) are set when they're made:
scripts read them (CTVE_DllObject checks its `Outer` is an actor).

Objects the VM doesn't run are given their own stored properties over
their class's defaults when they're first met: textures always, anything
else under 64 KB (meshes and sounds aren't read whole for it). The
Fallen's HUD textures keep where they go on the screen in theirs
([hud.md](hud.md)).

Classes' `config` properties take the player's and the game's settings
from the `.ini` files as the class loads (`Vm::applyConfig`):
- a `config` property from the class's own section (`[Engine.HUD]`);
- a `globalconfig` one from the section of the class that declares it;
- User.ini first, then DefUser.ini, then the game's own `.ini`;
- an array's elements as `Key[n]`; structs aren't read.

So the HUD shows dialogue (`bDisplayDialog=True`) but not cut scenes'
subtitles (`bDisplaySubtitles=False`), as DefUser.ini has it, and the
player's mouse sensitivity is 3. A console command's `set` (the options
windows, below) changes a setting for as long as the VM runs, before the
files' (`Vm::setConfig`); the files aren't written.

Classes' `localized` properties, the text players read (menu items,
messages), come from the game's `.int` files (`Vm::applyLocalized`): the
class's own section, by its name alone (`[DS9_Hud]`), of its package's
`.int`, else of `Classes.int`, where The Fallen keeps them all (2,131
lines). Each is looked for in the `int` folder beside System, then in
System, where the engine's own are (Engine.int, UMenu.int, SoftDrv.int).
Without them the HUD's menus had bullets and no words ("RESUME GAME",
"MISSION OBJECTIVES"...). `Localize(Section, Key, Package)` reads the
same files, without the quotes the engine's put round their values. One
that's missing is `<?int?Package.Section.Key?>`, as the engine marks it.

An object as a string (`string(Level)`, `$`) is its path, as UnrealScript
gives it:
- an object from a package is `Package.Group.Name`;
- a class is `Package.Class`;
- a level's actor is the map's name and its own (`M01_KiraL1A.LevelInfo0`);
- anything else is its `Outer`'s path and its name.

The scripts depend on this: The Fallen's `GetMissionLevel` takes the
mission number from `string(Level)`, `GetItemName` strips the package off
a class's, and the mutator list compares against `Package.Class`. While
it gave the name alone, every level counted as mission 100.

### The interpreter

Statements run from a function's or state's code (`Interpreter::run`),
expressions evaluated recursively (`eval`), variables through references
(`ref`) so assignments, out parameters and `++` change them in place.
What it does:

- Variables: local (the call's frame), instance (the context object:
  `self`, or the left of a `.`), default (the class's defaults), array
  elements (bounds checked), struct members.
- Calls: virtual (the object's state's function first, its parents'
  states', then its class's and parents'), final (that function: `Super`
  calls and final functions), global (skipping states), native (by
  number, through the function that declares it), static (on a class).
  Arguments are converted to the parameters' types; out parameters are
  written back after; optional ones left out are empty. `&&` and `||`
  leave their right side unevaluated when the left decides.
- `None`: reading through it gives an empty value and is counted
  ("Accessed None"), as the original logs and carries on.
- Control: jumps, `switch`/`case` (below), `foreach` (the iterator native gives
  its results, the loop assigns each to its out parameter in turn,
  skipping actors destroyed meanwhile), `return`, `stop`, labels and
  `goto` in state code (a missing label is reported and the code carries
  on), `assert`.
- States: `GotoState` calls `EndState` in the old state and
  `BeginState` in the new, and sets its code to the label (`Begin` by
  default; `Auto` finds the class's auto state). A state inherits a
  parent class's same-named state's functions and code.
- State code runs each tick until it waits in a latent function
  (`Sleep`, `FinishAnim`, pawns' `MoveTo` and kin, and those not
  written yet, which wait a tick so loops round them don't spin) or
  stops. Each object's timer (`SetTimer`) and `Tick`
  run each tick; `Disable('Tick')` and the like stop the engine calling
  them.
- Guards: calls nested past 250, a function past 2,000,000 statements or
  a tick of state code past 20,000 are stopped and reported.

Classes with no script (Mesh, LodMesh, Model, Primitive, Sound, Font...)
are made up the same way when an object of theirs is first met
(`Vm::nativeClass`), as kinds of Object, except LodMesh is a Mesh and
Mesh and Model are Primitives, so `Mesh(...)` casts work. A name looked
up by `DynamicLoadObject` is taken only as an object of the class asked:
`PLYR.DS9_Sisko` is both a class and a mesh.

Natives are C++ functions registered by the qualified name of the
function declaring them (`Object.Add_IntInt`): all of Core's Object
(`vm_natives_core.cpp`: the operators for every type, maths, vectors and
rotators, strings, states, `IsA`, `DynamicLoadObject`...), and the
Engine ones a level needs so far (`vm_level.cpp`): `Spawn`, `Destroy`,
`SetTimer`, `Sleep`, location, rotation and collision setters, the actor
iterators (`AllActors`, `ChildActors`, `BasedActors`, `RadiusActors` and
the visible ones by radius, `RadiusPawns`, `AllPawns`), the pawn list, animation
groups from the actor's mesh. A native not written yet returns an empty
value and is counted.

A `switch` finds its case itself: the labels in turn, each one's value
against the switch's, jumping on to the next label until one matches or
the default. A `case` label reached by falling through from the case
above is passed over, not tested again. Labels with no code between them
share the code after the last (`case 38: case 40: case 13:` in the
console's `KeyEvent`). Testing each label as it was reached took the
first match on to the next label, failed it there and jumped past the
shared code: the menu's arrows never reached the HUD.

## In Reluva

`vm/vm.cpp` loads the classes and runs the interpreter; Core's natives
are in `vm/vm_natives_core.cpp`, the level's in `vm/vm_level.cpp` and the
files each page names. `reluva-tool unreal script <game folder> <class |
Class.Function | -all <dir>>` writes compiled UnrealScript as text, and
`reluva-tool unreal run <game folder> <map | -all> [seconds]` runs a
level's scripts ([level.md](level.md)).

## Sources and checks

- **Where the facts come from:** Clean room as the rest of the engine ([REVERSE_ENGINEERING.md](../REVERSE_ENGINEERING.md)): the
  bytecode and the objects are as [script.md](../formats/script.md)
  reads them, what each native does comes from its declaration (name,
  parameters, flags, as `reluva-tool unreal script` shows them) and from what
  the scripts calling it expect, and the order of the start-up events is
  Reluva's reading of the public UnrealScript documentation of actors'
  lifetimes. None of it is translated from Epic's code or the clean-room
  port's.

- **How it was checked:** every class of the game loads, every default
  property converts to its type, and all 74 maps start and run 20
  seconds (`reluva-tool unreal run -all`, [level.md](level.md)).
