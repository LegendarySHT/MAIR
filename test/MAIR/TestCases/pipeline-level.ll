; RUN: mair-opt -mair-level=build %s | FileCheck %s --check-prefix=BUILD
; RUN: mair-opt -mair-level=optimize %s | FileCheck %s --check-prefix=OPT
; RUN: mair-opt -mair-level=build -passes="simplifygraph,locationmerge" %s | FileCheck %s --check-prefix=OPT
; RUN: not mair-opt -mair-level=fast %s 2>&1 | FileCheck %s --check-prefix=BAD

; -mair-level=build keeps the graph exactly as constructed: three adjacent
; loads stay three observations. -mair-level=optimize runs the standard O1
; pipeline, which contracts the dominating/post-dominating chain and then
; merges the three loads into one 12-byte read. Explicit -passes names the
; pipeline and therefore outranks -mair-level.

; BUILD-LABEL: mair graph of fn @adjacent {
; BUILD-COUNT-3: o: read ptr %p
; BUILD-NOT:     o: read ptr %p, 12

; OPT-LABEL: mair graph of fn @adjacent {
; OPT:       o: read ptr %p, 12
; OPT-NOT:   o: read ptr %p +

; BAD: -mair-level expects 'build' or 'optimize', not 'fast'

define void @adjacent(ptr %p) {
entry:
  %p8 = getelementptr i8, ptr %p, i64 8
  %v8 = load i32, ptr %p8, align 4
  %v0 = load i32, ptr %p, align 4
  %p4 = getelementptr i8, ptr %p, i64 4
  %v4 = load i32, ptr %p4, align 4
  ret void
}
