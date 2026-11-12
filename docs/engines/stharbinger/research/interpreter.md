# DEEP.EXE scene interpreter: run-time semantics

> **Working notes**, kept as they were written; not a reference page. See
> [README.md](README.md).

All addresses are linear addresses (code object at 0x10000, data at 0x50000).
In the disassembly, data operands show as `[0x2xxxx]`; add 0x50000 to get the real address
(for example `[0x21098]` is 0x71098).
Confidence levels: **H** (read directly from code), **M** (inferred from code with a small gap), **L** (guess).
Working files are in this directory: `hd.py` (disassembler), `handler-addresses.txt`, `full.asm` (whole code object),
`esd.asm` (scene executor), `rend.asm`/`rend2.asm` (animation renderer), `raw.py` (raw CC.ALL decoder).

---------------------------------------------------------------------------------------------------

## 0. Data structures

### Directive in memory (Load_Directive_List 0x1DD32) — H
Each directive is malloc(0x2D), and the directives form a singly linked list:

| off | size | meaning |
|---|---|---|
| +0x00 | u8 | command byte |
| +0x01..+0x08 | u8[8] | type byte of argument slot i (0 = absent) |
| +0x09+4i | u16 | "lo" word of slot i (initialised to 0x00FF) |
| +0x0B+4i | u16 | "hi" word of slot i (initialised to 0x00FF) |
| +0x29 | ptr | next directive (NULL at the end) |

How the loader stores each slot type (0x1DEBE..0x1E045):
* Types 01 and 04: the 1 byte goes into the low byte of lo.
* Types 02 and 20: a u16 v is stored as lo = v & 0xFF and hi = v >> 8. For type 20, the extra u16 is read and thrown away.
* Types 08, 10, 18 and 40: byte0 goes into the low byte of lo and byte1 into the low byte of hi.

`get_arg(d, i)` (0x167AC) returns `lo + (hi << 8)`, which is the raw u16. An absent slot reads as 0xFFFF.

`get_value(d, i)` (0x12E94): when the type is 02, it returns `G[ map[cur_scene][arg] ]`. For any other type it returns the raw arg. **H**

### Globals — H
* `G` = u16[3000] at 0x5EBC0. These are the game variables; they are 16-bit and **unsigned** in every comparison.
  They are zeroed at start-up (0x103E8). Only 963 are used (DS9.LVM).
* `map` = u16[50][300] at 0x60330 (row stride 0x258). It starts as the identity (0x10499), then is loaded from **DS9.LCV** (0x17F39).
* `cur_scene` u16 at 0x71DB2 is the scene's index in **link order (DS9.LCC)**, not the CC.ALL slot.
* `cur` (current directive pointer) is at 0x71098, and the head of the scene list is at 0x7113C.
* Label table: ptr[3500] at 0x6AA60. Hotspots: ptr[300] at 0x6F3D0. Timers: ptr[300] at 0x6F880. Animations: ptr[300] at 0x6FD30.
* GOSUB stack: ptr[24] at 0x7145C, with its depth at 0x714F4.

### Hotspot (malloc 0x0C, HOTSPOT 0x124E2) — H
+0 x, +2 y, +4 w, +6 h (u16), +8 enabled (u8), +9 label number (u16), +0xB cursor id (u8).

### Timer (malloc 8, TIMER 0x128CC) — H
+0 reload value, +2 current count, +4 label number, +6 active (u8), +7 type (u8: 0 scene, 1 global, 2 confused, 3 lost).

### Animation (malloc 0x34, Load_One_Animation 0x1C29F) — H
| off | meaning |
|---|---|
| +0,+2 | x, y (s16): position offset added to the frame-header x/y |
| +4 | rate (ticks between frame advances, see section 7) |
| +6 | rate countdown |
| +8 | flags (bit0 CYCLE, bit1 BACKWARD, bit2 STOP, bit3 TOEND, bit4 NOMOVE, bit5 OFF, bit6 ONEFRAME) |
| +9 | priority / draw layer 0..4 |
| +0xA,+0xC | velocity dx,dy (never set by any script command; always 0) |
| +0xE | ANIMSPD speed |
| +0x10,+0x12 | ANIMDEST target |
| +0x14 | frame count |
| +0x16 | first frame node |
| +0x1A | current frame node |
| +0x1E | last drawn frame node (0 = not drawn) |
| +0x22,+0x24 | last drawn position |
| +0x26 | 0xFF until ANIMDEF has run, then 0 |
| +0x27 | suspended (script waits on this animation) |
| +0x28 | ANIMEND label pointer |
| +0x2C | ANIMARRV label pointer |
| +0x30 | ANIMFRAM list |

Frame node (16 bytes) — H:
* +0 RLE data, +4 25-byte frame header, +8 next, +0xC prev.
* The frame list is circular: the last frame's next is the first.
* Frame header: +1 w, +3 h, +5 x, +7 y, +0x15 data length.

Draw rectangle (0x2388E) — H: (anim.x + hdr.x, anim.y + hdr.y, hdr.w, hdr.h).

---------------------------------------------------------------------------------------------------

## 1. Execution model

