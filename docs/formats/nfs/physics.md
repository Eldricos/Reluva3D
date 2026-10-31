# Need for Speed III: car handling (`carp.txt` and the car tasks)

How `nfs3.exe` turns a car's `carp.txt` into driving: where the file's
fields go, which per-step tasks use them, and the race settings that
change them. Worked out in October 2026 from the retail `nfs3.exe` (its
disassembly) and runs in `nfs-host` into a Hometown race. The step
tasks themselves are listed in
[nfs-threads.md](../../engines/nfs-threads.md); this is what they do.

Status: **read from the code**, with a few values checked at run time
(below). Not implemented in Reluva.

## In short

- `carp.txt` is read into a 0x4AC-byte record (P below). The car record
  keeps a pointer to it at **+0x528**. The race's set-up then changes P
  in place: the car set-up sliders, the automatic gearbox, the assists
  and codes (`0x4a78d6`).
- The **human's car** runs a four-wheel model every 32 Hz tick
  (`0x4ad7a3`, from the +0x608 task): engine and gears, brakes, four
  tyres with their loads, then the velocity and spin. The computer's
  cars use a simpler model (`0x4088f0`).
- **Car dynamics** (`0x4a2830`, every car): gravity, downforce, the
  height over the road, and whether the car is on the road, landing or
  in the air.
- Movement is separate: position += velocity / 64 at 64 Hz (`0x4a2c60`).
- Units are SI: metres, m/s, kg, rpm. One tick is 1/32 s, so velocity
  changes are acceleration × 1/32 (`0x53dea0`), gravity is 9.81/32 =
  0.306563 a tick (`0x53c8dc`).

## The race settings named

From the front end's option table (`0x5563fc`, 20-byte rows: name,
two lists of text numbers, a third, the variable) and `text.eng`:

| Variable | Option | Values |
|---|---|---|
| `0x6fd3b8` | `racetype` | 0 Single Race (text 213), 1 Tournament (214), 2 Knockout (215), 3 Hot Pursuit (220) |
| `0x5556d8` | `multracetype` (row `0x5563e8`) | Single Race, Hot Pursuit, Knockout (Beginner), Knockout (Expert), Tournament (Beginner), Tournament (Expert) (texts 213, 220, 218, 219, 216, 217) |
| `0x6fd470` | `gamelevel` | 0 Beginner (400), 1 Expert (401): the tournament's and knockout's level |
| `0x6fd4f0` | `ai` | 0 Normal (175), 1 Aggressive (143) |
| `0x6fd474`-`0x6fd488` | `braking`, `steering`, `bestline`, `navigator`, `collision`, `traction` | on, off; copied at a race's set-up to `0x6fd490`-`0x6fd4a4` (`0x473c36`) |

`0x6fd3b0`, the kind of game, is set from the link the player picked
(`0x440761`-`0x4407f7`, strings at `0x5378f8`-`0x537910`): 0 one
player, 1 two on one machine, **2 modem, 3 serial, 4 IPX, TCP or
`racenet`**.

`0x6fd520` is **the number of computer racers** (list `0x5efb88`). At
set-up (`0x473cd9`): with `oppnumber` (`0x6fd4d0`) Full Grid it is 8 −
the humans (`0x6fd518`), with One 1, with None 0. The loop at
`0x4c7c49` sends each one's u16 +8; the music's lap tables
([mus.md](mus.md)) are the one-opponent case.

Tournament and Knockout (and network games) skip the set-up sliders
and the assists below: everyone drives the car as its `carp.txt` says.

## carp.txt in memory (P, at car +0x528)

