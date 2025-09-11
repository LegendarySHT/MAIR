本目录为XSan在50+个项目上运行的根目录

- `<package>` : 被测试的软件包
    - [手写] `fetch.sh`: 拉取软件包的代码，尽可能减少包体积，比如git clone 时，应该指定 --depth=1 参数
    - [手写] `compile.sh`: 编译脚本，受外部环境变量，并极可能减少无关产物的编译, 并将产物移动到 artefacts目录
        ```requirements
        默认添加 -g 支持, 且需要支持并行编译, 尽量避免依赖package自身的动态库
        - 支持 P({TSan, UBSan, MSan}) + ASan 的各种组合 （共 8 种）
        - 支持原生的 ASan , MSan, TSan, UBSan 共 4 种编译选项
        - 支持不加sanitizer的版本作为 baseline
        - 支持 -O0 用于编译
        ```
    - repo: 软件包的代码, 尽可能减少包体积，比如git clone 时，应该指定 --depth=1 参数
    - temp: 临时目录，用于 compile.sh的编译
    - artefact
        - `<program1>`: 编译后的可执行文件名
        - `<program2>`: 编译后的可执行文件名
          - [手写] `run.sh` : 运行脚本，用于运行目标program，需要设置环境变量 PRE_OPT, POST_OPT, stdin 和 PROG_DIR
            - 注意：run.sh 需要适合大规模运行，即不会在工作目录产生‘垃圾’文件，不会就地修改输入文件。例如，解压缩软件，就需要将解压地址设置为 /tmp/ 下的一个临时目录。
          - `<prog>.dbg`: 用于debug的程序
          - `<prog>.raw`: 不开sanitizer的baseline
          - `<prog>.asan`: ASan
          - `<prog>.tsan`: TSan
          - `<prog>.ubsan`: UBSan
          - `<prog>.msan`: MSan
          - `<prog>.xsan-asan`: ASan@XSan
          - `<prog>.xsan-asan-msan`: (ASan+MSan)@XSan
          - `<prog>.xsan-asan-tsan`: (ASan+TSan)@XSan
          - `<prog>.xsan-asan-ubsan`: (ASan+UBSan)@XSan
          - `<prog>.xsan-asan-msan-tsan`: (ASan+MSan+TSan)@XSan
          - `<prog>.xsan-asan-msan-ubsan`: (ASan+MSan+UBSan)@XSan
          - `<prog>.xsan-asan-tsan-ubsan`: (ASan+TSan+UBSan)@XSan
          - `<prog>.xsan-asan-msan-tsan-ubsan`: (ASan+MSan+TSan+UBSan)@XSan
    - inputs: 测试输入（可以来自fuzzing的seeds corpus，也可以是程序自带的测试用例）
        - `<program1>` : 一个目录，表示 program1 的测试输入，拥有 1 个及以上的测试输入文件
        - `<program2>` : 如果program2 与program1共享一种输入用例，那么 用 `ln -s program1 program2` 即可
- `<pakcage>`: ...
  - ...
- `activate_compile_flags.sh`: 供 `compile.sh` 导入
- `compile-all.sh`: 调用 `compile.sh` 编译程序的所有Sanitizer模式
- `run.sh`: 运行脚本，仅供 artefacts/<prog>/run.sh 调用
- `common-run.sh`: 通用运行脚本，供 run.sh 调用
- `eval-sample.sh`: 单次运行脚本，用于测量程序的性能和内存占用
- `benchmarking.py`: 用于对不同 Sanitizer 配置下的基准程序进行自动化的性能测试与内存占用评测，并保存结果数据，便于后续分析。
- `benchmarks.py`: 用于定义基准程序和不同 Sanitizer 配置的集合。
- `gen_bench_config.py`: 用于自动生成 benchmarks.py 配置文件。
- `README.md`: 本文件

