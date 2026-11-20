# LEGO Alpha Team: sound

> **Status:** done · **Code:** `src/emulation/x86/a3d.cpp` · **Checked against:** the original running in Reluva, by ear · **Confidence:** confirmed unless marked

## Overview

The game's music, voices and sound effects, played through NetImmerse's
A3D sound (`NiA3dSound`); in Reluva through an A3D stand-in and the
engine's mixer.

## How it works

- Done: sound, through the A3D stand-in (`src/emulation/x86/a3d.cpp`,
  docs/common/x86/README.md). `NiA3dSoundSystem::Startup` succeeds, and
  `DDStreamingAudioManager::AddSource` returns sources. The streaming
  thread fills them as their play positions move, and the engine hears
  them through its mixer.
  - Which sounds are which, as the game makes them (`NiA3dSource`'s
    type): the streamed ones (`DDStreamingAudioManager`: music such as
    `ThemeLoop.dds`, `OgelIntro.dds`, `RewardMusic.dds`, and the voices)
    are type 5, native, not placed. The interface's sounds
    (`DDUIManager::AddSound`) and the actors' skills (`DDSkill::Update`;
    a byte in the skill's data picks native or 3D) are loaded from files.
    The 3D ones are placed at their actors, in world units, with minimum
    and maximum distances of 200 to 300 and 600 to 10,000; the listener
    follows the camera (`NiA3dListener::A3dUpdate`, each frame from
    `NiA3dSoundSystem::Click`), right-handed with z up.
  - Two things in the stand-in decide whether more than the streamed
    sounds are heard (both in
    [a3d.md](../../../../../common/x86/a3d.md)): `GetWaveFormat` doesn't
    answer success for an empty source (else `NiA3dSource::Load` never
    loads the file), and the listener is offered under the IID the game
    asks for (else it stays at the origin). So Mission One's skills and
    interface sounds load and play, placed around the camera.

## In Reluva

Reluva runs the game's own code, unchanged, in the x86 interpreter ([running.md](running.md)). Its A3D sound goes to Reluva's stand-in
(`src/emulation/x86/a3d.cpp`, [a3d.md](../../../../../common/x86/a3d.md)),
which plays it through the engine's mixer. What it doesn't do of A3D is
in [KNOWN_DIFFERENCES.md](../KNOWN_DIFFERENCES.md).

## Sources and checks

- **How it was checked:** Mission One's skills and interface sounds load
  and play, placed around the camera (above). The volumes and pans haven't
  been compared by ear with A3D hardware
  ([UNCONFIRMED.md](../UNCONFIRMED.md)).
