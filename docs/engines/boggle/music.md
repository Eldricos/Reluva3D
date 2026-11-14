# Classic background music

Choose **Classic music** in the launcher's per-game options: no music (default),
a specific installed track, or Random track. The fifteen short-loop WAV tracks
are loaded from the owner's game folder. Titles come from its English language
DLL at runtime; missing titles fall back to numbered labels. No retail display
text or audio is included in the repository.

Music starts with the first round, loops through play, manual pause/lid motion, review and
results, and continues across replay without restarting its cursor. Leaving the
engine stops audio before its mixer is destroyed. Returning to game selection
also stops round audio; entering Classic again restarts the same selected track.
A random choice stays fixed across menu re-entry within one engine launch. Music and sound effects are
independent: disabling effects still allows music, and selecting no music does
not disable effects. Missing audio hardware remains nonfatal. A selected track
that is missing or malformed reports an error. No music requires no music files.
The native implementation uses the shared bounded WAV decoder and mixer.

The launcher saves `classic_music` as `none`, `random`, or a zero-based track
number `0`..`14`; absent settings mean `none`. Invalid values are rejected.
Random chooses once per launch using a native clock-seeded generator, separate
from the board generator. This does not reproduce the original C runtime random
sequence. Settings apply on the next launch. CD-device music and in-game sound
options are not implemented.

## Evidence

These are observations from the owner's executable, SHA-256
`5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`.
Addresses are virtual addresses. Analysis is permitted under the repository's
clean-room process; the native implementation executes no original code.

- Sound-options loader `0x41d9e0` reads Music with default zero at `0x41da14`.
  Mode one uses the CD device; mode two enters WAV loading at `0x41dab2`.
- The WAV branch reads RandomTrack at `0x41dad3`, default false. Random selection
  loads count resource `0x32c8` (15) and selects a bounded index at
  `0x41db1b`..`0x41db33`. Fixed selection reads Track at `0x41db3c`..`0x41db54`.
- `0x41db5f` adds that index to resource base `0x332d`; the fifteen resources
  identify installed WAV paths. Corresponding display titles start at `0x32c9`.
  Wave creation at `0x41db9b`..`0x41dbd0` stores the music object at field `0x21dc`.
- Sound effects use a separate Sound setting, default true, at `0x41dc43`.
  Thus effects mute does not imply music mute.
- Classic round setup `0x429510` calls `0x45c190` on music at `0x429543`;
  end handling `0x41ac20` also calls it at `0x41ac49`. The music routine calls
  DirectSound Play with looping flag one and does not reset the buffer cursor
  in its normal branch. Native replay therefore preserves the music channel.
  Options handling stops/reloads/replays music at `0x417387`..`0x4173c4`.

The owner heard music in the original VM. Exact track choice and continuity
across transitions have not yet been explicitly corroborated by listening.
The owner subsequently confirmed distinct seven- and eight-letter word cues
in the VM, each replacing the standard writing sound; see [presentation](presentation.md).

## Validation

Invented PCM fixtures test all fifteen selections, music with effects disabled,
Ready-screen silence, looping beyond clip length, continuity across replay,
repeatable native random selection, invalid settings, malformed media and audio
callback shutdown. A scripted engine run exercises music-only play through
start, manual finish, results and replay with graphics initialized before audio.
Existing gameplay and presentation records remain unchanged.

`reluva-tool boggle music-check <folder>` decodes every track and reports only
frame counts and a checksum of rates, channel counts, frame counts and PCM in
explicit little-endian order. Optional game-data tests keep raw output outside
all Git worktrees and commit only metadata. The owner tested this version on
Windows and confirmed that music and sound effects work.

Both supplied data copies decode to 15 tracks / 3,108,331 audio frames,
checksum `f0a87351902d54d16a2da6a9b64dcb63`. All 15 Boggle CTest checks pass
with the supplied install; the normal full suite has 29 passing checks and 17
optional data checks skipped when no game paths are configured. Boggle unit
and engine tests and the actual-track decoder check also pass ASan/UBSan
(with LeakSanitizer disabled because the execution tracer does not support it).

An external music-only SDL run using dummy video/audio mixed 270,336 output
frames with nonzero PCM through the real backend callback. This validates
software integration through a dummy device. The owner's subsequent Windows
playback confirmation provides a separate native listening check.

The owner's subsequent VM check confirms that explicit Pause leaves music
playing, while loss of window focus pauses the game and stops its music.
Native focus handling now suspends the music channel without discarding its
sample position and resumes it on focus gain, including while manually paused.
Repeated focus events are harmless. Effects remain independent; their behavior
was not part of this VM observation. See [pause flow](pause-presentation.md).
