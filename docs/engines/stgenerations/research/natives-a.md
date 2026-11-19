# SGEN natives, batch A (most used in verid1)

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

Conventions
- "push order" lists the values in the order the script pushes them, so the first is deepest and the last is on top.
  Each case pops the top first. Where the interpreter calls `game.vt[...](a, b)`, `a` is the value that was on top.
- `id` is a script object id (16 bit). Most natives that take an `id` test `id == obj.id` (obj+4). If it matches they call
  obj.vt[x] directly. If not they call game.vt[y]\(id, ...), which looks the object up with game.vt[0x148]
  (0x437db8, a CMap lookup at game+0x9c) and calls the same obj.vt[x] on it. **The two paths do the same thing.**
  The lookup returns NULL for an unknown id and the callers do not check it, so an unknown id crashes the original.
- Script object classes, matched to verid1 object types: t1 = logic 0xE000 (vt 0x533a50), t2 = wall trigger 0x4000
  (vt 0x533c88), t4 = actor 0xA000 (vt 0x534100), t7 = floor trigger 0xC000 (vt 0x534a28, derives from t2),
  t8 = billboard 0x2000 (vt 0x5345b0, derives from t2). t3 (0x533ec8), t5 (0x534378) and t6 (0x5347f0) do not
  appear in verid1.
- SendEvent is obj.vt[0x180] = 0x44f4e1 `(event, nargs, arg)`. It returns at once when obj.vt[0x10]\() (= obj+8,
  "enabled") is 0. Otherwise it looks up the handler with key (event<<8 | state) and walks the parent chain at
  obj+0x48. It runs the interpreter synchronously, so the event nests inside the current block. If no handler
  exists it runs a default from the switch at 0x44f8eb, which maps event e to obj.vt[0x184 + 4*(e-1)] for
  e = 1, 2, 3, 5, 14..37, 43..48.
- Event numbers found here: **1 = timer tick** (default vt[0x184]), 2 = enter state, 3 = leave state,
  **4 = signal with one value** (no default), **5 = set value** (default stores obj+0x1c),
  0x26..0x2a = actor damage results (see 0x41), 0x2e = special timer expiry.

---

## 0x09 SEND_SIGNAL (named GAME_CALL_2 in optable)
- Push order: `value`, `id`. Pops 2 and pushes nothing.
- Case 0x450421 calls game.vt[0x18c] = 0x4382a2(id, value). That does lookup(id) and then
  `obj.vt[0x180](4, 1, (int)value)`, which sends event 4 with one argument (`value`) to object `id`. There is no
  self check, but sending to self works the same way. Event 4 has no default handler, so it does nothing unless the
  target's script handles event 4. The handler's return value is discarded.
- verid1: `push 0x63; push 0xe028; 09`. Logic objects (0xE0xx) are mostly used as message/state machines, and the
  value works as a message code.
- Confidence: high.

## 0x2B RANDOM
- Push order: `n`. Pops 1 and pushes 1.
- Case 0x4503ef calls 0x40e1d0(n). The seed lives at 0x546488; the main loop seeds it with timeGetTime()
  (0x48a449). Each call does `seed = seed*189 + 2749` (mod 2^32) and returns `(uint32)((uint64)seed * (uint32)n >> 32)`,
  a value in [0, n). n is sign-extended first, so a negative n gives garbage.
- Confidence: high.

## 0x2C SEND_SET_VALUE (event 5)
- Push order: `value`, `id`. Pops 2.
- Case 0x45045d calls obj.vt[0x30] = 0x44e5b2, which does SendEvent(5, 1, value). For another object it goes
  through game.vt[0x194] = 0x43832a(id, value).
- If the target has no script handler for event 5, the default obj.vt[0x194] runs: 0x45af90 stores `obj+0x1c = value`
  (a 16-bit "mode/value" word, zeroed in 0x44e202).
  - t6 overrides it (0x458c79): for modes 1/2/3 it starts a self timer with id|0x10000 that pulses brightness at
    obj+0x5a/+0x5e (light flicker/strobe, 100 ms or 125 ms). It is also read by t6 vt[0x11c] (0x45b880).
- Not used in verid1. Confidence: high for the mechanism, medium for what +0x1c means.

