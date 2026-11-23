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
- **`win32_locale.cpp`: the locale, time and memory.** English (United
  States) and code page 1252: `GetLocaleInfoA` and `W` for the types the
  C runtimes ask, `CompareStringA` and `W` (with `NORM_IGNORECASE`),
  `lstrlenA`, the interlocked counters, `FormatMessageA` ("Error n."),
  `GlobalAlloc` and its kin (a handle is its block), `GetLocalTime`,
  `GetSystemTime`, `GetTimeZoneInformation` (the bias only),
  `FileTimeToSystemTime` and `FileTimeToLocalFileTime`, by the system's
  clock and time zone, or by a date and time the engine gives
  (`Win32Process::dateTime`, as MSVCRT's `time`, `localtime` and `ctime`
  and the date and time formats take it too), local time then UTC, so a
  run repeats in any time zone; ADVAPI32's
  `GetUserNameA` ("Player"); and VERSION.dll's reading of a file's
  version information (`GetFileVersionInfoA`, `VerQueryValueA`, its
  texts given as ANSI in the block's second half).
- **`win32_ini.cpp`: INI files.** `GetPrivateProfileStringA` (a value, a
  section's keys, the sections, or the default; quotes taken off, cut to
  fit), `GetPrivateProfileIntA` and `WritePrivateProfileStringA` (a key
  set, added or removed, a section added or removed, the rest of the file
  kept as it was), through `mapPath` or the drives, so a drive's writes
  go to its writable folder. A file named without a folder is in
  `C:\WINDOWS`.
- **`win32_user.cpp`: USER32 and GDI32's start.** Window classes and windows, kept
  as records (procedure, place, size, visibility) that the engine can show
  as it likes. Making one sends `WM_NCCREATE` and `WM_CREATE`; besides the
  program's own classes, Windows' standard ones (`BUTTON`, `EDIT`,
  `STATIC`, `LISTBOX`, `SCROLLBAR`, `COMBOBOX`) are made with the
  controls' procedure the dialogs' items have (`win32_dialogs.cpp`).
  Showing it first activates it (`WM_ACTIVATEAPP`, `WM_ACTIVATE`,
  `WM_SETFOCUS`) and gives it the focus. `SetFocus` sends `WM_KILLFOCUS`
  to the window that had it and `WM_SETFOCUS` to the new one.
  `SetWindowTextA` sends `WM_SETTEXT`, which `DefWindowProcA` keeps as the
  window's text. Other pieces:
  - places as Windows keeps them: a window's on the screen, a child's
    (`WS_CHILD`) in its parent's client area; a sunken edge
    (`WS_EX_CLIENTEDGE`, 2 pixels; `WS_EX_STATICEDGE`, 1) inside the
    border, painted with the frame; `windowArea` and
    `clientArea` work out where one is on the screen, for
    `GetWindowRect`, `ClientToScreen`, `ScreenToClient`, drawing and
    input;
  - a window made, or shown (`SW_MAXIMIZE`), maximized fills the screen
    (its frame past the edges), a child its parent's client area; the
    place the program gave is ignored, as Windows does;
  - a window shown gets `WM_PAINT`, and its children shown with it; one
    hidden or destroyed has every window shown that it covered painted
    again (their frames at once, where it was, then `WM_PAINT`), and the
    desktop where none is (GDI's `paintDesktop`, black); a
    window with a frame has it painted plainly as it's shown (GDI's
    `paintFrame`: grey borders, a navy caption, without its title or
    buttons, and its scroll bars);
  - moved or sized (`SetWindowPos`, `MoveWindow`), a window gets `WM_MOVE`
    and `WM_SIZE`, and is painted again with what it uncovered;
  - `DestroyWindow`, and `DefWindowProcA` closing one, destroys a
    window's children and the windows it owns (a popup's parent is its
    owner) with it, and their timers;
  - scroll bars (`WS_VSCROLL`, `WS_HSCROLL`) lie outside the client area,
    16 pixels each, and `AdjustWindowRect` leaves them out, as documented;
    `ShowScrollBar` shows and hides them by the style (`WM_SIZE` when the
    client area changes), `SetScrollRange`, `SetScrollPos`,
    `GetScrollPos` and `GetScrollRange` keep their range and position
    (0 to 100 at first); they're drawn as Windows 98 draws them (two arrow
    buttons, a pale track, a square thumb at the position);
  - `DefWindowProcA` works them: on `WM_NCLBUTTONDOWN` an arrow sends
    `WM_VSCROLL` or `WM_HSCROLL` with `SB_LINEUP` or `SB_LINEDOWN`, the
    track `SB_PAGEUP` or `SB_PAGEDOWN`, and the thumb is held: dragged,
    `SB_THUMBTRACK` as its position changes, then `SB_THUMBPOSITION`;
    letting go ends it with `SB_ENDSCROLL`. While held the mouse is the
    bar's, as Windows' own loop takes it. An arrow or the track held is
    repeated, 400 ms after the press and then every 50 ms *(guessed: the
    timings aren't documented)*, while the pointer stays on the part
    pressed (on the track, until the thumb reaches the pointer). Where
    Windows' loop sends the repeats, they're posted, so the program takes
    them in its own loop: `PeekMessageA` and `GetMessageA` post one when
    it's due, and a wait for messages (`GetMessageA`,
    `MsgWaitForMultipleObjects`) ends for it;
  - `DefWindowProcA` answers `WM_NCHITTEST` by the window's frame (client
    area, scroll bars, a caption's buttons, caption, border, or nowhere);
    a press on a caption's button posts its `WM_SYSCOMMAND` (on the
    press: Windows waits for the release over it), and `SC_CLOSE`
    closes the window, `SC_MAXIMIZE` fills the screen (a child its
    parent) with the window, its place kept, and `SC_RESTORE` puts it
    back; `SC_MINIMIZE` does nothing, there being no taskbar to bring it
    back from and `WM_SETCURSOR` (a child's
    parent asked first; on the client area the class's cursor set, if it
    has one, elsewhere the arrow);
  - Windows' pointer: the cursor `SetCursor` sets (the arrow at first),
    shown while `ShowCursor`'s count is 0 or more, `Win32Process::pointer`
    giving its picture for the engine to show. `LoadCursorFromFileA` gives
    a handle while its file is there, its pictures read when it's first
    shown, an animated cursor's at the process's time
    ([cursors.md](../graphics/cursors.md)); Windows' own cursors
    (`LoadCursorA` with no instance) are all Reluva's own arrow, their
    pictures not Reluva's to have; a program's own cursor resources
    aren't read, and show nothing;
  - one message queue, which the engine posts its input to and
    `PeekMessageA` reads, calling `idle` when it's empty;
  - `DispatchMessageA` and `TranslateMessage`;
  - `DefWindowProcA` destroys a window it's asked to close;
  - frames and captions are Windows 98's sizes;
  - the mouse pointer, and the desktop's size and colour depth, which the
    engine sets;
  - `MessageBoxA` is reported and answered so the program goes on;
  - `GetSystemMetrics` answers for the screen, frames, icons and cursors,
    and that there's a mouse with three buttons.
  GDI32's own objects and drawing are [gdi.md](gdi.md)'s.
