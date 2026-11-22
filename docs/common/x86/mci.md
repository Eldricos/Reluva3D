# Movies through MCI

## What it is

`src/emulation/x86/mci.{h,cpp}`: `x86::MciVideo`, the movie window of
Video for Windows (`MSVFW32.dll`'s `MCIWndRegisterClass`, the
`MCIWndClass` window) and `WINMM.dll`'s `mciSendCommandA`, for programs
run in the [x86 emulator](README.md) that play AVI movies. Written from
the Windows SDK's documentation of MCIWnd (`vfw.h`'s messages) and of
MCI's commands and their structures (`mmsystem.h`).

## What Reluva supports

- **The movie window:** `MCIWndRegisterClass` registers `MCIWndClass`
  with the process's `RegisterClassA`; its windows are made by the
  program's own `CreateWindowExA`. Its messages:
  - `MCIWNDM_OPENA` (0x499) opens an AVI file, closing the one before;
  - `MCIWNDM_PUT_DEST` (0x48f) places the picture in the window (a
    `RECT`); until then it's the movie's own size, at the corner;
  - `MCIWNDM_GETDEVICEID` (0x464), `MCIWNDM_GETLENGTH` (0x468) and
    `MCIWNDM_GETPOSITIONA` (0x466), in frames;
  - MCI's commands sent as messages: `MCI_PLAY` (from where it is to the
    end), `MCI_STOP`, `MCI_PAUSE` and `MCI_CLOSE`;
  - `WM_CLOSE` destroys the window, and `WM_DESTROY` closes its movie;
    the rest goes to `DefWindowProcA`. Its playbar and menu aren't drawn.
- **`mciSendCommandA`** on those devices or its own: `MCI_OPEN` with
  the file as its element (`MCI_OPEN_ELEMENT`), `MCI_PLAY` with
  `MCI_FROM` and `MCI_TO` in frames, `MCI_STOP`, `MCI_PAUSE`,
  `MCI_CLOSE`, and `MCI_STATUS` for the length, position and mode.
  Other commands answer `MCIERR_UNSUPPORTED_FUNCTION` (274), which the
  run's output names; a device not open, `MCIERR_INVALID_DEVICE_ID`
  (257); a file not found or not an AVI, `MCIERR_FILE_NOT_FOUND` (275).
- **`MCI_NOTIFY`:** when a play ends, the window given as the callback
  (its low word, as documented: the process's handles are wider, so it's
  the window whose handle ends so) is posted `MM_MCINOTIFY` (0x3b9) with
  how it ended
  (`MCI_NOTIFY_SUCCESSFUL`, or `MCI_NOTIFY_ABORTED` when stopped or
  played again) and the device.
- **The movies:** AVI files in Cinepak, through Reluva's decoder
  (`src/video/avi.h`); their PCM sound, 8- or 16-bit, plays on the mixer
  set in `MciVideo::mixer`, when a play starts at the first frame.

## How it works

Opening reads the whole file through the process's `mapPath`, so a movie
on a drive the process sees is found as the program names it.

A play runs on a thread of the process's own (`createThread`), which
takes turns with the program's threads: it waits for each frame's time,
by the process's clock (`milliseconds`), decodes the frames whose time
has passed and draws only the newest. A frame is drawn into the screen's
pixels (`Gdi::screen`, which DirectDraw's primary surface sets) at the
window's place on the screen plus the destination's corner, stretched
to the destination's size and clipped to the screen; at 8 bits each
colour is the screen palette's nearest, remembered. `MciVideo::frameDrawn` then lets the engine show it.

Without host threads (`Win32Process::useHostThreads`) nothing would draw
the frames: a play ends as it starts, and says so.

## Sources and checks

- **Where the facts come from:** the Windows SDK's documentation of
  MCIWnd's window class and messages, and of `mciSendCommand`, its
  commands, flags, structures, error numbers and `MM_MCINOTIFY`. LEGO
  Loco's `loco.exe` shows what's needed: the window made with style
  `0x4000400b`, then `OPENA`, `PUT_DEST` and `GETDEVICEID` sent to it,
  then `MCI_PLAY` through `mciSendCommandA` with `MCI_NOTIFY` and
  `0x01000000` (`MCI_MCIAVI_PLAY_WINDOW`, playing in its window).
- **How it was checked:** `tests/common/mci.cpp` (`common.unit.mci`)
  makes a movie window, opens an AVI built in the test, reads its length
  and device, plays it and finds `MM_MCINOTIFY` posted, and checks
  `mciSendCommandA`'s answers. `reluva-tool legoloco run` plays LEGO
  Loco's `legoSpin.avi`: its 280 frames in 18.7 seconds, then the
  notice.
- **Confidence:** published unless marked.

## Used by

- **`legoloco`**: LEGO Loco's opening movie, `legoSpin.avi`
  ([running.md](../../engines/legoloco/systems/running.md)).
