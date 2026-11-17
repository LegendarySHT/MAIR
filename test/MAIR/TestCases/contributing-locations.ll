; RUN: mair-opt -passes="simplifygraph,locationmerge" %s | FileCheck %s
; RUN: mair-opt -passes="simplifygraph,locationmerge" --disable-output %s

; Retain contributing locations after merges.
;
; A merged observation stands for several source accesses, but the check it
; becomes carries one !dbg -- the one of the instruction the check is anchored
; at. That is the right thing to put in an Instruction's !dbg (a second one
; would only overwrite the first), so the rest of the list is kept beside it:
; Observation::getContributingDebugLocs() answers with every distinct location
; its contributing instructions carry, and the dump prints them after the
; region whenever a merge collected more than one.
;
; Nothing is stored. A merged observation already keeps every contributing
; instruction in Insts, so the list is derived from them: a per-observation
; cache would be paid for by every observation the graph builds, including the
; ones that never merge, for information only the merged ones can use.
;
; Two bounds keep the list honest:
;   - duplicates are dropped, because instructions merged from one source line
;     share one uniqued DILocation (see same_line below);
;   - the list stops at MaxContributingLocations = 4. A merged observation can
;     stand for any number of accesses, so an uncapped list would grow with the
;     merge tree; four is where the check has become "a wide access over there"
;     and the remaining lines stop adding information.
;
; This is a graph-level test because the list only exists while the graph does:
; instrumentMAIRObservation consumes it to pick the anchor, and the emitted
; check keeps the single !dbg that the anchor gives it.

define void @three_fields(ptr %s) !dbg !4 {
entry:
  %a = load i32, ptr %s, align 4, !dbg !5
  %s4 = getelementptr i8, ptr %s, i64 4
  %b = load i32, ptr %s4, align 4, !dbg !6
  %s8 = getelementptr i8, ptr %s, i64 8
  %c = load i32, ptr %s8, align 4, !dbg !7
  ret void, !dbg !8
}

; The three loads sit at consecutive offsets of one object, so location merging
; folds them into one 12-byte observation, and the dump names all three lines in
; the order the contributing instructions were collected.

; CHECK-LABEL: mair graph of fn @three_fields {
; CHECK: o: read ptr %s, 12 [loc 3:10 4:10 5:10]

define void @five_fields(ptr %s) !dbg !10 {
entry:
  %a = load i32, ptr %s, align 4, !dbg !11
  %p4 = getelementptr i8, ptr %s, i64 4
  %b = load i32, ptr %p4, align 4, !dbg !12
  %p8 = getelementptr i8, ptr %s, i64 8
  %c = load i32, ptr %p8, align 4, !dbg !13
  %p12 = getelementptr i8, ptr %s, i64 12
  %d = load i32, ptr %p12, align 4, !dbg !14
  %p16 = getelementptr i8, ptr %s, i64 16
  %e = load i32, ptr %p16, align 4, !dbg !15
  ret void, !dbg !16
}

; Five sources, four lines reported: the fifth is dropped rather than growing
; the metadata without adding anything a reader would use.

; CHECK-LABEL: mair graph of fn @five_fields {
; CHECK: o: read ptr %s, 20 [loc 9:10 10:10 11:10 12:10]
; CHECK-NOT: 13:10

define void @same_line(ptr %s) !dbg !18 {
entry:
  %a = load i32, ptr %s, align 4, !dbg !19
  %s4 = getelementptr i8, ptr %s, i64 4
  %b = load i32, ptr %s4, align 4, !dbg !19
  ret void, !dbg !20
}

; Two loads from one source line share one DILocation. The merge still happens,
; but it contributes a single distinct location, so the dump stays quiet: the
; suffix only appears when a merge really spans several lines.

; CHECK-LABEL: mair graph of fn @same_line {
; CHECK: o: read ptr %s, 8{{$}}

define void @one_field(ptr %s) !dbg !22 {
entry:
  %a = load i32, ptr %s, align 4, !dbg !23
  ret void, !dbg !24
}

; A single-source observation has nothing to contribute beyond the !dbg the
; check already carries, so its line is unchanged from before this feature.

; CHECK-LABEL: mair graph of fn @one_field {
; CHECK: o: read ptr %s, 4{{$}}

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug)
!1 = !DIFile(filename: "contributing-locations.c", directory: "/src")
!2 = !{i32 7, !"Dwarf Version", i32 5}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!4 = distinct !DISubprogram(name: "three_fields", scope: !1, file: !1, line: 2, spFlags: DISPFlagDefinition, unit: !0)
!5 = !DILocation(line: 3, column: 10, scope: !4)
!6 = !DILocation(line: 4, column: 10, scope: !4)
!7 = !DILocation(line: 5, column: 10, scope: !4)
!8 = !DILocation(line: 6, column: 3, scope: !4)
!10 = distinct !DISubprogram(name: "five_fields", scope: !1, file: !1, line: 8, spFlags: DISPFlagDefinition, unit: !0)
!11 = !DILocation(line: 9, column: 10, scope: !10)
!12 = !DILocation(line: 10, column: 10, scope: !10)
!13 = !DILocation(line: 11, column: 10, scope: !10)
!14 = !DILocation(line: 12, column: 10, scope: !10)
!15 = !DILocation(line: 13, column: 10, scope: !10)
!16 = !DILocation(line: 14, column: 3, scope: !10)
!18 = distinct !DISubprogram(name: "same_line", scope: !1, file: !1, line: 16, spFlags: DISPFlagDefinition, unit: !0)
!19 = !DILocation(line: 17, column: 10, scope: !18)
!20 = !DILocation(line: 18, column: 3, scope: !18)
!22 = distinct !DISubprogram(name: "one_field", scope: !1, file: !1, line: 20, spFlags: DISPFlagDefinition, unit: !0)
!23 = !DILocation(line: 21, column: 10, scope: !22)
!24 = !DILocation(line: 22, column: 3, scope: !22)
