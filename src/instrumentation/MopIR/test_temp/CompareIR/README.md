# CompareIR

该测试用于对比同一份 LLVM IR 在两套冗余检查实现下的“保留集合/冗余集合”是否一致：
- 旧实现：`MopRecurrenceReducer::distillRecurringChecks`
- 新实现：`MopIR::MopRedundancyAnalysis`

对比方法：
- 使用 MopIR 构建 MOP 集合（保证两侧分析对象一致）
- Reducer：对同一批 `Instruction*` 运行 `distillRecurringChecks()` 得到保留集合
- MopIR：运行 `MopRedundancyAnalysis`，以“非冗余”的 MOP 为保留集合
- 比较两侧保留集合中 `Instruction*` 是否一致

## 使用

```bash
cd src/instrumentation/MopIR/test_temp/CompareIR
bash ./run_compare.sh
```

脚本会分别以 ASan 语义（默认）和 TSan 语义（`--tsan`）运行，并在每个函数上给出是否一致的结论。

如需单独运行：

```bash
# 构建
mkdir -p build && cd build && cmake .. && make -j$(nproc)
# 对比某个 IR（ASan 语义）
./CompareIR test_simple.ll
# 对比某个 IR（TSan 语义）
./CompareIR test_simple.ll --tsan
```

## 说明
- 测试 IR 复用了同目录上级 `MopIRTest` 中的样例文件。
- MopIR 侧已加入 “interesting MOP” 过滤（忽略 nosanitize、原子）以匹配 Reducer 语义。