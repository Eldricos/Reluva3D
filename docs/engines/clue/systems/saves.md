# Clue: Murder at Boddy Mansion: saved games

From the Esc menu, **Save** (when it's a human's turn and nothing is
going on) opens the original's Save window: eight slots; click one,
type its name, OK (or Enter). **Load Game** on the main menu lists them
(as the original's, from screenshots: each name in the small font,
centred in its slot; an empty slot lights under the pointer but does
nothing);
choosing one goes into the mansion and on from where it was saved,
without the deal.

They are kept in Reluva's folder for the game, `saves/slot1.sav` ...
`slot8.sav`: text, a header line `reluva-clue-save 1`, the name, then
the game: the players, the rules' state (cards, places, the turn, the
die's random numbers), each computer player's notebook and random
numbers, each human's notepad, and where the suspects stand. A save is
written beside the old one and then put in its place. The random numbers
are kept as their seed and how many have been drawn, read back by drawing
as many again: `std::mt19937`'s own text isn't the same in every standard
library, and a save has to load on any system Reluva runs on.
