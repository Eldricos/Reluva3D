# Aureal A3D

## What it is

`src/common/x86/a3d.cpp`: Aureal's 3D sound API, for programs run in the
[x86 emulator](README.md), played through Reluva's mixer. No current
machine has an A3D card.

## What Reluva supports

- Sources hold their wave data (PCM, 8 or 16 bits, mono or stereo) in
  the program's memory. The program fills it through `Lock` and
  `Unlock`.
- A playing source's position follows the process's clock (`milliseconds`)
  at its rate and pitch. It wraps round when looped, and a source played
  once stops at its end. `GetWavePosition`, `GetWaveTime` and `GetStatus`
  report it, so programs that stream into a source or wait for it to
  finish see it move.
- What a source has played is handed to the mixer as a `SoundStream`
  (`audio::Mixer::playStream`). That happens on every call into A3D, the
  same calls a program makes to keep its streaming buffers filled.
- A positioned source (not created native, bit 0 of its type) gets a
  gain from its distance to the listener and a pan from its side:
  beyond its minimum distance the gain is
  `min / (min + scale * (distance - min))`, the distance going no
  further than its maximum and the scale being its own
  `SetDistanceModelScale` times the A3D object's (1: inverse distance).
  In `SetMinMaxDistance`'s mode 1 (`A3D_MUTE`) it's silent past its
  maximum. The side comes from the listener's front and top in the
  coordinate system `SetCoordinateSystem` chose (0 right-handed, 1
  left-handed).
- The listener is the A3D object's `{c398e563-…}` interface, the IID
  NiA3dSound asks for (its own constant). Offered under another IID, the
  listener was never found, never moved from the origin, and every
  positioned sound was heard from there.
- `GetWaveFormat` fails for a source that has neither a wave nor a
  format yet. NiA3dSound's `NiA3dSource::Load` asks it first and takes
  success to mean the wave is already loaded: answering success, every
  sound loaded from a file (the actors' skills, the interface) stayed
  empty and silent. Only the streamed ones (music, voices) were heard.
- Geometry (occlusion, reflections), materials, cones, Doppler and EQ
  are accepted and not heard. `IsFeatureAvailable` reports none.
- Wave events (`SetWaveEvent`) are set as play passes their offsets. They
  are the only events whose state the process keeps
  (`Win32Process::trackEvent`). Every other event reads as set to a
  wait, since threads take turns and none could set it while another
  waits. Programs stream by them: NiA3dSound's streaming manager puts
  one at the middle of a source's buffer and refills half the buffer
  each time it's set. Read as always set, it raced through the whole
  file in a few seconds and the line was cut short.

## Sources and checks

- **Where the facts come from:** names, method orders and constants come
  from Aureal's public SDK documentation (`ia3dapi.h`). Each method a
  program calls was checked against its call site: the vtable slot and the
  number of words pushed.

- `testA3d` creates A3D through `CoCreateInstance` and makes a source. It
  fills the source through a `Lock` that wraps round, plays it once and
  then looped as the clock moves, and checks its position, its status,
  the samples the mixer hears, and a wave event set as play passes it.
- **Confidence:** published unless marked.

## Used by

- **`netimmerse`**: LEGO Alpha Team's sound, through NetImmerse's
  `NiA3dSound` ([sound.md](../../engines/netimmerse/games/legoalphateam/systems/sound.md)).
