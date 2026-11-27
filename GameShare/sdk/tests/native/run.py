#!/usr/bin/env python3
"""Compile and test the actual native worker on the host; no console/network."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

here = Path(__file__).resolve().parent
sdk = here.parents[1]
scenarios = (
    "success", "cancel", "prepare_failure", "disconnect", "admission_grace",
    "blocked_snapshot", "cancel_waiting", "audio_fallback", "selector_success",
    "selector_cancel", "selector_cancel_waiting", "locked_launch",
    "locked_cached_launch", "locked_no_launch",
    "locked_bad_launch", "no_audio", "no_input",
    "invalid_styles", "invalid_timeout", "invalid_mode", "concurrent_hosts",
    "separate_success", "separate_audio", "separate_audio_failure",
    "separate_missing_target", "separate_open_failure",
    "separate_open_throw", "separate_retire_failure", "separate_retire_throw",
    "separate_close_failure", "separate_close_throw", "separate_partial_retained",
    "separate_finalize_retained", "finalize_retained",
)
with tempfile.TemporaryDirectory(prefix="moonlight-native-gameshare-") as directory:
    executable = Path(directory) / "native-gameshare-test"
    print("Compiling actual GameShare SDK host.cpp with host mocks and ASan/UBSan", flush=True)
    subprocess.run([
        *shlex.split(os.environ.get("CXX", "c++")), "-std=c++20", "-O0", "-g",
        "-D__SWITCH2__", "-pthread", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
        "-I", str(here / "fakes"), "-I", str(sdk / "include"),
        "-I", str(sdk / "src"),
        str(here / "lifecycle.cpp"),
        str(sdk / "src/host.cpp"),
        "-o", str(executable),
    ], check=True, timeout=180)
    for scenario in scenarios:
        environment = os.environ.copy()
        if scenario in ("finalize_retained", "separate_finalize_retained",
                        "separate_retire_failure", "separate_retire_throw",
                        "separate_close_failure", "separate_close_throw", "separate_partial_retained"):
            # This scenario must retain memory potentially referenced by a native
            # service until process exit. Other cases keep normal leak checking.
            environment["ASAN_OPTIONS"] = environment.get("ASAN_OPTIONS", "") + ":detect_leaks=0"
        subprocess.run([str(executable), scenario], check=True, env=environment, timeout=8)
