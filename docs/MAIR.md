# MAIR 使用说明

MAIR 是接在 sanitizer 插桩里的一层优化：把同一个函数里多条内存访问的驻留检查看成**图上的观测**，然后合并、移动、消除它们，减少检查条数。

这一页只讲怎么用。设计细节在 `docs/dev-MAIR/`，循环层次在 `docs/mair-loops.md`。

下面所有命令都用 `build-llvm22/` 指代构建目录，换成你自己的那个即可。

## 0. 先决：构建目录得选对 LLVM

MAIR 有没有被编进来、以及是否**生效**，都取决于构建用的 LLVM 版本（`src/cmake/ConfigLLVMCompiler.cmake` 里的 `XSAN_HAS_MAIR` 与后端选择）：

| `LLVM_VERSION_MAJOR` | 后端目录 | 有 MAIR |
|---|---|---|
| 15 | `llvm15/` | 否 |
| ≥ 22 | `llvm23/` | 是 |

15 这一档不含 MAIR：MAIR 的源码依赖 LLVM 22 的 API，所以它在配置阶段就被整个跳过——`add_subdirectory(MAIR)`、`XSAN_PASS_COMMON` 里的 `MAIRCore` 对象、`test/MAIR` 一起消失，只建出 `llvm15/` 那条后端。编译器照常可用，`mair-opt` 和 `check-mair` 不存在。

所以要用 LLVM 22+ 配置（其余步骤见 README 的 *Build From Source*）。注意**宿主编译器也必须是 clang-22**，否则 `ConfigLLVMCompiler.cmake` 会直接报错让你改：

```bash
mkdir -p build-llvm22 && cd build-llvm22
CC=/usr/lib/llvm-22/bin/clang CXX=/usr/lib/llvm-22/bin/clang++ \
cmake -G Ninja \
      -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DLLVM_DIR=/usr/lib/llvm-22/lib/cmake/llvm \
      -DClang_DIR=/usr/lib/llvm-22/lib/cmake/clang ..
ninja
```

想要断言帮忙抓问题就把 `CMAKE_BUILD_TYPE` 换成 `Debug`（见 §6 第 4 条），代价是全量重编。

编完先确认 MAIR 真的在里面，再去谈效果：

```bash
ls build-llvm22/mair-opt build-llvm22/pass/ASanInstPass.so
ninja -C build-llvm22 check-mair     # 178 gtest + 26 lit
```

### 不想装 LLVM：用 Docker

仓库根的 `Dockerfile` 能直接出一个编好的镜像，默认 LLVM 22 + Debug，`mair-opt` 在 `/workspace/build`：

```bash
docker build -t xsan:mair-22 .
docker run --rm xsan:mair-22 bash -lc 'mair-opt --help | head -5'
```

镜像约 3.1 GB（Debug 全量 + 工具链），构建约 5 分钟。测试也能在镜像里直接跑，不用回宿主装东西：

```bash
docker run --rm xsan:mair-22 bash -lc 'ninja -C build check-mair'   # 178 gtest + 26 lit
```

`LLVM_VERSION` 是唯一需要传的参数，但它不是"随便挑一个版本"——和上表同一个道理，只是坑更硬：

| 值 | 结果 |
|---|---|
| `22`（默认） | 正常，走 `llvm23/` 后端，有 MAIR |
| `15` | 能编过，走 `llvm15/` 后端，**镜像里没有 MAIR**（构建阶段整体跳过） |
| `23` | 配置能过，编译失败：`llvm23/` 目录实际对齐的是 LLVM 22 的 API（见 §4） |
| `16`–`21` | 配置阶段就 FATAL，`ConfigLLVMCompiler.cmake` 只接受 15 或 22+ |

`15` 那条腿能编过，靠的是 `XSAN_HAS_MAIR`：`MAIR/` 只在 LLVM 22+ 时才 `add_subdirectory` 进去，`XSAN_PASS_COMMON` 里的 `MAIRCore` 对象也随之去掉，`test/MAIR` 与 `check-mair` 一起消失。所以拿 15 的镜像跑 `mair-opt` 会报 command not found——那是设计如此，不是装坏了。

