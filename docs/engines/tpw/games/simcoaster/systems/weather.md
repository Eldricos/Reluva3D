# SimCoaster: weather

> **Status:** partial · **Code:** `src/engines/tpw/park/visitors.cpp`, `src/engines/tpw/ui/park_screen.cpp` · **Checked against:** the worlds' and general settings, the weather's pictures · **Confidence:** guessed unless marked

## Overview

A park has weather: fine, a heatwave, rain, a storm or snow. Each
world's settings give its seasons' chances of each, the general
settings what each does to the visitors and how many come, and each
attraction how much more or less it's wanted in it. Its pictures,
`generic/weather/` (`Raindrop.tga`, `snow.tga`, `lightning.tga`, and
smaller ones in `sweather/`), show it falling. The recording's park is
fine throughout.

## How it works

### Its chances

A world's `Standard.sam` gives four seasons' chances, in percent, of a
heatwave, rain, a storm and snow (`Seasons[n].PercentageChanceForRain`
and the like); Land of Invention's: snow 25 in the first, rain 30 in the
second, a heatwave 20 in the third, a storm 15 in the fourth, and
otherwise fine. Reluva takes the seasons as the year's quarters from
January, and rolls the weather as the park opens and every 12 days
*(guessed)*.

### What it does

- **Who comes:** the general settings' `Arrival.RainPercentageModifier`
  (90), `Storm` (80), `Snow` (75) and `Heatwave` (120) scale how many
  visitors a park draws ([park.md](park.md#visitors)).
- **Those out in it:** every `PeepInfo.WeatherModifierFrequency` days
  (4, "modifiers get applied once every X days", the settings say), each
  visitor not inside an attraction has their happiness, thirst and hunger
  changed by their generation's `PeepHappinessWeatherEffects`,
  `PeepThirstWeatherEffects` and `PeepHungerWeatherEffects` (a
  grandparent's happiness −3 in snow, a child's +1; thirst +2 in a
  heatwave) *(inferred)*. An umbrella saves `UmbrellaModifier` (1)
  unhappiness.
- **What they choose:** an attraction's `WeatherModifier` percentages
  scale how much it's wanted ([visitors.md](visitors.md#what-they-choose)):
  the umbrella shop's 250 in rain, 5 in a heatwave.

### How it looks

Over the screen, rain falls in slanting streaks, more in a storm (its
most `WeatherEffects.MaxRaindrops`, 600); a storm's lightning, the bolt
of `lightning.tga`, flashes now and then, lighting the sky; snow drifts
down, each flake one of the four of `snow.tga`. How many, how fast and
how often are guessed.

## In Reluva

`Park` reads the chances and effects, rolls the weather and applies it
(`Park::weather`); `ParkScreen` draws it over the park.
`reluva-tool tpw park-shot ... -weather <fine|heatwave|rain|storm|snow>`
sets it. The unit tests (`tpw.unit.park`) open a park in a January whose
only weather is snow, and count the visitors it draws.

## Sources and checks

- **Where the facts come from:** the worlds' and general settings, and
  their comments; the weather's pictures.
- **How it was checked:** `park-shot` in each weather. Not against the
  game: the recording's park is fine throughout.

## Open questions

- Which months the seasons are, how long weather lasts, and how fast it
  changes (`Weather.SpeedOfChange`); `WeatherEffects`' qualities,
  lightning countdown and thunder.
- How the game draws rain, snow and lightning: in the world or on the
  screen, with what of the pictures (`Raindrop.tga` has no alpha).
- Heatwaves' look; balloons given up for umbrellas
  (`DiscardBalloonPctModifier`); shelter (`UsageInfo.ISIndoors`).
