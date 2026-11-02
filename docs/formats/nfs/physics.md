# Need for Speed III: car handling (`carp.txt` and the car tasks)

How `nfs3.exe` turns a car's `carp.txt` into driving: where the file's
fields go, which per-step tasks use them, and the race settings that
change them. Worked out in October 2026 from the retail `nfs3.exe` (its
disassembly) and runs in `nfs-host` into a Hometown race. The step
tasks themselves are listed in
[nfs-threads.md](../../engines/nfs-threads.md); this is what they do.

Status: **read from the code**, with the steering, tyres, hand brake,
landing, the computer cars' controllers and the police chase's start
checked at run time (below). Not
implemented in Reluva.

## In short

- `carp.txt` is read into a 0x4AC-byte record (P below). The car record
  keeps a pointer to it at **+0x528**. The race's set-up then changes P
  in place: the car set-up sliders, the automatic gearbox, the assists
  and codes (`0x4a78d6`).
- The **human's car** runs a four-wheel model every 32 Hz tick
  (`0x4ad7a3`, from the +0x608 task): engine and gears, brakes, four
  tyres with their loads, then the velocity and spin. The computer's
  cars use a simpler model (`0x4088f0`).
- Many `carp.txt` fields do nothing in this build: the gas curves,
  wheel base, tyre specs, tyre wear and the slide and turn fields
  (38-44). The tyres use the load, the lateral grip multiplier, the
  minimum steering acceleration and the set-up's Tire Factor.
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

The cheat codes are bits of `0x6fd4fc`, set by `0x4525ed`-`0x452861`
from the names at `0x5594e4` (each letter + 9): 1 `GOFAST`, 2 `TDRIVE`,
4 `MONKEY`, 8 `RUSHHOUR`, 0x10 `BULLHORN`, 0x20 `MADLAND`. `MADLAND`
makes the tyres slide more in rain and snow (`0x4abde0`, below).

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
| +0x1E0, +0x1E8 | gas increasing, decreasing curve [8] (bytes) | not read in the race (below) |
| +0x1F0, +0x210 | brake increasing, decreasing curve [8] (floats) | the brake pedal's ramp (`0x4a9e86`, `0x4a9f2c`) |
| +0x230 | wheel base | not read in the race |
| +0x234 | front grip bias | load split front/rear (`0x4adea1`) |
| +0x238 | power steering | force feedback (`0x4718d1`) |
| +0x23C | minimum steering acceleration | the wheels' angle (`0x4ad613`), the tyres' lateral cap (`0x4acf3a`) |
| +0x240, +0x244 | turn in ramp, turn out ramp | the steering's ramp (`0x4aa354`, `0x4aa2c2`) |
| +0x248 | lateral acceleration grip multiplier | tyre loads (`0x4ade33`), tyres (`0x4acbec`), `0x4ae6ae` |
| +0x24C | aerodynamic downforce multiplier | `0x4a22b9`, `0x4aad74` |
| +0x250 | gas off factor | engine braking (`0x4ab6f7`) |
| +0x254 | g transfer factor | weight transfer (`0x4ae10a`) |
| +0x258 | turning circle radius | steering (`0x4aec27`) |
| +0x25C, +0x268 | tyre specs front, rear (3 ints) | not read in the race |
| +0x274-+0x290 | tyre wear, slide multiplier, spin and slide velocity caps, slide assistance (int), push factor (int), low and high turn factor | not read in the race |
| +0x294 | pitch roll factor | body pitch and roll (`0x4295b7`, `0x42a806`) |
| +0x298 | road bumpiness factor | `0x42952a` |
| +0x29C, +0x2A0 | spoiler type, speed | `0x4ab2f9` (type 8 changes the rpm fall) |
| +0x2A4-+0x2B8 | turn cutoffs (int) and speed modifiers | the computer's cars (`0x4a85e5`-`0x4a8686`) |
| +0x2BC | subdivide level | |
| +0x2C0 | camera arm (written with `0x4b08d0`) | the camera (`0x424484`) |
| +0x2C4-+0x2E8 | the damage and tuning fields (57-66: body, engine, suspension damage, then the seven set-up values) | not read in the race but +0x2E0, Suspension Stiffness: the landing's bounce (`0x4a1d7d`). The set-up sliders come from elsewhere (below) |
| +0x2EC | AI acceleration table, 8 × 14 floats | the computer's cars (`0x40828b`, `0x40657d`) |

"Not read in the race": the only code that touches these fields is
the parser (from `0x4b0bb0`, which first copies the defaults at
`0x55f874`; its stores at `0x4b0e64`-`0x4b106d`, `0x4b1850`-`0x4b1c0d`),
the debug writer `0x4b2340` (`0x4b3033`-`0x4b39e8`) and `0x4b48d0`,
which compares two P records field by field (80 flags). A search of
every read at these offsets, from any register, finds nothing else, and
P is never copied (the only 0x4AC-sized copy is the parser's
defaults). So the gas curves, wheel base, tyre specs, tyre wear and the
seven slide and turn fields (38-44) **do nothing** in this build. The
brake curves were missed before because they are read with a computed
index (P + 0x1F0 + 4 × (brake / 32)).

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
- **`GOFAST`** (`0x6fd4fc` & 1): torque × 1.75, max velocity × 1.75,
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
+0x510 shift timer, +0x50D the hand-brake flag (below), +0x4F4 speed
along the road.

- **Drag**: D = +0x58C × v³ × 0.367572 × 2/3 (`0x4ab000`), v = +0x4F4;
  over the maximum velocity, + 0.01 × (|v| − max)².
- **Over-rev**: rpm above redline + redline/3 sets +0x50D; rpm is
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

## Gears

The gear is +0x50B (0 reverse, 1 neutral, 2 first...), the gear asked
for +0x506, the gear before +0x50A, +0x50C 1 for a change down. A
change sets +0x510 to the shift delay (P +0x10, ticks) and plays sound
13 (`0x416d90`) for the player.

- **Manual** (`0x48f6e0`, from `0x4314c4`): control bit 0x10 asks one
  gear up (to number of gears − 1), 0x20 one down (to reverse).
  `0x4aa1b4` makes the change at once. `0x4a9769`, every tick in gear:
  in fourth or higher below first gear's top speed (redline ×
  +0x554), it drops to third.
- **Automatic** (`0x48f710` for the keys; `0x4a90a0`, every tick in
  first or higher). With R the redline, the "rpm" of a gear g is
  int(+0x5A4 × velocity to rpm[g]):
  - the top gear used is the last, or the one before it when that one
    already reaches the maximum velocity (redline × +0x54C[n − 2]);
  - **change up** when the rpm in this gear is over **R − R/8**
    (5,250 for the Corvette) and this isn't the top gear, but only
    with throttle over 0.9 or the rpm over R;
  - **change down** two gears or one with throttle over 0.9 (only one
    with less throttle), when the rpm in that gear would be under R − R/6 (under
    R/2 when that gear is first) and |+0x5B0| < 0.05 (0.1 with less
    throttle, one gear down);
  - from neutral or reverse, first is picked as the last gear whose top
    speed (redline × +0x54C[g]) is under the speed (`0x4aa137`), so
    first when stopped.
- **Both boxes** (`0x4a97cb`, from the controls): from step 572 on,
  stopped (|velocity| < 0.5) with the brake held over half and no gas,
  first or neutral changes to reverse; in reverse the gas and brake
  bytes are swapped, so the brake key drives. Stopped with no gas in
  reverse or first and the rpm under min + 50, the car goes to
  neutral, its brake is held full and its road-frame velocity zeroed
  (`0x4a9b07`-`0x4a9b52`). Before step 572 the automatic always asks
  for first (`0x4aa004`).

Checked at run time (`-watch 0x4ab42f`, the gear in dl; `-watch
0x4ab44b`, the wheel rpm in eax; the speed by `-watchmem`), the
Corvette with the throttle held from rest on Hometown's grid: the wheel
rpm was the speed × 271.8 in first and × 144.8 in second, the
**automatic** gearbox's velocity to rpm ratios (the default gearbox,
copied over the manual ones), not the manual's 256.6 and 171.7. 0 to
16.7 m/s took 2.3 s in first (7.3 m/s², under the cap of 10). With
`-watch 0x4a93ad` (the gear's rpm in eax) and `-watch 0x4a9414` (the
new gear in al), the changes up came at 5,261 rpm (to second, 55.1 s)
and 5,265 (to third, 64.9 s): the first tick over 5,250. The 5,237
seen before was the engine's rpm a tick earlier.

## The human's car each tick (`0x4ad7a3`)

All in the **road's frame**: the matrix at car +0x190 (rows right
+0x190, up +0x19C, forward +0x1A8, from the road under the car).

1. Velocity into the road's frame: +0x4EC lateral, +0x4F0 up, +0x4F4
   forward (zero before step 512, the start). Spin likewise into
   +0x4F8.
2. Surface grip +0x20C = `0x55ed30`[+0x18C]: 16 values, 1.0, 1.0, 0.75,
   0.8, 0.98, 0.8, 0.65, 0.95, 0.75, 0.6, 0.98, 0.95, 0.95, 0.8, 1.0,
   0.6.
3. Controls (`0x4a9bc4`), the front wheels' angle (`0x4ad2e1`), both
   pedals (`0x4aeffc`), the drive (`0x4aaed1`); all below. The drive is
   split: each front wheel × front drive ratio, each rear wheel × the
   rest. The front wheels' slip velocity gets + Y, the rear − Y, a yaw
   term (`0x4af254`): Y = |+0xE8| × 32 × +0xB0 × +0x100 × k, k =
   0.0125 × +0x5A4 − 0.25 kept in 0.1-1, halved in reverse. The code
   takes the absolute value of the yaw rate, so Y has one sign.
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
7. **Tyres** (`0x4aca4f`, each wheel; below).
8. **Integrate**: forces averaged by axle into +0x4E0 (lateral) and
   +0x4E8 (forward), into the world (+0x2E8), velocity += /32
   (`0x4ae188`). Yaw: (front − rear lateral) × +0xB0 × +0x100 / 32 into
   the spin (+0xE4-+0xEC). Then `0x4ae79b`: gas ≥ 0.5, brake ≥ 0.75,
   |steering| > 64 and under 5 m/s zeroes the velocity. Lateral damping
   (`0x4ae85b`): the lateral speed × 0.9 under 1 m/s, × 0.99 over 2,
   linearly between. With +0x50D the speed × 0.9985 or 0.9995
   (`0x4ae98b`). **In neutral** (`0x4af140`, from `0x4ae4b3`) on a road
   whose gravity along it is under 0.5 m/s² (`0x7a3a48`): velocity and
   spin × 0.99 a tick under 20 m/s or with |steering| > 32, else ×
   0.998.

In the air or tipped (+0x8A set, or +0x10C < 0.9, `0x4ae4c1`) the rpm
falls 500 a tick to the minimum and nothing drives.

## Controls (`0x4a9bc4`, each tick)

The control frame is unpacked into the car at `0x431462`-`0x431545`:
+0x504 gas (byte), +0x505 brake (byte), +0x518 steering (signed byte,
−128..127), +0x506 the gear asked for, and from the bits of
`0x79c4ab`: 0x04 → **+0x50D**, 0x08 → +0x50E, 0x80 → +0x512, 0x40 the
view. +0x50D is the **hand brake**: bit 0x04 comes from slot 6 of the
key bindings (`0x48f1f0`, `0x48f550`), **Space** by default ([net.md](net.md), the input word),
and it acts while held (the edge mask 0xF2 at `0x48f6be` leaves it
alone). Over-rev and over-speed also set +0x50D. Checked at run time
(`sw1-a`): with Space held from 64.0 to 68.0 s, `0x4314d8` (bit 0x04
set) ran 128 times, from 64.05 to 68.03 s, 32 a second, and at no
other time.

