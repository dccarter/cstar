
#include "ast.h"

#include <memory.h>

typedef struct {
    FormatState *state;
} AstPrintContext;

static inline void printManyAsts(ConstAstVisitor *visitor,
                                 const char *sep,
                                 const AstNode *nodes)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    for (const AstNode *node = nodes; node; node = node->next) {
        astConstVisit(visitor, node);
        if (node->next)
            format(context->state, sep, NULL);
    }
}

static inline void printManyAstsWithDelim(ConstAstVisitor *visitor,
                                          const char *open,
                                          const char *sep,
                                          const char *close,
                                          const AstNode *nodes)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    format(context->state, open, NULL);
    printManyAsts(visitor, sep, nodes);
    format(context->state, close, NULL);
}

static inline void printAstWithDelim(ConstAstVisitor *visitor,
                                     const char *open,
                                     const char *close,
                                     const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    format(context->state, open, NULL);
    astConstVisit(visitor, node);
    format(context->state, close, NULL);
}

static inline void printManyAstsWithinBlock(ConstAstVisitor *visitor,
                                            const char *sep,
                                            const AstNode *nodes,
                                            bool newLine)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    if (!nodes)
        format(context->state, "{{}", NULL);
    else if (!newLine && !nodes->next)
        printAstWithDelim(visitor, "{{ ", " }", nodes);
    else
        printManyAstsWithDelim(visitor, "{{{>}\n", sep, "{<}\n}", nodes);
}

static inline void printManyAstsWithinParen(ConstAstVisitor *visitor,
                                            const AstNode *node)
{
    if (node && isTuple(node))
        astConstVisit(visitor, node);
    else
        printManyAstsWithDelim(visitor, "(", ", ", ")", node);
}

static inline void printOperand(ConstAstVisitor *visitor,
                                const AstNode *operand,
                                int prec)
{
    if (operand->tag == astBinaryExpr &&
        getBinaryOpPrecedence(operand->binaryExpr.op) > prec) {
        printAstWithDelim(visitor, "(", ")", operand);
    }
    else
        astConstVisit(visitor, operand);
}

static void printGroupExpr(ConstAstVisitor *visitor, const AstNode *expr)
{
    printAstWithDelim(visitor, "(", ")", expr->groupExpr.expr);
}

static void printUnaryExpr(ConstAstVisitor *visitor, const AstNode *expr)
{
    csAssert0(expr && expr->tag == astUnaryExpr);
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    const Operator op = expr->unaryExpr.op;
    switch (op) {
#define f(name, ...) case op##name:
        AST_PREFIX_EXPR_LIST(f)
#undef f
        if (isPrefixOpKeyword(op)) {
            printKeyword(context->state, getUnaryOpString(op));
            format(context->state, " ", NULL);
            astConstVisit(visitor, expr->unaryExpr.operand);
        }
        else
            printAstWithDelim(
                visitor, getUnaryOpString(op), "", expr->unaryExpr.operand);
        break;
#undef f

#define f(name, ...) case op##name:
        AST_POSTFIX_EXPR_LIST(f)
#undef f
        printAstWithDelim(
            visitor, "", getUnaryOpString(op), expr->unaryExpr.operand);
        break;
    default:
        csAssert(false, "operator is not unary");
    }
}

static void printBinaryExpr(ConstAstVisitor *visitor, const AstNode *expr)
{
    csAssert0(expr && expr->tag == astBinaryExpr);

    AstPrintContext *context = getConstAstVisitorContext(visitor);
    int prec = getBinaryOpPrecedence(expr->binaryExpr.op);

    printOperand(visitor, expr->binaryExpr.lhs, prec);
    format(context->state,
           " {s} ",
           (FormatArg[]){{.s = getBinaryOpString(expr->binaryExpr.op)}});
    printOperand(visitor, expr->binaryExpr.rhs, prec);
}

static void printAssignExpr(ConstAstVisitor *visitor, const AstNode *expr)
{
    csAssert0(expr && expr->tag == astAssignExpr);

    AstPrintContext *context = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, expr->assignExpr.lhs);
    format(context->state,
           " {s} ",
           (FormatArg[]){{.s = getAssignOpString(expr->binaryExpr.op)}});
    astConstVisit(visitor, expr->assignExpr.rhs);
}

