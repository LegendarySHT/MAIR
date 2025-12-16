; ModuleID = '/XSan/src/instrumentation/MopIR/test_temp/MopIRTest/test_redundant_complex2.cpp'
source_filename = "/XSan/src/instrumentation/MopIR/test_temp/MopIRTest/test_redundant_complex2.cpp"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%struct.Container = type { [10 x ptr], [200 x i32] }
%struct.Node = type { [100 x i32], ptr }

; Function Attrs: mustprogress noinline nounwind uwtable
define dso_local void @_Z18sideEffectFunctionPii(ptr noundef %ptr, i32 noundef %size) #0 {
entry:
  %ptr.addr = alloca ptr, align 8
  %size.addr = alloca i32, align 4
  %i = alloca i32, align 4
  store ptr %ptr, ptr %ptr.addr, align 8
  store i32 %size, ptr %size.addr, align 4
  store i32 0, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.inc, %entry
  %0 = load i32, ptr %i, align 4
  %1 = load i32, ptr %size.addr, align 4
  %cmp = icmp slt i32 %0, %1
  br i1 %cmp, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %call = call i32 @rand() #7
  %2 = load ptr, ptr %ptr.addr, align 8
  %3 = load i32, ptr %i, align 4
  %idxprom = sext i32 %3 to i64
  %arrayidx = getelementptr inbounds i32, ptr %2, i64 %idxprom
  store i32 %call, ptr %arrayidx, align 4
  br label %for.inc

for.inc:                                          ; preds = %for.body
  %4 = load i32, ptr %i, align 4
  %inc = add nsw i32 %4, 1
  store i32 %inc, ptr %i, align 4
  br label %for.cond, !llvm.loop !6

for.end:                                          ; preds = %for.cond
  ret void
}

; Function Attrs: nounwind
declare i32 @rand() #1

; Function Attrs: mustprogress noinline nounwind uwtable
define dso_local noundef i32 @_Z12pureFunctionii(i32 noundef %a, i32 noundef %b) #0 {
entry:
  %a.addr = alloca i32, align 4
  %b.addr = alloca i32, align 4
  store i32 %a, ptr %a.addr, align 4
  store i32 %b, ptr %b.addr, align 4
  %0 = load i32, ptr %a.addr, align 4
  %1 = load i32, ptr %b.addr, align 4
  %add = add nsw i32 %0, %1
  ret i32 %add
}

; Function Attrs: mustprogress noinline uwtable
define dso_local void @_Z23testComplexOptimizationP9ContainerPii(ptr noundef %container, ptr noundef %externalBuffer, i32 noundef %n) #2 {
entry:
  %container.addr = alloca ptr, align 8
  %externalBuffer.addr = alloca ptr, align 8
  %n.addr = alloca i32, align 4
  %ptr1 = alloca ptr, align 8
  %i = alloca i32, align 4
  %i1 = alloca i32, align 4
  %val = alloca i32, align 4
  %current = alloca ptr, align 8
  %i14 = alloca i32, align 4
  %i23 = alloca i32, align 4
  %current37 = alloca ptr, align 8
  %i40 = alloca i32, align 4
  %finalNode = alloca ptr, align 8
  %i52 = alloca i32, align 4
  %outer = alloca i32, align 4
  %node = alloca ptr, align 8
  %i68 = alloca i32, align 4
  %inner = alloca i32, align 4
  %i82 = alloca i32, align 4
  %offset = alloca i32, align 4
  %i97 = alloca i32, align 4
  %tsanBuffer = alloca ptr, align 8
  %i118 = alloca i32, align 4
  %sum = alloca i32, align 4
  %i127 = alloca i32, align 4
  %i137 = alloca i32, align 4
  %alias1 = alloca ptr, align 8
  %alias2 = alloca ptr, align 8
  %i153 = alloca i32, align 4
  %i163 = alloca i32, align 4
  %i175 = alloca i32, align 4
  store ptr %container, ptr %container.addr, align 8
  store ptr %externalBuffer, ptr %externalBuffer.addr, align 8
  store i32 %n, ptr %n.addr, align 4
  %0 = load ptr, ptr %container.addr, align 8
  %buffer = getelementptr inbounds %struct.Container, ptr %0, i32 0, i32 1
  %arraydecay = getelementptr inbounds [200 x i32], ptr %buffer, i64 0, i64 0
  store ptr %arraydecay, ptr %ptr1, align 8
  store i32 0, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.inc, %entry
  %1 = load i32, ptr %i, align 4
  %cmp = icmp slt i32 %1, 100
  br i1 %cmp, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %2 = load i32, ptr %i, align 4
  %mul = mul nsw i32 %2, 2
  %3 = load ptr, ptr %ptr1, align 8
  %4 = load i32, ptr %i, align 4
  %idxprom = sext i32 %4 to i64
  %arrayidx = getelementptr inbounds i32, ptr %3, i64 %idxprom
  store i32 %mul, ptr %arrayidx, align 4
  br label %for.inc

