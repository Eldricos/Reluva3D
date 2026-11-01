// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
//
// Ghidra headless post-script: apply Reluva's symbols.json (from make_symbols.py)
// to the nfs3.exe program.  Re-runnable: it replaces what an earlier run applied
// (names it set, the /nfsre data types, curated signatures) without re-analysis.
//
//   args: <symbols.json>
//
// - adds a __watcall prototype (Watcom register convention: eax, edx, ebx, ecx,
//   then stack; callee pops; every register but eax kept) as a program
//   specification extension, and makes it the convention of every function whose
//   signature analysis did not lock;
// - creates the curated structs in category /nfsre and applies them to the struct
//   arrays and pointer tables, and the curated global types;
// - names functions and globals (SourceType.USER_DEFINED; names this script set
//   earlier that symbols.json no longer has are removed);
// - for functions with curated argument types: lets the decompiler recover the
//   parameters, then commits them with the curated registers typed (car* in eax).
//@category Reluva

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.regex.*;

import com.google.gson.*;

import ghidra.app.decompiler.*;
import ghidra.app.decompiler.parallel.*;
import ghidra.util.task.TaskMonitor;
import ghidra.app.script.GhidraScript;
import ghidra.program.database.SpecExtension;
import ghidra.program.model.address.*;
import ghidra.program.model.data.*;
import ghidra.program.model.lang.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.listing.Function.FunctionUpdateType;
import ghidra.program.model.pcode.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.util.CodeUnitInsertionException;
import ghidra.program.model.data.DataUtilities.ClearDataMode;
import ghidra.program.model.mem.Memory;

public class NfsreApply extends GhidraScript {

	static final String CC = "__watcall";
	static final String[] ARGREGS = { "EAX", "EDX", "EBX", "ECX" };
	static final CategoryPath CAT = new CategoryPath("/nfsre");

	static final String WATCALL_XML =
		"<prototype name=\"__watcall\" extrapop=\"unknown\" stackshift=\"4\">\n" +
		"  <input>\n" +
		"    <pentry minsize=\"1\" maxsize=\"4\"><register name=\"EAX\"/></pentry>\n" +
		"    <pentry minsize=\"1\" maxsize=\"4\"><register name=\"EDX\"/></pentry>\n" +
		"    <pentry minsize=\"1\" maxsize=\"4\"><register name=\"EBX\"/></pentry>\n" +
		"    <pentry minsize=\"1\" maxsize=\"4\"><register name=\"ECX\"/></pentry>\n" +
		"    <pentry minsize=\"1\" maxsize=\"500\" align=\"4\"><addr offset=\"4\" space=\"stack\"/></pentry>\n" +
		"  </input>\n" +
		"  <output killedbycall=\"true\">\n" +
		"    <pentry minsize=\"4\" maxsize=\"10\" metatype=\"float\" extension=\"float\"><register name=\"ST0\"/></pentry>\n" +
		"    <pentry minsize=\"1\" maxsize=\"4\"><register name=\"EAX\"/></pentry>\n" +
		"  </output>\n" +
		"  <unaffected>\n" +
		"    <varnode space=\"ram\" offset=\"0\" size=\"4\"/>\n" +
		"    <register name=\"ESP\"/><register name=\"EBP\"/><register name=\"ESI\"/><register name=\"EDI\"/>\n" +
		"    <register name=\"EBX\"/><register name=\"ECX\"/><register name=\"EDX\"/>\n" +
		"    <register name=\"DF\"/><register name=\"FS_OFFSET\"/>\n" +
		"  </unaffected>\n" +
		"  <killedbycall><register name=\"ST0\"/><register name=\"ST1\"/></killedbycall>\n" +
		"</prototype>\n";

	DataTypeManager dtm;
	Map<String, Structure> structs = new HashMap<>();
	int nStructs, nFuncNames, nFuncNew, nGlobNames, nTypes, nSigs, nCC, nRemoved, nTypeSkipped, nRet;

	Address addr(String hex) {
		return toAddr(Long.parseLong(hex.replaceFirst("^0x", ""), 16));
	}

	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		if (args.length < 1) {
			printerr("usage: NfsreApply.java <symbols.json>");
			return;
		}
		JsonObject root = JsonParser.parseString(
			new String(Files.readAllBytes(Paths.get(args[0])), StandardCharsets.UTF_8)).getAsJsonObject();
		dtm = currentProgram.getDataTypeManager();

