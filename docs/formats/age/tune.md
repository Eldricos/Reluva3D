# Tuning text and city lighting

Much of the game's behaviour is in text files of one form, starting with
a `type: a` line, then named blocks of lines of a name and values, with
blocks inside blocks:

```
type: a
clear-noon {
  KeyHeading -2.000000
  KeyColor 0.950000	0.900000	1.000000
  Ambient -14145486
}
```

Cars' `.vehCarSim`, `.vehCarDamage` and the like, cameras, the HUD maps
(`.mmHudMap`) and the cities' lighting use it. Read by
`src/engines/age/tune.cpp` into a tree; names are matched ignoring case.
Worked out from the files themselves; values are separated by spaces or
tabs, lines end in CR LF.

## City lighting (.lt00-.lt15)

Each city has 16: `city/sf.lt00` to `.lt15`. The block names give their
order: time x 4 + weather, the times morning, noon, evening and night,
the weathers clear, cloudy, foggy and rainy (`clear-morning` is 00,
`rainy-night` 15).

| Name | Contents |
|---|---|
| KeyHeading, KeyPitch | the main light's direction, in radians |
| KeyColor | its colour, 1 = full |
| Fill1Heading, Fill1Pitch, Fill1Color | a second light |
| Fill2Heading, Fill2Pitch, Fill2Color | a third |
| Ambient | a colour as a signed 32-bit number: 0xAARRGGBB (-14145486 is 0xFF282832) |

The direction a light comes from is (cos pitch sin heading, -sin pitch,
cos pitch cos heading), which puts noon's key light high above (pitch -1)
and evening's low. Reluva lights the city's own surfaces (the PSDL) as
the file reads: the ambient colour, plus each of the three lights by how
squarely the surface faces it, at most 1 in all, as Direct3D lights
vertices; done for every vertex when the lighting changes. That's what
darkens night: San Francisco's clear night has its second fill (0.7,
0.5, 0.5) at pitch +1, from below, so it lights almost nothing, where
Reluva used to add a quarter of each fill to every surface. Placed
models, cars and people, which move or are shared, still get one light:
the key at three quarters of its colour, and the ambient colour plus a
quarter of each fill's as ambient light. Foggy nights are lit brightly
(a white key light): their fog darkens them.

Whether the city is also lit by its ambient maps isn't known: the
`city/amb_*.ldef` files name `.tif` pictures (`\\taxi\projects\...
\sfambient_clearmorn.tif`) that aren't shipped, then two corners of the
city. `city/<map>.lmap` (`LMP0`, a count, then a 32-bit number for each
block) holds -267 for every block but block 0, which is unset
(0xcdcdcdcd): nothing to go on.

## Fog (`<map>_fog.csv`)

`city/sf_fog.csv` and `london_fog.csv`: a header, then a line for each
of the 16 lightings in the same order: red, green, blue (0-255), where
the fog starts and where it's complete (metres), and a description
("clear-morning"). Reluva mixes it into everything drawn in the city,
after texturing: none before the start, all fog colour from the end on
(glows and flares fade instead). Foggy noon in San Francisco is grey
from 10 to 120 m; London's clear noon from 900 to 1,000.

## The sky

`city/<map>.sky` names the sky's model and three numbers not worked out
(`sky_dome 0 0.95 0.005`). The dome's 16 paint jobs are the same 16
times and weathers, in the same order: its textures are `sky_ca_f`,
`sky_pa_f`, `sky_fa_f`, `sky_ra_f`, `sky_cn_f`... with c, p, f and r the
weathers and a, n, d and m (morning, noon, dusk, midnight) the times.
