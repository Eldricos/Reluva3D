# Browsing submitted words

Classic's installed-artwork word panel now has active up/down buttons and a
visible row range when the list exceeds one view. Up shows one older row;
down shows one newer row. Disabled arrows are dimmed at the beginning/end.
The artwork view reserves the lower panel for those controls and displays
23 word rows. Long words remain fitted to the word column; points stay in the
separate column. Short lists retain the existing presentation.

Page Up / Page Down moves one visible view: 23 rows with artwork, 14 in the
prototype view. The mouse wheel moves three rows per notch while the pointer
is over the word panel, in either view. Wheel motion over the board or other
controls does not browse the list. The wheel uses the most recent mouse
position; move the pointer into the panel before scrolling.

Browsing preserves unfinished text and selected cubes, and does not alter
accepted words, provisional points or the timer. Submitting a word returns to
the newest entries, as before. The range excludes words declined during review.
Review remains modal: history input cannot dismiss or answer its prompt.
Starting/replaying a round clears the previous history view with the words.
Mouse presses on the arrows are consumed rather than beginning cube selection.

These are native controls with measured original direction/step behavior, not
a claim of matching the original font-dependent visible row count or keyboard
and wheel behavior. The row-range label, page keys and wheel support are native
conveniences. No new resources, import changes or launcher settings are needed.

## Evidence

The source executable SHA-256 is
`5768d2fbdaf5435deb2cc2e076f1756736f0178532195f73b2e19ee114395639`;
its identity and English language DLL are in the
[install manifest](../../plans/boggle-1997/install-manifest.tsv).
English dialogs 260 and 360 contain previous/next controls 1336 and 1335.
The executable's message-map entries at file offsets `0x78538` and `0x78520`
associate them with handlers `0x45b1b0` and `0x45b160`. Those handlers move the
list position backward/forward by one and constrain it to the available range.
Their shared visible-capacity helper is at `0x45b8a0`.

Only those behavior facts informed the implementation. Native code stores a
row distance from the newest view, recomputes ranges from the current filtered
word count and performs bounded arithmetic. It is independently written;
no original instruction sequence or translated implementation is included.

## Validation

`boggle.unit.history` covers empty/exact/overflowing views, changed word counts,
button edges and extreme scroll values. Synthetic 60-word rounds use invented
dice and vocabulary generated from their paths in both variants and both
renderers. They verify equivalent views after opposite controls, wheel bounds,
preserved unfinished input, ignored wheel events outside the panel, modal
review, score restoration after a declined word and cleared history on replay.
Records contain only checksums of those invented fixtures.

The supplied install passes all 24 Boggle CTest checks. The normal suite passes
35 checks and skips 22 unconfigured optional data checks. The history/layout
checks also pass ASan/UBSan with LeakSanitizer disabled for the execution tracer.
The owner confirmed the controls work on Windows; exact original-VM appearance
and key/wheel comparison remain open.
