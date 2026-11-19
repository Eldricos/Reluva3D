# The Fallen: dialogue

> **Status:** partial · **Code:** `src/engines/unreal/vm/vm_dialog.cpp` · **Checked against:** the code (Engine.dll's `FDialogEngine`), the original's saves, and a recording · **Confidence:** from the code unless marked

## Overview

The characters' lines play from the dialogue tables
([dialogue-tables.md](../formats/dialogue-tables.md)), shown as they're
said: cut scenes, hails over the communicator (requests for supplies,
beamed down in the beam-down zones; reports, signing off) and the AI's
own lines (spotting the player, losing them, chatter).

## How it works

`vm_dialog.cpp` plays the characters' lines from the game's dialogue
tables ([dialogue-tables.md](../formats/dialogue-tables.md)) as Engine.dll's
dialogue engine (`FDialogEngine`, kept by the level) does, read from it;
the code is Reluva's own.
- **Two tables a level:** the general one (`GEN_Dialogue`) and its own:
  LevelInfo's `DialogFile`, else the one its map's mission names
  (M01_KiraL1A: `M01_Dialogue`). The menu's Entry has none. Their header
  rows give each type's priority (`Types`: 1 DIE, 2 CIN, 3 the hails and
  TRN, 4 PAN EXB EXG GSP DWN, 5 FSE, 6 SUR TTP GPI KIL FHE EVT HUD TBD,
  7 BAT LSE INT LED ACK, 8 AMB REP) and the faces' order (`Faces`).
- **Keys:** each line is known by TYPE, SPEAKER, LISTENER (`___` for
  none), LINE ID and MISSION run together (`HBOOBNKRA010K03`). A key
  asked for fits when each of its letters is `?`, or the line's is `_`
  (a line to no one fits any listener), or they're the same. NEXTLINE
  and RETURNLINE (MISSION TYPE SPEAKER ID in the tables) are keys so,
  found at load when only one line fits.
- **Running on:** in the tables' order (the general one, then the
  level's), a line runs on to the next when it's of the same type and
  the next number (Kira's log on the Promenade, 010 to 011; the opening's
  conversation, whoever speaks). That's its next line, whatever its
  NEXTLINE says, and such a line is never picked to start one.
- **Defaults:** the player's hail option (HOP) with no NEXTLINE goes on
  to the crewmate's answer (HBO from the listener to the speaker), with
  no RETURNLINE back to the player's sign-off (HCL); an answer to someone
  with no NEXTLINE to that someone's sign-off.