## 0x2D RESET (default action)
- Push order: `id`. Pops 1.
- Case 0x44fb3f calls obj.vt[0x34]. For another object it goes through game.vt[0x190] = 0x4382ef. The method
  differs by class:
  - t1/t4/t5: 0x45afc0, which does nothing.
  - t2/t7/t8: 0x45b300 calls vt[0x4c] = 0x45b2d0, which runs `vt[0x48](obj+0x5a, 1, 0)`. vt[0x48] = 0x45b260 sets
    texture/frame fields +0x5c/+0x5e/+0x60 and queues the object on the dirty list (vt[0x28] = 0x44e364, list 0x545eb4).
    That list is drained in 0x44e3a4, which calls vt[0x18]; for walls that is 0x456075 → 0x4b60b0(idx, tex, a, b).
    The net effect is that the wall, floor or billboard goes back to its initial texture/animation (+0x5a). This is
    the same as native 0x37.
  - t3: 0x45b410 calls vt[0x5c] (sets +0x5c = 4 and marks the object dirty; same as native 0x3A).
  - t6: 0x45b900 calls vt[0x120], i.e. vt[0x11c]\(+0x5c), which restores the default brightness (same as native 0x67).
- Not used in verid1. Confidence: medium.

## 0x2E DISABLE_OBJECT
- Push order: `id`. Pops 1.
- Case 0x4504d0 calls obj.vt[0x38] = 0x45afe0, which sets `obj+8 = 0`. For another object it goes through
  game.vt[0x198] = 0x43836a.
- obj+8 is the "enabled" flag that SendEvent tests through vt[0x10] = 0x45b020. A disabled object ignores every
  event: timer ticks, signals, triggers and so on.
- verid1 usually runs it on self (`self 2E`), often right before 0x46 HIDE.
- Confidence: high.

## 0x2F ENABLE_OBJECT
- Push order: `id`. Pops 1.
- Case 0x450588 calls obj.vt[0x40] = 0x45b000, which sets `obj+8 = 1`. For another object it goes through
  game.vt[0x1a0] = 0x4383e0.
- verid1 example: `push 0xc041; 2F` enables a floor trigger.
- Confidence: high.

## 0x30 UNSUBSCRIBE_BROADCAST
- Push order: `id`. Pops 1.
- Case 0x45052c calls obj.vt[0x3c] = 0x44e4c0. For another object it goes through game.vt[0x19c] = 0x4383a5.
- If obj+0xc is set, it clears it and unlinks the object from the doubly linked list headed at 0x545eb8
  (next at +0x50, prev at +0x54).
- Confidence: high.

## 0x31 SUBSCRIBE_BROADCAST
- Push order: `id`. Pops 1.
- Case 0x4505e4 calls obj.vt[0x44] = 0x44e45d. For another object it goes through game.vt[0x1a4] = 0x43841b.
- If obj+0xc is 0, it sets it to 1 and pushes the object onto the front of the list at 0x545eb8.
- Who reads the list: 0x44e405(ev, n, arg) walks it and calls SendEvent on each member. Its only caller is 0x4357b9,
  which the game calls with these global events:
  - 6 (game.vt[0xec] 0x43e742)
  - 7 (vt[0x10c])
  - 8, 9 and 0x3d with arg 0/1 (vt[0xdc] 0x43ad04)
  - 9 (vt[0x34c])
  - 0xa (vt[0x354])
  - 0x31 (vt[0x3a4])
  - 0x32 (vt[0x3a0])
  - 0x38/0x39/0x3a (vt[0xe0] 0x43d0c9)
  - 0x3b with 4 args (vt[0x16c]/vt[0x170])
- So 0x31 makes the object a listener for game-wide notifications. verid1 calls `self 31` in init blocks
  (14 times).
- Confidence: high for the mechanism. The events are not named yet.

## 0x32 SET_TIMER_MS
- Push order: `period_ms`, `id`. Pops 2.
- Case 0x450640 calls obj.vt[0x2c] = 0x44e579, which calls game.vt[0x178]\(obj.id, ms). For another object it calls
  game.vt[0x178] = 0x4380b0 directly. That leads to 0x45ff6d: `timerMgr->vt[4](id, ms, cb=0x45fffc, game)`.
