# Which Part of TSan is Included?

TSan runtime directory architecture is as follows:
```
- tsan
    - benchmarks    : benchmarks for TSan
    - dd            : standalone deadlock detector runtime
    - go            : designated for Go runtime
    - rtl           : new TSan runtime
    - rtl-old       : old TSan runtime
    - tests         : tests for TSan
```

We only require the `rtl` directory for our purposes, which contains the runtime library for TSan.

- dd: we do not require the standalone deadlock detector runtime.
- go: we now do not support Golang.
- tests: pending to be supported.