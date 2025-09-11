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
'''


EVAL_ROOT = Path(__file__).parent.absolute()


def generate_bench_configs(dump_program_list: bool = False):
    '''
    生成基准测试配置文件
    '''
    benchmarks = []
    for package in EVAL_ROOT.iterdir():
        if not package.is_dir() or package.name.startswith("."):
            continue
        artefacts_dir = package / "artefacts"
        if not artefacts_dir.exists():
            continue
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
    if dump_program_list:
        with open(EVAL_ROOT / "program_list.txt", "w") as f:
            for benchmark in benchmarks:
                f.write(f"{benchmark['program']}\n")
        return
    with open(EVAL_ROOT / "benchmarks.py", "w") as f:
        bench_config = '[\n'
        for benchmark in benchmarks:
            bench_config += f'    {repr(benchmark)},\n'
        bench_config += ']\n'
        config = CONFIG_SPEC.format(bench=bench_config)
        f.write(config)
     

if __name__ == '__main__':
    generate_bench_configs()