### Program / scene loop (main 0x10038, scene loop 0x10550..0x1094B) — H
1. At start-up, the linker files are read (0x17C00..0x18235). The name `ds9` comes from `deep.exe ds9`.

   | File | Contents |
   |---|---|
   | DS9.LCC | text list of the scenes in link order, one per line: opening, holosuit, habrm1 … mazeb1 |
   | DS9.LCV | for each scene in link order: u16 n, then u16[n], which is the variable map for that scene |
   | DS9.LVM | u16 n, then n NUL-terminated global variable names |
   | DS9.LCX | u16 n, then u16[n]: scene symbol number (the argument of SCENE/NEWSCENE) → link index. Stored into 0x6A998 |
   | DS9.LDM | dialogue names |

2. The first scene is link index 0, `opening`. Each loop iteration loads the scene:
   1. Its name is LCC[cur_scene], found in 0x71B2A+9*idx.
   2. It is looked up in the CC.ALL scene-name table (0x1B671, which uses 0x55294).
   3. The directive list is loaded (0x1B741).
   4. The ANIMDEF prepass runs (0x10AC4, see section 7).
   5. Animations, sounds and backgrounds are loaded (0x1BF43, 0x1D1EB, 0x1C81F).
   6. Labels are resolved (0x10A36).
   7. `run_scene(head, mode, 0)` (0x10BDD) is called.
3. `run_scene` returns one of these codes:

   | code | meaning |
   |---|---|
   | 0 | NEWSCENE |
   | 1 | game menu (Game_Setup via 0x20D54). After the menu, run_scene is called again with mode 2, which restores the saved position (0x204A9) |
   | 0xFFFF or 2 | run_scene is simply called again **from the head of the script** (this happens if the script runs off its end) |

   After that, the scene is unloaded. 0x13B06 unloads the on-the-fly anims. **0x13B78 busy-waits until every ASNEEDED one-shot sound has finished**, then frees that sound data. 0x1B915 frees all hotspots and timers and clears the label table. Then the loop loads the next scene.

**The whole scene script is reloaded from CC.ALL and run again from directive 0 on every scene entry.** Directive 0 is SCENE, which is a no-op at run time (0x1115E). Nothing is kept from the previous visit except the global variables. — H

### run_scene (0x10BDD) — H
Each loop iteration does this:
```
while cur != NULL and r == 0xFFFF:
    rand()                                   ; 0x10C96: the RNG is advanced every iteration
    r = step_frame_and_wait(&cur, 1)         ; 0x23DB1, see section 10
    return_pop(&cur)                         ; 0x10E29
    if cmd(cur) in (RETURN 0x6F, RETURNVAL 0x83):      ; idle: the script has nothing to do
        r = poll_input(&cur)                 ; 0x23FB1: mouse/keys, may set cur = hotspot label
        if r == 0xFFFF: r = tick_timers(&cur, 0)        ; 0x23E40: may set cur = timer label
        if cmd(cur)==RETURN and r==2: r = 0xFFFF
        return_pop(&cur)
    if cur==NULL or cmd(cur) is RETURN/RETURNVAL: continue          ; keep idling
    r = Execute_Scene_Directive(&cur, 0)     ; 0x10FE5, jump table 0x11000
    switch r: 0xFFFF -> cur = cur->next; return_pop(&cur)
              2      -> (handler already set cur) r = 0xFFFF
              1      -> save cur in 0x710AC; cur = NULL      (game menu)
              0      -> cur = NULL                           (NEWSCENE)
    if mode==1 and cmd(cur)==RETURNVAL: return eval_returnval()
```

Return codes from Execute_Scene_Directive:

| code | meaning |
|---|---|
| 0xFFFF | go to the next directive |
| 2 | jump (the handler has already set cur) |
| 1 | leave to the game menu (0x79, CONVERSE→0xFFFE, AMBIENT?/CD-prompt ESC) |
| 0 | NEWSCENE |

* **RETURN (0x6F)** does nothing as an instruction (0x12DE9). `return_pop` (0x10E29) handles it:
  * If it reaches a RETURN or RETURNVAL while the GOSUB stack is non-empty, it pops: cur = the saved pointer, which is the directive after the GOSUB/IF…SUBR. — H
  * With an empty stack, `cur` simply **stays on the RETURN**. This is the "idle / give control to the player" state: input and timers are polled until one of them changes `cur`. — H
* Execution therefore stops only at RETURN/RETURNVAL (idle), NEWSCENE, 0x79, or a menu request. Two commands also block in place:
  * **ANIMSUSPEND** blocks inside step_frame_and_wait. Animation keeps going and *all* timers tick while it waits.
  * **CONVERSE** runs its own modal loop. — H
* **GOSUB stack** (gosub_push 0x12E2D) — H:
  * It holds 24 entries (0x18). A push when it is full logs "Subroutine nesting too deep" and does not push, but the jump still happens.
  * It is reset only at program start (0x1016A), **not on NEWSCENE**. Stale entries can survive a scene change; resetting it per scene is recommended.