- **Gas**: +0x508 moves towards +0x504 by at most 16 a tick up and
  16 down (+ (set-up +0x48 − 1) × 60 down, single races only).
  Throttle +0x5F0 = +0x508 / 255. The gas curves are not used.
- **Brake**: with the flag at +0x2C of the car's set-up record (car
  +0x220), +0x509 moves towards +0x505 by brake increasing[+0x509 / 32] a tick up and brake
  decreasing[+0x509 / 32] down (`0x4a9e28`-`0x4a9fa6`); without it
  +0x509 = +0x505. Before the start (`0x7d3680` 0) the brake is held
  at 255.
- **Steering**: with the set-up record's flag +0x24 (on with the keys), +0x51C moves
  towards +0x518 by at most the **turn in ramp** a tick (16 for the
  Corvette: full lock in 8 ticks), or back towards 0 by the **turn out
  ramp** (32) when the two have opposite signs (`0x4aa278`-`0x4aa3c3`).
  Without it (an analogue wheel) +0x51C = +0x518.
- With the braking or steering assist on, `0x4a83d3` drives the pedals
  instead of the ramps (The assists' driver, below).
- A car with +0x200 bit 1 and +0x2C4 = 3 (out of the race) gets no
  controls, neutral, +0x50D set and its velocity and spin × 0.98 a
  tick (`0x4a9c32`-`0x4a9ccc`).
- **Both pedals** (`0x4aeffc`): gas and brake both ≥ 32, on the road,
  slow (under 4 m/s, or under 5 in reverse or neutral): velocity and
  spin × 0.8 a tick.

## The front wheels' angle (`0x4ad2e1`)

In turns (one turn = 2π, as `0x496bf0` and `0x4ea930` take angles).
s = +0x51C, v = +0x4F4:

1. In the air: 0.
2. With +0x50D, moving, steering against +0x5B0's sign: s × max(0.85 −
   0.0056 |v|, 0.55).
3. Else over 26.7 m/s with |+0x5B0| > 0.05 and steering the same way:
   s × 2|+0x5B0| when that is under 1; over 13.4 m/s the same with
   0.15.
4. Over 40 m/s: s / (0.015 |v| kept in 1-1.5): from 67 m/s up to a
   third less lock at 100.
5. angle = s / 128 × **minimum steering acceleration** × 1.5 / 360
   turns. For the Corvette (17.1) full lock is 25.4°.

The code then clamps the angle to ±1 but returns the unclamped value.

Checked at run time (`sw1-a`, the Corvette, keypad 6 held; `-watch
0x4ad663`, the angle in eax, and `-watchmem` on +0x51C): +0x51C went
16, 32 ... 112, 127 a tick and back 95, 63, 31, 0, and every angle was
+0x51C / 128 × 17.1 × 1.5 / 360 to the last digit (0.070693 turns at
127). With Space held as well the angle was × 0.6965 at first, rising
to × 0.755 as the car slowed: rule 2's 0.85 − 0.0056 |v| for 27 to 17
m/s; on some ticks it was × 1 (+0x5B0's sign the other way).
The angle turns the front wheels' slip velocity into the wheel's
frame (`0x496bf0`) before the tyre and the tyre's force back. The
yaw comes from the axles' lateral forces (step 8 above).

## Tyres (`0x4aca4f`)

Each wheel record (0x30 bytes on the stack at `0x4ad7a3`'s ebp−0x14C,
front pair first): +0 drive and brake force, +4 load, +8 downforce
factor, +0xC steer angle (front), +0x10, +0x18 the lateral and forward
slip "forces" (−16 × the wheel's slip velocity), +0x1C, +0x20, +0x24
the lateral, up and forward force out, +0x28 1 for a front wheel,
+0x2C the skid level. The forces are accelerations (m/s²).

S, the **Tire Factor** (set-up +0x58, t; 1 in Tournament, Knockout and
network games), is a loss of grip (`0x4abd9c`):

| Weather (+0x160) | S | at t = 1 | with `MADLAND` |
|---|---|---|---|
| 0 dry | 1 / max(28 − 40(t − 0.8), 16) | 0.050 | the same |
| 1 rain | 1 / max(24 − 32(t − 0.8), 6) | 0.057 | 3 / max(16 − 32(t − 0.8), 6): 0.31 |
| 2 snow | 1 / max(16 − 24(t − 0.8), 4) | 0.089 | 3 / max(12 − 24(t − 0.8), 4): 0.42 |

G = load × (1 − S) is the wheel's grip.

1. Rotate the slip forces by the steer angle.
2. When the drive force and the forward slip force have the same sign,
   the drive is limited to the slip force (braking can't reverse the
   car) unless driving with throttle over 0.25 in that direction.
3. Drive force × **lateral acceleration grip multiplier** (3.3).
4. With +0x50D, +0x514 counts up 1 a tick (3 in rain, 4 in snow) to
   384; else 0. A rear wheel then goes to `0x4ac094` (below), a front
   one too when its drive force is over its load.
5. **ABS** (P +0x1D4), when the force is over the load: clamp it to the
   load; the skid level is 3 every 4th step, else 0. Without ABS
   nothing is clamped here.
6. **Slip angle** α = atan2(lateral, |forward|) / 2π (turns,
   `0x4ea9c0`). The lateral limit L = |4 G sin(πα)| (|2 G sin(πα)| in
   reverse), at most 2 × **minimum steering acceleration** (34.2).
   Under 13.4 m/s with no gas (under 0.015, or under 0.02 at full
   lock): L / 4.
7. The lateral force out = the lateral slip force kept within ±L,
   **× 0.8 at the front, × 1.4 at the rear**. A front wheel with
   +0x50D under 0.5 m/s: 0. Up 0. Forward = the drive force.
8. **Friction circle** (`0x4ac3af`): M = max(|lat|, |fwd|) + ¼
   min(...). Only when M > G:
   - traction assist on, rear wheel, |+0x5B0| > 0.15: both forces ×
     G/M (the lateral only above first gear); skid level 2G on three
     steps of four;
   - else: skid level = (old × k + (M − G) × k), k = 0.95 − t/4 (0.7
     in Tournament...), at least 0.5; the kinetic grip G − R, R =
     min((M − G)S, GS) × 1 dry, 1.5 rain, 2 snow. Only a rear wheel in
     first on a surface of grip ≥ 0.95, throttle ≥ 0.85 and rpm ≥ 0.85
     × redline has its forces scaled to it (× (G − R)/M; the forward
     × another 0.5 without traction assist): the launch's wheelspin.
     Otherwise the forces stay over the circle.
   - A rear wheel over 1.15 × the gear's top speed (+0x594): skid level
     = M, × 4 in first and second.
   - The skid level is kept at most int(12 − 4t) (`0x4ac027`) and
     stored at +0x5C0 (front) or +0x5C4 (rear); `0x42a699` (× 7.5, at
     most 10) and the force feedback (`0x47179f`) read it.
9. Rotate the forces back by the steer angle.

Checked at run time (`sw1-c`): `-dumpmem at=0x4acf74#N` of the stack
(the frame is at the same place each call, ebp `0x17fdb5c`) and of the
car, four wheels at 61.7 s (23.7 m/s, third gear, full right lock) and
four at 62.3 s (wheels straight). For each, G = load × 0.950 (dry, t =
1), α = atan2(lateral, |forward|) / 2π, and L = 4 G sin(πα) agreed with
the stored values to the last digit: front 0.1010 turns and L 22.6 with
the lock on, rear 0.0295 and 7.75. The forward slip force was about
−16 × the car's speed: the wheels have no spin of their own.

**The hand-brake wheel** (`0x4ac094`): +0x59C counts the wheels (1 a
front, 2 a rear). G = load × downforce factor × (1 − S), M as above;
f = G/M if M > G, else 0.75. Lateral out = lateral slip force × f ×
H, forward = drive force × f; in neutral both × 0.05. H (`0x4ac9dd`)
falls with the time held, x = +0x514 / 384: H = 0.75 × (1 − 0.8 / (1 +
e^(−4π(x − 0.5)))) (1 while +0x514 is 0), from 0.75 to 0.15, half way at 6 s dry (2 s in
rain, 1.5 s in snow).

## Car dynamics, every car (`0x4a2830`)

Skipped before step 512 (the start) and for a car not in play (+0x89).

1. `0x4a2260`: **downforce**, over 10 m/s on the road: velocity's up part
   (car frame) −= downforce (doubled for computer cars) × forward speed;
   **in the air**, a drag by axis (0.006, 0.002, 0.004; traffic 0.006,
   0.004, 0.004).
2. `0x4a00d0` the road's height under the car (+0x158; the plane through
   the road point +0x148 with up +0x130, below), `0x4a21a0` a roof over the road, `0x4a0440` the lowest
   corner's height over the road (+0x15C, from the half sizes +0x114,
   +0x118, +0x11C).
3. +0xB8 = max(|vx|, |vz|) + ¼ min(...), the cheap ground speed.
4. **Gravity**: in the air (+0x164 > 0), vy −= 0.306563. On the road,
   gravity is split along the road's three axes: the up part is always
   added; the side and forward parts only on a steep road (normal y <
   0.87); on a flatter one **the forward part is added while the car is
   upright** (+0x10C > 0.9, `0x4a2af7`).
5. Clearance < 0.6: on the road (`0x4a1610`: push out of the road,
   zero the speed along the road's up when it is under +0.5 m/s, turn
   the body towards the road at most 0.5 rad/s) or landing (`0x4a1ce0`, below). Else in
   the air: +0x164 counts, spin kept within ±2.4 (`0x4a20c0`).

**Landing** (`0x4a1ce0`): when the velocity (+0xA4) goes into the road
(its dot with the road's normal is negative), `0x4a1210` finds a part
of the body under the road, and the car is upright (+0x10C > 0.9):

- e = 0.1 × P's **Suspension Stiffness** (+0x2E0) × the set-up slider
  (+0x50; 1 in Tournament, Knockout and network games), kept in 0-0.2;
- in the road's frame (`0x4e0110` of +0x124), the speed along the
  normal × (0.5 + e) (`0x4a1e68`), the spin (+0xE4) × (0.5 + e);
- an impact: strength +0x168 = 4 × |the speed into the road|, +0x16C
  0, +0x170 = the surface | 0x10000, points at 0.75 of the half
  sizes.

**The speed into the road is not reversed.** The landing scales it
and keeps its sign; nothing later turns it round. Each tick that
+0x164 is set and the clearance is under 0.6, the landing runs again
(`0x4a2b3d`); once the speed along the normal is under 0.5
(`0x5efddc`, set at `0x42c636`), every spin is under 0.6, `0x49fde0`
agrees and the clearance is 0 or less, +0x164 is cleared and
`0x4296c0` runs (`0x4a2068`). From then on the on-road code
(`0x4a1610`) takes the speed into the road out. The 0.5 in both tests
is signed: "under 0.5 m/s" means any speed into the road, however
large, and any speed away from it slower than 0.5. So a landing is a
loss, with no bounce. Checked at run time (`sw1-f`): with +0x164 poked
to 1 and the vertical velocity to −4 on Hometown's grid, the body's
contact (`0x42e8f7`) left −3.17, the landing made it −1.90 (× 0.6, e =
0.1; the spin × 0.6 too) and ended in the same tick (`0x4a2071`);
gravity took it to −2.19 and the next tick's on-road code (store
`0x4a1b10`) to −0.0009. It never went positive.

Why all 2.19 m/s went at once: in `0x4a1610`, when the clearance is
0 or less, the velocity is put into the road frame (`0x4e0110` of a
copy of +0x124, `0x4a1a3d`), its up part is compared with `0x5efddc`
(0.5, `0x4a1aa2`) and set to 0 when it is less (`0x4a1aaf`), and the
velocity is put back (`0x4a1ac4`-`0x4a1b10`). There is no "under 0.5"
limit on the size. The frame used is the car's road frame blended half
way (`0x4b7680`, weight 0.5) towards the frame at the point the car
will reach next tick, so the zeroed up is not quite the world's: the
−0.0009 left is that difference.