The writer `0x4b2340` (the debug dump of a car's values) walks the
label table at `0x55eddc` (80 names; the "(n)" numbers come from
`0x4af3b2`) and shows where each field lives. Arrays of 8 are by gear:
0 reverse, 1 neutral, 2 first, and so on, the same index as the gear
byte +0x50B.

| P | Field | Used by |
|---|---|---|
| +0x0 | Car Classification | |
| +0x4 | mass | the acceleration factor (`0x4a8179`); collisions (`0x42bd3f`) |
| +0x8 | number of gears | gear changes, drag set-up |
| +0x10 | gear shift delay (ticks) | gear changes (`0x4a9423`, `0x4aa0b4`) |
| +0x14, +0x34 | shift blip, brake blip [8] (int rpm) | rpm while a shift is under way (`0x4ab254`, `0x4ab276`) |
| +0x54 | gear ratios [8] | not read in the race (only copied) |
| +0x94 | velocity to rpm ratio [8] | wheel rpm (`0x4ab43e`), the factors below |
| +0xD4 | gear efficiency [8] | the acceleration factor (`0x4a81ab`) |
| +0x114 | torque curve [41], 256 rpm a step | the engine (`0x4a8a99`) |
| +0x1B8 | final gear | only copied |
| +0x1C0, +0x1C4 | engine minimum rpm, redline (int) | the engine, gears |
| +0x1C8 | maximum velocity | the drag set-up, a speed cap |
| +0x1CC | top speed cap | race end and the AI (`0x4ce2e9`, `0x4093ab`) |
| +0x1D0 | front drive ratio | drive split front/rear (`0x4adc18`) |
| +0x1D4 | ABS (byte) | the tyres (`0x4acd99`) |
| +0x1D8 | maximum braking deceleration | the brakes (`0x4add0b`) |
| +0x1DC | front bias brake ratio | the brakes (`0x4addae`) |
| +0x1E0, +0x1E8 | gas increasing, decreasing curve [8] (bytes) | not found by search |
| +0x1F0, +0x210 | brake increasing, decreasing curve [8] | not found by search |
| +0x230 | wheel base | not found by search |
| +0x234 | front grip bias | load split front/rear (`0x4adea1`) |
| +0x238 | power steering | force feedback (`0x4718d1`) |
| +0x23C | minimum steering acceleration | tyres (`0x4acf3a`), `0x4ad613` |
| +0x240, +0x244 | turn in ramp, turn out ramp | steering input (`0x4aa354`, `0x4aa2c2`) |
| +0x248 | lateral acceleration grip multiplier | tyre loads (`0x4ade33`), tyres (`0x4acbec`), `0x4ae6ae` |
| +0x24C | aerodynamic downforce multiplier | `0x4a22b9`, `0x4aad74` |
| +0x250 | gas off factor | engine braking (`0x4ab6f7`) |
| +0x254 | g transfer factor | weight transfer (`0x4ae10a`) |
| +0x258 | turning circle radius | steering (`0x4aec27`) |
| +0x25C, +0x268 | tyre specs front, rear (3 ints) | not found by search |
| +0x274-+0x290 | tyre wear, slide multiplier, spin and slide velocity caps, slide assistance (int), push factor (int), low and high turn factor | not found by search |
| +0x294 | pitch roll factor | body pitch and roll (`0x4295b7`, `0x42a806`) |
| +0x298 | road bumpiness factor | `0x42952a` |
| +0x29C, +0x2A0 | spoiler type, speed | `0x4ab2f9` (type 8 changes the rpm fall) |
| +0x2A4-+0x2B8 | turn cutoffs (int) and speed modifiers | the computer's cars (`0x4a85e5`-`0x4a8686`) |
| +0x2BC | subdivide level | |
| +0x2C0 | camera arm (written with `0x4b08d0`) | the camera (`0x424484`) |
| +0x2C4-+0x2E8 | the damage and tuning fields (57-66) | not read in the race: the set-up sliders come from elsewhere (below) |
| +0x2EC | AI acceleration table, 8 × 14 floats | the computer's cars (`0x40828b`, `0x40657d`) |

"Not found by search" means no read through the +0x528 pointer was
found; they may be read through a computed pointer.

## The race's set-up (`0x4a78d6`)

Called once per car at set-up (`0x42c3c8`).

- **Automatic gearbox** (the player's `0x6fd548` + car × 0x6C is 1, and
  not the `MONKEY` code): the automatic fields (+0xC, +0x1BC, +0x74,
  +0xF4, +0xB4) are copied over the manual ones.
- **The set-up sliders.** The car's set-up record (car +0x220, +0x40),
  7 floats, default all 1 but brake balance 0 (`0x55d024`). For a
  computer car, and in Tournament, Knockout and network games, the
  defaults are used. Each changes P:

  | Slider | Changes |
  |---|---|
  | +0x40 Engine Tuning t | each torque value −= (t − 1) × 0.1 × value × (r − i)/(R − r), r = (min + redline)/2/256 + 3, R = redline/256 + 3 (`0x4a7f25`); max velocity × (1 + (t − 1) × 0.1) |
  | +0x44 Brake Balance b | front brake bias −= b |
  | +0x48 Steering Speed s | min steering acceleration × s; front grip bias × (1 + (s − 1) × 0.625), kept in 0.46-0.56; turning circle + (1 − s)/2 × circle |
  | +0x4C Gear Rat Factor g | velocity to rpm × g; max velocity / g |
  | +0x50 Suspension Stiffness k | g transfer × k, kept in 0.3-0.7; pitch roll / f(k) (`0x4ea920`) |
  | +0x54 Aero Factor a | downforce × (1 + (a − 1)/4); max velocity / (1 + (a − 1) × 0.05); the drag's scale (below) |
  | +0x58 Tire Factor | tyre grip (`0x4abd9c`, `0x4ae70a`) |

- **Braking assist** (`0x6fd490`): front brake bias + 0.05, g transfer
  − 0.05. The front brake bias is then kept in 0.3-0.75.
- **`GOFAST`** (`0x6fd4fc` bit 1): torque × 1.75, max velocity × 1.75,
  every gear's efficiency at least 1; wheel grip × 1.41.
- **The per-gear factors**, into the car record:
  - +0x54C[g] = 1 / (velocity to rpm[g]), m/s per rpm (0.01 when 0);
  - +0x56C[g] = (velocity to rpm[g] / mass) × 0.1 × efficiency[g]:
    acceleration (m/s²) per N·m of engine torque. (0.1 is about 2π/60:
    velocity to rpm is gear × final × 60 / (2π × tyre radius); the
    Corvette's first four gears fit its 275/40R18 rear tyre exactly.)
  - +0x52C[g] = 1 / (redline × +0x54C[g]), one over the gear's top
    speed.
- **Drag** (+0x58C), so that top gear just reaches the maximum velocity
  v: A = torque(v × v2rpm[top] / 256) × +0x56C[top] / efficiency[top];
  +0x58C = 0.98 × A / ((2/3) × 0.367572 × v³) (with the Aero slider,
  (1 + a)/3 in place of 2/3).

## The engine and gears (`0x4aaed1`)

Returns the drive acceleration along the road. The car's fields:
+0x590 rpm (int), +0x50B gear, +0x5F0 throttle 0-1, +0x5EC brake 0-1,
+0x510 shift timer, +0x50D wheel-spin flag, +0x4F4 speed along the road.

- **Drag**: D = +0x58C × v³ × 0.367572 × 2/3 (`0x4ab000`), v = +0x4F4;
  over the maximum velocity, + 0.01 × (|v| − max)².
- **Over-rev**: rpm above redline + redline/3 sets the spin flag; rpm is
  kept at most at the redline.
- **Neutral, shifting or in the air**: the rpm moves by itself. Up 250 a
  tick towards max(min rpm, redline × throttle); while a shift runs the
  blip is added; else it falls by 150 (or 25-75 by spoiler type and
  shift delay). No drive.
- **In gear on the road**: wheel rpm = |v × v2rpm[gear]| (`0x4ab43e`).
  The torque is the curve at the engine rpm, linearly between the 256
  rpm steps (`0x4a8a99`, rpm / 256 kept in 0-40), and acceleration =
  torque × +0x56C[gear], at most 10 m/s² in first, 8 in the others,
  −6 in reverse. Then:
  - target (redline × throttle, at least min rpm) below the wheel rpm:
    **engine braking**, −acceleration × gas off factor, against the
    motion; the rpm follows the wheels;
  - else the rpm moves up to 200 a tick towards the wheels' (at least
    min rpm, at most the target), and the acceleration is × throttle ×
    (1 + 0.6 × |+0x5B0|, at most 1.5);
  - faster than 1.15 × the gear's top speed (not in reverse or
    neutral): the acceleration is × top speed / |v|, the spin flags
    are set (+0x594, +0x50D) and +0x514 counts up by the weather
    (`0x4ab914`-`0x4aba6d`).
