#!/usr/bin/env /usr/bin/python3

'''
该脚本用于自动生成基准测试配置文件（benchmarks.py）。

该脚本遍历 evaluation 目录下的各个 package，
查找 artefacts 目录下的程序，并收集相关路径信息，最终生成一个包含所有基准程序信息的配置文件。

Usage:
- 直接运行本脚本，将在 evaluation 目录下生成 benchmarks.py 配置文件:
   python3 gen_bench_config.py
'''

from pathlib import Path


CONFIG_SPEC = '''# 基准测试配置文件
from pathlib import PosixPath

BENCHMARKS = {bench}

PACKAGE_2_PROGRAMS = {package_2_programs}

def find_bench_config(program: str) -> dict:
    return next((cfg for cfg in BENCHMARKS if cfg['program'] == program), None)

'''


EVAL_ROOT = Path(__file__).parent.absolute()


def generate_bench_configs(lazy: bool = False):
    '''
    生成基准测试配置文件
    '''
    benchmarks = []
    package_programs = {}
    programs_set = set()
    for package in EVAL_ROOT.iterdir():
        if not package.is_dir() or package.name.startswith("."):
            continue
        artefacts_dir = package / "artefacts"
        if not artefacts_dir.exists():
            continue
        programs = []
        for program in artefacts_dir.iterdir():
            if not program.is_dir() or program.name.startswith("."):
                continue
            benchmark = {
                "program": program.name,
                "package": package.name,
                "program_dir": program,
                "package_dir": package,
                "data_dir": package / "data" / program.name,
                "corpus": package / "corpus" / program.name
            }
            benchmarks.append(benchmark)
            programs.append(program.name)
            programs_set.add(program.name)
        package_programs[package.name] = programs
    should_gen = not lazy
    if lazy:
        try:
            from benchmarks import BENCHMARKS
            old_programs_set = set(bench['program']
                                   for bench in BENCHMARKS)
            if old_programs_set != programs_set:
                print("old_programs_set != programs_set, generating benchmarks.py...")
                should_gen = True
        except ModuleNotFoundError:
            print(
                "ModuleNotFoundError: benchmarks.py not found, generating benchmarks.py...")
            should_gen = True
        except Exception as e:
            print(f"Unknown Exception: {e}")
            should_gen = True
    if not should_gen:
        return
    with open(EVAL_ROOT / "benchmarks.py", "w") as f:
        bench_config = '[\n'
        for benchmark in benchmarks:
            bench_config += f'    {repr(benchmark)},\n'
        bench_config += ']\n'
        package_2_programs = '{\n'
        for package, programs in package_programs.items():
            list_str = (",\n" + str(' ' * 8)).join(repr(program)
                                                   for program in programs)
            package_2_programs += f'    {repr(package)}: [\n        {list_str}\n    ],\n'
        package_2_programs += '}\n'
        config = CONFIG_SPEC.format(
            bench=bench_config, package_2_programs=package_2_programs)
        f.write(config)


if __name__ == '__main__':
    generate_bench_configs()