`0x4a1210` tests twelve points of the body box (its corners and points
at 0.75, 0.5 and 0.8 of the half sizes) against the road slices
(`0x5dd948`). There is no suspension model: the car is a box on the
road's plane. The pitch and roll seen are the body's drawing (pitch
roll factor, `0x4295b7`).

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
is −0.004), so a loss of about 1% of the speed a tick balances it.

**The loss is `0x4af140`** (step 8 above): a car in **neutral** on a
road whose gravity along it is under 0.5 m/s² has its velocity and spin
× 0.99 every tick while under 20 m/s. 0.0025 / 0.01 = 0.25 m/s, the
speed it levels at. The car is in neutral because, stopped with no gas
from step 572, the gearbox puts it there (`0x4a9b07`, Gears above).
Checked at run time: with `-watchmem 0x5e11c4 12` (the player's world
velocity) each tick's stores were `0x4a2a91`/`0x4a2b0e` (gravity),
`0x4ae1a3` (the forces, nothing at rest), `0x4ae2c0` and `0x4e0065`
called from `0x4af221`, which took x from 0.16461 to 0.16295 (×
0.98992); `-watch 0x4e0050` showed its caller `0x4af226` 583 times for
the player's car.

## The computer's cars (`0x4088f0`)

Each tick, per car:

- skipped when `0x4be1a0` says so or +0x620 has bit 4;
- in the air or tipped (+0x8A, or +0x10C < 0.9): no grip, no skid,
  the rpm falls 500 a tick to the minimum, then `0x4aa8e7`;
- before the start (`0x7d3680` 0) velocity and spin are zeroed;
- on the road: velocity and spin into the **car's** frame (+0xC0,
  +0xCC, +0xD8 rows) at +0x4EC-+0x500, then one of three models:
  - **+0x88 set** (`0x4068a0`): no forces, below;
  - +0x620 bit 0x80: `0x408520`, which copies another car's brake
    (+0x509);
  - else `0x408600`, below.

The computer cars use no tyres and no engine torque. They are steered
and pushed by a few clamped controllers, read from the code
(`0x408600` and its callees) and checked at run time (`gh1-ai`, below).

**The fields.** +0x7B0 the speed wanted (m/s, below). +0x7B8 the car's
side offset on the road, +0x7B4 the side offset it wants, +0x7C8 its
present aim, moved towards +0x7B4 by 0.2 a tick (`0x408870`). +0x7C0
is the speed along the road: the velocity (+0xA4) dotted with the
road's forward at +0x13C (`0x42cdd0`); +0x7C4 is its size. Both are worked out
for every car in the Sc32 task `0x42ce70` (stores `0x42cef8`,
`0x42cf25`), which also sets +0x210, the heading (atan2 of the road
frame's forward row +0x1A8 × 1024, so 1024 a turn, `0x42cee9`).

**`0x408600`**, each tick:

1. Tipped (+0xD0 < 0.3): `0x4aa8e7` only. Out of the race (+0x2C4 3 or
   4): `0x407380`, then `0x4061b0`.
2. `0x408870(0.2)` (the aim, above).
3. **Gear and rpm** (`0x4061b0`). With T the track section's speed
   (`0x40ed40`: hard-coded slice ranges per track, 13.41 to 31.29
   m/s): if |+0x7B0| is over T + 10, the rpm aimed at is 0.8 × redline
   (redline − 200 when the car is 20 m/s or more under the speed
   wanted), else 0.5 × redline. The gear picked is the one (from first
   up) whose rpm at this speed (+0x7C4 × velocity to rpm) is nearest
   that aim; reverse while reversing (+0x7D4). A change sets +0x64C to
   the shift delay, and while it counts down the rpm aims at 0.4 or
   0.2 × redline. The rpm moves 1/8 of the way each tick. This is only
   for the sound: no torque is read.
4. **Reversing** (`0x4060d0`, `0x405fd0`): +0x7D4 1 while backing up.
   A hit under 10 m/s starts or ends it for 128 steps (`0x406120`, from
   the impacts task `0x42b9a8`). Each start adds 1 to +0x658; over 4
   the car changes lane, over 8 it is put back on the road (`0x429100`,
   from `0x406e50`).
5. **The heading error** (`0x4070c0`): `0x406e50` picks the point to
   steer at: the target car +0x624 (a chase, or a grudge, below), or
   the road ahead (`0x406d40`, `0x406c00`; a car slow for 1000 ticks,
   counted in +0x7D8, is put back on the road). `0x56ec60` = the car's
   heading (+0x210 / 1024) − the direction to that point, in turns,
   kept in ±0.5.
6. The **speed wanted** is set to 0 before +0x65C's step for a car with
   +0x200 bit 0x10 (traffic).
7. One of three controllers, then `0x4071a0`:
   - reversing, or +0x4F4 < 0: **`0x407c30`**;
   - else, unless +0x200 has 0x200: if the car is outside the road
     (+0x7B8 beyond the slice's lanes: byte 0x13's nibbles × bytes
     0x17 and 0x1B × 0x8000), or |heading error| > 0.04 × the surface
     grip (`0x5523d8`[surface × 3 + weather]: 16 surfaces × dry, rain,
     snow, 0.6-1.0), or +0x7C4 < 0.5, or before +0x65C's step, or
     sliding (+0xB8 > 2.5 and |+0x4EC| > 0.5 × +0x4F4): **`0x407fa0`**;
   - else under 13.41 m/s `0x407c30`, over it **`0x4076b0`**.

The gains are constants set once by `0x405e60` (from the race tasks'
set-up, `0x4c46e1`) at `0x56ebd0`-`0x56ec54`.

**The point steered at** (`0x406e50`, step 5). With no target car:

- +0x620 bit 0x800: `0x406c90`, the car's own slice point, and the
  speed wanted = its distance × 32: drive to a point and stop there.
- On the road's lanes (`0x40f110` 0: the car's lane +0x638 or one next
  to it is within the slice's lanes): `0x406d40`, unless the car has
  been slow (+0x7C4 ≤ 10 m/s) and reversed more than 4 times (+0x658)
  while outside the lanes, then `0x406c00`; more than 8 times puts it
  back on the road (`0x429100`).
- Off the lanes: +0x7D8 counts the ticks under 3 m/s; `0x406d40` while
  bit 0x40 of the count is clear, `0x406c00` while it is set (64 ticks
  each), and over 1000 the car is put back on the road.

With a target (+0x624): a cop (+0x200 bit 0x20) with +0x711 bit 2
within 4 slices of it uses `0x406b60`; with +0x620 bit 0x2000 or 0x8000,
`0x406b00`; else `0x406d40` (the road, with the target only for the
speed).

- **`0x406d40`, the road ahead**: the slice from `0x406980` and the
  point there moved sideways along the car's road right (+0x124) by
  the aim +0x7C8 (10 m more to the negative side on Hometown's slices 0x205-
  0x210 when the aim is negative). `0x406980` starts 4 slices on (the
  way the car runs: +0x621 bit 0x10 xor `0x6fd4c0`) and steps on to the
  look-ahead slice `0x40e1b0(car, 1.7, 4, 20)`, +0x7C4 × 1.7 / 6
  slices, 4 to 20 (backward when reversing), but stops at the first
  slice whose forward, in x and z, has a dot of 0.7 or less with the
  car's slice's (`0x40ef70`): about 1.7 s ahead, or nearer where the
  road turns by more than 45°. Hometown's slices 0x225-0x235 use the
  look-ahead slice as it is.
- **`0x406c00`**: the car's own slice point: back to the road's middle.
- **`0x406b00`**: the target's position; the aim +0x7C8 is set to the
  target's side offset (+0x7B8).
- **`0x406b60`**: the target's position + its velocity × 0.4 (where it
  will be in 0.4 s); the aim likewise.

All return the vector from the car (+0x98) to the point.

**`0x407380`, a car out of the race** (+0x2C4 3, finished, or 4,
busted; step 1 of `0x408600`). Only on the road and upright (+0x164 0,
+0x10C > 0.9). No controls (steering, gas, hand brake 0, brake 255).
In the car's frame, the forward speed × 0.98 a tick (× 0.95 when the
ground speed +0xB8 is under 22.35 m/s); between −10 and 5 m/s × 0.5
more, else the rear skid level is 0.1 × |speed|; the side speed ×
0.85, the yaw spin (+0x4FC) × 0.5 (over 0.2 it adds to the rear skid);
when the side speed is bigger than the forward, both skid levels are
|side| and the side speed × 0.2. Then back to the world. The car coasts
to a stop in a few seconds and its tyres sound. Not hit in `gh1-ai` (no
car finished).

**The drivers** (`0x55226c`, 15 rows of 0x18 bytes, indexed by the
set-up record's +0x30): the name (Ace, Lead Foot, Gotcha, Ram Rod,
Dead Beat, Mulligan, Wild One, then the cops Shield, Smooth, Trigger,
Blue, Moose, Blender, Fancy, Sterno), +4 1 for a cop, +8 the **skill**
0-2, +0xC 0 for a driver who surges, +0x10 a personality 0-5, +0x14 a
colour (`0x404810`, the front end). **`0x404610` returns the skill**,
at least 1 in Tournament and Knockout. Ace and Lead Foot have skill 2,
Mulligan and Wild One 0. It is used:

- the slide on a slippery surface (`0x40841e`, in `0x407fa0`): skill 0
  every tick that `0x408459`'s test passes, skill 1 only when step bit
  0x20 is set, skill 2 only when bits 0x60 are clear: worse drivers
  slide more;
- the lane vote (`0x40974d`): skill 0 votes 6 for the best line, else
  10, or 12 with personality over 2;
- the speed factor `0x408dc0`, below; the braking (no effect, above).

`0x404690` (from `0x40cb70`) sets the grudge limits per slot
(`0x56ea70` + slot × 20) by personality: hits 10 to 110,
times 0x40-0xC0, a count 1-5 (less one with Aggressive).

**The speed scale +0x7BC** (`0x409330`) is the product of
`0x408dc0` × `0x4090a0` × `0x408f10` × `0x40ee80` (two more return 1:
`0x408f00`, `0x409090`), **kept at most 1** (`0x409410`-`0x409449`),
or 1 with `MADLAND`; the speed wanted is × it.

- `0x40ee80`: a per-track factor (`0x56ef94` + track × 32 with `ai`
  Normal outside Tournament and Knockout, else `0x56ef98` + track × 32).
  The rows (`0x56ef80`, 32 bytes a track, tracks 0-8) are filled at
  each race's start by `0x40e410` through `0x40e3c0`, by the race type.
  Single race, Normal then Aggressive: 0.82/0.94 on tracks 0, 3, 5 and
  7, 0.84/0.96 on 1, 2, 4, 6 and 8. Hot Pursuit: 0.78/0.88, 0.82/0.90,
  then 0.80/0.85, and 0.80/0.83 on track 8. Read from memory in a Hot Pursuit
  (`cq-reset1`, at 62 s), the rows were these. Tournament and Knockout use
  only +0x18: Beginner 0.92 or 0.94 by track; Expert 0.98, 0.99 or 1.0
  less 0.005 for each of night, weather, mirrored and backward. Hot
  Pursuit racers are the slowest computer racers. The same rows hold
  the traffic: +0 the most traffic cars in play (`0x40ef10`, read by
  `0x40c210`: 8, 6, 9, 5, 7, 6, 5, 5 and 12 by track in a single race
  or Hot Pursuit, 0 in Tournament and Knockout, 15 with cheat bit 8),
  +4 how long a parked traffic car waits (`0x40eed0`: rand × +4 + 1,
  `0x405d40`), and +0x1C 13.41 m/s. A flag (`0x55eb2e` bit 0x20)
  sets track 3's row to 1, 100, 0.9, 1.0.
