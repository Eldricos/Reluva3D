# CD audio

## What it is

`src/emulation/x86/cdaudio.cpp`: a CD drive's audio, as programs of the
1990s played their music from the disc's audio tracks, for programs run in
the [x86 emulator](README.md): WINMM's MCI `cdaudio` device
(`mciSendCommandA`) and the CD's volume through an auxiliary device
(`auxGetNumDevs`, `auxGetDevCapsA`, `auxGetVolume`, `auxSetVolume`).
Played through Reluva's mixer as music.

## What Reluva supports

- **The disc** is the engine's (`CdAudio::disc`), asked each time the
  device is opened and when the engine changes it
  (`CdAudio::discChanged`, which stops what was playing): its number of tracks and each audio track's length
  and sound. `CdAudio::discIn(folder)` makes one from a folder holding
  either
  - a cue sheet and its image files (`common/cue.h`), the audio tracks
    read from the image as they're played; the data track's image may be
    left out, as when the CD's files have been copied instead; or
  - tracks ripped from it as `trackNN.wav`, `.mp3`, `.ogg` (Ogg Vorbis)
    or `.flac` (track 1 is the data track); `reluva-tool cue-rip` makes
    `.wav` ones.
- **MCI commands:** `MCI_OPEN` (by type name `cdaudio` or type ID),
  `MCI_CLOSE`, `MCI_SET` (time format: milliseconds, MSF or TMSF),
  `MCI_PLAY` (from, to, or both; a range past the disc's end is
  `MCIERR_OUTOFRANGE`), `MCI_STOP`, `MCI_PAUSE`, `MCI_RESUME`, `MCI_SEEK`,
  and `MCI_STATUS`: length (the disc's or a track's), position, number of
  tracks, mode, media present, ready, time format, current track, and
  whether a track is audio. Any other device type fails to open
  (`MCIERR_INVALID_DEVICE_NAME`).
- **Where it is:** the tracks end to end, the data track taking no time.
  Play follows the process's clock, so a program that waits for a track
  to end (Jedi Knight asks for the mode every five seconds, and plays
  again when it has stopped) sees it end on time, heard or not.
- **The volume:** one auxiliary device, the CD's (`AUXCAPS_CDAUDIO`,
  left and right volume); what's set turns the music up or down at once.
- **Not supported:** `MCI_NOTIFY`'s message at the end of a play (no
  program using it has been met), `mciSendStringA`, and a disc's
  pregaps, which aren't heard.

## Sources and checks

- **Where the facts come from:** Microsoft's documentation of MCI and the
  auxiliary audio functions (`mmsystem.h`): the commands, flags,
  structures, time formats, modes and errors.
- `testCdAudio` (`common.unit.windows`) makes a disc of three ripped
  tracks and drives it as Jedi Knight does: opened by type, set to TMSF,
  the number of tracks, a track's length, a track played to the next one
  heard through the mixer, its mode as the clock passes its end, the
  volume set to 0 through the auxiliary device, a range off the disc's
  end, another disc put in, the device closed, and another device type
  refused; then a disc of an Ogg Vorbis and a FLAC track, their lengths
  and sound. `testCue`
  (`common.unit.archives`, its "cd images" case) reads a cue sheet
  without its data track's image.
- **Confidence:** published.

## Used by

- **`sith`**: Jedi Knight's music, from each CD's audio tracks
  ([hosting.md](../../engines/sith/games/jk/systems/hosting.md)).