static void printTernaryExpr(ConstAstVisitor *visitor, const AstNode *expr)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, expr->ternaryExpr.cond);
    format(context->state, "? ", NULL);
    astConstVisit(visitor, expr->ternaryExpr.body);
    format(context->state, " : ", NULL);
    astConstVisit(visitor, expr->ternaryExpr.otherwise);
}

static void printStmtExpr(ConstAstVisitor *visitor, const AstNode *expr)
{
    astConstVisit(visitor, expr->stmtExpr.stmt);
}

static void printStringExpr(ConstAstVisitor *visitor, const AstNode *expr)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    format(context->state,
           "{$}f\"{$}",
           (FormatArg[]){{.style = literalStyle}, {.style = resetStyle}});

    for (const AstNode *part = expr->stringExpr.parts; part;
         part = part->next) {
        if (part->tag == astStringLit) {
            format(context->state,
                   "{$}{s}{$}",
                   (FormatArg[]){{.style = literalStyle},
                                 {.s = part->stringLiteral.value},
                                 {.style = resetStyle}});
        }
        else {
            format(context->state, "${{", NULL);
            astConstVisit(visitor, part);
            format(context->state, "}", NULL);
        }
    }

    format(context->state,
           "{$}\"{$}",
           (FormatArg[]){{.style = literalStyle}, {.style = resetStyle}});
}

static void printTypedExpr(ConstAstVisitor *visitor, const AstNode *expr)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, expr->typedExpr.expr);
    format(context->state, " : ", NULL);
    astConstVisit(visitor, expr->typedExpr.type);
}

static void printCallExpr(ConstAstVisitor *visitor, const AstNode *expr)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    if (expr->callExpr.callee->tag == astClosureExpr)
        printAstWithDelim(visitor, "(", ")", expr->callExpr.callee);
    else
        astConstVisit(visitor, expr->callExpr.callee);
    if (expr->tag == astMacroCallExpr)
        format(context->state, "!", NULL);
    printManyAstsWithinParen(visitor, expr->callExpr.args);
}

static void printClosureExpr(ConstAstVisitor *visitor, const AstNode *expr)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    if (expr->closureExpr.isAsync)
        printKeyword(context->state, "async");

    printManyAstsWithinParen(visitor, expr->closureExpr.params);

    if (expr->closureExpr.ret) {
        format(context->state, " : ", NULL);
        astConstVisit(visitor, expr->closureExpr.ret);
    }

    if (expr->closureExpr.body) {
        format(context->state, " => ", NULL);
        astConstVisit(visitor, expr->closureExpr.body);
    }
}

static void printArrayExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    printManyAstsWithDelim(visitor, "[", ",", "]", node->arrayExpr.elements);
}

static void printIndexExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    if (node->indexExpr.target)
        astConstVisit(visitor, node->indexExpr.target);
    printAstWithDelim(visitor, "[", "]", node->indexExpr.index);
}

static void printFieldExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    format(context->state, "{s}: ", (FormatArg[]){{.s = node->fieldExpr.name}});
    astConstVisit(visitor, node->fieldExpr.value);
}

static void printStructExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    astConstVisit(visitor, node->structExpr.left);
    printManyAstsWithinBlock(visitor, ", ", node->structExpr.fields, false);
}

static void printMemberExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->memberExpr.target);
    format(context->state, ".", NULL);
    astConstVisit(visitor, node->memberExpr.member);
}

static void printIdentifier(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    format(context->state, "{s}", (FormatArg[]){{.s = node->ident.value}});
}

static void printNullLiteral(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    printKeyword(context->state, "null");
}

static void printBoolLiteral(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    printKeyword(context->state, node->boolLiteral.value ? "true" : "false");
}

static void printCharLiteral(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    format(context->state,
           "{$}'{c}'{$}",
           (FormatArg[]){{.style = literalStyle},
                         {.u32 = node->charLiteral.value},
                         {.style = resetStyle}});
}

static void printIntLiteral(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    format(context->state,
           "{$}{s}{u}{$}",
           (FormatArg[]){{.style = literalStyle},
                         {.s = node->intLiteral.hasMinus ? "-" : ""},
                         {.u = node->intLiteral.value},
                         {.style = resetStyle}});
}

