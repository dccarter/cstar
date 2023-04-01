//
// Created by Carter on 2023-03-31.
//

#include <lang/ast.h>

u64 addClosureCapture(ClosureCapture *set, cstring name, const Type *type);
u64 getOrderedCapture(ClosureCapture *set, const Type **capture, u64 count);
