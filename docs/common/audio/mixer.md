# The mixer

## What it is

`src/audio/mixer.{h,cpp}` (`reluva::audio::Mixer`) mixes the sounds every
engine plays: each a channel with its volume, pan and rate, under the
player's master volume and the music, speech and effects volumes. This
page so far describes what it can do to a sound on its way: filters,
reverb and muffling.

## What Reluva supports

- **Filters** (`setFilter`): a 2nd-order Butterworth low-pass or
  high-pass on a sound, at a cutoff clamped to 20 Hz up to half the
  output rate; one filter a sound, so a high-pass takes a low-pass's
  place.
- **Reverb** (`setReverb`): a Schroeder reverb on a sound, with how much
  of it is wet, the time for its tail to fall 60 dB, the cutoff its wet
  sound is damped at, a pre-delay, and how long it goes on through
  silence once the sound has ended (not looping), its echoes dying away.
  It's built as Miles' reverb filter (`Reverb3.flt`) does it.
- **Muffling** (`setMuffle`): 0 (as recorded) to 1, a one-pole low-pass
  whose cutoff falls from 20,000 Hz to about a kilohertz, for a sound
  heard through a wall.

## How it works

The reverb is fed what the sound mixes once its volume and pan are on,
its two sides summed (a sound in the middle twice as strongly as one at a
side), delayed, low-passed at the damping, then through four combs side
by side (29.7, 37.1, 41.1 and 43.7 ms, each fed back at 0.001^(its delay
/ the reverb's time), so its echoes fall 60 dB over that time), then two
all-passes in turn (5 ms and 1.7 ms, gain 0.7), the second fed a quarter
of the first's output on the left and 0.15 of it on the right. Out comes
the dry sound less the wet's share, and the wet.

A sound with a reverb tail plays on for the tail's length once its data
has run out, so the echoes die away rather than stop.

## Sources and checks

- **Where the facts come from:** the Butterworth and Schroeder designs
  are published; the reverb's lines and gains are Miles' reverb filter's,
  as read from The Fallen's audio driver for the Unreal engine
  ([sound.md](../../engines/unreal/games/ds9fallen/systems/sound.md)).
- **How it was checked:** the unit tests (`common.unit.mixer`, its
  "filters" test among them).
- **Confidence:** published; the reverb's lines from the code.

## Used by

- **`unreal`**: The Fallen's sounds, filtered and reverberated as its
  audio driver does
  ([sound.md](../../engines/unreal/games/ds9fallen/systems/sound.md)).