for.inc:                                          ; preds = %for.body
  %5 = load i32, ptr %i, align 4
  %inc = add nsw i32 %5, 1
  store i32 %inc, ptr %i, align 4
  br label %for.cond, !llvm.loop !8

for.end:                                          ; preds = %for.cond
  store i32 10, ptr %i1, align 4
  br label %for.cond2

for.cond2:                                        ; preds = %for.inc9, %for.end
  %6 = load i32, ptr %i1, align 4
  %cmp3 = icmp slt i32 %6, 30
  br i1 %cmp3, label %for.body4, label %for.end11

for.body4:                                        ; preds = %for.cond2
  %7 = load ptr, ptr %ptr1, align 8
  %8 = load i32, ptr %i1, align 4
  %idxprom5 = sext i32 %8 to i64
  %arrayidx6 = getelementptr inbounds i32, ptr %7, i64 %idxprom5
  %9 = load i32, ptr %arrayidx6, align 4
  store i32 %9, ptr %val, align 4
  %10 = load i32, ptr %val, align 4
  %call = call noundef i32 @_Z12pureFunctionii(i32 noundef %10, i32 noundef 1)
  %11 = load ptr, ptr %ptr1, align 8
  %12 = load i32, ptr %i1, align 4
  %idxprom7 = sext i32 %12 to i64
  %arrayidx8 = getelementptr inbounds i32, ptr %11, i64 %idxprom7
  store i32 %call, ptr %arrayidx8, align 4
  br label %for.inc9

for.inc9:                                         ; preds = %for.body4
  %13 = load i32, ptr %i1, align 4
  %inc10 = add nsw i32 %13, 1
  store i32 %inc10, ptr %i1, align 4
  br label %for.cond2, !llvm.loop !9

for.end11:                                        ; preds = %for.cond2
  %14 = load i32, ptr %n.addr, align 4
  %cmp12 = icmp sgt i32 %14, 0
  br i1 %cmp12, label %if.then, label %if.else

if.then:                                          ; preds = %for.end11
  %15 = load ptr, ptr %container.addr, align 8
  %nodes = getelementptr inbounds %struct.Container, ptr %15, i32 0, i32 0
  %arrayidx13 = getelementptr inbounds [10 x ptr], ptr %nodes, i64 0, i64 0
  %16 = load ptr, ptr %arrayidx13, align 8
  store ptr %16, ptr %current, align 8
  store i32 0, ptr %i14, align 4
  br label %for.cond15

for.cond15:                                       ; preds = %for.inc20, %if.then
  %17 = load i32, ptr %i14, align 4
  %cmp16 = icmp slt i32 %17, 50
  br i1 %cmp16, label %for.body17, label %for.end22

for.body17:                                       ; preds = %for.cond15
  %18 = load i32, ptr %i14, align 4
  %19 = load ptr, ptr %current, align 8
  %data = getelementptr inbounds %struct.Node, ptr %19, i32 0, i32 0
  %20 = load i32, ptr %i14, align 4
  %idxprom18 = sext i32 %20 to i64
  %arrayidx19 = getelementptr inbounds [100 x i32], ptr %data, i64 0, i64 %idxprom18
  store i32 %18, ptr %arrayidx19, align 4
  br label %for.inc20

for.inc20:                                        ; preds = %for.body17
  %21 = load i32, ptr %i14, align 4
  %inc21 = add nsw i32 %21, 1
  store i32 %inc21, ptr %i14, align 4
  br label %for.cond15, !llvm.loop !10

for.end22:                                        ; preds = %for.cond15
  %22 = load ptr, ptr %externalBuffer.addr, align 8
  call void @_Z18sideEffectFunctionPii(ptr noundef %22, i32 noundef 20)
  store i32 10, ptr %i23, align 4
  br label %for.cond24

for.cond24:                                       ; preds = %for.inc34, %for.end22
  %23 = load i32, ptr %i23, align 4
  %cmp25 = icmp slt i32 %23, 20
  br i1 %cmp25, label %for.body26, label %for.end36