static void printFloatLiteral(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    format(context->state,
           "{$}{f64}{$}",
           (FormatArg[]){{.style = literalStyle},
                         {.f64 = node->floatLiteral.value},
                         {.style = resetStyle}});
}

static void printStringLiteral(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    format(context->state,
           "{$}\"{s}\"{$}",
           (FormatArg[]){{.style = literalStyle},
                         {.s = node->stringLiteral.value},
                         {.style = resetStyle}});
}

static void printPrimitiveType(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    printKeyword(context->state, getPrimitiveTypeName(node->primitiveType.id));
}

static void printTupleType(ConstAstVisitor *visitor, const AstNode *node)
{
    printManyAstsWithDelim(visitor, "(", ", ", ")", node->tupleType.args);
}

static void printArrayType(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->arrayType.elementType);
    format(context->state, "[", NULL);
    if (node->arrayType.size != NULL)
        astConstVisit(visitor, node->arrayType.size);
    format(context->state, "]", NULL);
}

static void printPointerType(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    format(context->state, "&", NULL);
    if (node->pointerType.isConst) {
        printKeyword(context->state, "const");
        format(context->state, " ", NULL);
    }
    astConstVisit(visitor, node->pointerType.pointed);
}

static void printFuncType(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    if (node->funcType.isAsync) {
        printKeyword(context->state, "async");
        format(context->state, " ", NULL);
    }
    printKeyword(context->state, "func");
    printManyAstsWithinParen(visitor, node->funcType.params);
    format(context->state, " -> ", NULL);
    astConstVisit(visitor, node->funcType.ret);
}

static void printFuncParam(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    if (node->funcParam.isVariadic)
        format(context->state, "...", NULL);

    format(context->state, "{s}: ", (FormatArg[]){{.s = node->funcParam.name}});
    astConstVisit(visitor, node->funcParam.type);
}

static void printError(ConstAstVisitor *visitor,
                       attr(unused) const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    format(context->state,
           "{$}<error>{$}",
           (FormatArg[]){{.style = errorStyle}, {.style = resetStyle}});
}

static void printAttribute(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    format(context->state, "{s}", (FormatArg[]){{.s = node->attr.name}});
    if (node->attr.args)
        printManyAstsWithinParen(visitor, node->attr.args);
}

static void printGenericParam(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    format(
        context->state, "{s}", (FormatArg[]){{.s = node->genericParam.name}});
    if (node->genericParam.constraints) {
        format(context->state, ": ", NULL);
        printManyAstsWithDelim(
            visitor, "", "|", "", node->genericParam.constraints);
    }
}

static void printPathElement(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    format(context->state, "{s}", (FormatArg[]){{.s = node->pathElement.name}});
    if (node->pathElement.args)
        printManyAstsWithDelim(visitor, "[", ",", "]", node->pathElement.args);
}

static void printPath(ConstAstVisitor *visitor, const AstNode *node)
{
    printManyAstsWithDelim(visitor, "", ".", "", node->path.elements);
}

static void printFuncDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    if (node->funcDecl.isAsync) {
        printKeyword(context->state, "async");
        format(context->state, " ", NULL);
    }

    printKeyword(context->state, "func");
    format(context->state, " {s}", (FormatArg[]){{.s = node->funcDecl.name}});
    if (node->funcDecl.genericParams) {
        printManyAstsWithDelim(
            visitor, "[", ",", "]", node->funcDecl.genericParams);
    }
    printManyAstsWithinParen(visitor, node->funcDecl.params);

    if (node->funcDecl.ret) {
        format(context->state, " : ", NULL);
        astConstVisit(visitor, node->funcDecl.ret);
    }
    format(context->state, " ", NULL);

    if (node->funcDecl.body) {
        if (node->funcDecl.body->tag == astExprStmt)
            format(context->state, " => ", NULL);
        astConstVisit(visitor, node->funcDecl.body);
    }
}

