# Windows stand-ins

## What it is

`src/emulation/x86/win32*.cpp`, `msvcrt.cpp` and `com.cpp`: a Windows
process for programs run in the [x86 emulator](README.md), with stand-ins
for the parts of Windows and the C runtime they call. DirectX, A3D and the
C++ library have pages of their own ([directx.md](directx.md),
[a3d.md](a3d.md), [msvcp60.md](msvcp60.md)).

## What Reluva supports

- **`win32.h`, `win32.cpp`: a Windows process.** It loads a DLL or an EXE
  (relocated if its preferred address is taken), binds its imports
  (functions, and variables for data imports), and runs its `DllMain`, or
  an EXE's entry point. Where the stack and thread blocks go is the
  engine's choice (`Win32Process`'s constructor): by default first in
  memory, as the engine tests' records were made with; kept clear of
  0x400000 when an EXE is to be loaded (`loadsExe`); or at the top of
  memory (`stackAtTop`, LEGO Alpha Team's), so an EXE without relocations
  can have 0x400000. The KERNEL32
  functions a Visual C++ 6 C runtime calls are stood in for:
  - the heap and pages, and `LocalAlloc`'s blocks, all fixed (a movable
    block's handle is its address) and zeroed;
  - the version: Windows XP, so the runtime uses the system's heap;
  - the command line, environment and start-up details;
  - text conversion for code page 1252;
  - `CompareStringA` and `CompareStringW`, character by character
    rather than by Windows' word sort: the locale is ignored, and of the
    flags only `NORM_IGNORECASE` (Latin-1's letters) counts, so "abc"
    sorts after "ABC" as it wouldn't on Windows;
  - the standard handles, whose output is passed on.

  An import nothing stands in for is bound to a function that fails the
  run, naming it. Threads and windows aren't there yet.
- **`win32_files.cpp`: files.** The program's Windows paths become paths on
  this machine through `mapPath`, which the engine provides. These are
  stood in for:
  - handles: `CreateFileA`, `ReadFile`, `WriteFile`, `SetFilePointer`,
    `GetFileSize`, `GetFileTime`, and the 16-bit era's `OpenFile`/`_lread`;
  - searches (`FindFirstFileA` with wildcards);
  - memory-mapped views: copies, as these programs only read them;
  - the current folder;
  - the drives: C: and a CD-ROM drive, whose volume label the engine sets
    so a game's CD check passes.
- **`msvcrt.h`, `msvcrt.cpp`: MSVCRT.dll**, for programs that link
  Visual C++'s C runtime dynamically, written from Microsoft's
  documentation of each function:
  - memory, strings and characters (the "C" locale);
  - numbers, and Visual C++'s `rand` (so random choices come out as they
    did);
  - maths, including the `_CI` functions that take their operands on the
    x87 stack, `_ftol` and `_controlfp`;
  - `printf` and `scanf` formatting, and `FILE` files over the process's
    file handles;
  - `_stat` with Visual C++ 6's structure, and time;
  - the start-up helpers (`__getmainargs`, `_initterm`, its variables);
  - `_EH_prolog`'s exception frame, and the `exception` class.

  Throwing a C++ exception, or reaching an exception handler, ends the run
  naming the type thrown, since dispatching isn't done yet.
- **`win32_system.cpp`: the system.** System information (one Pentium
  II-class processor, 256 MB), the performance counter (microseconds),
  `MulDiv`, the computer's name, and `GetDateFormatA`/`GetTimeFormatA` with
  English (United States) pictures. The time and the time zone are the
  host's (`GetLocalTime`, `GetSystemTime`, `GetTimeZoneInformation`,
  `FileTimeToLocalFileTime`, `FileTimeToSystemTime`). No console can be
  made; what's written to one goes to `output`. The environment stays
  empty: `SetEnvironmentVariableA` is accepted, not kept. There's one thread, so critical
  sections do nothing, `Sleep` returns at once and a wait on an event finds
  it signalled. No other program can be run: `WinExec` and `LoadModule`
  fail with `ERROR_FILE_NOT_FOUND`, as for a program that isn't there,
  saying so to `output`. `DebugBreak` ends the run, naming it, there being
  no debugger.
