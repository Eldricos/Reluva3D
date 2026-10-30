# Need for Speed III: threads and the timer

Which threads `nfs3.exe` makes, what its multimedia timer does, and
where the race simulation runs. Worked out in October 2026 from the
retail `nfs3.exe` (its disassembly) and a run in `nfs-host` with `-log`
into a Hometown race. See [nfs.md](nfs.md) for how Reluva schedules
them.

## In short

**The race simulation runs on the game's main thread.** The timer and
its helper threads only count ticks and wake other threads: one polls
the controls, one mixes the sound, two read files. The main thread's
race loop runs two 64 Hz steps for each control frame the controls
thread has made (32 a second, woken by the timer), then draws. When it
falls behind, at most 4 frames wait and the game slows down.

## The timer

`0x48eff0(rate)` starts it (`timeGetDevCaps`, `timeBeginPeriod(1)`). It
is called with rate 128 from start-up (`0x4b6160`). It was also called
earlier (`0x4f8a93`) with 0, which means 100 Hz.

- **A one-shot timer, re-armed each time**: `timeSetEvent(delay, 1,
  0x48ed30, 0, 0)` (0 = `TIME_ONESHOT`), at `0x48edbe`.
- The callback `0x48ed30` adds the period (`1000 << 16 / rate`, 16.16
  ms, at `0x79c170`) to its due time (`0x79c178`) and sets the next
  delay from it. At 128 Hz the delays alternate 7 and 8 ms (log: 530
  of 8, 122 of 7).
- On each tick it signals two events: the tick thread's (`0x79c17c`)
  and the sound thread's (`0x79c168`). It then calls `0x4f2a60`, which
  signals the network service thread's event if there is one.
- Start-up checks the tick thread really counts (`0x79c1a4` must move
  within 5 periods). If not, it retries once more. When it works,
  `0x56441c` is set to `0x48ed00`.
- `0x48ef50` stops it (`timeKillEvent`, `timeEndPeriod`). It is
  registered to run at exit (`0x4f2a78`).

## Threads

All of them but the C runtime's are made by `win\threads.c`
`createthread` (`0x4df7a0`). It calls `CreateThread(0, stack,
0x4df730, start, CREATE_SUSPENDED)`, then `SetThreadPriority`, then
`ResumeThread`. Its priority argument maps -3..3 to IDLE, LOWEST,
BELOW_NORMAL, NORMAL, ABOVE_NORMAL, HIGHEST and TIME_CRITICAL (table
`0x4dfa10`). Each thread waits on its own event (`0x4dfb70`, which
calls `WaitForMultipleObjectsEx` at `0x4df705`).

| Start | Made by | Priority | Woken | What it does |
|---|---|---|---|---|
| `0x4cd4e0` | start-up | above normal (1) | window messages | the window's message loop (`GetMessageA` `0x4cd59f`, `DispatchMessageA`). It also starts iSND, so it makes the sound thread |
| `0x48eb90` | `0x48eff0` (timer start) | highest (2) | each tick | the tick thread: counts `0x79c1a4` and `0x79c1a0` (128 Hz), keeps an 18.2 Hz count (`0x79c16c`, ms × 1193 / 65536), then calls up to 8 tick handlers (table `0x79c130`, added by `0x48ee30`) |
| `0x48ec40` | `0x48ec80`, from iSND start (`0x4e2331`) | time critical (3) | each tick | the sound thread: calls `iSNDdirectserve` (`GetProcAddress` result at `0x9ef634`) inside a critical section (`0x4e2234`). This is the software mixer and the DirectSound `Lock`/`Unlock` |
| `0x42dc20` | start-up (`0x4b6205`), handle `0x7a3aac` | above normal (1) | every 4th tick (32 Hz) | the controls thread: reads the devices (`0x48f550`), then puts the bits in a 32-entry ring (`0x79c2b8`) and counts `0x79c4ac` |
| `0x4e53c0` | stream open (`0x4e4578`) | above normal (1) | on demand | `cmn\cdstream.c` `streamreaderthread`: reads the music (`.mus`) from the CD (`D:\GameData\Audio\pc\show5.mus` in the menus). Stopped with `TerminateThread` and made again for the race's music |
| `0x4f1820` | `initasync` (`0x4f0e9f`) | above normal (1) | on demand | `win\async.c` `asyncreader`: reads files in the background. It waited once and did nothing in this run |
| `0x4f27a0` | `0x4f2890` | highest (2) | each tick (`0x4f2a60`) | calls handlers in a table (`0x9f0c00`) under a critical section; `cmn\connect.c` adds them. Network play only; not made in this run |
| `0x4f5b10` | `win\ipx.c` | highest (2) | - | IPX receive. Network only |
| `0x4f68c0` | `win\tcp.c` | highest (2) | - | TCP receive (`TCPrecv`). Network only |
| `0x4fa210` | `win\tapipkt.c` | above normal (1) | - | TAPI (modem). Network only |
| `0x51af10` | `win\comm.c` | highest (2) | - | serial port reads. Serial play only |
| `0x4fbda0` | `win\nfile.c` `FILE_startdevice` | above normal (1) | - | a file device thread. Not made in this run |
| `0x512ad0` | `0x512d1a`, when an IPX game is opened | above normal (1) | a packet, or its 60 s timer | the IPX game's advertiser: answers `netgathr` questions and broadcasts the game (below). Network only |

