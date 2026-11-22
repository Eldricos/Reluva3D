# Need for Speed II SE: testing

## Automated checks

### Unit tests

None of its own yet. They're to be in `tests/engines/nfs/unit/`, beside
NFS III's, each a suite `nfs.unit.<topic>` on data built in the tests
(never the game's files), starting with recognising its releases.

### The basic engine test

`nfs.engine` (`tests/engines/nfs/engine.cpp`) is the engine's, and covers
NFS III only so far.

### The game-files test

Not yet. It's to be registered as `nfs.nfs2se`, run with
`RELUVA_NFS2SE=<the install>` (docs/DEVELOPMENT.md, Engine tests).

### What the automated checks don't cover

Everything, until the game is recognised.

## A playthrough by hand

Not yet.

## Last checked

| Date | Release | Platform | What was checked |
|---|---|---|---|
| November 2026 | US CD and an install from it | Linux | NFS III's readers over every file (`reluva-tool nfs survey`), and `nfs2sen.exe` loaded in the x86 emulator ([research/first-survey.md](research/first-survey.md)) |
