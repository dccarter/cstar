; ModuleID = '/Users/dccarter/projects/cxy-llvm/tests/hello.cxy'
source_filename = "/Users/dccarter/projects/cxy-llvm/tests/hello.cxy"

declare i32 @printf(ptr, ...)

declare i64 @read(i32, ptr, i64)

define i32 @main() {
entry:
  %data = alloca [64 x i8], align 1
  %res = alloca i32, align 4
  %data1 = load [64 x i8], ptr %data, align 1
  %calltmp = call i64 @read(i8 0, [64 x i8] %data1, i8 64)
  %0 = trunc i64 %calltmp to i32
  store i32 %0, ptr %res, align 4
  br label %end

end:                                              ; preds = %entry
  %1 = load i32, ptr %res, align 4
  ret i32 %1
}
