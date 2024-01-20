//
// Created by Carter on 2023-03-31.
//

#include "ast.h"

struct Capture {
    AstNode *node;
    u32 id;
    u64 flags;
};

Capture *addClosureCapture(ClosureCapture *set, AstNode *node);
u64 getOrderedCapture(ClosureCapture *set, Capture *capture, u64 count);
cstring getCapturedNodeName(const AstNode *node);
AstNode *getCapturedNodeType(const AstNode *node);
