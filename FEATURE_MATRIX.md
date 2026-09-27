# Feature matrix

| Upstream behavior or option | Crunch-FZ status | Notes |
|---|---|---|
| Minimum/maximum length | Implemented | 1..32; shortest complete length first |
| Default lowercase | Implemented | Exact upstream ordering |
| Numeric, uppercase, mixed-case, alphanumeric, symbols | Implemented | On-device preset selector |
| Custom charset | Implemented | Printable ASCII; repeated entries remain repeated |
| `-t` patterns | Implemented | `@`, `,`, `%`, `^` plus fixed text |
| `-l` literal positions | Implemented | Blank or same-length literal mask |
| Prefix/suffix text | Implemented | Fixed text at either side of active pattern markers |
| Exact line calculation | Implemented | Checked unsigned 64-bit arithmetic |
| Exact byte calculation | Implemented | Includes the newline written after every word |
| `-o` output | Implemented | User filename and absolute `/ext` directory |
| Existing-file protection | Implemented | Separate overwrite confirmation before truncation |
| Output streaming | Implemented | One generated line is written at a time |
| Cancellation | Implemented | Current write completes, file closes, partial totals shown |
| Progress | Implemented | Actual lines, bytes, percentage, elapsed time, and measured entries/s |
| Disk-full/write handling | Implemented | Short write or sync failure becomes a closed write-error result |
| Raspberry Pi/Linux mode | Implemented | Genuine upstream executable; Flipper controls it over CWF1 UART |
| External measured status | Implemented | Real output lines, bytes, elapsed time, filename, exit state, and version |
| External collision handling | Implemented | Selects a new numbered filename; never silently overwrites |
| Arbitrary remote shell | Intentionally absent | Fixed validated argument vector only |
| `-f charset.lst` | Adapted | Presets plus direct custom-set editor; no charset file parser |
| `-d` adjacent duplicate suppression | Not implemented | Base upstream duplicate behavior is preserved |
| `-s` start block / `-e` end block | Not implemented | Full configured range or pattern is generated |
| `-i` inverted increment | Not implemented | Standard rightmost-fastest order only |
| `-b` byte splitting / `-c` line splitting | Not implemented | One explicitly named output file |
| `-p` permutation / `-q` word-file permutation | Not implemented | Different factorial algorithm and input model |
| `-r` resume | Not implemented | Partial cancelled output is reported but not resumed |
| `-z` compression | Not implemented | No compressor is bundled into the FAP |
| Unicode sets/output | Not implemented | Printable single-byte ASCII only |
| stdout pipelines | Native: not applicable | Native output goes to microSD; full CLI remains available on Linux |
| NFC/RFID/radio behavior | Not applicable | Crunch is a wordlist generator and uses no radio hardware |

An omitted feature is rejected or absent from the UI. It is never represented by fabricated output.
