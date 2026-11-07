# The bridge (STELLAR.DAT)

From STG-Engine's notes on the 2 CD Windows release ([README](README.md) says how they were made). All values are little-endian.

## The bridge (`STELLAR.DAT`): `src/engines/stgenerations/game/bridge.h`

`DESIGN/UNIVERSE.INI` describes the map: `[Universe]` (the ten suns, 25
fleets), `[Display]` (MinZ = zoom distance, MaxZ = world scale and the sector
camera's height, SizeMagnification, DistanceMagnification, CenterX/Y), each
sun (`Location=x,y,z`: with z = 0 a screen position in the sector view; its
texture, destruction and scan pictures, GotoSound, SimDestructionSound,
planets), each planet and moon (`Distance`, `Speed` in seconds an orbit,
`OrbitPitch`/`OrbitIntersect`), `[SpaceStation]`, `[FleetN]` (`Type`,
`NumShips`, `Cloaked`, `Alert`), `[DataN]`/`[PicardN]` (frames and
`SequenceK=frame,ms`) and `[Nexus]`. Most sections appear again at the end
with the scan texts; lookups see both. The projection (0x46CD96) is
`x = CenterX + (X - camera.x) / d`, `r = Size / 2 * SizeMagnification / d`
(1..63) with `d = |Z - camera.z|`, the sun at
`((x + 320 - CenterX) * MaxZ, (y + 180 - CenterY) * MaxZ, 10 z)`.

The fleets' and the Nexus's `.RLE` files are OVERLAY data (frames top down,
each frame's rows bottom up), in `FLEET\ENTSIN.BMP`'s and `NEXUS\NEXUS.BMP`'s
palettes. Every other picture has its own palette, so the bridge is drawn in
true colour; index 254 is see-through.

The stardate (48623.4 on a new game) only moves as the player acts: a warp
1.0, a simulated destruction 0.3, a scan or a zoom 0.1; the display climbs a
tenth every 500 ms. `SDTimer d` sets a deadline d past the stardate shown, and
`Wait Timer` holds until it's passed with the Enterprise at a star.
`OnTransport`/`OnTactical` don't wait: they arm the panel's button, which
lights when a short-range scan has found Soran in the Enterprise's system.
They are the only things that set what it offers (the panel's +0x170: 1
transport, 2 tactical; set by 0x474149, called only from their commands,
0x418349 and 0x4182FC, and kept in 0x56F840 for the next panel): a warp
doesn't change it, the button is only off while the controls are. Nothing
in the original sets it back to nothing, even a new game; this engine does
on a new game, which makes no difference as each section arms it again.
`IfClue` is true when the last away mission set any outcome code.
