# Security and robustness

## Arithmetic

Every count multiplication and sum is checked against `UINT64_MAX`. Pattern products, length-range totals, and newline-inclusive byte totals fail closed with an explicit overflow message. Progress percentage uses bounded integer comparisons and does not multiply an arbitrary 64-bit total by 100.

## Input validation

The core rejects empty/oversized sets, invalid length ranges, pattern-length mismatches, literal masks that do not match the pattern, non-printable bytes, and unrepresentable totals. The UI restricts output to absolute `/ext` paths, rejects traversal and repeated separators, and rejects FAT-invalid filename characters.

## Storage

The application asks for confirmation before replacing an existing file. Output is opened only after successful preflight. Every line write must return its exact requested length. Short writes, synchronization failures, missing SD storage, and allocation/open failures produce explicit failure states. Cancellation and write failure close the file; the completion page identifies partial output instead of claiming completion.

The output directory and filename are passed directly to the storage API. They are never inserted into a shell command. The application does not execute generated words or parse generated content.

## External companion

CWF1 accepts only `HELLO`, `STATUS`, `RUN` with seven bounded fields, and `STOP`. Arbitrary commands are rejected. Variable values use bounded hexadecimal ASCII encoding, are decoded and validated again on Linux, and are passed to `subprocess.Popen` as a fixed argument list with `shell=False`. Output names are reduced to safe basenames, collision-renamed instead of overwritten, and confined beneath `/var/lib/crunch-fz/output`.

The UART is acquired through the official USART control API. The firmware expansion listener is disabled only while the external screen owns USART, then restored on Back or teardown. RX lines, stream buffers, tokens, numeric fields, and display fields are bounded; malformed and overflowing protocol messages are discarded.

## Memory and lifecycle

Generation uses fixed-size stack state and streams one line at a time. The complete output is never accumulated. Worker, file, mutex, GUI, and storage resources have explicit close/free paths. Back requests cancellation while a worker owns the file; teardown joins the worker before releasing application state.

## Intended use

Wordlists have legitimate testing and recovery uses but can also be misused. Use Crunch-FZ only with systems, files, and credentials you own or are authorized to test.
