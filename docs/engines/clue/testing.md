# Clue: what to check in a playthrough

A checklist for playing Reluva's Clue (and, where it says so, the original
beside it) to confirm what was built from `Clue.exe`'s code but hasn't been
seen working, and to collect what [unconfirmed.md](unconfirmed.md) still
needs. Highest value first within each part.

**Have running:** a screen recording with sound, for anything about timing,
motion or sound; screenshots are enough for positions. Note the recording's
time with each finding. **[shot]** = a screenshot is enough; **[rec]** =
needs a recording; **[orig]** = compare with the original game.

**Set up:** the game data from the CD and the 2.2 patch; the CD's music
ripped (`reluva-tool cue-rip CLUE.cue <game folder>`) for the music checks.

## 1. The board and moving

- **Walking about your room** [rec]: on your turn, in a room, click its
  floor in a few places, round the furniture. The suspect should walk
  there without using a move; a move you had started planning is dropped
  (with `unting`). Clicking a door's square starts a move as before. Try it
  before and after rolling. [orig] Compare the way taken round furniture.
  In a network game, the other players should see the same walk.
- **Move numbers** [rec]: plan a move square by square. Each number should
  appear on its square with a `ting`; clicking back to an earlier square
  takes the later numbers away with an `unting`; a square you can't go to
  gives `nogo`.
- **Secret passages** [rec]: take one from a corner room; the suspect
  should come out of the other end's passage, facing out of it.

## 2. Hot spots

- **Reactions** [rec]: on your turn, in a room, click its furniture that
  reacts (the bear and the rhino in the billiard room, the piano, the
  phonograph, the stove, the globe, the grandfather clock ...). Each
  should play one of its reactions with its sound, and not again until it
  has finished.
- **When they don't react** [rec]: clicking them on a computer player's
  turn, from the corridor, or in a room you're not in should do nothing
  (the click is then a move's, or nothing).
- **The credits portraits** [rec]: in the study and the ballroom, the two
  small portraits in the credits painting should react when the pointer
  passes over them, with no click.
- **The board from above** [rec]: with the board shown, click where a hot
  spot of your room is drawn; it should react.

## 3. Sounds and music

- **Speed** [rec] [orig]: every game sound (clicks, steps, voices,
  thunder, the menus' thunder) plays 1/64 faster and higher than the
  file. Compare a voice line ("... cannot disprove") with the original.
- **Weather** [rec]: start a game and leave it running for six minutes
  without menus. Light gusts with rain should start at 1:00, stop at 1:15,
  start at 1:30, turn heavy at 2:00, a big thunder at 2:30, light again at
  3:00, stop at 3:30, light at 3:45, stop at 4:00, and the cycle starts
  again at 5:00; the drapes blow with the gusts. Small and middle thunders
  come by chance during the gusts. [orig] Time the same in the original
  (see [unconfirmed.md](unconfirmed.md#the-weathers-pace)).
- **The fire and the fountain** [rec]: walk past the library's fire and
  the conservatory's fountain; each should fade in and out with distance
  and pan with the view.
- **Footsteps** [rec]: each suspect's steps should fall as its feet come
  down (two a walking cycle), men's and women's different.
- **Music by screen** [rec] (needs the ripped tracks): the menus with the
  storm play no CD music; the character picker starts track 5, which plays
  on through the menus; leaving the main menu's Preferences stops it; the
  loading screen plays its own loop; the game starts on track 2 and goes
  2, 3, 4, 2 ...; after a suggestion's video one of 2-4 starts by chance.
- **Volumes** [rec]: the music slider changes only the music, the master
  slider everything else. At the default settings the music should sit
  about as loud against the effects as in the original [orig].

## 4. Pictures and animation

- **Animation speeds** [rec] [orig]: the suspects walk at 13-16 frames a
  second, their idles go to the end and back (4-8 a second), the fires
  flicker at 8, the menus' storm runs at 15.
- **The fountain** [shot]: its water should show the floor through it.
- **The menus' lightning** [rec]: each click in a menu with the storm
  flashes the four corner pictures with a thunderclap for about a second
  before the click acts.
- **The screen's frames** [shot]: the left and right frames start at y 5
  (just under the top one) and meet the bar; the ticker's text scrolls
  between x 41 and 599.

## 5. The windows over the game

- **Esc menu** [shot]: the Esc menu, its Save window, Preferences, How to
  Play and the exit window all at the same place (left 205, top 11).
- **In-game Preferences** [rec]: each switch, both volume sliders (the
  change heard at once), Continue back to the game, Esc back to the Esc
  menu; the game going on behind it.
- **In-game How to Play** [rec]: each of the seven topics; the text's
  arrows and scroll knob; Back to the topics; Continue back to the game.
- **The loser's window** [shot] [orig]: make a wrong accusation and note
  where its window is (see [unconfirmed.md](unconfirmed.md#the-losers-window-dialog-40)).

## 6. The computer players

- **Self-naming** [rec] [orig]: in a game against five experts (and one
  against normal players), note every suggestion from the ticker: who
  suggests whom, and when each kind of the answer seems settled. Count
  those where a player names itself before that. The more games the
  better (see [unconfirmed.md](unconfirmed.md#computer-players-naming-themselves)).
- **Pace** [rec]: a computer player's turn without a suggestion takes 5-9 s
  from one "..., it is your turn." to the next; with one, about 20 s.