- `0x408dc0`, the driver: skill 0 × 0.94, 1 × 0.97, 2 × 1; a surging
  driver (+0xC 0) × 1.05 or × 0.95 in alternate 128-step quarters of
  512 steps; personality 0 × 0.95, 1 × 0.97, else 1 (with +0x620 bit
  0x20, 0.95 + 0.05 × personality instead). 1 in a one-machine Knockout.
- `0x4090a0`, the **catch-up**. `0x404c80` (each step, from the human's
  states) keeps the human furthest on (`0x56ebb8`, race distance +8; a
  cop counts as 1) and `0x56ebb4`: 0 when no computer racer is ahead
  of him, 1 when some are, 2 when all are. With 0: a car more than 15
  slices behind him (10 Aggressive, 5 in an Expert Tournament or
  Knockout) gets × 1.1 (1.15 in Tournament and Knockout). With 1: a car
  more than 50 slices ahead (70, 100) × 0.85. With 2: × 0.95
  Aggressive, × 0.85 Hot Pursuit, else × 0.9 in places 1-5 or a
  network game, × 0.8 behind. Never under 1 in Knockout on track 8,
  never over 1 in a network game. As the product is kept at most 1,
  the catch-up's × 1.1 only undoes the other factors.
- `0x408f10`: in a network game with positional updates, a factor
  by the distance to the local player (1 + (2 − slices) × `0x53640c`).

**Seen in footage** (the 17 race clips, 8 cars, 2 laps). The racer
list at the bottom left is in place order, each name in its driver's
colour (`0x404810`: Lead Foot green, Ram Rod and Dead Beat blue,
Mulligan red, Wild One yellow, Gotcha and Ace white), with the gap to
the player: red "−" ahead, green "+" behind, dashes for the player
(gap from `0x485a60`). In `Hometown_Day` the player leads from early
in lap 2 and the gap to second grows steadily: Lead Foot +0:00.71,
1.23, 1.64, 1.81, 2.54, 3.85, 4.73, 5.50 every 8 s, the last car from
7.9 to 17 s. Nobody is pulled back towards the player: with no
computer racer ahead the catch-up's × 1.1 only cancels other factors
(the product is kept at most 1), as read. The gap on the HUD at the
line (Ram Rod +0:05.07) is not the finish difference (4.56 s on the
results): it is an estimate. Traffic is seen in both directions; no
cop car was seen in the race clips (one frame every 15 s of four
clips looked at). Lane changes and the AI's recovery weren't measured.

**Checked at run time** (`gh1-ai`: Hometown, single race, seven
computer racers, keypad 8 held from 52 s; `-watch` on the three
controllers' first instructions, eax the car, and `-watchmem` on car 1
+0x7B0-+0x7CC). Every car ran a controller every tick from the start
(48.9 s), about 1,300 calls each by 90 s. For car 1 (`0x5e1acc`): all
63 calls of `0x407c30` came with +0x7C4 under 13.41 m/s, all 1,166 of
`0x4076b0` over it; it changed from one to the other at 50.98 s, at
13.50 m/s. `0x407fa0` ran 65 times: the first tick (+0x7C4 0, under
0.5) and in short bursts while the aim +0x7C8 moved between lanes
(4 → 12 and back) or after a bump (71-77 s). The aim moved 0.2 a tick
towards +0x7B4, which took the values 4.0, 12.0 and 17.1 (lanes) and
2.6-3.75 after 75 s. The speed followed the speed wanted to within
about 0.5 m/s (43.6-43.7 for 44.0; 36.9 for 36.5), from 0 to 12 m/s in
1.8 s. +0x7BC was 0.66 throughout (the human behind all of them).

- **`0x4076b0`** (on the road, lined up, fast): yaw rate wanted = −2.5
  × heading error, in ±0.8; side term = −5 × (+0x7B8 − +0x7C8), in ±32
  and ±0.5 × +0xB8; under 22.35 m/s these are kept within 0.129 and
  0.012 × +0x7C4. Yaw force (into +0x2F8) = −5 × (yaw rate − wanted),
  in ±0.8. Side force = (heading term − side term) × 4 × (1 + 0.0015 ×
  speed), at most 38 × (1 + 0.0015 × speed); both × +0x7BC when it is
  over 1, which never happens: `0x409330` keeps +0x7BC at most 1. Forward force: the acceleration below when under the speed
  wanted, else minus the braking. Over 44.7 m/s a racer's drive loses
  3.45 / P's lateral grip multiplier (others: |side force| / (speed /
  2)). Skid marks when |heading term| > 7.
- **`0x407fa0`** (sliding or off the line): the same kind of
  controller on the side *velocity* (+0x4EC) rather than the offset,
  with its own gains (`0x56ec30`-`0x56ec54`: 32, 40, 6, 3, 3, 1, 10,
  0.2, 0.04).
  On a surface with grip under 1, an AI racer (+0x200 & 8) every few
  steps gets its forces × 1.1 (Aggressive) or the yaw × 50 and side ×
  10 (Normal), by `0x404610` and the step's bits (`0x408459`): it
  slides. In the first 20 of race distance, one AI racer picked by a
  random number (`0x554f10`) gets a push sideways at the start.
- **`0x407c30`** (slow or reversing): the speed wanted is kept under
  16.41 m/s; yaw from the heading error (× 2.5, in ±1.6, and ±0.02 ×
  +0xB8); the side speed damped (× 3, in ±38).
- **`0x4071a0`**: the road-frame force (`0x56ec6c`-`0x56ec74`) and the
  car-frame force (+0x4E0) go into the world (+0x2E8), velocity +=
  force × the tick; spin += +0x2F4 × the tick; `0x56ec68`, when set,
  damps the velocity.

**The acceleration** (`0x4064f0`): P's AI acceleration table (+0x2EC,
112 floats) at int(|+0x7C4| − 0.5), so **one value per m/s**, 0-111.
It is × 0.9 with `ai` Normal after race distance 100, × 0.9 in rain or
snow under 45 m/s, × 1.3 while +0x6E4 > 0 (the grudge, below). In a
network game with positional updates (`0x6fd50c`), a car other than
the local player's that is 3 or more slices behind it or 15 or more
ahead gets × 2 (within −8..30 slices) or × 3. Traffic under 2 m/s: at most 12. A cop
over 20 m/s with `0x55eb2e` & 0x20: × 0.5. A racer is held under P's
top speed cap × +0x7BC. **The braking** (`0x4063e0`): brake 255 and
P's maximum braking deceleration, × 2 for a cop, × 2 going backward, ×
1.25 for an AI racer. `0x404610` is called there (`0x40648a`) but
every branch after it goes to the same `0x40649f`: the skill changes
nothing.

**The grudge** (`0x404a00`, from the impacts task `0x42b9af`): with
`ai` Aggressive, an AI racer hit by a human's car (not a cop's) takes
it as its target (+0x624) and counts the hits at +0x6E8; past a limit
for its slot (`0x56ea78` + slot × 20) +0x6E4 gets a time (`0x56ea7c`),
and the car drives at the human with × 1.3 acceleration.

**`0x4068a0`** (+0x88 set, at `0x46681e`): no forces. `0x4066c0` moves
the speed along the road +0x7C0 towards the speed wanted by the
acceleration (or braking) × the tick, signed −1 for a car running the
track the other way (+0x621 & 0x10 xor `0x6fd4c0`), not past it; +0x7C4
its size. Then velocity = the car's forward row (+0xD8) × +0x7C4 + the
vector at +0x124 × 8 × (+0x7B4 − +0x7B8) (`0x406880`), the spin zeroed,
+0x4F4 = +0x7C0, and the car put on the road (`0x4a0980`).

- The **speed wanted** (`0x409330`): +0x7B0 = the speed table's byte
  for this slice pair (m/s). Over 67.05 m/s and with +0x700 < 1 it becomes
  67.05 + (speed − 67.05) × +0x700. It is kept under P's top speed cap
  × `0x40ee80`'s factor.
- **speedsF.bin / speedsR.bin** (`0x40e000`; R when `0x6fd4c0`, the
  track run backward): after a 16-byte header, one byte per two road
  slices, the speed (`0x40e080`; 64 without the file); then one byte
  per slice whose high 4 bits `0x40e0c0` returns (7 without the file,
  `0x56f22c` set).
- **The slice nibble is the best line's lane.** Lanes are numbered as
  +0x638 (the car's lane, from `0x40e2b0`), 7 the middle.
  `0x4096b0` (from the cop states `0x4057db`, the AI driver `0x409ca6`
  and `0x40bfbd`) takes the nibble 4 slices ahead and adds a vote for
  it (2, or 6, 10 or 12 by the driver) to the lane choice's counts for
  left, same and right of the car's lane (`0x56ef14`, `0x56ef18`,
  `0x56ef1c`). `0x493a30` counts each step whether the car is on that
  lane (its middle or either side, half width +0x114): +0x2DC on (and
  every step before 1152), +0x2E0 off: the race statistics.

**The assists' driver** (`0x4a83d3`, from `0x4a9d1d` in the controls
with `0x6fd490` braking or `0x6fd494` steering on, not out of the
race). It drives only the pedals:

1. The player's pedal p = gas − brake + 256 (0-511), moved at most 32
   a tick (+0x218).
2. The bend: the road's heading 10 slices on (10 back when the car
   faces more than a quarter turn from the road; `0x49fee0`) − the
   heading at the car's slice (+0x154), in ±512, smoothed: +0x214 =
   (7 × +0x214 + bend) / 8.
3. Its size against P's turn cutoffs (+0x2A4, +0x2A8, +0x2AC) picks a
   speed modifier (+0x2B0, +0x2B4, +0x2B8; 1 under the first).
4. Target = the speed table 7 slices ahead (−7 backward) × the modifier.
   With e = target − speed, +0x520 (256 = neither pedal): e > 1 adds
   min(int(0.5 e) + 4, 16); e < 0 adds max(int(0.67 e) − 8, −24);
   else it is set to 256 + 256 × (wheel rpm − min) / (redline − min).
   It is kept at most p and at least 0.
5. Under 255: brake 255 − +0x520, no gas; else gas +0x520 − 256.

So the assist brakes before bends and lifts off, but never gives more
than the player's own pedal. It does not steer: with the steering
assist on, the police states (`0x405740`, state 2) run the computer's
lane functions (`0x40dd60`, `0x4096b0`, `0x40ddb0`, `0x409660`,
`0x40de30`) for the human's car. **For a human they change nothing
that drives.** They set the car's lanes (+0x638, and +0x63C, +0x640
for its two sides, `0x40e350`), fill the lane votes (`0x56ee90`-
`0x56ef78`, cleared each call by `0x40dd60`), pick a lane
(`0x40da30`, into `0x56ef74`) and write the side offset wanted +0x7B4
(`0x40daf0`). The only readers of +0x7B4 are the computer's code
(`0x408870`, `0x406880`) and the warp `0x40cd80`; the human's tick,
controls and the assists' driver read neither +0x7B4 nor `0x56ef74`
(every access listed with `field car 0x7b4` and `xref 0x56ef74`).
+0x638 is read anyway by the best-line count `0x493a30`, but
`0x40e350` sets it in the other states too. In the wheels' angle,
`0x4ad320` compares `0x6fd494` and then adds a term that is always 0
(`[ebp−4]`, `0x4ad330`): a steering assist that was taken out. The
steering assist is the pedal driver and nothing more. Hot Pursuit's
set-up (`0x4746ab`) turns it off.

## The Hot Pursuit states (`0x405740`, car +0x6E0)

