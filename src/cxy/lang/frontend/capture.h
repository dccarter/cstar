//
// Created by Carter on 2023-03-31.
//

#include "ast.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Capture {
    AstNode *node;
    AstNode *field;
    u32 id;
    u64 flags;
};

Capture *addClosureCapture(ClosureCapture *set, AstNode *node);
u64 getOrderedCapture(ClosureCapture *set, Capture *capture, u64 count);
cstring getCapturedNodeName(const AstNode *node);
AstNode *getCapturedNodeType(const AstNode *node);

#ifdef __cplusplus
}
#endif
