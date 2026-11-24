# LEGO Loco: postcards (`.crd`)

> **Status:** decoded · **Code:** `src/engines/legoloco/formats/postcard.cpp` · **Checked on:** the CD's three English `Easter` cards and cards made and sent in the game, version 1.0.18.2 (November 2026) · **Confidence:** from the code unless marked

## Overview

A postcard is a file of 920 bytes: the game's card record, written
straight from memory (`0x442ae0`, read back by `0x442b50`), so bytes it
never set keep what the memory held: `0xCD`, the fill of Visual C++'s
debug heap, in the CD's cards. It holds who the card is to and from, a
message, a stamp, a background, and up to 128 stickers, each a clip art
picture placed on the card.

Where cards are kept, under `art-res\PostBag`:

| Folder | Cards |
|---|---|
| `Easter\<language>` | the CD's, by language (`Eng`, `Dan`...), named from `0910000.crd` |
| `Sort\Out` | posted, waiting for a mail train to collect them |
| `Sort\In` | received, numbered from 10000 *(seen, not traced)* |
| `Album` | filed in the album, with an index in `AlbIndex` |
| `Att_Out`, `Att_In` | on their way to and from another player: the files the game sends and asks for |

A file is named by its card's number, seven digits (`%07d.crd`).

## Layout

Numbers little-endian.

| Offset | Size | Value |
|---|---|---|
| `0x00` | 2 | `0x66`: the game refuses a card without it |
| `0x04` | 4 | set from the player's profile when a card is made (`0x442880`): its `+0x18`, which, if the profile keeps its file at `+4` as a card does, is the number at `+0x14` of the player's `<name>.usr` *(inferred)*; 1 for a player who had sent a card, 0 for one who hadn't *(seen, not traced)*: perhaps the cards sent so far; `0x5d` in the CD's cards |
| `0x08` | 4 | the card's number, as its file is named |
| `0x0c` | 21 | the addressee's name, ending in 0: `Anyone`, or a player of a network game |
| `0x21` | 21 | the sender's name, ending in 0 |
| `0x36` | 2 | set on a card that came with its own file in `Att_In`: filing it deletes that file and clears this (`0x42d931`) *(from the code)* |
| `0x38` | 4 | 1 on a new card (`0x442876`); copied with the card, read nowhere else found *(from the code)* |
| `0x3c` | 3 | 0 on a new card; copied with the card, read nowhere else found *(from the code)* |
| `0x3f` | 80 | the message, ending in 0; `...?` when none was written |
| `0x8f` | 1 | the stamp: clip art `S0nnn`'s number; 0 for none |
| `0x90` | 1 | the background: clip art `Rdnnn`'s number; 0 for none |
| `0x91` | 1 | the background's digit `d`, plus one |
| `0x92` | 768 | 128 stickers of 6 bytes |

### A sticker

| Offset | Value |
|---|---|
| 0 | its set: the clip art's category (high five bits) and digit (low three) |
| 1 | its number in the set, from 1; 0 for a slot not used |
| 2 | its centre's x, halved |
| 3 | its width, in pixels |
| 4 | its centre's y, halved |
| 5 | its height, in pixels |

The editor fills the slots in order, and when one's removed the rest
move down (`0x442e00`): the used ones come first, but every slot is
looked at (`0x442d30`, finding the sticker at a point, the last first).
The width and height are the picture's, as placed (`0x41c3c7`).

### The clip art's names

A sticker's set and number, a stamp and a background name a picture in
`art-res\Clipart` or the resource file's `CLIPART` folder (`0x445700`),
by the category `c` and digit `d` of the set and the number `n`:

| Category | Name | Example |
|---|---|---|
| 0 to 15 | `c` as a hex digit, `d`, `n` in three digits | 13, 1, 4: `d1004.bmp` |
| 16 to 25 | the letter `c` + `0x58`, `d`, `n` | 16, 0, 10: `h0010.bmp` |
| 26 to 29 | the letter `c` + `0x5a`, `d`, `n` | |
| 30 (backgrounds) | `R`, `d`, `n` | 30, 0, 2: `R0002.bmp` |
| 31 (stamps) | `S0`, `n` | 31, 30: `S0030.bmp` |

The files' own names on the CD mix cases (`D1004.BMP`, `00002.bmp`); the
game's are as the table writes them.

## Behaviour

Sent between players: a card posted to another player in a network game is collected by a
mail train and arrives in their `Sort\In`
([running.md](../systems/running.md#network-play)). On the way the game
sends a compact form of the record (its fields, then a count and only
the stickers used; read back into a card by `0x4428e0`), and the
receiver asks the sender (message `0x3ed`) for any clip art picture the
card names that it doesn't have (`0x438faa`), which comes in pieces
(message `0x3fc`, `0x439df0`).

## In Reluva

- `src/engines/legoloco/formats/postcard.cpp`: `parsePostcard` reads a
  card into `Postcard`, its stickers the slots used; `clipArtName` names
  a picture by its set and number.
- `reluva-tool legoloco crd <file | game folder>` prints a card, or
  every card in a copy, with its pictures' files, marking any the
  resource file doesn't have.

## Sources and checks

- **Where the facts come from:** `loco.exe`'s card class: its saving and
  loading, making a new card, adding, finding and removing a sticker,
  the copy from the compact form, the clip art names, and the network
  code that looks for a received card's pictures. The folders and
  numbers in Sort\In were seen in a network game played in Reluva.
- **How it was checked:** `reluva-tool legoloco crd` reads the CD's
  three English cards and a card made, posted and received in a network
  game played in Reluva: every sticker, stamp and background names a
  picture in the resource file. The unit tests
  (`legoloco.unit.postcards`) read a card built in the test, refuse a
  damaged one, and check the names of each category.

## Open questions

- What `+0x04` counts, and whether anything reads `+0x38` and `+0x3c`.
- The other languages' cards, and how the game draws a card.