- Timer manager (vt 0x536f00, 24-byte entries):
  - Set (0x48d30b) reuses an existing entry for the same id, so setting again restarts it with the new period.
    It stores period at +0xc, due = now + period at +0x10, cb at +4, ctx at +8 and id at +0x14.
  - Tick (0x48d501) gets game-loop milliseconds (0x4608d9, delta clamped by 0x546d6c/0x546d70). For each due entry
    it does `due += period` and reinserts it, so **the timer is periodic** and keeps firing until killed (0x35).
  - The callback 0x45fffc calls SendMessageA(game.hwnd, WM_TIMER, id, 0). The handler OnTimer is 0x4425fc,
    found in the message map at 0x531550.
  - For a plain id, OnTimer sends **event 1** to the object.
  - Bit 0x10000 calls obj.vt[0x188]; the engine uses it for internal timers such as the t6 flicker.
  - Bit 0x20000 kills the timer and sends event 0x2e.
- verid1: `push 0x32/0x1f4/0x2ee/0x5dc; self; 32` (50, 500, 750 or 1500 ms).
- Confidence: high.

## 0x33 SET_TIMER_SECONDS
- Push order: `seconds`, `id`. Pops 2.
- Case 0x4506b9 computes `ms = seconds*1000` (lea x5 three times, then shl 3) and does the same as 0x32.
- verid1: `push 3; self; 33`.
- Confidence: high.

## 0x34 SET_TIMER_RANDOM (tenths of a second)
- Push order: `max`, `min`, `id`. Pops 3.
- Case 0x45074c computes `ms = (min + RANDOM(max - min + 1)) * 100` and then does the same as 0x32. The random
  number is drawn once, when the timer is set; the period stays fixed after that.
- verid1: `push 0x96; push 0x32; self; 34` gives 5.0–15.0 s. `push 0x708; push 0x4b0` gives 120–180 s.
- Confidence: high.

## 0x35 KILL_TIMER
- Push order: `id`. Pops 1.
- Case 0x45080f calls game.vt[0x17c] = 0x4380da(id), then 0x45ffbb, then `timerMgr->vt[8](id)`, which removes the
  timer entry. There is no self check, and the id is zero-extended.
- verid1 almost always uses `self 35` (210 uses), typically at the top of a timer handler or before a state change.
  Since timers repeat, a one-shot is written as "set" followed by "kill in the handler".
- Confidence: high.

## 0x41 SET_ACTOR_COMBAT_STATS (self only)
- Push order: `hp`, `hp_max`, `stun`, `stun_max`, `t_notice`, `t_stun`, `t_full`. Pops 7.
  (`t_full` is on top.)
- Case 0x450c86 calls obj.vt[0x78]\(a0 = top, …, a6 = first pushed).
  - For non-actors it is 0x44e6f0, which does nothing (ret 0x1c).
  - For actors (t4) it is 0x457334. That stores each argument whose value is not 0x8000 (0x8000 = keep current)
    into a dword field, as below.
- Fields:

  | Argument | Field | Meaning |
  |---|---|---|
  | a0 = top | +0x90 | full-damage threshold |
  | a1 | +0x94 | stun-damage threshold |
  | a2 | +0x98 | notice threshold |
  | a3 | +0x9c | stun max (unread) |
  | a4 | +0xa0 | stun points (current) |
  | a5 | +0xa4 | hp max (unread) |
  | a6 = first pushed | +0xa8 | hit points (current) |
- Reader: actor vt[0x24c] = 0x45747e TakeHit(damage, power). Callers: 0x457b29 (hit with team check +0xd4) and
  0x457b6b (weapon object: damage = vt[0x250], power = vt[0x254], team = vt[0x258]).
  - If `power > +0x90`: subtract damage from both hp and stun. If hp <= 0, set both to 0 and send **event 0x2a**
    (killed). Else if stun <= 0, set stun to 0 and send **0x29** (knocked out). Otherwise send **0x28** (hurt).
  - Else if `power > +0x94`: subtract damage from stun only. Send 0x29 if it reaches 0, else **0x26** (stunned/hit).
  - Else if `power > +0x98`: send **0x27** (hit, no damage).
