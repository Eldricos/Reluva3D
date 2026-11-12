# Options

The game's settings, its options screen, and what each setting changes.

## How this was worked out

This comes from `motoe.exe` (English 1.0), read in a disassembler to make
Reluva work with the game:

- the options screen's object (`0x42bd40`, vtable `0x482c50`);
- the registry code (`0x42bae0` reads the settings, `0x42b990` writes
  them);
- the places each setting is read.

It was checked against the screen's scene, `Menu.Options/menu.options_01.ac2`.

## The settings

The game keeps its settings in globals (`0x490048..0x490068`) and in the
registry, under `HKCU\Software\DreamCatcher\Agatha Christie\<title>\Settings`
as DWORDs. It reads them once at start-up, with no range check. It writes
them on the options screen's OK and Cancel, and when a game is saved or
loaded.

| Setting (registry) | Screen | Default | Range | What it does |
|--------------------|--------|--------:|-------|--------------|
| `Music` | Music Volume | 7 | 0..10 | Volume of every sound whose path holds `music`, as a tenth of full. |
| `Sound` | Effects Volume | 7 | 0..10 | Volume of every other sound, speech included. Movies and scene videos use it too: Bink volume 3200·v + 767, or 1000 for 0. |
| `Gamma` | Brightness | 5 | 0..10 | A linear gamma ramp of slope (32g + 97)/257, clipped. 5 leaves colours as they are. Full screen only. |
| `Captions` | Text Captions | 1 | 0/1 | Off: the dialogue box and its spoken line aren't drawn, except while choosing and for the ham-radio scenes; movie subtitles aren't drawn either. |
| `Shadows` | 3D Shadows | 1 | 0/1 | The characters' shadows ([shadows.md](shadows.md)). |
| `Water` | Animated Effects | 1 | 0/1 | Off: scene videos flagged `BNKX effect` (water, flames...) stop advancing. |
| `Fog` | Fog, Haze, and Clouds | 1 | 0/1 | Off: pictures from `elements\fog` or `skies\clouds` with `SPRT scroll` set aren't drawn. |
| `Rain` | Particle Effects | 1 | 0/1 | Off: particle systems neither move nor draw. |
| `AntiAlias` | Anti-Aliasing | 0 | 0/1 | 4× multisampling if the card has it, else 2×. Changing it resets the device. |
| `Slot` | — | 0 | | The last save slot used. Nothing reads it back. |

## The options screen

`menu options` opens the scene `Menu.Options`. Its `PUZZ options` record
creates the screen's object. The scene itself is a little 3D scene, with
a backdrop, animated water, snow, fog and a deer. Each of its hotspots
makes the deer play `scare`, then sends `action options<n>`.

**The controls**

- **Pictures:** the object finds the scene's three `sliderbar.jpg`
  tracks, three `slider.jpg` thumbs, six `checkbox.tga` boxes and the
  `check.tga` tick.
- **Labels:** `options1`..`options9` from `Text\interface.txt`, in the
  font for "save, load, and options screens" with the "selected" colour.
  - A slider's label is centred over its track, 5 pixels above its thumb.
  - A box's label is right-aligned 10 pixels left of the box.
- **Sliders** (`options1..3`: Music, Effects, Brightness):
  - The thumb sits at x = 694 + 23·value.
  - A mouse-down starts a drag. The thumb then follows the cursor,
    snapping to the nearest of the 11 steps.
  - Letting go applies the value at once: you hear the volume (Effects
    also plays `sound\soundtest.wav`), or see the brightness. The stored
    setting only changes on OK.
- **Boxes** (`options4..9`):
  - The tick is drawn 2 pixels up and left of each ticked box.
  - Captions only changes on OK.
  - The other five change at once, and the screen's own water, fog and
    snow react.

**OK and Cancel**

- **OK** stores Music, Sound, Gamma and Captions.
- **Cancel** puts back the volumes, the brightness and the five toggles
  as they were.
- Both then write the registry, and `previous` goes back to the menu that
  opened the screen.
- Escape is Cancel. Enter does nothing: the OK hotspot's key is
  `VK_ENTER`, which the game never matches.

## In Reluva

`scene_options.cpp` has the settings and the screen. The settings are
kept in Reluva's folder for the game, as `options.ini` lines
(`Music=7`).

**What each setting does in Reluva:**

- **Volumes** set the mixer's music, effects and speech volumes.
- **Brightness** applies to the scenes' pictures, and scales the 3D
  characters' colours by the same factor, clipped.
- **Captions** hide the dialogue's lines and the movie subtitles.
- **Animated Effects** freeze the flagged videos.
- **Fog** hides the fog and cloud pictures.
- **Particle Effects** stop the particles.
- **3D Shadows** turns the characters' shadows on and off
  ([shadows.md](shadows.md)).
- **Anti-Aliasing** draws the 3D with 4× multisampling, or 2× if the
  device lacks 4×. It changes at once, from the options screen too: the
  frame's targets are made again instead of the device being reset. The
  pictures are 2D and aren't touched, and the shadows' edges stay hard
  (their mask is made on the CPU, [shadows.md](shadows.md)).
