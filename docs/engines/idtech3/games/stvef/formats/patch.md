# Elite Force: the 1.2 patch

The 1.2 patch, `eliteforcepatch1_2.exe` (20,636,701 bytes), is a Wise
installer ([wise.md](../../../../../common/wise.md)). Reluva takes the files it
needs out of it without running it.

Elite Force's 1.2 patch holds 33 files, the last ending at the end of the
installer. Among them, whole (not as differences from the old files):

| # | Offset | Packed | Size | File |
|---|---|---|---|---|
| 2 | 0x601b | 86,709 | 160,768 | `Wise0132.dll` (the installer's own) |
| 21 | 0x49157 | 428,322 | 925,696 | `stvoyHM.exe` 1.2 |
| 22 | 0xb1a7d | 338,764 | 720,963 | `stvoy.exe` 1.1 |
| 24 | 0x122a76 | 442,801 | 974,924 | `efgamex86.dll` 1.1 |
| 25 | 0x18ec2b | 137,675 | 376,904 | `efuix86.dll` 1.1 |
| 26 | 0x1b05fa | 6,427,478 | 6,492,912 | `baseEF/pak1.pk3` |
| 27 | 0x7d1954 | 12,329,744 | 12,408,486 | `baseEF/pak2.pk3` |

Each is byte for byte the file a patched install has. The rest are the
installer's screens, readme (RTF), text and settings (`[ActiveNet]`...).

Reluva takes `pak1.pk3`, `pak2.pk3` and the two 1.1 DLLs (`games.cpp`,
`patchFiles`).
