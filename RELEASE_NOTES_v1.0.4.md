# Crunch FZ v1.0.4

Crunch FZ ports genuine Crunch wordlist-generation order and pattern concepts to Flipper Zero. It generates real newline-delimited output directly to microSD and optionally controls the genuine Crunch executable on Linux.

## Native Flipper generation

- Built-in Numeric, Lowercase, Uppercase, Mixed case, Alphanumeric, and Symbols character sets.
- Custom character sets with preserved character order.
- Minimum and maximum output lengths from 1 through 32.
- Genuine range ordering; for custom `ab`, lengths 1–2 produce `a`, `b`, `aa`, `ab`, `ba`, `bb`.
- Crunch-style pattern markers: `@` selected/custom set, `,` uppercase, `%` digits, and `^` symbols.
- Fixed text in patterns for prefixes and suffixes.
- Literal mask support when a marker character must be output literally.
- Overflow-safe preflight calculation of combinations, expected lines, newline-inclusive bytes, and current microSD free space.
- Explicit rejection when counts overflow or output cannot fit.
- Live measured generated count, percentage, elapsed time, entries per second, and output bytes; no fabricated ETA.
- Direct bounded streaming through a 1 KiB buffer; the complete wordlist is never stored in RAM.
- Separate overwrite confirmation for an existing output.
- Transactional `.partial` and `.backup` handling so cancellation, disk-full, SD removal, short write, or failed synchronization preserves previous valid output.

## Genuine Crunch on Raspberry Pi/Linux

External mode runs the real `crunch` executable on a Raspberry Pi, Linux laptop, desktop, mini PC, or VM. The Flipper sends the validated charset, length, optional pattern/literal mask, and filename through bounded 3.3 V UART and displays actual lines, bytes, elapsed time, output name, and genuine Crunch version.

External output remains under `/var/lib/crunch-fz/output`. Existing files are never silently overwritten; the bridge selects `name-1`, `name-2`, and so on. A fixed argument vector is used with no shell execution. Advanced upstream options remain available directly on Linux.

Native mode intentionally does not claim permutation mode, start/end blocks, inverted output, duplicate-run suppression, split/compressed output, Unicode sets, session resume, or stdout pipelines. Crunch FZ is a generator and does not use NFC, RFID, Sub-GHz, infrared, or Wi-Fi hardware.

## Quick start

1. Open **Charset and lengths** and select a built-in or custom set.
2. Choose minimum and maximum lengths, or set equal lengths and configure a pattern.
3. Set the output filename and slash-free folder name under `/ext`.
4. Open **Preflight / Generate** and review exact lines, bytes, path, and free space.
5. Press **Generate** and confirm overwrite separately if required. Back requests safe cancellation.
6. For genuine external Crunch, follow `EXTERNAL_CRUNCH.md`, match UART baud, open **External Crunch**, and press OK to run/cancel.

## Install and verification

Requires official Flipper firmware 1.4.3 or later and a microSD card. Copy `crunch_fz.fap` to `/ext/apps/Tools/` and open **Apps → Tools → Crunch FZ**.

- Native ordering/calculation/protocol regression tests passed.
- External companion tests: 7 passed.
- uFBT APPCHK: target f7/API 87.1, no unresolved symbols.
- FAP size: 30,460 bytes.
- SHA-256: `6A14F814523F3D77C85645E7DCF44F1BF8E576C0EA8EC9AEA78A19C237A9076F`

Use generated wordlists only for systems and credentials you own or are authorized to test. Crunch FZ preserves upstream Crunch's GNU GPL v2-only license.