Example:
```
.
├── binutils
│   ├── artefacts
│   │   ├── cxxfilt
│   │   │   ├── cxxfilt.asan
│   │   │   ├── cxxfilt.msan
│   │   │   ├── cxxfilt.raw
│   │   │   ├── cxxfilt.tsan
│   │   │   ├── cxxfilt.ubsan
│   │   │   ├── cxxfilt.xsan-asan
│   │   │   ├── cxxfilt.xsan-asan-msan
│   │   │   ├── cxxfilt.xsan-asan-msan-tsan
│   │   │   ├── cxxfilt.xsan-asan-msan-tsan-ubsan
│   │   │   ├── cxxfilt.xsan-asan-msan-ubsan
│   │   │   ├── cxxfilt.xsan-asan-tsan
│   │   │   ├── cxxfilt.xsan-asan-tsan-ubsan
│   │   │   └── cxxfilt.xsan-asan-ubsan
│   │   ├── objcopy
│   │   │   ├── objcopy.asan
│   │   │   ├── objcopy.msan
│   │   │   ├── objcopy.raw
│   │   │   ├── objcopy.tsan
│   │   │   ├── objcopy.ubsan
│   │   │   ├── objcopy.xsan-asan
│   │   │   ├── objcopy.xsan-asan-msan
│   │   │   ├── objcopy.xsan-asan-msan-tsan
│   │   │   ├── objcopy.xsan-asan-msan-tsan-ubsan
│   │   │   ├── objcopy.xsan-asan-msan-ubsan
│   │   │   ├── objcopy.xsan-asan-tsan
│   │   │   ├── objcopy.xsan-asan-tsan-ubsan
│   │   │   └── objcopy.xsan-asan-ubsan
│   │   ├── objdump
│   │   │   ├── objdump.asan
│   │   │   ├── objdump.msan
│   │   │   ├── objdump.raw
│   │   │   ├── objdump.tsan
│   │   │   ├── objdump.ubsan
│   │   │   ├── objdump.xsan-asan
│   │   │   ├── objdump.xsan-asan-msan
│   │   │   ├── objdump.xsan-asan-msan-tsan
│   │   │   ├── objdump.xsan-asan-msan-tsan-ubsan
│   │   │   ├── objdump.xsan-asan-msan-ubsan
│   │   │   ├── objdump.xsan-asan-tsan
│   │   │   ├── objdump.xsan-asan-tsan-ubsan
│   │   │   └── objdump.xsan-asan-ubsan
│   │   ├── readelf
│   │   │   ├── readelf.asan
│   │   │   ├── readelf.msan
│   │   │   ├── readelf.raw
│   │   │   ├── readelf.tsan
│   │   │   ├── readelf.ubsan
│   │   │   ├── readelf.xsan-asan
│   │   │   ├── readelf.xsan-asan-msan
│   │   │   ├── readelf.xsan-asan-msan-tsan
│   │   │   ├── readelf.xsan-asan-msan-tsan-ubsan
│   │   │   ├── readelf.xsan-asan-msan-ubsan
│   │   │   ├── readelf.xsan-asan-tsan
│   │   │   ├── readelf.xsan-asan-tsan-ubsan
│   │   │   └── readelf.xsan-asan-ubsan
│   │   ├── size
│   │   │   ├── size.asan
│   │   │   ├── size.msan
│   │   │   ├── size.raw
│   │   │   ├── size.tsan
│   │   │   ├── size.ubsan
│   │   │   ├── size.xsan-asan
│   │   │   ├── size.xsan-asan-msan
│   │   │   ├── size.xsan-asan-msan-tsan
│   │   │   ├── size.xsan-asan-msan-tsan-ubsan
│   │   │   ├── size.xsan-asan-msan-ubsan
│   │   │   ├── size.xsan-asan-tsan
│   │   │   ├── size.xsan-asan-tsan-ubsan
│   │   │   └── size.xsan-asan-ubsan
│   │   ├── strings
│   │   │   ├── strings.asan
│   │   │   ├── strings.msan
│   │   │   ├── strings.raw
│   │   │   ├── strings.tsan
│   │   │   ├── strings.ubsan
│   │   │   ├── strings.xsan-asan
│   │   │   ├── strings.xsan-asan-msan
│   │   │   ├── strings.xsan-asan-msan-tsan
│   │   │   ├── strings.xsan-asan-msan-tsan-ubsan
│   │   │   ├── strings.xsan-asan-msan-ubsan
│   │   │   ├── strings.xsan-asan-tsan
│   │   │   ├── strings.xsan-asan-tsan-ubsan
│   │   │   └── strings.xsan-asan-ubsan
│   │   └── strip-new
│   │       ├── strip-new.asan
│   │       ├── strip-new.msan
│   │       ├── strip-new.raw
│   │       ├── strip-new.tsan
│   │       ├── strip-new.ubsan
│   │       ├── strip-new.xsan-asan
│   │       ├── strip-new.xsan-asan-msan
│   │       ├── strip-new.xsan-asan-msan-tsan
│   │       ├── strip-new.xsan-asan-msan-tsan-ubsan
│   │       ├── strip-new.xsan-asan-msan-ubsan
│   │       ├── strip-new.xsan-asan-tsan
│   │       ├── strip-new.xsan-asan-tsan-ubsan
│   │       └── strip-new.xsan-asan-ubsan
│   ├── compile.sh
│   ├── fetch.sh
│   ├── repo
│   └── temp
├── activate_compile_flags.sh
├── compile-all.sh
├── ...
└── README.md

```