- Actor vt[0x7c] = 0x457448 returns (+0xa0, +0xa8); this is native 0x42.
- verid1: `push 0x25,0x25,0x25,0x25,0x28,6,0xb; 41`. Here hp = hp_max = stun = stun_max = 37, t_notice = 40 (never
  reached), t_stun = 6, t_full = 11. Another: `0x1f4,0x1f4,0x43,0x43,0,5,0x1f4` (hp 500, stun 67, full damage
  never). The pairs of equal values support the max/current reading of +0x9c and +0xa4.
- Confidence: high for the fields and threshold logic. Medium for the event names and for +0x9c/+0xa4 being maxima.

## 0x46 HIDE_OBJECT (native 0x47 is the matching SHOW)
- Push order: `id`. Pops 1.
- Case 0x4513f8 calls obj.vt[0x80]. For another object it goes through game.vt[0x1d0] = 0x4386b4.
- By class:
  - t1/t3/t5/t6: 0x44e720, which does nothing.
  - t2 wall: 0x456101 calls 0x4b5f30(id & 0x1fff, 0). That finds the wall polygons of trigger idx (table 0x57a578)
    and clears flag 0x100 (kFlagVisible, at poly+0x6c) on each one. 0x47 sets the flag again (0x45613f).
  - t7 floor: 0x458f1e calls 0x4b6070(idx, 0), which does the same on the flat table 0x57aa64.
  - t8 billboard: 0x45b790 sets +0x68 = 0 and marks the object dirty. The update 0x4587dc then calls
    0x4b5fc0(idx, 0), which clears flag 0x100 on the billboard records and sets record+0x78 = 1 (2 when shown).
  - t4 actor: 0x456b7f sets +0x64 = 8, removes the actor's attached 3D object (0x456ad9 → 0x4b7870(+0x70)), sets
    +0x68 = 1 and marks the actor dirty. The update 0x456907 copies +0x64 into the level actor record byte +0x10e.
    The renderer treats 8 specially (0x4be2fd), which here means hidden. 0x47 for actors (0x456bbc) sets +0x64 = 0.
- verid1: mostly `self 46`, often next to `self 2E`. An object hides itself when it is used up.
- Confidence: high for walls, floors and billboards. Medium for actors (the meaning of +0x10e = 8).

## 0x68 PLAY_SOUND
- Push order: `volume`, `sound`. Pops 2 and pushes 1.
- The sound manager is *0x53fe00 (vt 0x530394). Its sound definitions are 56 bytes each, at mgr+4.
- Case 0x4520ca does two calls:
  - 0x41ed07(sound, volume). For sound >= 0 it sets `def[sound]+4 = volume`. If the sign-extended high word is -1
    (negative sound) it instead changes the volume of the voices already playing that sound (0x41cda3).
  - 0x41f1a4(sound, 0, 0). This plays the sound: 0x41c619 allocates a voice that copies the 56-byte def
    (0x41d050) and starts it (0x41d25e).
- Volume runs 0..64. 0x41e90b clamps it, scales it by the master SFX volume (0x53fc88) and converts it to dB through
  the table at 0x53fca0; the DirectSound buffer's SetVolume is called through 0x4d62e6.
- Returns voice handle + 1 (voice+0x5c), or -1 if no voice was available.
- verid1 always pushes `volume = 0x40` (full) and usually drops the result with op 03.
- Related: 0x69 is the same but sets def+0x24 (start delay, see 0x8D) instead of the volume.
- Confidence: high for "play sound with volume", medium for the exact return encoding.

## 0x8D PLAY_SOUND_CUE (sound group, bound to an object)
- Push order: `cue`, `id`. Pops 2.
- Case 0x452f2b calls obj.vt[0x13c] = 0x44ee4d (same in every class), which calls `soundMgr.0x41f4e1(cue, obj.id)`.
  For another object it goes through game.vt[0x284] = 0x439257.
