# The Miles Sound System

## What it is

`src/emulation/x86/miles.cpp`: the digital sample functions of RAD Game
Tools' Miles Sound System (`mss32.dll`, its `AIL_*` functions), for
programs run in the [x86 emulator](README.md), played through Reluva's
mixer. Programs of 1998 to 2002 shipped Miles' DLL beside them; Reluva
doesn't load it, and stands in for the functions they import from it.

## What Reluva supports

Only the functions a program asks for: those Starfleet Command II
imports, 25 of them, and `AIL_sample_volume`, which its 2.0.3.6 patch's
program imports too. Each is found by its decorated name, as programs
import them (`_AIL_load_sample_buffer@16`: its arguments' bytes after
the `@`).

- **Start-up:** `AIL_startup`, `AIL_shutdown` (which stops every sample),
  `AIL_set_redist_directory` (where Miles' own DLLs would be; it returns
  the folder named before), `AIL_last_error` (always empty), `AIL_serve`
  (nothing to do), and `AIL_mem_alloc_lock` and `AIL_mem_free_lock`, from
  the process's memory.
- **The driver:** `AIL_waveOutOpen` gives one driver, whatever format it's
  asked for, as the mixer plays each sample at its own;
  `AIL_waveOutClose`, `AIL_set_DirectSound_HWND`, and
  `AIL_set_digital_master_volume`, which scales every sample.
  `AIL_minimum_sample_buffer_size` answers a tenth of a second of the
  rate and format asked for, in whole 256-byte pieces: Reluva's own
  choice, longer than the mixer waits before it starts a stream.
- **Samples:** `AIL_allocate_sample_handle`, `AIL_release_sample_handle`,
  `AIL_init_sample` (8-bit unsigned mono at 11,025 Hz, full volume, in
  the middle), `AIL_end_sample`, `AIL_sample_status` (2 done, 4
  playing), `AIL_set_sample_type`, `AIL_set_sample_adpcm_block_size`,
  `AIL_set_sample_playback_rate`, `AIL_set_sample_volume` and
  `AIL_set_sample_pan` (0 to 127; a pan of 64 is the middle), and
  `AIL_set_sample_user_data`, kept and not read back.
- **Formats** (`AIL_set_sample_type`'s format and flags): bit 0 of the
  format for 16-bit samples, bit 1 for stereo, bit 2 for IMA ADPCM, in
  blocks of `AIL_set_sample_adpcm_block_size`'s bytes, as WAV files' format
  0x11 has them; bit 0 of the flags for signed 8-bit samples.
- **Double buffering**, the way programs stream through Miles:
  `AIL_sample_buffer_ready` gives the buffer to fill next (0 and 1 take
  turns) or -1 while it's still to be heard, and `AIL_load_sample_buffer`
  queues a buffer after the others and starts the sample playing. A
  sample's play position follows the process's clock (`milliseconds`) at
  its rate, so a buffer comes free once it would have been heard. A
  sample that runs out of buffers is done, and waits for the next to be
  loaded. Loading an empty buffer queues nothing *(guessed)*.
- What's loaded is decoded to 16 bits and handed to the mixer as it's
  loaded, as a `SoundStream` (`audio::Mixer::playStream`): the mixer waits
  for a little of it, and again when it runs dry, so a program a little
  late filling a buffer isn't heard as a gap. Volume, pan and rate
  changes go to the playing stream.
- `Miles::bufferLoaded` tells a tool of each buffer loaded, decoded:
  `reluva-tool sfc host -sound` writes what each sample played to a WAV.

Not there: Miles' other sample functions (`AIL_start_sample`, loops,
3D providers), its MIDI (XMIDI) and its streams (`AIL_open_stream`), and
the MP3 and other codecs of its later versions. A program that imports
one stops at it, named.

## Sources and checks

- **Where the facts come from:** the functions' names and their
  arguments' sizes from the importing program's own table; what each
  does, the format bits, the status values and the volume and pan
  ranges from Miles' published function reference *(published)*; how a
  program uses them from Starfleet Command II's calls, as `reluva-tool sfc
  host -calls` lists them.
- **How it was checked:** Starfleet Command II's menu music, 16-bit
  stereo at 22,050 Hz (format 3) in 128 KB buffers, played through it:
  what the mixer was given is, byte for byte, the music zip's
  `MNU_A1.WAV` followed by `MNU_A2.WAV`, the segment its music table
  plays next *(confirmed)*. Its buffers were asked for and loaded in turn,
  0 then 1, then waited for. The ADPCM bit, the signed 8-bit flag and
  the status values aren't yet seen in a program's use.
- `testMiles` (`tests/common/windows.cpp`) opens a driver, allocates a
  sample, loads two 16-bit buffers in turn and checks which comes free
  as the clock moves, the samples the mixer hears, its status, an IMA
  ADPCM buffer decoded, and the sample ended and released.
- **Confidence:** published unless marked.

## Used by

- **`sfc`**: Starfleet Command II's music and sound
  ([hosting.md](../../engines/sfc/games/sfc2/systems/hosting.md#its-sound)).
