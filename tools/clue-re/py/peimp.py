import sys, pefile
for p in sys.argv[1:]:
    pe = pefile.PE(p)
    print("=====", p, hex(pe.FILE_HEADER.TimeDateStamp), "linker", pe.OPTIONAL_HEADER.MajorLinkerVersion, pe.OPTIONAL_HEADER.MinorLinkerVersion)
    for s in pe.sections: print("  sec", s.Name.rstrip(b'\0'), hex(s.VirtualAddress), s.Misc_VirtualSize)
    for e in getattr(pe,'DIRECTORY_ENTRY_IMPORT',[]):
        names = [ (i.name.decode() if i.name else f"#{i.ordinal}") for i in e.imports]
        print(" ", e.dll.decode(), len(names), ", ".join(names))
    if hasattr(pe,'DIRECTORY_ENTRY_EXPORT'):
        print("  EXPORTS:", ", ".join((x.name or b'?').decode() for x in pe.DIRECTORY_ENTRY_EXPORT.symbols))
