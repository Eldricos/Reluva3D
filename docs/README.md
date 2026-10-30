# Reluva's documentation

- [BUILDING.md](BUILDING.md): building Reluva on Linux, Windows and macOS.
- [../CONTRIBUTING.md](../CONTRIBUTING.md): the clean-room rules every
  contribution follows.

## Engines

Each engine keeps everything about itself in its own folder,
`docs/engines/<engine id>/`. Its `README.md` says what the engine runs, how
far it has got, and how to use it: the folder to add, the keys, and its
`reluva-tool` commands. It also lists the engine's other pages: the file
formats it reads (in `formats/`) and its notes.

| Engine | Games | Docs |
|---|---|---|
| `idtech3` | Star Trek: Voyager - Elite Force | [engines/idtech3/](engines/idtech3/README.md) |
| `netimmerse` | LEGO Alpha Team | [engines/netimmerse/](engines/netimmerse/README.md) |
| `newworlds` | Star Trek: New Worlds | [engines/newworlds/](engines/newworlds/README.md) |

## Shared code

What's in `src/common/` and any engine can use is documented in
`docs/common/`:

- [common/x86.md](common/x86.md): the x86 interpreter and the parts of
  Windows, DirectX and Aureal A3D it stands in for. Elite Force's single
  player and LEGO Alpha Team run their original programs on it.
- [common/video/avi.md](common/video/avi.md): AVI movies.
- [common/video/indeo5.md](common/video/indeo5.md): Intel Indeo Video 5.

## Adding an engine's docs

Give the engine a folder `docs/engines/<engine id>/` with a `README.md` laid
out like the others:

- what it runs;
- where it stands;
- using it;
- its developer tools;
- its documents.

File formats go in `formats/`, one page each. Each page records how the
format was worked out, as [CONTRIBUTING.md](../CONTRIBUTING.md) asks. Add
the engine to the table above. Anything that more than one engine uses
belongs in `docs/common/`.
