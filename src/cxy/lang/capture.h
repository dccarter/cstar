//
// Created by Carter on 2023-03-31.
//

#include <lang/ast.h>

struct Capture {
    const AstNode *node;
    u32 id;
    u64 flags;
};

Capture *addClosureCapture(ClosureCapture *set, const AstNode *node);
u64 getOrderedCapture(ClosureCapture *set, const Capture **capture, u64 count);
cstring getCapturedNodeName(const AstNode *node);