# Star Trek: Away Team: a survey of the game's files

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

A first look at the US English retail install and its CD, November 2026:
file listings, sizes and hashes, the executable's headers and imports
read with `objdump -p`, and the start of one or two files of each kind
in `DATA.dat`, dumped as hex or text. Nothing was run.

## The install

| File | Size | What it is |
|---|---|---|
| `Star Trek.exe` | 2,437,120 | The game. PE32, Windows GUI, linked 2 March 2001 with Visual C++ 6 (linker 6.0); its C runtime is linked in (no `MSVCRT.dll`). Sections `.text`, `.rdata`, `.data`, `.rsrc` only: no packer or copy protection seen |
| `DATA.dat` | 325,841,399 | Nearly all the game's data ([below](#datadat)) |
| `DATA/Movies/` | | Bink movies, loose |
| `texture.dat` | 40 | Unknown: 4 bytes `0a 00 00 00`, then 36 bytes that look random |
| `binkw32.dll` | 272,384 | RAD's Bink player (June 2000) |
| `ogg.dll`, `vorbis.dll`, `vorbisfile.dll` | | The Ogg Vorbis reference libraries (February 2001), with their licence text in `Vorbis/` |
| `register.exe`, `GETINFO.DLL`, `anet.inf` | | Online registration and a system information tool; not the game's |
| `stat.isu` | | InstallShield's uninstall log |
| `readme.rtf`, `Extras/` | | The readme, the manual (`Extras/Help/Manuals/manual.pdf`) and the support pages |

The CD's `Setup/` holds files of the same names loose (not compared
byte for byte), and again in an
InstallShield cabinet (`data1.cab`, `data1.hdr`), with DirectX's
installer and other companies' software in `Extras/`. Its root has
`AUTORUN.EXE` and `Setup.exe`.

The executable asks for the CD while it runs: *"Please insert the
original Star Trek Away Team CD now."* (a string in `.rdata`).

### What the executable calls

- **`DDRAW.dll`:** `DirectDrawCreate` only. No Direct3D import; the
  game draws in 2D.
- **`DSOUND.dll`:** by ordinal. The string `EAXDirectSoundCreate` is
  there too, so it looks for EAX *(inferred)*.
- **`ole32.dll`:** `CoCreateInstance`, with `DPERR_...` strings: DirectPlay
  for the network game *(inferred)*.
- **`binkw32.dll`:** the movie player, drawing through DirectDraw
  (`BinkBufferSetDirectDraw`) and playing through DirectSound.
- **`vorbisfile.dll`:** `ov_open_callbacks`, `ov_read`, `ov_info`,
  `ov_pcm_total`, `ov_clear`: Ogg Vorbis decoded through callbacks, so
  from inside `DATA.dat` *(inferred)*.
