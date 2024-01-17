; ModuleID = '/Users/dccarter/projects/cxy-llvm/tests/hello.cxy'
source_filename = "/Users/dccarter/projects/cxy-llvm/tests/hello.cxy"

@num = global i32 20, align 4

declare i32 @printf(ptr, ...)

define internal ptr @hello() {
entry:
  %res = alloca ptr, align 8
  store ptr @num, ptr %res, align 8
  br label %end

end:                                              ; preds = %entry
  %0 = load ptr, ptr %res, align 8
  ret ptr %0
}

define i32 @main() {
entry:
  %y = alloca i32, align 4
  %x = alloca ptr, align 8
  %res = alloca i32, align 4
  %calltmp = call ptr @hello()
  store ptr %calltmp, ptr %x, align 8
  %x1 = load ptr, ptr %x, align 8
  %0 = getelementptr i32, ptr %x1, i8 0
  %1 = load i32, ptr %0, align 4
  store i32 %1, ptr %y, align 4
  %y2 = load i32, ptr %y, align 4
  store i32 %y2, ptr %res, align 4
  br label %end

end:                                              ; preds = %entry
  %2 = load i32, ptr %res, align 4
  ret i32 %2
}
