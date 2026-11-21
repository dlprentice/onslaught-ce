#!/usr/bin/env python3
"""The CE Dodge preference is separate from stock keyboard and career records."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[2]
base=root/"local-data/port/ce-dodge-binding-tests"
base.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(dir=base) as job:
    exe=Path(job)/"binding"
    subprocess.run(["c++","-std=c++17","-O2","-Wall","-Wextra","-Werror",
        str(root/"port/game/ce_dodge_binding.cpp"),str(root/"port/tools/tests/ce_dodge_binding_driver.cpp"),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
    # Compile the unmodified widget body, including capture and reverse ownership, with a host input loop.
    scratch=Path(job)
    widget=(root/"port/game/ce_dodge_control_item.cpp").read_text().splitlines(keepends=True)
    (scratch/"widget.inc").write_text("".join(line for line in widget if not line.startswith('#include "') or 'ce_dodge_binding.h' in line))
    target=scratch/"widget"
    subprocess.run(["c++","-std=c++17","-O2","-Wall","-Wextra","-Werror","-DCE_VERSION=1",
        "-I"+str(scratch),"-I"+str(root/"port/game"),str(root/"port/game/ce_dodge_binding.cpp"),
        str(root/"port/tools/tests/ce_dodge_widget_driver.cpp"),"-o",str(target)],check=True)
    subprocess.run([str(target)],check=True)