static void printVariableDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    if (node->tag == astVarDecl)
        printKeyword(context->state, "var");
    else
        printKeyword(context->state, "const");
    format(context->state, " ", NULL);

    if (node->varDecl.names->next)
        printManyAstsWithinParen(visitor, node->varDecl.names);
    else
        astConstVisit(visitor, node->varDecl.names);

    if (node->varDecl.type) {
        format(context->state, " : ", NULL);
        astConstVisit(visitor, node->varDecl.type);
    }

    if (node->varDecl.init) {
        format(context->state, " = ", NULL);
        astConstVisit(visitor, node->varDecl.init);
    }
}

static void printTypeDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);

    printKeyword(context->state, "type");
    format(context->state, " {s}", (FormatArg[]){{.s = node->typeDecl.name}});
    if (node->typeDecl.genericParams) {
        printManyAstsWithDelim(
            visitor, "[", ",", "]", node->typeDecl.genericParams);
    }

    if (node->typeDecl.aliased) {
        format(context->state, " = ", NULL);
        astConstVisit(visitor, node->typeDecl.aliased);
    }
}

static void printUnionDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);

    printKeyword(context->state, "type");
    format(context->state, " {s}", (FormatArg[]){{.s = node->unionDecl.name}});
    if (node->typeDecl.genericParams) {
        printManyAstsWithDelim(
            visitor, "[", ",", "]", node->unionDecl.genericParams);
    }

    if (node->typeDecl.aliased) {
        format(context->state, " = ", NULL);
        printManyAstsWithDelim(visitor, "", " | ", "", node->unionDecl.members);
    }
}

static void printEnumDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);

    printKeyword(context->state, "enum");
    format(context->state, " {s}", (FormatArg[]){{.s = node->enumDecl.name}});
    if (node->enumDecl.base) {
        format(context->state, " : ", NULL);
        astConstVisit(visitor, node->enumDecl.base);
    }

    if (node->enumDecl.options) {
        format(context->state, " = ", NULL);
        printManyAstsWithinBlock(visitor, ",", node->unionDecl.members, true);
    }
}

static void printEnumOption(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    format(context->state, " {s}", (FormatArg[]){{.s = node->enumOption.name}});
    if (node->enumOption.value) {
        format(context->state, " = ", NULL);
        astConstVisit(visitor, node->enumOption.value);
    }
}

static void printStructField(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    format(
        context->state, " {s}: ", (FormatArg[]){{.s = node->structField.name}});
    astConstVisit(visitor, node->structField.type);
    if (node->structField.value) {
        format(context->state, " = ", NULL);
        astConstVisit(visitor, node->structField.value);
    }
}

static void printStructDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);

    printKeyword(context->state, "struct");
    format(context->state, " {s}", (FormatArg[]){{.s = node->structDecl.name}});
    if (node->structDecl.genericParams) {
        printManyAstsWithDelim(
            visitor, "[", ",", "]", node->structDecl.genericParams);
    }

    if (node->structDecl.base) {
        format(context->state, " : ", NULL);
        astConstVisit(visitor, node->structDecl.base);
    }

    if (node->structDecl.members) {
        if (node->structDecl.isTupleLike)
            printManyAstsWithinParen(visitor, node->structDecl.members);
        else
            printManyAstsWithinBlock(
                visitor, ";", node->structDecl.members, true);
    }
}

static void printExprStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    astConstVisit(visitor, node->exprStmt.expr);
}

static void printBreakContinueStmt(ConstAstVisitor *visitor,
                                   const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    printKeyword(context->state,
                 node->tag == astBreakStmt ? "break" : "continue");
}

static void printReturnStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    printKeyword(context->state, "return");
    if (node->returnStmt.expr) {
        format(context->state, " ", NULL);
        astConstVisit(visitor, node->returnStmt.expr);
    }
}

static void printBlockStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    printManyAstsWithinBlock(visitor, ";\n", node->blockStmt.stmts, true);
}

static void printIfStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    printKeyword(context->state, "if");
    format(context->state, " (", NULL);
    astConstVisit(visitor, node->ifStmt.cond);
    format(context->state, ")", NULL);
    if (node->ifStmt.body->tag != astBlockStmt) {
        format(context->state, " {{{>}\n", NULL);
        astConstVisit(visitor, node->ifStmt.body);
        format(context->state, "{<}\n}", NULL);
    }
    else {
        format(context->state, " ", NULL);
        astConstVisit(visitor, node->ifStmt.body);
    }

    if (node->ifStmt.otherwise) {
        printKeyword(context->state, " else");
        format(context->state, " ", NULL);
        astConstVisit(visitor, node->ifStmt.otherwise);
    }
}