* **RETURNVAL (0x83)** — H:
  * In a normal scene run it behaves exactly like RETURN: it pops if the stack is non-empty, and otherwise idles. Its value is ignored.
  * In `run_scene(mode 1)` its argument is evaluated by eval_returnval (0x10E83) and returned. Mode 1 is used when a timer fires during a conversation (0x1739D).

  The value is computed as follows, then ORed with 0x8000:

  | arg type | value |
  |---|---|
  | 02 | the variable value |
  | 08 | (byte0<<8 \| byte1) + 0x80 |
  | 10 | (byte0<<8 \| byte1) + 0x40 |
  | 40 | the constant, which must be < 3 (otherwise "Illegal RETURN value", reset to 1) |

  In the dialogue loop, 0x8000 (RETURNVAL #0) means "continue the conversation". Any other value makes the dialogue loop act as if ESC was pressed and use the bits/choice number (0x174F7..). This dialogue-side meaning is **M**.
* Error helpers: 0x18AFF does nothing, and 0x18BBC/0x18B18 only append to `<scene>.gme`. Every run-time error is therefore non-fatal, and the command is usually skipped. — H
* Flag 0x71EC7 is a debug "trace only" mode: commands are printed instead of executed. The `dl` argument of Execute_Scene_Directive is always 0. Ignore both. — H

---------------------------------------------------------------------------------------------------

## 2. Values and variables

* **The map is built by the linker, by name, ahead of time.** DS9.LCV row s = for each entry of that scene's CV list, the index of the same name (case-insensitive) in DS9.LVM. — H
  * Verified for TURBO, link index 24: its 30 CV names equal LVM[LCV[24][i]] exactly, for example scturbex → global 611.
  * A reimplementation can rebuild it by matching CV names against a global name table, or simply read DS9.LCV/LVM.
* Variables are **global across scenes**: there is one u16 array of 3000 entries, and 963 are used. — H
* Initial value: all 0. The "new game" state is just this zero state, saved to savegame\new.can on first run (0x107E0, 0x1EABB = Save_Game). — H
* A type-02 argument means "variable" only for the slots that call get_value or index the map. In every other slot it is a symbol: a name-list index, label number or keyword. — H

### Operand order — H

| cmd | semantics | addr |
|---|---|---|
| LET a, v | G[v] = value(a) | 0x11B3B |
| ADD a, v | G[v] += value(a) (16-bit wrap, no check) | 0x11B95 |
| SUB a, v | if value(a) > G[v]: log "result of subtraction negative" and **leave G[v] unchanged**; else G[v] -= value(a) | 0x11BF8 |
| MULTIPLY v, b | **reversed**: G[v] = G[v]*value(b), arg0 is the variable. The overflow check is inverted: it multiplies only when 0xFFFF/value(b) <= G[v], otherwise logs. Unused in scenes. | 0x11CBF |
| DIVIDE v, b | **reversed**: G[v] /= value(b) (unsigned); b==0 → log, no change. Unused in scenes. | 0x11D9D |
| RANDOM v, n | n = G[n] if type 02, the constant if type 40, else 0xFFFF; G[v] = (n==0) ? 0 : rand() % n. rand = Watcom LCG s = s*1103515245+12345, (s>>16)&0x7FFF (0x33FDF) | 0x11E39, 0x1380D |
| IFE/IFNE/IFGT/IFGE/IFLT/IFLE a, b, L | if value(a) OP value(b) (u16 **unsigned**): cur = label[L], r = 2. OP is ==, !=, >, >=, <, <= | 0x11FC2.. 0x121B1 |
| IF…SUBR a, b, L (0x9D..0xA2) | the same test; if true, push(next), then jump | 0x12214.. |
| JUMP L / GOSUB L | cur = label[L] / push(next) then jump | 0x11F8C / 0x124A2 |
| SWITCH v, L0..L6 | k = G[map[arg0]] (always read through the map, whatever the type). If k < 7 **and** slot k+1 has type 02: jump label[slot k+1]. **Otherwise (out of range or missing label) fall through to the next directive** | 0x11EFC |
| TIMERGET t, v | G[v] = timer.count (+2) | 0x12AE6 |
| ACTIONSEQ id, v | G[v] = actionseq(value(id)) | 0x11ACE |

In every case the destination is `map[cur_scene][get_arg(slot)]`, whatever the slot's type.

---------------------------------------------------------------------------------------------------

## 3. Labels — H

* A label is defined by LABEL 0xC8 with arg0 = label number.
* At scene load, resolve_labels (0x10A36) sets `label[n] = LABEL_directive->next`, which is the directive *after* the LABEL. The maximum label number is 3499 (0xDAC, "Label index to large").
* At run time a LABEL directive is a no-op: cmd-0x64 > 0x46 → default. So code falls through labels.
* A label argument is the raw u16 label number, used directly as the table index. In TURBO the numbers start at 20, not 0, so they are not 0-based per scene. Implement it as a map from label number to "index of the directive after the LABEL".
* An undefined label gives a NULL pointer. Treat it as end-of-script.

---------------------------------------------------------------------------------------------------

## 4. Hotspots

* **HOTSPOT n, x, y, w, h, L, cur** (0x124E2) — H:
  * It allocates the record. The numbers are raw values, so they must be constants.
  * **enabled = 0**, so a hotspot is *disabled on definition*; HOTON enables it.
  * Redefining a hotspot logs "Duplicate hotspot definition" and is ignored.
* **HOTON / HOTOFF h1..h8** (0x126D6 / 0x12789): for each slot of type 02, set enabled to 1 or 0. An undefined hotspot is logged. — H
* **HOTTYPE h, c** (0x1283C): cursor byte = c. — H
* **SETRECT h, x, y, w, h** (0x1261B): x/y/w/h = get_value, so variables are allowed. It also stores h in 0x70FF4, which is used by some ACTIONSEQ types (0x25B22). — H
* Hit test (0x18DAB) — H:
  * The test point is the mouse position (top-left of the cursor sprite) plus half the cursor's width and height, i.e. the **cursor centre** (0x559B0 + [0x559CA]>>1, 0x559B2 + [0x559C8]>>1).
  * The **lowest-index** enabled hotspot whose rectangle contains the point wins. The rectangle is inclusive: `x <= px <= x+w`, `y <= py <= y+h`.
  * Only indexes below the hotspot count (0x71D92, set at scene load 0x1E4CC) are scanned.
  * None hit → 0xFFFE. While an ANIMSUSPEND wait is active (0x71EB9), the hit test returns 0.
  * Coordinates are 640×480 screen pixels, with the mouse clamped to 640-w-1 / 480-h-1.
* Cursor (0x18F36 → 0x1902F) — H:
  * The cursor is the hit hotspot's cursor byte, or 0 (NORMAL) when nothing is hit.
  * In a special mode (0x71EC3 set) it is instead hotspot_index+14, capped at 25.
  * The images come from **art\mouse.ani**, which must contain exactly 26 cursors (0x19335). Cursor id = image index.
* Cursor names: exe name table 0x5512E (the table at 0x5512A starts one entry earlier with AMBIENT) — H:

  | id | name | id | name | id | name |
  |---|---|---|---|---|---|
  | 0 | NORMAL | 6 | BEARLEFT | 12 | ZOOMIN |
  | 1 | FORWARD | 7 | BEARRITE | 13 | TOUCH |
  | 2 | BACK | 8 | UP | 14 | GUNSIGHT |
  | 3 | LEFT | 9 | DOWN | 15 | OFF |
  | 4 | RIGHT | 10 | TALK | 16 | LOADING |
  | 5 | TURN180 | 11 | DOOR | | |

  Your list matches for 0..15. The exe's own string for 16 is "LOADING", not "shoot", although the script symbol may differ. Checked against TURBO: door=11, turn180=5, left=3, right=4, talk=10.
* Triggering (poll_input 0x23FB1, only while the script idles at RETURN) — H:
  * Mouse buttons are word 0x559C2 (bit0 left, bit1 right). The previous state is kept in 0x71DAE, and only press *edges* count.
  * **Left press**: first reload every timer of type 2/3 (0x23F41). Then, if the point is on an enabled hotspot, `cur = label[hotspot.label]` immediately (r = 2), and the handler runs from that label until the next RETURN.
  * **Right press** while MAPON: go to the game menu (save cur, return 1).
  * The **'m'/'M' key** while MAPON opens the deck map screen (0x24299). Other keys are flushed.
  * Clicks made while the script is busy are not queued. Only the button state seen at the next idle poll counts.
* During CONVERSE: a left click on an enabled hotspot *other than the CONVERSE's hotspot argument* ends the conversation, and CONVERSE then jumps to that hotspot's label (see section 9). — H

---------------------------------------------------------------------------------------------------

## 5. Timers

* **Clock** — H:
  * An AIL timer callback (0x2B409) is registered at **250 Hz** (AIL_set_timer_frequency(h, 0xFA) at 0x2B3A7). Each call adds 4 to several ms counters.
  * Script timers use counter 0x7108C. tick_timers (0x23E40) does nothing until it reaches ≥ 100, then resets it to 0.
  * **So one timer tick = 100 ms (10 Hz)**. Missed time is not caught up, because the counter is reset rather than decremented, and ticks happen only while the timer pass runs.
* **TIMER t, ticks, L, kind** (0x128CC) — H:
  * reload = count = value(ticks), so a variable is allowed; label = raw L; **active = 1**.
  * kind = the keyword in slot 3 if its type is 02: **0 = scene, 1 = global** (exe table 0x10010: scene, global, confused, lost). If slot 3 is absent, kind = 0.
  * Data uses only 0 (40×) and 1 (13×).
* **Tick** (0x23E40, one call per 100 ms) — H:
  * For t = 0..299, until one timer fires: an existing, active timer whose type passes the filter has `if count != 0: if --count == 0: cur = label[label]; return 2`.
  * Filter: types 1 and 3 always tick. Types 0 and 2 tick only when the call is not from the conversation loop.
  * **Timers are one-shot**: after firing, count stays 0 and the timer never fires again until TIMER, TIMERSET or 0x82 rearms it. TIMEROFF in the handler is hygiene only.
  * Timers after the one that fired are not decremented in that tick.
* When timers tick — H:
  * while the scene idles at RETURN (only if no input event happened in that poll), all types;
  * during an ANIMSUSPEND wait, all types (0x23DB1→0x23E40(&cur, 0));
  * inside CONVERSE, only types 1/3, i.e. **"global" = also runs during conversations** (0x17384).

  In the conversation case the handler runs as a nested `run_scene(label, mode 1)` up to RETURNVAL, and the dialogue position is restored afterwards (0x17374..0x173A8).
* Timers do **not** survive a scene change. Unload (0x1B915) frees all hotspots and timers, so "global" does not mean persistent. — H
* Types 2 "confused" and 3 "lost" are idle timers: every left click reloads count = reload (0x23F41). They are unused in the data. — H
* Related commands — H:

  | cmd | addr | effect |
  |---|---|---|
  | TIMERON t | 0x12A15 | active = 1; count unchanged |
  | TIMEROFF t | 0x129C1 | active = 0 |
  | TIMERSET t, v | 0x12A69 | count = reload = value(v) |
  | TIMERGET t, v | 0x12AE6 | G[v] = count |
  | 0x80 "TIMERLABEL" t, L | 0x12B6D | label = L |
  | 0x81 t, L | 0x12BD2 | label = lo word of slot 1, i.e. the same as 0x80 |
  | 0x82 "TIMERRELOAD" t | 0x12C2F | count = reload (re-arm) |

  An undefined timer is logged and the command skipped. 0x80 and 0x81 are unused in scenes; 0x82 is used 3 times.

---------------------------------------------------------------------------------------------------

## 6. Sound

* **SOUND s, kw** (0x11A0B) — H:
  * If slot 1 has type 02, Start_Sound_With_Type(kw = low byte, s) (0x14424) is called:

    | kw | name | behaviour |
    |---|---|---|
    | 0 | AMBIENT | Start_Ambient_Sound (0x12FAA): looped with AIL_set_sample_loop_count(h, 0), i.e. forever. **At most 2 ambients** (0x711AC, handles 0x706B0[2], ids 0x706D8[2]). The data must already be loaded; otherwise the misleading "too many ambients" error is logged. There is no check whether the same sound is already playing |
    | 1 | ASNEEDED | if its data isn't loaded, load it now (0x1D084); play it **once** and record the handle in a list of at most 25 (0x71148, count 0x71130) |
    | 2 | | nothing |

    The names are exe strings: "Start_Ambient_Sound", "ASNEEDED".
  * With no keyword: a one-shot play of the preloaded data (Start_Sound 0x143A0 = AIL_allocate_file_sample + AIL_start_sample), not tracked.
  * **Warning: the listing tool's names are swapped in at least TURBO.** `SOUND promloop, asneeded` is raw 0 = AMBIENT (the promenade loop), and `SOUND doorclos, ambient` is raw 1 = ASNEEDED (one-shot). Decode by raw value, not by the listing's keyword.
* Ambients — H:
  * They are stopped (AIL_end_sample on every playing ambient, 0x12F4A) when the next scene loads its sounds (0x1D200/0x1D389), and the ambient list is cleared (0x1061C). So an ambient lasts until NEWSCENE, or until a new SOUND x,0 adds a third and fails.
  * Sample format: 22050 Hz raw (see [../formats/audio.md](../formats/audio.md)).
* **0x79 "SOUNDSOFF?"** (0x12DC4) is really **DEATH / GAME OVER**. — H
  1. 0x714F0 = 1 (the "dead" flag),
  2. busy-wait until all ASNEEDED one-shots have finished (0x13B78), freeing as-needed data,
  3. return 1, so run_scene ends and the game menu (Game_Setup, via 0x20D54) opens.

  It is used after `aredead:`, `blewup:` and `dead2:` labels.
* **0x8D "AMBIENT?" #n** (0x12C93 → 0x13095) is really **CHANGE CD / require disc n** (#0 or #1). — H
  * If disc n is already current (0x71DC4), nothing happens.
  * Otherwise a 400×156 box is drawn at (162,120) with "insert CD" text, and the game waits for a right click or ESC. It re-checks the CD (0x183F1) and shows "no CD" / "incorrect CD" errors with the 'brkndoor' sound.
  * ESC returns 1 (game menu). On success, current disc = n.

  A reimplementation can treat it as a no-op (or just record the disc).
* 0x94 (and 0x7C/0x92/0x93) in the **scene** table go to the default case: **no-op**. They are lip-sync commands of the dialogue interpreter. — H
* **Music (.MUS)**: the only caller of Load_Music_Pattern_File (0x1D5F9) is at 0x262F2, inside the action-sequence player. There is **no scene command for music**; it comes with ACTIONSEQ. — H
* 0x13B78 (the busy-wait for one-shots) also runs on **ANIMSOFF** and on **every scene exit**. A pending ASNEEDED sound delays the transition until it ends. — H

---------------------------------------------------------------------------------------------------

## 7. Animations

### ANIMDEF modes (prepass 0x10AC4 + Load_Animations 0x1BF43 + renderer) — H
* At scene load, the ANIMDEF prepass prefixes the animation's name with a byte p built from slot 1. The mapping comes from the jump table at 0x10B04:

  | slot 1 | p |
  |---|---|
  | 0 | 1 |
  | 1 | 3 |
  | 2 | 2 |
  | 3 | 4 |

* **bit0 of p = load on the fly.** p 1/3 is *not* preloaded. It is loaded on first use by any animation command (0x13566 → 0x1C155), and that sets **priority = 3**. ANIMSOFF unloads it again (0x13B06).
* p 2/4, or no ANIMDEF at all, is preloaded at scene entry (0x1BFCE).
* **bit1 of p = "clear"**: before each redraw, the renderer restores the background under the animation's previous rectangle (0x22E5E..0x22EB0). Non-clear animations are not erased, so their frames are expected to be opaque.

  | value | name | meaning | uses |
  |---|---|---|---|
  | 0 | onthefly | lazy, no clear | 286 |
  | 1 | flyclear | lazy + clear | 361 |
  | 2 | clear | preload + clear | 120 |

  These names fit the bits; the value→name pairing itself is **M** (from the xref, consistent with the semantics).
* What the ANIMDEF directive does at run time (0x11719):
  * If the animation is loaded: rate = 10, flags = 0x20 (OFF), prio = 0, x = y = 0, and the frame is reset. A second ANIMDEF logs "Duplicate animation definition".
  * **If the animation is on-the-fly (not loaded yet), the run-time ANIMDEF does nothing.** Lazy animations therefore keep the loader defaults: rate 0, flags 0x20, **prio 3**.

### Keyword bits (tables 0x550D6 names, 0x5510E AND-masks, 0x5511C OR-bits) — H

| value | name | effect on flags |
|---|---|---|
| 0 | LOOP | clear bit0 |
| 1 | CYCLE | set bit0 |
| 2 | FORWARD | clear bit1 |
| 3 | BACKWARD | set bit1 |
| 4 | START | clear bit2 |
| 5 | STOP | set bit2 |
| 6 | NONSTOP | clear bit3 |
| 7 | TOEND | set bit3 |
| 8 | MOVE | clear bit4 |
| 9 | NOMOVE | set bit4 |
| 10 | ANIMON | clear bit5 |
| 11 | ANIMOFF | set bit5 |
| 12 | ALLFRAME | clear bit6 |
| 13 | ONEFRAME | set bit6 |

The listing tool's ANIMSEQ keyword names are inconsistent between scenes (for example raw 10 is shown as "stop" in some scenes). **Decode by raw value with this table.** TURBO's `ANIM utle, start, #1` = raw 4, and `ANIMSEQ utle, toend, animon` = raw 7, 10.

### Commands — H

| cmd | addr | effect |
|---|---|---|
| ANIM a, kw, rate, x, y | 0x13CA5 | If kw==ANIMOFF and the anim is already OFF: nothing at all. Otherwise: if kw is present, **flags = ORbit[kw]**, which *replaces* all flags (START → 0, i.e. play/loop/forward/visible; STOP → 4; TOEND → 8; ANIMON → 0; ANIMOFF → 0x20; ONEFRAME → 0x40). If present: rate = slot 2, x = slot 3, y = slot 4 (raw constants). Then **restart**: current frame = first, last-drawn = 0, rate countdown = 0 |
| ANIMSEQ a, k1..k7 | 0x13ED6 | for each type-02 slot: flags = (flags & AND[k]) \| OR[k], so bits are changed individually. No restart |
| ANIMRATE a, r, k… | 0x13F5A | rate = r (raw), then the same keyword processing as ANIMSEQ from slot 2 |
| ANIMPOS a, x, y | 0x13DDF | x, y = const (type 40) or variable (type 02). This is the offset added to the frame-header x/y. Only the base position is updated; there is no restart |
| ANIMSETFRAME a, n | 0x13DA9 → 0x24D33 | current frame = first, advanced n times (0-based; n = value, so variables are allowed). n=0xFFFF sets NULL. Flags are not changed |
| ANIMPRIO a, p | 0x112A9 | prio (byte +9) = value(p) |
| ANIMEND a, L | 0x141A8 | end label = label[L] |
| ANIMSUSPEND a | 0x141F0 | if (ONEFRAME) or (OFF and drawn) or (!STOP and TOEND): suspended = 1 and the global wait flag 0x71EB9 = 1. **Otherwise it does nothing** (for example on a looping animation without TOEND) |
| ANIMUNSUS a | 0x1426E | suspended = 0 only; the global wait flag is not cleared. Unused |
| ANIMSOFF | 0x114D5 | unload all on-the-fly anims; busy-wait for one-shot sounds; set OFF on **every** animation; set 0x71EBB = 1, which suppresses all screen updates of the renderer until the next BACKGRND |
| ANIMCOMPOSIT a | 0x1429D | copy the anim's first-frame rectangle (at x+hdr.x, y+hdr.y) **from the work buffer 0x71028 into the clean background 0x71038**. This "bakes" what is on screen there into the background (M on the buffer roles) |
| ANIMPRISTINE a | 0x1432F | draw the current frame RLE directly into the background buffer 0x71038 (M) |
| ANIMDUMP a1..a8 | 0x11514 | free (unload) the given animations (0x1C4F5) |
| ANIMSPD / ANIMDEST / ANIMARRV / ANIMFRAM | 0x13FAA / 0x14027 / 0x1409B / 0x140E3 | only store fields (+0xE, +0x10/+0x12, +0x2C, the +0x30 list {label, frame}). **Nothing reads them**: the velocity +0xA/+0xC is never computed. All four are unused in the scene data |

### Renderer (0x22AE3, "step") — H
* It runs when ms counter 0x71094 is ≥ 17. The counter rises in steps of 4, so in practice **every 20 ms (50 Hz)** at most. The counter is then reset.
* It is called once per interpreter iteration, during waits, and in dialogue loops.

**Entry and throttle (0x22AE3..0x22BD4) — H.** The renderer first finds the lowest and highest slot of the animation table 0x6FD30 that holds an animation (empty slots, e.g. after ANIMDUMP, are skipped by both passes). With dl = 0 it only runs if the millisecond counter 0x71094 has reached 0x11 (17 ms) and then resets it; dl = 1 (BACKGRND, Restore_Backgrounds) and dl = 2 (lip-sync) always run. The beam overlay timer 0x70FA8 is counted down next, on every render that runs, whatever dl is (see special.md).

**Pass 1, erase.** Animations are visited from the highest index to the lowest. An animation is erased if it is OFF, or if it is a "clear" animation (p 2/3) that is not STOP; with dl = 2 a stopped "clear" animation is erased too (0x22E91: the STOP test is skipped), which is what lets a mouth change shape cleanly during lip-sync. A freed animation (empty slot) is not erased, so its last frame stays in the work buffer. Erasing means: if it has been drawn (+0x1E != 0), restore the background rectangle of the last drawn frame and add it to the dirty list. An OFF animation also gets last-drawn = 0, and if it was suspended the wait is released.

**Pass 2, draw.** Priority levels 0, 1, 2, 3, 4 are drawn in that order, with **4 on top**; level 4 also first draws overlay rectangles (0x71004 list). Within a level, animations go from the highest index to the lowest, so the lowest index ends up on top. For each animation that is not OFF:
1. If it is suspended, wait flag = 1.
2. Draw the current frame at (x+hdr.x, y+hdr.y) into the work buffer 0x71028 using the RLE decoder (0x29C36/0x2A98F); last pos = pos.
   * Movement code exists: if !NOMOVE and the velocity is non-zero, the old rectangle is restored, pos += v, and the animation turns OFF when off-screen. The velocity is always 0, though.
3. Frame advance (skipped when the renderer is called with dl=1, e.g. by BACKGRND):
   * If STOP or ONEFRAME: last-drawn = current; no advance.
   * Else, if countdown != 0: countdown -= speedstep. speedstep is 0x71518, normally 1; it is 3 while some speech playback runs (0x2C3FB). An underflow is clamped to 0.
   * Else: countdown = rate; last-drawn = current; then advance.

   **So a frame is shown for rate+1 ticks.** Rate 0 gives 50 fps, rate 1 gives 25 fps, and the default rate 10 gives about 4.5 fps. The very first frame after a restart is shown for only 1 tick.

   Advancing:
   * **Forward**: if current is the last frame (current.next == first), the END handling below runs. Otherwise current = next.
   * **Backward**: the mirror image, using prev and current == first.

   END handling:
   1. Release a suspend wait (suspended = 0, 0x71EB9 = 0).
   2. **If an ANIMEND label is set: cur = that label.** This hijacks the script asynchronously and does not touch the GOSUB stack.
   3. If TOEND: set STOP.
   4. If CYCLE: reverse direction (ping-pong) and step one frame back.
   5. Else, if not STOP: wrap to first (LOOP).

   So with TOEND the animation stays on its last frame.
4. If ONEFRAME is set: the animation is drawn once, then OFF is set, ONEFRAME cleared, the suspend wait released and last-drawn = 0.

**Pass 3, flush.** Unless 0x71EBB is set (after ANIMSOFF), the dirty rectangles are copied to VGA (0x23940/0x23AA3/0x23B32).

**Async vs. wait.** Everything is asynchronous *except* ANIMSUSPEND:
* The interpreter loop calls step_frame_and_wait (0x23DB1) before every directive. While the global wait flag is set and `cur` is unchanged, it keeps rendering and ticking timers (all types).
* The wait ends when the animation hits its end (any mode), when a ONEFRAME animation has been shown, or when the animation is turned OFF.
* The typical pattern is `ANIMSEQ x, toend, animon` / `ANIMSUSPEND x` (raw 7, 10). "toend" alone does not wait.

---------------------------------------------------------------------------------------------------

## 8. BACKGRND / palette / fades

**BACKGRND b [, palette]** (0x1384D) — H:
1. cur_bg (0x71DAA) = b. Load the .CMP data (0x1C5D4) and hide the mouse.
2. Fill buffer 0x71020 with 1 (L: it is probably a mask/overlay buffer).
3. Decompress the background into the work buffer 0x71028.
4. If the "tricorder icon" flag 0x714E4 is set (ACTIONSEQ 0x80), draw anims\trsi\trsicon.rle at (0,0).
5. Copy work → clean background buffer 0x71038, and free the compressed data.
6. Draw the animations once without advancing them (0x22AE3 with dl=1).
7. **Blit the whole 640×480 to the screen immediately** (0x2D61A), and clear 0x71EBB, which re-enables updates after ANIMSOFF.
8. If slot 1 has type 02 and value 0 (`palette`): set the 256-colour palette from 0x6EF20[b], which was loaded at scene load from PALETTES/<bg>.PAL, and copy palette entries 1..5 into the UI colour table 0x71EA3.
9. Show the mouse.

Without the keyword the palette is unchanged. There is no fade.

The fade commands have **swapped names** in the listings — H:

| cmd | listed as | really | addr | what it does |
|---|---|---|---|---|
| 0xA5 #n | "FADEIN?" | **FADE OUT to black** | 0x12CCF → 0x1AB8C | n steps: subtract pal/n per step, set the palette, render one animation frame, delay 50 ms (0x20BDB, a busy-wait on the ms counter); then all black |
| 0xA6 #n | "FADEOUT?" | **FADE IN from black** | 0x12D04 → 0x1ACA6 | step i sets pal*i/n, 50 ms per step; the final palette is set at the end |

* The data uses n = 4..6 (about 200–300 ms). 0xA5 is used before death (`dead: FADE #5`).
* Both use the current palette pointer 0x710A0. Both are blocking.

---------------------------------------------------------------------------------------------------

## 9. CONVERSE, ACTIONSEQ, MAPON/MAPOFF

**CONVERSE d, h** (0x1189F) — H:
* It is skipped entirely if 0x71458 != 0.
* If the previous command was not CONVERSE, 0x179DE is called first (unknown).
* d is a dialogue index (name table 0x5DDB0). The dialogue slot comes from 0x1CFBE, and the speech archive is opened with 0x2AF51; if that fails, the conversation is skipped.
* It then runs **modally**: Execute_Dialogue loop 0x14518 / 0x17123.., which renders animations and ticks global timers.

The dialogue returns one of these:

| return | effect |
|---|---|
| 0xFFFF | normal end: the script **continues with the next directive** |
| 0xFFFE | right click with MAPON: return 1, game menu |
| a hotspot index | the player left-clicked an enabled hotspot other than h (0x17459..0x174D7); CONVERSE jumps to `label[hotspot.label]`, r = 2 |

So **h is the "speaker" hotspot whose clicks do not abort the conversation.** This is M–H.

**ACTIONSEQ id, v** (0x11ACE → 0x25027) — M:
* It is a dispatcher of special functions. If id ≥ 256, type = id >> 8 and the low byte is a parameter; otherwise type = id.
* G[v] = the return value, which defaults to 999 (0x3E7). Arcade results seen in scripts are 32768 and 65534.
* Types found:

  | type | effect |
  |---|---|
  | 0..6, 0x0E..0x18, 0x1C..0x1F | action/arcade/puzzle sequences: Smacker + .MUS music, with the music player at 0x262F2 |
  | 0x80 | turn the tricorder icon overlay on (0x714E4 = 1) |
  | 0x81 | turn it off |
  | 0xA0xx | set 0x714CC = xx (TURBO's #41215 = 0xA0FF sets 0xFF; meaning unknown) |
  | 0xC0/0xC1 | mouse/hotspot-centred effect using SETRECT's hotspot (unknown) |
  | 0xC3 | no-op |
  | 0xF0 | stop the countdown timer 0x71144 |
  | 0xF5xx | start a countdown with a 'tick' sound; duration from table 0x54F13 |
  | anything else | sets 0x7142C = 0xFFFF and returns 999 |

  The details of each sequence are **unknown** (not analysed).
* It is blocking.

**MAPON / MAPOFF** (0x12D39 / 0x12D55) — H:
* They set or clear 0x714C4, which starts at 1.
* While it is set, 'M' opens the deck map (0x24299) and a right click opens the game menu, both in the scene idle loop and inside conversations. MAPOFF disables both, e.g. during cut-scenes and lifts.

---------------------------------------------------------------------------------------------------

## 10. Other commands and the frame loop

* No-ops in the scene interpreter: NOP 0x66, SCENE 0x68, 0x7C/0x92/0x93/0x94, LABEL 0xC8, and any command outside 0x64..0xAA. **H**
* **NEWSCENE n** (0x12D71) — H:
  * slot = LCX[n] (0x6A998). If it is 0xFFFF, nothing happens and execution continues.
  * Otherwise cur_scene = slot and r = 0, so run_scene ends right away. Directives after NEWSCENE never run.
  * The new scene then loads after the exit waits described in section 1.
* Order of one interpreter iteration — H:
  1. rand();
  2. render step (≤ 50 Hz). This includes waiting for ANIMSUSPEND, during which timers tick;
  3. pop GOSUB on RETURN;
  4. **if idle at RETURN**: input (keys → mouse/cursor → hotspot click), then, if no input event, timers (10 Hz);
  5. otherwise execute one directive.

  Animations and timers therefore run while the script executes, but timers are only *checked* while idle or waiting.
* ANIMEND labels can redirect `cur` in the middle of a script, from the render step that runs before every directive. Unused except 3×.
* Edge cases worth reproducing:
  * Running off the end of the script → run_scene returns 0xFFFF → main restarts the script from the head **without reloading** (0x10901). Duplicate HOTSPOT/ANIMDEF definitions are then logged and ignored.
  * The GOSUB stack is not reset per scene.
  * SUB never goes below 0.
  * SWITCH falls through when out of range.

---------------------------------------------------------------------------------------------------

## Keyword raw values (use these; the listing's learned names are unreliable)

| command, slot | values |
|---|---|
| SOUND, slot1 | 0 AMBIENT (loop) · 1 ASNEEDED (one-shot) |
| ANIMDEF, slot1 | 0 onthefly · 1 flyclear · 2 clear |
| TIMER, slot3 | 0 scene · 1 global (2 confused, 3 lost unused) |
| BACKGRND, slot1 | 0 palette |
| ANIM / ANIMSEQ / ANIMRATE | table in section 7 |
| HOTSPOT slot6 / HOTTYPE slot1 | cursor table in section 4 |