```bash
docker build --build-arg BUILD_TYPE=RelWithDebInfo -t xsan:mair-22 .     # 要跑起来快一点
```

两个常见卡点：

- **拉包慢**：基础镜像可换，`--build-arg UBUNTU_IMAGE=docker.1panel.live/library/ubuntu:22.04`。
- **apt.llvm.org 慢**：给构建过程传代理，`--build-arg HTTPS_PROXY=http://host.docker.internal:7890`（Docker Desktop 下容器内用 `host.docker.internal` 指向宿主）。

## 1. 它做什么

传统插桩是"一条访问一条检查"，每个检查都带着自己的地址计算和 shadow 查询。MAIR 把观测构造成图之后做四类事：合并、移动、消除、化简。

| 变换 | pass | 干什么 |
|---|---|---|
| 位置合并 | `locationmerge` | 同一程序点上相邻/重叠的访问合成一条更宽的检查 |
| 公共观测移动 | `com` | 多个程序点重复的同一观测上提到共同支配点，只留一条 |
| 循环不变 / 周期合并 | `liom` / `lpoc` | 循环里每轮都成立的检查提到 preheader；等距访问合成周期模式 |
| 冗余消除 | `roe` | 已被支配检查覆盖的观测去掉 |
| 图化简 | `simplifygraph` | 收掉被搬空观测的程序点，同时为上述变换创造同点机会 |

效果实例（`test/asan/TestCases/mair-merged-range.cpp`，`s->a + s->b + s->c` 三个相邻 4 字节读）：

```llvm
; 传统插桩：三条各自查 4 字节
call void @__asan_report_load4(i64 <s>)
call void @__asan_report_load4(i64 <s+4>)
call void @__asan_report_load4(i64 <s+8>)

; MAIR：合成一条 12 字节区间检查。12 不是 2 的幂，ASan 侧探首尾两个字节，
; 所以落下来是两条调用，而不是一条。
call void @__asan_report_load_n(i64 <s>, i64 12)
call void @__asan_report_load_n(i64 <s+11>, i64 12)
```

**一条硬约束**：MAIR 只是优化，不是正确性要求。图建不出来、pass 产出后端拒绝的图、观测无法降级成具体调用——任一失败都退回传统逐访问插桩，代价只是没有优化收益（A4 / B4）。所以 `MAIR=1` 编出来的程序，被检查的字节数不允许下降；这是 `test-e2e/regression/compare-checks.sh` 守的那条线。

## 2. 怎么开

MAIR 是**编译器进程的环境变量**，不是 CFLAGS，也不是目标程序的运行期开关：

```bash
MAIR=1 xclang++ -asan -O1 -mllvm -asan-opt=false -S -emit-llvm foo.cpp -o -
```

| 值 | 行为 |
|---|---|
| 未设置 / 空串 / `0` | 关闭，完全走传统插桩 |
| `build` | 只构图并物化，**不跑任何图变换** |
| `1`（或任何其他非空值） | 完整流水线 |

`build` 这一档是给消融实验用的：它把"框架本身的开销"和"优化带来的收益"分开——两边都不做变换时差值就是构图成本。

上面那个例子的实测（同一份源码，只改环境变量，数 `__asan_report_*` 调用）：

| `MAIR` | 检查条数 |
|---|---|
| 未设置 | 3 |
| `""`（空串） | 3 |
| `0` | 3 |
| `build` | 3 |
| `1` | 2 |

前置条件，缺一个 MAIR 就不会生效：

- 只有 `xclang` / `xclang++` 包装器会挂插件（`-fpass-plugin=<build>/pass/ASanInstPass.so`，TSan 是 `TSanInstPass.so`）。`xgcc` 不走这条路。
- 只有 `-asan` / `-tsan` 的插桩路径调用了 MAIR。`-msan` / `-ubsan` 不受影响。
- 只有 "llvm23" 这一份后端有 MAIR：`src/cmake/ConfigLLVMCompiler.cmake` 对 LLVM 22+ 选它，**目录名比它实际编译的 LLVM 大一个版本**——它编的是 **LLVM 22**。`src/instrumentation/llvm15/` 里没有 MAIR。
- ASan 与 TSan 用的都是 **O1 流水线**（`KeepFullDebug=false`）。Og 只在 `mair-opt -Og` 里可选。