static void printForStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    printKeyword(context->state, "for");
    format(context->state, " (", NULL);
    astConstVisit(visitor, node->forStmt.var);
    format(context->state, " : ", NULL);
    astConstVisit(visitor, node->forStmt.range);
    format(context->state, ")", NULL);
    if (node->forStmt.body->tag != astBlockStmt) {
        format(context->state, " {{\n{>}", NULL);
        astConstVisit(visitor, node->forStmt.body);
        format(context->state, " {>}}", NULL);
    }
    else {
        format(context->state, " ", NULL);
        astConstVisit(visitor, node->forStmt.body);
    }
}

static void printWhileStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    printKeyword(context->state, "while");
    format(context->state, " (", NULL);
    astConstVisit(visitor, node->whileStmt.cond);
    format(context->state, ")", NULL);
    if (node->whileStmt.body) {
        if (node->whileStmt.body->tag != astBlockStmt) {
            format(context->state, " {{{>}\n", NULL);
            astConstVisit(visitor, node->whileStmt.body);
            format(context->state, " {<}\n}", NULL);
        }
        else {
            format(context->state, " ", NULL);
            astConstVisit(visitor, node->whileStmt.body);
        }
    }
    else {
        format(context->state, ";", NULL);
    }
}

static void printCaseStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    if (node->caseStmt.isDefault) {
        printKeyword(context->state, "default");
    }
    else {
        printKeyword(context->state, "case");
        format(context->state, " ", NULL);
        astConstVisit(visitor, node->caseStmt.match);
    }
    format(context->state, ":", NULL);
    if (node->caseStmt.body) {
        if (node->caseStmt.body->tag != astBlockStmt) {
            format(context->state, " {{{>}\n", NULL);
            astConstVisit(visitor, node->caseStmt.body);
            format(context->state, " {<}\n}", NULL);
        }
        else {
            format(context->state, " ", NULL);
            astConstVisit(visitor, node->caseStmt.body);
        }
    }
}

static void printSwitchStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    printKeyword(context->state, "switch");
    format(context->state, "(", NULL);
    astConstVisit(visitor, node->switchStmt.cond);
    format(context->state, ") ", NULL);
    printManyAstsWithinBlock(visitor, "\n", node->switchStmt.cases, true);
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

void astConstVisit(ConstAstVisitor *visitor, const AstNode *node)
{
    if (visitor->visitors[node->tag]) {
        visitor->visitors[node->tag](visitor, node);
    }
}

