# AI settings (`.aimap`)

Text files saying what the computer-driven cars, the pedestrians and the
police are: `city/<map>.aimap` for each city, then a file for each race
(`race/<city>/<race><n>.aimap` for Amateur, `.aimap_p` for Professional,
checkpoint races named `race`) and for cruising (`race/<city>/roam.aimap`).
Read by `src/engines/age/aimap.cpp`. Worked out from the files, which
carry their own comments; 209 were read.

## Form

`#` starts a comment line. A `[Section]` line, then its values; a list's
first value is its count:

```
# Default Road Speed Limit
[Speed Limit]
15

[Ambient Types/Density]
11
va_bus_f 0.01 0
va_eurovan_f 0.13 0
...
va_smallsuv_s 1.0 0
```

Reluva reads a race's file over its city's, so the sections a race file
leaves out keep the city's values.

## Sections

| Section | Values | Reluva |
|---|---|---|
| `Speed Limit` | m/s (15 throughout) | traffic's speed; the police chase a car going half as fast again |
| `Ambients Drive On The Left` | 0 or 1 (London 1) | which way the lanes are driven ([bai.md](bai.md)) |
| `Ambient Types/Density` | model, cumulative share, 0 | the traffic's models: a random 0-1 picks the first whose share is above it |
| `GoodWeatherPedName / BadWeatherPedName` | pairs of models | the pedestrians, the second of each pair when it rains |
| `Exceptions` | road, density, speed limit (`# Rd Id, Density, Speed Limit`) | no traffic starts on those roads with density 0; a speed limit above 0 replaces the default |
| `Police` | model, x, y, z, heading, then four values (`0 15 0.5 50`) | where the police wait; the heading in degrees as the race files' (0 faces -Z); the comment names the fields "Geo File, StartLink, Start Dist, Start Mode, Start Lane, Patrol Route", but the lines don't follow it |
| `Opponent` | model, `.opp` file, skill (0.70-1.00), then nine values | each opponent's car, and its skill as the share of its top speed Reluva's driver aims for |
| `Density` | a share (cruising: London 0.1) | read, not used: the Drive page has its own levels |
| `CopChaseDistance` | metres (Crash Course 150) | how near the police notice the player |
| `AmbientLaneChanges` | 0 or 1 | read, not used |
| `Hookmen`, `Subway`, `Traffic Lights` | | not read |

Police lines appear in cruising (19 in San Francisco, 20 in London) and
in some checkpoint races and Crash Course lessons (San Francisco's
race 1 at Professional has 3, as its Cops column says; Reluva takes as
many of them as the column). Amateur opponents have skills of 0.70-0.75 and
professional ones 0.93-1.00 in San Francisco's first race.

Of the 19 police headings in San Francisco's cruising file, 10 are 0 and
several cars stand off the road, so the headings couldn't be checked
against the roads; Reluva reads them as the race files' headings.