- **`MSACM32.dll`:** audio format conversion.
- **`WINMM.dll`:** timers, joysticks, and `mciSendCommandA` (the CD check
  or CD audio; there's no CD audio track seen).
- **`ADVAPI32.dll`:** the registry.

The executable holds some 600 strings that look like C++ class names
(`C` and a capital: `CStarTrekRules`, `CLayerSaveData`,
`CSaveGameDescription`...), the names the object text uses
([below](#object-text)): the class factory's table *(guessed)*.

## `DATA.dat`

A ZIP archive: 6,949 local file headers from the file's start, every one
stored (method 0) with no flags, then the central directory (`PK\1\2`
records) as usual. The end of central directory record is standard but
for its signature: `52 45 05 06` (`RE\5\6`) where ZIP has `50 4b 05 06`
(`PK\5\6`). Readers that look for `PK\5\6` find no archive. Names use
`/` between folders.

| Extension | Files | Size (MB) |
|---|---|---|
| `.mdl16` | 3,738 | 130.0 |
| `.ogg` | 1,484 | 76.7 |
| `.txt` | 689 | 0.4 |
| `.frm16` | 672 | 14.9 |
| `.seq16` | 198 | 7.8 |
| `.rds` | 48 | 0.2 |
| `.way` | 33 | 19.9 |
| `.zax` | 33 | 44.2 |
| `.mds` | 18 | 0.1 |
| `.bak` | 16 | 24.4 |
| `.sty` | 8 | <0.1 |
| `.pal` | 6 | 0.2 |
| `.rla` | 3 | 5.1 |
| `.bmp` | 2 | 0.3 |
| `.wav` | 1 | <0.1 |

The top folders: `Cache/` (the pictures: `Models`, `Levels`, `Interface`,
`Inventory`, `Character`, `Fonts`, `Textures`, `Sequences`, `Animated
Textures`), `Levels/` (18 missions, 33 maps, the dialogue and voice-overs),
`Character/` (roster, portraits, ranks, fields, races, 711 sound files),
`Interface/Screens/` (310 files for the menus), `Music/`, `Sounds/`,
`Palettes/`, `Textures/`, the definitions (`Categories/`, `Damage Types/`,
`Death Behaviors/`, `Inventory/`, `Process Types/`, `Surface Types/`,
`Water Warp Effects/`, `Transporter Backgrounds/`), and `Rules.txt` and
`Credits.txt` at the top. `Test/` and the `.bak` copies of maps look like
leftovers of development.

### Object text

`Rules.txt`, the maps (`.zax`, 1-2 MB each), the missions (`.MDS`), the
surface types (`.STY`), the roster and most `.txt` definitions are text,
tab-indented, CRLF:

```text
CStarTrekRules
{
	First Map Name=Mission1 MainMap
	Camera Bound Width=142
	Custom Rules=CStarTrekCustomRules
	{
		Screen Pos=75,0
		Screen Size=565,423
		Menu=Array
		{
			Item Count=13
			Menu=CAnimatedMenuInfo
			{
```

A block is a class name, then `{`, then `Key=Value` lines; a value that
is itself an object is the class name after `=`, and its block on the
following lines. Lists are `Array` with an `Item Count` (or `Array
Count`) first, then the items, each under the same key. Numbers are
written as text (`0.25`, `374.000000,0.000000`, `75,0`).

What the files say of the game, at a glance:

- `Rules.txt`: the first map, the camera's bounds, the screen's layout
  (`Screen Pos=75,0`, `Screen Size=565,423`: in a 640x480 screen
  *(inferred)*), the noise each way of moving makes (crouch 0.25, walk
  3, run 6), the 13 menus.
- A mission (`.MDS`, `CMissionDescription`): its name, stardate, movies,
  whether there's a briefing and team selection, the map, and the
  briefing's lines (`CBriefingScriptLine`: the `.RDS` file and label,
  who speaks).
- A map (`.zax`, `CLayerSaveData`): the minimap picture, the song, the
  camera scripts (`CCameraScript` with actions: `CInfoDialogAction`,
  `CMultipleActionsAction`...), its size in pixels (Mission 02's Security
  Deck: `Width=2350`, `Height=1280`), the sound environment
  (`Environment Type=Forest`, `Rolloff`), and the objects on it.
- A roster entry (`Character/Starting Roster/*.txt`, `CTeamMemberInfo`):
  name, rank, field, portrait, and attributes Speed, Stealth and
  Stamina, with starting inventory.

### Dialogue: `.RDS`

Text, a label line starting with `:` and the text after it until the
next label:

```text
:Dead Body
This is the dead body of $trigger.  {Inventory It contains $inventory.}
```

`$name` is filled in by the game; `{Name ...}` looks like text shown
only when `Name` applies *(guessed)*.

### Pictures

- **`.pal`:** 768 bytes, 256 RGB triples, 8 bits each. `GamePalette.pal`,
  and tables for blending (`Alpha20_80`, `Alpha40_60`, `FadeTable`,
  `BlackBias`, `InvColorTable`).
- **`.frm16`:** a frame of 16-bit pixels. Header starts `2e 10 00 00 00
  00`, then the width and height as 16-bit numbers (a minimap: 130x100;
  a portrait icon: 50x50). The data after it is run-length coded:
  runs of 16-bit pixels, values like `0x2129`, `0x4229` (5:6:5 or 5:5:5,
  not yet known).
- **`.seq16`:** animated textures. Starts with a float (`0x41d00000`,
  26.0), another, a count, then named layers (`Transparent Mask`), and
  frames with the same `2e 10` mark as `.frm16`.
- **`.mdl16`:** a model: a sprite with named animations (`Idle`...), the
  sound each makes (`WEAPONS/Laser Bounce.ogg`), its dialogue script.
  Starts as `.seq16` does with the float 26.0. Strings are a 32-bit length
  then the characters.
- **`.rla`:** three, in `Test/`. Wavefront's RLA image format
  *(guessed)*; unused.

### The maps' other files

- **`.way`:** binary, one per map (Mission 01's: 1,095,776 bytes).
  Starts with the same float 26.0, then floats (101.0...) and counts:
  waypoints or a walk grid *(guessed)*.
- **`Cache/Levels/`:** one `.frm16` per map, the minimap. The maps
  themselves are built from models, textures and placed objects, not
  one picture.

### Sound

- **`.ogg`:** Ogg Vorbis, every sound in the game: 31 files in `Music/`
  (a "Full Mix" and two "Sub Mix" for several missions), effects in
  `Sounds/`, voices in `Character/Sounds/` and `Levels/Voice Overs/`.
- **`.wav`:** one.

27 of the `.ogg` files, one in every 60 in the archive's order and two
songs, checked with `ffprobe` and stb_vorbis (v1.22):

- Stereo for the music, mono for voices and effects; 22,050 Hz but for
  three of the 27 at 44,100 Hz (two voices and an interface sound).
- Made with the pre-1.0 Vorbis encoder: each file's comment header names
  `Xiphophorus libVorbis I 20001031` (most) or `... 20010119`.
- Every one uses floor 0, the Vorbis floor encoders used before 1.0:
  stb_vorbis refuses all 27 (`VORBIS_feature_not_supported`, its error
  for floor 0), while FFmpeg's decoder plays them. A decoder for this
  game needs floor 0.
- libvorbis 1.3.7 decodes all 27 as FFmpeg does, but for 5 files where
  it gives 128 samples more at the end
  ([Ogg Vorbis](../../../common/audio/vorbis.md#sources-and-checks)).

### Movies

Bink, revision `i` (`BIKi`) in the two checked (`Paramount.bik`,
`Starfield.bik`): the logos (`Activision.bik`,
`Paramount.bik`, `Reflexive.bik`), `Intro.bik`, `EndGame.bik`, the
main menu's background (`Main Screen Background.bik`, `STAT Logo for
Main.bik`), the transporter, a starfield, and three missions' briefings.