Run by the human's driver (`0x431455`) when there are computer cars or
cops, or the steering assist is on. A human's car is a cop when its
set-up record's role (+0x18 of the 0x6C-byte record at `0x6fd52c`) has
8: `0x474732` sets it in the Hot Pursuit set-up (`0x474640`) when
player 1's car (`0x6fd2bc`) is a police car (`0x4396b0`); `0x428700`
then gives the car +0x200 bit 0x20.

- **State 0** (set at `0x40cbe5`): a cop goes to 3 with no target, any
  other car to 2.
- **State 2**, a human racer: when the record at +0x628 has +0x1C = 2
  (`0x401500`), the car goes to **6**, becomes computer-driven (+0x200
  | 2) for 320 steps and pulls over; then **7**, stopped; then back to
  2 and reset (`0x429100`). This is the speeder being busted.
- A **computer racer** has its own states in `0x409c00` (same field):
  1 or 2 racing; **5** when its record has +0x1C = 2 (pulling over,
  until it is under 3 m/s or 320 steps pass, 640 with +0x620 bit
  0x4000; not while tipped, +0x10C ≤ 0.9); **6** stopped (speed wanted
  0, `0x40ce70`); back to 1 when the record's +0x1C clears (the
  ticket); and **4** once out of the race (+0x2C4 3 or 4), where each
  step `0x408600` calls **`0x407380`**: no controls, brake 255, speed
  × 0.98 (× 0.95 under 22.35 m/s), so it stays where it stopped. In a
  state 5 with no cops left on the record (+0x2C 0) it goes back to 1
  (`0x409b40`). Seen in `cp-stop7b` (car 1): 2 → 5 at 64.05 s, 5 → 6
  at 64.16 s, 6 → 1 at 67.28 s, 1 → 4 at 67.38 s; `0x407380` first ran
  for it at 67.22 s (`cp-stop8`), and it was still at the roadside, at
  0 m/s, at 70 s.
- **State 3**, a cop with no target. Each step:
  - `0x4052e0`: each racer within 50 slices gets +0x6F0 = the most it
    has been over the section's speed (`0x40ed40`);
  - `0x404ff0`: the nearest racer within 50 slices (by slice, then by
    distance) into +0x730, and the rows of the cop's racer list
    (`0x485180`);
  - `0x404f80`: the **horn key** (bit 0x08, +0x50E) toggles the siren,
    +0x710 bit 0x10 (+0x734 1 or 0), once a press;
  - `0x404e40`: the spike strip (bit 0x80);
  - **`0x4053b0`: with the siren on, no target yet, and +0x730's car
    over the speed by more than 2 m/s (+0x6F0 > 2) and still racing,
    `0x4018b0(2, ...)` makes it the target (+0x624, `0x4019a1`).**
    Then state 3 → **4**, the chase (`0x405879`).
- **State 4**: the chase. Each step the steps of state 3 but
  `0x4053b0`, then `0x405630`. The cop does nothing by itself: the
  human drives. The chase record is +0x628 (static records from
  `0x56e410`, one per chased racer: +0 and +0x14 counts, +4 the most
  over the speed, +0x1C 2 when the racer is pulled over, +0x28 a step,
  +0x2C the cops on it, +0x30 their list, +0x50 the cops stopping it).
  `0x405630`: with the siren off (+0x710 bit 0x10) or the target over
  **300 m** away, or a racer that has finished, the chase is lost
  (`0x401a80`: the record's counts + 1, +0x624, +0x628, +0x734
  cleared, siren off) and the next check (`0x4058db`) goes back to 3.
  Else `0x405430`: when the cop is within **7 m** of the target, the
  target's +0x7C4 is at most `0x56ebb0` (**20.12 m/s**, 45 mph, or
  11.18, 25 mph; `0x404c50`), and
  the cop is on the road and upright (+0x10C > 0.5): the first such
  step calls `0x4127b0`, which is empty (a bare `ret`: the sound
  there was taken out), and sets +0x720 = step + 24 and +0x710 bit
  0x40; if it still holds 24 steps (0.375 s) later, the record's +0x1C
  = 2 and +0x28 = step + 640 (`0x40554a`). Failing a test clears bit
  0x40. Once the target has stopped for it (an AI racer in its state
  6, a human in 7, see state 2), `0x405430` returns 1 and the cop goes
  to **5** (`0x4058bf`) through `0x401e40`: of the record's cops the
  nearest to the target does the ticket (+0x710 | 0x80, siren off,
  record +0x50 + 1); on the first, +0x28 = step + 192 for a human cop
  (3 s), or `0x5521b8`[the target's tickets × 16] for a computer one
  (384 steps, 6 s, for 0-4 tickets; 448 from 5), and the two cars
  point at each other (+0x9A4). Also on the first: for a human target,
  `0x401580` plays police speech at the target (`0x412880` →
  `0x4c8b30`, bank entry 8, `copspch`; volume by distance) chosen by
  the tickets left, `0x6fd4bc` − its +0x2C8: 1 left slot 0x20-0x22
  (random, not the last one played), 2 slot 0x13, 3 slot 0x11-0x12,
  else 0x10-0x11. Summit_Night's two scenes (out at 3) carry slots 17
  and 19 (bnk.md): 3 left, then 2, as this gives. Then `0x401c20`
  (always) places **the officer on foot**: +0x910 of the target = its
  position 2 m along its road right, to the side picked by which way
  it faces on its slice (`0x401b50`), +0x8EC his frame from the
  heading +0x210, and +0x8E4 = +0x8E8 = 1. While both are set the car
  drawing (`0x4bbba0`) draws him (`0x4b9bc0`, within 100 m). After the
  ticket `0x401e20` clears both and he goes. The spot is lowered by
  the target's half height (+0x118) and handed to `0x4205a0` with the
  slice; on what that gives back (`0x401d78`) the other side may be
  taken instead. Run (`r1-ram4`, below): he stood 1.99 m across and
  0.94 m below the target's centre.
  - **The 7 m** and the 300 m are not true distances: `0x4965b0` takes
    the largest of |dx|, |dy|, |dz| plus a quarter of each of the
    others (`0x53bedc` 0.25), so straight ahead it is 7 m and on a
    diagonal about 6 m.
  - **Which limit.** `0x404c50` compares the human racers
    (`0x6fd518`; a human cop is counted in `0x6fd51c`, not there) with
    the computer racers (`0x6fd520`): 20.12 m/s when there are fewer
    human racers, else 11.18. From `0x474640`: one player as a cop
    (0 against 6) and two cops (0 against 6) get 20.12; one player as
    a racer (1 against 1), two players with a racer, and network
    games with a human racer (no computer racers then) get 11.18. So
    the limit is 20.12 when every human is a cop, and 11.18 when a
    human is being chased. Read from memory at run time: 20.12 in a
    cop game (`cp-stop1`: `0x6fd518` 0, `0x6fd51c` 1, `0x6fd520` 6),
    11.18 in a racer's pursuit (`cp-racer1`: 1, 0, 1, and 6 computer
    cops at `0x6fd524`).
  - **A strip's stop needs no 7 m.** A racer that hits a spike strip
    has its record's +0x1C set to 2 at once (`0x401650`), and
    `0x405430` returns 1 as soon as the target is in its state 6,
    wherever the cop is (`cp-spike1`, below).
