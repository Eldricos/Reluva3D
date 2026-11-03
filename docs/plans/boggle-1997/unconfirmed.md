# Boggle: outstanding evidence and limits

Companion to [inspection.md](inspection.md). These items are not verified
by the static inspection and must not become undocumented assumptions in code.

- Exact retail edition, patch level and original CD pressing. A 1997 PE
  timestamp and December 2000 copied-file timestamps are not release proof.
- Original-game execution. No suitable Windows reference environment or
  Wine was available; no original program was executed. Collect About-screen,
  mode/options, graphics, input, timing, scoring and network observations
  before making behavioral compatibility claims.
- `cd/data.z` contents and SHA-256. It exceeds the executor's 32 MiB transfer
  limit. No compressed installation archive was extracted or installer run.
- Full registry/CD lookup contract, marker semantics and behavior without
  the original CD. Strings such as `SourceDirectory` are research leads.
- Nonempty `.dic` encoding, indexes, normalization, encodings/accents,
  minimum-word rules, word membership and AI dictionary behavior.
- INSO `.DAT` layout, definition rendering and DLL ordinal contracts.
- Exact purpose of duplicate or unused-looking dictionary files. Determine
  the files the selected language actually opens before pruning an import.
- Dice-set use and randomization: the `.dst` row/field counts are observed;
  face order, generator, seed, shuffle and multi-letter behavior are not.
- The five modes' board sizes, rules, scoring, timers, difficulty, player
  counts, camera behavior, animations and AI behavior.
- Save/high-score/user-dictionary persistence and writable-path behavior.
- Network wire protocol, DirectPlay/Third-i session model, host authority,
  timeouts, discovery, synchronization and interoperability.
- Exact UI layout and appearance across resource DLLs, fonts, palette modes
  and display settings. Successful media decoding is not a rendering match.
- A proven minimum runtime file set. The inspection proposes a conservative
  resource set; only dependency tracing and end-to-end runs can establish
  sufficiency. No Boggle engine, recognition table or importer exists yet.

Research evidence must cite a manifest SHA-256 and file offset/RVA or a
repeatable observation. Keep retail assets, dictionary entries, screenshots,
decompiler output and raw logs outside Git. Record confirmed results in the
inspection/specifications and remove an uncertainty only when evidence resolves it.