The C runtime's own `_beginthread` (`0x52ec04`, calls `CreateThread`
at `0x52ec9b`) is reached only through a pointer (`0x56b1c0`). The game
never called it.

## The tick handler and the game clock

The race's only tick handler is `0x42dc60`. It is added by `0x42dcb0`
(called at `0x4c62fa`, as a race starts) and removed by `0x42dd00`
(`0x4a3665`). On each 128 Hz tick it counts:

| Counter | Rate | Read by |
|---|---|---|
| `0x5efe00` | 128 Hz | `0x471ee2`... |
| `0x5efdf8` | 64 Hz | the race loop (`0x4c4e4b`, `0x4c4ea4`), only to add the odd step between control frames |
| `0x5efdfc` | 32 Hz; also wakes the controls thread | the race start countdown (`0x4c4be7`, `0x4c4d97`) |

The steps run 64 a second (two for each 32 Hz control frame), the game
tick that [frd.md](../formats/nfs/frd.md) gives for the keyed objects.

## The race loop, on the main thread

`0x4c4e40`, called from `0x4a37e3` in a loop, goes round until the
race ends (`0x7d369c`). Each time round it:

1. runs the due system tasks (`cmn\systask.c`, `0x4e7630`). They are
   polled here, against `0x79c1a0`, not run on the timer;
2. sets the step target `0x7d36ac` from the control frames that are
   ready (below, `0x4c4e98`-`0x4c4ebf`);
3. handles each control frame the controls thread has made: while
   `0x7d3688 <= 0x79c4ac`, it does the keys for that frame (pause,
   views, `0x48f7b0`) and adds 1 to `0x7d3688` (`0x4c5276`). If more
   than 16 frames wait, it skips to the newest (`0x4c4ee5`);
4. runs the simulation: while the steps done (`0x7d36b0`) are not past
   the target, it calls `0x4c81c0(step)` and `0x4c48d0(step)`, then
   adds 1 to `0x7d36b0` (`0x4c52c0`-`0x4c5462`; every 32nd step also
   `0x4c7bd0`). Before every even step it takes that step's control
   frame (`0x4c67a0(step / 2)`, below);
5. draws the frame (`0x4bc680`, `0x4c5489`), but only if a step was
   due: with none, `0x7d3674` is set and the drawing is skipped.

The main thread calls `0x4c81c0` and `0x4c48d0`, never a timer or
helper thread. The timer only sets how many steps are due.

In a replay (`0x7cda58` = 2) step 4 calls only `0x4c48d0`, at the
replay's speed `0x7cd94c` (`0x4c5303`-`0x4c539d`): 0 every 4th step, 1
every 2nd, 2 every step, 3 twice a step; any other value (paused) runs
only the camera (`0x426680`).

## The step target, and falling behind

The target is not the 64 Hz tick count. It comes from the **control
frames**, which the controls thread makes at 32 Hz:

- `0x560104` counts the frames ready. The controls thread adds 1 each
  time it stores one (`0x48fa10` → `0x4c6530`, from `0x48f68c`).
- Each pass sets the target to `2 × 0x560104` (`0x4c4e98`), and adds 1
  more if the 64 Hz tick count (`0x5efdf8`) has moved further since
  the last pass than the target has (`0x4c4ebb`). So each frame gives
  two steps, and the odd step runs between frames.
- Each car keeps the frames in a ring of 128 (input word at
  `0x7d3714` + car × 0x200, a "full" flag at `0x7d4714` + car ×
  0x200). `0x7d5718` + car × 4 counts the frames stored, `0x7d5714`
  the frames used. The main thread uses one on each even step
  (`0x4c67a0`): it copies each car's word to `0x7d36f0` + car × 4,
  clears the flags and adds 1 to `0x7d5714`.
