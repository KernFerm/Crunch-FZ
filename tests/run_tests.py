#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Build and run the host-native Crunch-FZ core tests."""

from pathlib import Path
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build-host-tests"
OUT.mkdir(exist_ok=True)

if os.name == "nt":
    visual_studio_root = Path(r"C:\Program Files\Microsoft Visual Studio\2022")
    editions = ("BuildTools", "Community", "Professional", "Enterprise")
    installs = [visual_studio_root / edition for edition in editions]
    install = next((path for path in installs if (path / "VC" / "Tools" / "MSVC").is_dir()), None)
    if install is None:
        raise SystemExit("Visual Studio C++ tools not found in a standard VS 2022 location")
    tool_versions = sorted(
        (path for path in (install / "VC" / "Tools" / "MSVC").iterdir() if path.is_dir()),
        key=lambda path: tuple(int(part) for part in path.name.split(".")),
    )
    msvc = tool_versions[-1]
    compiler = msvc / "bin" / "Hostx64" / "x64" / "cl.exe"
    kits = Path(r"C:\Program Files (x86)\Windows Kits\10")
    sdk_versions = sorted(
        (path for path in (kits / "Include").iterdir() if path.is_dir()),
        key=lambda path: tuple(int(part) for part in path.name.split(".")),
    )
    sdk_version = sdk_versions[-1].name
    include_root = kits / "Include" / sdk_version
    library_root = kits / "Lib" / sdk_version
    environment = os.environ.copy()
    environment["INCLUDE"] = os.pathsep.join(
        str(path)
        for path in (
            msvc / "include",
            include_root / "ucrt",
            include_root / "shared",
            include_root / "um",
            include_root / "winrt",
        )
    )
    environment["LIB"] = os.pathsep.join(
        str(path)
        for path in (
            msvc / "lib" / "x64",
            library_root / "ucrt" / "x64",
            library_root / "um" / "x64",
        )
    )
    environment["PATH"] = os.pathsep.join(
        (str(compiler.parent), str(install / "Common7" / "IDE"), environment["PATH"])
    )
    executable = OUT / "core_test.exe"
    subprocess.run(
        [
            str(compiler),
            "/nologo",
            "/W4",
            "/WX",
            "/std:c11",
            f"/I{ROOT}",
            str(ROOT / "crunch_core.c"),
            str(ROOT / "crunch_external_protocol.c"),
            f'/Tc{ROOT / "tests" / "core_test.c.host"}',
            f"/Fe:{executable}",
        ],
        cwd=OUT,
        check=True,
        env=environment,
    )
else:
    compiler = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    if not compiler:
        raise SystemExit("No C compiler found")
    executable = OUT / "core_test"
    subprocess.run(
        [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", f"-I{ROOT}",
         str(ROOT / "crunch_core.c"), str(ROOT / "crunch_external_protocol.c"),
         "-x", "c", str(ROOT / "tests" / "core_test.c.host"),
         "-o", str(executable)],
        cwd=ROOT,
        check=True,
    )

subprocess.run([str(executable)], cwd=ROOT, check=True)
