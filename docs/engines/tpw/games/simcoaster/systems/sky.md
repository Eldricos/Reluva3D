# SimCoaster: the sky

> **Status:** partial · **Code:** `src/engines/tpw/ui/sky.cpp` · **Checked against:** the game's files only · **Confidence:** inferred unless marked

## Overview

Each world has a sky behind its park, from three pictures in its
`Data/levels/<world>/sky/` folder (and smaller copies in `ssky/`), its
fog colour and its light, from its `Standard.sam`. No sky model is in
the files: the game makes the sky's shape itself. How it draws it isn't
known; what follows is what the files say, and Reluva's guess at the
rest.

## How it works

### The pictures

All 256 × 256 TGAs:

| File | What it holds | Arabian Nights | Land of Invention | Polar Zone |
|---|---|---|---|---|
| `sky_rgb.tga` | A colour from the middle out to the edges, round: the dome's colours, the middle overhead *(inferred)* | a yellow sun fading to red | white to light blue | deep blue to an orange rim |
| `sky.tga` | Pale, mottled, near opaque (alpha 72 to 255): clouds | beige | lilac-white | white and brown |
| `sky_cyl.tga` | A colour band, twice, one copy above the other; each copy clear at its top (alpha 24) and opaque at its bottom: a band round the horizon *(inferred)* | orange over dark brown | sky blue | orange over deep blue |

The Intro World's three are black throughout. Each band's opaque bottom
is close to its world's fog colour (the Arabian Nights' dark brown, the
Polar Zone's dark blue), as if the band blended the sky into the fog
below the horizon *(inferred)*.

The Arabian Nights' and the Polar Zone's folders also have moons
(`moon.tga`, `moon2.tga`: a cratered moon, a ringed planet, a moon with
a snowman's face).

### The settings

From a world's `Standard.sam`, over `Data/levels/Standard.sam`'s:

- `ThemeEngine.FogColour`: 0xRRGGBB. The Arabian Nights' 0x502000
  ("sinister dark arabian fog", its comment says), the Land of
  Invention's and the Intro World's 0x48D8F8, the Polar Zone's 0x112244.
- `LightNormal.X`, `.Y`, `.Z`: the light's direction, 0.4, −0.8, 0.4 in
  all four worlds; `ThemeEngine.AmbientLightLevel` and
  `DirectionalLightLevel`, colours with alpha (0xFF555568, 0xFFFFFFD8),
  not used yet.
- `NumSkyObjects.n` and `SkyObjects[i].X`, `.Y`, `.Z`, `.Size`,
  `.FrontFlag` ("drawn over the sky (1) or behind it (0)") and
  `.Filename` ("should be in data\levels\THEME\sky\"): the moons. The
  Arabian Nights describes two, `moon.tga` and `moon2.tga`, and the
  Polar Zone one, but their counts are 0 in `Standard.sam`; in
  `Online_Standard.sam`, the settings of the online parks Reluva leaves
  out, they are 2 and 1. So the moons show only online *(inferred)*.

The detail settings (`Data/high.sam`, `med.sam`, `low.sam`, `voodoo.sam`) have
`GraphicalOptions.SKYQUALITY`, "1, 2 or 4 Layers" (4 high, 2 medium and
`voodoo.sam`'s, 1 low), `SKYDETAIL` (on but for low) and `SKYSHADOW`,
off in all of them: the sky drawn in
layers, perhaps of clouds, and their shadows on the ground *(guessed)*.

## In Reluva

The viewer's Worlds page draws the world's sky behind its terrain, and
lights the terrain by the world's light (K turns both off). Its guess
(`src/engines/tpw/ui/sky.cpp`), for each pixel's direction:

- Overhead to the horizon, the dome's colour from `sky_rgb.tga`, its
  middle overhead and its edge at the horizon, times the clouds'
  colour from `sky.tga`, laid on a plane overhead.
- Within 12.5° above and below the horizon, the band from `sky_cyl.tga`
  over both, as its alpha says, its middle at the horizon.
- Below that, the fog colour.

One layer of clouds, standing still; no moons. `reluva-tool tpw
world-shot` draws the same. The unit tests (`tpw.unit.ui`) read a
world's sky and settings built in the test and draw it.

## Sources and checks

- **Where the facts come from:** the US release's sky pictures and
  settings files (November 2026); the settings' own comments.
- **How it was checked:** the four worlds drawn with `world-shot`, from
  high and from the ground: a sunset over the Arabian Nights, a cloudy
  blue sky over the Land of Invention, a deep blue over the Polar Zone.
  None has been set beside a recording of the game's sky yet.

## Open questions

- How the game draws the sky: the dome's shape and the pictures'
  mapping, how many cloud layers move and how, and where the band lies.
- Whether the moons are shown outside the online parks.
- `skytop.tga` in `Data/Sky/`, dark clouds, used by no world's folder:
  for storms, perhaps *(guessed)*.
- What the ambient and directional light levels do.
