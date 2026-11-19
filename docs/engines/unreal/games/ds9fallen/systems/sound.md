# The Fallen: sound and music

> **Status:** partial · **Code:** `src/engines/unreal/vm/vm_sound.cpp`, `audio/level_audio.cpp` · **Checked against:** the code (Engine.dll and Miles.dll) · **Confidence:** from the code unless marked

## Overview

Effects heard from where they are (muffled through walls), ambient
sounds, the music (the MP3s, [sound.md](../formats/sound.md)) and the
dialogue's voices with their lip-sync.

## How it works

`vm_sound.cpp` takes what the scripts play, and the page plays it
(`level_audio.cpp`, through `src/audio`'s mixer). How is Engine.dll's
(`execPlaySound`, `CheckHearSound`) and the game's audio driver's
(Miles.dll, The Fallen's `MilesAudioSubsystem`: its `PlaySound` and
per-frame `Update`), as read from them; the code is Reluva's own.

- **`PlaySound(Sound, Slot, Volume, bNoOverride, Radius, Pitch,
  bNoHearNoise)`** and `PlayOwnedSound`:
  - Left out: the slot is `SLOT_Misc`; `Volume` and `Pitch` are the
    actor's `TransientSound` ones, `Radius` its `TransientSoundRadius`.
    A `Radius` of 0 is 1600 (`SoundRadius` is the ambient sound's only).
  - **Who hears it** (`CheckHearSound`): the player, from its view target,
    else its camera. Beyond `Radius` / √1.3 it isn't played at all. With
    the level's walls in the way (not for a `SpecialEvent` or `Keypoint`)
    it's at 0.35 of its volume for good, and unless the player set it off
    (its `Instigator`) heard only within √0.6 of that.
  - **Slots:** an actor's slot (all but none) holds one sound. One played
    with `bNoOverride` keeps it: a new one isn't played. Otherwise a new
    one takes it.
  - **Channels:** 16 (the .ini's `Channels`). With all taken, a new sound
    takes the least important one's if that's no more important than it:
    importance is its volume × (1 − distance / radius), at least 0.0001
    of it; else it isn't played.
  - **Loudness:** its volume × (1 − distance / `Radius`), in hundredths of
    the driver's 127, at most 100; nothing beyond `Radius`. The SFX or
    speech volume (a sound named `DIALOG`) and the master go over it.
  - **Side:** a flat sound's, the driver's: the angle off where the
    camera faces (one behind heard as if in front), less within a tenth
    of the radius, as Miles' 0..127. A positional one's, its 3D
    provider's: where the driver hands it the sound (in the camera's
    axes, x the other way with `ReverseStereo`; without `UseSurround`,
    the default, every sound ahead), its x over its whole distance
    (height pulls it to the middle too) through a table of 127 −
    acos(...) × 127 / π; then Miles' pan law, the near side full and the
    far side 2 × pan / 128 toward the middle.
  - **Following:** it's heard from where its actor is; one destroyed is
    heard where it was.
  - **Looping:** a sound whose name has `LOOPING` (the `Looping` groups)
    goes round until something takes its slot.
  - **Flat or positional.** A sound whose actor has a `LowPass` or
    `HighPass` (Hz) goes through the driver's flat (2D) mixing with Miles'
    filter on it: a 2nd-order Butterworth low-pass or high-pass, both in
    the one place, so a high-pass takes a low-pass's. The rest go
    through Miles' positional (3D) provider, "Miles Fast 2D Positional
    Audio" (`Mssfast.m3d`, from the install's `redist`). A voice on the
    communicator (`DS9_Hud_Mesh_Actor`) is high-passed at 600 Hz and
    1.3 times as loud; `PlaySpeech`'s `bForceNoReverb` high-passes it at
    20 Hz only to put it on the flat path, out of the reverb. (`UseFilter`
    doesn't decide it.)
  - **Walls:** either kind is quietened by its actor's
    `ObstructionFactor` (Pawn 0.4, CTVE_Pawn 0.7) while the level's walls
    stand between it and the camera. With `UseSurround` (off by
    default), a positional one behind the camera is at 71% (the
    provider's; without it the driver hands every sound over as ahead).
    The provider's own distance falloff never comes in (the driver sets
    its near and far both to twice the radius).
  - **Doppler:** its actor's `Velocity` less the listener's (the
    player's `ViewTarget`'s, else the player's); none for a voice. A
    flat sound's pitch is the driver's: times 1 − its speed toward the
    camera / `DopplerSpeed` (35,000), within 0.5 to 2. A positional
    one's is the provider's: the whole speed, not along the line, over
    a speed of sound of about 62,100 a second (0.355 / (0.355 ± speed ×
    5.715e-6), lower moving away), unclamped; away or toward judged
    from where it's handed the sound (ahead, without `UseSurround`).
  - **Reverb** (`UseReverb`): the room where the camera is: in a water
    zone room 22 (Underwater) at 0.5, else the zone's `ZoneRoomType` at
    its `ZoneRoomFactor` (which the zone's script caps at 0.2; room 0
    none). Every positional sound goes through Miles' reverb filter
    (`Reverb3.flt`) with that room's settings, its wet share the zone's
    level of the room's own mix: a Schroeder reverb, fed what the sound
    mixes once its volume and side are on (the two sides summed: one in
    the middle twice as strongly as one at a side), low-passed at the
    room's damping (of a
    quarter of 22,050 Hz, at least 200), through four combs side by side
    (29.7, 37.1, 41.1, 43.7 ms, each fed back at 0.001^(delay / the
    room's time)), then all-passes of 5 ms and 1.7 ms (0.7), the second
    fed a quarter of the first on the left, 0.15 on the right. The
    rooms are EAX 1's 26 (Room: 0.417 wet, 0.4 s; Hangar: 0.5, 10 s;
    Underwater: 1, 1.5 s, all damped). The zones' `bReverbZone`,
    `MasterGain`, `CutoffHz`, `Delay` and `Gain` aren't read. A flat
    sound and the music have none. Once a sound's data has run out its
    reverb goes on through silence: the provider streams it in 32 KB
    buffers and pads with silence until the sixth refill, about five
    buffers (1.9 s for a 22,050 Hz sound, twice that at 11,025 or for one
    of 8 bits). `unreal play`'s `sounds` says the room.
  - **The provider** (read from Miles.dll): with `Use3DHardware` the
    driver tries the `AudioProvider` named, then two it knows, then
    every 3D provider in the install's `redist\win32`, taking the first
    that opens; none, and every sound plays flat. The install puts only
    Miles' fast 2D positional provider (`Mssfast.m3d`) there (the
    hardware ones, EAX, A3D, DirectSound 3D, are in `redist\win32\Extras`,
    which Miles doesn't look in), so that's the one: Reluva's.
    `UseFilter` does nothing (the filters always open), and the driver
    always mixes at 22,050 Hz whatever `OutputRate` says. The provider
    is read to refuse sounds in stereo; the game has none (`unreal check`:
    of 2,868 sounds, none stereo, 2,836 IMA ADPCM, which it plays, so
    they reach it decoded).
  - **The AI hears it** unless `bNoHearNoise`, or the sound is played
    from a function declared `simulated` (the function calling, not
    the actor's `Role`; state code isn't one), or on a network client
    (`PlayOwnedSound` never; `PlayDummySound` always):
    each pawn but the players, the actor and its owner, if the actor has
    an owner and it or its owner is a player, a weapon or a projectile.
    Heard at 1 − (distance / `Radius`)², with 0.65 off through walls,
    if that's above 0 and above 1 − `HearingThreshold`: `HearNoise`. A
    CTVE_Pawn whose enemy isn't the actor first gets `DLG_Command`
    `FHE`. `PlayDummySound(Volume, Radius)` is that noise alone.
- **Ambient sounds:** each actor's `AmbientSound` with a `SoundVolume`
  above 0. It starts when the camera comes within 25 × (`SoundRadius` +
  1) units and stops when it leaves (starting afresh on coming back).
  Its volume is 2 × `SoundVolume` × `AmbientFactor` (0.7) / 255; a
  Keypoint's (the `AmbientSound` actors) times the level's `AmbientBus`
  / 128; a light's times its `LightBrightness` / 255. Its pitch is
  `SoundPitch` / 64. Then loudness and side as a played sound's.
- **Music** is a sound whose name has `MUSIC`, or a streamed one (the
  MP3s in `mp3/`, [sound.md](../formats/sound.md)). It's heard
  alike everywhere. A stream is at the music volume, twice as loud as
  the others before it (`MusicVolume` 200 of 255 puts a track at full).
  - The player's `CurrentMusic` names the track. The start sets it
    (`OpeningSoundTrack`).
  - A music trigger (`CTVE_MusicEvent`) plays a track through the HUD's
    `PlaySound`. Played while it's playing, it goes on. Another starts at
    once, at full volume, where the HUD's `StartTimeOffset` says, while
    the old one fades out over 3 seconds.
  - Setting `CurrentMusic` to None fades it out over 3 seconds.
  - When a track ends, `CurrentMusic` is None again. The filler music
    (`CTVE_FillerMusic`) waits for that, then plays its own as an
    ambient sound: heard alike anywhere in its range, faded in and out
    over 3 seconds, and not started while a track plays.
  - A loaded game starts its music again.
- **Voices:** a dialogue line's Sound (its record, in the package of its
  table's name) plays through the speaker's `PlaySpeech`
  ([dialogue.md](dialogue.md)): on its talk slot, as far as 1100 units, at the speech
  volume (its package's name has `DIALOG`).
- **Lip-sync** is the audio driver's (Miles.dll), for any sound an
  actor plays whose WAV has a `phnm` chunk (every voice of the dialogue
  tables: [dialogue-tables.md](../formats/dialogue-tables.md)), not only
  dialogue:
  - The sound's clock is the level's time (`TimeSeconds`: stopped while
    paused, slowed by `TimeDilation`) from the driver's last update, less
    the actor's `StartTimeOffset`. Each update looks at the window from
    the last update's time to now, the start in and the end out.
  - Walking the sound's words in order, the first that gives something
    is the one, so at most one shape a sound an update and the earliest
    (the rest of a fast frame's phonemes are skipped). A word ending in
    the window is silence, (0, 1), unless the next word's first phoneme
    starts exactly where it ends (words run together). Else a word the
    window's start or end is in gives its first phoneme starting in the
    window.
  - The phoneme's IPA letter gives `C3DSpeechEvent(shape, weight)` to
    the actor that played it: 1 for i and ɪ (0.2), æ ɑ ɔ ə ʌ (0.8), e ɚ ɛ
    (1); 2 u w ʊ; 3 b m p; 4 f v; 5 s z ð ʃ ʒ ʤ ʧ θ; 6 d g h j k l n t
    ŋ ɫ ɹ ɾ; 7 o. Others give nothing, though they use the update up: the
    voices' r (ɻ, 4,252 of their 73,513 phonemes), h (ɦ, 779) and g (ɡ,
    553) never move the mouth, and nothing in them is e or o.
  - When the sound stops (it ends, a new one takes its slot, a line is
    cut off), (0, 0); the update it ends in gives nothing else.
  - The script side (`Pawn.C3DSpeechEvent`) tweens the face's `Phonemes`
    channel to the shape's frame (shape × 0.033 seconds) over a tenth of
    a second, and now and then nods or tilts the head while the weight
    isn't 0.
- **`GetSoundDuration(Sound)`** is its length; `CTVE_Dialog` times its
  lines by it.
- **Footsteps** go by the floor's material: `GetSurfaceTexture(Start,
  End)` is the texture of the level's surface a line meets (`TexMaterial`:
  cement, carpet, metal, water...). The surface is the node whose plane
  the trace stopped at, or the coplanar one whose polygon holds the
  point.
- **The volumes** are the game's settings (`[Engine.Engine]
  AudioDevice`'s section: Miles, in Unreal.ini `MasterVolume=160`,
  `SFXVolume=160`, `MusicVolume=200`, `SpeechVolume=160`, each out of
  255). Each kind's is under the master's, and the options page changes
  them as it goes. So do `UseStereo` and `ReverseStereo`.
  `SetMasterVolume` sets the master for now (a level fading out), and
  `KillSound` stops every sound.
- **A WAV's own loop:** a looping sound with a loop in its `smpl` chunk
  plays through once to the loop's end, then round the loop (its first
  loop, start to last frame). Five of the game's sounds have one
  (`unreal check`): the cantina's and a big fire's loops, the underwater
  sound and the two breathing sounds.

On M04_KiraL1, in the first seconds:
- the vents' and engines' hums and the lights' buzz (37 ambient sounds);
- the lift, footsteps on cement, carpet and metal, the consoles'
  telemetry, the communicator's chirp and Kira's line.

M01_KiraL1A opens with its music ("evil"), and CM01a_SW, the Defiant
at warp, with the tragic cue.

`unreal play` mixes offline (`wav <file>`: what was heard since the last,
and how loud each second; `sounds`: what's playing, and the last
sounds started).

## In Reluva

`vm/vm_sound.cpp` takes what the scripts play; `audio/level_audio.cpp`
plays it through the shared mixer, whose filters and reverb are
described in [mixer.md](../../../../../common/audio/mixer.md).

## Sources and checks

- **Where the facts come from:** Engine.dll (`execPlaySound`,
  `CheckHearSound`) and The Fallen's audio driver, Miles.dll
  (`MilesAudioSubsystem`), read for what they do; the install's `redist`.
- **How it was checked:** in play, and offline with
  `reluva-tool unreal play`'s `wav` and `sounds` (above).

## Open questions

- A level's `Song` isn't played: The Fallen's driver never reads it
  ([sound.md](../formats/sound.md)).