for.body26:                                       ; preds = %for.cond24
  %24 = load ptr, ptr %current, align 8
  %data27 = getelementptr inbounds %struct.Node, ptr %24, i32 0, i32 0
  %25 = load i32, ptr %i23, align 4
  %idxprom28 = sext i32 %25 to i64
  %arrayidx29 = getelementptr inbounds [100 x i32], ptr %data27, i64 0, i64 %idxprom28
  %26 = load i32, ptr %arrayidx29, align 4
  %mul30 = mul nsw i32 %26, 2
  %27 = load ptr, ptr %current, align 8
  %data31 = getelementptr inbounds %struct.Node, ptr %27, i32 0, i32 0
  %28 = load i32, ptr %i23, align 4
  %idxprom32 = sext i32 %28 to i64
  %arrayidx33 = getelementptr inbounds [100 x i32], ptr %data31, i64 0, i64 %idxprom32
  store i32 %mul30, ptr %arrayidx33, align 4
  br label %for.inc34

for.inc34:                                        ; preds = %for.body26
  %29 = load i32, ptr %i23, align 4
  %inc35 = add nsw i32 %29, 1
  store i32 %inc35, ptr %i23, align 4
  br label %for.cond24, !llvm.loop !11

for.end36:                                        ; preds = %for.cond24
  br label %if.end

if.else:                                          ; preds = %for.end11
  %30 = load ptr, ptr %container.addr, align 8
  %nodes38 = getelementptr inbounds %struct.Container, ptr %30, i32 0, i32 0
  %arrayidx39 = getelementptr inbounds [10 x ptr], ptr %nodes38, i64 0, i64 1
  %31 = load ptr, ptr %arrayidx39, align 8
  store ptr %31, ptr %current37, align 8
  store i32 0, ptr %i40, align 4
  br label %for.cond41

for.cond41:                                       ; preds = %for.inc47, %if.else
  %32 = load i32, ptr %i40, align 4
  %cmp42 = icmp slt i32 %32, 30
  br i1 %cmp42, label %for.body43, label %for.end49

for.body43:                                       ; preds = %for.cond41
  %33 = load i32, ptr %i40, align 4
  %sub = sub nsw i32 0, %33
  %34 = load ptr, ptr %current37, align 8
  %data44 = getelementptr inbounds %struct.Node, ptr %34, i32 0, i32 0
  %35 = load i32, ptr %i40, align 4
  %idxprom45 = sext i32 %35 to i64
  %arrayidx46 = getelementptr inbounds [100 x i32], ptr %data44, i64 0, i64 %idxprom45
  store i32 %sub, ptr %arrayidx46, align 4
  br label %for.inc47

for.inc47:                                        ; preds = %for.body43
  %36 = load i32, ptr %i40, align 4
  %inc48 = add nsw i32 %36, 1
  store i32 %inc48, ptr %i40, align 4
  br label %for.cond41, !llvm.loop !12

for.end49:                                        ; preds = %for.cond41
  br label %if.end

if.end:                                           ; preds = %for.end49, %for.end36
  %37 = load ptr, ptr %container.addr, align 8
  %nodes50 = getelementptr inbounds %struct.Container, ptr %37, i32 0, i32 0
  %arrayidx51 = getelementptr inbounds [10 x ptr], ptr %nodes50, i64 0, i64 2
  %38 = load ptr, ptr %arrayidx51, align 8
  store ptr %38, ptr %finalNode, align 8
  store i32 0, ptr %i52, align 4
  br label %for.cond53

for.cond53:                                       ; preds = %for.inc59, %if.end
  %39 = load i32, ptr %i52, align 4
  %cmp54 = icmp slt i32 %39, 100
  br i1 %cmp54, label %for.body55, label %for.end61

for.body55:                                       ; preds = %for.cond53
  %40 = load ptr, ptr %finalNode, align 8
  %data56 = getelementptr inbounds %struct.Node, ptr %40, i32 0, i32 0
  %41 = load i32, ptr %i52, align 4
  %idxprom57 = sext i32 %41 to i64
  %arrayidx58 = getelementptr inbounds [100 x i32], ptr %data56, i64 0, i64 %idxprom57
  store i32 0, ptr %arrayidx58, align 4
  br label %for.inc59

for.inc59:                                        ; preds = %for.body55
  %42 = load i32, ptr %i52, align 4
  %inc60 = add nsw i32 %42, 1
  store i32 %inc60, ptr %i52, align 4
  br label %for.cond53, !llvm.loop !13

for.end61:                                        ; preds = %for.cond53
  store i32 0, ptr %outer, align 4
  br label %for.cond62

for.cond62:                                       ; preds = %for.inc114, %for.end61
  %43 = load i32, ptr %outer, align 4
  %cmp63 = icmp slt i32 %43, 5
  br i1 %cmp63, label %for.body64, label %for.end116

