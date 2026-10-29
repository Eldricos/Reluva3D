# Testing (Murder on the Orient Express)

How Reluva's AWE engine is checked: the automated checks, what they
cover and don't, and a list of things to try in a playthrough by hand.

## Automated checks

### Unit tests

`ctest --test-dir build` runs `tests/test_awe.cpp` among the others:
the text tables, scenes, Granny files, Oodle1, the dialogue manager
(choices, picture choices, the wheel, talk starts), lighting and so on,
on small made-up data. They need no game files and run in CI.

### The whole game: `reluva-tool awe-regress`

    reluva-tool awe-regress <game folder> <route.txt> [-3d] [-quick]

Plays the whole game four ways, each from a new game, and ends with a
summary (exit code 1 if anything failed):

1. **The route by the mouse, with sound.** Every click and use is made
   at a point where the game's pick finds the object; voices and sounds
   take their own time. With `-3d`, the real renderer on a display.
2. **By the player's hand alone.** Every action by mouse and keys as a
   player makes them: items taken up from the inventory window, the bar
   brought down to travel, parts put in the combine boxes, choices
   clicked in the box, lines clicked through, movies skipped by Escape.
3. **Saved and loaded after every action,** played on from the loaded
   game; the story state (act, scene, tags, items, conversations, scenes
   seen) must come back the same.
4. **Wandering off the route.** Every 10 actions, up to 30 random actions
   (one in eight an item used where it isn't wanted), with saves and
   loads at random, then back to the route. Anything stuck, failed, lost
   on loading, not done or with nothing to do is listed.

Each passes when the end credits are reached and nothing is listed. The
route file comes from `awe-acts -solve`. It isn't in the repository, as
it names the game's files; `awe-regress` searches for one if the file
isn't there and keeps it. On the English install the quick run takes
about ten minutes. The options are described in
[acts.md](../../formats/awe/acts.md).

### Frames to look at

`awe-acts -play <route> -mouse -3d -shots <dir>` saves the screen before
every action (1,105 for the route); `-shot <n>c<k>` before a given
dialogue choice. These were set side by side with the walkthrough
recordings to check how scenes look (see
[reverse-engineering.md](reverse-engineering.md)).

### What the automated checks don't cover

- The real window: the operating system's events, the audio device, a
  real graphics card, the frame rate. The checks drive the game page's
  own event handling, on a virtual display at most.
- Whether the original game would answer an off-route action the same
  way: wandering only checks that nothing breaks.
- Whether the game can still be finished after wandering: the checks go
  back to the route.
- Releases other than English 1.0.

## A playthrough by hand

Things to try in a real window, on real hardware, that the checks don't
or can't. Note the act and scene for anything that looks or sounds
wrong, and if possible compare with the same moment of a recording.

### Throughout

- [ ] The window: the picture keeps its shape at your window or screen
      size.
- [ ] Sound: music, effects and voices play, and their volume sliders
      work; nothing crackles or stops.
- [ ] Frame rate: walking, the moving train's windows, snow and steam
      stay smooth; fades take about a third of a second.
- [ ] The cursor: the right verb over hotspots, doors, characters and
      items; the item in hand as the cursor; the top bar comes down at
      the top of the screen.
- [ ] Right-click opens and closes the inventory, or puts the item in
      hand back.
- [ ] The mouse wheel and middle button in a conversation's choices.
- [ ] Escape skips movies (not the legal notice); F2 quick-saves, F3
      quick-loads.

### Options

- [ ] Each option changes at once and is kept after quitting: volumes,
      brightness, captions (lines and movie subtitles go, choices stay),
      3D shadows, animated effects, fog, particles, anti-aliasing.

### Saving and loading

- [ ] Save in each act, in a close-up, and try mid-conversation, then
      load: the scene, items, characters' places and the music come back.
- [ ] Overwrite a save; the thumbnail and name show in the slot.
- [ ] Load a save made by the original game, if you have one.

### Off the route

- [ ] Use items on the wrong things and people: Antoinette's "that won't
      work" lines.
- [ ] Combine the wrong items; take combined items apart.
- [ ] Give wrong answers in act 8's quizzes, and pick "I need a moment
      to decide".
- [ ] Walk away from conversations and come back; talk to someone twice
      in a visit and on a later visit (the greeting changes).
- [ ] Travel between cars by the top bar where it's allowed; the bar
      refuses where it isn't (act 1 to 3, the forest, act 8).
- [ ] In the forest, walk without and with the snowshoes; the wolf;
      the hut's window.

### Moments worth a close look

- [ ] Act 1, Sirkeci: the market's crowd and the cleaver puzzle.
- [ ] Act 4: Ratchett's body on the bed, the gun close-up, the scarlet
      kimono in the corridor.
- [ ] Act 5: the puzzle box (closed on the carpet, opening), the torn
      letter, the ashes.
- [ ] Act 6: the forest and the shepherd's hut, the engine's cab, the
      baggage car's safe.
- [ ] Act 7: Barnaby's radio, the fingerprint cards under the magnifier.
- [ ] Act 8: the evidence choices with their pictures, the three
      solutions, the coda and the credits.
