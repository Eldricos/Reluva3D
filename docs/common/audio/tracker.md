# Tracker music

## What it is

Reluva's player for tracker modules, `src/audio/tracker.{h,cpp}`: MOD
(ProTracker and its multi-channel kin), S3M (Scream Tracker 3), XM
(FastTracker 2) and IT (Impulse Tracker). The loaders
(`loadTrackerModule`) turn each format into one common form
(`TrackerModule`), which `renderTrackerModule` plays into a sound. It was
written from the formats' public descriptions.

## What Reluva supports

- **MOD:** M.K., FLTn and xCHN modules.
- **S3M.**
- **XM:** instruments, envelopes, delta samples.
- **IT:** samples or instruments, envelopes, compressed samples.
- **Rendering:** a song from an order, at the rate asked (44,100 Hz for
  the Unreal engine), to where it jumps back to an order already played,
  at most a given length (ten minutes for the Unreal engine). That point
  is the sound's loop.
- **Not done:** new-note actions (a new note cuts the old),
  auto-vibrato, pitch and filter envelopes, tremor, panbrello, 15-sample
  MODs and S3M's ADPCM samples.

## Sources and checks

- **Where the facts come from:** the formats' public descriptions.
- **How it was checked:** the unit tests (`common.unit.tracker`), on
  modules the tests build; not yet on a real song.
- **Confidence:** published unless marked.

## Used by

- **`unreal`**: the music modules (`.umx`) of Unreal Engine 1 games, on
  the data viewer's Sounds page. The Fallen has none
  ([sound.md](../../engines/unreal/games/ds9fallen/formats/sound.md)).
