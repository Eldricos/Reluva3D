# <Game or engine>: <what the files are> (`.ext`)

> **Status:** decoded / partial / unknown · **Code:** `src/engines/<engine>/...` · **Checked on:** <release, and how many files> · **Confidence:** <confirmed / from the code / ...> unless marked

<!-- Copy to formats/<name>.md. "Decoded" means a reader exists and has been
checked against the game's own files. All numbers little-endian unless
said. Confidence: the words and how to mark exceptions are in
docs/DOCUMENTATION_STANDARD.md; mark only what differs from the default,
with *(inferred)* and the like at the end of its sentence. -->

## Overview

<!-- What the files hold, where they are in the install, how many there are,
and what reads them. -->

## Layout

<!-- The header, records and tables, as tables: offset, size, field. Name a
field only when its meaning is known; else give its offset and the values
seen. -->

| Offset | Size | Field |
|---|---|---|
| | | |

## Behaviour

<!-- Optional: how the game uses what the file holds, where that isn't a
system page of its own. -->

## In Reluva

<!-- Where it's read (file, class), what the viewer shows, the reluva-tool
commands for it. -->

## Sources and checks

- **Where the facts come from:** <the files compared, the executable's
  loader at an address, a published write-up>.
- **How it was checked:** <every file read to its end, a tool's sweep, a
  unit test>.

## Open questions

<!-- Fields not understood, values not seen yet. Delete once there are none. -->
