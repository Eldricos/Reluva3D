# How the AWE engine was worked out (Murder on the Orient Express)

How Reluva's knowledge of *Murder on the Orient Express* was gathered,
and how each piece was checked. It's the record CONTRIBUTING.md asks for:
every line of Reluva is its own, written from these sources. The format
pages in `docs/engines/awe/` say, for each part, which addresses and
files it came from; this page describes the methods.

## Ground rules

- **All of Reluva's code is new.** Nothing is taken from the game's code.
- **The game's code, text and assets stay in the player's copy.** Nothing
  of them is in this repository.
- See [CONTRIBUTING.md](../../../../../CONTRIBUTING.md) for what may and may not
  be used.

## Sources

Only sources CONTRIBUTING.md allows:

- **A legally owned install** of the game (English 1.0): `motoe.exe`,
  its DLLs (`granny2.dll`, `binkw32.dll`, `mss32.dll`) and its data
  files. The files were used locally and never committed.
- **Recordings of the original game:** a ten-part walkthrough on video,
  and short clips recorded by the project's owner (used for the
  characters' shadows).
- **Public write-ups:** the MultimediaWiki's Bink pages, and the
  decoders that document what they leave out (see
  [bink.md](../../../../common/video/bink.md)).

No leaked or unlicensed code, nor anything derived from it, was used.

## Methods

### Reading the executable