- **State 5**: the stop. The speed wanted is 0 and `0x40ce70` halves
  the cop's velocity and spin each step with the brake at 255 (on the
  road, upright). `0x401fb0` ends it when the step passes the record's
  +0x28 (or the target is gone), with **the ticket** `0x401ff0`, and
  the cop goes back to 3 (`0x405911`):
  - the record: +0 + 1, +4 = the most of int(target +0x6F0), +0x14 0;
  - the cop: +0x2CC tickets given + 1 (reaching `0x6fd4b8`, +0x2D4 =
    step − 512, the race clock when the cop made its quota: the clock
    starts at step 512), +0x2D0 += int(the target's +0x6F0, the most it was
    over the speed);
  - the target: +0x2C8 tickets + 1, +0x6F0 0, and at `0x6fd4bc`
    tickets +0x2C4 = 4, **out of the race** (it then coasts,
    `0x407380`); +0x634 = step + `0x5521b4`[tickets × 16] (960, 896,
    768, 640, 576: 15 down to 9 s), before which no cop picks it again
    (`0x402e42`); +0x8A 0, `0x401e20` (the officer gone); record +0x2C, +0x50, +0x1C 0,
    which lets the racer's state 7 go back to 2 and onto the road.
  - then `0x4040e0`: the record's cops lose it; a human cop's strip is
    cleared (`0x4c9f50`) and the cop is put back on the road by
    `0x405220`, the same as its reset key: in a one-player game with
    the cop assist on (`0x6fd4a8`, copied from the option `0x6fd48c`
    by `0x474640`) and after step 0x480, at **one slice behind the
    leading racer still in** (lowest +0x2BC), then `0x429100`. In
    `cp-stop7b` the cop was at slice 0 beside the ticketed car and
    was at slice 121 a step later, behind the leader (slice 117 at
    the ticket).
  - `0x474640` sets the limits. One player as a cop: `0x6fd4b8` 6
    (all six racers), `0x6fd4bc` 1 (one ticket and a racer is out),
    six computer racers. One player as a racer: `0x6fd4bc` laps + 1,
    `0x6fd4b8` 100, **one** computer racer (`0x6fd520` 1). Two players:
    both cops, quota 3 and out at 1; else out at laps + 1 (quota the
    same, or 100 with two racers). So the count comes from the race
    length, not from a setting or the number of opponents. It also
    turns every assist off (`0x4746ab`).
  - `0x5521ac`, 16 bytes by the target's tickets (0-7): +0 50, 50, 70,
    70, 90, 90, 90, 90; +4 65, 65, 75, 85, 105, 105, 115, 115; +8 the
    steps before a cop may pick it again (960, 960, 896, 768, 640, 576,
    512, 512); +0xC the stop (384 × 5, then 448). +0 is a slice range:
    `0x4043a0` (from a chase) sends free computer cops on the other
    side of the target within +0 × 2 slices, and those with +0x711
    bit 1 within +0 × 8, after it (`0x404340`); +4 (+0 without bit 1)
    is how far, in slices, the computer cop's driver `0x4025c0` keeps
    up the chase. Read, not run: a racer with more tickets draws cops
    from further away.
  - The HUD (`0x482b00`): "TICKETED" (text 422) while the target has
    fewer than `0x6fd4bc` − 1 tickets, else "ARRESTED" (423), while
    +0x9A4 is set; "tickets: n/m" (text 333, `0x489a50`, "%s: %d/%d")
    in a Hot Pursuit, for a cop as well; and for a racer the radar
    detector `0x48c0e0` with level 4 − (d − 15) / 10 in 0-4, d the
    slices to the nearest cop (`0x4013b0`), and a beep (`0x418990`).

  Checked at run time only up to the chase (`gh1-cop`: the cop with H
  at 47.0 s and keypad 8 from 48 s): the siren at 47.1 s
  (`0x404fce`), car 1 targeted at 52.76 s (`0x4019a1`, record
  `0x56e410`) and state 4 (`0x405883`) to the end at 80 s. The cop,
  driving straight, never got within 7 m of a slow enough target, so
  `0x40554a`, state 5 and the ticket were not reached; the chase
  wasn't lost either (within 300 m).

  **The whole stop, run** (`cp-stop7b`, November 2026, nfs-host with
  the cop pokes). Car 1's +0x6F0 was poked to 5.0 at 41 s so that it
  could be chased on the grid, and its speed wanted (+0x7B0) was
  poked to 0 at each `0x408600` so that it stayed near the grid (it
  crept backwards at under 2 m/s). Keys: H at 41.5 s, then gas
  (keypad 8; the cop starts in neutral, A for first gear), left, brake.
  The race clock started at 48.86 s.
  - 41.57 s, step 58 (before the start): the chase (`0x405879`).
  - 63.61 s, step 1466: the first step of the stop test (`0x40559b`).
    Its inputs: the cop 6.99 m away by `0x4965b0` (6.82 m true: dx
    6.28, dz 2.66), the target's +0x7C4 1.68 m/s against `0x56ebb0`
    20.12, the cop on the ground, +0x10C 1.0. +0x720 = 1490.
  - 64.00 s (24 steps on): record +0x1C = 2, +0x28 = step + 640
    (`0x40554a`), and again each check until the target stops.
  - 64.05 s the target 2 → 5, 64.16 s 5 → 6 (it was under 3 m/s).
  - 64.17 s: state 5 (`0x4058bf`, `0x401e40`). The cop 6.48 m from the
    target, which was at 1.33 m/s. +0x710 went 0x10 (siren) → 0x50
    (stop test) → 0xC0 (ticket, siren off); +0x9A4 each the other.
  - 67.21 s (192 steps, 3.04 s): the ticket (`0x401ff0`), `0x4040e0`,
    the strip cleared (`0x4c9f50`, none was out), the cop back to 3
    (`0x405911`). The target: tickets 1, +0x2C4 4 (out), +0x6F0 0.
  - Frames (`fr-cp-stop7b`): 2600 (63.7 s, the target boxed beside
    the cop), 2608 (64.2 s, the scene: no HUD, an officer at the
    racer's door, the cop car behind), 2672 (67.1 s, the scene), 2680
    (67.4 s, the HUD back with "tickets: 1/6", the cop on an empty road
    ahead, by the leaders).

  **A natural stop, run** (`cq-ram4`, November 2026, nfs-host with
  the cop pokes and nothing else poked). The cop blocked a racer at
  full speed. Keys: R at 61.0 s (race clock 12.1 s), which put the
  cop at slice 80, one behind the leader, standing (`0x405220`); A
  (first gear) at 61.1 s; keypad 8 and 6 (gas, right) from 61.15 s;
  6 let go at 62.8 s, 8 at 63.0 s; keypad 2 (brake) 63.01-63.9 s; H at
  64.4 s. The cop drove about 5 m to its right, into the line three
  racers took (x 750 to 745), and stopped there. The last racer, car 1
  (`0x5e1acc`), at 40 m/s, ran into it at 64.35 s: 40.6 m/s, then 10.4
  m/s a tenth of a second later, and at rest 4 m from the cop by 65.1
  s. In a run with the same keys but no H (`cq-ram2`) it stopped there
  too and then backed off: the crash slowed it, not the stop.
  - 64.48 s: the chase (`0x405879`): H turned the siren on, and car 1,
    the nearest racer (+0x6F0 18.4), became the target.
  - 64.48 s, the first step of the stop test (`0x40559b`): the cop 3.99
    m away by `0x4965b0` (4.14 m true), the target's +0x7C4 9.21 m/s
    against 20.12, the cop on the ground (+0x164 0), +0x10C 1.0, at
    0.76 m/s.
  - 64.91 s: record +0x1C = 2 (`0x40554a`); the target 2 → 5, then
    5 → 6 at 65.03 s (`0x409f71`), 4.6 m away at 0.59 m/s.
  - 65.07 s: state 5 (`0x4058bf`, `0x401e40`, `0x401c20`); +0x710 0x10
    → 0x50 → 0xC0 as in `cp-stop7b`.
  - 68.11 s (3.03 s on): the ticket (`0x401ff0`, `0x4040e0`, back to 3
    at `0x405911`). Just before it the target's +0x8E4 and +0x8E8
    were 1 (the officer); after it 0. The target: tickets 1, +0x2C4 4,
    +0x6F0 0; the cop: +0x2CC 1, put back at slice 127 behind the
    leaders.
  - **The officer and the speech, rerun** (`r1-ram4`, the same keys,
    November 2026; the same steps to the millisecond). At 65.074 s
    `0x401c20` put +0x910 at (742.79, 22.14, −266.21), the target at
    (741.09, 23.08, −267.25): 1.99 m across, 0.94 m down; +0x8E4 =
    +0x8E8 = 1. The last race view is frame 2729 (65.19 s); the scene
    starts at frame 2730 (65.25 s, camera fixed by the white car), the
    officer standing at its right front, and he is in every scene
    frame through 2788 (68.19 s). `0x401e20` ran at 68.105 s; the next
    frame, 2790 (68.26 s), is the race view again with "tickets: 1/6"
    and the cop moved, no officer. `0x401580` didn't run: the target
    was a computer racer (`0x401f78` tests its +0x200 bit 4), and the
    sound log has no speech. With that test poked out (`r1-ram4s`,
    `r1-slot`, `0x401f7f` = 90 90) it ran at 65.074 s: tickets left
    1 (`0x6fd4bc` 1 − 0), `0x4c8b30` picked **slot 0x21** (0x20 +
    random of 3) at d² 100 and started it (`0x4c8c6a`); the mix was
    louder from 65.0 to 67.0 s. Which line a human target hears
    depends only on that rule, and wasn't run with one.
  Earlier tries: `cq-ram1` (steering right until 62.4 s, braking from
  63.2 s) and `cq-ram3` (no brake) missed: car 1 went by 4-9 m away at 40 m/s. Their H came
  late: the host then took keys from a queue in the order written, so an
  H written after a later key waited for it. It now queues each key at
  its time (November 2026), so the order written doesn't matter.

Checked at run time (`sw1-cop3`): the Hot Pursuit pokes put at
`0x474ca0`, before the set-up picks its function by the race type
(`0x474cd9`), not at `0x473bf0` (by then the single race's set-up has
been picked and the player is a racer, in state 2). Then `0x474732`
ran (37.4 s), the player went 0 → 3 at `0x405782` (40.8 s), and with
H pressed at 54.0 s the siren went on (`0x404fce`) and in the same
step `0x4019a1` targeted car 1 (`0x5e1acc`) and `0x405879` went to
state 4. Without H (`sw1-cop2`) the player stayed in state 3.

### The computer cops' driver (`0x4032e0`)

Read (November 2026), then run (below). A computer cop's state is
+0x70C; its orders come in an 8-byte record at +0x7AC (+0 the kind, +1
set once taken, +2 and +4 a slice range, +6 the way to face), written
by `0x403830` for computer cops only (`0x4040e0` gives each kind 2 when
a racer is ticketed). **The driver acts one call in four**: it is a
32 Hz task, but `0x40df10` → `0x401240` lets it through only when
(step / 2 + car +0) mod 4 is 0 (`0x4be1a0(8, ...)`), so each cop acts
every 8 steps, 8 times a second, each on its own phase (in `r2-cop4`
cop 2 on steps 3532, 3540, ...; cop 5 on 3942, 3950, ...). Every count
below is in steps but tested at that rate. On a new order (+1 still 0,
`0x404560`), and in state 1 at each call without a target,
`0x403030` applies it: kind 0 goes to state 3 when the car is inside
the slice range (`0x402fd0`), else turns towards it (`0x403000`); 1 is
state 2; 2 state 1; 3 state 7 and 4 state 8 (or 10 when already in
range), both with the siren on (+0x710 bit 0x10). Then by state:

- **1**, patrol: speed wanted = the section's speed × 1.19
  (`0x401430`, kept under `0x401370`), the racers' lane votes, and
  `0x402e10` looks for a target. **2**: the same, driving to the
  order's slices.
- **3**, to the roadside: votes 100 to the nearer road edge
  (`0x402d90`), speed 6.5 m/s. Off the slice's lanes (`0x402f70`) it
  goes to **4**, stopped (`0x40ce70` each step), and with +0x711 bit 8
  lays a spike strip across the middle, lane 7 (`0x403d30`; odd-numbered
  cops take another path, `0x403d7d`). A cop with a strip out radios
  once as its racer comes within 30 slices of it, and clears it when
  the racer is 50 slices past (`0x403df0`).
- **7**, **8**: the siren on, the section's speed × 2. 7 (a call for
  help, below) goes back to 1 with the siren off once in the order's
  slices. 8 goes to **9** there (6 m/s) and, at 6.5 m/s or less, **10**
  (radio `0x40bac0`): +0x621 bit 8 and `0x403180`, which waits while
  any racer is within 240 m (`0x4965b0`) and otherwise turns the car a
  quarter turn (`0x4dfde0`, 0.75 turn about y) on its slice
  (`0x4a0980`), speed wanted 0, bit 8 off: then **11**, stopped hard
  across the road, a roadblock. +0x711 bit 4 ends it when the racer is
  5 slices past (`0x403ed0`: back to patrol, an order of kind 2).
- **5**, the chase: `0x4025c0` drives at the target and gives up past
  the table's slices (above). Every 320 steps (the record's +0x24),
  with no human cop (`0x6fd51c` 0), it asks for more: the cop that
  radioed last time (+0x5C) only clears it; others try a strip
  (`0x4039c0`), then a roadblock (`0x403b20`), and failing both call
  free cops (`0x4043a0`: orders of kind 3, a run with the siren to the
  target's slices, radio `0x40b7f0`). `0x402110` is its stop test; on 1
  it goes to **6** (`0x401e40`), the stop, and from there back to 1
  (`0x401fb0`).

**Strips and roadblocks are ordered by heat**, h = the record's +0x14
(chases lost) + its +0x2C (cops on it) + the target's tickets + (step
− the record's +0x20, the first chase's step) / 2048:
- `0x4039c0`, **a strip**, when h > 6 and none is ordered yet (record
  +0x58 0). The first time it only radios (`0x40b910`; record +0x54 |=
  2, +0x5C = this cop); the next time it takes the nearest free
  computer cop (no target, no +0x711 bit 8) 51 slices to ¾ of the track
  ahead of the target, orders it kind 0 from its slice − 6, and sets
  its +0x711 bit 8 and the record's +0x58. That cop drives to the
  roadside (3), stops (4) and lays its strip (`0x403d30`,
  `0x4c9dd0(index, slice, 7, step)`).
- `0x403b20`, **a roadblock**, for a human target only (and not with
  command-line flag 0x20), when h > 3. The first time it radios
  (`0x40b860`; +0x54 |= 4, +0x5C) and in the same call takes the two
  nearest free cops 51 slices to ¾ of the track ahead, within 400
  slices of each other, and sends both (kind 4, siren, +0x710 |=
  0x410) to the slice halfway between them.

**Picking a target** (`0x402e10`, in every state but 3, 5 and 6):
racers still in, free (+0x634 passed) and within 16 slices, scored by
`0x403f80`. A racer scores only when it is already chased (record
+0x14), or 6.71 m/s over the section's speed now, or +0x6F0 over 2
(`0x4014b0` keeps +0x6F0 for each one looked at). A computer racer
scores the cops / 3 (at least 1), a human the cops − 1, less the cops
already on it; +0x730's car scores the most. A cop with a strip out
or a roadblock waits up to 960 steps for its own racer within 4
slices, 256 steps, before it takes another.

**The computer cop's stop** (`0x402110`) is wider than the human's:
within **20 m** (`0x4965b0`; 40 m once 3 cops are stopping it), the
target at most `0x56ea50`, **11.18 m/s** whatever the players (set
once at `0x40130a`), the cop on the ground and upright (+0x10C > 0.5).
The hold is 192 steps (3 s) for a human target, for a computer one
160 when it is more than 60 slices ahead of the leading human, 224
when 60 behind, else 192. While it holds, a human target off the gas
(+0x508 at most 11) takes 19 steps off it at each of the cop's calls
(every 8 steps), so 192 lasts 64 steps. Then the record
+0x1C = 2 and +0x28 = step + 320. Once the target has stopped for it
(a computer racer in its state 6, a human in 7: state 6 of a human
is not enough), the stop starts within 16 m. Failing a test clears
+0x710 bit 0x40. With cheat bit 0x10, `0x401520` adds speech
(`0x4127c0`) every 384 steps at a target over 8.94 m/s.