- **`win32_windows.cpp`: windows past their making.** Their parents,
  IDs, text and longs (`GetWindowLongA`, `SetWindowLongA`, the window
  procedure and styles among them), enabling, focus and capture,
  `ScreenToClient`, `WindowFromPoint` (the top window shown there, made
  last, then its child shown and enabled there, and so down: `windowAt`),
  `MoveWindow`,
  `CallWindowProcA`; painting (`InvalidateRect` queues one `WM_PAINT`;
  `GetUpdateRect` is the client area until it's handled); timers
  (`SetTimer`, `KillTimer`: `WM_TIMER` comes when one is due and nothing
  else is queued, from `PeekMessageA` and `GetMessageA`, made as it's
  taken and never queued, so a peek without removing leaves the timer
  due and the queue as it was; and
  `DispatchMessageA` calls a timer's own procedure); `GetMessageA`, which
  waits (with host threads, letting the others run);
  `SystemParametersInfoA` (the work area, the screen saver off);
  `LoadStringA` from the program's string table. With host threads,
  `MsgWaitForMultipleObjects` is a wait that a message or a due timer
  also ends. Rectangles (`SetRect`,
  `IntersectRect`, `UnionRect`, `PtInRect`...), `wsprintfA` and
  `IsCharAlphaNumericA` are in `win32_user.cpp`. Drawing is
  [GDI's](gdi.md).
- **`win32_dialogs.cpp`: dialogs.** A dialog made from its template in a
  module's resources (`DLGTEMPLATE` or `DLGTEMPLATEEX`), sized in dialog
  units of MS Sans Serif 8 (6 by 13 pixels), its items child windows of
  the standard classes; `WM_INITDIALOG` to its procedure, which
  `DefDlgProcA` calls for every message; `CreateDialogParamA`,
  `DialogBoxParamA` (its own message loop until `EndDialog`, the parent
  disabled), `GetDlgItem` and `SendDlgItemMessageA`. The controls keep
  their state and answer their messages: text, buttons' checks (auto
  radio buttons unchecking their group, `BM_CLICK` telling the dialog),
  list boxes' items, selection and data. An edit control is a single
  line of text that's typed into and drawn: `WM_CHAR` (Backspace, and
  `ES_UPPERCASE` and `ES_LOWERCASE`), the arrows, Home and End (with
  Shift choosing), Delete, a click placing the caret by the nearest
  character and taking the focus; `EM_SETSEL`, `EM_GETSEL`,
  `EM_REPLACESEL`, `EM_LIMITTEXT` (30,000 at first), `WM_SETFONT`; the
  parent told of each change (`EN_CHANGE`); painted in the brush and
  text colour its parent gives (`WM_CTLCOLOREDIT`, on a DC of its own),
  white and black if none, its text in its font, the selection in the
  highlight's colours and the caret while it has the focus (GDI's
  `paintEdit`). Other controls aren't drawn yet,
  nor an edit control's several lines (`ES_MULTILINE`).
