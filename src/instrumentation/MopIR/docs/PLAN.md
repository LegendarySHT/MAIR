# 背景

Sanitizer（消毒器）是一种动态程序分析手段，在实际程序执行中监控程序执行状态，收集并分析信息，以报告可能的程序缺陷。

- ASan是最主流的Sanitizer，通过插桩内存操作来检测内存错误。
- TSan 同样对内存操作进行插桩，进而检测数据静态

目前，存在一些对ASan插桩操作的优化

- ASan—
- GiantSan
- Tech-ASan

然而，这些优化都是

- 直接内嵌在ASan的插桩代码里的：可拓展不强，无法轻易适配其它基于内存操作的Sanitizer（如TSan）
- 彼此是相互隔离、独立进行的：在LLVM优化中，一个优化往往可以触发更多优化机会（如内联），目前这种独立的优化的优化效果有限
- 基于规则+模式匹配，难以处理比较复杂的模式，也难以拓展新的优化模式

# 想法：统一的内存操作抽象 + 相关优化

能否设计一个专门针对内存操作的抽象IR （MOP），先在LLVM IR之上进行 MOP的抽象，得到MOP抽象后，再在MOP IR上进行优化流水线。

在该MOP抽象上实施优化，尽可能在语义不变的情况下，消除MOP IR

比如在合适情况下， `mop1 = ((ptr, 4), read)`  和 `mop2 = ((ptr + 4, 4), read)` 可以合并为 `mop = ((ptr, 8), read)`

- 基于MOP抽象的优化可以轻易为所有基于MOP的sanitizer提供优化帮助
- 基于MOP IR，可以设计类似LLVM IR的优化流水线，让不同优化收益于彼此
- 基于MOP IR，抹除了无关的语义，相关的优化匹配规则可以大幅简化，可以更好处理复杂情形和进行优化模式拓展

## 初步设计如下优化Pass

令 `MOP = (range, read | write)`,

- [ ]  去除赘余检查： 令检查的内存操作为  则MOP1对与MOP2检查赘余的条件为 （该条件成立，则MOP2的检查可以消除MOP1的检查）
    1. range1 $\subset$ range2
    2. (MOP2  $\mathrm{ dom }$  MOP1 ) $\lor$ ( MOP2  $\mathrm{ pdom }$  MOP1 )
    3. MOP2 到 MOP1 之间的任何路径不存在任何危险指令 （需要数据流静态程序分析）
        - 对 ASan：危险指令为可能包含 `free` 的函数调用
        - 对 TSan：危险指令为 可能包含同步操作的函数调用 / 携带内存序的IR指令
    4. TSAN：写可以覆盖读；读不能覆盖写
    
    ```cpp
    // MOP2 可以消除 MOP1
    if (a)
      // MOP1
      a = 2;
    // MOP2
    a = 3;
    
    ```
    
    ```cpp
    // ASAN: MOP2 可以消除 MOP1
    // TSAN: 无法消除
    if (a) {
      // MOP1
      a = 2;
      // happens-before : release -> acquire
      memory_fence(memory_order_acquire);
    }
    // MOP2
    a = 3;
    ```
    
- [ ]  检查 hoist / sink, 若分支中所有检查的对象一致，可以将该检查提升至共同 dominator或者延后至共同post-dominator;
    - 见下
        
        
        ```cpp
        if (..) {
          check(p)
          *p = 10;
        } else {
          check(p)
          _ = *p;
        }
        ```
        
        ```cpp
        check(p)
        if (..) {
          *p = 10;
        } else {
          _ = *p;
        }
        
        ```
        
    - 这样可以方便其它优化的进行
    - 若考虑延后，则需要考虑 SEGV的问题。
- [ ]  合并邻近检查:
    - 见下
        
        
        ```cpp
        char *p = ...;
        check1(p);
        p[0] = 1;
        check1(p+1);
        p[1] = 2;
        ```
        
        ```cpp
        char *p = ...;
        check(p, p+2);
        p[0] = 1;
        p[1] = 2;
        ```
        
- [ ]  对循环进行优化：1）将不变检查提升（hoist） 2）合并为区间检查
    - 合并为区间检查，见下
        - 可以使用 `SCEV` 分析来判断指针如何演进。
        - 四种类型
            - Range  =  `(L, R)`：访问 $[L,R)$
            - Period = `(L, S, A, N)` : 以步幅 $S$ 从 $L$ 开始，步进 $N$ 次，每次访问 $A$ bytes
            - Cyclic = `(L, R, B, E)` : 访问 $[B, R)$ 和 $[L, E)$, 即 循环溢出的情况
            - Cyclic Period = `(L, R, B, E,`
        - 目前仅处理简单的case，即循环内 load/store 不在分支中的情况 + 简单的单入口单出口循环
        
        ```cpp
            while ((check_read(str),
                   *str != 0)) { /* non input consuming */
                str++;
                ...
            }
        
        ```
        
        ```cpp
        str_ = str;
        while (*str != 0) { /* non input consuming */
            str++;
            ...
        }
        check_read(str_, str - str_)
        ```
        
    - 不变检查hoist/sink，见下
        
        
        ```cpp
        while ((check_read(p), *p)) {
          ...
        } 
        ```
        
        ```cpp
        check_read(p);
        while (*p) {
          ..
        }
        ```
        
- [ ]  不可满足的检查消除
    - [ ]  ASan中特定分支下的数组访问
    - [ ]  TSan中对栈变量的访问（若可以证实栈变量地址不会流向线程，那就可以不对栈变量作检查插桩。

# 实验

评估有效性（Sanitizer自带的回归测试用例 + CVE集合） + 评估性能（CPU SPEC 2006)