**Run** (November 2026, nfs-host, a racer's Hot Pursuit on Hometown:
the single race's clicks with `-poke at=0x474ca0:0x6fd3b8:3`, a normal
car, 2 laps, 6 computer cops, one computer racer; the player's car
driven by the computer through its task slots, as in
[bnk.md](bnk.md)'s object-sound runs; `-watch` at the state writes,
orders, radio calls, strip and stop addresses; the cars, the chase
records `0x56e410` and the strips `0x7dd850` dumped each second).
Race clock = run time − 48.9 s.
- **The first cop met** (`r2-cop1`): cop 3 on patrol came towards the
  two racers; car 1, 4 slices ahead of the player, came within 16
  slices first and was picked (state 1 → 5 at 71.10 s), so the player
  went by unchased (its +0x6F0 was never looked at, 0). Hold 160 at
  4.87 m (75.48 s), record +0x1C = 2 at 78.09 s (168 steps: the next
  call after 160), car 1 5 → 6, the stop (78.35 s) and the ticket 384
  steps on (84.47 s); car 1 then free for 960 steps.
- **The chase of the player**: cop 5 (89.17 s, lost at 97.79 s past the
  table's slices) and cop 2 (91.39 s), which drove at 46-59 m/s (the
  player at 50) for the next 65 s. Every 320 steps the calls for help:
  kind 3 orders to cops 6 (104.25 s), 7 (109.38 s) and 4 (119.59 s),
  each joining the chase (state 7 → 5).
- **A roadblock** (`r2-cop1`): at 124.63 s (step 5364), with three cops
  on the record and h = 0 + 3 + 0 + 1 = 4, cop 2 radioed (`0x40b860`)
  and sent cops 3 and 6, then 398 and 111 slices ahead, to slice 822
  (kind 4; states 8, 9 at 145.2 and 154.8 s, 10 and 11 at 158.9 and
  162.9 s, each turned by `0x403180`). The player had stopped short of
  it, at slice 782. In `r2-cop5` (the record's +0x14 poked to 4 at
  92 s, so h = 5 at once) cops 4 and 7 were sent to slice 539 at 104.25
  s; cop 4 turned across the road at 112.18 s (11); cop 7 came at
  122.5 s with the player within 240 m, so it waited in 10 and went
  back to patrol when the player was 5 slices past (122.86 s). The
  player hit cop 4 at 50 m/s and drove on at 17 m/s; cop 4 took it as
  its target from state 11 (120.83 s). Frames (`r2-cop6`, the same
  run with `-frames`): the police car across the road at 120.5 s, the
  player striking it at 121.3 s (111 then 38 mph).
- **A strip** (`r2-cop5`): cop 4 radioed at 124.69 s (`0x40b910`); 320
  steps later (129.71 s) cop 6, 50+ slices ahead, got the kind 0 order
  from slice 777 and bit 8; it reached the roadside (3, 134.7 s),
  stopped (4) and laid its strip at 139.51 s: `0x4c9dd0(4, 774, 7,
  6316)`, a 12 m strip from (337.7, −30.9, 518.8) to (335.8, −30.1,
  507.0) beside the cop. Nobody hit it: the player was stopped at slice
  699 at 148.4 s, and car 1 went by at 170 s 6 m beyond the strip's
  end. The strip stayed out to the end (200 s): its racer, the player,
  never got 50 slices past.
- **The player's ticket** (`r2-cop4`): as `r2-cop1` up to 93 s; then
  the player's tasks were put back to the human's (the Sc32 and Sc64
  entries, list `0x185e6d0` + 8 + 16 × n: function at +4; poking car
  +0x600 alone does nothing, the lists hold the functions), +0x6E0 =
  2, and keypad 2 (brake) held. Cop 2 began its hold at 19.68 m
  (96.01 s; the player at 9.6 m/s), the player off the gas took 19 off
  it at each call, and at 97.03 s (64 steps) record +0x1C = 2; the
  player 2 → 6 (97.04 s) → 7 (97.08 s), and cop 2 began the stop
  (5 → 6, 97.13 s, `0x401e40`). Cop 5 came at 102.42 s and joined it
  (6 at 102.53 s). The ticket (`0x401ff0`) came at 103.19 s, step 3990,
  386 steps after the stop began (+0x28 = step + 384 for 0 tickets),
  given by cop 5 (whichever cop's `0x401fb0` first finds the step
  past); the player 7 → 2 and put back on the road, tickets 1, free for
  960 steps. Frames: 96.7 s, the player braking at 6 mph, "tickets:
  0/3"; 99.6 s, the scene (no HUD, "TICKETED", the officer at the
  car); 103.8 s, the HUD back with "tickets: 1/3".
- In `r2-cop1` and `r2-cop5`, with the computer still driving the
  player's car, cop 2 also stopped it (hold 192 at 4.70 m, 155.90 s;
  record +0x1C = 2 at 156.90 s) and the car pulled over in the
  computer racer's state 6. A human target must be in state 7, so cop
  2 stayed in its stop test (+0x710 0x50) to the end, 43 s: an artefact
  of driving the player's car that way.

### Seen in footage (Summit_Night)

A YouTube recording of a one-player Hot Pursuit (`Summit_Night`, 2
laps; menu at 0:00: Aston Martin DB7, Summit (2 Laps), assists on,
player OGC). **The player is a racer, not a cop.** There are two
racers, OGC and Lead Foot (also a DB7): the one computer racer the
code gives. The HUD says "tickets: 0/3", laps + 1, as `0x474640` sets
`0x6fd4bc`. Times below are the race clock (the HUD's lap time).

- **First ticket** (lap 1, 2:37-2:50). The player hits a row of SUVs
  and a pickup across the road at 2:38 (a roadblock, probably) and
  stops. At 2:42.4 the view jumps; a cop SUV drives in from the left
  at 2:43.1. The HUD goes and a scene plays: a "TICKETED" banner, an
  officer on foot at the driver's door, the cop car behind, lights on.
  It lasts 6.13 s (clip 3:06.33-3:12.47); the race clock runs on
  (2:43.59 to 2:49.79). The HUD comes back with 1/3, speed 0.
- **Second ticket** (lap 1, 4:27-4:51). A dark cop sedan with blue
  lights sits one or two car lengths behind from 4:27.8. The player's
  speed is low (the needle near 50 on the 0-250 dial, mph on the
  results screen) from about 4:36 and 0 at 4:43.6. The same scene,
  6.13 s again (clip 5:07.10-5:13.23, clock 4:44 to 4:50.60), then
  2/3. The player drives off at 4:51.0; no cop follows at once.
- Both stops last 384 steps, as `0x5521b8` gives for 0 and 1 tickets
  (6.0 s; the 0.13 s more is the time to the first step of the
  scene). The 7 m and 20.12 m/s tests can't be measured from the
  clip, but both times the player was slow and the cop close.
- **Lead Foot arrested.** Out of sight (19 s behind): at 4:42.78 the
  racer list shows "Lead Foot arrested" (text 330) instead of a gap,
  and at 4:43.78 the place goes from "1st of 2" to "1st of 1". The
  results screen gives him "N/A", "Busted", tickets 3: out at
  `0x6fd4bc` = 3, as `0x401ff0` does.
- The player finishes with 2/3 (results: 1st, 9:35.03, tickets 2/3;
  laps 5:12.41 and 4:22.62). After the line the clock stops and the
  car slides to a stop (camera round the car).
- The "assists on" of the menu is "No" in the track records: the Hot
  Pursuit turned them off (`0x4746ab`).
- No spike strip and no "ARRESTED" banner are seen; the player is
  never the cop, so the human cop's 3 s stop and the cop's quota
  (+0x2D4) aren't seen either (they are in the next section).

### Seen in footage (the player as the cop)

Three YouTube recordings of a one-player Hot Pursuit with the player
(OGC) as the cop: `Atlantica_Night` (Pursuit Corvette, 8 laps, track
2), `Aquatica_Night` (Pursuit Diablo SV, 2 laps, track 6) and
`Empire_City_Night` (Pursuit El Niño, 2 laps, track 8). Each has six
computer racers and "tickets: 0/6", as `0x474640` sets. Times are the
HUD's race clock unless marked "clip". The game runs 64 steps a
second. The HUD there has the racer list on (other layouts hide it).

- **The cop's HUD.** A racer list: name, "n tickets" and speed in KPH
  (`0x48a660`, rows set by `0x404ff0` through `0x485180`). A row is
  dark grey past 50 slices (no speed), grey within 50, light grey for
  the nearest, **yellow for the target**, and "arrested" (text 330)
  once out. During a chase a green radar panel shows at the top left
  (`0x47d480`, only while the cop has a target) and a box marks the
  target car. A row of eight spikes at the top right is the spike
  strip (below). There is no radar detector: `0x482b00` draws it
  only for a racer. The top centre is the mirror, the top right dial
  the rev counter.
- **The chase starts** (footage and code agree). `Empire_City_Night`:
  the light bar comes on at 0:02.62 (H). The chase starts at 0:03.23:
  Ram Rod's row turns yellow and the radar panel comes up, as his
  speed goes from 77 to 81 KPH. Empire City's limit there is 20.12
  m/s (`0x40ed40`), and 20.12 + 2 m/s is 79.6 KPH. Gotcha (80 KPH) and
  Ace (82 KPH) were over it 0.3 s before and weren't taken: only the
  nearest racer (+0x730) is tested (`0x4053b0`).
- **The stop.** 18 stops, six a clip. At the step the stop scene
  starts the target shows 0-35 KPH (Atlantica 35, 3, 9, 3, 11, 13;
  Aquatica 10, 8, 9, 10, 1, 11; Empire 9, 6, 0, 0, 7, 6), so under
  both 20.12 and 11.18 m/s. A cop game has 20.12 (above; read in
  `cp-stop1`). Most
  targets were rammed or spun first (`Empire_City_Night` 1:26.95-1:28.34:
  Wild One 163 to 7 KPH after a side hit). The 7 m can't be measured.
  A chase can be short: 1.5 s from the yellow row to the scene (Empire,
  clip 99.60-101.13).
- **The scene lasts 192 steps.** The HUD is gone for 3.03-3.07 s at
  all 18 stops: state 5 with a human cop's +0x28 = step + 192.
  The race clock runs on (`Atlantica_Night` 0:48.89 to 0:52.06). The
  camera is fixed beside the racer's car, an officer on foot at its
  door and the cop car behind with its lights on. There is no
  "TICKETED" banner: `0x482b00` returns early for a cop with +0x9A4
  set.
- **After the ticket.** The ticket count goes up by one and the row
  says "arrested" after one ticket, with 8 laps as with 2:
  `0x6fd4bc` = 1. The arrested car stays at the roadside and the cop
  drives on (Empire 1:31.8). Whether the cop was moved behind the
  leader there (`0x405220`, the cop assist) can't be told from the
  clips: the view after the scene is a dark road with no car in
  sight. The siren is off (`0x401e40`); the next
  yellow row comes 2.7 s after the scene at the soonest (Empire, clip
  38.17-40.83), so H was pressed again.
- **A target not stopped.** `Atlantica_Night` 2:48-2:56: Gotcha is
  the target, crashed and at 0 KPH for 5 s, but the cop isn't within
  7 m and nothing happens. At 2:56 the yellow row moves to Ace, who is
  stopped. **The siren went off and on**: at 2:55.57 the cop's light
  bar goes dark, the radar panel goes and Gotcha's row is no longer
  yellow; at 2:55.78 (0.21 s later) the light bar is back and Ace's
  row is yellow, with the panel and box. Frames at 30 a second
  (clip 192.2-192.7). A human cop's chase ends only in `0x405630`
  (siren off, 300 m by `0x4965b0`, or a finished target) or with the
  ticket; nothing else changes +0x624 for it. With the siren on again
  `0x4053b0` takes the nearest racer (+0x730) whose +0x6F0 is over 2
  m/s, and +0x6F0 is the most it has ever been over (kept, not the
  speed now), so a stopped racer can be taken. Ace was the nearest.
- **The quota.** The sixth ticket ends the race. The results' quota
  time is the race clock at the end of the sixth scene: Atlantica
  scene from 5:01.7, quota 5:04.78; Aquatica 3:31.3, 3:34.34; Empire
  2:55.8, 2:58.87. So +0x2D4 = step − 512 at the ticket, as
  `0x401ff0` does. The HUD then comes back for 3.4-3.8 s with the
  lap counter at the last lap (the cop's counter shows the lowest lap
  of the racers still in, `0x482b00`) and the clock from 0:00, then
  the results.
- **The results.** "Pursuit Summary": Player (OGC, the car), Busts
  "6/6 (Met Quota)" (text 404), Quota Time, and "Sort By". Then
  Performance, Track Records and "Race Summary": each racer N/A,
  Busted, tickets 1. With one cop the sort by +0x2D4 isn't seen. In
  a run where the scripted cop gave no tickets (`cop-list`, nfs-host)
  the race ended when the racers finished: Busts 0/6, Quota Time N/A.
- **Speech.** No `copspch` line is heard (the racer's ticket lines
  aren't played for a cop). The officer bank is `off02a` in Atlantica
  and Aquatica and `off08a` in Empire City (lines only in those
  banks), never B or C; the dispatcher is `disp02` in Aquatica and
  `disp08` in Empire City. Atlantica's dispatcher lines are all
  shared, but one (slot 20) is only in `disp02` and `disp08`. That
  fits the track-mod-4 rule (bnk.md).
- **No computer cops** are seen on the player's side in any clip:
  the stops, scenes and racer list show only the player's car.
- **Spike strips.** The spike row at the top right dims (it is still
  drawn, greyer) once before each of the 18 stops, 1.4 to 10 s before
  the scene, and at no other time. The 14 dims not cut by a scene or
  the race's end last 2.90-2.97 s, close to the strip's 0xC0 steps
  (3 s, [net.md](net.md)). So the player probably laid a strip in
  every chase. No strip is seen on the dark roads, and no racer is
  seen hitting one. **Our exe can't draw that.** The icon is one quad
  of the HUD's quad list `0x749a38` (vertices at `0x791acc` + player
  × 4, set up at `0x47bc40`: white, the spike picture of `hud98.fsh`'s
  atlas, u 0.709-0.9375, v 0.719-0.762, at `CopSpikeBeltIndicator` of
  the `.pos` layout). Each frame `0x482b00` adds it (`0x49bd30`) while
  the cop has no strip out and takes it out (`0x49bdc0`) while one is
  out, and `0x49be40` draws the list. There is no dim picture and no
  colour change, and nothing about the arming step: the row is gone
  from the press until the strip is cleared. Runs agree (`cop-spike2`;
  `cp-spike1`: gone from the press at 43.1 s until a racer hit the
  strip at 190.1 s, then back). It isn't a renderer's alpha either:
  the quad isn't sent at all. So the recorded game is probably
  another build, whose icon dims for the arming time; this one is
  not known.
- **The racer list's "over".** `0x485180` writes "%d %s - %d %s %s"
  ("81 KPH - 8 KPH over", text 350) for a racer with +0x6F0 over 2
  m/s, at each step of a cop's states (`0x404ff0`), and
  `0x48a660` draws that text as the third column of every row in
  state 4 or more (within 50 slices). The clips never show it, not
  even for the target, which must be over that to be chased. Our exe
  shows it (`cp-stop8`, frame 2418 at 54.4 s: "Ace 0 tickets 0 MPH -
  11 MPH over" in yellow, the other rows "69 MPH - 19 MPH over";
  `cp-spike1`: "Gotcha 0 tickets 84 MPH - 45 MPH over"). In those runs
  the list itself was off in the saved options (`0x6fbc58` and
  `0x6fbdfc` 0; `0x485880` sets them to 1 unless the renderer is
  softtria) and was poked on. The clips' rows end at the speed, at
  every length ("196 KPH"), so the text isn't cut by the box: another
  build again, as for the spike icon.

## Other car fields

- **+0x124-+0x154, the road frame at the car.** `0x49ffa0` (from the
  road contact task `0x4a08c0`, and `0x4a0580`) gets nine numbers and
  a point from `0x420230` for the car's track position (+0xC, whose
  +0x10 is the slice +0x1C) and stores them × 1/65536: **+0x124 the
  road's right, +0x130 its up, +0x13C its forward, +0x148 the road
  point** (the virtual road point's position, bytes 0-11 of the
  36-byte point in [col.md](col.md)); +0x154 is the slice's heading
  (`0x49fee0`). The three vectors are the point's normal, forward and
  right bytes (16-18, 20-22, 24-26; `0x420230` reads them as ints at
  +0xD, +0x11, +0x15 and shifts down 24). On a road polygon with its
  own normal (track position +0x14 set, +0x6C 0, +0x64 2) up and forward
  come from the polygon and right is their cross product. `0x4a0aa0`,
  which puts a car on a slice (start, reset `0x429100`'s callers,
  `0x40cd80`), writes the point's vectors directly and normalises them
  (`0x4972f0`). So +0x13C is the road's own direction at the car,
  not the car's: the computer's +0x7C0 is the speed along the road,
  and the road height (`0x4a00d0`, through `0x4968a0`) is the plane
  through +0x148 with up +0x130 (−32000 when up y ≤ 0.1). It is not
  the matrix at +0x190 the human's tick uses. The step loop skips a car
  whose **+0x89** is 0 (`0x49ffab`, `0x4a284d`): +0x89 is "in play".

