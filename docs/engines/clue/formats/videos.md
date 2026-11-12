# Clue: Murder at Boddy Mansion: videos (`.avi`)

44 files, all Cinepak at 15 frames a second with 16-bit mono PCM sound at
22,050 Hz: the intro, the deal and the six suspects' prison clips
(`CM_prison.avi` ...) at 320×240; the 36 suggestion clips, a suspect
with a weapon, at 200×400 (`CM_candle.avi` alone at 250×500). The
suggestion clips last 6.3 s: 1.4-2.7 s of picture (in the five
looked at), then black frames while the sound goes on, as the original
shows them.

They're played by the shared AVI and Cinepak decoder
([docs/common/cinepak.md](../../../common/cinepak.md)), shown by the clock
(src/engines/clue/movie.h), with their sound at full volume, as Clue.exe's
DirectShow graph plays it.
