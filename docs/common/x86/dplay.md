# DirectPlay

## What it is

`src/emulation/x86/dplay.{h,cpp}`: `x86::DirectPlay`, DirectX 6's
DirectPlay for programs run in the [x86 emulator](README.md): what
`DPLAYX.dll` exports, and its two classes through `CoCreateInstance`.
Written from the DirectX SDK's documentation of DirectPlay and its lobby;
the interfaces' methods are in the order `dplay.h` and `dplobby.h`
declare them.

## What Reluva supports

There's no network yet: DirectPlay answers as on a machine where no
service provider can connect.

- **The exports**, by name or by ordinal as programs import them: 1
  `DirectPlayCreate` (an `IDirectPlay`, whatever service provider is
  asked for), 2 and 3 `DirectPlayEnumerateA` and `W` (no service
  providers), 4 and 5 `DirectPlayLobbyCreateA` and `W`.
- **The classes**: `CLSID_DirectPlay` and `CLSID_DirectPlayLobby`, through
  `CoCreateInstance`; other classes go to the `createComObject` set
  before.
- **`IDirectPlay`, `IDirectPlay4` and `IDirectPlay4A`** on one object:
  `EnumConnections` finds none; `EnumSessions` times out at once, its
  callback told so (`DPESC_TIMEDOUT`); `InitializeConnection`, `Open`,
  `SecureOpen`, `GetPlayerAddress` (its size set to 0), `GetCaps` and the
  message counts are unavailable (`DPERR_UNAVAILABLE`); `Close` and
  `Initialize` succeed.
- **The lobby**, `IDirectPlayLobby` to `IDirectPlayLobby3A` on one object:
  the program wasn't started by one (`DPERR_NOTLOBBIED` from
  `GetConnectionSettings`, `Connect`, `ConnectEx` and the lobby
  messages); no programs are registered, no address types known.
  Addresses are made and read as documented: `CreateAddress` (the total
  size, the service provider, then the element given),
  `CreateCompoundAddress`, `EnumAddress`; too small a buffer gets
  `DPERR_BUFFERTOOSMALL` and the size needed.
- A method not listed ends the run, naming it.

## How it works

A DirectPlay address is a list of elements, each a `DPADDRESS` (the
element's type, a GUID, and its data's size) followed by its data. Those
made here start with `DPAID_TotalSize`, holding the whole address's size.

## Sources and checks

- **Where the facts come from:** the DirectX 6 SDK's documentation of
  DirectPlay, the lobby and DirectPlay addresses; `MAKE_DPHRESULT`'s
  codes. The interfaces' IIDs, the classes' and the address types' are
  those in LEGO Loco's `loco.exe`, which holds them; `IDirectPlay4`'s is
  its ANSI twin's less one *(inferred)*, as such pairs are numbered.
- **How it was checked:** `tests/common/dplay.cpp`
  (`common.unit.dplay`) makes the lobby and DirectPlay, asks for their
  later interfaces, builds an address and reads it back through a
  callback, and checks the answers without a network.
- **Confidence:** published unless marked.

## Used by

- **`legoloco`**: LEGO Loco makes the lobby and DirectPlay at its start,
  for its network games ([running.md](../../engines/legoloco/systems/running.md)).