- **`win32_user.cpp`: USER32 and GDI32.** Window classes and windows, kept
  as records (procedure, place, size, visibility) that the engine can show
  as it likes. Making one sends `WM_NCCREATE` and `WM_CREATE`. Showing a
  top-level window the first time activates it (`WM_ACTIVATEAPP`,
  `WM_ACTIVATE`, `WM_SETFOCUS`), whether by `ShowWindow` or by making it
  with `WS_VISIBLE`, as Windows does: Jedi Knight makes its window visible
  and waits for the activation before it starts. Other pieces:
  - the active window and the focus (`SetFocus`, with `WM_KILLFOCUS` and
    `WM_SETFOCUS`), and window longs (`GetWindowLongA`, `SetWindowLongA`:
    the procedure, style, identifier and user data);
  - `GetWindow` and `GetTopWindow` among windows in the order they were
    made, and `GetClassInfoA` for a class's style and procedure;
  - one message queue, which the engine posts its input to and
    `PeekMessageA` reads, calling `idle` when it's empty; `GetMessageA`
    waits for a message (calling `idle`, and letting other threads run),
    and gives up when the process is ending;
  - `DispatchMessageA` and `TranslateMessage` (a key that types makes
    `WM_CHAR`: letters, digits, the marks and the keypad's, unshifted, as
    on a US keyboard);
  - `DefWindowProcA` destroys a window it's asked to close;
  - frames and captions are Windows 98's sizes;
  - the mouse pointer, and the desktop's size and colour depth, which the
    engine sets;
  - `MessageBoxA` and `MessageBoxW` are reported and answered so the
    program goes on;
  - nothing is ever waiting to be painted (`GetUpdateRect`, `BeginPaint`),
    and the desktop is a window of its own (`GetDesktopWindow`);
  - COMCTL32's `InitCommonControls`, by its ordinal, does nothing;
  - `LoadStringA` reads the module's string table resources, from its
    image in memory, as Latin-1 ('?' beyond);
  - `wsprintfA` formats as MSVCRT's `sprintf` does (a superset of its
    conversions: it would print no floating point), at most 1024 bytes;
  - a window's text: `SetWindowTextA`, `GetWindowTextA`,
    `GetWindowTextLengthA`; `GetParent`; `DestroyWindow` destroys a
    window's children with it.
- **`win32_dialog.cpp`: dialog boxes.** Made from their templates
  (`DLGTEMPLATE` and `DLGTEMPLATEEX`), a resource of the module's
  (`CreateDialogParamA`, `DialogBoxParamA`) or in memory (their
  `Indirect` forms), as windows of class `#32770` with their controls
  (buttons, edits, statics, list and combo boxes) as child windows; the
  program's dialog procedure gets `WM_INITDIALOG` and `WM_COMMAND`, and
  reads and sets the controls: `GetDlgItem`, `GetDlgCtrlID`,
  `SendDlgItemMessageA`, `Set`/`GetDlgItemTextA`, `Set`/`GetDlgItemInt`,
  `CheckDlgButton`, `IsDlgButtonChecked`, `CheckRadioButton`, and the
  controls' messages (text, `BM_GETCHECK` and `BM_SETCHECK`, list and
  combo boxes' strings and choice). Dialog units are pixels by MS Sans
  Serif's average size where the template names a font (6 by 13), the
  system font's otherwise (8 by 16).
  - Nothing is drawn. The engine is told of each dialog, its caption and
    controls (`Win32Process::dialogBox`); unset, they go to `output`.
  - A modal dialog (`DialogBoxParamA`) is answered at once: by the
    button `dialogBox` names, else its default button (the one marked
    `BS_DEFPUSHBUTTON`, or IDOK), as a player pressing Enter would; if
    that doesn't end it, IDCANCEL; if nor that, it ends as cancelled. It
    returns what the procedure gave `EndDialog`.
  - A modeless one runs as the program runs: `IsDialogMessageA` takes
    the messages for it and its controls, Enter pressing its default
    button and Escape IDCANCEL; `EndDialog` hides it.
