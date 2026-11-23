# DirectPlay

## What it is

`src/emulation/x86/dplay.cpp`: DirectPlay 3, Microsoft's multiplayer
API of 1997, for programs run in the [x86 emulator](README.md), made
through `CoCreateInstance` as the DirectPlay COM class.

## What Reluva supports

- `IDirectPlay2` and `IDirectPlay3`, each in its Unicode and ANSI form.
- No service providers are installed: `EnumConnections` lists none, as
  on a machine without a network, so a program offers no multiplayer.
  `Close` succeeds. Any other method stops the run, naming itself.

Jedi Knight creates DirectPlay at start-up and uses the object without
checking it was made. Without the class, it read through a null
pointer.

## Sources and checks

- **Where the facts come from:** the DirectX 5 documentation and SDK
  headers (`dplay.h`): the class and interface identifiers, and the
  methods' order and arguments.
- `testDirectPlay` (`common.unit.windows`) creates it through
  `CoCreateInstance` and lists its connections: none.
- **Confidence:** published.

## Used by

- **`sith`**: Jedi Knight's start-up
  ([hosting.md](../../engines/sith/games/jk/systems/hosting.md)).
