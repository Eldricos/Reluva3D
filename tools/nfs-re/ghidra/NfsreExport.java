// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
//
// Ghidra headless post-script: export what the gdecomp / gxref commands read.
//
//   args: <outdir> <stamp> [func-addr ...]
//
// Writes <outdir>/decomp/<addr>.c (one per function, first line "// stamp <stamp> ..."),
// and, when no function addresses are given, <outdir>/functions.json (entry, name,
// body ranges) and <outdir>/xrefs.json (every memory reference, by target).
// With addresses, only those functions are decompiled again.
//@category Reluva

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.atomic.AtomicInteger;

import ghidra.app.decompiler.*;
import ghidra.app.decompiler.parallel.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.util.task.TaskMonitor;

public class NfsreExport extends GhidraScript {

	static String hex(Address a) {
		return String.format("0x%x", a.getOffset());
	}

	static String js(String s) {
		StringBuilder b = new StringBuilder("\"");
		for (char c : s.toCharArray()) {
			if (c == '"' || c == '\\')
				b.append('\\').append(c);
			else if (c < 0x20)
				b.append(String.format("\\u%04x", (int) c));
			else
				b.append(c);
		}
		return b.append('"').toString();
	}

	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		if (args.length < 2) {
			printerr("usage: NfsreExport.java <outdir> <stamp> [func-addr ...]");
			return;
		}
		File out = new File(args[0]);
		File dir = new File(out, "decomp");
		dir.mkdirs();
		String stamp = args[1];
		FunctionManager fm = currentProgram.getFunctionManager();
		List<Function> todo = new ArrayList<>();
		if (args.length > 2) {
			for (int i = 2; i < args.length; i++) {
				Function f = fm.getFunctionContaining(toAddr(Long.decode(args[i])));
				if (f != null)
					todo.add(f);
				else
					println("no function at " + args[i]);
			}
		}
		else {
			for (Function f : fm.getFunctions(true))
				if (!f.isExternal() && !f.isThunk())
					todo.add(f);
			exportFunctions(new File(out, "functions.json"));
			exportXrefs(new File(out, "xrefs.json"));
		}
		long t0 = System.currentTimeMillis();
		AtomicInteger ok = new AtomicInteger(), bad = new AtomicInteger();
		DecompileConfigurer conf = d -> {
			DecompileOptions o = new DecompileOptions();
			o.grabFromProgram(currentProgram);
			d.setOptions(o);
			d.toggleCCode(true);
			d.toggleSyntaxTree(false);
			d.setSimplificationStyle("decompile");
		};
		DecompilerCallback<Void> cb = new DecompilerCallback<Void>(currentProgram, conf) {
			@Override
			public Void process(DecompileResults r, TaskMonitor m) throws Exception {
				Function f = r.getFunction();
				String body;
				if (r.getDecompiledFunction() != null) {
					body = r.getDecompiledFunction().getC();
					ok.incrementAndGet();
				}
				else {
					body = "// decompiler failed: " + r.getErrorMessage() + "\n";
					bad.incrementAndGet();
				}
				String head = String.format("// stamp %s\n// %s %s  (Ghidra %s, %s)\n", stamp,
					hex(f.getEntryPoint()), f.getName(), getGhidraVersion(), f.getCallingConventionName());
				Path p = new File(dir, String.format("%x.c", f.getEntryPoint().getOffset())).toPath();
				Path tmp = new File(dir, String.format("%x.c.tmp", f.getEntryPoint().getOffset())).toPath();
				Files.write(tmp, (head + body).getBytes(StandardCharsets.UTF_8));
				Files.move(tmp, p, StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
				return null;
			}
		};
		cb.setTimeout(120);
		try {
			ParallelDecompiler.decompileFunctions(cb, todo, monitor);
		}
		finally {
			cb.dispose();
		}
		println(String.format("nfsre export done: %d functions decompiled (%d failed) in %.1f s", ok.get(),
			bad.get(), (System.currentTimeMillis() - t0) / 1000.0));
	}

	void exportFunctions(File file) throws IOException {
		try (Writer w = new BufferedWriter(new OutputStreamWriter(new FileOutputStream(file), StandardCharsets.UTF_8))) {
			w.write("{\n");
			boolean first = true;
			for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
				if (f.isExternal())
					continue;
				StringBuilder rs = new StringBuilder();
				for (AddressRange r : f.getBody().getAddressRanges())
					rs.append(rs.length() > 0 ? "," : "").append(String.format("[%d,%d]",
						r.getMinAddress().getOffset(), r.getMaxAddress().getOffset()));
				w.write(String.format("%s%s: {\"name\": %s, \"cc\": %s, \"sig\": %s, \"body\": [%s]}",
					first ? "" : ",\n", js(hex(f.getEntryPoint())), js(f.getName()),
					js(f.getCallingConventionName()), js(f.getPrototypeString(false, false)), rs));
				first = false;
			}
			w.write("\n}\n");
		}
	}

	void exportXrefs(File file) throws IOException {
		ReferenceManager rm = currentProgram.getReferenceManager();
		Listing listing = currentProgram.getListing();
		FunctionManager fm = currentProgram.getFunctionManager();
		int n = 0;
		try (Writer w = new BufferedWriter(new OutputStreamWriter(new FileOutputStream(file), StandardCharsets.UTF_8))) {
			w.write("{\n");
			boolean first = true;
			AddressIterator it = rm.getReferenceDestinationIterator(currentProgram.getMemory(), true);
			while (it.hasNext()) {
				Address to = it.next();
				if (!to.isMemoryAddress())
					continue;
				StringBuilder rows = new StringBuilder();
				for (Reference r : rm.getReferencesTo(to)) {
					Address from = r.getFromAddress();
					if (!from.isMemoryAddress())
						continue;
					Function f = fm.getFunctionContaining(from);
					CodeUnit cu = listing.getCodeUnitContaining(from);
					String text = cu == null ? "" : (cu instanceof Instruction ? cu.toString()
						: "data " + ((Data) cu).getDataType().getName());
					rows.append(rows.length() > 0 ? "," : "").append(String.format("[%d,%s,%s,%d,%s,%s]",
						from.getOffset(), js(r.getReferenceType().getName()), js(r.getSource().toString()),
						r.getOperandIndex(), f == null ? "null" : Long.toString(f.getEntryPoint().getOffset()),
						js(text)));
					n++;
				}
				if (rows.length() == 0)
					continue;
				w.write(String.format("%s\"%d\": [%s]", first ? "" : ",\n", to.getOffset(), rows));
				first = false;
			}
			w.write("\n}\n");
		}
		println("nfsre export: " + n + " references");
	}
}
