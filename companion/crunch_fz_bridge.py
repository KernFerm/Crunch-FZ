#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""UART bridge that runs genuine upstream Crunch on Linux/Raspberry Pi."""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import tempfile
import threading
import time
from pathlib import Path

import serial

BRIDGE_VERSION = "1.0.4"
PROTOCOL_VERSION = 1
MAX_LINE = 512
DATA_ROOT = Path("/var/lib/crunch-fz")
DEFAULT_OUTPUT = DATA_ROOT / "output"
SERIAL_RE = re.compile(r"/dev/(serial[0-9]+|tty(?:AMA|USB|ACM|S)[0-9]+)\Z")
PRESETS = (
    "0123456789",
    "abcdefghijklmnopqrstuvwxyz",
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ",
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789",
    "!@#$%^&*()-_+=~`[]{}|\\:;\"'<>,.?/ ",
)


def token(value: object, limit: int) -> str:
    cleaned = re.sub(r"[^A-Za-z0-9_.:+-]", "_", str(value))
    return (cleaned or "-")[:limit]


def os_error_token(prefix: str, error: OSError) -> str:
    detail = error.strerror or str(error) or error.__class__.__name__
    return token(f"{prefix}_{detail}", 63)


def decode_hex(value: str, maximum: int) -> str:
    if value == "-":
        return ""
    if not value or len(value) % 2 or len(value) > maximum * 2:
        raise ValueError("invalid hex length")
    try:
        decoded = bytes.fromhex(value).decode("ascii")
    except (ValueError, UnicodeDecodeError) as error:
        raise ValueError("invalid ASCII hex") from error
    if any(ord(character) < 0x20 or ord(character) > 0x7E for character in decoded):
        raise ValueError("non-printable input")
    return decoded


