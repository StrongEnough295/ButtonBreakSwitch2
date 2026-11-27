#!/usr/bin/env python3
"""Exercise actual CPU uploader with synthetic GSV/VI2; no console or SDK link."""
from pathlib import Path
import os,shlex,subprocess,tempfile
here=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="gameshare-cpu-upload-") as work:
    binary=Path(work)/"test"
    subprocess.run([*shlex.split(os.environ.get("CC","clang")),"-std=c11","-O1","-g","-Wall","-Wextra","-Werror","-fsanitize=address,undefined","-fno-sanitize-recover=all","-fno-omit-frame-pointer","-I",str(here/"stubs"),str(here/"test.c"),"-o",str(binary)],check=True,timeout=180)
    subprocess.run([str(binary)],check=True,timeout=20)
    sdk=here.parents[1]
    target=Path(work)/"target"
    subprocess.run([*shlex.split(os.environ.get("CXX","c++")),"-std=c++20","-O1","-g","-Wall","-Wextra","-Werror","-pthread","-fsanitize=address,undefined","-fno-omit-frame-pointer","-I",str(here.parent/"guest_display/fakes"),"-I",str(sdk/"include"),str(sdk/"src/guest_display.cpp"),str(sdk/"src/cpu_image_target.cpp"),str(here/"target.cpp"),"-o",str(target)],check=True,timeout=180)
    subprocess.run([str(target)],check=True,timeout=20)
    subprocess.run([str(target),"contradictory-close"],check=True,timeout=20)
print("CPU target claim, frame forwarding, retire/close and quarantine tests passed")
