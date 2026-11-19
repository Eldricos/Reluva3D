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
  - the heap and pages;
  - the version: Windows XP, so the runtime uses the system's heap;
  - the command line, environment and start-up details;
  - text conversion for code page 1252;
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
  English (United States) pictures. There's one thread, so critical
  sections do nothing, `Sleep` returns at once and a wait on an event finds
  it signalled.
- **`win32_user.cpp`: USER32 and GDI32.** Window classes and windows, kept
  as records (procedure, place, size, visibility) that the engine can show
  as it likes. Making one sends `WM_NCCREATE` and `WM_CREATE`. Showing it
  first activates it (`WM_ACTIVATEAPP`, `WM_ACTIVATE`, `WM_SETFOCUS`).
  Other pieces:
  - one message queue, which the engine posts its input to and
    `PeekMessageA` reads, calling `idle` when it's empty;
  - `DispatchMessageA` and `TranslateMessage`;
  - `DefWindowProcA` destroys a window it's asked to close;
  - frames and captions are Windows 98's sizes;
  - the mouse pointer, and the desktop's size and colour depth, which the
    engine sets;
  - `MessageBoxA` is reported and answered so the program goes on.
- **`win32_registry.cpp`: the registry and COM.** An in-memory registry,
  seeded with the installed locales. The engine can add an installer's
  values and hear of the program's writes. `CoCreateInstance` asks the
  engine's `createComObject`; with none, no class is registered.
- **`com.h`, `com.cpp`: COM objects.** An interface is a table of method
  names and argument counts. `ComHost` builds its vtable of native
  functions in the program's memory, and makes the interface pointers,
  each dispatching to a C++ `ComObject`.
  - QueryInterface, AddRef and Release are done here: an object is freed
    when its count reaches 0.
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
  changed is copied there first, and a search lists both;
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
- **`nfs`**: Need for Speed III's whole program, on drives, with host
  threads ([the game's threads](../../engines/nfs/games/nfs3/systems/threads.md)).