class Bridge:
    def __init__(self, port: str, baud: int, output_dir: Path):
        self.port = port
        self.baud = baud
        self.output_dir = output_dir
        self.crunch = shutil.which("crunch")
        self.crunch_version = self._version()
        self.state = "IDLE"
        self.lines = 0
        self.bytes = 0
        self.elapsed_ms = 0
        self.exit_code = 0
        self.output_name = "-"
        self.output_path: Path | None = None
        self.process: subprocess.Popen[bytes] | None = None
        self.worker: threading.Thread | None = None
        self.started = 0.0
        self.measured_offset = 0
        self.cancel_requested = False
        self.error = "-"
        self.lock = threading.Lock()

    def _version(self) -> str:
        if not self.crunch:
            return "missing"
        for option in ("-V", "-h"):
            try:
                result = subprocess.run(
                    [self.crunch, option],
                    check=False,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    timeout=5,
                )
            except (OSError, subprocess.TimeoutExpired):
                return "unknown"
            match = re.search(r"crunch version\s+([0-9]+\.[0-9]+(?:\.[0-9]+)?)", result.stdout, re.I)
            if match:
                return match.group(1)
        return "unknown"

    @staticmethod
    def write(uart: serial.Serial, line: str) -> None:
        uart.write((line[: MAX_LINE - 2] + "\n").encode("ascii", "strict"))
        uart.flush()

    def info(self, uart: serial.Serial) -> None:
        self.write(
            uart,
            f"CWF1 INFO {PROTOCOL_VERSION} {BRIDGE_VERSION} {token(self.crunch_version, 31)}",
        )

    def _measure(self, require_file: bool = False) -> str | None:
        path = self.output_path
        if not path:
            return None
        try:
            if not path.is_file():
                return "OUTPUT_MISSING" if require_file else None
            size = path.stat().st_size
            if size < self.measured_offset:
                self.measured_offset = 0
                self.lines = 0
            with path.open("rb") as source:
                source.seek(self.measured_offset)
                while True:
                    block = source.read(65536)
                    if not block:
                        break
                    self.lines += block.count(b"\n")
                    self.measured_offset += len(block)
            self.bytes = size
            return None
        except OSError as error:
            return os_error_token("MEASURE", error)

    def status(self, uart: serial.Serial) -> None:
        process_to_stop: subprocess.Popen[bytes] | None = None
        with self.lock:
            if self.state in ("STARTING", "RUNNING", "STOPPING"):
                measure_error = self._measure()
                if measure_error:
                    self.error = measure_error
                    self.state = "ERROR"
                    self.exit_code = -1
                    process_to_stop = self.process
                self.elapsed_ms = max(0, int((time.monotonic() - self.started) * 1000))
            values = (
                self.state,
                self.lines,
                self.bytes,
                self.elapsed_ms,
                self.exit_code,
                self.output_name,
            )
            error_text = self.error if self.state == "ERROR" else "-"
        if process_to_stop and process_to_stop.poll() is None:
            process_to_stop.terminate()
        self.write(
            uart,
            f"CWF1 STATUS {token(values[0], 15)} {values[1]} {values[2]} "
            f"{values[3]} {values[4]} {token(values[5], 63)}",
        )
        if error_text != "-":
            self.write(uart, f"CWF1 ERROR {token(error_text, 63)}")

    def busy(self) -> bool:
        return self.worker is not None and self.worker.is_alive()

    def _next_output(self, requested: str) -> Path:
        safe = re.sub(r"[^A-Za-z0-9._-]", "_", requested).strip(".")
        safe = safe[:63] or "wordlist.txt"
        candidate = self.output_dir / safe
        stem, suffix = candidate.stem, candidate.suffix
        counter = 1
        while candidate.exists():
            candidate = self.output_dir / f"{stem}-{counter}{suffix}"
            counter += 1
        return candidate

    def parse_run(self, fields: list[str]) -> tuple[list[str], Path]:
        if len(fields) != 9:
            raise ValueError("invalid field count")
        minimum, maximum, mode = (int(fields[index]) for index in (2, 3, 4))
        if not 1 <= minimum <= maximum <= 32 or not 0 <= mode <= 6:
            raise ValueError("invalid range")
        custom = decode_hex(fields[5], 80)
        pattern = decode_hex(fields[6], 32)
        literal = decode_hex(fields[7], 32)
        requested = decode_hex(fields[8], 63)
        main_set = custom if mode == 6 else PRESETS[mode]
        if not main_set or not requested or "/" in requested or "\\" in requested:
            raise ValueError("invalid set or filename")
        if pattern:
            if len(pattern) != minimum or minimum != maximum:
                raise ValueError("pattern length mismatch")
            if literal and len(literal) != len(pattern):
                raise ValueError("literal length mismatch")
        elif literal:
            raise ValueError("literal without pattern")
        output = self._next_output(requested)
        command = [self.crunch or "crunch", str(minimum), str(maximum), main_set]
        if pattern:
            command.extend(["+", "+", "+", "-t", pattern])
            if literal:
                command.extend(["-l", literal])
        command.extend(["-o", str(output)])
        return command, output

    def generation_worker(self, command: list[str], output: Path) -> None:
        try:
            with tempfile.TemporaryFile() as error_output:
                with self.lock:
                    if self.cancel_requested:
                        self.state = "CANCELLED"
                        return
                    self.process = subprocess.Popen(
                        command,
                        stdin=subprocess.DEVNULL,
                        stdout=subprocess.DEVNULL,
                        stderr=error_output,
                    )
                    process = self.process
                    self.state = "RUNNING"
                return_code = process.wait()
                error_output.seek(0, 2)
                error_size = error_output.tell()
                error_output.seek(max(0, error_size - 256))
                diagnostic = error_output.read().decode("utf-8", "replace").strip()
            with self.lock:
                measure_error = self._measure(require_file=True)
                self.elapsed_ms = max(0, int((time.monotonic() - self.started) * 1000))
                self.exit_code = return_code
                self.process = None
                if self.cancel_requested:
                    self.state = "CANCELLED"
                elif return_code == 0 and not measure_error:
                    self.state = "DONE"
                else:
                    self.state = "ERROR"
                    if return_code != 0:
                        self.error = token(diagnostic or f"CRUNCH_EXIT_{return_code}", 63)
                    else:
                        self.error = measure_error or "OUTPUT_MEASUREMENT_FAILED"
        except OSError as error:
            with self.lock:
                self.process = None
                self.state = "ERROR"
                self.exit_code = -1
                self.error = os_error_token("PROCESS", error)

    def start(self, uart: serial.Serial, fields: list[str]) -> None:
        with self.lock:
            if self.busy():
                self.write(uart, "CWF1 ERROR ALREADY_RUNNING")
                return
            if not self.crunch:
                self.write(uart, "CWF1 ERROR CRUNCH_NOT_INSTALLED")
                return
            try:
                command, output = self.parse_run(fields)
            except (ValueError, OverflowError):
                self.write(uart, "CWF1 ERROR INVALID_CONFIGURATION")
                return
            except OSError as error:
                self.write(uart, f"CWF1 ERROR {token('OUTPUT_PATH_' + str(error), 63)}")
                return
            try:
                self.output_dir.mkdir(parents=True, exist_ok=True)
            except OSError as error:
                self.state = "ERROR"
                self.error = os_error_token("OUTPUT_DIRECTORY", error)
                self.write(uart, f"CWF1 ERROR {self.error}")
                return
            self.lines = self.bytes = self.elapsed_ms = self.measured_offset = 0
            self.exit_code = 0
            self.output_path = output
            self.output_name = output.name
            self.cancel_requested = False
            self.error = "-"
            self.started = time.monotonic()
            self.state = "STARTING"
            self.worker = threading.Thread(
                target=self.generation_worker,
                args=(command, output),
                name="crunch-generation",
                daemon=True,
            )
            worker = self.worker
        worker.start()
        self.status(uart)

    def stop(self) -> None:
        with self.lock:
            self.cancel_requested = True
            process = self.process
            if process and process.poll() is None:
                self.state = "STOPPING"
        if process and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=2)

    def handle(self, uart: serial.Serial, text: str) -> None:
        fields = text.split()
        if fields == ["CWF1", "HELLO"]:
            self.info(uart)
            self.status(uart)
        elif fields == ["CWF1", "STATUS"]:
            self.status(uart)
        elif len(fields) >= 2 and fields[:2] == ["CWF1", "RUN"]:
            self.start(uart, fields)
        elif fields == ["CWF1", "STOP"]:
            self.stop()
            self.status(uart)
        else:
            self.write(uart, "CWF1 ERROR INVALID_COMMAND")

    def run(self) -> None:
        try:
            with serial.Serial(self.port, self.baud, timeout=0.5, write_timeout=2) as uart:
                try:
                    self.output_dir.mkdir(parents=True, exist_ok=True)
                except OSError as error:
                    with self.lock:
                        self.state = "ERROR"
                        self.error = os_error_token("OUTPUT_DIRECTORY", error)
                    self.write(uart, f"CWF1 ERROR {self.error}")
                while True:
                    raw = uart.readline(MAX_LINE)
                    if not raw:
                        continue
                    if len(raw) >= MAX_LINE and not raw.endswith(b"\n"):
                        uart.reset_input_buffer()
                        self.write(uart, "CWF1 ERROR LINE_TOO_LONG")
                        continue
                    try:
                        text = raw.decode("ascii").strip()
                    except UnicodeDecodeError:
                        self.write(uart, "CWF1 ERROR NON_ASCII")
                        continue
                    self.handle(uart, text)
        finally:
            self.stop()


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", default="/dev/serial0", help="3.3 V UART device")
    parser.add_argument("--baud", type=int, choices=(115200, 230400, 460800), default=115200)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    if not SERIAL_RE.fullmatch(args.serial):
        parser.error("serial device must be a supported /dev UART")
    args.output_dir = args.output_dir.expanduser().resolve()
    if not args.output_dir.is_relative_to(DATA_ROOT.resolve()):
        parser.error("output directory must stay under /var/lib/crunch-fz")
    return args


def main() -> int:
    args = arguments()
    Bridge(args.serial, args.baud, args.output_dir).run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
