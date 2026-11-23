# DirectSound

## What it is

`src/emulation/x86/directx/dsound.cpp`: DirectSound, up to DirectX 5's
interfaces, for programs run in the [x86 emulator](README.md), played
through Reluva's mixer. `directx/sound3d.cpp` is DirectSound 3D's model of
where a sound is heard, shared with Need for Speed III's own stand-in.

## What Reluva supports

- **The card:** one, mixing in software: 8 and 16 bits, mono and
  stereo, 100 to 100,000 Hz (`GetCaps`), no hardware buffers.
  `DirectSoundCreate` makes it; `DirectSoundEnumerateA` lists it.
- **Buffers** (`IDirectSoundBuffer`): PCM, in the program's memory, which
  it writes between `Lock` and `Unlock` (in two pieces when it wraps round
  the end, from the write cursor or the whole buffer). `Play` once or
  looped, `Stop`, `SetCurrentPosition`; volume, pan and frequency where the
  buffer was made with their controls; `GetStatus`.
  `DuplicateSoundBuffer` shares the sound, which is freed with the last.
- **The play position** follows the process's clock at the buffer's
  frequency, so a program streaming into a buffer, or waiting for one to
  end, sees it move whether or not anything is heard. The write cursor is
  15 ms ahead of it while it plays. A buffer played once stops at its end.
- **The primary buffer:** its format (`SetFormat`) and its volume, which
  is the card's; it can't be locked (`DSERR_PRIOLEVELNEEDED`), its mix
  isn't the program's to write.
- **3D** (`IDirectSound3DBuffer` from a buffer made with
  `DSBCAPS_CTRL3D`, `IDirectSound3DListener` from the primary buffer):
  positions, velocities, cones, distances, modes, the listener's
  orientation and factors, each now or deferred (`DS3D_DEFERRED`) until
  `CommitDeferredSettings`. How a source is heard is `sound3d.h`'s model,
  from DirectSound's documentation:
  - distance: full volume up to the minimum distance; past it,
    min / (min + rolloff * (d - min)), no quieter than at the maximum
    distance;
  - the cone's outside volume, in decibels linearly between its angles;
  - pan: the nearer speaker gets it all, the farther one turned down by
    up to 20 dB for a sound straight to one side;
  - Doppler from the velocities along the line, within half to twice the
    frequency;
  - behind the listener, 3 dB down and muffled (a low-pass from 20 kHz
    beside to 4 kHz straight behind): Reluva's own, as DirectSound leaves
    front and back to the card.

## How it works

The card's output (`DirectSound::output`) is an `audio::OutputStream` the
engine plays with `Mixer::playOutput`. On the mixer's thread it reads each
playing buffer from the program's memory at its own steady pace, kept to
the play position: within a tenth of a second it keeps its pace, beyond
that it jumps to the position. The buffers' state is shared with the
program's thread under a lock. Once the `DirectSound` object is gone the
output is silent.

## Sources and checks

- **Where the facts come from:** the DirectX 5 documentation and SDK
  headers (`dsound.h`): the interfaces' methods in order, their
  arguments, flags, structures and errors.
- `testDirectSound` (`common.unit.windows`) streams into a looping buffer
  for two seconds as SMACKW32 does, a quarter at a time behind the play
  position, and checks every frame comes out once and in order; then
  places a 3D buffer to the listener's right and, by a deferred
  setting committed, to its left, and checks each speaker's level.
  `testSound3d` checks the model: distance, pan, cone, the listener's
  turn, head-relative and disabled modes, Doppler and the rear.
- **Confidence:** published, but the rear's muffling, which is Reluva's
  own.

## Used by

- **`sith`**: Jedi Knight's sound, its cut scenes' through SMACKW32.DLL
  ([hosting.md](../../engines/sith/games/jk/systems/hosting.md)).
- **`nfs`**: Need for Speed III uses the 3D model in its own DirectSound
  stand-in ([PLAN.md](../../engines/nfs/games/nfs3/PLAN.md)).
