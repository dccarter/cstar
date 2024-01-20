; ModuleID = '/Users/dccarter/projects/cxy-llvm/tests/hello.cxy'
source_filename = "/Users/dccarter/projects/cxy-llvm/tests/hello.cxy"

%User = type { ptr, i32 }
%__Closure0 = type { i8 }

declare i32 @printf(ptr, ...)

declare i64 @read(i32, ptr, i64)

define void @User_op__init(ptr %this) {
entry:
  %this1 = alloca ptr, align 8
  store ptr %this, ptr %this1, align 8
  %this2 = load ptr, ptr %this1, align 8
  %age = getelementptr inbounds %User, ptr %this2, i32 0, i32 1
  store i32 32, ptr %age, align 4
  br label %end

end:                                              ; preds = %entry
  ret void
}

define internal i8 @__Closure0_op__call(ptr %this) {
entry:
  %this1 = alloca ptr, align 8
  store ptr %this, ptr %this1, align 8
  %res = alloca i8, align 1
  %x = load, align 1
  store i8 %x, ptr %res, align 1
  br label %end

end:                                              ; preds = %entry
  %0 = load i8, ptr %res, align 1
  ret i8 %0
}

define i32 @main() {
entry:
  %y = alloca %__Closure0, align 8
  %x = alloca i8, align 1
  %res = alloca i32, align 4
  store i8 100, ptr %x, align 1
  %x1 = load i8, ptr %x, align 1
  %0 = insertvalue %__Closure0 undef, i8 %x1, 0
  store %__Closure0 %0, ptr %y, align 1
  %1 = call i8 @__Closure0_op__call(ptr %y)
  %2 = sext i8 %1 to i32
  store i32 %2, ptr %res, align 4
  br label %end

end:                                              ; preds = %entry
  %3 = load i32, ptr %res, align 4
  ret i32 %3
}
