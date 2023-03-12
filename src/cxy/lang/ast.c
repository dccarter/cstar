
#include "ast.h"

#include <memory.h>

static inline void printManyAsts(FormatState *state,
                          const char *sep,
                          const AstNode *nodes)
{
    for (const AstNode *node = nodes; node; node = node->next) {
        printAst(state, node);
        if (node->next)
            format(state, sep, NULL);
    }
}

static inline void printManyAstsWithDelim(FormatState *state,
                                      const char *open,
                                      const char *sep,
                                      const char *close,
                                      const AstNode *nodes)
{
    format(state, open, NULL);
    printManyAsts(state, sep, nodes);
    format(state, close, NULL);
}

static inline void printAstWithDelim(FormatState *state,
                                     const char *open,
                                     const char *close,
                                     const AstNode *node)
{
    format(state, open, NULL);
    printAst(state, node);
    format(state, close, NULL);
}

static inline void printManyAstsWithinBlock(FormatState *state,
                                            const char *sep,
                                            const AstNode *nodes,
                                            bool newLine)
{
    if (nodes)
        format(state, "{{}", NULL);
    else if (!newLine && !nodes->next)
        printAstWithDelim(state, "{{ ", " }", nodes);
    else
        printManyAstsWithDelim(state, "{{{>}\n", sep, "{<}\n}", nodes);
}

static inline void printManyWithinBlock(FormatState *state, const AstNode *node)
{
    if (isTuple(node))
        printAst(state, node);
    else
        printAstWithDelim(state, "(", ")", node);
}

static inline void printPrimitiveType(FormatState *state, AstTag tag)
{
    printKeyword(state, getPrimitiveTypeName(tag));
}

static inline void printOperand(FormatState *S,
                                const AstNode *operand,
                                int prec)
{
    if (operand->tag == astBinaryExpr
        && getBinaryOpPrecedence(operand->binaryExpr.op) > prec)
    {
        printAstWithDelim(S, "(", ")", operand);
    }
    else
        printAst(S, operand);
}

static void printUnaryExpr(FormatState *S, const AstNode *expr)
{
    csAssert0(expr && expr->tag == astUnaryExpr);
    const Operator op = expr->unaryExpr.op;
    switch (op) {
#define f(name, ...) case op##name:
        AST_PREFIX_EXPR_LIST(f)
        printAstWithDelim(S,
                          getUnaryOpString(op),
                          "",
                          expr->unaryExpr.operand); \
        break;
#undef  f

#define f(name, ...) case op##name:
        AST_POSTFIX_EXPR_LIST(f)
        printAstWithDelim(S,
                          "",
                          getUnaryOpString(op),
                          expr->unaryExpr.operand);
        break;
    default:
        csAssert(false, "operator is not unary");
    }
}

static void printBinaryExpr(FormatState *S, const AstNode *expr)
{
    csAssert0(expr && expr->tag == astBinaryExpr);
    int prec = getBinaryOpPrecedence(expr->binaryExpr.op);
    printOperand(S, expr->binaryExpr.lhs, prec);
    format(S,
" {s} ",
    (FormatArg[]){{.s = getBinaryOpString(expr->binaryExpr.op)}});
    printOperand(S, expr->binaryExpr.rhs, prec);
}

AstNode *makeAstNode(MemPool *pool, const FileLoc *loc, const AstNode *init)
{
    AstNode *node = allocFromMemPool(pool, sizeof(AstNode));
    memcpy(node, init, sizeof(AstNode));
    node->loc = *loc;
    return node;
}

AstNode *copyAstNode(AstNode *dst, const AstNode *src, FileLoc *loc)
{
    memcpy(dst, src, sizeof(AstNode));
    dst->loc = *loc;
    return dst;
}

void astVisit(AstVisitor *visitor, AstNode *node)
{
    if (visitor->visitors[node->tag]) {
        visitor->visitors[node->tag](visitor, node);
    }
}

void printAst(FormatState *state, const AstNode *node)
{
}