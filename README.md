# Crunch FZ

Crunch FZ generates real newline-delimited wordlists and streams them directly to a Flipper Zero microSD card. Version 1.0.0 includes an external Linux/Raspberry Pi mode: genuine upstream Crunch runs on the Linux computer, while the Flipper is its 3.3 V UART controller and measured status display. End users can choose native Flipper generation or genuine external Crunch generation.

Current release: **v1.0.0**.

## Install the FAP

You need a Flipper Zero with a working microSD card. The supplied release build targets official firmware 1.4.3, target f7, API 87.1. An incompatible firmware API may require rebuilding the app.

### Install with qFlipper

1. Download `crunch_fz.fap` from the latest GitHub release or this repository's `dist` folder.
2. Connect the Flipper Zero by USB and open qFlipper.
3. Open the microSD card file browser.
4. Open `apps`, then `Tools`.
5. Copy `crunch_fz.fap` into `/ext/apps/Tools/`.
6. Safely disconnect the device.
7. On the Flipper, open **Apps → Tools → Crunch FZ**.

The filename ends in `.fap` (Flipper Application Package), not `.fab`.

### Install directly from a microSD card

Put the microSD card in a computer, copy `crunch_fz.fap` to `apps/Tools/`, safely eject it, and return it to the Flipper. The app then appears under **Apps → Tools**.

## Use the application

### Generate a length range

1. Open **Charset and lengths**.
2. Select Numeric, Lowercase, Uppercase, Mixed case, Alphanumeric, Symbols, or Custom.
3. Select minimum and maximum lengths from 1 through 32.
4. If using Custom, open **Edit custom set** and enter the characters in the exact order you want generated.
5. Use **Clear pattern** to ensure range mode is active.
6. Set **Output filename** and an absolute **Output directory** under `/ext`.
7. Open **Preflight / Generate** and review the exact lines, output bytes, mode, and path.
8. Press **Generate**. If the file already exists, confirm **Overwrite** separately.

For custom set `ab` and lengths 1 through 2, genuine ordering is:

```text
a
b
aa
ab
ba
bb
```

### Generate from a pattern

Set minimum and maximum length equal to the pattern length, then open **Edit pattern**. Pattern markers follow Crunch:

- `@` uses the selected charset (`@` therefore supports a custom first set).
- `,` uses uppercase `A-Z`.
- `%` uses digits `0-9`.
- `^` uses the upstream symbol set.
- Every other character is fixed text.

Fixed text creates prefixes and suffixes. For example, `pre@@` with custom set `ab` produces `preaa`, `preab`, `preba`, and `prebb`.

To output a marker literally, enter a **literal mask** with exactly the same length as the pattern. Put the same marker at positions that must be literal; other mask characters are ignored. Use **Clear literal mask** to reactivate every marker.

### Preflight and progress

Preflight calculates the exact combination count, expected line count, and newline-inclusive output byte count with overflow-safe 64-bit arithmetic. If the exact result cannot be represented, generation is rejected instead of wrapping.

During generation the screen reports actual generated lines, total lines, percentage, elapsed time, measured entries per second, and bytes successfully written. No ETA or speed is fabricated. Press **Back** to request cancellation; the current write finishes, the file closes, and the completion page reports real partial totals.

The default output is `/ext/crunch_fz/wordlist.txt`. A short write, failed final synchronization, missing SD card, or full card produces an explicit storage error. Partial output is never labeled complete.

### External Crunch on Raspberry Pi/Linux

This mode uses the real `crunch` executable on the companion computer; it does not imitate generation or fabricate progress. Output stays on the Pi/Linux computer under `/var/lib/crunch-fz/output`; the Flipper sends the validated charset/length/pattern configuration, starts or cancels the process, and displays actual lines, bytes, elapsed time, output name, and Crunch version.

1. Install the companion by following [EXTERNAL_CRUNCH.md](EXTERNAL_CRUNCH.md).
2. Connect crossed 3.3 V UART TX/RX and a common ground. Never connect a 5 V UART signal.
3. Configure the same charset, lengths, optional pattern/literal mask, and output filename used by native mode.
4. In **Charset and lengths**, select the companion's UART baud.
5. Open **External Crunch** and wait for the genuine Crunch version and `IDLE` state.
6. Press OK to run genuine Crunch. Press OK again to cancel it. Back cancels any running process, closes UART, and restores the Flipper expansion service.

If a requested Pi output filename already exists, the bridge selects `name-1`, `name-2`, and so on; it never silently overwrites a Pi wordlist. The bridge uses a fixed argument vector and never executes UART text through a shell. All other upstream Crunch command-line options remain available directly on the Pi/Linux computer.

## What the app does not do

The native Flipper mode does not implement upstream permutation modes, start/end blocks, inverted output, duplicate-run suppression, split output, compression, Unicode sets, session resume, or stdout pipelines. These are absent rather than simulated. External mode uses the genuine executable for the configuration sent by the Flipper; advanced CLI options are run directly on the companion computer. See [FEATURE_MATRIX.md](FEATURE_MATRIX.md) for the complete option mapping.

Crunch is a generator. Crunch-FZ does not use NFC, RFID, Sub-GHz, infrared, or Wi-Fi hardware.

Use generated wordlists only for systems, files, and credentials that you own or are authorized to test. See [PORTING_ANALYSIS.md](PORTING_ANALYSIS.md), [SECURITY.md](SECURITY.md), and [TESTING.md](TESTING.md) for implementation boundaries and validation evidence.

## Build from source

Requirements: Python 3, `ufbt` 0.2.6 or newer, and official firmware/SDK 1.4.3 (API 87.1) or a compatible SDK.

```powershell
python -m pip install --upgrade ufbt
python -m ufbt update --channel release
python tests/run_tests.py
python tests/test_companion.py
python -m ufbt
```

The build creates `dist/crunch_fz.fap`. In a full firmware checkout, place the project at `applications_user/crunch_fz` and run:

```sh
./fbt fap_crunch_fz
```

## License

Crunch-FZ preserves upstream Crunch's **GNU General Public License, version 2 only (`GPL-2.0-only`)**. See [LICENSE](LICENSE), [NOTICE](NOTICE), and [UPSTREAM_VERSION.md](UPSTREAM_VERSION.md).
