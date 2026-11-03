# Classic round presentation and sound

This pass adds the owner's Classic roll/lid animation and WAV effects to the
native Original/Master round flow. This document describes the round transition and audio behavior. Installed
scenes, panels and the later dice projection are documented in [Classic layout](layout.md);
intro/mode playback is documented in [movies](movies.md).

## Runtime flow

**Enter / Start** plays the selected variant's `Anims/ClsO1All.avi` or
`ClsM1All.avi`, opening/rolling frames 0..43 at a 100 ms cadence. Words and
board changes are blocked during the transition. The round clock starts when
opening completes, so animation/loading does not consume playing time.

At timeout or manual End Round / F1, word input stops immediately. Frames
44..54 close the lid, then results or pending-word review appear. An Enter or
New Board event cannot bypass displaying results; dictionary choices are
ignored until the prompt is visible. Escape and window close work throughout,
and returning to the launcher stops the audio callback before destroying its
mixer. Manual finish also supports Not Timed.

The native opening phase lasts 4.4 seconds and closing lasts 1.1 seconds.
Scheduling uses elapsed time rather than blocking sleeps. Late frames advance
to the current image; the timer still starts at the actual end of opening.
Decoded 640x480 frames are scaled into the native 800x600 framebuffer. Original
moving 3D cube overlays/camera movement during the opening and closing clips
remain unimplemented. The playable board uses a native projective layout and
indexed hourglass animation; see [Classic layout](layout.md).

The launcher's **Classic round animations** and **Classic sound effects**
checkboxes are on by default. Disabling animations preserves immediate-start
behavior and skips transition playback. With installed artwork enabled, the
scene still loads from AVI and the hourglass still animates; sound can remain enabled with
a single shake cue. Disabling sound skips WAV loading/device startup. A missing
audio device permits silent play. Missing/corrupt enabled media fails explicitly;
no external codec DLLs or original executable instructions are used at runtime.
Settings are per launcher target (`classic_animations` / `classic_sound`, 0/1).

## Cues

| Trigger | Runtime WAV |
|---|---|
| Roll/open | `Waves/Shake.wav`, looping through the roll portion |
| Select a touching cube / release selection | `ClkDown.wav` / `ClkUp.wav` |
| Newly recorded word below seven letters | `Writing.wav` |
| Newly recorded seven-letter word | `Seven.wav` |
| Newly recorded eight-plus-letter word | `Eight.wav` |
| Countdown floor-second buckets 10..1 | `Tick.wav`, once per observed bucket |
| Countdown zero bucket / expiry | `GameOver.wav`, once per round |

A newly recorded pending/red word gets the same length cue as a dictionary hit;
invalid paths, too-short words and duplicate entries do not get recording cues.
Qu contributes its complete spelling to the letter count. Countdown cues do not
run in untimed rounds, opening, closing or results. No missed-tick burst is
played after a clock jump. The zero floor-second bucket can begin with less
than one second remaining; expiry does not replay the cue. Manual finish does
not invent a countdown-zero cue. Replaying resets cue flags. Repeated uses of
one cue restart its channel rather than accumulating copies of the same sound.

## Provenance

Executable SHA-256:
`5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`.
Addresses below are executable virtual addresses; original instructions are
analysis evidence, not an implementation template:

- Classic roll sequence `0x42ad50` loads the variant's `All.avi`, plays the
  roll portion through frame 30, and loads `Waves/Shake.wav`. Raise-lid handler
  `0x42a010` seeks to frame 30 (`0x42a0f6`) and advances through 43. Lower-lid
  handler `0x42a7c0` seeks to 43 (`0x42a8a1`) and advances through 54. Their
  playback loops wait on 100 ms increments despite the AVI's nominal 30 fps.
- Raise-lid completion at `0x42a70f` reads the current clock and invokes the
  game timer update, providing evidence for starting play after opening.
- WAV creation around `0x415f20..0x4163c4` identifies Tick, GameOver, click,
  Seven, Eight and Writing resources. Word handling at `0x40b044` chooses
  Eight for length >=8, Seven for >=7, otherwise Writing.
- Player countdown `0x44afa0` divides remaining milliseconds by 1000, checks
  buckets 0..10 and remembers which have sounded. Bucket zero selects
  GameOver; other buckets select Tick. This drives the native cue schedule.

The AVI decoder is independently implemented in shared code; see its
[format and reference checks](../../common/avi-video1.md). The existing shared
WAV decoder and mixer handle sounds. Retail files, decoded frames/audio,
disassembly, reference harnesses and recordings stay outside every Git worktree.
The owner subsequently heard startup shaking, a selection effect, writing for
words, ticking in the last ten seconds and ringing at expiry in the original
VM. A subsequent VM check confirmed that seven- and eight-letter words each
play their own distinct cue instead of the standard writing sound. This
corroborates the native length-based selection; the extension of Eight to
lengths above eight remains established by executable analysis. Precise opening/timer
alignment and the sub-second zero-bucket boundary were not measured; those
schedules still come from executable analysis and deterministic native tests.
The original also plays music, as the owner noted; native WAV loop music is
now supported through the independent [music setting](music.md).

## Validation

Synthetic codec tests cover Video 1 block modes, delta retention and malformed
input. Sound tests distinguish every length/countdown cue using invented PCM,
check repeated-cue restart, shake looping/stopping, missing-device fallback,
malformed media, muted mode and callback shutdown. Scripted engine tests cover
opening input guards, timer start after opening, unfinished input loss, closing
before results/review, boundary Enter/New Board handling and exit during opening.
Existing rule/UI regression cases explicitly disable media and retain their
prior records. No retail pixels, samples or word/face lists are fixtures.

An external native SDL run with dummy video/audio output rendered the flow and
produced nonzero PCM through the real backend callback. This checks software
integration through a dummy device. The owner subsequently confirmed that
native music and sound effects work on their Windows setup. Precise transition
timing and visual matching remain separate comparison work.

For both supplied expanded data copies, `presentation-check <folder>` reports
2 clips / 110 frames and 8 sounds / 146,905 audio frames, checksum
`631bbf21384eacdaa178b71ef2c7c056`. The committed optional game-data record
contains metadata only. Original's 737 installed files remain unchanged.
