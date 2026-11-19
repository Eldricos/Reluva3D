// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
//
// Ghidra headless pre-script (runs after the import, before auto-analysis).
// nfs3.exe's Watcom linker writes .bss with VirtualSize 0, PointerToRawData 0 and
// SizeOfRawData 0x4aa400.  Ghidra's PE loader then maps .bss from file offset 0,
// truncated to the file length: the block holds the file's own bytes (headers,
// code) and stops at 0x6f2000, so the globals above it (0x6fd50c...) do not exist
// and get no references.  This replaces every such section with an uninitialized
// read/write block of its full size.
//@category Reluva

import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.*;

public class NfsreFixBss extends GhidraScript {
	@Override
	public void run() throws Exception {
		byte[] hdr = new byte[0x1000];
		try (RandomAccessFile f = new RandomAccessFile(currentProgram.getExecutablePath(), "r")) {
			f.readFully(hdr, 0, (int) Math.min(hdr.length, f.length()));
		}
		ByteBuffer b = ByteBuffer.wrap(hdr).order(ByteOrder.LITTLE_ENDIAN);
		int pe = b.getInt(0x3c);
		int ns = b.getShort(pe + 6) & 0xffff;
		int osz = b.getShort(pe + 20) & 0xffff;
		long base = b.getInt(pe + 52) & 0xffffffffL;
		int sec = pe + 24 + osz;
		Memory mem = currentProgram.getMemory();
		for (int i = 0; i < ns; i++) {
			int o = sec + 40 * i;
			String name = new String(hdr, o, 8, "latin1").replace("\0", "");
			long vsize = b.getInt(o + 8) & 0xffffffffL;
			long va = b.getInt(o + 12) & 0xffffffffL;
			long rsize = b.getInt(o + 16) & 0xffffffffL;
			long rptr = b.getInt(o + 20) & 0xffffffffL;
			int flags = b.getInt(o + 36);
			boolean uninit = (flags & 0x80) != 0 || rptr == 0;
			if (!uninit || rsize == 0 && vsize == 0)
				continue;
			long size = Math.max(vsize, rsize);
			Address start = toAddr(base + va);
			MemoryBlock blk = mem.getBlock(start);
			if (blk != null && !blk.isInitialized() && blk.getSize() >= size)
				continue;
			if (blk != null)
				mem.removeBlock(blk, monitor);
			MemoryBlock nb = mem.createUninitializedBlock(name, start, size, false);
			nb.setPermissions(true, true, false);
			println(String.format("nfsre fixbss: %s at 0x%x, 0x%x bytes, uninitialized", name, base + va, size));
		}
	}
}
