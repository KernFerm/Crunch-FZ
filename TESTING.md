# Testing

## Host-native core suite

Run:

```powershell
python tests/run_tests.py
```

The suite compiles `crunch_core.c` and `crunch_external_protocol.c` as C11 with MSVC `/W4 /WX` on Windows or `-Wall -Wextra -Werror` elsewhere. It validates:

- the upstream `crunch 1 2 ab` order: `a`, `b`, `aa`, `ab`, `ba`, `bb`;
- a one-character set across multiple lengths;
- rightmost-fastest `@%x` pattern ordering;
- a literal `^` marker through the same-length literal mask;
- fixed pattern prefixes;
- exact line and newline-inclusive byte totals;
- preservation of repeated custom-set entries;
- empty set, pattern mismatch, literal-mask mismatch, and 64-bit overflow rejection;
- cancellation after a measured number of writes;
- a simulated storage short-write failure.
- exact CWF1 INFO/STATUS parsing, fragmented delivery, malformed fields, and integer overflow.

The expected vectors are taken directly from the ordering and pattern behavior in upstream Crunch 3.6 `loadstring`, `increment`, `chunk`, and the documented command examples at revision `3bdc4a8941eb64e5c15086778df56af79d54e06b`.

Result on 2026-09-26: all Crunch-FZ core tests passed.

`python tests/test_companion.py` validates bounded printable hex fields, fixed genuine-Crunch argument construction, collision-safe output naming, and measured line/byte counts from a real output file. Result on 2026-09-26: 4/4 passed. The companion also passes `py_compile`.

## Firmware build

```powershell
ufbt -c
ufbt
```

Clean build result on 2026-09-26: APPCHK passed for target f7/API 87.1 with no unresolved symbols.

Current v1.0.0 external-capable artifact: 29,364 bytes; SHA-256 `0A12D9FED08725B378C98567CE5590E5EC37F6EA26CF3AA21AA94C6C4EE10D76`.

## Physical-device acceptance

Record these observations from the attached Flipper before calling the release hardware-validated:

1. Open the FAP and confirm Settings and About navigation.
2. Select Custom, enter `ab`, set minimum 1 and maximum 2, preflight 6 lines/16 bytes, and generate.
3. Read `/ext/crunch_fz/wordlist.txt` and verify `a`, `b`, `aa`, `ab`, `ba`, `bb` in exact order.
4. Set a three-character pattern and verify marker ordering and literal-mask behavior.
5. Confirm an existing file requires a separate overwrite action.
6. Cancel a larger generation and confirm partial measured totals and a closed readable file.
7. Remove or fill the SD card and confirm a closed storage error without a crash.
8. Compare displayed elapsed time, measured rate, lines, and bytes with the resulting file.
9. With Raspberry Pi/Linux hardware, verify CWF1 handshake, the genuine Crunch version, configuration transfer, generation/cancellation, measured counts, collision naming, disconnect recovery, and UART/expansion release.

No physical result is recorded until it is observed. External hardware behavior remains unvalidated until a Pi/Linux UART setup is available.

## Static/security checks

The repository has no package dependency manifest. Authenticated Snyk Code analysis restricted to Crunch-FZ on 2026-09-26 completed with zero low-or-higher findings; no finding was ignored or suppressed. Native arithmetic, path validation, storage return values, and worker lifecycle were also reviewed directly.