- The result: on the road, acceleration − D going forward,
  acceleration + 2D going backward; in the air −D/2 or D.

The gear and shifting are `0x4a9769` (manual) and `0x4a90a0`
(automatic); not followed in detail.

Checked at run time (`-watch 0x4ab42f`, the gear in dl; `-watch
0x4ab44b`, the wheel rpm in eax; the speed by `-watchmem`), the
Corvette with the throttle held from rest on Hometown's grid: the wheel
rpm was the speed × 271.8 in first and × 144.8 in second, the
**automatic** gearbox's velocity to rpm ratios (the default gearbox,
copied over the manual ones), not the manual's 256.6 and 171.7. 0 to
16.7 m/s took 2.3 s in first (7.3 m/s², under the cap of 10); it
changed up at 5,237 rpm, 55.1 s.

## The human's car each tick (`0x4ad7a3`)

All in the **road's frame**: the matrix at car +0x190 (rows right
+0x190, up +0x19C, forward +0x1A8, from the road under the car).

1. Velocity into the road's frame: +0x4EC lateral, +0x4F0 up, +0x4F4
   forward (zero before step 512, the start). Spin likewise into
   +0x4F8.
2. Surface grip +0x20C = `0x55ed30`[+0x18C]: 16 values, 1.0, 1.0, 0.75,
   0.8, 0.98, 0.8, 0.65, 0.95, 0.75, 0.6, 0.98, 0.95, 0.95, 0.8, 1.0,
   0.6.