## 3. `mair-opt`：离线看 MAIR 在图上的样子

`mair-opt` 拿 `.ll` 当输入，只跑 MAIR 的图变换，用于调试和对比，不参与正常编译。

```bash
# 默认只构图、按程序点 dump 观测，不做任何变换
build-llvm22/mair-opt test/MAIR/TestCases/locationmerge.ll

# 指定 pass，逗号分隔，顺序就是执行顺序
build-llvm22/mair-opt -passes="simplifygraph,locationmerge" test/MAIR/TestCases/locationmerge.ll

# 两条预置流水线，与后端里那两条一一对应
build-llvm22/mair-opt -Og foo.ll    # 保守：循环相关 pass 不开，不合并等价观测
build-llvm22/mair-opt -O1 foo.ll    # ASan/TSan 实际用的那条

# 统计：程序和观测各多少（打到 stderr，所以重定向 stdout）
build-llvm22/mair-opt -passes="print<stat>" foo.ll 2>&1 >/dev/null

# 画图：只接受单函数模块（多函数会报 "expects exactly one function definition"）
build-llvm22/mair-opt -passes="simplifygraph,locationmerge" -emit-dot f.ll | dot -Tpdf > graph.pdf
```

`-emit-dot` 本身只吐 DOT 文本，渲染要靠 graphviz（本仓库不依赖它，没装的话直接看文本也够用）。一个两读的例子：

```llvm
define void @f(ptr %p) {
  %a = load i32, ptr %p          ; 传统：o: read ptr %p, 4
  %q = getelementptr i8, ptr %p, i64 4
  %b = load i32, ptr %q          ;       o: read ptr %p + 4, 4
  ret void
}
```

不带 pipeline 时图上两个观测；加 `-passes="simplifygraph,locationmerge"` 之后变成一条 `o: read ptr %p, 8`。

`-passes` 可用的名字：

| 名字 | 说明 |
|---|---|
| `simplifygraph` | 图化简 |
| `locationmerge` | 位置合并 |
| `com` | 公共观测移动 |
| `liom` | 循环不变观测移动 |
| `lpoc` | 循环周期观测合并 |
| `roe` | 冗余观测消除 |
| `print<stat>` | 打印统计 |
| `default<Og>` / `default<O1>` | 直接展开成预置流水线 |

其他开关：`-o FILE`、`-emit-llvm`（把结果写成带 MAIR 标注的 IR）、`-emit-dot`、`-disable-output`、`-mair-level=build|optimize`（默认 `build`；`-passes` / `-Og` / `-O1` 优先于它）。

> 默认 `-mair-level=build` 意味着**不加任何 pipeline 参数时图是未优化的**。第一次跑容易误以为"MAIR 什么都没做"，其实是你没让它做。

## 4. 当前限制（诚实版）

| 限制 | 表现 / 原因 |
|---|---|
| 只在 ASan / TSan 的 "llvm23" 后端 | `llvm15/` 后端没有 MAIR；`-msan` / `-ubsan` 不受影响 |
| 只有连续的观测能合并 | 非连续、volatile、call-like、地址算不出确定区间的观测直接退传统插桩 |
| 合并观测要求每个来源本身可插桩 | 任一来源不合格（向量/可伸缩向量类型、一条指令多个访存、被 ASan 自身的优化消除、`-as-debug-min` 与 `-as-debug-max` 同时给出），整条观测退回逐来源插桩 |
| 别名合并只认 MustAlias | `MAIRAliasAnalysis` 只回答"两者是同一个地址"；May / Partial 一律不合并（C3/C4） |
| 不可归约环直接跳过整个函数 | 不能用 `LoopInfo` 表示的环，构图拒绝、函数走传统插桩，不会中止编译（C2） |
| TSan 非标准宽度 | 1/2/4/8/16 之外的宽度走 `__tsan_read_range` / `__tsan_write_range`，而不是被静默丢弃（B3） |
| 合并后的检查只有一个源码位置 | `Instruction` 只装得下一条 `!dbg`。贡献位置只存在于图侧 dump（`[loc 3:10 4:10]`），不进 IR（C5） |
| 少数并发用例的报告形态与不带 MAIR 时不同 | TSan 全库强制 `MAIR=1` 有 8 条已知差异（合并后一次访问覆盖更大区间，报告的行/列随之变化），不是漏检 |

