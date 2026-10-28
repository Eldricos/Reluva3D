# DirectMusic segments, styles and bands (`.sgt`, `.sty`, `.bnd`)

Microsoft DirectMusic's interactive music, as Midtown Madness 2 uses it
(`aud/dmusic`: 62 segments, 17 styles, a band, 14 DLS collections; see
[dls.md](dls.md)). Read and played by `common/audio/directmusic.cpp`.

## How this was worked out

- The forms, chunks and structures are Microsoft's published DirectX SDK
  documentation (learn.microsoft.com, "previous-versions": DirectMusic
  File Format, the Segment, Track, Style and Band forms, the Reference
  List, the track chunks, and the `DMUS_IO_*` structures), as are the
  music value's fields, the play mode flags, the command types, the
  embellishment and pattern-repeat values, the grid-to-music-time formula
  and the randomisation ranges' formula.
- The game's files are older (DirectX 7) versions of several structures,
  shorter than the documentation's; each chunk's size says which (below).
- What the documentation leaves open was settled against the game's own
  files: which nibble of a music value is which, the chord a segment
  without a chord track plays over, which variations are in use, the
  curve types. How each was settled is given where it's used.

## Music time

768 ticks to a quarter note. A beat is 768 x 4 / (the time signature's
beat note) ticks; a grid (`wGridsPerBeat` to a beat) a beat's share. A
note at grid g and offset o is at (g / grids) beats + (g % grids) grids
+ o ticks (Microsoft's formula, in whole numbers).

## Segment (`RIFF DMSG`)

`segh` (24 bytes here, the documented header's first six fields): u32
repeats, then music times: length, play start, loop start, loop end, and
a resolution. Then `LIST trkl` of tracks, each `RIFF DMTK`: a `trkh`
(class, position, group, and the data chunk's id or list type), then
the data. The game's segments have:

| Track data | Segments | Contents |
|---|---|---|
| `LIST sttr` | 62 | `LIST strf`: `stmp` (time), `LIST DMRF` (the style: `name`, `file`) |
| `RIFF DMBT` | 62 | `LIST lbdl` of `LIST lbnd`: `bdih` (time), `RIFF DMBD` (a band) |
| `tetr` | 62 | u32 record size (16), each: time, 4 bytes, double: beats a minute (120-160) |
| `cmnd` | 61 | u32 record size (12), each: time, u16 measure, beat, command, groove level, groove range, and a byte the old format doesn't use (223 throughout) |
| `LIST cord` | 1 | `crdh` (key root << 24, key scale), `crdb` chords: u32 size of the chord (name, 16 UTF-16 characters, then its time), u32 subchords, u32 their size, then each: chord pattern, scale pattern, inversion points, levels, chord root, scale root |
| `seqt` | 1 | chunks inside: `evtl` (MIDI events: time, duration, channel, offset, status, two data bytes) and `curl` (curves) |
| `tims` | 1 | time signatures |

`bigair.sgt` is a sequence (a motif for a jump); every other segment is
style-based: its notes come from its style, chosen by its command track.

Each tune has a segment for each state of play
(`aud/dmusic/csv_files/singlerace.csv` and `singleroam.csv`, one row per
tune: start, return, idle, idle with cops, cop chase, pause, results; the
cruising file has cop chase before idle with cops, and no results).
They play for long: 99 repeats of their loop for most.

## Style (`RIFF DMST`)

`styh` (12 bytes: the time signature, then the tempo as a double, packed
with no padding), then:

- `LIST part`, each: `prth` (154 bytes, DirectX 7's: time signature, 32
  variation choice words, the part's GUID, u16 measures, play mode,
  inversion limits), `note` (u32 record size 22, each: grid, variation
  bits, duration, offset, music value, velocity, time, duration and
  velocity ranges, inversion group, play mode) and `crve` (u32 size 28,
  each: grid, variation bits, duration, reset duration, offset, start,
  end and reset values, type, shape, controller, flags).
- `LIST pttn`, each a pattern: `ptnh` (10 bytes: time signature, groove
  bottom and top, u16 embellishment, u16 measures), `rhtm` (chord
  rhythm), its name, and `LIST pref` of `prfc` (22 bytes, DirectX 7's:
  the part's GUID, u16 logical part, which is its channel here, variation
  lock, subchord level, priority, variation order).
- `RIFF DMBD` bands.

The game's patterns are plain (158), end (14) and motifs (11, played
over the music by the game, not chosen by the command track).

## Band (`RIFF DMBD`)

`LIST lbil` of `LIST lbin`: `bins` (40 bytes, DirectX 7's: patch,
assigned patch, 128 note bits, channel, flags, pan, volume, u16
transpose, channel priority) and a `LIST DMRF` naming the DLS file. The
patch is MSB << 16 | LSB << 8 | program: the game's are bank 1 (LSB 1),
as their DLS instruments are. Instruments with no DLS reference name
General MIDI ones, which the game's music doesn't play.

## Music values

A part's notes are music values, made a MIDI key over the chord playing
by the play mode (the part's, unless the note gives its own; 16 means
the part's). The game's parts are all 14 (`ALWAYSPLAY`: chord root,
chord and scale intervals) or 0 (fixed: the value is the key).

The four 4-bit fields, from the top: octave, chord position, scale
position, accidental (signed). Microsoft lists them in that order without
saying it's the bit order; it is, as the game's drum parts show: read so,
their notes land on keys their kits have sounds for (below).

The key: octave x 12 + the chord's root + the chord position's interval
(its nth note above the root) + as many steps up the scale as the scale
position + the accidental. Past the chord's top, normal-chord mode plays
nothing; always-play mode counts chord positions as every other note of
the scale (Reluva's reading of "a second try in melodic mode").

**The chord with no chord track.** 61 of the 62 segments have none, and
Microsoft doesn't say what plays then. Every part is chord-relative, the
drums too, so it matters. Checked against the drum kits' key ranges:
taking the chord as C major 7 (C, E, G, B) over the C major scale, 95-100%
of every style's notes land on a key its instrument has a region for
(London's 23,414 of 23,422; with a C major triad 22,410; with a single
C, 16,769). The few misses are parts whose band differs in the segment
from the style's. So: C major 7 in C major.

## Playing (Reluva's performance)

- **Commands**: at each measure the command there, if any, sets the
  groove level (randomised within its range) and the embellishment asked
  for (fill, intro, break, end).
- **Patterns**: a pattern plays out its measures; at a command (or its
  end) another is chosen: of the embellishment asked for (else a plain
  one), whose groove range holds the level (else the nearest), at
  random. Motifs aren't chosen. How the original weighs `bDestGroove` and
  the chord rhythm isn't documented, and isn't used.
- **Variations**: each part plays one of its variations through a
  pattern. In use are the variations whose choice word has any chord bit
  (the game's are `0x3fffffff`, all, or `0x20000000`, the mode bit
  alone: not in use). The part reference's order applies (in order, at
  random, from a random start, never the same twice); parts with the same
  lock id play the same one.
- **Notes**: at their grid and offset, randomised by their time,
  duration and velocity ranges (Microsoft's range formula: 0-190 as is,
  then steps of 5, 10 and 50 ticks, up to 1,650; the time and duration
  within plus or minus half of it).
- **Curves**: type 4 is a controller curve (its controller: 7 volume, 10
  pan, 11 expression, 1 mod wheel, 64 sustain), type 3 pitch bend (14
  bits, two semitones). The game uses shapes 1 and 4; their numbers
  aren't documented, and Reluva takes 0 linear, 1 instant, 2 exponential,
  3 logarithmic, 4 sine (the game's pan sweeps are 4).
- **Bands** set each channel's instrument, volume, pan and transpose as
  their time comes; a segment with none uses its style's first.
- **Tempo**: the segment's tempo track, else the style's.
- **Loops**: from the loop end back to the loop start, as many times as
  the segment repeats; at its length it ends.
- **Changes**: a new segment starts at the next measure of the one
  playing; the notes sounding are let go.
- The synthesizer's whole mix is taken down to 0.35, so the busiest
  bands stay under full scale.

Not read: `rhtm`, markers, resolutions, anticipations, chordmaps, audio
paths, scripts. Motifs (the big-air jingle) aren't played yet.