		addWatcall();
		buildStructs(root.getAsJsonObject("structs"));

		// wanted names: address -> name
		Map<Address, String> fnames = new HashMap<>();
		Map<Address, JsonObject> fentries = new HashMap<>();
		for (Map.Entry<String, JsonElement> e : root.getAsJsonObject("functions").entrySet()) {
			JsonObject o = e.getValue().getAsJsonObject();
			if (o.has("name")) {
				fnames.put(addr(e.getKey()), o.get("name").getAsString());
				fentries.put(addr(e.getKey()), o);
			}
		}
		Map<Address, String> gnames = new HashMap<>();
		Map<Address, JsonObject> gentries = new HashMap<>();
		for (Map.Entry<String, JsonElement> e : root.getAsJsonObject("globals").entrySet()) {
			JsonObject o = e.getValue().getAsJsonObject();
			if (o.has("name")) {
				gnames.put(addr(e.getKey()), o.get("name").getAsString());
				gentries.put(addr(e.getKey()), o);
			}
		}

		if (args.length > 1)
			createMissing(Paths.get(args[1]));
		removeStale(fnames, gnames);
		applyData(root, gentries);
		for (Map.Entry<Address, String> e : gnames.entrySet()) {
			Symbol s = getSymbolAt(e.getKey());
			if (s != null && s.getName().equals(e.getValue()) && s.getSource() == SourceType.USER_DEFINED)
				continue;
			createLabel(e.getKey(), e.getValue(), true, SourceType.USER_DEFINED);
			nGlobNames++;
		}

		// functions: names and the calling convention
		FunctionManager fm = currentProgram.getFunctionManager();
		for (Map.Entry<Address, String> e : fnames.entrySet()) {
			Address a = e.getKey();
			Function f = fm.getFunctionAt(a);
			if (f == null) {
				if (getInstructionAt(a) == null)
					disassemble(a);
				f = createFunction(a, e.getValue());
				if (f == null) {
					println("could not create function at " + a);
					continue;
				}
				nFuncNew++;
			}
			if (!f.getName().equals(e.getValue()) || f.getSymbol().getSource() != SourceType.USER_DEFINED) {
				f.setName(e.getValue(), SourceType.USER_DEFINED);
				nFuncNames++;
			}
			JsonObject o = fentries.get(a);
			String desc = o.has("desc") ? o.get("desc").getAsString() : null;
			f.setComment(desc);
		}
		Map<Address, JsonObject> curatedArgs = new HashMap<>();
		for (Map.Entry<Address, JsonObject> e : fentries.entrySet())
			if (e.getValue().has("args"))
				curatedArgs.put(e.getKey(), e.getValue().getAsJsonObject("args"));