- At a race's start (`0x4c63f0`) frames 0-3 are filled with 0, so 4
  are stored, 0 used, and `0x560104` is 3.

**There is no cap on steps per pass and no frame skip in the race
loop**: it runs every step up to the target, then draws once. The
limit is on the controls thread's side. Before it stores a frame,
`0x4c6ae0` checks how far ahead it is: with one player or two on one
machine (`0x6fd3b0` 0 or 1) it stores the frame only while frames
stored < frames used + 4 (`0x4c6c26`; the 4 is `0x7d573c`, set by
`0x4c6a40`). Otherwise `0x4c6530` returns 0, the frame's controls are
dropped and `0x560104` stays.

So at most 4 frames wait: a pass runs at most 8 or 9 steps (about 1/8
s). **If the main thread falls further behind, the game slows down**:
game time follows the frames it uses, not the clock. Network play
allows more lead (`0x4c6a40`, by `0x6fd3b0` and `0x7a22e4`: 4, 8, 16 or
32 frames).

In a replay (`0x6fd3a0` > 1) no controls are stored; each control frame
just adds 1 to `0x560104` (`0x48fa33`), unless paused.

Checked in a run with the same keys as the one below (`-watch
0x4c6c37 -watch 0x4c5457`, 3.5 billion instructions at
`-deterministic 50000`): the hold at `0x4c6c37` was never reached,
and at each 32nd step the target was the step or at most 3 ahead
(`-watch 0x4c5457`: steps 0x20, 0x40, 0x60... with targets 0x20,
0x41, 0x62, about 500 game ms apart).

## What a step does

`0x4c48d0(step)` is the world's step. It keeps its own count,
`0x7d3684` (64 a second, from the race's set-up), and:

1. at counts 320, 384, 448 and 512 (5, 6, 7 and 8 s) plays countdown
   sample 0-3 (`0x4182b0`) and sets a colour (`0x477170`: red, orange,
   yellow, then green). At 512 the race starts (`0x7d3680` = 1; the
   step is kept in `0x6fd3ac`);
2. clears the collision count `0x5537bc` (`0x430cd0`);
3. on even counts (32 Hz): polls the system tasks (`0x4e7630`), runs
   the 32 Hz task list "Sc32" (`0x7d3690`), and, unless paused, checks
   whether the race is over (`0x4ceae0`);
4. runs the 64 Hz task list "Sc64" (`0x7d368c`);
5. adds 1 to `0x7d3684`, works out the places (`0x4cde00`: sorts the
   racers, list `0x5efc48`, with `0x4ee4f8` and writes each one's place,
   1 for the leader, to +0x2BC);
6. on odd counts, before the add (so 32 Hz), updates the sound
   (`0x411410`: the engines `0x417390`, the music's zones and laps
   `0x4133ad`-`0x4136c6`; see [bnk.md](../formats/nfs/bnk.md) and
   [mus.md](../formats/nfs/mus.md)).

The task lists are made at the race's set-up (`0x4c45f0`, named "Sc32"
and "Sc64", `0x5401ec`, `0x5401f4`). `0x4be250(list, function, arg,
priority)` adds a task, kept in priority order; `0x4be3a0(list)` calls
them all. Each car brings its own (slots `+0x5F8`-`+0x61C`, filled by
`0x42c400`, added by `0x42c830`):

