#!/usr/bin/env python3
"""Exercise the actual SDK C++ reader using synthetic shared memory only."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

here = Path(__file__).resolve().parent
sdk = here.parents[1]
with tempfile.TemporaryDirectory(prefix="gameshare-sdk-guest-input-") as work:
    binary = Path(work) / "guest-input-test"
    print("Compiling actual GameShare SDK guest_input.cpp with ASan/UBSan", flush=True)
    subprocess.run([
        *shlex.split(os.environ.get("CXX", "c++")), "-std=c++20", "-O1", "-g",
        "-D__SWITCH2__", "-Wall", "-Wextra", "-Werror", "-pedantic",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(here), "-I", str(sdk / "src"),
        str(here / "test.cpp"), "-o", str(binary),
    ], check=True, timeout=180)
    subprocess.run([str(binary)], check=True, timeout=10)