		// every signature but the imported ones is derived again (so a re-run gives the same result)
		List<Function> todo = new ArrayList<>();
		for (Function f : fm.getFunctions(true)) {
			if (f.isThunk() || f.isExternal() || f.getSignatureSource() == SourceType.IMPORTED)
				continue;
			resetSignature(f);
			todo.add(f);
		}
		nCC = todo.size();
		applySignatures(todo, curatedArgs);
		println(String.format("nfsre apply done: %d structs, %d data types applied (%d skipped: overlap/code), " +
			"%d function names (%d new functions), %d global labels, %d functions set to %s, " +
			"%d signature updates, %d stale names removed",
			nStructs, nTypes, nTypeSkipped, nFuncNames, nFuncNew, nGlobNames, nCC, CC, nSigs, nRemoved));
	}

	/**
	 * Ghidra's analysis misses many functions only reached through pointer tables
	 * (a quarter of the code angr finds).  Make one at each angr function start that no
	 * Ghidra function contains yet, in address order (so angr's split-off interior
	 * blocks end up inside the function made at the real start).
	 */
	void createMissing(Path starts) throws Exception {
		if (!Files.exists(starts))
			return;
		FunctionManager fm = currentProgram.getFunctionManager();
		Memory mem = currentProgram.getMemory();
		int n = 0, bad = 0;
		TreeSet<Long> list = new TreeSet<>();
		for (String line : Files.readAllLines(starts))
			if (!line.isBlank())
				list.add(Long.decode(line.trim()));
		for (long v : list) {
			Address a = toAddr(v);
			if (!mem.getExecuteSet().contains(a) || fm.getFunctionContaining(a) != null)
				continue;
			if (getInstructionAt(a) == null) {
				if (getDataContaining(a) != null || getInstructionContaining(a) != null) {
					bad++;
					continue; // data or the middle of an instruction in Ghidra's view
				}
				disassemble(a);
			}
			if (getInstructionAt(a) != null && createFunction(a, null) != null)
				n++;
			else
				bad++;
		}
		long t0 = System.currentTimeMillis();
		if (n > 0)
			analyzeChanges(currentProgram); // references, constants and stack for the new code
		println(String.format("nfsre apply: %d functions made at angr's function starts (%d not), " +
			"analysed in %.1f s", n, bad, (System.currentTimeMillis() - t0) / 1000.0));
	}

	void addWatcall() throws Exception {
		PrototypeModel m = currentProgram.getCompilerSpec().getCallingConvention(CC);
		if (m != null)
			return;
		SpecExtension ext = new SpecExtension(currentProgram);
		ext.addReplaceCompilerSpecExtension(WATCALL_XML, monitor);
		println("added the " + CC + " prototype (specification extension)");
	}

	void resetSignature(Function f) throws Exception {
		f.setCustomVariableStorage(false);
		f.replaceParameters(new ArrayList<Variable>(), FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS, true,
			SourceType.DEFAULT);
		f.setReturnType(DataType.DEFAULT, SourceType.DEFAULT);
		f.setCallingConvention(CC);
		f.setSignatureSource(SourceType.DEFAULT);
	}

	// ---- types ---------------------------------------------------------------------

	static final Pattern TY = Pattern.compile("^(\\w+)(?::(\\w+))?(?:\\[(\\w*)\\])?\\??$");

	DataType base(String b, String target) {
		switch (b) {
			case "u8": return ByteDataType.dataType;
			case "s8": return SignedByteDataType.dataType;
			case "u16": return UnsignedShortDataType.dataType;
			case "s16": return ShortDataType.dataType;
			case "u32": return UnsignedIntegerDataType.dataType;
			case "s32": return IntegerDataType.dataType;
			case "f32": return FloatDataType.dataType;
			case "ptr":
				if (target != null && structs.containsKey(target))
					return dtm.getPointer(structs.get(target));
				return dtm.getPointer(VoidDataType.dataType);
		}
		return null;
	}

	/** the type for a curated type string; null when unknown or the array length is not given */
	DataType parse(String t) {
		Matcher m = TY.matcher(t.trim());
		if (!m.matches())
			return null;
		DataType b = base(m.group(1), m.group(2));
		if (b == null || m.group(3) == null)
			return b;
		if (m.group(3).isEmpty())
			return null;
		int n = Integer.decode(m.group(3));
		return new ArrayDataType(b, n, b.getLength(), dtm);
	}

	static String ident(String n) {
		return n.replaceAll("[^A-Za-z0-9_]", "_");
	}

	void buildStructs(JsonObject js) throws Exception {
		// pass 1: every struct exists (so pointer fields can name them)
		for (Map.Entry<String, JsonElement> e : js.entrySet()) {
			String name = e.getKey();
			int size = Integer.decode(e.getValue().getAsJsonObject().get("size").getAsString());
			DataType old = dtm.getDataType(CAT, name);
			Structure s;
			if (old instanceof Structure)
				s = (Structure) old;
			else
				s = (Structure) dtm.addDataType(new StructureDataType(CAT, name, size, dtm),
					DataTypeConflictHandler.REPLACE_HANDLER);
			structs.put(name, s);
		}
		// pass 2: the layouts
		for (Map.Entry<String, JsonElement> e : js.entrySet()) {
			String name = e.getKey();
			JsonObject so = e.getValue().getAsJsonObject();
			int size = Integer.decode(so.get("size").getAsString());
			StructureDataType s = new StructureDataType(CAT, name, size, dtm);
			if (so.has("desc"))
				s.setDescription(so.get("desc").getAsString());
			TreeMap<Integer, JsonObject> fields = new TreeMap<>();
			for (Map.Entry<String, JsonElement> f : so.getAsJsonObject("fields").entrySet())
				fields.put(Integer.decode(f.getKey()), f.getValue().getAsJsonObject());
			int prevEnd = 0;
			DataTypeComponent prev = null;
			for (Map.Entry<Integer, JsonObject> f : fields.entrySet()) {
				int off = f.getKey();
				JsonObject fo = f.getValue();
				String fname = ident(fo.get("name").getAsString());
				String ty = fo.get("type").getAsString();
				String desc = fo.has("desc") ? fo.get("desc").getAsString() : null;
				if (off < prevEnd && prev != null) {
					// curated fields overlap (a name inside an array): note it on the outer one
					String c = prev.getComment();
					prev.setComment((c == null ? "" : c + "; ") + String.format("+0x%x %s (%s)", off, fname, ty));
					continue;
				}
				DataType dt = parse(ty);
				if (dt == null)
					dt = Undefined1DataType.dataType;
				Integer next = fields.higherKey(off);
				int limit = Math.min(next == null ? size : next, size);
				if (off + dt.getLength() > limit && dt instanceof Array) {
					// an array running into the next curated field: cut it there
					Array arr = (Array) dt;
					int n = (limit - off) / arr.getElementLength();
					desc = (desc == null ? "" : desc + "; ") + "curated " + ty + ", cut at the next field";
					dt = n > 1 ? new ArrayDataType(arr.getDataType(), n, arr.getElementLength(), dtm)
							: arr.getDataType();
				}
				if (off + dt.getLength() > size) {
					println("struct " + name + ": field " + fname + " runs past the end, skipped");
					continue;
				}
				prev = s.replaceAtOffset(off, dt, dt.getLength(), fname, desc);
				prevEnd = off + dt.getLength();
			}
			structs.get(name).replaceWith(s);
			nStructs++;
		}
	}

	boolean putData(Address a, DataType dt) {
		try {
			DataUtilities.createData(currentProgram, a, dt, -1, ClearDataMode.CLEAR_ALL_CONFLICT_DATA);
			nTypes++;
			return true;
		}
		catch (CodeUnitInsertionException | RuntimeException ex) {
			nTypeSkipped++;
			println("type " + dt.getName() + " at " + a + " not applied: " + ex.getMessage());
			return false;
		}
	}

	void applyData(JsonObject root, Map<Address, JsonObject> gentries) {
		AddressSet covered = new AddressSet();
		for (String sect : new String[] { "arrays", "ptr_tables" }) {
			for (Map.Entry<String, JsonElement> e : root.getAsJsonObject(sect).entrySet()) {
				JsonObject o = e.getValue().getAsJsonObject();
				Address a = addr(o.get("addr").getAsString());
				Structure s = structs.get(o.get("struct").getAsString());
				int n = o.get("count").getAsInt();
				DataType el = sect.equals("arrays") ? s : dtm.getPointer(s);
				DataType dt = new ArrayDataType(el, n, el.getLength(), dtm);
				if (putData(a, dt))
					covered.add(a, a.add(dt.getLength() - 1));
			}
		}
		// curated global types, by address; one inside a struct array or an earlier global is left as a label
		TreeMap<Address, JsonObject> byAddr = new TreeMap<>(gentries);
		for (Map.Entry<Address, JsonObject> e : byAddr.entrySet()) {
			JsonObject o = e.getValue();
			if (!o.has("type"))
				continue;
			DataType dt = parse(o.get("type").getAsString());
			Address a = e.getKey();
			if (dt == null || covered.contains(a))
				continue;
			AddressRange r = new AddressRangeImpl(a, a.add(dt.getLength() - 1));
			if (covered.intersects(r.getMinAddress(), r.getMaxAddress()))
				continue;
			if (putData(a, dt))
				covered.add(r);
		}
	}

	void removeStale(Map<Address, String> fnames, Map<Address, String> gnames) throws Exception {
		SymbolTable st = currentProgram.getSymbolTable();
		List<Symbol> drop = new ArrayList<>();
		for (Symbol s : st.getAllSymbols(false)) {
			if (s.getSource() != SourceType.USER_DEFINED || s.isExternal())
				continue;
			Address a = s.getAddress();
			if (s.getSymbolType() == SymbolType.FUNCTION) {
				if (!s.getName().equals(fnames.get(a))) {
					Function f = getFunctionAt(a);
					if (f != null && !fnames.containsKey(a)) {
						f.setName(null, SourceType.DEFAULT);
						nRemoved++;
					}
				}
			}
			else if (s.getSymbolType() == SymbolType.LABEL && s.isGlobal()) {
				if (!s.getName().equals(gnames.get(a)) && !s.getName().equals(fnames.get(a)))
					drop.add(s);
			}
		}
		for (Symbol s : drop) {
			s.delete();
			nRemoved++;
		}
	}

	// ---- curated signatures -----------------------------------------------------------

	/** the parameters the decompiler found for one function (callee side), as plain data */
	static class Spec {
		Address entry;
		TreeMap<Integer, Object[]> regs = new TreeMap<>(); // ARGREGS index -> {name, type, storage}
		TreeMap<Integer, Object[]> stack = new TreeMap<>(); // stack offset -> {name, type, size}
		boolean eaxPassthrough; // every return gives back the eax it was called with
	}

	/** some instance of the variable is read (a locked parameter has a variable even when unused) */
	static boolean used(HighVariable hv) {
		if (hv == null)
			return false;
		for (Varnode v : hv.getInstances())
			if (v.getDescendants().hasNext())
				return true;
		return false;
	}

	/** v flows only (through copies) into RETURN ops */
	static boolean onlyReturned(Varnode v, int depth) {
		if (depth > 16)
			return false;
		Iterator<PcodeOp> d = v.getDescendants();
		while (d.hasNext()) {
			PcodeOp op = d.next();
			if (op.getOpcode() == PcodeOp.RETURN)
				continue;
			if (op.getOpcode() != PcodeOp.COPY || !onlyReturned(op.getOutput(), depth + 1))
				return false;
		}
		return true;
	}

	Spec recover(DecompileResults res, Register[] regs) throws Exception {
		Spec sp = new Spec();
		sp.entry = res.getFunction().getEntryPoint();
		HighFunction hf = res.getHighFunction();
		if (hf == null)
			return sp;
		// Watcom keeps every register but eax, so a function that never sets eax "returns"
		// its input eax, and the decompiler then takes eax for an argument and the return
		// value.  Find that case: the return value is the input eax, used for nothing else.
		Address eax = regs[0].getAddress();
		int nret = 0, npass = 0;
		boolean eaxOnlyReturned = true;
		Iterator<PcodeOpAST> ops = hf.getPcodeOps();
		while (ops.hasNext()) {
			PcodeOpAST op = ops.next();
			if (op.getOpcode() != PcodeOp.RETURN)
				continue;
			nret++;
			Varnode v = op.getNumInputs() > 1 ? op.getInput(1) : null;
			while (v != null && v.getDef() != null && v.getDef().getOpcode() == PcodeOp.COPY)
				v = v.getDef().getInput(0);
			if (v != null && v.isInput() && v.isRegister() && v.getAddress().equals(eax)) {
				npass++;
				if (!onlyReturned(v, 0))
					eaxOnlyReturned = false;
			}
		}
		sp.eaxPassthrough = nret > 0 && npass == nret;
		boolean dropEax = sp.eaxPassthrough && eaxOnlyReturned;
		Iterator<HighSymbol> it = hf.getLocalSymbolMap().getSymbols();
		while (it.hasNext()) {
			HighSymbol hs = it.next();
			VariableStorage vs = hs.getStorage();
			String n = hs.getName();
			boolean input = hs.isParameter() || n.startsWith("unaff_") || n.startsWith("in_");
			// Ghidra's ordered parameter list makes eax..ecx parameters as soon as a stack
			// argument is used; keep only the inputs that are used
			if (!input || vs == null || !used(hs.getHighVariable()))
				continue;
			if (vs.isRegisterStorage()) {
				int k = Arrays.asList(regs).indexOf(vs.getRegister().getBaseRegister());
				if (k >= 0 && !(k == 0 && dropEax))
					sp.regs.put(k, new Object[] { "param_" + (k + 1), hs.getDataType(), vs });
			}
			else if (vs.isStackStorage() && vs.getStackOffset() >= 4) {
				sp.stack.put(vs.getStackOffset(),
					new Object[] { String.format("stack_%x", vs.getStackOffset()), hs.getDataType(), hs.getSize() });
			}
		}
		return sp;
	}

	/**
	 * Watcom register convention, function by function: the decompiler's own view of
	 * each function's inputs (eax, edx, ebx, ecx in that order, then the stack), committed
	 * with custom storage so callers pass exactly those.  Ghidra's ordered parameter
	 * lists cannot express "stack arguments without the four registers", and with edx,
	 * ebx and ecx preserved by the callee (as Watcom does) it calls them unaff_ rather
	 * than parameters; this fixes both.  The return value stays unlocked.  Curated
	 * argument types (car* in eax) replace the recovered ones (SourceType.USER_DEFINED);
	 * the rest are SourceType.ANALYSIS.
	 */
	static final Set<String> ST0_USERS = new HashSet<>(Arrays.asList("FSTP", "FST", "FISTP", "FIST", "FCOMP",
		"FCOM", "FCOMPP", "FUCOMP", "FUCOM", "FUCOMPP", "FMUL", "FMULP", "FADD", "FADDP", "FSUB", "FSUBP", "FSUBR",
		"FSUBRP", "FDIV", "FDIVP", "FDIVR", "FDIVRP", "FCHS", "FABS", "FXCH", "FTST", "FSQRT", "FRNDINT", "FIMUL",
		"FIADD", "FISUB", "FIDIV", "FICOMP", "FICOM"));

	/** most call sites use st0 at once (fstp, fmul...): the function returns a float in st0 */
	boolean callersTakeSt0(Function f) {
		int n = 0, st0 = 0;
		for (Reference r : getReferencesTo(f.getEntryPoint())) {
			if (!r.getReferenceType().isCall())
				continue;
			Instruction call = getInstructionAt(r.getFromAddress());
			if (call == null)
				continue;
			n++;
			// the first x87 instruction after the call (a few integer moves may come first)
			Instruction next = call.getNext();
			for (int i = 0; i < 6 && next != null; i++, next = next.getNext()) {
				String m = next.getMnemonicString().toUpperCase();
				if (m.startsWith("F")) {
					if (ST0_USERS.contains(m))
						st0++;
					break;
				}
				if (next.getFlowType().isJump() || next.getFlowType().isCall() || next.getFlowType().isTerminal())
					break;
			}
		}
		return n > 0 && st0 * 2 > n;
	}

	void applySignatures(List<Function> todo, Map<Address, JsonObject> curated) throws Exception {
		Register[] regs = new Register[ARGREGS.length];
		for (int i = 0; i < ARGREGS.length; i++)
			regs[i] = currentProgram.getRegister(ARGREGS[i]);
		long t0 = System.currentTimeMillis();
		DecompileConfigurer conf = d -> {
			DecompileOptions o = new DecompileOptions();
			o.grabFromProgram(currentProgram);
			d.setOptions(o);
			d.toggleCCode(false);
			d.toggleSyntaxTree(true);
			d.setSimplificationStyle("decompile");
		};
		DecompilerCallback<Spec> cb = new DecompilerCallback<Spec>(currentProgram, conf) {
			@Override
			public Spec process(DecompileResults r, TaskMonitor m) throws Exception {
				return recover(r, regs);
			}
		};
		cb.setTimeout(120);
		FunctionManager fm = currentProgram.getFunctionManager();
		Set<Address> retLocked = new HashSet<>();
		Collection<Function> work = todo;
		// round 1: every function; later rounds: the callers of functions whose arguments
		// changed (a caller passing its own edx on is only seen once the callee takes edx,
		// and in round 1 a callee with stack arguments seems to take all four registers)
		for (int round = 1; round <= 8 && !work.isEmpty(); round++) {
			List<Spec> specs;
			specs = ParallelDecompiler.decompileFunctions(cb, work, monitor);
			Set<Function> changed = new HashSet<>();
			for (Spec sp : specs) {
				if (sp == null)
					continue;
				Function f = fm.getFunctionAt(sp.entry);
				if (round > 1 && !differs(f, sp, curated.get(sp.entry), regs))
					continue;
				if (commit(f, sp, curated.get(sp.entry), regs, round == 1, retLocked))
					changed.add(f);
			}
			Set<Function> next = new HashSet<>();
			for (Function f : changed)
				for (Function c : f.getCallingFunctions(monitor))
					if (!c.isThunk() && !c.isExternal() && c.getSignatureSource() != SourceType.IMPORTED)
						next.add(c);
			println(String.format("nfsre apply: round %d: %d functions decompiled, %d signatures set (%.1f s)",
				round, work.size(), changed.size(), (System.currentTimeMillis() - t0) / 1000.0));
			work = next;
		}
		cb.dispose();
		println(String.format("nfsre apply: %d functions without an eax result", retLocked.size()));
	}

	/** the decompiler's arguments now differ from the committed ones (more, or fewer: an
	 *  argument only passed on to a callee that turned out not to take it) */
	boolean differs(Function f, Spec sp, JsonObject cur, Register[] regs) {
		Set<Integer> have = new HashSet<>(), haveStack = new HashSet<>();
		for (Parameter p : f.getParameters()) {
			VariableStorage vs = p.getVariableStorage();
			if (vs.isRegisterStorage())
				have.add(Arrays.asList(regs).indexOf(vs.getRegister().getBaseRegister()));
			else if (vs.isStackStorage())
				haveStack.add(vs.getStackOffset());
		}
		int top = sp.regs.isEmpty() ? -1 : sp.regs.lastKey();
		if (cur != null)
			for (String r : cur.keySet())
				top = Math.max(top, Arrays.asList(ARGREGS).indexOf(r.toUpperCase()));
		Set<Integer> want = new HashSet<>();
		for (int k = 0; k <= top; k++)
			want.add(k);
		return !have.equals(want) || !haveStack.equals(sp.stack.keySet());
	}

	boolean commit(Function f, Spec sp, JsonObject cur, Register[] regs, boolean first, Set<Address> retLocked)
			throws Exception {
		DataType u4 = Undefined4DataType.dataType;
		TreeMap<Integer, Object[]> byReg = new TreeMap<>(sp.regs);
		if (cur != null) {
			for (Map.Entry<String, JsonElement> a : cur.entrySet()) {
				int k = Arrays.asList(ARGREGS).indexOf(a.getKey().toUpperCase());
				Structure s = structs.get(a.getValue().getAsString());
				if (k >= 0 && s != null)
					byReg.put(k, new Object[] { ident(a.getValue().getAsString()), dtm.getPointer(s),
						new VariableStorage(currentProgram, regs[k]) });
			}
		}
		Parameter ret = new ReturnParameterImpl(DataType.DEFAULT, VariableStorage.UNASSIGNED_STORAGE, currentProgram);
		if (retLocked.contains(f.getEntryPoint()))
			ret = f.getReturn();
		else if (first && sp.eaxPassthrough) {
			// no eax result: an x87 one when the callers use st0 straight after the call, else void
			try {
				boolean st0 = callersTakeSt0(f);
				ret = new ReturnParameterImpl(st0 ? Float10DataType.dataType : VoidDataType.dataType,
					st0 ? new VariableStorage(currentProgram, currentProgram.getRegister("ST0"))
							: VariableStorage.VOID_STORAGE,
					currentProgram);
				retLocked.add(f.getEntryPoint());
			}
			catch (Exception ex) {
				println("return of " + f.getName() + " not set: " + ex);
			}
		}
		else if (byReg.isEmpty() && sp.stack.isEmpty())
			return false;
		try {
			List<Parameter> ps = new ArrayList<>();
			int top = byReg.isEmpty() ? -1 : byReg.lastKey();
			for (int k = 0; k <= top; k++) {
				Object[] v = byReg.get(k); // an argument register below a used one is an argument too
				if (v == null)
					v = new Object[] { "param_" + (k + 1), u4, new VariableStorage(currentProgram, regs[k]) };
				ps.add(new ParameterImpl((String) v[0], (DataType) v[1], (VariableStorage) v[2], currentProgram));
			}
			int end = 4;
			for (Map.Entry<Integer, Object[]> e : sp.stack.entrySet()) {
				int off = e.getKey(), size = (Integer) e.getValue()[2];
				if (off < end || size <= 0)
					continue; // overlapping pieces: keep the first
				DataType dt = (DataType) e.getValue()[1];
				if (dt.getLength() != size)
					dt = Undefined.getUndefinedDataType(size);
				ps.add(new ParameterImpl((String) e.getValue()[0], dt,
					new VariableStorage(currentProgram, off, size), currentProgram));
				end = off + size;
			}
			f.updateFunction(CC, ret, ps, FunctionUpdateType.CUSTOM_STORAGE, true,
				cur != null ? SourceType.USER_DEFINED : SourceType.ANALYSIS);
			nSigs++;
			return true;
		}
		catch (Exception ex) {
			println("signature of " + f.getName() + " not set: " + ex);
			return false;
		}
	}
}