## 5. 测试怎么跑

```bash
# 一次跑完 MAIR 自己的 gtest + lit（178 + 26）
ninja -C build-llvm22 check-mair

# 单条 lit
lit -sv build-llvm22/test/MAIR --filter=locationmerge
lit -sv build-llvm22/test/asan/X86_64LinuxConfig --filter=mair-
lit -sv build-llvm22/test/tsan/X86_64Config --filter=mair-

# 能力回归：MAIR=1 是否曾经少检查了字节（D1 的门禁）
test-e2e/regression/compare-checks.sh --suite all --out /tmp/checks.tsv --fail-on-missing
```

**注意套件目录名不统一**：asan 是 `X86_64LinuxConfig`、tsan 是 `X86_64Config`、msan 是 `X86_64`，直接 `lit build-llvm22/test/asan` 会报 "contained no tests"。

看门禁看的是**覆盖字节**而不是调用条数：合并本来就会让调用数合法下降，所以 `--fail-on-missing` 比 `--fail-on-call-count` 更适合当 gate。

想把整个 ASan/TSan 套件都压在 `MAIR=1` 下跑（相当于全库回归），往站点配置追加一行再跑整套；两个套件各来一次：

```bash
for d in asan/X86_64LinuxConfig tsan/X86_64Config; do
  printf '\nconfig.environment["MAIR"] = "1"\n' >> build-llvm22/test/$d/lit.site.cfg.py
  lit -s build-llvm22/test/$d
done
```

当前的期望值是 ASan 全绿、TSan 只有上面那 8 条已知差异。**跑完记得把这一行删掉**，它是构建产物，不是源码。

## 6. 已知坑

1. **必须 `-mllvm -asan-opt=false` 才看得见合并效果。** ASan 自己也会消除冗余检查，两台优化器同时动手时很难分辨是谁干的。所有 `mair-*` 用例都带这个开关，照抄即可。
2. **`env -u MAIR` 而不是 `env MAIR=`。** 当前实现（`getMAIRLevel()`）把空串当**关闭**，但早于 D2 的脚本/文档是按"变量存在即开启"写的；写"对照 legacy"的用例时用 `-u`，两种语义下都对。
3. **测别名分析的负向用例必须有两个不同的 base。** `%p` 与 `%p+4` 这种同一 base 的常量偏移根本不经过 AA：偏移折进观测自身的 offset，靠 `locationmerge` 就合了。能测 AA 的只有参数、`select`、`phi`、`alloca` 这类独立 base（C4 的教训）。
4. **断言只在 Debug 构建生效。** `RelWithDebInfo` 下 `NDEBUG` 把 `assert` 关掉，C1—C5 期间一个"收缩方向只认一种"的断言 bug 因此一直没暴露，首次 Debug 构建时 15 条 ASan 用例全崩（E1）。改完 `MAIR.h` 记得用 Debug 树重编 gtest 再报数。
5. **手工跑 lit 要 `FileCheck` 在 PATH 里。** E1 之后 `lit.site.cfg.py.in` 会把 `LLVM_TOOLS_BINARY_DIR` 拼进 PATH，但**已经配置好的旧构建目录不会自动更新**；老目录要么重跑 cmake，要么自己 `export PATH=/usr/lib/llvm-22/bin:$PATH`。
6. **`mair-opt` 默认不跑 pipeline**（见 §3 末尾）。看"优化后"的图必须显式给 `-passes` / `-Og` / `-O1`。

## 7. 相关文档

- `docs/dev-MAIR/1-InitMAIRGraph.md`、`2-ConstructMAIRGraph.md`：图模型与构图
- `docs/mair-loops.md`：循环层次与循环专用优化
- `test-e2e/regression/README.md`：能力回归脚本的口径
- `src/instrumentation/MAIR/PassRegistry.def`：pass 名的唯一来源