- **Disassembly.** `motoe.exe` was disassembled whole into a listing (the
  PE file's code section by Capstone, through `pefile`), with each
  address on its own line and the strings an instruction refers to
  written beside it (`push 0x490ffc ; L"act >07a"`). Functions were read
  from there, and are named in the docs by their address (`0x43a630`,
  the dialogue's Start).
- **Calls into other code** were named from the PE import table (Granny's
  functions by `granny2.dll`'s exports, Bink's, Miles' and Windows'), and
  Direct3D 8's from their offsets in the device's vtable (`SetTransform`
  +0x94, `SetMaterial` +0xa8, `SetLight` +0xb0, `LightEnable` +0xb8,
  `SetRenderState` +0xc8).
- **Classes** were followed through their vtables in the executable's
  data (a scene object's methods by slot), their constructors' field
  defaults, and their readers and writers. A field read nowhere but its
  reader and writer is recorded as unused.
- **Globals and constants** were read from the data section (the mouse's
  place at `0x48b08c`, the dialogue box's 42.0 at `0x483098`).
- **Written up as it went.** What each function does was written up in
  the format pages, with its address, alongside Reluva's code for it,
  which is written in Reluva's own terms rather than translated
  instruction by instruction.

### Running the original's code to compare

`src/emulation/x86` is an interpreter for 32-bit x86 code (written for the
id Tech 3 games; see [../../../../common/x86/README.md](../../../../common/x86/README.md)). It runs pieces of the
original to check Reluva's own code against them:

- **Granny:** Oodle1 decompression, the Bink-compressed textures and the
  compressed animation curves were written natively, then compared with
  `granny2.dll` run in the interpreter, byte for byte (bit for bit for
  the curves) on every file of the install (`awe files-check`). See
  [../../formats/gr2.md](../../formats/gr2.md).
- **Whole poses:** a model posed by an animation, by Reluva and by
  `granny2.dll`'s own sampling, compared bone by bone (`awe pose-check`) over
  every model and animation pair. This found the Count's slumped pose:
  curves with no knots, which Granny reads as the identity.
- **Movies:** each movie and video loop decoded by Reluva and by the
  game's `binkw32.dll`, its frames copied out as the game copies them,
  compared pixel by pixel (`awe bink-check`). This found Reluva's colour
  conversion rounding differently from Bink's, by up to 7.
- **The path-finder:** Reluva's port of `motoe.exe`'s path planning was
  compared with the game's own code in the interpreter: 22,480 searches
  over the 281 scenes with a walk grid (the camera's 64 × 64 map of the
  floor's cells a character can walk on), every point and heading the same
  (`awe path-check -all`). See [formats/ac2.md](formats/ac2.md).

### Sweeping the install

Readers were run over every file of the install (`reluva-tool
awe files-check`): every scene, Granny file, text table, movie and sound. A
reader that skips a field, reads past an end, or meets an unknown record
says so. `awe script-check` lists every verb and condition word the scripts
use, so each could be handled; `awe ac2-dump` and `awe table-show` show single
files. Counts over the install (values of a field, which scenes use
what) went into the docs.

### Checking against the recordings

The walkthrough is about ten hours of video. To find a moment in it:

- **By picture:** one frame a second of each part was matched against
  every scene's background (small greyscale images), giving an index of
  which scene shows when.
- **By sound:** a voice file of the install was matched against a part's
  soundtrack by normalised cross-correlation. A line of dialogue is
  found to a tenth of a second (scores near 1 for a match, under 0.25
  otherwise). This found, for example, that clicking Hubbard in the 6B
  salon gives her "not in the mood" line, not her conversation's
  opening.

To compare colours, the recording's own colour transform is fitted
first: the background a frame shows is a picture in the install, so the
frame's pixels where nothing covers it, against the picture's, give per
channel how the capture and the video changed the game's colours. Reluva's
frame is then put through the same transform and the same encoding (1280
x 720, 4:2:0 H.264 at the walkthrough's bitrate) before the two are
compared.

Then frames of the recording and Reluva's frames of the same moment
were set side by side, or measured: shadows' darkness, direction and
length from the owner's clips, lit colours in patches, fade lengths
from frame to frame, the dialogue box's and pictures' places, movie
subtitles' places and colours.

### Playing the game without a player

`reluva-tool awe game-run` plays the game headless, with Reluva's own
engine:

- **The route** (`-solve`): a search from a new game to the credits,
  branching by saving and loading the game in its own format, found a
  playthrough of about 1,100 actions. It's played back by the mouse,
  each click at a point where the game's own pick finds the object.
- **Frames** (`-shots`, with the real renderer on a virtual display): one
  before every action, looked through for anything wrong, then compared
  with the recordings. The missing dead body, the puzzle box's black
  backing and the Examine page's stuck arrows were found so.
- **The checks in [TESTING.md](TESTING.md)**: the route by the hand
  alone, saved and loaded after every action, and wandering off it.

### When the sources disagree, or are silent

- The executable decides how the game behaves; the recordings decide
  what was actually seen (a reading of the code that a recording
  contradicts is read again).
- What neither can settle is listed in [UNCONFIRMED.md](UNCONFIRMED.md);
  where Reluva knowingly differs, in
  [KNOWN_DIFFERENCES.md](KNOWN_DIFFERENCES.md).

## The tools

`reluva-tool help awe` lists the engine's commands
(`src/engines/awe/tools/`).

## Working files not included

Working files the notes above rely on. None is in the repository, as each
holds or names the game's own material; each can be made again from a copy
of the game:

- **The disassembly of `motoe.exe`.** One instruction a line with its
  address, about 180,000 lines, the strings an instruction refers to
  beside it. Made by a short Python script: `pefile` maps the executable
  and Capstone disassembles its code section in 32-bit mode
  (`capstone.Cs(CS_ARCH_X86, CS_MODE_32)`). Reluva's own disassembler
  (`reluva::x86::disassemble`, [disasm.md](../../../../common/x86/disasm.md))
  writes the same syntax.
- **The route** (`route.txt`): made by `reluva-tool awe game-run <game
  folder> -solve route.txt -mouse -characters`, or by the
  `awe.gamedata.route-search` step, which keeps it in the build
  ([TESTING.md](TESTING.md)). The search is deterministic: it finds the
  same route each time.
- **The walkthrough's scene index.** For each part of the recordings, one
  line a second: the scene whose background it shows, and how close the
  match was. Made by matching each second's frame (small, greyscale)
  against every scene's background picture.
- **Voice matches.** Where a line of dialogue falls in a recording: a voice
  file of the install cross-correlated with the part's soundtrack, as
  described above.
- **Frames and comparisons.** The route's screenshots (`awe game-run -play
  <route> -mouse -3d -shots <dir>`), frames cut from the recordings, and
  the pictures put side by side from them.

The walkthrough recordings themselves are someone else's videos, not made
here.
