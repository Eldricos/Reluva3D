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
- The separate title-sequence paths `Bitmaps/Title1.bmp`, `Bitmaps/Title3.bmp`,
  `Bitmaps/hasbro1.bmp` and `Bitmaps/hasbro3.bmp` are absent from both supplied
  expanded trees. `GS1CS.bmp` was incorrectly listed as absent earlier; it is
  present in both manifests and copies. See [frontend evidence](../../engines/boggle/frontend.md). Determine whether they are unused paths, require other
  packaging/extraction, or reflect incomplete resources before promising
  original menu/intro behavior. Present-file reader checks do not resolve this.
- Classic roll/lid and WAV playback now exist; the owner has corroborated startup,
  selection, writing, final-countdown ticking and expiry ringing in the VM.
  A subsequent VM check confirms distinct seven- and eight-letter cues replacing
  Writing; native Windows music/effects playback also passed the owner's test.
  Precise sequence timing still needs comparison. Full original overlays,
  full original frontend dialogs/animation, paletted/timer AVI and Cinepak remain
  unsupported. Native selection and Classic setup now exist with installed
  selection artwork, but their full original flow still needs VM comparison. See
  [presentation evidence](../../engines/boggle/presentation.md).
- Exact UI layout and appearance across resource DLLs, fonts, palette modes
  and display settings. Successful media decoding is not a rendering match.
- A proven minimum runtime file set. The inspection proposes a conservative
  resource set; only dependency tracing and end-to-end runs can establish
  sufficiency for all original features. A tested Classic import set now covers
  the native English Classic Original/Master slice and existing detection markers
  (40 files with menu artwork, or the earlier 39-file subset);
  the owner also confirmed that CD importing works. See
  [import scope](../../engines/boggle/import.md). It is not a full-game minimum.

Research evidence must cite a manifest SHA-256 and file offset/RVA or a
repeatable observation. Keep retail assets, dictionary entries, screenshots,
decompiler output and raw logs outside Git. Record confirmed results in the
inspection/specifications and remove an uncertainty only when evidence resolves it.

- WAV background music now supports the fifteen installed tracks and an independent
  launcher selector. Original CD-device music remains unsupported. Track timing
  and continuity still need VM listening checks; see [music evidence](../../engines/boggle/music.md).
