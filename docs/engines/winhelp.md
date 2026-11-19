# Windows Help files (`.hlp`)

The help files of Windows 3.1 to 98, compiled by Microsoft's help
compilers. Midtown Madness 2's is `MM2HELP.HLP` in its folder (2.7 MB:
183 topics, 135 with titles, and 24 pictures), opened by the menus'
Help button. Read by `common/winhelp.cpp`: the topics' titles and text.

## How this was worked out

The format is described in public by Manfred Winterhoff's `helpfile.txt`,
the description that comes with his helpdeco decompiler, which Reluva's
reader follows (Reluva has none of helpdeco's code). The description
leaves a few things out, or says them in a way that didn't fit this
file. Those were settled on `MM2HELP.HLP` with a throwaway Python script:
the reader's output had to read as English all the way through, with no
command left unknown, and each of the 2,698 records' "previous" had to
be the record before it (they all are). Those things are:

- A picture's size is a signed number (the compressed 2-byte form less
  0x4000): 19 for the windows embedded in the text, 4 for a picture by
  number (`|bm22`).
- A table cell is its column (-1 ends the row), then three bytes,
  then a paragraph as anywhere else. That was measured on the file's six
  tables.

## Layout

Little-endian throughout.

**The file** starts with a 16-byte header: magic `3F 5F 03 00`, where
its directory starts, the first free block (-1) and the file's size.

**Internal files.** The file holds a small file system. Each internal
file starts with 9 bytes, the space reserved and used (32-bit each) and
a flags byte, and then its contents. The directory is one of these: a
B+ tree of names (`|SYSTEM`, `|TOPIC`, `|PhrIndex`, `|bm0`...), each
with the offset of its file.

**B+ trees** have a 38-byte header:

| Offset | Contents |
|---|---|
| 0 | magic `0x293B` |
| 4 | page size (1,024 for the directory) |
| 6 | 16 bytes: the key's form (`z4`: a name, then a 32-bit value) |
| 26 | the root page |
| 32 | levels |
| 34 | entries |

Then the pages. An index page starts with 6 bytes: something not
known, its entries and its first child. A leaf page starts with 8
bytes: something not known, its entries, the previous leaf and the
next (-1 at the ends). Reluva goes down the first children to the first
leaf, then along the leaves.