- **`FindBestDialogLine(out Index, Type, FilterTag, ListenerTag,
  SpeakerTag, LineID)`:** the key is the type, the speaker (asked, else
  the caller's own `Speaker_Tag`), the listener and the number, `?`s
  where not asked. Only a type and no tag finds nothing. The mission is
  never part of it. Of the lines it fits (not those that run on):
  - just one is the answer as it is;
  - else each is scored. Asked for by tag, only lines with that tag;
    otherwise a tagged line only for the caller whose `Dialogue_Tag` it
    is (+100,000). For whoever the player plays: a cut scene's always, a
    G mission's, one of the player's letter (K, S, W), an M mission's for
    Sisko and Worf (never Kira). Its condition met (+1,000,000). 10,000
    off for each hundred of its number. The times it's been said off (a
    pain cry's: a random 0 to 14 instead). The highest wins; of equals,
    the later in key order.
  - It returns how many fit, not how many would do.
- **Conditions** (`Conditional`): brackets and commas are spaces; words
  gather into terms between `AND` and `OR`, taken left to right (a 0
  before AND gives 0, a 1 before OR gives 1).
  - A term with a `.` in it is about a variable (`Class.Property op
    value`, of the caller), any other an event of the dialogue input's,
    by a fixed list: `ObjectiveCompleted` (1 to 9), `PlayerFaceEnemy`
    (the caller's enemy), `EnemyIsA`, `FriendClose`, `LowHealth`,
    `HasItem`, `SpecialCondition`, `PlayerIs` (`IsPlayer`), `InPlace`
    (`PlayerInPlace`), `DS9Phase` (`OpsLevel`)... A `!` anywhere in the
    term turns it round.
  - A name not on the list, or arguments missing, makes the condition
    one the engine can't test: such a line is never picked, and
    `TestCondition` reads it as met (and logs "Test Failed").
  - `LowAmmo( 0.2 )` has a `.`, so the engine takes it for a variable
    and it never holds: the six hail options that ask for ammunition
    when low are never picked, and the beam-down below can't happen.
    The original game's log would show it.
- **`IsDialogLineSpecial`:** a line never said yet, numbered under 800,
  and with a tag longer than a speaker's code or a condition. The HUD
  shows that a crewmate has something to say when hailing them would
  find one (`CheckSpecialComm`).
- **`StartDialog`** (and `PlayDialog`, which calls it): a hail (HAL)
  with no tag is the player's options to whom it hails (HOP), asked as
  the player; one asked by tag that finds nothing is tried as HOP. A new
  hail cuts off any hail line playing. It returns a thread number (0 if
  no line would do), and `IsDialogThreadPlaying` stays true while a line
  of it is said or waits.
- **A line starting** (`StartLine`):
  - A line for another character than the player's is swapped for the
    same line of the player's letter (then M, for Sisko and Worf), or
    not said.
  - Who says it: the caller, if it's the line's speaker; else (and for a
    voice-over or communicator line, V or H) whom the player's
    `GetSpeakerActor` names for its code.
  - **One line a speaker:** busy with a more important line, it refuses
    the new one; with a less important one (or the same type, or a HUD
    line) that one's cut off (no event, its thread ends); else the new
    one waits its turn, and lines waiting for that speaker that matter
    less are dropped.
  - **Its voice** through the speaker's `PlaySpeech` (at 0.65; 1.4 for
    deaths, pain, first sight and battle cries; 0.3 for EXG), which
    plays it on the talk slot within 1100 units, high-passed on the
    communicator's picture. The voice moves the speaker's mouth (Sound:
    lip-sync).
  - Said by a pawn: **the listener** (none for a hail; else of the pawns
    within 800 of the speaker, one with the line's listener code, else
    the nearest, the player counted at a fifth of its distance; in a cut
    scene only one with the code). **The level's** `Talker`,
    `Listener`, `CurrentLineType`, `CurrentLineIndex` and
    `CurrentLineFlags` (the CAMERA column's flags) if no line has them,
    or this one is of a more important type, or as important and said
    nearer the camera. Its animations (`PlayDialogAnim` at a tween of
    0.3) while the pawn is `Frozen`. Its face (`FacialExpression`, its
    place among the tables' faces; `FacialTimer` to half a second after
    it ends). `HudText` for its voice's length and pause. A note in
    brackets is heard only.
  - **How long:** the frame's time, its voice's length (3.5 seconds
    without one), its pause and a quarter second; a HUD line's clock runs
    while paused, the others' don't.
- **A line ending:** the level's fields cleared if they were its, its
  voice stopped. Then its EVENT (words apart by spaces: a tag to
  trigger, `DoEvent`, or with a `-` before it one of the HUD's
  beam-downs, `DoSpecialEvent`): so a line cut off never sets its event
  off. Then its thread goes on: its next line (found then, for no
  caller, if the tables didn't name just one), else the RETURNLINE its
  thread holds (not a sign-off after a sign-off), else the thread ends.
  A speaker destroyed ends its thread.
- **Hails** on M06_WorfL1A: "Captain, what is your status?", Sisko's
  report ("Heading to the objective. Neutralizing hostiles."),
  "Acknowledged. Worf out."
- **The CAMERA column's flags**: `V:1` a voice-over (a log), `H:1` heard
  over the communicator. They become `Level.CurrentLineFlags` 16 and 32
  (Pawn's head tracking then doesn't turn to the talker), and an H line's
  speaker is the HUD's picture of them (`GetSpeakerActor`'s
  `bForceUseHudMeshActor`). A line's place and turn (`A:`, `R:`) are the
  scripts' to ask for (`GetDialogueCameraData`, the cut scenes'
  directors and speech triggers).
- **The AI's own lines**, which no script asks for: the dialogue input's
  native `DialogTick`, which its `Tick` calls each tick while the HUD
  shows no letterbox. Reluva does as Engine.dll's does. It does nothing if
  the HUD's `IsLetterBoxOn` says so, or the player's `PlayerStart` has
  `bNoAutoTriggerDialog`. Otherwise it goes through every `CTVE_Pawn` that
  isn't the player and has a speaker code. Each pawn's state is kept in
  its `DLG_` variables.
  - **Gates:** more than 0.5 seconds since it last spoke
    (`DLG_LastSpeakTime`), not `bForceStasis`, and within 800 of the
    player. (The engine also wants the pawn's zone drawn in the last 5
    seconds; Reluva doesn't keep that.) On a level with `bNoActionLevel`,
    only TTP is possible.
  - **The type, first that fits** (a type it has no lines of at all is
    marked in `DLG_Flags` and never tried again):
    - DIE: the tick it dies.
    - PAN: hurt this tick. 70% of the time; otherwise a breath
      (`PlayFoleySound` 15) instead.
    - FSE: a new enemy that's alive and `IsActorAnEnemy`, unless the
      pawn is a friend and its enemy the player.
    - LSE: its enemy gone while still alive, with the same exception.
    - KIL: the player died with this pawn as their enemy. Once: it sets
      `DLG_Command` to KIL.
    - FHE and GPI: when its `DLG_Command` becomes that. The Vorta's and
      others' card-giving sets GPI.
    - The rest only after 4 seconds since a try that failed
      (`DLG_LastAttemptedSpeakTime`):
      - LED: while it's `bIsLeading`.
      - SUR: while the dialogue input's `PawnSurrenderOrPray` says so (in
        `Cowering` or `Surrender`).
      - Both of those within 300 of the player, at random about once
        every 4 seconds, once it's been 4 seconds quiet. Nothing else is
        said while either holds.
      - TTP: no enemy; the player alive, within 200 and not faster than
        200; the pawn facing the player (within about 45°, or 72° inside
        75 units). On a no-action level the player must be looking at
        the pawn too.
      - Else, not on a no-action level:
        - AMB: at random about once every 15 seconds (5 with a
          `Dialogue_Tag`), once it's been 15 quiet. Only with no live
          enemy, or as a friend its enemy isn't after.
        - Else, about once every 4 seconds: BAT while `bIsAttacking`
          against a live enemy, or INT while playing
          `InterrogateBajoran`.
        - Else REP: about once every 5 seconds, after 5 quiet, with no
          live enemy as for AMB.
  - **Saying it:**
    - Beyond (1.2 − `ObstructionFactor`) × 800 the player must see the
      pawn, else the try fails.
    - The line is its own (its speaker code; a tagged line only for the
      actor with that `Dialogue_Tag`, as a Vorta's watch-station report is
      that Vorta's), the start of what it says, its conditions about it.
    - A TTP isn't said again: not the same line, nor a general one after
      one (`DLG_LastSpokenTTP`). Nor while a hail is on.
    - Said, it sets `DLG_LastSpeakTime` and `DLG_LastSpokenLine`.
    - Then it goes through `StartLine`'s rules for a busy speaker, as
      any line does.
  - **Every tick**, for every pawn within the gates or not, its
    `DLG_OldHealth`, `DLG_OldEnemy` and `DLG_OldCommand` are what it has
    now. So a change while it's far off is never voiced.
  - Over all maps' first 3 seconds the AI says 2 lines.
- Over all maps' first 20 seconds, 30 dialogues start (66 lines said)
  and the AI says 11 lines of its own. FindBestDialogLine finds no line
  130 times: mostly scripts asking whether a pawn has a line to say.

## In Reluva

`vm/vm_dialog.cpp` runs the dialogue engine; the HUD shows the lines
([hud.md](hud.md)) and [sound.md](sound.md) plays the voices.

## Sources and checks

- **Where the facts come from:** Engine.dll's dialogue engine
  (`FDialogEngine`), read for what it does, and the tables.
- **How it was checked:** in play (above); the tables as the engine
  builds them against the original's save
  ([save.md](../formats/save.md)).

## Open questions

- No dialogue table for mission 12: the game's own gap, which nothing
  here can fill ([dialogue-tables.md](../formats/dialogue-tables.md)).