for.body64:                                       ; preds = %for.cond62
  %44 = load ptr, ptr %container.addr, align 8
  %nodes65 = getelementptr inbounds %struct.Container, ptr %44, i32 0, i32 0
  %45 = load i32, ptr %outer, align 4
  %idxprom66 = sext i32 %45 to i64
  %arrayidx67 = getelementptr inbounds [10 x ptr], ptr %nodes65, i64 0, i64 %idxprom66
  %46 = load ptr, ptr %arrayidx67, align 8
  store ptr %46, ptr %node, align 8
  store i32 0, ptr %i68, align 4
  br label %for.cond69

for.cond69:                                       ; preds = %for.inc76, %for.body64
  %47 = load i32, ptr %i68, align 4
  %cmp70 = icmp slt i32 %47, 100
  br i1 %cmp70, label %for.body71, label %for.end78

for.body71:                                       ; preds = %for.cond69
  %48 = load i32, ptr %outer, align 4
  %mul72 = mul nsw i32 %48, 100
  %49 = load i32, ptr %i68, align 4
  %add = add nsw i32 %mul72, %49
  %50 = load ptr, ptr %node, align 8
  %data73 = getelementptr inbounds %struct.Node, ptr %50, i32 0, i32 0
  %51 = load i32, ptr %i68, align 4
  %idxprom74 = sext i32 %51 to i64
  %arrayidx75 = getelementptr inbounds [100 x i32], ptr %data73, i64 0, i64 %idxprom74
  store i32 %add, ptr %arrayidx75, align 4
  br label %for.inc76

for.inc76:                                        ; preds = %for.body71
  %52 = load i32, ptr %i68, align 4
  %inc77 = add nsw i32 %52, 1
  store i32 %inc77, ptr %i68, align 4
  br label %for.cond69, !llvm.loop !14

for.end78:                                        ; preds = %for.cond69
  store i32 0, ptr %inner, align 4
  br label %for.cond79

for.cond79:                                       ; preds = %for.inc111, %for.end78
  %53 = load i32, ptr %inner, align 4
  %cmp80 = icmp slt i32 %53, 3
  br i1 %cmp80, label %for.body81, label %for.end113

for.body81:                                       ; preds = %for.cond79
  store i32 20, ptr %i82, align 4
  br label %for.cond83

for.cond83:                                       ; preds = %for.inc93, %for.body81
  %54 = load i32, ptr %i82, align 4
  %cmp84 = icmp slt i32 %54, 40
  br i1 %cmp84, label %for.body85, label %for.end95

for.body85:                                       ; preds = %for.cond83
  %55 = load ptr, ptr %node, align 8
  %data86 = getelementptr inbounds %struct.Node, ptr %55, i32 0, i32 0
  %56 = load i32, ptr %i82, align 4
  %idxprom87 = sext i32 %56 to i64
  %arrayidx88 = getelementptr inbounds [100 x i32], ptr %data86, i64 0, i64 %idxprom87
  %57 = load i32, ptr %arrayidx88, align 4
  %58 = load i32, ptr %inner, align 4
  %add89 = add nsw i32 %57, %58
  %59 = load ptr, ptr %node, align 8
  %data90 = getelementptr inbounds %struct.Node, ptr %59, i32 0, i32 0
  %60 = load i32, ptr %i82, align 4
  %idxprom91 = sext i32 %60 to i64
  %arrayidx92 = getelementptr inbounds [100 x i32], ptr %data90, i64 0, i64 %idxprom91
  store i32 %add89, ptr %arrayidx92, align 4
  br label %for.inc93

for.inc93:                                        ; preds = %for.body85
  %61 = load i32, ptr %i82, align 4
  %inc94 = add nsw i32 %61, 1
  store i32 %inc94, ptr %i82, align 4
  br label %for.cond83, !llvm.loop !15

for.end95:                                        ; preds = %for.cond83
  %62 = load i32, ptr %inner, align 4
  %63 = load i32, ptr %outer, align 4
  %call96 = call noundef i32 @_Z12pureFunctionii(i32 noundef %62, i32 noundef %63)
  store i32 %call96, ptr %offset, align 4
  store i32 60, ptr %i97, align 4
  br label %for.cond98

for.cond98:                                       ; preds = %for.inc108, %for.end95
  %64 = load i32, ptr %i97, align 4
  %cmp99 = icmp slt i32 %64, 80
  br i1 %cmp99, label %for.body100, label %for.end110