- **+0x8, the race distance** (int; the network status message sends
  its low 16 bits). `0x42ce10`, from the Sc32 task `0x42ce70`: lap
  (+0x204) × the track's road slices (`0x5dd940`) + the car's slice
  (+0x1C), or slices − slice when the track is run backward; 0 while
  +0x208 > 0. Seen at run time counting 1, 2, 3... as the car drove
  off the grid (`-watchmem` on car +8).
- **+0x208, the laps owed**: crossings of the start line the wrong way
  not yet made good. `0x42cc20` (each step; +0x1C the slice, +0x4 the
  slice before): going from slice 0 to one over 500 adds 1; going from
  over 500 to 0-50 takes 1 off while it is over 0, and only then adds
  a lap (+0x204). Mirrored when `0x6fd4c0` is set (the track run
  backward). The race's set-up (`0x42be2f`) starts it at 1 when
  `0x6fd4c0` is set, else 0, so that the first crossing from the grid
  doesn't count. Copied between cars at `0x4085b2`.
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
- The tyre, steering, gear and landing code by a script that folds
  the unoptimised code's register loads into names (car, wheel, P),
  then by hand.
- Runs: `nfs-host -native -deterministic 50000` with the usual keys,
  `-watchmem` on the player's car (record `0x5e1120`, records 0x9AC
  bytes apart, found with `-watch 0x4ad7a3`): +0x4EC-+0x4F4,
  +0x1A8-+0x1B0 and +0x8; `-keys` with `52000:+up,64000:-up` for the
  throttle.
- The November 2026 sweep (runs named `sw1-`): the static-analysis
  tools (`field car 0x6e0`, `decomp`, `callers`) to find the
  functions, the disassembly to confirm them; runs with the menu clicks
  `16000:click,24000:click,32000:move0/240,36000:click`, keypad 8 from
  52 s, then keypad 6, Space or H; `-watch` on single instructions,
  `-watchmem` on one car field and `-dumpmem at=ADDR#N` of the car and
  the stack; `-poke` for the Hot Pursuit (at `0x474ca0`) and for the
  landing (+0x164 and +0xA8 at 51 s).
- The `gh1-` runs (November 2026): `gh1-ai` (`-watch 0x4076b0 -watch
  0x407fa0 -watch 0x407c30 -watch 0x407380 -watchmem 0x5e227c 0x20`,
  keypad 8 from 52 s) and `gh1-cop` (the Hot Pursuit pokes, H at 47 s,
  `-watch` on the state stores and `-watchmem 0x5e1800 4`). The store
  lines carry no time: each was given the time of the `-watch` line
  before it. Constants read from the executable (doubles at
  `0x536304`-`0x536344`, `0x5361b4`-`0x5361d4`, `0x5363e4`-`0x536404`).

**+0x5B0 and +0x5A4** (traced to their writers with a cross-reference
of every access to those offsets):

- +0x5B0 is the slip ratio: the road-frame lateral speed over the
  forward speed, +0x4EC / +0x4F4, when |+0x4F4| is over 5, else 0
  (`0x4ad672`, stored at `0x4ad6cd`).
- +0x5A4 is the road speed: an approximate hypot of those two
  (`0x4967e0`, piecewise), negative when going backward (`0x4ad6eb`,
  stored at `0x4ad77a`).
- Both are worked out only in the human car's tick (`0x4ad7a3`).
  Everything else writes them 0 (`0x408950` and `0x4073c8` among the
  computer cars' code, `0x42bf8a`), so for computer cars they stay 0.

## What isn't known

- The cop's stop and ticket were run three ways: by the 7 m test with
  a computer racer held near the cop by a poke (`cp-stop7b`), by a
  strip (`cp-spike1`), and by the 7 m test after the cop blocked a
  racer at speed (`cq-ram4`, nothing poked). A stop of a racer slowed
  by bumping it while both drive on (not stopped by a crash) wasn't
  run; neither was a two-cop game. In footage,
  a computer cop's stop of a human racer lasts 6 s, as the table says,
  and a human cop's stop of a computer racer 3 s, with the quota time
  as `0x401ff0` gives (above). `0x4127b0` is empty, `0x401580` is
  the speech, `0x401c20` and `0x401e20` show and hide the officer
  (above); the officer was seen in frames of a human cop's stop and
  appears and goes with `0x401c20` and `0x401e20` (`r1-ram4`), but a
  stop of a human racer by a computer cop, where `0x401580` speaks,
  wasn't run (the slot was seen only with its test poked out). +0x2D4 is used by
  the results screen's sort (`0x455834`, key 7, `0x559618`; key 6
  sorts by tickets taken, 5 by tickets given): cops that made their
  quota first, earliest +0x2D4 first, then by tickets given. The cop
  game's results screen is in the footage, but with one cop only, so
  the sort isn't seen.
- The computer cops' driver (`0x4032e0`, above) was run in a racer's
  pursuit (`r2-cop1`, `r2-cop4`, `r2-cop5`): chases, calls for help, two
  roadblocks, a strip and the player's ticket. Not seen: a racer
  running over a computer cop's strip (the one laid lay beside the
  line both racers took), an odd-numbered cop's strip (`0x403d7d`), a
  stop with three cops (the 40 m), and the player driving through a
  whole chase by hand (the runs drove its car by the computer and gave
  it back to the human only to stop). `0x401370` and the radio calls
  (`0x40b7f0`, `0x40b860`, `0x40b910`, `0x40bac0`, `0x40bca0`,
  `0x40bd00`; their sounds) aren't followed.
- Why the recorded game's spike icon dims for 3 s and its racer list
  has no "over" text, when this exe hides the icon until the strip is
  cleared and shows the text: probably another build, not found.
- What the track position's +0x64 and +0x6C are, that pick the
  polygon's frame over the road point's in `0x420230`.
- The lane votes' own rules (`0x40cee0`, `0x40cfc0`, `0x40d1f0`,
  `0x40d2e0`, `0x40d3d0`, `0x40d720`-`0x40d9f0`): how a computer car
  weighs cars ahead and the best line.
