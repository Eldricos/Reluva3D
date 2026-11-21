# DirectSound

## What it is

`src/emulation/x86/directx/dsound.{h,cpp}` stands in for `DSOUND.dll`'s
DirectSound, up to DirectX 5, for programs run in the x86 interpreter.
`DirectSoundCreate` makes the DirectSound object; everything else is
reached through it, as COM objects (`src/emulation/x86/com.h`). It's
written from Microsoft's DirectX documentation: its interfaces, their
methods' order, the IIDs, structures and constants are the SDK's
(`dsound.h`).

## What Reluva supports

- **`IDirectSound`:** making buffers (`CreateSoundBuffer`, the primary
  one included), duplicating them (`DuplicateSoundBuffer`, the same sound
  played apart), the capabilities (mixed in software, any rate from 100
  to 100,000 Hz), stereo speakers, any cooperative level.
- **`IDirectSoundBuffer`:** `Lock` (from an offset, from the write
  position, or all of it, wrapping round) and `Unlock`, `Play` (once or
  looping), `Stop`, the play and write positions, the status, volume, pan
  and frequency. The primary buffer's format can be set; it plays
  nothing itself.
- **3D:** the primary buffer's `IDirectSound3DListener` (position,
  orientation, distance and rolloff factors) and a 3D buffer's
  `IDirectSound3DBuffer` (position, minimum and maximum distance, mode),
  heard as a volume and a pan.
- **Heard:** PCM, 8- or 16-bit, mono or stereo, on Reluva's mixer.
- **Not handled:** other formats (they play in silence; their positions
  still move); position notifications (`IDirectSoundNotify`, DirectX 6);
  Doppler shifts, cones and velocities (kept, not heard); deferred 3D
  settings (applied at once); capture.

## How it works

### Buffers and their play position

A buffer's sound is in the process's memory, where the program writes
it through `Lock`; a duplicate shares it. Its play position isn't a
sound card's: it follows the process's clock (`Win32Process::milliseconds`)
from where the buffer was started, at the buffer's frequency, a whole
block at a time. So a program polling `GetCurrentPosition` to keep a
streaming buffer filled sees it move as it would, and a buffer played
once stops, and reads as stopped, at its end. The write position is 15 ms
ahead of the play position, as a card's is ahead by what's already on
its way out.

What the play position passes is handed to the mixer as a stream
(`audio::SoundStream`), each time the program calls the stand-in. A
buffer whose frequency changes while it plays starts a new stream at the
new rate.

### Volume, pan and 3D

- **Volume** is hundredths of a decibel down: -600 is 6 dB down, half as
  loud; -10,000 is silence.
- **Pan** is the other side's attenuation, the same way: -10,000 is left
  only.
- **3D:** a buffer's distance from the listener (times the listener's
  distance factor) past its minimum distance divides its volume by
  `1 + rolloff * (distance / minimum - 1)`, the distance going no further
  than its maximum. Its pan is where it is across the listener: DirectSound
  is left-handed, so the listener's top cross its front points right. A
  head-relative buffer is placed from a listener at the origin, facing
  +z. A buffer whose 3D mode is "disable" is heard as a plain buffer.

## Sources and checks

- **Where the facts come from:** Microsoft's DirectX 5 SDK documentation:
  the interfaces, their methods and arguments, the structures
  (`DSBUFFERDESC`, `DSCAPS`, `DSBCAPS`, `DS3DBUFFER`, `DS3DLISTENER`), the
  flags and the error codes, and how volume, pan and distance are heard.
- **How it was checked:** `common.unit.windows`'s `directsound` case:
  a buffer locked wrapping round and written, played once by a clock the
  test sets, its play and write positions and status as it goes, then
  stopped at its end and heard through the mixer, quieter for its
  distance from the listener; looped from an offset and stopped; a
  duplicate sharing its sound; the listener's and a 3D buffer's positions
  kept. LEGO Island's program creates DirectSound, finds a sound card,
  and goes on ([running.md](../../engines/legoisland/systems/running.md)).
- **Confidence:** published unless marked.

## Used by

- **`legoisland`**: LEGO Island's program, which won't start without a
  sound card; its code imports nothing else to play sound with
  ([running.md](../../engines/legoisland/systems/running.md)).
