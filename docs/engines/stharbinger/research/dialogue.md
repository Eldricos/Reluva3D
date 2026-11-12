# DEEP.EXE conversation (dialogue) system: run-time semantics

Addresses are linear (code 0x10000.., data 0x50000..). In the disassembly, `[0x2xxxx]` = 0x7xxxx.
Confidence: **H** read directly from code, **M** small inference gap, **L** guess.
Working files (this folder): `dlg1.asm` (0x14424..0x14A8F), `dlg2.asm` (all dialogue command handlers 0x14A8F..0x16666),
`dlg3.asm`/`turn.asm` (helpers + dlg_turn 0x169DD..0x178F1), `ldlg.asm` (Load_Dialogues), `dl_load2.asm` (record loader),
`audio.asm` (0x2AE6D..0x2C600: speech DB + speak), `lip.asm` (talker registry + lip-sync 0x246D7..0x24D33), `save.asm`.
`handler-addresses.txt` was corrected: the previous names for 0x157C2.. (d_GOSUB_9B, d_IFGT_96, ...) were wrong; see section 3.

Terminology used below: **NPC tree** = "tree of NPC lines" (record +0x4BF), **choice tree** = player choices (+0x587).
"group g" = slot g (0..49) of a tree. **mode** = record +0xA: 0 = "GOTO" (next turn: an NPC line of group cur),
1 = "REPLY" (next turn: the player's menu of choice group cur).

---------------------------------------------------------------------------------------------------

## 1. Data in memory

### Dialogue record (malloc 0x2A52, Load_Dialogues 0x1C873; table of record pointers `0x6E5C0[slot]`) — H

| off | type | meaning |
|---|---|---|
| +0x000 | ptr | directive list A |
| +0x004 | ptr | directive list B |
| +0x008 | u16 | **cur** = current group number |
| +0x00A | u8 | **mode**: 0 GOTO (NPC turn next), 1 REPLY (menu next) |
| +0x00B | u8 | RUDE group (0xFF = unset) |
| +0x00C | u8 | "was rude" flag (conversation was aborted) |
| +0x00D | u8 | BACK group (0xFF = unset) |
| +0x00E | u8 | EXIT flag (ends the conversation loop; stays 1 afterwards = "have talked before") |
| +0x00F | u8[50] | DONE table (cmd 0x3C) |
| +0x041 | u8[50] | EMPTY table, "goto" variant (cmd 0x41 with keyword 2) |
| +0x073 | u8[50] | EMPTY table, "reply" variant (cmd 0x41, other keyword) |
| +0x0A5 | ptr[50] | DONEX nodes (cmd 0x45): malloc 5 {u8 target group, ptr excluded choice item} |
| +0x16D | ptr[50] | EMPTYX nodes "goto" variant (cmd 0x46 keyword 2) |
| +0x235 | ptr[50] | EMPTYX nodes "reply" variant (cmd 0x46 other keyword) |
| +0x2FD | ptr[50] | per conversation: last choice item picked in choice group g (cleared at conversation start) |
| +0x3C5 | ptr[50] | **current NPC item** of NPC group g (SET/IFSET); initialised to the group's first item |
| +0x48D | u8[50] | index of that item (only for save/trace); init 0 |
| +0x4BF | ptr[50] | NPC tree group heads (NULL = empty group) |
| +0x587 | ptr[50] | choice tree group heads |
| +0x64F | ptr[50] | HOOK chains per choice group (see HOOK) |
| +0x717/+0x719/+0x71B | u16 | counts of CCQ / CCR / CCF strings |
| +0x71D | ptr[750] | CCQ line texts (only loaded in the debug text mode, 0x71EC9; normally NULL) |
| +0x12D5 | ptr[750] | CCR menu labels, indexed by the item's **choice** number |
| +0x1E8D | ptr[750] | CCF clip names, indexed by **line** number |
| +0x2A45 | char[13] | dialogue name |

Loader init (0x1CA15..0x1CF12, 0x1DC25): +8=0, +A=0, +C=0, +E=0, DONE/EMPTY tables = 0xFF, node tables NULL,
+0xB = +0xD = 0xFF, current[g] = group head, +0x48D[g] = 0.

### Tree item (malloc 0x12, 0x1E218; loader 0x1E06C) — H
| off | meaning |
|---|---|
| +0 u16 | **line**: index into this dialogue's CCQ/CCF lines and into the speech archives (DIALOGUE/PHONEMES entry number) |
| +2 u16 | **choice**: 0xFFFF for NPC items; for player choices the index into CCR (= menu label) |
| +4 ptr | directive list |
| +8 ptr | next item in group |
| +0xC ptr | first sub-item |
| +0x10 u8 | **on** (init 1) |
| +0x11 u8 | **said** (init 0) |

Sub-items use the same node: +0 line, +2 choice (always 0xFFFF in data), +4 list, +0xC = next sub-item.
Sub-items are **additional lines spoken right after the item's own line** (see section 2). — H

Groups: file `u8 n` = number of NON-empty groups; the loader walks slots 0..: a slot with `k=0` stays NULL and does not
count, so slot numbers = group numbers as printed in the listings ("group 10 (empty)"). — H (0x1E06C)

### Hook node (malloc 10, 0x168EA) — H
+0 u8 state (0 = HOOK permanent, 1 = THOOK one-shot, 0xFF = THOOK consumed), +1 next, +5 ptr to the hooked
choice group's first item, +9 u8 hooked group number.

### Argument conventions in dialogue directives — H
* `x g.i` (type 08) = choice item i (0-based position inside group g) of the **choice tree**;
  `y g.i` (type 10) / `z` (type 18) = item i of NPC group g (find_item 0x1693C: type & 0x10 → NPC tree, else choice tree).
  The index is the position in the group's linked list, not the global choice number.
* `b` (type 01) = byte (group number), read as the slot's lo word. `c` (type 04) = byte keyword.
* `d` (type 20) = u16 (line number) via get_arg. Note: type 01/04 via get_arg would give 0xFF00+byte (hi word is
  initialised 0x00FF), so handlers read lo directly.
* Type 02 symbols index the **current scene's** CCS list (0x58600, loaded from CCS.ALL by 0x1AF60): speaker codes
  ("odo", "kir", "qrk"...), or the scene's anim list (0x6FD30) for anim slots, or variables via the map.

### Where records live and persistence — H
* Record slot = index of the CD-list name in **DS9.LDM** (Get_Dialogue_Index 0x1CFBE, "dialogcrossref" table 0x6A740,
  case-insensitive); CCB/CCQ/CCR/CCF entry = index of `<source>.ccb` in the CCB name table (0x1B671).
* Every scene load calls 0x1C81F, which loads every dialogue named in the scene's CD list **that is not loaded yet**.
  Records are never reset during a game. At every scene exit they are written to `ds9.swp` and reloaded
  (0x20C0D/0x20CB3, memory compaction) — state survives.
* All mutable state is saved in savegames (Save_Game → 0x1F0D0 per slot): name, +8, +A, +B, +C, +D, +E, DONE[50],
  EMPTY[50]×2, on/said of every item of both trees, DONEX/EMPTYX nodes (as group,index), current[g] (as index),
  +0x48D, hook chains. Load_Saved_Game frees all records (0x1BAF1) and reloads them (0x1FEAB).
* So **on/said flags, current NPC items, BACK/RUDE, DONE/EMPTY tables, hooks and the EXIT flag persist across
  conversations, scenes and saves.** Per-conversation only: +0x2FD, the PROTAGONIST list, TON/TOFF, talker registry.

---------------------------------------------------------------------------------------------------

## 2. Conversation flow

### CONVERSE d, h (0x1189F) — H
```
slot = Get_Dialogue_Index(d, CDlist[d].name)            ; 0x1CFBE
if Open_Dialogue_SoundDB(CDlist[d].name) fails: skip    ; 0x2AF51 (needs DIALOGUE/<n>/<n>.LST+.ALL and PHONEMES/<n>/<n>.LST+.ALL)
0x71DA6 = max pixel width over ALL CCR labels of the record (0x178F1, font 0)
r = dlg_run(slot, rec, h)                               ; 0x14518
0x18474() (forces cursor refresh); restore TON/TOFF (0x16758); close speech DB (0x2B175)
r == 0xFFFE -> return 1 (game menu); r == 0xFFFF -> continue with next directive;
else r = hotspot index -> cur = label[hotspot[r].label]
```
0x71458 (skip flag) is never set; 0x179DE/0x17A84 are empty stubs.

### dlg_run (0x14518) — H
```
rec.lastPick[0..49] = NULL (+0x2FD); protagList.count = 0 (0x71DA4), protagList[0..24] = 0 (0x71AF8)
rec.cur = 0; rec.mode = GOTO
if rec.exit:   rec.cur = pick(rec.back)         ; talked before (previous conversation ended)
if rec.rude:   rec.cur = pick(rec.rudeGroup); rec.rude = 0
               ; pick(g) (0x146AB): g != 0xFF ? g : first non-empty NPC group (or cur if none)
exec_dlist(listA); exec_dlist(listB)            ; EXIT inside A/B has no effect:
rec.exit = 0
do {
    rec.cur = DONE_check(rec.DONE table)        ; 0x1473B kind 0
    rec.cur = DONEX_check(rec.DONEX table)      ; 0x1483E kind 0
    r = dlg_turn(rec, h, slot)                  ; 0x169DD
} while (!rec.exit)
return r
```
`exhausted(g[, excl])` = no item of choice group g, nor of any group hooked to g (hook chain +0x64F[g], all states),
has `on==1 && said==0` (excluding item `excl` for the X variants).
* DONE_check: `if DONE[cur] != 0xFF && exhausted(cur): cur = DONE[cur]` (mode unchanged).
* DONEX_check: `if DONEX[cur] && exhausted(cur, DONEX[cur].item): cur = DONEX[cur].group`.

### dlg_turn (0x169DD) — H
```
if 0x71ECA == 2: render(dl=1)            ; erase the previous menu
if mode == REPLY:  ---------------------------------------------- menu turn
    g0 = cur
    cur = EMPTY_check(EMPTYgoto[+0x41]); cur = EMPTYX_check(EMPTYXgoto[+0x16D])
    if cur != g0: mode = GOTO; return                       ; "menu empty -> NPC group t speaks"
    repeat (max 50): cur = EMPTY_check(EMPTYreply[+0x73]); cur = EMPTYX_check(EMPTYXreply[+0x235])
        until unchanged      ; 50 iterations -> log "EMPTY directives form an infinite loop"
    if no choice group >= cur exists and hooks[cur] == NULL: exit = 1; return
    mode = GOTO                                             ; default after the player's choice
    avail = [items of choicegroup[cur] with on==1 && said==0] ++
            for each hook h of cur with state != 0xFF: (if state==1: state = 0xFF  ; THOOK shown once)
                  [items of h's group with on==1 && said==0]
    if avail empty: log "Weirdness: no replies are active or unspoken here"; exit = 1; return
    if len(avail) == 1: item = avail[0]                     ; NO menu, auto-picked
    else: item = show_menu(avail) (below); may instead return ESC (abort/timer)
    on pick:
        speak(item.line)                                    ; player's (protagonist's) line
        item.said = 1; lastPick[cur] = item
        exec_dlist(item.list)
        if no GOTO/REPLY/IFGOTO/IFREPLY fired: cur += 1     ; -> NPC group cur+1 (mode GOTO)
    on ESC (see section 7): ...
    clear the menu overlay (memset 0x71020, 1, 640*480), request erase (0x71ECA = 2)
else: ---------------------------------------------------------- NPC turn (mode GOTO)
    restore all TON/TOFF changes (0x16758)
    g = first NPC group >= cur that is non-empty; none -> exit = 1; return
    cur = g; mode = REPLY                                   ; default: then the menu of choice group g
    item = current[g] (+0x3C5; set by SET/IFSET, initially the first item; NOT advanced automatically,
                       on/said of NPC items are ignored)
    speak(item.line)
    for each sub-item s of item: exec_dlist(s.list); speak(s.line)   ; sub list runs BEFORE its line
    exec_dlist(item.list)                                   ; the item's own list runs LAST
    (a GOTO/REPLY in any of these lists sets cur/mode; it only cuts the rest of THAT list)
```
Notes — H:
* A GOTO/REPLY just records the target; the loop then (DONE/DONEX checks) and the next dlg_turn act on it.
* The conversation ends when: EXIT ran (after the current turn finishes), no NPC group >= cur exists, the menu has
  no available choice, the player clicks another enabled hotspot or right-clicks (MAPON), or a global timer returns a
  non-zero RETURNVAL constant.
* The 0x71EB7 "jump taken" flag is cleared only inside dlg_turn; a GOTO in list A or B would make the next list
  executed be skipped (no data does this).
* An NPC group search falls through empty groups upward; the menu turn does not (it uses group cur exactly; it only
  checks that some group >= cur or a hook exists).
* Off-by-one quirks: a group found at index 49 is treated as "none" in both searches.

### Typical data pattern
List B: `BACK bN; RUDE bN; MOOD d0; ON/OFF/IFON/IFSET...`. NPC item lists end with nothing (→ menu of the same
group) or `ON/TOFF...`; choice item lists end with `SET yG.I; GOTO bG` (NPC group G says item I next) or `REPLY bG`
(another menu without NPC speech) or `EXIT`.

### Pseudocode of the menu loop (0x17112..0x174E5) — H
```
build entries; position (section 5); draw all labels (normal colour) into overlay 0x71020; 0x71ECA = 1
ret = 0x8000; hover = 0
loop:
  if buttons == 0 and ret == 0x8000:
      p = (mouse.x + 0x71D8E, mouse.y + 0x71D8C)
      find i with x0 <= p.x <= x0+width[i] and y0+i*lh <= p.y < y0+(i+1)*lh
      if found: cursor 15; if i != hover: redraw old hover normal, draw i highlighted, render(dl=1); hover = i
      else if hover: redraw all normal; hover = 0
      if !hover: hs = hit_test_and_cursor() (0x18F36)
      render(dl=0)
      if tick_timers(&tmp, conversation=1) == 2: ret = run_scene(tmp, mode 1)   ; global timers only
      prevButtons = 0; continue
  if ret != 0x8000: -> ESC                                   ; timer RETURNVAL
  if right edge and MAPON: result = 0xFFFE; -> ESC
  if left edge: reload idle timers (0x23F41)
      if hover: -> PICK entry hover
      if hs not in (h, 0xFFFF, 0xFFFE) and hotspot[hs].enabled: result = hs; -> ESC (menu erase requested)
      (click on h, on nothing or on a disabled hotspot: ignored)
  prevButtons = buttons
```
No keyboard handling at all in the menu (0x1B "ESC" is only an internal sentinel). — H

---------------------------------------------------------------------------------------------------

## 3. Dialogue commands (exec_dlist 0x1496E, jump table 0x149B0, command-0x32, 0x79 entries) — H unless noted

exec_dlist(rec, list): for each directive while the jump flag 0x71EB7 is clear: execute, then
step_frame_and_wait(.., dl=0) (0x23DB1): renders and **blocks while an ANIMSUSPEND wait is active**; timers do NOT
tick there (only dl=1 ticks them). Cursor is set to 0 while waiting.

| cmd | name (proposed) | args | effect (addr) |
|---|---|---|---|
| 0x32 | GOTO | b g | cur=g, mode=GOTO, stop this list (0x14BB6) |
| 0x33 | REPLY | b g | cur=g, mode=REPLY, stop this list (0x14BE4) |
| 0x34 | HOOK | b g, b t | add choice group g to the menu of choice group t (append node state 0); ignored if g is already in t's chain (even if consumed) (0x14C12, 0x167F3) |
| 0x35 | THOOK | b g, b t | same, state 1: shown in ONE menu display, then consumed (0xFF) forever (0x14C8D) |
| 0x36 | ON | x/y | item.on = 1; also updates any pending TON/TOFF record so the restore keeps 1 (0x14D08, 0x16713) |
| 0x37 | OFF | x/y | item.on = 0 (same) (0x14D40) |
| 0x38 | SET | y g.i | current[g] = item i of NPC group g; idx[g] = i (0x14E03) |
| 0x39 | EXIT | – | exit = 1: conversation ends after this turn; rest of list still runs (0x14E58) |
| 0x3A | TALKHEAD | dlg, ?, fullname, code, mouth, head | reset talker registry (0x246D7: 20 slots, count=1); register slot 0 = name CCS[listA-first-directive slot 3], mouth anim = slot 4, head anim = slot 5 (only if both are type 02) (0x14E64→0x247DA). Slots 0..2 unused at run time |
| 0x3B | RUDE | b g | rudeGroup = g (start group of the next conversation after an aborted one) (0x14FA4) |
| 0x3C | DONE | b g, b t | DONE[g] = t (0x1510D) |
| 0x3D | ADDLINE (op_3D) | d line | protagList[count++] = line (lo byte only), max 25, reset each conversation (0x14FE0) |
| 0x3E | MOOD | d line | speak(line) with name "MOOD". Data: always `MOOD d0`; line 0 is a `{...}` mood note whose clip is empty → in practice a 250 ms pause (0x15098→0x18935) |
| 0x3F | UNSPEAK | x/y | item.said = 0 (choice becomes available again) (0x150B7) |
| 0x40 | BACK | b g | backGroup = g (start group of the next conversation after a normal end) (0x14FC2) |
| 0x41 | EMPTY (LINEATTR?) | b g, b t, c k | k==2: EMPTYgoto[g]=t, else EMPTYreply[g]=t (0x15132). Data: k ∈ {2, 17} |
| 0x42 | IFON | $v, #n, x/y | if G[v]==n: ON (0x1548A) |
| 0x43 | IFOFF | $v, #n, x/y | if G[v]==n: OFF (0x1540C) |
| 0x44 | IFSET | $v, #n, y g.i | if G[v]==n: SET (0x1562B) |
| 0x45 | DONEX (SUBMENU?) | b g, b t, x item | DONEX[g] = {t, item} (0x15172) |
| 0x46 | EMPTYX (SUBMENU4?) | b g, b t, x item, c k | k==2: EMPTYXgoto[g], else EMPTYXreply[g] = {t, item} (0x151FC). Data: k = 2 always |
| 0x47 | PROTAGONIST | $v or #n | i = value; if i < protagList.count: speak(protagList[i]) (0x15003) |
| 0x48 | IFREPLY | $v, #n, b g | if G[v]==n: REPLY g (0x15394) |
| 0x49 | IFGOTO | $v, #n, b g | if G[v]==n: GOTO g (0x15319) |
| 0x4A | TOFF | x/y | temporary OFF: record {item, original on} in list 0x7106C, on = 0 (0x14DBC, 0x16666) |
| 0x4B | TON | x/y | temporary ON (0x14D75) |
| 0x4C | IFTOFF | $v, #n, x/y | conditional TOFF (0x1550B) |
| 0x4D | IFTON | $v, #n, x/y | conditional TON (0x1559B) |
| 0x4E | COUNTCHOICES | b g, $v | G[v] = number of on && !said items of choice group g + hooked groups with state != 0xFF (0x156C7) |
| 0x4F | SPEAK | x/y | item.said = 1 (hide choice) — does NOT speak (0x150E2) |
| 0x50 | ANIMPHONEME | a mouth, a head | re-register talker slot 0 (name = listA-first-directive slot 3) with these anims (0x14EA9) |
| 0x51 | TALKER | a mouth, a head, code | if code (CCS name) already registered: replace that slot's anims, else new slot (count++, max 20, error "too many talkers") (0x14EE6) |
| 0x7C, 0x92, 0x93, 0x94 | SAY (LIPSYNC*) | name, name2, d line | speak(line) (0x15DA3 → 0x18566 → 0x2C0C9). All 4 identical; slot 1 unused; slot 0 name only used in debug text mode |

TON/TOFF restore (0x16758): at the start of every NPC turn and at CONVERSE end, each record (in creation order)
writes its saved value back and the list is freed. So temporary changes last until the next NPC line, i.e. they shape
exactly the next menu. (Quirk: TOFF twice on the same item → the second record saved 0 → stays off.) — H

Registration details (0x247DA): mouth anim: flags=STOP(4), current frame = rest frame (phoneme 5), prio 2,
countdown 0; head anim: frame 0, flags STOP, prio 1. On-the-fly anims are loaded (0x13566). — H

### Scene commands allowed in dialogue lists (same handlers as the scene interpreter) — H
BACKGRND, ANIM, ANIMPOS, ANIMSEQ, ANIMSETFRAME, ANIMPRIO, ANIMRATE, ANIMSPD, ANIMDEST, ANIMSOFF, ANIMDUMP,
ANIMSUSPEND (blocks), ANIMUNSUS, ANIMCOMPOSIT, ANIMPRISTINE, NOP, SOUND, LET, ADD, SUB, MULTIPLY, DIVIDE, RANDOM,
HOTON, HOTOFF, TIMEROFF, TIMERON, TIMERSET, TIMERGET, 0x82 TIMERRELOAD, MAPON, MAPOFF, 0xA5/0xA6 fades, 0x8D CD.
Handler addresses: 0x157C2 BACKGRND, 0x157EC ANIM, 0x15830 ANIMPOS, 0x15874 ANIMSEQ, 0x158BD ANIMSETFRAME,
0x15958 ANIMPRIO, 0x159F9 ANIMRATE, 0x15A3D/0x15A81 SPD/DEST, 0x15AC5 ANIMSOFF, 0x15B04 ANIMDUMP, 0x15BD7 ANIMSUSPEND,
0x15C18 ANIMUNSUS, 0x15C59/0x15C9F COMPOSIT/PRISTINE, 0x15CE5 NOP, 0x15CEA SOUND, 0x15DD6 LET, 0x15E64 ADD,
0x15EF2 SUB, 0x15FE1 MUL, 0x160A8 DIV, 0x16132 RANDOM, 0x161DF HOTON, 0x16285 HOTOFF, 0x1632B TIMEROFF,
0x1637F TIMERON, 0x163D3 TIMERSET, 0x16488 TIMERGET, 0x16504 TIMERRELOAD, 0x1655F MAPON, 0x16575 MAPOFF,
0x1658B/0x165B7 fades, 0x165E0 CD.
**No-ops in dialogue lists** (default 0x16648): 0x52..0x63, SCENE, TIMER, RETURN, CONVERSE, IFE..IFLE, HOTSPOT,
NEWSCENE, JUMP, SWITCH, 0x79, ANIMDEF, ACTIONSEQ, 0x80/0x81, RETURNVAL, ANIMARRV/ANIMFRAM/ANIMEND, HOTTYPE, IF*SUBR,
SETRECT, GOSUB, LABEL. (No conditionals or jumps besides the IF* dialogue commands.)

---------------------------------------------------------------------------------------------------

## 4. Speech audio and lip-sync

### Archives (Open_Dialogue_SoundDB 0x2AF51, path builder 0x195A1) — H
`n` = the dialogue's **CD-list name** (first string of the CD entry, e.g. `Quark010`; same names as DS9.LDM):
* `<cd>:\ds9\PHONEMES\n\n.lst` → u32 offsets into 0x71134 (count = filesize/4); `.all` kept open (FILE*, 0x711B8)
* `<cd>:\ds9\DIALOGUE\n\n.lst` → u32 offsets (0x710C8); `.all` opened raw (fd 0x711B4) and streamed
* Any file missing → error logged, CONVERSE is skipped entirely.
* **Entry index = the line number** (item.line) in both archives (0x2AED3: size = off[i+1]-off[i]; 0 = empty clip).
  The CCF clip name is NOT used to find audio; only its characters 3..5 (speaker code) are used. — H
* Example (real upload COM070): LST = {0, 0, 80187}: line 0 empty, line 1 = 80187 bytes = 3.64 s.

### speak(name, rec, line) (0x2C0C9) — H
```
if debug text mode (0x71EC9, never set in retail): draw name + CCQ text in a 640x445 box at y=35, wait for SPACE
cursor = 15
code = CCF[line][3..5] (speaker code, e.g. "pro", "qrk", "odo")
t = talker_lookup(code)                  ; 0x24741, case-insensitive name compare over registry slots 0..count-1
if not found (bit 0x8000):  play_plain(line)                     ; 0x2B26A
else: speedstep(0x71518) = 3; play_lipsync(t, line); speedstep = 1   ; 0x24964
wait 250 ms while rendering (0x2C424: ms counter 0x7143C up to 250)
```
* Sample format (0x2B1E2): AIL sample type (0,0) = **8-bit unsigned mono**, playback rate 0x5622 = **22050 Hz**
  (H; data check: COM070 mean 128.0). The 8-bit branch also calls 0x30155(s, 75) — probably sample volume 75/127 (L).
* play_plain: streams the entry in 0x71428-byte chunks; loop {feed, render(dl=0), until AIL status != PLAYING}.
  Blocks the whole game: no timers, no mouse; the only skip is key 'b' when 0x7144C != 0, and 0x7144C is never set.
  → **speech cannot be skipped** in retail. — H
* Protagonist lines ("pro") are never registered → plain playback (no lip-sync). (M: no data registers "pro")

### PHONEMES entry format (reader 0x24BC3) — H (structure), M (units)
```
u32 frameSamples                          ; time unit
repeat: u8 code; if code == 0xFF: end
        u8 dur; char word[] NUL-terminated (usually empty, the word text at word starts)
        ; nodes are malloc 0x24: +0 code, +1 dur, +2 word (<= 30 chars), +0x20 next
```
### play_lipsync(t, line) (0x24964) — H
```
if audio size == 0 or no phoneme nodes: return (silent)
unit = frameSamples * 100000 / 2205000            ; = frameSamples / 22.05 = ms
msClock(0x7143C) = 2*unit; last = 0; node = first
loop while playing:
    feed audio
    if mouth: mouth.frame = phonemeFrame(mouth, node.code)
    interval = (unit * node.dur) >> 6;  if 0: interval = unit      ; dur in 1/64 units, 0 = one unit
    if msClock - last >= interval:
        last = msClock; mouth.flags = STOP; mouth.rate = 0; render(dl=2) ; forced render
        if node.next: node = node.next
end: mouth.frame = phonemeFrame(mouth, 5)          ; rest mouth
```
phonemeFrame (0x24DE8): if the mouth anim has 13 frames: frame = code; else frame = table[code] with
table (0x246CA) = `02 00 02 01 04 03 03 04 00 03 04 03 03` (5-frame mouths). Codes 0..12. — H
render(dl=2) bypasses the 20 ms throttle, also erases STOP "clear" anims and advances frames; renders only happen on
phoneme changes, which is why other anims advance with speedstep 3 meanwhile. — H

---------------------------------------------------------------------------------------------------

## 5. Presentation

* **No subtitles.** NPC/player text (CCQ) is only shown in the debug text mode 0x71EC9 (never enabled); CCQ is not
  even loaded otherwise. — H
* **Talking heads are ordinary scene animations** (RLE from ANIMS/xxxx/NAME.RLE, defined in the scene, e.g.
  br002q2/br002q5) drawn by the normal renderer at their own positions; the dialogue lists show/hide them with
  ANIMSEQ/ANIM/ANIMSUSPEND, and TALKHEAD/ANIMPHONEME/TALKER tell the speech code which anim is the mouth (prio 2,
  frames = phonemes) and which the head (prio 1). 0x58600 = current scene's CCS symbol names (speaker codes). — H
  Data quirk: QUARK010/XOQ010 TALKHEAD register code "ras" although Quark's clips are "Qrk" (listing symbol may be
  off or data bug) → those lines would play without lip-sync (L).
