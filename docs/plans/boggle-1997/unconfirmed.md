# Boggle: outstanding evidence and limits

Companion to [inspection.md](inspection.md). These items are not verified
by the static inspection and must not become undocumented assumptions in code.

- Exact retail edition, patch level and original CD pressing. A 1997 PE
  timestamp and December 2000 copied-file timestamps are not release proof.
- The owner can run the original in a Windows VM and has confirmed Classic
  Original's timer, minimum length and scoring. Broader input, focus/pause,
  graphics, timing and network observations remain needed. See
  [Classic evidence](../../engines/boggle/classic.md).
- `cd/data.z` contents and SHA-256. It exceeds the executor's 32 MiB transfer
  limit. No compressed installation archive was extracted or installer run.
- Full registry/CD lookup contract, marker semantics and behavior without
  the original CD. Strings such as `SourceDirectory` are research leads.
- Multilingual normalization and Spanish legacy-byte lookup aliases; AI and
  multilingual user dictionary behavior. English deferred review and native
  persistence now exist:
  [evidence and limits](../../engines/boggle/user-dictionary.md).
  Binary `.dic` structure and English membership
  are now validated: [dictionary specification](../../engines/boggle/formats/dictionary.md).
- INSO `.DAT` layout, definition rendering and DLL ordinal contracts.
- Exact purpose of duplicate or unused-looking dictionary files. Determine
  the files the selected language actually opens before pruning an import.
- Original generator seed and shuffle equivalence. The prototype uses an
  independent deterministic generator; Classic adjacency, no cube reuse and
  complete multi-letter faces are confirmed in executable analysis.
- The five modes' board sizes, rules, scoring, timers, difficulty, player
  counts, camera behavior, animations and AI behavior.
- Save/high-score persistence, original user-dictionary editing/settings,
  multilingual behavior and original import/editor interoperability. Deferred
  scoring and path eligibility are corroborated by the owner in the VM.
  Native English user words persist separately from the original installation.
- Network wire protocol, DirectPlay/Third-i session model, host authority,
  timeouts, discovery, synchronization and interoperability.
- Several bitmap paths embedded in the EXE, including `Bitmaps/Title1.bmp`,
  `Bitmaps/Title3.bmp` and `Bitmaps/GS1CS.bmp`, are absent from both supplied
  expanded trees. Determine whether they are unused paths, require other
  packaging/extraction, or reflect incomplete resources before promising
  original menu/intro behavior. Present-file reader checks do not resolve this.
- Exact UI layout and appearance across resource DLLs, fonts, palette modes
  and display settings. Successful media decoding is not a rendering match.
- A proven minimum runtime file set. The inspection proposes a conservative
  resource set; only dependency tracing and end-to-end runs can establish
  sufficiency. Detection and initial resource readers now exist; see the
  [engine README](../../engines/boggle/README.md). An early Classic Original slice is playable; importing remains unavailable.

Research evidence must cite a manifest SHA-256 and file offset/RVA or a
repeatable observation. Keep retail assets, dictionary entries, screenshots,
decompiler output and raw logs outside Git. Record confirmed results in the
inspection/specifications and remove an uncertainty only when evidence resolves it.