for.body100:                                      ; preds = %for.cond98
  %65 = load ptr, ptr %node, align 8
  %data101 = getelementptr inbounds %struct.Node, ptr %65, i32 0, i32 0
  %66 = load i32, ptr %i97, align 4
  %idxprom102 = sext i32 %66 to i64
  %arrayidx103 = getelementptr inbounds [100 x i32], ptr %data101, i64 0, i64 %idxprom102
  %67 = load i32, ptr %arrayidx103, align 4
  %68 = load i32, ptr %offset, align 4
  %sub104 = sub nsw i32 %67, %68
  %69 = load ptr, ptr %node, align 8
  %data105 = getelementptr inbounds %struct.Node, ptr %69, i32 0, i32 0
  %70 = load i32, ptr %i97, align 4
  %idxprom106 = sext i32 %70 to i64
  %arrayidx107 = getelementptr inbounds [100 x i32], ptr %data105, i64 0, i64 %idxprom106
  store i32 %sub104, ptr %arrayidx107, align 4
  br label %for.inc108

for.inc108:                                       ; preds = %for.body100
  %71 = load i32, ptr %i97, align 4
  %inc109 = add nsw i32 %71, 1
  store i32 %inc109, ptr %i97, align 4
  br label %for.cond98, !llvm.loop !16

for.end110:                                       ; preds = %for.cond98
  br label %for.inc111

for.inc111:                                       ; preds = %for.end110
  %72 = load i32, ptr %inner, align 4
  %inc112 = add nsw i32 %72, 1
  store i32 %inc112, ptr %inner, align 4
  br label %for.cond79, !llvm.loop !17

for.end113:                                       ; preds = %for.cond79
  br label %for.inc114

for.inc114:                                       ; preds = %for.end113
  %73 = load i32, ptr %outer, align 4
  %inc115 = add nsw i32 %73, 1
  store i32 %inc115, ptr %outer, align 4
  br label %for.cond62, !llvm.loop !18

for.end116:                                       ; preds = %for.cond62
  %call117 = call noalias noundef nonnull ptr @_Znam(i64 noundef 400) #8
  store ptr %call117, ptr %tsanBuffer, align 8
  store i32 0, ptr %i118, align 4
  br label %for.cond119

for.cond119:                                      ; preds = %for.inc124, %for.end116
  %74 = load i32, ptr %i118, align 4
  %cmp120 = icmp slt i32 %74, 100
  br i1 %cmp120, label %for.body121, label %for.end126

for.body121:                                      ; preds = %for.cond119
  %75 = load i32, ptr %i118, align 4
  %76 = load ptr, ptr %tsanBuffer, align 8
  %77 = load i32, ptr %i118, align 4
  %idxprom122 = sext i32 %77 to i64
  %arrayidx123 = getelementptr inbounds i32, ptr %76, i64 %idxprom122
  store i32 %75, ptr %arrayidx123, align 4
  br label %for.inc124

for.inc124:                                       ; preds = %for.body121
  %78 = load i32, ptr %i118, align 4
  %inc125 = add nsw i32 %78, 1
  store i32 %inc125, ptr %i118, align 4
  br label %for.cond119, !llvm.loop !19

for.end126:                                       ; preds = %for.cond119
  store i32 0, ptr %sum, align 4
  store i32 10, ptr %i127, align 4
  br label %for.cond128

for.cond128:                                      ; preds = %for.inc134, %for.end126
  %79 = load i32, ptr %i127, align 4
  %cmp129 = icmp slt i32 %79, 90
  br i1 %cmp129, label %for.body130, label %for.end136

for.body130:                                      ; preds = %for.cond128
  %80 = load ptr, ptr %tsanBuffer, align 8
  %81 = load i32, ptr %i127, align 4
  %idxprom131 = sext i32 %81 to i64
  %arrayidx132 = getelementptr inbounds i32, ptr %80, i64 %idxprom131
  %82 = load i32, ptr %arrayidx132, align 4
  %83 = load i32, ptr %sum, align 4
  %add133 = add nsw i32 %83, %82
  store i32 %add133, ptr %sum, align 4
  br label %for.inc134

for.inc134:                                       ; preds = %for.body130
  %84 = load i32, ptr %i127, align 4
  %inc135 = add nsw i32 %84, 1
  store i32 %inc135, ptr %i127, align 4
  br label %for.cond128, !llvm.loop !20

for.end136:                                       ; preds = %for.cond128
  store i32 0, ptr %i137, align 4
  br label %for.cond138

for.cond138:                                      ; preds = %for.inc146, %for.end136
  %85 = load i32, ptr %i137, align 4
  %cmp139 = icmp slt i32 %85, 100
  br i1 %cmp139, label %for.body140, label %for.end148

