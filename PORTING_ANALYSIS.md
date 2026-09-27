# Porting analysis

## Upstream generation model

Upstream Crunch 3.6 constructs an initial word and advances it as an odometer. In normal mode the rightmost character advances fastest, overflow carries left, and complete shorter lengths are emitted before the next length. Its default sets are lowercase `abcdefghijklmnopqrstuvwxyz`, uppercase `ABCDEFGHIJKLMNOPQRSTUVWXYZ`, numeric `0123456789`, and symbols `!@#$%^&*()-_+=~` plus the remaining punctuation and a trailing space.

Pattern mode assigns a set to each active marker: `@` uses the first/configured set, `,` uppercase, `%` numeric, and `^` symbols. Other characters are fixed. Upstream `-l` takes a string equal in length to the pattern; a marker is literal when the character at the same literal-string position equals that marker. Fixed pattern text naturally creates prefixes, suffixes, and text between generated positions.

Upstream does not remove repeated characters from a supplied charset. Repeated charset entries therefore produce repeated output unless a separate duplicate-limiting mode is selected. Crunch-FZ preserves this behavior.

## Flipper adaptation

`crunch_core.c` ports the compatible ASCII generation loop into a platform-neutral, allocation-free core. It retains the original set order and odometer order while replacing process globals, `wchar_t`, standard output, and desktop file handling with an explicit configuration, preflight plan, and streaming callback.

The Flipper worker sends one completed line at a time directly to the official storage API. It never retains the complete wordlist. A mutex protects the measured line/byte counters read by the GUI. Back sets a cancellation flag; the core checks it before every line, and the worker closes the output before showing its completion state.

Preflight uses checked unsigned 64-bit multiplication and addition for every set product, line sum, and newline-inclusive byte total. A configuration is rejected if either exact value cannot be represented. Generation never starts from a wrapped or approximate count.

## Resource limits

- Word and pattern length: 1 through 32 printable ASCII bytes.
- Custom charset: 1 through 80 printable ASCII bytes.
- Output directory: absolute `/ext` path, up to 127 bytes, without traversal.
- Output filename: up to 63 printable bytes, excluding FAT-invalid separators and metacharacters.
- RAM: fixed configuration, plan, one 33-byte output line, GUI state, and storage objects.
- Results are newline-delimited ASCII and include one byte per newline in preflight/output counters.

Unicode generation, compression, permutation input, file splitting, inversion, start/end blocks, session resume, and duplicate-run suppression depend on substantially different upstream paths or additional UI/state. They are mapped explicitly in [FEATURE_MATRIX.md](FEATURE_MATRIX.md) and are not silently imitated.

## External architecture

The optional CWF1 UART mode runs the genuine upstream `crunch` executable on a Raspberry Pi or other Linux computer. The Flipper hex-encodes only its bounded printable configuration fields. The companion decodes and revalidates them, maps presets to exact character sets, constructs a fixed `subprocess.Popen` argument list without a shell, and restricts output to `/var/lib/crunch-fz/output`.

The companion incrementally reads only newly appended output bytes to count newline-delimited entries and reports the real file size, elapsed monotonic time, process state, exit code, collision-safe filename, and detected Crunch version. Back/Stop terminates the process, escalates to kill only after a bounded wait, measures the remaining output, and leaves the partial file closed on Linux.

This external path provides real upstream execution without pretending that desktop-only functionality was compiled into the Flipper. Advanced upstream options not represented by the bounded Flipper configuration remain available directly through Crunch's CLI on the companion computer.
