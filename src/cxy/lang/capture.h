//
// Created by Carter on 2023-03-31.
//

#include <lang/ast.h>

u64 addClosureCapture(ClosureCapture *set, cstring name, const AstNode *node);
u64 getOrderedCapture(ClosureCapture *set, const AstNode **capture, u64 count);
cstring getCapturedNodeName(const AstNode *node);