for.body140:                                      ; preds = %for.cond138
  %86 = load ptr, ptr %tsanBuffer, align 8
  %87 = load i32, ptr %i137, align 4
  %idxprom141 = sext i32 %87 to i64
  %arrayidx142 = getelementptr inbounds i32, ptr %86, i64 %idxprom141
  %88 = load i32, ptr %arrayidx142, align 4
  %mul143 = mul nsw i32 %88, 2
  %89 = load ptr, ptr %tsanBuffer, align 8
  %90 = load i32, ptr %i137, align 4
  %idxprom144 = sext i32 %90 to i64
  %arrayidx145 = getelementptr inbounds i32, ptr %89, i64 %idxprom144
  store i32 %mul143, ptr %arrayidx145, align 4
  br label %for.inc146

for.inc146:                                       ; preds = %for.body140
  %91 = load i32, ptr %i137, align 4
  %inc147 = add nsw i32 %91, 1
  store i32 %inc147, ptr %i137, align 4
  br label %for.cond138, !llvm.loop !21

for.end148:                                       ; preds = %for.cond138
  %92 = load ptr, ptr %container.addr, align 8
  %buffer149 = getelementptr inbounds %struct.Container, ptr %92, i32 0, i32 1
  %arraydecay150 = getelementptr inbounds [200 x i32], ptr %buffer149, i64 0, i64 0
  store ptr %arraydecay150, ptr %alias1, align 8
  %93 = load ptr, ptr %container.addr, align 8
  %buffer151 = getelementptr inbounds %struct.Container, ptr %93, i32 0, i32 1
  %arraydecay152 = getelementptr inbounds [200 x i32], ptr %buffer151, i64 0, i64 0
  %add.ptr = getelementptr inbounds i32, ptr %arraydecay152, i64 50
  store ptr %add.ptr, ptr %alias2, align 8
  store i32 0, ptr %i153, align 4
  br label %for.cond154

for.cond154:                                      ; preds = %for.inc160, %for.end148
  %94 = load i32, ptr %i153, align 4
  %cmp155 = icmp slt i32 %94, 50
  br i1 %cmp155, label %for.body156, label %for.end162

for.body156:                                      ; preds = %for.cond154
  %95 = load i32, ptr %i153, align 4
  %mul157 = mul nsw i32 %95, 3
  %96 = load ptr, ptr %alias1, align 8
  %97 = load i32, ptr %i153, align 4
  %idxprom158 = sext i32 %97 to i64
  %arrayidx159 = getelementptr inbounds i32, ptr %96, i64 %idxprom158
  store i32 %mul157, ptr %arrayidx159, align 4
  br label %for.inc160

for.inc160:                                       ; preds = %for.body156
  %98 = load i32, ptr %i153, align 4
  %inc161 = add nsw i32 %98, 1
  store i32 %inc161, ptr %i153, align 4
  br label %for.cond154, !llvm.loop !22

for.end162:                                       ; preds = %for.cond154
  store i32 0, ptr %i163, align 4
  br label %for.cond164

for.cond164:                                      ; preds = %for.inc172, %for.end162
  %99 = load i32, ptr %i163, align 4
  %cmp165 = icmp slt i32 %99, 30
  br i1 %cmp165, label %for.body166, label %for.end174

for.body166:                                      ; preds = %for.cond164
  %100 = load ptr, ptr %alias2, align 8
  %101 = load i32, ptr %i163, align 4
  %idxprom167 = sext i32 %101 to i64
  %arrayidx168 = getelementptr inbounds i32, ptr %100, i64 %idxprom167
  %102 = load i32, ptr %arrayidx168, align 4
  %add169 = add nsw i32 %102, 1
  %103 = load ptr, ptr %alias2, align 8
  %104 = load i32, ptr %i163, align 4
  %idxprom170 = sext i32 %104 to i64
  %arrayidx171 = getelementptr inbounds i32, ptr %103, i64 %idxprom170
  store i32 %add169, ptr %arrayidx171, align 4
  br label %for.inc172

for.inc172:                                       ; preds = %for.body166
  %105 = load i32, ptr %i163, align 4
  %inc173 = add nsw i32 %105, 1
  store i32 %inc173, ptr %i163, align 4
  br label %for.cond164, !llvm.loop !23

for.end174:                                       ; preds = %for.cond164
  store i32 40, ptr %i175, align 4
  br label %for.cond176

for.cond176:                                      ; preds = %for.inc186, %for.end174
  %106 = load i32, ptr %i175, align 4
  %cmp177 = icmp slt i32 %106, 90
  br i1 %cmp177, label %for.body178, label %for.end188