- 0x41f4e1 searches the cue table at mgr+0x10 for an entry with key == cue. Each entry is
  {key, flags, count, count × (member, used)}. A member >= 0 is a sound index; a member < 0 is a delay that goes
  into the next sound's def+0x24. The flags pick which members play:
  - 0x20: play all of them, as a sequence.
  - 0x10: always play the first.
  - 4: once each.
  - 1: no repeats (marks a member used).
  - 2: pick one at random with RANDOM.
  - 8: when the group is exhausted, call mgr+8 → vt[0x174]\(id), presumably a notification.
- The chosen sounds are queued on a voice chain created for the object id (0x41c705/0x41c779) and started
  (0x41d25e). It returns the voice handle or -1, but the native discards the result.
- verid1: `push 0x2f/0x70/0x63…; self; 8D`. These are mostly actor lines and sounds chosen per state (e.g. random
  barks).
- Confidence: medium-high. Where the cue table is loaded from was not traced (probably a per-mission sound list).

## 0x9E SET_PANEL_BUTTON (self only)
- Push order: `frame`, `image`, `button`. Pops 3.
- Case 0x45348f calls obj.vt[0x14c] = 0x44ef1f (all classes), which calls 0x449680(obj, button, image, frame).
- *0x545ea0 is the UI panel container (created in 0x4489d9; it holds the image-name table at +0x5c, 12-byte
  entries, and a path prefix at +0x50).
  - 0x44960c(obj) finds the child panel window whose vt[0x104]\() == obj, i.e. the panel owned by this script object.
  - 0x4488bd(button) finds the button control in that panel with (ctrlId & 0x3fff) == button.
  - 0x44959d(image, &str) builds the bitmap file name: either the literal name after a '*', or prefix + name.
- Then it calls `button.vt[0xa4](image+1, name, 3)`. That is 0x426257, which loads a DIB with 3 frames (CPanelDib).
  It then calls `button.vt[0xa8](frame)`. That is 0x4264ad → 0x42598b, which selects the displayed frame/state and
  repaints.
- So 0x9E puts bitmap `image` on button `button` of the object's control panel and shows frame `frame` (0/1/2,
  e.g. normal, lit or pressed).
- verid1: `push 0; push 0x22; push 2; 9E` and `push 1; push 0x12; push 0x6a; 9E`. These are panel/console screens
  (the button ids 0x64.. come from panel creation, 0x4487c6 ORs in 0x4000).
- Confidence: medium (UI layer only partly traced).

---

## Summary table

| op | name | push order (first → top) | returns | target | conf |
|----|------|--------------------------|---------|--------|------|
| 09 | SEND_SIGNAL | value, id | – | id (event 4, 1 arg) | high |
| 2B | RANDOM | n | [0,n) | – | high |
| 2C | SEND_SET_VALUE | value, id | – | id (event 5; default obj+0x1c=value) | high |
| 2D | RESET_DEFAULT | id | – | id (class default action: restore initial texture/frame, brightness) | med |
| 2E | DISABLE_OBJECT | id | – | id (obj+8=0: ignores all events) | high |
| 2F | ENABLE_OBJECT | id | – | id (obj+8=1) | high |
| 30 | UNSUBSCRIBE_BROADCAST | id | – | id (unlink from list 0x545eb8) | high |
| 31 | SUBSCRIBE_BROADCAST | id | – | id (receive game-wide events 6..0x3d) | high |
| 32 | SET_TIMER_MS | ms, id | – | id (periodic, event 1) | high |
| 33 | SET_TIMER_SECONDS | s, id | – | id (ms = s*1000) | high |
| 34 | SET_TIMER_RANDOM | max, min, id | – | id (ms = (min+rand(max-min+1))*100) | high |
| 35 | KILL_TIMER | id | – | id | high |
| 41 | SET_ACTOR_COMBAT_STATS | hp, hpMax, stun, stunMax, tNotice, tStun, tFull | – | self (actor only; 0x8000 = keep) | high/med |
| 46 | HIDE_OBJECT | id | – | id (clear poly/billboard flag 0x100; actor state 8) | high/med |
| 68 | PLAY_SOUND | volume(0..64), sound | handle+1 / -1 | – | high |
| 8D | PLAY_SOUND_CUE | cue, id | – | id (sound group bound to object) | med-high |
| 9E | SET_PANEL_BUTTON | frame, image, button | – | self's UI panel | med |