- **`win32_gdi.cpp`: GDI's device contexts, fonts and text, bitmaps and
  palettes.** A device context draws into a bitmap in the program's
  memory: DirectDraw's surfaces hand them out (`GetDC`), through
  `createDeviceContext`, with how a colour becomes a pixel and back
  (`GdiBitmap`). A window's (`GetDC`, `BeginPaint`) draws into a surface
  of 32 bits a pixel the size of its client area, made anew when the
  window's size changes; after each blit into it (`BitBlt`, `StretchBlt`,
  `StretchDIBits`) the engine is given the client area's pixels
  (`Win32Process::windowDrawn`): a game windowed shows its frames so. The
  desktop's (`GetDC(0)`) draws nowhere and reads as black. Fonts
  (`CreateFontA`) keep their height; `TextOutA` draws in Reluva's built-in
  bitmap font scaled to it, in the text colour, over the background colour
  unless the mode is transparent, and `GetTextExtentPoint32A` measures in
  the same font. The letters' shapes aren't the font's: no font of
  Windows is at hand. Besides:
  - memory device contexts (`CreateCompatibleDC`, `DeleteDC`) draw into
    the bitmap selected into them;
  - bitmaps are of 8, 16, 24 or 32 bits: `CreateDIBSection` (BI_RGB, or
    BI_BITFIELDS's masks; its colour table as RGBQUADs or, with
    DIB_PAL_COLORS, indices into the context's palette), and
    `CreateCompatibleBitmap`, as the context's bitmap is, else of the
    desktop's depth (16 bits being 5-6-5). A memory context that has no
    bitmap yet gives the desktop's depth too, where Windows would make a
    monochrome bitmap. A file mapping given to `CreateDIBSection` isn't
    shared: the pixels are in memory of their own;
  - `BitBlt`, `StretchBlt` and `StretchDIBits` (a bottom-up DIB's source
    measured from its lower left corner) convert between depths and
    colour tables, scale by the nearest pixel whatever
    `SetStretchBltMode` keeps, and mirror where the signs of the widths or
    heights differ. Raster operations are the ternary table, but those
    that use a brush's pattern fail, there being no brushes;
  - `GetDIBits` gives a bitmap's format, or its scan lines in 16, 24 or
    32 bits (8 only from an 8-bit bitmap: colours aren't reduced to a
    table);
  - palettes: `CreatePalette`, `SelectPalette`, `GetNearestPaletteIndex`,
    `AnimatePalette` (only PC_RESERVED entries change) and an 8-bit DIB
    section's `SetDIBColorTable`. `RealizePalette` maps every entry on a
    desktop of 8 bits and none above, where `GetSystemPaletteUse` says
    SYSPAL_ERROR; at 8 bits it's SYSPAL_STATIC. A palette realized
    doesn't change how a DirectDraw surface's context matches colours.
- **`win32_registry.cpp`: the registry and COM.** An in-memory registry,
  seeded with the installed locales. The engine can add an installer's
  values and hear of the program's writes. `RegDeleteKeyA` deletes a key
  and its values as Windows NT does, not one with keys under it
  (`ERROR_ACCESS_DENIED`); the engine isn't told of a deletion.
  `CoCreateInstance` asks the engine's `createComObject`; with none, no
  class is registered.
- **`com.h`, `com.cpp`: COM objects.** An interface is a table of method
  names and argument counts. `ComHost` builds its vtable of native
  functions in the program's memory, and makes the interface pointers,
  each dispatching to a C++ `ComObject`.
  - QueryInterface, AddRef and Release are done here: an object is freed
    when its count reaches 0.
  - An object can hand out another for an interface it hasn't
    (`ComObject::aggregate`), as COM's aggregation does: Direct3D 1's
    device asked of a DirectDraw surface.
  - A method an object doesn't stand in for ends the run, naming it.
- **Input** (`win32_user.cpp`): the engine passes the mouse and keyboard
  to the process with `mouseMoveTo`, `mouseButton`, `mouseWheel` and
  `key`.
  - They become window messages to the main window (`WM_MOUSEMOVE`,
    button presses, double clicks for classes with `CS_DBLCLKS`,
    `WM_KEYDOWN`/`WM_KEYUP` with the scan code in lParam).
  - They also update the pointer (within `ClipCursor`'s rectangle) and
    what `GetKeyState` and `GetAsyncKeyState` report.
  - `rawInput` listeners (DirectInput) hear the same movements, buttons
    and keys.
- Threads (`_beginthread`) take turns with the main thread, so no two
  ever run at once (`Win32Process::startThread`, `runThreads`):
  - Each has its own registers, stack and thread information block (a
    `Cpu::Context`).
  - When the main thread calls `Sleep`, `PeekMessage` or `GetMessage`,
    each thread that's due runs (`Cpu::resume`).
  - A thread runs until its own `Sleep`, which yields (`Cpu::yield`)
    until its wake time, or until it ends (`_endthread`, or returning from
    its function).
  - Code a thread shares with the main thread is never run by both at
    once, so the critical sections can stay no-ops. `testThreads` checks
    the turns.
- `LoadLibraryA` loads a DLL from the program's files (through
  `mapPath`) and runs its start-up. `GetProcAddress` reports the names it
  can't find to `output`.

### Drives (`addDrive`)

For a program that expects drive letters of its own (Need for Speed III
installs to C: and reads its CD on D:), instead of `mapPath`. The first
drive added puts its own stand-ins for files, folders and drives in
place of those above; `mapPath` then finds files on the drives too
(`LoadLibraryA`, the C runtime's). Each drive letter the program sees is
a host folder:

- names are matched ignoring case;
- what the program writes can be kept in a folder of its own
  (`Drive::writable`), so the game's folder stays as it was; a file
  changed is copied there at its first write, and a search lists both. A
  file opened for writing that's never written isn't copied: Jedi Knight
  opens its 150 MB of archives so, and only reads them;
- a CD drive is read-only, with its label and serial;
- the stand-ins cover files (`CreateFileA` with each disposition's errors,
  a new file only in a folder that's there, `NUL`), searches, folders,
  drives, volume labels, free space and read-only file mappings;
- handles are kernel objects (`addHandle`, `object<T>`), from 0x10000000;
- `COM1` to `COM9` are devices the engine provides (`openSerialPort`, a
  `DeviceObject`), read plainly or overlapped: an overlapped read with
  nothing there is left pending, its event signalled when there is, the
  bytes taken by `GetOverlappedResult`.

### Host threads (`useHostThreads`)

For a whole program whose threads wait on each other, instead of the
turns above. The program's threads take turns on the one processor. Each
runs on a host thread of its own, with its own stack and information
block; only the one holding the turn runs, and the processor's context
(`Cpu::saveContext`) changes with it. As on Windows:

- the runnable thread of highest priority runs, in turn with those of
  the same priority;
- one that wakes a thread of higher priority hands over at once;
- otherwise they change over when one waits, and every few thousand
  instructions (`slice`), so that a thread polling a variable lets the
  others run.

`useHostThreads` puts their stand-ins in place: threads, events, mutexes,
critical sections, the waits, thread-local storage (the TLS array at the
block's 0xE10, as Windows 95's), `Sleep`, `ExitProcess`, and the
multimedia timer (`timeSetEvent`), whose callbacks run on one timer
thread of time-critical priority, as WINMM's do. `run` runs an
executable's entry point on the main thread until it ends the process.

### Executables

- an EXE is loaded at 0x400000, which is kept clear (`loadsExe`);
- a DLL loaded before the EXE (one it imports) that asks for the EXE's
  place is put elsewhere, as Windows loads the program first: Jedi
  Knight's `SMACKW32.DLL` asks for 0x400000, as `JK.EXE` does, which has
  no relocations;
- its name, folder and command line are set by the engine (`setProgram`,
  `setCommandLine`);
- zero-filled sections are left as zeros whatever raw size the file
  gives them (Watcom's linker gives one);
- resources can be read (`findResource` in `src/common/pe.cpp`: the
  version information, for one);
- `VirtualProtect` succeeds (every page is readable and writable);
- a library a program loads can be a set of native functions under its
  exports' names: `src/emulation/x86/glide/glide2_x86.h` serves 3dfx's
  `glide2x.dll` so, reading the structures its arguments point at from
  guest memory ([the Glide stand-in](../graphics/glide.md)).

## Sources and checks

- **Where the facts come from:** Microsoft's published API documentation,
  each function as it's documented.

- **How it's checked:** the tests (`common.unit.windows`) call the
  stand-ins as a program would.
- They call MSVCRT's and the file stand-ins:
  - strings;
  - `rand`'s sequence;
  - `sscanf` and `sprintf`;
  - `_ftol` and `_CIpow` on the x87 stack;
  - `_EH_prolog`'s frame;
  - `fopen` relative to the program's folder;
  - a mapped view, a search, and the CD's label;
  - `MulDiv`'s rounding, date and time text, the performance counter.
- It makes a window with a native window procedure and checks the
  messages it gets, the queue, frame sizes, and closing. It also checks
  the registry's keys, values, sizes and enumeration, and
  `CoCreateInstance`.
- `testInput` checks the messages input becomes, double clicks, key
  states and `ClipCursor`. It also checks DirectInput's mouse (its state,
  its movement since the last read, its buffer) and keyboard.
- `testGdi` blits an 8-bit DIB section into a 32-bit one, mirrored, with
  raster operations and a changed colour table, stretches a 24-bit DIB
  over it, reads it back with `GetDIBits`, and checks palettes.
  `testOddsAndEnds` checks `CompareStringA` and `CompareStringW`,
  `LocalAlloc`, `wsprintfA`, a dialog that isn't there and programs that
  fail, `RegDeleteKeyA` and `DebugBreak`. `testDialogs` makes a dialog
  from a `DLGTEMPLATE` and a `DLGTEMPLATEEX` in memory, its procedure
  filling and reading its controls, modal (answered by its default
  button, then by the engine's Cancel) and modeless (Enter through
  `IsDialogMessageA`, then destroyed with its controls).
  `tests/common/windows_resources.cpp` (`common.unit.windows_resources`)
  loads a module with a string table and checks `LoadStringA`, and one
  with a dialog found by its name.
- **Confidence:** published unless marked.

- `tests/common/drives.cpp` (`common.unit.drives`) checks the drives'
  paths, a new file's folder, and a serial port read plainly and
  overlapped.

## Used by

- **`idtech3`**: Star Trek: Voyager - Elite Force's single-player DLLs
  ([single-player.md](../../engines/idtech3/games/stvef/systems/single-player.md)).
- **`netimmerse`**: LEGO Alpha Team's whole program
  ([running.md](../../engines/netimmerse/games/legoalphateam/systems/running.md)).
- **`awe`**: Murder on the Orient Express's `granny2.dll` and `binkw32.dll`,
  run by its tools to check Reluva's own decoders against them.
- **`sith`**: Jedi Knight's whole program, on drives, with host threads,
  in a trial of how far it gets
  ([hosting.md](../../engines/sith/games/jk/systems/hosting.md)).
- **`nfs`**: Need for Speed III's whole program, on drives, with host
  threads ([the game's threads](../../engines/nfs/games/nfs3/systems/threads.md)).
