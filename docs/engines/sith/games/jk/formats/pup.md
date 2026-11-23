# Jedi Knight: puppets (`.pup`)

> **Status:** decoded · **Code:** `src/engines/sith/formats/puppet.cpp` · **Checked on:** US retail, all 49 puppets · **Confidence:** confirmed unless marked

## Overview

Which animation ([key.md](key.md)) a character plays for each thing it
does, in each of its modes: `ky.pup`, Kyle's, has a default mode, one
aiming a weapon, one with the saber, and three swimming. They are in
`Res2.gob`'s `misc\pup\`, plain text; a level's templates name each
character's.

## Layout

Line by line; a `#` starts a comment to the line's end.

- **`Mode=<n>`**, or **`Mode=<n>, BasedOn=<m>`**, in either case: a mode,
  and the mode whose actions it takes where it has none of its own.
- **An action:** `<name> <keyframe> <flags> <lopri> <hipri>`, as the
  header comment names them (`# submode keyframe flags lopri hipri`):
  what the character does (`stand`, `walk`, `fire`, `death`...), its
  `.key`, flags in hexadecimal, and two priorities.
- **`joints`**, then `<joint>=<node>` lines: which of the model's nodes
  are its head (0), neck (1), torso (2), and the weapons' firing and
  aiming points (3 to 6), as the comments name them.
- **`end`**: the last line read.

Every one of the 934 actions names an animation that's in the archives.

## In Reluva

`parsePuppet` (`src/engines/sith/formats/puppet.h`) reads one into a
`Puppet`; `Puppet::find` looks an action up in a mode and the modes it's
based on. `reluva-tool sith formats <install>` reads them all.

## Sources and checks

- **Where the facts come from:** the 49 files and their comments.
- **How it was checked:** every puppet read by `reluva-tool sith
  formats`, every action's animation looked for in the archives;
  `sith.unit.models` on a puppet made by the test.

## Open questions

- The actions' flags and priorities.
