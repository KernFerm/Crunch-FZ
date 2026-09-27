# Changelog

## 1.0.0 — 2026-09-26

- Ported genuine Crunch 3.6 character ordering and odometer generation.
- Added numeric, lowercase, uppercase, mixed-case, alphanumeric, symbol, and custom sets.
- Added `@`, `,`, `%`, and `^` patterns, fixed prefix/suffix text, and literal masks.
- Added checked exact line/output-byte preflight calculations.
- Added direct microSD streaming, overwrite confirmation, cancellation, and storage errors.
- Added real line, byte, percentage, elapsed-time, and measured-throughput progress.
- Added host tests for ordering, calculations, overflow, cancellation, and write failure.
- Added on-device Settings, About/version, preflight, progress, and completion pages.
- Added optional Raspberry Pi/Linux execution through the bounded CWF1 UART protocol.
- Added a Linux companion that launches genuine Crunch without a shell and reports measured output.
- Added external version/state, line, byte, elapsed-time, filename, cancel, and resource-lifecycle handling.