* **Choice menu** (0x16DB2..0x17112) — H:
  * Labels: CCR[item.choice] (the short `[label]`), 25-byte buffer (≤24 chars), at most 20 entries, no scrolling.
  * Font: `art\short.fnt` (font 0; font 1 = `art\han.fnt`), context 0x55209 {font, fg, shadow, align=0 left}.
    FNT format (0x19A0B): u16 n (≤95, chars 0x20..), per glyph u8 height, u8 width (advance), u8 len, len bytes of
    1-bpp MSB-first rows, each row starting on a byte boundary. Line height = height of glyph 0 (0x715C6).
  * Glyph drawing (0x19BB9): fg pixel at (c,r) + shadow pixels at (c+1,r),(c,r+1),(c+1,r+1) in shadow colour, fg drawn
    on top; transparent elsewhere (cell 0xFF).
  * Colours (0x19720/0x1977F, from the palette loaded with the scene backgrounds): normal = palette index nearest to
    grey (40,40,40) [6-bit], highlight = nearest to (63,63,63), shadow = nearest to (0,0,0); nearest = min
    |dr|+|dg|+|db| over entries 0..254 excluding 1..5. Indexes 1..5 are a separate reserved set (copied to 0x71EA3,
    used by the 250 Hz timer code, probably colour cycling — L); not used by the menu.
  * Position: x = hs.x + hs.w + 10 if hs.x + hs.w + W + 20 < 640, else hs.x - (W + 10) (negative → 0), where hs =
    CONVERSE's hotspot rect and W = 0x71DA6 = widest CCR label of the whole dialogue. y = hs.y, raised to at least
    0x71D8C, and if y + (count+1)*lh > 480: y = 480 - (count+1)*lh. Entry i at (x, y + i*lh).
  * Drawn into the 640x480 overlay buffer 0x71020 (1 = transparent); the renderer composites the rect
    (x, y, maxWidth, count*lh) on top of all anim layers every frame while 0x71ECA != 0 (0x2372C), and erases it from
    the clean background when 0x71ECA == 2 (0x22DBF).
  * Hover: entry under the mouse in highlight colour, cursor image 15; selection = left-click edge on the entry.
  * The menu stays visible (chosen entry highlighted) while the player's line plays and its list runs; erased after.
  * One available choice → no menu; it is spoken automatically.

