#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Host tests for the Linux/Raspberry Pi Crunch bridge."""

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "companion" / "crunch_fz_bridge.py"
SPEC = importlib.util.spec_from_file_location("crunch_fz_bridge", MODULE_PATH)
assert SPEC and SPEC.loader
BRIDGE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = BRIDGE
SPEC.loader.exec_module(BRIDGE)


class CompanionTests(unittest.TestCase):
    def test_hex_is_bounded_printable_ascii(self):
        self.assertEqual(BRIDGE.decode_hex("616225", 8), "ab%")
        self.assertEqual(BRIDGE.decode_hex("-", 8), "")
        with self.assertRaises(ValueError):
            BRIDGE.decode_hex("00", 8)
        with self.assertRaises(ValueError):
            BRIDGE.decode_hex("414243", 2)

    def test_command_is_fixed_argv_for_genuine_crunch(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            with patch.object(BRIDGE.shutil, "which", return_value=None):
                bridge = BRIDGE.Bridge("/dev/serial0", 115200, output)
            bridge.crunch = "/usr/bin/crunch"
            fields = [
                "CWF1", "RUN", "3", "3", "6", "6162", "407825", "-", "776F7264732E747874"
            ]
            command, path = bridge.parse_run(fields)
            self.assertEqual(
                command,
                [
                    "/usr/bin/crunch", "3", "3", "ab", "+", "+", "+",
                    "-t", "@x%", "-o", str(output / "words.txt"),
                ],
            )
            self.assertEqual(path.name, "words.txt")

    def test_existing_output_gets_a_new_real_path(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            (output / "words.txt").write_text("existing", encoding="ascii")
            with patch.object(BRIDGE.shutil, "which", return_value=None):
                bridge = BRIDGE.Bridge("/dev/serial0", 115200, output)
            bridge.crunch = "/usr/bin/crunch"
            fields = [
                "CWF1", "RUN", "1", "1", "1", "6162", "-", "-", "776F7264732E747874"
            ]
            _, path = bridge.parse_run(fields)
            self.assertEqual(path.name, "words-1.txt")

    def test_measured_lines_and_bytes_come_from_output(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            target = output / "real.txt"
            target.write_bytes(b"a\nb\naa\n")
            with patch.object(BRIDGE.shutil, "which", return_value=None):
                bridge = BRIDGE.Bridge("/dev/serial0", 115200, output)
            bridge.output_path = target
            bridge._measure()
            self.assertEqual(bridge.lines, 3)
            self.assertEqual(bridge.bytes, 7)

    def test_missing_required_output_is_reported_without_exception(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            with patch.object(BRIDGE.shutil, "which", return_value=None):
                bridge = BRIDGE.Bridge("/dev/serial0", 115200, output)
            bridge.output_path = output / "removed.txt"
            self.assertEqual(bridge._measure(require_file=True), "OUTPUT_MISSING")

    def test_measurement_filesystem_error_is_bounded(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            with patch.object(BRIDGE.shutil, "which", return_value=None):
                bridge = BRIDGE.Bridge("/dev/serial0", 115200, output)
            bridge.output_path = output / "wordlist.txt"
            with patch.object(Path, "is_file", side_effect=OSError("media removed")):
                self.assertEqual(bridge._measure(), "MEASURE_media_removed")

    def test_process_stderr_is_preserved_as_protocol_error(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            with patch.object(BRIDGE.shutil, "which", return_value=None):
                bridge = BRIDGE.Bridge("/dev/serial0", 115200, output)
            target = output / "failed.txt"
            bridge.output_path = target
            bridge.started = BRIDGE.time.monotonic()
            bridge.generation_worker(
                [sys.executable, "-c", "import sys; print('disk full', file=sys.stderr); sys.exit(7)"],
                target,
            )
            self.assertEqual(bridge.state, "ERROR")
            self.assertEqual(bridge.exit_code, 7)
            self.assertEqual(bridge.error, "disk_full")


if __name__ == "__main__":
    unittest.main()
