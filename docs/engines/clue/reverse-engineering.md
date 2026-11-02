# Clue: how it was worked out

How Reluva's Clue was made to match the original: the sources, the rules
followed, and the methods, with what each found and the mistakes made on
the way. The findings themselves are in [clue.md](../clue.md) and the
format pages ([res.md](../../formats/clue/res.md),
[level.md](../../formats/clue/level.md), [board.md](../../formats/clue/board.md),
[resi.md](../../formats/clue/resi.md)), each with the addresses in the
executables it came from.

## Rules

- **Clean room.** No leaked or decompiled source code of the game was used
  or written down. Reluva's code is written from what the game's files and
  its executables were seen to do, described in these pages in words and
  numbers.
- **Reverse engineering of an owned copy.** The executables of a retail
  copy (the CD and the 2.2 patch) were disassembled and, in parts, run in
  an emulator, to see what they do. Every finding taken from them names
  its address (`Clue.exe` 0x41C690, `!EagleM.dll` 0x1000BFF0 ...), so it can
  be checked.
- **No game data in the repository.** No images, sounds, videos, text or
  executables of the game are committed; tests build their own small data.

## Sources

- **The game's files:** the retail CD (`Clue V2.0.01.27`), a clean install
  from it and the 2.2 patch. Executables: `Clue.exe` (the game, Visual C++
  5 and MFC), `!EagleM.dll` (EAI's "Eagle" library: data files, graphics,
  its own window system), `ImageRLE16.dll` (the image drawing),
  `Mss32.dll` (the Miles Sound System), `SHW32.DLL` (SmartHeap).
- **Recordings of the original** (YouTube, and the owner's own in a
  virtual machine), listed in [clue.md](../clue.md#reference-recordings):
  used for what the code doesn't show (where EagleM places things), to
  check what the code says, and before the code was read.

## Reading the executables

**Disassembly.** `Clue.exe` was disassembled once with Capstone into a
list of every instruction, kept on disk and searched with small Python
scripts: the strings and what refers to them, what calls an address, the
bounds of a function, the words at an address, every instruction with a
given operand (an object field such as `+0x564`, a constant such as
`0x1388`). `!EagleM.dll` and `ImageRLE16.dll` the same way. A linear sweep
stops at data among the code, so the sweep was restarted past each bad
byte.

**Names.** Nothing in `Clue.exe` names its own functions, but much around
them does:

- **Imports.** Its import table names every function it calls in the
  libraries: Miles' (`AIL_set_stream_volume`, `AIL_redbook_play` ...),
  USER32's, and `!EagleM.dll`'s, which are exported under their MSVC
  decorated names (`?MoveWindow@EGuiWindow@@QAEXHHHHH@Z`): class, method,
  calling convention and argument types. Demangling them gave EagleM's
  whole interface. MFC's are by ordinal (2233 `CRuntimeClass::CreateObject`,
  540 a `CString`'s constructor ...).
- **MFC's runtime classes.** Each dynamically created class has a record
  with its name (`FamilyAndSinglePlayerMenu`, `PrefMenu`, `MainMenu`,
  `Number` ...) and its create function, which leads to its constructor
  and the table of its virtual functions. A function's slot in that table
  gave its role (0x118 the dialogs' "shown", 0x40 an object's set-up ...).
- **Strings.** Resource names (`UI:IleftFrame`, `menu:Alt1flash`),
  sound file names (the table of sound names at 0x45E698, indexed by
  number everywhere), debug and text-format strings. Both libraries can
  write their objects out as text, and the keywords of those forms
  (`FPS`, `Flags`, `FrameSize`; `Relative2D`, `RelativeWorld`, `BBox`,
  `KeyList`) named most of the fields of the animation files.

**Reading the code.** Mostly by hand, a function at a time: what it's
given, what fields of which object it reads and writes, which library
functions it calls with what. Jump tables (switches) were read from the
file through the section table. Constants that are times, sizes and
positions were checked against the game's data (pictures' sizes, the
level's coordinates).

## Running the code

Where the code works its result out from much else (the game screen's
layout: each window placed from others' rectangles and its picture's
size), parts of `Clue.exe` were run in an emulator (Unicorn, from Python)
instead of being followed by hand:

- the executable mapped at its base, each imported function pointing at a
  stub that does what it needs to: EagleM's windows as records of their
  rectangles (`Create`, `MoveWindow`, `GetWindowRect`, `SizeToContent` from
  the pictures' real sizes, `GetDlgItem`), MFC's `CString`, SmartHeap's
  allocator, USER32's `SetRect` and `OffsetRect`; a thread block for the
  exception chain; each stub popping its arguments as its decorated name
  says;
- then the game's own code run from where it builds the screen (0x435050),
  through its layout (0x435630) and the bar's (0x43C2D0), and the
  rectangles read off the stubs.

That gave every control's place, which matched the recordings' but for
three (corrected).

## Data files

`reluva-tool` reads every format and checks them all (`clue-check`), lists
and extracts the archives (`clue-list`, `clue-extract`), prints the level
(`clue-level`) and plays thousands of games between computer players
(`clue-board`, with the computer players' statistics). Small programs
built against Reluva's own code printed what a question needed: every
animation's speed and flags, every object's keys, the dialogs' controls,
pictures' sizes.

## Recordings

- **Frames:** `ffmpeg` cut each recording to the game's 640 × 480 at a
  frame a second (or two). Positions were found by matching the game's own
  pictures (OpenCV template matching); each recording's offset and scale
  were checked by where the bar's ornaments fall, which are known to the
  pixel.
- **Sound:** the recording's sound at 8 kHz, cross-correlated with each of
  the game's WAV files (normalised, so a quiet sound under music still
  shows), listing when each plays. Speeds were measured by trying each
  rate from 0.96 to 1.06 and keeping the best.
- **The ticker:** the sentences the game showed (turns, suggestions,
  accusations) read off the ticker, from its strips stacked into sheets,
  to follow a game's suggestions.

## Checks

- Unit tests (`tests/test_clue.cpp`) of the formats, the rules, the
  computer players, the drawing order, the lighting, the hit tests.
- A headless harness that drives Reluva by script (clicks, keys, waits)
  and saves screenshots, to see a change without a display.
- `clue-board`'s statistics set against what the recordings show.

## Mistakes worth knowing

- **Sounds played 65/64 fast.** `Clue.exe` sets each sound's rate to the
  file's × 65/64 (an argument read like a pan's middle is a rate factor).
  Searched for at the file's own speed, the wind and the small thunders
  were never found in five recordings, which led to a wrong account of
  the weather; at 65/64 they all were.
- **The animations' speed.** A constant 10 in every animation's header
  was taken for its frame rate; EagleM reads each frame's part with it
  (its size). The rate is a byte after the name, and differs between
  animations (2 to 100 a second).
- **Crops.** A recording cut 12 pixels off its game area gave positions
  12 / 1.5 pixels out; calibrating every recording against known pieces
  of the screen caught it.
- **Classes that look alike.** The main menu's Preferences and the game's
  own (from the Esc menu) are different windows with different code; one
  stops the music on leaving, the other doesn't.
- **Halves of a story.** Thunders found in a recording turned out to be the
  suggestion videos' own sound, not the weather's.

## What is kept where

The disassembly lists, the scripts that searched them, the emulator
script and the recording-analysis scripts were working tools, not kept in
the repository: they work on the game's executables and recordings, which
aren't in it either. What they found is in these pages, with addresses.
