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

Reluva lights the World page with the key light (at three quarters of its
colour) from the direction (cos pitch sin heading, -sin pitch, cos pitch
cos heading), which puts noon's key light high above (pitch -1) and
evening's low; and the ambient colour plus a quarter of each fill's
colour as ambient light. That's Reluva's approximation: how the game
combines them, and whether the city itself is lit by them or by its
ambient maps (`.ldef` files name `.tif` pictures that aren't shipped), is
still to be compared with the game. Night comes out brighter than the
game's.

## The sky

`city/<map>.sky` names the sky's model and three numbers not worked out
(`sky_dome 0 0.95 0.005`). The dome's 16 paint jobs are the same 16
times and weathers, in the same order: its textures are `sky_ca_f`,
`sky_pa_f`, `sky_fa_f`, `sky_ra_f`, `sky_cn_f`... with c, p, f and r the
weathers and a, n, d and m (morning, noon, dusk, midnight) the times.
