#!/usr/bin/env python3
"""Test the actual display target using synthetic VI2 services, never a console."""
from pathlib import Path
import os,shlex,subprocess,tempfile
here=Path(__file__).resolve().parent
sdk=here.parents[1]
with tempfile.TemporaryDirectory(prefix="gameshare-display-") as work:
    binary=Path(work)/"test"
    subprocess.run([*shlex.split(os.environ.get("CXX","c++")),"-std=c++20","-Wall","-Wextra","-Werror","-pedantic","-O1","-g","-pthread","-fsanitize=address,undefined","-fno-omit-frame-pointer","-I",str(here/"fakes"),"-I",str(sdk/"include"),str(sdk/"src/guest_display.cpp"),str(here/"test.cpp"),"-o",str(binary)],check=True,timeout=180)
    subprocess.run([str(binary)],check=True,timeout=20)
print("guest display lifetime, partial-failure, ownership and draw/close tests passed")
