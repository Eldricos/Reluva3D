# DirectSound

## What it is

`src/emulation/x86/dsound.{h,cpp}`: `x86::DirectSound`, DirectSound for
programs run in the [x86 emulator](README.md): what `DSOUND.dll`
exports, `IDirectSound` and `IDirectSoundBuffer`, and `WINMM.dll`'s
`PlaySoundA`, mixed into one output for Reluva's mixer. Written from the
DirectX SDK's documentation of DirectSound (`dsound.h`'s interfaces, in
the order it declares their methods, its structures and its codes) and
the Windows SDK's of `PlaySound`.

The `nfs` engine has a DirectSound of its own, with DirectSound 3D, for
Need for Speed III's sound DLL; this one is for any program, and has no
3D yet.

## What Reluva supports

- **The exports**, by name or by ordinal as programs import them: 1
  `DirectSoundCreate`, 2 and 3 `DirectSoundEnumerateA` and `W`.
- **One device:** the enumeration names the primary driver (no GUID),
  then Reluva's device, with a GUID of its own
  (`{8c8b2a40-4e3b-4b2f-9a35-52454c555641}`, Reluva's: programs that
  pick a device by its capabilities only take one that has a GUID).
  `DirectSoundCreate` makes it for no GUID, `GUID_NULL` or that one;
  another device is no driver (`DSERR_NODRIVER`).
- **`IDirectSound`:** `CreateSoundBuffer` (`DSBUFFERDESC` of DirectX 6
  or 7, 20 or 36 bytes); `GetCaps` (software mixing: every PCM format,
  rates of 100 to 100,000 Hz, one primary buffer, no hardware buffers);
  `DuplicateSoundBuffer`; `SetCooperativeLevel`; `Compact`; the speaker
  configuration (stereo unless set); `Initialize`, already done.
- **The primary buffer:** one, however often it's asked for. `SetFormat`
  needs `DSSCL_PRIORITY` or above (`DSERR_PRIOLEVELNEEDED`) and is kept,
  but Reluva mixes at its own rate. Writing it (`Lock`) isn't stood in
  for.
- **Secondary buffers:** 8- or 16-bit PCM, mono or stereo, 4 bytes to
  `DSBSIZE_MAX`. `Lock` hands out the buffer's own memory, in two parts
  where it wraps round, from an offset, the write cursor
  (`DSBLOCK_FROMWRITECURSOR`) or whole (`DSBLOCK_ENTIREBUFFER`); `Play`
  once or looping, `Stop`, `SetCurrentPosition`, `GetCurrentPosition`,
  `GetStatus`; volume, pan and frequency where the buffer was made with
  their control (else `DSERR_CONTROLUNAVAIL`); `GetCaps`, `GetFormat`,
  `Restore`.
- **`PlaySoundA`:** a WAV file (8- or 16-bit PCM or IMA ADPCM, as
  Reluva's WAV reader reads), or one's image in memory (`SND_MEMORY`);
  `SND_ASYNC`, `SND_LOOP`, `SND_NOSTOP`; a null sound stops it. Played
  without `SND_ASYNC`, the call returns when it has played. System
  sounds by alias and WAV resources aren't stood in for: the call fails
  and the run's output says so.
- A method not listed ends the run, naming it.

## How it works

A buffer's sound lives in the process's memory, where the program
writes it; duplicates share it. Its play position follows the process's
clock (`Win32Process::milliseconds`) from where it was last started,
moved or stopped, at its frequency; a buffer played once stops at its
end and is back at its start *(inferred: what programs that play a sound
again without moving it expect)*. The write cursor is 10 ms ahead of the
play cursor. A new buffer holds silence (128 for 8-bit samples, 0 for
16-bit) *(inferred)*.

`output()` is an `audio::OutputStream` for the mixer
(`Mixer::playOutput`). On the audio thread it reads each playing
buffer's samples from the process's memory at the output's rate, keeping
its own place, which it moves to the clock's when they're more than a
tenth of a second apart (the program moved it, or the mix fell behind).
Volume and pan are attenuations in hundredths of a decibel: a gain of
10^(v/2000), silence at -10000; a pan to one side lowers the other.
`PlaySoundA`'s wave is added to the same mix.

## Sources and checks

- **Where the facts come from:** the DirectX SDK's documentation of
  DirectSound: its interfaces and methods, `DSBUFFERDESC`, `DSCAPS`,
  `DSBCAPS`, `WAVEFORMATEX`, the flags and `MAKE_DSHRESULT`'s codes; the
  Windows SDK's of `PlaySound`. The export ordinals are those LEGO Loco
  imports, matched to the functions by their arguments *(inferred)*:
  ordinal 2 is given a callback and its context, ordinal 1 a device, an
  out pointer and none. The interfaces' IIDs are those in `loco.exe`.
- **How it was checked:** `tests/common/dsound.cpp`
  (`common.unit.dsound`) lists the devices, sets up the primary buffer
  as LEGO Loco does, writes a secondary buffer, plays it once and
  looping by a clock the test sets, mixes it at two volumes and a pan,
  duplicates it, and plays a WAV file through `PlaySoundA`.
  `reluva-tool legoloco run` sets up LEGO Loco's sound and starts its
  music.
- **Confidence:** published unless marked.

## Used by

- **`legoloco`**: LEGO Loco's sound and music
  ([running.md](../../engines/legoloco/systems/running.md)).
