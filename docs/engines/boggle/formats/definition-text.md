# Definition article text

This independently authored decoder consumes the entry reader's bounded
markup, not HTML, RTF or executable grammar programs. Its input remains outside
Git. `DefinitionText` emits UTF-8 spans with bold/italic and raised/lowered
style bits and optional literal/root references. A plain string can be derived
without running a renderer. The compatibility text used by earlier metadata
checks remains separate.

## Characters and fields

Printable legacy bytes are Latin-1; byte FF and line endings separate text.
Named backslash/slash escapes describe glyphs. The native mapping covers
Latin vowels/ligatures with macrons, breves and other combining diacritics,
Greek letters, stress primes, schwa/open-o, phonetic underbars, fractions,
mathematical signs, currency, punctuation and music accidentals. Compound
recipes preserve their component marks rather than transliterating them to
ASCII names. Below-breve/below-macron escapes attach to the preceding letter;
they do not insert a new vowel. Primary stress uses a bold prime; secondary
stress uses the light prime. Unknown names remain explicit and cause the
full-archive validation tool to fail, rather than disappearing silently.

WD/IWD and run-on headings are bold. POS, grammatical/language labels and
examples are italic. Balanced EM tags control inline emphasis; EMON applies
to the following field. UP/UB raise/lower text and end on their closing tag or
next structural field. Pronunciation gets parentheses, DS/SDS supplies sense
labels, and entry/part-of-speech/etymology/usage boundaries provide paragraphs.
No embedded scripts, formatting instructions or external URLs are executed.

KW/HWREF/XREF/DRV targets retain literal spelling and IEKW selects the separate
root-name catalog. Reference labels decode the same symbol recipes and omit
syllable dots for lookup. KW can also label etymological fragments or aliases without an article. The
entry layer preserves all their printed text, and only valid local targets
remain interactive. KW can also label etymological fragments or aliases without an article. The
entry layer preserves all their printed text, and only valid local targets
remain interactive. The viewer follows only the already validated local
dictionary, retains at most 32 pages and offers recoverable no-hit/error views.
The native layout is intended to make every supported article readable; it
does not reproduce Win95 line breaking or a pixel-identical dictionary window.

## Evidence, limits and checks

The complete supported archive has 94,693 articles. Every article decodes with
zero unsupported symbol names. Runtime text is capped at 1 MiB per article,
32,768 spans per decoded article and 262,144 displayed codepoints; viewer
lookups retain the existing 64-article limit. The shared font reader caps font
size, tables, glyphs, contour recursion, flattened edges and raster storage;
font bytecode is bounded and skipped.

The original definition static initializes six Times New Roman styles around
0x4316b0–0x43188b. Native formatting is independently implemented. The publisher's
current [pronunciation key](https://www.ahdictionary.com/word/pronkey.html) and
[Semitic transliteration reference](https://ahdictionary.com/word/semitic.html)
provide cross-checks for character meanings; no definitions, fonts or code from
those sources are bundled. The original output and glyph contexts were also
inspected in isolated read-only research.

Invented fixtures cover Unicode encoding/decoding, accents, compound escapes,
style scope, pronunciation, sense labels, links, malformed tags/escapes, limits,
font cmap 4/12, composite glyphs, cycles, fallback, clipping and navigation.
Game-data records contain only checksums/counts, never article text or images.
