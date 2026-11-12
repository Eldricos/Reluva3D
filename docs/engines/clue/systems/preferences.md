# Clue: Murder at Boddy Mansion: preferences

Reluva keeps its own (`preferences.ini` in its folder for the game).
The first time, with none yet, it takes the original's `Prefs.ini` from
the game's folder: 16 bytes, the seven switches in the Preferences
screen's order (a byte each, 1 on), a byte unused, then the music's and
the master volume as 32-bit numbers from 0 to 10 (`Clue.exe` 2.2 makes
them its mixer's by 127 / 10, at 0x415E63; its preferences' defaults at
0x42FA30: everything on but Auto Notes, both volumes 5). Reluva's
defaults are the same. The master volume is Miles' for the game's
sounds and voices (`AIL_set_digital_master_volume`), each of which plays
at 100 of 127 under it; the music's is the CD's own
(`AIL_redbook_set_volume`): the master volume doesn't touch the music.
The videos' sound is under neither: `AviPlayer.cpp` adds it to its
DirectShow stream with the default audio renderer
(`AddMediaStream(NULL, &MSPID_PrimaryAudio, AMMSF_ADDDEFAULTRENDERER, NULL)`,
its error message names the call) and never sets its volume (the
executable has no `IBasicAudio`), so it plays at the system's volume.
Reluva does the same: the videos (the intro, the deal, the suggestions,
the prison) at full whatever the sliders.