- **`win32_shell.cpp`: the common dialogs and SHELL32.**
  `GetOpenFileNameA` and `GetSaveFileNameA` (`OPENFILENAMEA`) ask the
  engine (`Win32Process::chooseFile`) for a file: the program's title,
  its filter's names and patterns, and the name it suggests are passed
  on, and the engine shows the system's own dialog, Windows 98's not
  being Reluva's to draw. The file's folder becomes the program's drive
  X:, readable and writable, and its buffer gets `X:\name`, with
  `nFileOffset`, `nFileExtension`, the title buffer, and, saving, the
  default extension added to a name without one. Cancelled, the answer
  is 0 with `CommDlgExtendedError` 0; a buffer too small, 0 with
  `FNERR_BUFFERTOOSMALL` and the size needed in its first two bytes.
  Without `chooseFile` every dialog is cancelled. The program's hook
  procedure and template aren't used. `ShellExecuteA` opens a web page
  (`http://`, `https://`, `www.`, and the `http:\\` Windows also took)
  through `Win32Process::openUrl`, which asks the player first: done
  (42) if it's opened, `SE_ERR_ACCESSDENIED` if not; anything else isn't
  run (`SE_ERR_NOASSOC`), reported.
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
  - They become window messages (`WM_MOUSEMOVE`, button presses, double
    clicks for classes with `CS_DBLCLKS`, `WM_KEYDOWN`/`WM_KEYUP` with
    the scan code in lParam). The mouse's go to the window that has
    captured it, else the window under the pointer, in its client
    coordinates, and to none that's disabled; keys and the wheel to the
    window with the focus. Either falls back to the main window (the
    first still there). Without a capture each mouse message is led, as
    in Windows, by `WM_NCHITTEST` and `WM_SETCURSOR` sent to the window;
    off its client area it becomes the non-client message
    (`WM_NCMOUSEMOVE`, `WM_NCLBUTTONDOWN`...), with the hit-test code.
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
  a new file only in a folder that's there, `NUL`), copies (`CopyFileA`,
  into the writable folder), searches, folders,
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
- `testEditControl` makes an `EDIT` with `CreateWindowExA`, its edge
  taken from the client area; sets its text, chooses it all and types
  over it past its limit (the parent told each change), deletes,
  places the caret with a click (the focus's messages sent), and paints
  it on a screen in memory, white and in its parent's brush.
- `testPointer` checks the arrow shown at first and hidden by
  `ShowCursor`, a cursor file loaded and set (and one missing), the
  arrow from `LoadCursorA`, `SetCursor(NULL)`, and `DefWindowProcA`'s
  `WM_SETCURSOR`: the class's cursor on the client area, the arrow on
  the caption.
- `testFileDialogs` checks the Open and Save dialogs with a pick given
  in the test: what the player is asked, the program's buffer and
  offsets, the file read and written through X:, a dialog cancelled and
  a buffer too small; and `ShellExecuteA`'s web pages, opened, declined,
  and a file that isn't one.
- `testScrollBars` checks `AdjustWindowRect` leaving scroll bars out,
  their range and position, `DefWindowProcA`'s hit test, a click on the
  track and on an arrow, a thumb dragged, an arrow held (repeated after
  the pause, steadily, and not once the pointer's off it), the track held
  until the thumb passes the pointer, and hiding them.
- **Confidence:** published unless marked.

- `tests/common/windows_dialogs.cpp` (`common.unit.windows_dialogs`)
  makes a dialog from a template in a DLL built in the test, clicks its
  radio buttons and fills its list box; it checks a window's longs and
  text, a timer's `WM_TIMER`, rectangles and `wsprintfA`. Its `places`
  case checks a maximized window, children's places, `WindowFromPoint`,
  where the pointer's, a captured mouse's and the keys' messages go,
  `DefWindowProcA`'s hit-test and a non-client move, `WM_MOVE` and
  `WM_SIZE`, a timer peeked, an owner's closing taking its popup and
  timer, the painting when a window is shown or destroyed, and
  `LoadCursorFromFileA`.
- `tests/common/windows_kernel.cpp` (`common.unit.windows_kernel`)
  reads and writes an INI file on a drive (the writes kept apart), and
  checks the locale's answers, string comparison, global memory, a
  FILETIME's date and `FormatMessageA`, and that a search finds a folder
  of the drive's own though the writable folder has one beside it.
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
- **`legoloco`**: LEGO Loco's whole program, on a drive, with host
  threads ([running.md](../../engines/legoloco/systems/running.md)).
