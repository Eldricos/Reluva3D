# MSVCP60

## What it is

`src/common/x86/msvcp60.h` and `runtime/`: MSVCP60.dll, Visual C++ 6's C++
library.
Programs build its classes inline and derive from them, so it isn't
stood in for natively. `runtime/msvcp60.cpp` is our own replacement,
written from the C++ standard with the layouts measured from the
programs (below). `runtime/build_msvcp60.sh`
builds it with clang into a real DLL (`runtime/msvcp60_dll.h`, embedded),
importing only MSVCRT; `loadMsvcp60` loads it into a process.

## What Reluva supports

- Destructors and the vftable entries of the streams take `this` at the
  ios. Constructors take a hidden last argument, whether to build the
  ios.
- Exported vftables, in order:
  - `ctype<char>`: destructor, `do_tolower` (range), `do_tolower` (char),
    `do_toupper` (range), `do_toupper` (char);
  - `basic_streambuf`: destructor, `overflow`, `pbackfail`, `showmanyc`,
    `underflow`, `uflow`, `xsgetn`, `xsputn`, `seekoff`, `seekpos`,
    `setbuf`, `sync`, `imbue`.
- Open modes: in 1, out 2, ate 4, app 8, trunc 0x10, binary 0x20. State
  bits: eof 1, fail 2, bad 4.
- File buffers read and write the `FILE` directly and keep no characters
  of their own, so inline `sgetc` and similar always reach the virtual
  functions.
- Only the "C" locale, with `ctype<char>` its one facet.
- `_Lockit` does nothing while the process runs one thread.
- What would throw (`_Xlen`, `_Xran`, a stream's exception mask) prints why
  and aborts, as exceptions aren't dispatched yet.

## How it works

LEGO Alpha Team's code derives from the C++ library's classes, builds them
inline and reads their fields, so stand-ins written natively (as MSVCRT's
are) won't do: the objects have to be laid out as Visual C++ 6's were.
`src/common/x86/runtime/msvcp60.cpp` is a replacement written here, each
function doing what the C++ standard says. `build_msvcp60.sh` builds it
with clang (`i686-pc-windows-msvc`, freestanding, importing only MSVCRT)
into a DLL embedded in Reluva and loaded before the program's modules.
`msvcp60.def` maps each of the 167 names LEGO Alpha Team imports to its function.
clang was checked to give the same names for the same declarations, but
plain structs at measured offsets plus the `.def` keep the layouts under
our control. The tests call it as that game does (`testMsvcp60`).

### The layouts

Measured from LEGO Alpha Team's code (its inline constructors, the offsets it
reads, the vftables it builds for its own subclasses):

| Class | Size | What the game touches |
|---|---|---|
| `string` | 16 | allocator byte, characters +4, length +8, capacity +12; a sharing count in the byte before the characters (255: frozen); calls `_Grow`, `_Copy`, `_Eos`, `_Split`, `_Freeze`, `_Refcnt`, `_Tidy` |
| `ios_base` | 0x28 | state +4, exceptions +8, flags +0xc, precision +0x10, width +0x14, the locale pointer +0x20, the standard-stream index +0x24 (set to 0 inline) |
| `basic_ios` | 0x34 | stream buffer +0x28, tied stream +0x2c, fill +0x30 (set to `' '` inline) |
| `basic_streambuf` | 0x38 | get/put starts +4/+8; pointers to where the starts, next characters (+0x14, +0x18) and counts (+0x24, +0x28) are kept (+0xc, +0x10, +0x1c, +0x20, +0x2c, +0x30); locale +0x34. The get count is from the next character |
| `basic_filebuf` | 0x54 | a `string*` the inline destructor deletes +0x44, whether that destructor closes the file (a byte) +0x48, its own locale +0x4c, the `FILE*` +0x50 |
| `strstreambuf` | 0x50 | built inline by NiMain, then `_Init(n, get, put, mode)` |
| `istream`, `ostream` | 8, 4 + the ios | the vbtable pointer (the ios's offset is its second entry), the read count +4 |
| `ifstream`, `ofstream`, `fstream` | ios at +0x5c, +0x58, +0x60 | filebuf at +8, +4, +0xc |
| `istrstream`, `ostrstream` | ios at +0x58, +0x54 | |
| `ctype<char>` | 0x18 | built inline by LoadComp and a behaviour DLL: the reference count +4, `_Getctype`'s 16 bytes copied to +8, the table +0x10 |
| `fpos<int>` | 24 | offset, a 64-bit position at +8, state +16 |

## Sources and checks

- **Where the facts come from:** the C++ standard for what each function
  does; the layouts were measured from LEGO Alpha Team's code (above).

- It loads our MSVCP60 and calls it as a program would: strings in Visual
  C++ 6's layout, an `ifstream` reading lines and bytes, a string stream
  buffer growing as it's written (with a vftable built as the programs
  build theirs), and `cerr` with numbers and `endl`.
- **Confidence:** published (the C++ standard); the layouts from the code unless marked.

## Used by

- **`netimmerse`**: LEGO Alpha Team imports it
  ([running.md](../../engines/netimmerse/games/legoalphateam/systems/running.md)).