void printAst(FormatState *state, const AstNode *node)
{
    AstPrintContext context = {.state = state};
    // clang-format off
    ConstAstVisitor visitor = makeConstAstVisitor(&context, {
        [astError] = printError,
        [astImplicitCast] = printError,
        [astAttr] = printAttribute,
        [astPathElem] = printPathElement,
        [astPath] = printPath,
        [astGenericParam] = printGenericParam,
        [astIdentifier] = printIdentifier,
        [astTupleType] = printTupleType,
        [astArrayType] = printArrayType,
        [astPointerType] = printPointerType,
        [astFuncType] = printFuncType,
        [astPrimitiveType] = printPrimitiveType,
        [astNullLit] = printNullLiteral,
        [astBoolLit] = printBoolLiteral,
        [astCharLit] = printCharLiteral,
        [astIntegerLit] = printIntLiteral,
        [astFloatLit] = printFloatLiteral,
        [astStringLit] = printStringLiteral,
        [astFuncParam] = printFuncParam,
        [astFuncDecl] = printFuncDecl,
        [astConstDecl] = printVariableDecl,
        [astVarDecl] = printVariableDecl,
        [astTypeDecl] = printTypeDecl,
        [astUnionDecl] = printUnionDecl,
        [astEnumOption] = printEnumOption,
        [astEnumDecl] = printEnumDecl,
        [astStructField] = printStructField,
        [astStructDecl] = printStructDecl,
        [astGroupExpr] = printGroupExpr,
        [astUnaryExpr] = printUnaryExpr,
        [astBinaryExpr] = printBinaryExpr,
        [astAssignExpr] = printAssignExpr,
        [astTernaryExpr] = printTernaryExpr,
        [astStmtExpr] = printStmtExpr,
        [astStringExpr] = printStringExpr,
        [astTypedExpr] = printTypedExpr,
        [astCallExpr] = printCallExpr,
        [astMacroCallExpr] = printCallExpr,
        [astClosureExpr] = printClosureExpr,
        [astArrayExpr] = printArrayExpr,
        [astIndexExpr] = printIndexExpr,
        [astTupleExpr] = printTupleType,
        [astFieldExpr] = printFieldExpr,
        [astStructExpr] = printStructExpr,
        [astMemberExpr] = printMemberExpr,
        [astExprStmt] = printExprStmt,
        [astBreakStmt] = printBreakContinueStmt,
        [astContinueStmt] = printBreakContinueStmt,
        [astReturnStmt] = printReturnStmt,
        [astBlockStmt] = printBlockStmt,
        [astIfStmt] = printIfStmt,
        [astForStmt] = printForStmt,
        [astWhileStmt] = printWhileStmt,
        [astSwitchStmt] = printSwitchStmt,
        [astCaseStmt] = printCaseStmt,
    });
    // clang-format on

    astConstVisit(&visitor, node);
}

bool isTuple(const AstNode *node)
{
    if (node->tag != astTupleExpr)
        return false;
    if (node->tupleExpr.args->next == NULL)
        return false;
    return true;
}

bool isAssignableExpr(attr(unused) const AstNode *node)
{
    csAssert(node->type, "expression must have been type-checked first");
    return false;
}

bool isPublicDecl(const AstNode *node)
{
    switch (node->tag) {
    case astVarDecl:
    case astConstDecl:
        return node->varDecl.isPublic;
    case astFuncDecl:
        return node->funcDecl.isPublic;
    case astTypeDecl:
        return node->typeDecl.isPublic;
    case astEnumDecl:
        return node->enumDecl.isPublic;
    case astUnionDecl:
        return node->unionDecl.isPublic;
    case astStructDecl:
        return node->structDecl.isPublic;
    default:
        return false;
    }
}

bool isOpaqueDecl(const AstNode *node)
{
    switch (node->tag) {
    case astFuncDecl:
        return node->funcDecl.isNative;
    case astTypeDecl:
        return node->typeDecl.isOpaque;
    case astEnumDecl:
        return node->enumDecl.isOpaque;
    case astUnionDecl:
        return node->unionDecl.isOpaque;
    case astStructDecl:
        return node->structDecl.isOpaque;
    default:
        return false;
    }
}

u64 countAstNodes(const AstNode *node)
{
    u64 len = 0;
    for (; node; node = node->next)
        len++;
    return len;
}

AstNode *getLastAstNode(AstNode *node)
{
    while (node->next)
        node = node->next;
    return node;
}

AstNode *getParentScopeWithTag(AstNode *node, AstTag tag)
{
    AstNode *parentScope = node->parentScope;
    while (parentScope && parentScope->tag != tag)
        parentScope = parentScope->parentScope;
    return parentScope;
}

const AstNode *getLastAstNodeConst(const AstNode *node)
{
    while (node->next)
        node = node->next;
    return node;
}

const AstNode *getParentScopeWithTagConst(const AstNode *node, AstTag tag)
{
    const AstNode *parentScope = node->parentScope;
    while (parentScope && parentScope->tag != tag)
        parentScope = parentScope->parentScope;
    return parentScope;
}

void insertAstNodeAfter(AstNode *before, AstNode *after)
{
    getLastAstNode(after)->next = before->next;
    before->next = after;
}

const char *getPrimitiveTypeName(PrtId tag)
{
    switch (tag) {
#define f(name, str)                                                           \
    case prt##name:                                                            \
        return str;
        PRIM_TYPE_LIST(f)
#undef f
    default:
        csAssert0(false);
    }
}

