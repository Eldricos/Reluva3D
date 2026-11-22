# Original definition archive: research status

Word definitions are not implemented in the native engine. They are separate
from the `.dic` membership tree and the new personal dictionary editor.
The supplied `Dicts/R016E613.DAT` is 13,263,360 bytes, SHA-256
`49789292122be628e46bad04cec4142c23e6513ac19123c76110babf108b54d1`.
It identifies an INSO reference database, with an entry-file component and
indexed storage. It is not a text dictionary or an RTF file to display directly.
No assumption that incidental compression signatures delimit complete entries
has been adopted.

Read-only original analysis links Boggle's reference initialization at
`0x432f10` to REDLL exports REmemory, REapp and REdb. Querying at `0x433360`
allocates an entry-list request, performs REentlst lookup, then allocates a
separate token context and calls REtoken. Contexts are packed structures with
unaligned fields; the main word tree is not a replacement for this protocol.

An external interpreter harness now loads and initializes the owner's
PDDLLW32/REDLLW32 modules, supplies bounded emulated memory/mutex operations and
read-only file/seek callbacks, opens this archive and obtains an entry-list hit
for a controlled English query. Token retrieval returns the caller-accepted
status 4. This establishes a working reference route for further independent
format work. The original wrappers, raw structures, archive and derived data
remain outside Git. None of these DLLs, original instructions or oracle calls
are used by the Boggle runtime; the import does not add the archive or DLLs.

Remaining work: specify the archive directory/index and entry decompression,
independently implement bounded headword lookup and token/markup decoding,
corroborate definitions and no-hit/variant cases against the original, and then
add a native definition view. Definition text must remain runtime data; no
retail entries may become source fixtures. This task is still open rather than
being represented as completed by the personal dictionary editor.
