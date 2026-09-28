# Changelog

## 1.0.4 — 2026-09-27

- Refreshed application and companion version metadata.
- Rebuilt and revalidated the target f7/API 87.1 FAP.

## 1.0.1 — 2026-09-27

- Made native output transactional with `.partial` and recovery `.backup` files.
- Preserved existing wordlists across cancellation, disk-full, write, sync, and promotion failures.
- Added microSD free-space validation to preflight.
- Added a bounded 1 KiB native write buffer.
- Replaced the absolute-path editor with a slash-free folder name beneath `/ext`.
- Initialized UART before starting the external receive worker.
- Made companion filesystem failures bounded protocol errors and surfaced genuine Crunch diagnostics.
- Added direct upstream differential tests using an installed genuine Crunch executable.

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