---------------------------------------------------------------------------------------------------

## 6. Timing — H
* After every spoken line (incl. MOOD and empty clips): 250 ms pause with rendering (0x2C424).
* No click/key skip of speech in retail (only 'b' with 0x7144C, never enabled). Mouse is ignored during speech.
* speedstep 0x71518 = 3 only during lip-synced playback; 1 otherwise (plain playback renders every 20 ms normally).
* No other pause between turns; NPC lines and lists chain immediately; ANIMSUSPEND in lists blocks (no timers).
* Global timers (type 1/3) tick only while the menu is waiting for input. Nothing ticks during speech/lists.

---------------------------------------------------------------------------------------------------

## 7. Timers, aborts, right-click — H
* Timer fire in menu: handler runs as `run_scene(label, mode 1)` until RETURNVAL; value v (with 0x8000):
  * 0x8000 (RETURNVAL #0): continue waiting in the menu.
  * v & 0xC0 (x or y arg): `cur = (v & 0x7FFF) >> 8` (= group byte); 0x80 (x g.i) → mode REPLY (menu of choice
    group g; missing group → "illegal reply textblock RETURNed"); 0x40 (y g.i) → mode GOTO (NPC group g; missing →
    "illegal character textblock"). Item byte ignored (and a value ≥ 0x40/0x80 would carry). Conversation continues.
  * otherwise (constant 1 or 2, or a variable): conversation ends (exit=1); if (v & 1) == 0 → rude=1.
    RETURNVAL #1 = polite end (next time BACK group), #2 = rude end (next time RUDE group).
* Abort paths (hotspot click, right-click, timer constant): exit = 1; rude = 1 unless timer value odd;
  lastPick[cur] = NULL, then every other lastPick[g] item gets said = 0 again (choices picked during this
  conversation in other groups become available again).
* Left-click on another enabled hotspot (not h): CONVERSE jumps to its label. Right-click with MAPON: returns 0xFFFE
  → CONVERSE returns 1 → game menu. Right-click with MAPOFF: ignored. 'M' key: not handled in conversations.
* Every left click reloads the idle timers (types 2/3, 0x23F41).