3. Steering, controls (`0x4a9bc4`), the drive (`0x4aaed1`). The drive is
   split: each front wheel × front drive ratio, each rear wheel × the
   rest.
4. Each wheel (4 records of 0x30 bytes on the stack) starts with −½ of
   its own slip velocity × 32 (`0x4ad9d7`): the force that would stop
   half the sliding this tick.
5. **Brakes**: brake × max braking deceleration, at most |v| × 32, against
   the motion; front wheels × front bias (+ 0.05 when spinning), rear
   the rest.
6. **Loads** (`0x4aab41`): each wheel's grip +0x5D8-+0x5E4 = road normal
   y (kept 0.75-1) × its surface's grip, × 0.85 in rain or 0.8 in snow
   (+0x160 1 or 2), × 1.41 with the traction assist (`0x6fd4a4`) or
   `GOFAST`. Over 10 m/s, downforce: +0x5C8.. = 1 + v × downforce (rear
   × 1.5). Then wheel load = −gravity's up part × lateral grip
   multiplier × wheel grip; front × front grip bias, rear × (1 − bias).
   **Weight transfer** +0x5BC = forward acceleration (+0x4E8) × g
   transfer − gravity's forward part × 0.1 (`0x4ae116`), moved from
   front to rear (`0x4ae5c2`).
7. **Tyres** (`0x4aca4f`, each wheel): force limited by the load (the
   friction circle); ABS clamps the brake to the grip and pulses every
   4th step; without it a locked wheel slides (`0x4ac094`); slip angles
   by atan2 (`0x4ea9c0`) and sines.
8. **Integrate**: forces averaged by axle into +0x4E0 (lateral) and
   +0x4E8 (forward), into the world (+0x2E8), velocity += /32
   (`0x4ae188`). Yaw: (front − rear lateral) × +0xB0 × +0x100 / 32 into
   the spin (+0xE4-+0xEC). Lateral damping (`0x4ae85b`), and with the
   spin flag the speed × 0.9985 or 0.9995 (`0x4ae98b`).

In the air or tipped (+0x8A set, or +0x10C < 0.9, `0x4ae4c1`) the rpm
falls 500 a tick to the minimum and nothing drives.

## Car dynamics, every car (`0x4a2830`)

Skipped before step 512 (the start) and for a car not in play (+0x89).

1. `0x4a2260`: **downforce**, over 10 m/s on the road: velocity's up part
   (car frame) −= downforce (doubled for computer cars) × forward speed;
   **in the air**, a drag by axis (0.006, 0.002, 0.004; traffic 0.006,
   0.004, 0.004).
2. `0x4a00d0` the road's height under the car (+0x158; plane at +0x124,
   normal +0x130), `0x4a21a0` a roof over the road, `0x4a0440` the lowest
   corner's height over the road (+0x15C, from the half sizes +0x114,
   +0x118, +0x11C).
