# DLS collections (`.dls`)

Downloadable Sounds, Level 1: the instrument sets DirectMusic music plays
on (Midtown Madness 2's `aud/dmusic/*.dls`: 14 collections, 204
instruments, 368 waves). Read by `common/audio/dls.cpp`, played by
`common/audio/synth.cpp`.

## How this was worked out

DLS is a RIFF form published by the MIDI Manufacturers Association: the
Downloadable Sounds Level 1 specification (v1.1b) gives the chunk grammar,
the structures, the connection sources and destinations with their values,
the default articulation and the units (time cents, relative pitch and
gain, 0.1% units). Each chunk's
fields below were checked against the game's files: every chunk's size
is the one the layout gives (`insh` 12 bytes, `rgnh` 12, `wsmp` 20 plus
16 per loop, `wlnk` 12, `art1` 8 plus 12 per connection), every pool
table offset finds a wave, and the instruments' tunings, played back,
give the pitches their keys should (London's strings at keys 45 and 57:
110 and 220 Hz; its Moog bass at 33 and 45: 54 and 109 Hz).

## Layout

`RIFF 'DLS '` holds:

| Chunk | Contents |
|---|---|
| `colh` | u32: instruments |
| `vers`, `dlid`, `LIST INFO` | version, an id, `INAM` the collection's name |
| `LIST lins` | the instruments, each a `LIST 'ins '` |
| `ptbl` | pool table: u32 its header size (8), u32 cues, then each cue's offset of its wave's `LIST` from the start of `wvpl`'s data (after its type) |
| `LIST wvpl` | the waves, each a `LIST wave`: `fmt ` (PCM; here all 16-bit mono at 8-44.1 kHz), `data`, `wsmp`, `LIST INFO` |

An instrument (`LIST 'ins '`):

- `insh`: u32 regions, u32 bank, u32 program. The bank is the MIDI bank
  select (CC 0 in bits 8-14, CC 32 in bits 0-6), with bit 31 set for a
  drum kit. The game's are all bank 1, programs 91-110 and the like.
- `LIST lrgn` of `LIST 'rgn '`: each `rgnh` (u16 lowest and highest key,
  lowest and highest velocity, options, key group), optionally its own
  `wsmp`, `wlnk` (u16 options, u16 phase group, u32 channel, u32 the pool
  table cue of its wave) and its own `LIST lart`.
- `LIST lart` with `art1`: u32 header size (8), u32 connections, each
  u16 source, control, destination, transform and s32 scale.

`wsmp`: u32 size (20), u16 the key the wave plays at as recorded, s16 fine
tune (cents), s32 gain (1/655360 dB, negative quieter), u32 options, u32 loops; each loop
u32 size, type, start and length in samples.

## Playing them (Reluva's synthesizer)

- A note plays the first region whose keys hold it. Level 1 has no
  velocity ranges: the game's files leave them 0-0, taken as any.
- Its pitch is the wave's rate, moved by (key - unity key) x 100 cents
  (the key-number-to-pitch connection's scale / 128 per key, where one
  is given), the fine tune and any pitch connection, the pitch envelope
  and the LFO.
- Loops repeat from start to start + length. One wave loops past its
  end (London's Moog bass: from 3,455 for 3,456 of 3,456 samples); such a
  loop is taken as ending at the end, as long, so the whole wave loops.
- A region in a key group stops the group's other notes on the channel.
- Volume: velocity squared, the gains, and channel volume squared times
  expression; panned by the channel's pan plus the articulation's.

Connections used by the game's instruments (destination: count), and how
Reluva takes their scales (all in 65536ths):

| Destination | Count | Read as |
|---|---|---|
| 0x001 attenuation | 8 | gain, 1/655360 dB, negative quieter (from the LFO: tremolo depth) |
| 0x003 pitch | 17 | cents (from the key number: per 128 keys; the LFO: vibrato, with the mod wheel as control 0x81 or not; envelope 2: its depth) |
| 0x004 pan | 67 | tenths of a percent, -500 left to 500 right |
| 0x104, 0x105 | 14, 11 | LFO frequency (absolute pitch cents: 440 Hz at 6900) and delay (time cents) |
| 0x206, 0x207, 0x209 | 30, 41, 130 | volume envelope attack, decay, release (time cents: 2^(v/1200) s; 0x80000000 none) |
| 0x20a | 39 | volume envelope sustain, tenths of a percent of the 96 dB range (20%: -76.8 dB) |
| 0x30a, 0x30b, 0x30d, 0x30e | 3, 4, 1, 2 | pitch envelope attack, decay, release, sustain |

The volume envelope's attack rises evenly; decay and release fall 96 dB
over their times (the decay holding at the sustain level). Envelope
stages are at least 4 ms, so notes don't click. Sources 0x002 (velocity)
and 0x004 (envelope 1) as connections, and transforms, aren't applied.
