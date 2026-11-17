# -*- Python -*-

import os
import subprocess

import lit.formats

config.name = "MAIR"
config.test_format = lit.formats.ShTest(execute_external=True)
config.suffixes = [".ll"]
# The .ll tests call FileCheck by its bare name. A distro package only installs
# /usr/bin/FileCheck-N (so plain "FileCheck" is not on PATH), and CI runs
# "cmake --build build --target check-mair" from a shell that has not set
# anything up, so the LLVM tools directory has to be added here.
_tool_dirs = [os.path.dirname(config.mair_opt)]
_llvm_tools_dir = getattr(config, "llvm_tools_dir", "")
if _llvm_tools_dir:
    _tool_dirs.append(_llvm_tools_dir)
config.environment["PATH"] = os.path.pathsep.join(
    _tool_dirs + [config.environment["PATH"]]
)
config.substitutions.append(("mair_opt", config.mair_opt))
config.substitutions.append(("mair-opt", config.mair_opt))

stats_probe = subprocess.run(
    [
        config.mair_opt,
        "-stats",
        "--disable-output",
        "-passes=simplifygraph",
        "-",
    ],
    input="define void @f() { ret void }\n",
    text=True,
    stdout=subprocess.DEVNULL,
    stderr=subprocess.PIPE,
    env=config.environment,
    check=False,
)
if (
    stats_probe.returncode == 0
    and stats_probe.stderr
    and "Statistics are disabled" not in stats_probe.stderr
):
    config.available_features.add("mair_stats")