3. +0xB8 = max(|vx|, |vz|) + ¼ min(...), the cheap ground speed.
4. **Gravity**: in the air (+0x164 > 0), vy −= 0.306563. On the road,
   gravity is split along the road's three axes: the up part is always
   added; the side and forward parts only on a steep road (normal y <
   0.87); on a flatter one **the forward part is added while the car is
   upright** (+0x10C > 0.9, `0x4a2af7`).
5. Clearance < 0.6: on the road (`0x4a1610`: push out of the road,
   remove the into-road speed under 0.5 m/s, turn the body towards the
   road at most 0.5 rad/s) or landing (`0x4a1ce0`). Else in the air:
   +0x164 counts, spin kept within ±2.4 (`0x4a20c0`).

## The creep on the grid

A car standing on the grid after the start moves at 0.1-0.24 m/s with no
throttle. It is **gravity's along-the-road part** (step 4 above): the
code adds it on any road flatter than normal y 0.87, and nothing holds
a car at rest (no rolling resistance, no hand brake; the brake only
works when pressed, and the engine gives nothing at zero throttle).

Checked at run time: Hometown's grid road has forward y −0.009 (car
+0x1AC), so 0.306563 × 0.009 = 0.0028 m/s a tick; the car's speed
(+0x4F4) rose from 0 by 0.0025 a tick. It levels off (0.245 m/s, then
0.21 and 0.14 as the road under it changes, then 0.125 where forward y
is −0.004), so a loss of about 1% of the speed a tick balances it; which
code gives that loss wasn't found.

## Other car fields

- **+0x8, the race distance** (int; the network status message sends
  its low 16 bits). `0x42ce10`, from the Sc32 task `0x42ce70`: lap
  (+0x204) × the track's road slices (`0x5dd940`) + the car's slice
  (+0x1C), or slices − slice when the track is run backward; 0 while
  +0x208 > 0. Seen at run time counting 1, 2, 3... as the car drove
  off the grid (`-watchmem` on car +8).
- **+0x8D4, the hit's smoke timer.** An impact harder than 30 (5 for
  traffic; strength +0x168) starts it at strength × 0.15 (`0x42ad35`).
  Each tick (`0x42b480`, from the +0x61C task) it counts up and puts
  particles at the hit point (+0x8D8, car frame): kind 6 to 16, kind 9
  to 64, then `0x4caea0`. It stops when the car is back on its wheels
  and its spin is under 0.2 (`0x42b64d`). While it runs, the count at +0x998
  that the sound code keeps (`0x41188f`) doesn't move. Not damage.
- **`0x471270`**: the force-feedback jolt of an impact, for the car on
  the force-feedback device (`0x471dc0`): the hit point (+0x178) in the
  car's frame, its direction by atan2, and the strength.

## How it was worked out

- The field offsets of P from the debug writer `0x4b2340`, whose jump
  table (`0x4b218c`) has one case per label; each case reads one P
  offset.
- Readers of P: every load of car +0x528 followed through registers and
  stack slots by a script over the disassembly.
- The tasks read instruction by instruction (`0x4a2830`, `0x4ad7a3`,
  `0x4aaed1`, `0x4a78d6`, `0x4aab41`). Constants read from the
  executable.
- The race settings from the option rows at `0x5563e8`-`0x5566f4` and
  `text.eng` (`reluva-tool text-dump`).
- Runs: `nfs-host -native -deterministic 50000` with the usual keys,
  `-watchmem` on the player's car (record `0x5e1120`, records 0x9AC
  bytes apart, found with `-watch 0x4ad7a3`): +0x4EC-+0x4F4,
  +0x1A8-+0x1B0 and +0x8; `-keys` with `52000:+up,64000:-up` for the
  throttle.

## What isn't known

- The tyre model (`0x4aca4f`, `0x4ac094`, `0x4ac3af`) in full: the slip
  curves and how the slide fields of `carp.txt` (38-44) enter.
- The steering (`0x4aa2c2`, `0x4aec27`), gear changes and the automatic
  gearbox's shift points; the gas and brake curves' readers.
- The landing and suspension (`0x4a1ce0`, `0x4a1210`).
- The computer cars' model (`0x4088f0`, `0x4068a0`).
- What gives the creep's loss of about 1% a tick.
- What +0x208 is (it zeroes the race distance).