const char *getUnaryOpString(Operator op)
{
    switch (op) {
#define f(name, _, str)                                                        \
    case op##name:                                                             \
        return str;
        AST_UNARY_EXPR_LIST(f)
#undef f
    default:
        csAssert0(false);
    }
}

const char *getBinaryOpString(Operator op)
{
    switch (op) {
#define f(name, p, t, s, ...)                                                  \
    case op##name:                                                             \
        return s;
        AST_BINARY_EXPR_LIST(f)
#undef f
    default:
        csAssert0(false);
    }
}

const char *getAssignOpString(Operator op)
{
    switch (op) {
#define f(name, p, t, s, ...)                                                  \
    case op##name:                                                             \
        return s "=";
        AST_ASSIGN_EXPR_LIST(f)
#undef f
    default:
        csAssert0(false);
    }
}

const char *getBinaryOpFuncName(Operator op)
{
    switch (op) {
#define f(name, p, t, s, fn)                                                   \
    case op##name:                                                             \
        return fn;
        // NOLINTBEGIN
        AST_BINARY_EXPR_LIST(f)
        // NOLINTEND
#undef f
    default:
        csAssert0(false);
    }
}

const char *getDeclKeyword(AstTag tag)
{
    switch (tag) {
    case astFuncDecl:
        return "func";
    case astTypeDecl:
    case astUnionDecl:
        return "type";
    case astEnumDecl:
        return "enum";
    case astStructDecl:
        return "struct";
    default:
        return false;
    }
}

const char *getDeclName(const AstNode *node)
{
    switch (node->tag) {
    case astFuncDecl:
        return node->funcDecl.name;
    case astTypeDecl:
        return node->typeDecl.name;
    case astEnumDecl:
        return node->enumDecl.name;
    case astUnionDecl:
        return node->unionDecl.name;
    case astStructDecl:
        return node->structDecl.name;
    default:
        return false;
    }
}

int getMaxBinaryOpPrecedence(void)
{
    static int maxPrecedence = -1;
    if (maxPrecedence < 1) {
        const int precedenceList[] = {
#define f(n, prec, ...) prec,
            AST_BINARY_EXPR_LIST(f)
#undef f
        };
        for (int i = 0; i < (sizeof(precedenceList) / sizeof(int)); i++) {
            maxPrecedence = MAX(maxPrecedence, precedenceList[i]);
        }
        maxPrecedence += 1;
    }

    return maxPrecedence;
}

int getBinaryOpPrecedence(Operator op)
{
    switch (op) {
#define f(name, prec, ...)                                                     \
    case op##name:                                                             \
        return prec;
        // NOLINTBEGIN
        AST_BINARY_EXPR_LIST(f);
        // NOLINTEND
#undef f
    default:
        return getMaxBinaryOpPrecedence();
    }
}

bool isAssignmentOperator(TokenTag tag)
{
    switch (tag) {
#define f(O, P, T, ...)                                                        \
    case tok##T:                                                               \
        AST_ASSIGN_EXPR_LIST(f)
        return true;
#undef f
    default:
        return false;
    }
}

Operator tokenToUnaryOperator(TokenTag tag)
{
    switch (tag) {
#define f(O, T, ...)                                                           \
    case tok##T:                                                               \
        return op##O;
        AST_PREFIX_EXPR_LIST(f);
#undef f
    default:
        csAssert(false, "expecting unary operator");
    }
}

Operator tokenToBinaryOperator(TokenTag tag)
{
    switch (tag) {
#define f(O, P, T, ...)                                                        \
    case tok##T:                                                               \
        return op##O;
        AST_BINARY_EXPR_LIST(f);
#undef f
    default:
        return opInvalid;
    }
}

Operator tokenToAssignmentOperator(TokenTag tag)
{
    switch (tag) {
#define f(O, P, T, ...)                                                        \
    case tok##T:                                                               \
        return op##O;
        AST_ASSIGN_EXPR_LIST(f);
#undef f
    default:
        csAssert(false, "expecting binary operator");
    }
}

bool isPrefixOpKeyword(Operator op)
{
    switch (op) {
#define f(O, T, ...)                                                           \
    case op##O:                                                                \
        return isKeyword(tok##T);
        AST_PREFIX_EXPR_LIST(f)
#undef f
    default:
        return false;
    }
}