| List | Prio | Task | What it is | Evidence |
|---|---|---|---|---|
| Sc32 | 6 | car `0x4a08c0` | road contact: wet road (+0x160, weather on and not under cover, `0x420bf0`), walls (`0x4a0580`), where on the track (`0x4207e0` → +0x188) | data touched |
| Sc32 | 8 | `0x42ce70` | for all cars: lane slot (+0x638), sideways offset (+0x7B8), speed along the road (+0x7C0, signed; +0x7C4 its size), wrong way (+0x5F4) | [col.md](../formats/nfs/col.md) names +0x638; fields read by the cops' and AI code |
| Sc32 | 10 | `0x471e10` | probably force feedback: works on the input device at `0x678b50` of the device list `0x6fc970` (a name compared with "Microsoft"), effects through `0x435d20`/`0x436230`; not added in replays | likely, not checked |
| Sc32 | 21 | car +0x600 | **the driver**: a human `0x431430` (copies the control word's bytes `0x79c4a5`-`0x79c4ab` to the car: +0x504, +0x505, steering +0x518, gear +0x506, buttons); an AI racer `0x409c00`; traffic `0x40c9f0`; a cop `0x4032e0`; another network player's car `0x40bf50` | car kind flags below |
| Sc32 | 30 | car `0x4a2830` | car dynamics at 32 Hz (not split up) | constants 0.6, 0.25 |
| Sc32 | 30 | car +0x608 | a human's engine, gears and tyres `0x4ae4c1` (`0x4ad7a3`, gear +0x50B; `0x4aa8e7` when in the air or slow); the computer's cars `0x4088f0` | |
| Sc32 | 30 | car +0x614 | computer-driven cars: turns the orientation (+0xC0) by the spin (+0xE4) / 32 (`0x4a2f10` → `0x4a2e20`) | |
| Sc32 | 40 | car `0x4311d0` | collisions with walls and cars (`0x4304b0`, `0x430da0`; wall hits in [col.md](../formats/nfs/col.md)) | `0x5537bc` count |
| Sc32 | 50 | car `0x42b900` | the results of impacts: reads and clears +0x168-+0x170 (`0x42ac40` effects), resets (`0x428f90`) | |
| Sc64 | 25 | car `0x4ce280` | racers only: laps (+0x204 against `0x6fd4b4`), finish (+0x2C4 = 3, step in +0x228) | |
| Sc64 | 30 | car `0x4a2c60` | position (+0x98) += velocity (+0xA4) / 64 | constant 1/64 |
| Sc64 | 30 | car +0x614 | humans: orientation by the spin / 64 (`0x4a2f30`) | |
| Sc64 | 100 | `0x426680` | the camera ([col.md](../formats/nfs/col.md), `0x426450`) | also run when paused |
| Sc64 | 100 | `0x4cb2c0` | probably the particle effects (shapes `SP%02d`, `SL%02d`, `SN%02d`; same code as `0x4cae00`, `0x4cac20`) | likely |

The car kind flags (+0x200, set by `0x428770`-`0x428951` as cars are
added): 1 racer (list `0x5efc48`), 2 driven by the computer (`0x5efb08`),
4 human (`0x5efa48`; `0x6fd4f4` is the local player's slot), 8 an AI
racer (`0x5efb88`), 0x10 traffic (`0x5efb48`), 0x20 a cop (`0x5efa88`;
a human can be one), 0x80 out of a knockout. The race is over
(`0x4ceae0`) by the race type `0x6fd3b8`: 0 and 1 `0x4ce8e0`, 2
`0x4ce840`, 3 `0x4ce960` (it counts the cops' lists: the pursuit).

**Replay recording** is part of a human's driver task: its first call is
the car's +0x5FC, `0x4bd380`. While recording (`0x7cda58` 0 or 1) it
fetches the car's control word (`0x48f6a0` → `0x4c6840`) into
`0x79c4a8` and records it (`0x4bd000`, `0x4bda00`, buffer `0x7c3f48`);
in a replay it puts the recorded word there instead (`0x4bd090`). The
file is `replay.rp3` (`0x4bd21b`).

**`0x4c81c0(step)`** does nothing but in network play (`0x6fd3b0` 2-4,
`0x6fd50c` set) and on odd steps. It then takes, under the critical
section `0x560114`, the newest status message (type 0x0b, below) from
each other player (`0x4c8250`: 20-byte rows at `0x7dd070` + slot × 20
to 12-byte rows at `0x7dd010` + slot × 12). If that player's step is
more than 32 steps past this machine's target, it wakes the controls
thread once more (`0x42dc40`, the same event the tick handler sets), so
this machine makes an extra control frame and catches up.

**`0x4c7bd0(step)`** (every 32nd step, 2 Hz) also does nothing but in
network play. It sends message 0x0b ([net.md](../formats/nfs/net.md))
to all: +1 the sender's slot (`0x7a2278`), +4 the step, +8 the local
car's speed along the road (s16, from +0x7C0) and its u16 at +8, +0xC
byte 3 of its control word. The host adds each AI car's u16 +8 (list
`0x5efb88`, count `0x6fd520`; 28 bytes in all, 14 from a player). A
player also keeps its own copy (`0x7dd110`, 20 rows by step / 32) and
compares it with the host's (`0x4c80e1`-`0x4c819a`): a check that the
machines agree. A finished local car (+0x2C4 = 3) also sends 0x0d with
its slot (`0x4c7e50`). Message 0x0b's handler is `0x4c7d30`; the host
passes it on to the others.

## Checked at run time

`nfs-host -log 400000000 -native -deterministic 50000`, the usual keys
into a Hometown race, 2.5 billion instructions. Its output was summed
on the fly by awk (a 70 KB summary, not a full log). The logged thread
ids:

| Id | Thread |
|---|---|
| 260 | main (WinMain): every THRASH call, so all drawing |
| 264 | `0x4cd4e0` window messages |
| 268, then 276 | `0x48eb90` tick thread: made at 100 Hz, killed, made again at 128 Hz |
| 272 | Reluva's WINMM timer thread, running `0x48ed30` |
| 280 | `0x4f1820` async reader |
| 284 | `0x48ec40` sound |
| 288, then 296 | `0x4e53c0` CD stream reader (menus, then race) |
| 292 | `0x42dc20` controls |

In the race (from the `Tr00.frd` load to the end of the run, about 11.5
s of game time):

- 1,466 `timeSetEvent` calls from the callback (about 128 a second).
- The tick thread woke 1,466 times. It set the controls thread's event
  312 times, and the controls thread woke 312 times.
- The sound thread woke 1,466 times. It did 1,466 sets of
  `GetCurrentPosition`, `GetStatus`, `Lock` and `Unlock`, and all of
  the mixing (2,547 mixes to 16 bits).
- The race's stream reader opened its music file once and read it 18
  times.
- All the drawing was on thread 260: `drawtri` from `0x433fff`,
  `drawquad` from `0x4c1439` and so on.
- `nfs-host`'s per-second line: "game thread 94%, timer 0%". The rest
  of the instructions are the sound thread's mixing.

Thread creation seen in the run, in order: `0x4cd4e0`, `0x48eb90`,
`0x48eb90`, `0x4f1820`, `0x48ec40` (from thread 264), `0x4e53c0`,
`0x42dc20`; in the race, `TerminateThread` of the stream reader, then
`0x4e53c0` again. With the WINMM thread that is 8 threads in all, 7 of
them the game's own.

## The IPX game advertiser (`0x512ad0`)

`0x512c80(address, name)` starts it. Its only caller is `0x4f5f70`, an
entry (+0x2C) of the IPX service's function table at `0x566e20` (`DOS
IPX compatible`, `win\ipx.c`), so it runs on the machine that opens an
IPX game, from game set-up, not during the race. `0x512c80`:

1. opens an IPX socket (`0x5129e0`, handle `0xa0b0bc`);
2. builds the answer of [net.md](../formats/nfs/net.md)'s "finding
   games" at `0xa0b0d2`: BE 2, BE 0x08BE, the 48-character `EAGAME.`
   name (format at `0x55022c`, used at `0x512fa3`), the game's 12-byte IPX
   address (`0xa0b106`), BE u16 1, open (`0xa0b112`);
3. makes the thread and waits, yielding (`0x4df8e0`), until it has
   started (`0xa0b0b8` = 1).

The thread (`0x512ad0`) binds IPX socket 0x452, then loops while
`0xa0b0b8` is set:

- `select` (`0x533e20`) on the socket until the next broadcast is due
  (the first after 4 ms, then every 60,000 ms, `0x512c18`; times from
  `0x4f2790`, the import at `0x534568`, in ms);
- a 4-byte packet that is a question (BE 1, then BE 0x08BE or 0xFFFF,
  `0x512980`) gets the answer, 66 bytes, sent to the asker (`0x512940`,
  `sendto`);
- when the time is up, it broadcasts the answer.

`0x512d70` stops it: it clears `0xa0b0b8`, closes the socket and waits
for `0xa0b0b4`. On its way out the thread broadcasts the answer once
more with 0x10, withdrawn, in place of 1. A `select` error also ends
it. The asking side (`0x5127d0`, broadcast question) is `0x4f5fa0`, the
table's next entry.

## What isn't known

- Which `NET_` function (`cmn\connect.c`, `0x4f4c80`-`0x4f5200`)
  calls table entry +0x2C: the names its trace strings give don't line
  up with what the entries do, so when in the front end the advertiser
  starts and stops wasn't pinned down. Not checked at run time.
- What some step tasks do in detail: `0x4a2830` (32 Hz car dynamics),
  `0x4088f0`, the drivers, `0x471e10` (force feedback is a guess) and
  `0x4cb2c0` (particles is a guess); the u16 at car +8 that the
  network status message carries.
- The race types' names (`0x6fd3b8` 0-3; 3 is the pursuit) and the
  network kinds of `0x6fd3b0` 2-4.
- The slow-down when the main thread falls behind was read from the
  code; no run was slow enough to hit it (a run at `-deterministic
  10000` didn't reach the race with the usual keys).