**`|SYSTEM`**: magic `0x036C`, the minor version (33 here: help
compiler 4), major (1), the date compiled and flags. Flags 4 or 8:
the topics are compressed, in 4 KB or 2 KB blocks. Files of minor 16
and before (Windows 3.0's) aren't read.

**`|TOPIC`**: blocks, each a 12-byte header (the last topic link, the
first, the last topic header) and then the records. Compressed blocks
are LZ77, unpacked to up to 16 KB. A record's place is its block's
number times 0x4000 plus its offset in the unpacked block, header
counted. The first record is at 12.

**LZ77** (the help compiler's): a flag byte, then eight items, its bits
lowest first. A 0 bit is a byte as it is. A 1 bit is a 16-bit word:
(length - 3) in its top 4 bits, (distance back - 1) in the other 12.

**Records** (topic links) are a 21-byte header:

| Offset | Contents |
|---|---|
| 0 | the record's size |
| 4 | LinkData2's size unpacked |
| 8 | the previous record |
| 12 | the next (-1 at the end) |
| 16 | LinkData1's size, header counted |
| 20 | type: 2 a topic's header, 0x20 text, 0x23 a table |

Then LinkData1 and LinkData2. When LinkData2 is shorter than its
unpacked size it's phrase-compressed.

- **A topic's header** (type 2): LinkData1 holds the topic's numbers.
  Among them, at 16 and 20, are where its non-scrolling region starts
  and where its scrolling part starts. LinkData2 holds its title, up to
  a 0. In this file the non-scrolling region is a row of buttons'
  names (CONTENTS, BACK, NEXT...), which Reluva leaves out.
- **Text** (0x20) and **tables** (0x23): LinkData2 is the words, a 0
  between each piece. LinkData1 is the layout: the topic's size so far
  and this record's (compressed numbers), for a table its columns, and
  then for each paragraph its layout (4 bytes, a 16-bit word saying
  which of spacing, indents, a border and tab stops follow) and its
  commands. The pieces of text and the commands take turns: a piece,
  then a command.

| Command | Meaning | Bytes after |
|---|---|---|
| `0x80` | a font | 2 |
| `0x81` | a line break | |
| `0x82` | the paragraph's end | |
| `0x83` | a tab | |
| `0x86`-`0x88` | a picture (in the line, left, right): a kind (3 a picture, `0x22` one with hot spots, 5 a window a program draws), its size (signed), the hot spots' count for `0x22`; a picture's data is a 16-bit source (0: the file's `\|bm` pictures) and its number | the size |
| `0x89` | a hot spot's end | |
| `0x8B`, `0x8C` | a space, a hyphen, that don't break | |
| `0xC8`, `0xCC` | a macro | a 16-bit size, then it |
| `0xE0`, `0xE1` | Windows 3.0's jump and pop-up | 4 |
| `0xE2`, `0xE3`, `0xE6`, `0xE7` | a pop-up and a jump (the last two without the link's font): the hash of the topic's context name; the link's words run to the next `0x89` | 4 |
| `0xEA`-`0xEF` | to another file | a 16-bit size, then it |
| `0x20`, `0x21` | a field | 4, 2 |
| `0xFF` | the end | |

**Compressed numbers**: the lowest bit of the first byte says which
form. A word is 1 byte (value / 2) or 2 bytes (value / 2). A long is 2
or 4 bytes. The signed forms subtract 0x40, 0x4000 or 0x40000000.

**Hall compression** (`|PhrIndex`, `|PhrImage`): the phrases. `|PhrIndex`
is a 28-byte header (entries at 4; the image's size unpacked and packed
at 12 and 16; a bit count in the low 4 bits of the word at 24) and then
the phrases' lengths in a stream of bits, read from 32-bit words lowest
bit first. Each length is 1, plus `1 << bits` for every 1 bit before a
0, plus then `bits` more bits, each adding 1, 2, 4... `|PhrImage` is the
phrases one after another, LZ77-packed when its two sizes differ. In
LinkData2:

| Byte | Meaning |
|---|---|
| even | phrase byte / 2 |
| low bits `01` | phrase 128 + (byte / 4) x 256 + the next byte |
| low bits `011` | the next byte / 8 + 1 bytes as they are |
| low bits `0111` | byte / 16 + 1 spaces |
| low bits `1111` | byte / 16 + 1 zeros |

The older phrase form (`|Phrases`) isn't read: this file doesn't have it.

**Text.** It's in Windows' character set. Reluva changes the quotes,
dashes, ellipsis, bullet and marks to ASCII.

## Links

A jump names its topic by the 32-bit hash of the topic's context name.
Two B+ trees turn that into a topic:

- `|CONTEXT` (key form `L4`): a hash and its topic offset.
- `|TTLBTREE` (`Lz`): each topic's offset and its title, in the topics'
  order.

The topic is the last whose offset is at or before the hash's. All 468
jumps in `MM2HELP.HLP` are found in `|CONTEXT` (167 entries); 465 land
exactly on a topic's start; `|TTLBTREE`'s 183 entries are its 183
topics. Reluva's reader marks a link's words in the topic's text with
the topic it goes to (`winhelp.h`'s marks), and a picture's place with
its number.

## Pictures

`|bm<n>`: the help compiler's own bitmap files. Each is:

- a magic (`lP`, `0x506C`; or `lp`) and how many pictures it holds, each
  one's offset (32-bit), then each picture:
- a type (5 a device's bitmap, 6 a DIB, 8 a metafile, not read) and a
  packing (0 none, 1 runs, 2 LZ77, 3 LZ77 then runs);
- in compressed numbers (as above): the resolution (two longs), planes
  and bits a pixel (words), width, height, colours used and important,
  the packed data's size and the hot spots' size (longs);
- where the data and the hot spots are (32-bit, from the picture's
  start), and a DIB's colours (BGR and a zero, as many as it uses).

The data unpacks to rows from the bottom, each a whole 4 bytes (2 for a
device's bitmap). Runs: a byte with its top bit set is followed by that
many (less the bit) bytes as they are, else by one byte to repeat that
many times.

`MM2HELP.HLP`'s 24 are all type 6 packed with LZ77: 23 at 24 bits (the
help's banner, the cars, the screens and the HUD, 385 x 206 to 640 x 480)
and an arrow at 8 bits. Each was checked by eye.

## Not read

- The hot spots in pictures, the keywords (`|KWBTREE`...), the fonts
  (`|FONT`), the macros and the contents.
