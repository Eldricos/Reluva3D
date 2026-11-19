import sys, zlib, struct, os
out = sys.argv[1]
for p in sys.argv[2:]:
    d = open(p,'rb').read()
    if d[:4] != b'PACK': continue
    csz, usz = struct.unpack('<II', d[4:12])
    u = zlib.decompress(d[12:12+csz])
    ok = len(u)==usz
    n = os.path.join(out, os.path.basename(p))
    open(n,'wb').write(u)
    print(f"{os.path.basename(p):28s} file={len(d):8d} c={csz:8d} u={usz:8d} ok={ok} trail={len(d)-12-csz} head={u[:16].hex()} {u[:16]!r}")