for.body178:                                      ; preds = %for.cond176
  %107 = load ptr, ptr %container.addr, align 8
  %buffer179 = getelementptr inbounds %struct.Container, ptr %107, i32 0, i32 1
  %108 = load i32, ptr %i175, align 4
  %idxprom180 = sext i32 %108 to i64
  %arrayidx181 = getelementptr inbounds [200 x i32], ptr %buffer179, i64 0, i64 %idxprom180
  %109 = load i32, ptr %arrayidx181, align 4
  %mul182 = mul nsw i32 %109, 2
  %110 = load ptr, ptr %container.addr, align 8
  %buffer183 = getelementptr inbounds %struct.Container, ptr %110, i32 0, i32 1
  %111 = load i32, ptr %i175, align 4
  %idxprom184 = sext i32 %111 to i64
  %arrayidx185 = getelementptr inbounds [200 x i32], ptr %buffer183, i64 0, i64 %idxprom184
  store i32 %mul182, ptr %arrayidx185, align 4
  br label %for.inc186

for.inc186:                                       ; preds = %for.body178
  %112 = load i32, ptr %i175, align 4
  %inc187 = add nsw i32 %112, 1
  store i32 %inc187, ptr %i175, align 4
  br label %for.cond176, !llvm.loop !24

for.end188:                                       ; preds = %for.cond176
  %113 = load ptr, ptr %tsanBuffer, align 8
  %isnull = icmp eq ptr %113, null
  br i1 %isnull, label %delete.end, label %delete.notnull

delete.notnull:                                   ; preds = %for.end188
  call void @_ZdaPv(ptr noundef %113) #9
  br label %delete.end

delete.end:                                       ; preds = %delete.notnull, %for.end188
  ret void
}

; Function Attrs: nobuiltin allocsize(0)
declare noundef nonnull ptr @_Znam(i64 noundef) #3

; Function Attrs: nobuiltin nounwind
declare void @_ZdaPv(ptr noundef) #4

; Function Attrs: mustprogress noinline uwtable
define dso_local noundef ptr @_Z19createTestContainerv() #2 {
entry:
  %c = alloca ptr, align 8
  %i = alloca i32, align 4
  %call = call noalias noundef nonnull ptr @_Znwm(i64 noundef 880) #8
  call void @llvm.memset.p0.i64(ptr align 16 %call, i8 0, i64 880, i1 false)
  store ptr %call, ptr %c, align 8
  store i32 0, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.inc, %entry
  %0 = load i32, ptr %i, align 4
  %cmp = icmp slt i32 %0, 10
  br i1 %cmp, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %call1 = call noalias noundef nonnull ptr @_Znwm(i64 noundef 408) #8
  call void @llvm.memset.p0.i64(ptr align 16 %call1, i8 0, i64 408, i1 false)
  %1 = load ptr, ptr %c, align 8
  %nodes = getelementptr inbounds %struct.Container, ptr %1, i32 0, i32 0
  %2 = load i32, ptr %i, align 4
  %idxprom = sext i32 %2 to i64
  %arrayidx = getelementptr inbounds [10 x ptr], ptr %nodes, i64 0, i64 %idxprom
  store ptr %call1, ptr %arrayidx, align 8
  %3 = load ptr, ptr %c, align 8
  %nodes2 = getelementptr inbounds %struct.Container, ptr %3, i32 0, i32 0
  %4 = load i32, ptr %i, align 4
  %idxprom3 = sext i32 %4 to i64
  %arrayidx4 = getelementptr inbounds [10 x ptr], ptr %nodes2, i64 0, i64 %idxprom3
  %5 = load ptr, ptr %arrayidx4, align 8
  %next = getelementptr inbounds %struct.Node, ptr %5, i32 0, i32 1
  store ptr null, ptr %next, align 8
  br label %for.inc

for.inc:                                          ; preds = %for.body
  %6 = load i32, ptr %i, align 4
  %inc = add nsw i32 %6, 1
  store i32 %inc, ptr %i, align 4
  br label %for.cond, !llvm.loop !25

for.end:                                          ; preds = %for.cond
  %7 = load ptr, ptr %c, align 8
  ret ptr %7
}

; Function Attrs: nobuiltin allocsize(0)
declare noundef nonnull ptr @_Znwm(i64 noundef) #3

; Function Attrs: argmemonly nocallback nofree nounwind willreturn writeonly
declare void @llvm.memset.p0.i64(ptr nocapture writeonly, i8, i64, i1 immarg) #5

; Function Attrs: mustprogress noinline norecurse uwtable
define dso_local noundef i32 @main() #6 {
entry:
  %retval = alloca i32, align 4
  %container = alloca ptr, align 8
  %externalBuffer = alloca [100 x i32], align 16
  %i = alloca i32, align 4
  store i32 0, ptr %retval, align 4
  %call = call noundef ptr @_Z19createTestContainerv()
  store ptr %call, ptr %container, align 8
  %0 = load ptr, ptr %container, align 8
  %arraydecay = getelementptr inbounds [100 x i32], ptr %externalBuffer, i64 0, i64 0
  call void @_Z23testComplexOptimizationP9ContainerPii(ptr noundef %0, ptr noundef %arraydecay, i32 noundef 1)
  %1 = load ptr, ptr %container, align 8
  %arraydecay1 = getelementptr inbounds [100 x i32], ptr %externalBuffer, i64 0, i64 0
  call void @_Z23testComplexOptimizationP9ContainerPii(ptr noundef %1, ptr noundef %arraydecay1, i32 noundef -1)
  %2 = load ptr, ptr %container, align 8
  %arraydecay2 = getelementptr inbounds [100 x i32], ptr %externalBuffer, i64 0, i64 0
  call void @_Z23testComplexOptimizationP9ContainerPii(ptr noundef %2, ptr noundef %arraydecay2, i32 noundef 0)
  store i32 0, ptr %i, align 4
  br label %for.cond

for.cond:                                         ; preds = %for.inc, %entry
  %3 = load i32, ptr %i, align 4
  %cmp = icmp slt i32 %3, 10
  br i1 %cmp, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %4 = load ptr, ptr %container, align 8
  %nodes = getelementptr inbounds %struct.Container, ptr %4, i32 0, i32 0
  %5 = load i32, ptr %i, align 4
  %idxprom = sext i32 %5 to i64
  %arrayidx = getelementptr inbounds [10 x ptr], ptr %nodes, i64 0, i64 %idxprom
  %6 = load ptr, ptr %arrayidx, align 8
  %isnull = icmp eq ptr %6, null
  br i1 %isnull, label %delete.end, label %delete.notnull

delete.notnull:                                   ; preds = %for.body
  call void @_ZdlPv(ptr noundef %6) #9
  br label %delete.end

delete.end:                                       ; preds = %delete.notnull, %for.body
  br label %for.inc

for.inc:                                          ; preds = %delete.end
  %7 = load i32, ptr %i, align 4
  %inc = add nsw i32 %7, 1
  store i32 %inc, ptr %i, align 4
  br label %for.cond, !llvm.loop !26

for.end:                                          ; preds = %for.cond
  %8 = load ptr, ptr %container, align 8
  %isnull3 = icmp eq ptr %8, null
  br i1 %isnull3, label %delete.end5, label %delete.notnull4

delete.notnull4:                                  ; preds = %for.end
  call void @_ZdlPv(ptr noundef %8) #9
  br label %delete.end5

delete.end5:                                      ; preds = %delete.notnull4, %for.end
  ret i32 0
}

; Function Attrs: nobuiltin nounwind
declare void @_ZdlPv(ptr noundef) #4

attributes #0 = { mustprogress noinline nounwind uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { nounwind "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #2 = { mustprogress noinline uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #3 = { nobuiltin allocsize(0) "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #4 = { nobuiltin nounwind "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #5 = { argmemonly nocallback nofree nounwind willreturn writeonly }
attributes #6 = { mustprogress noinline norecurse uwtable "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #7 = { nounwind }
attributes #8 = { builtin allocsize(0) }
attributes #9 = { builtin nounwind }

!llvm.module.flags = !{!0, !1, !2, !3, !4}
!llvm.ident = !{!5}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 7, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{i32 7, !"frame-pointer", i32 2}
!5 = !{!"Ubuntu clang version 15.0.7"}
!6 = distinct !{!6, !7}
!7 = !{!"llvm.loop.mustprogress"}
!8 = distinct !{!8, !7}
!9 = distinct !{!9, !7}
!10 = distinct !{!10, !7}
!11 = distinct !{!11, !7}
!12 = distinct !{!12, !7}
!13 = distinct !{!13, !7}
!14 = distinct !{!14, !7}
!15 = distinct !{!15, !7}
!16 = distinct !{!16, !7}
!17 = distinct !{!17, !7}
!18 = distinct !{!18, !7}
!19 = distinct !{!19, !7}
!20 = distinct !{!20, !7}
!21 = distinct !{!21, !7}
!22 = distinct !{!22, !7}
!23 = distinct !{!23, !7}
!24 = distinct !{!24, !7}
!25 = distinct !{!25, !7}
!26 = distinct !{!26, !7}
