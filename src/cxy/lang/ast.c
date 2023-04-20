
#include "ast.h"

#include <memory.h>

typedef struct {
    FormatState *state;
    u32 isWithinStruct;
    const AstNode *parent;
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

static void printAttributes(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    if (node) {
        format(context->state, "@", NULL);
        if (node->next)
            printManyAstsWithDelim(visitor, "[", ", ", "]", node);
        else
            astConstVisit(visitor, node);
    }
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
    if (expr->unaryExpr.isPrefix) {
        if (isPrefixOpKeyword(op)) {
            printKeyword(context->state, getUnaryOpString(op));
            format(context->state, " ", NULL);
            astConstVisit(visitor, expr->unaryExpr.operand);
        }
        else
            printAstWithDelim(
                visitor, getUnaryOpString(op), "", expr->unaryExpr.operand);
    }
    else {
        printAstWithDelim(
            visitor, "", getUnaryOpString(op), expr->unaryExpr.operand);
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
    if (expr->flags & flgAsync)
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
    printManyAstsWithDelim(visitor, ".[", ", ", "]", node->indexExpr.index);
}

static void printRangeExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    format(context->state, "range(", NULL);
    astConstVisit(visitor, node->rangeExpr.start);
    format(context->state, ", ", NULL);
    astConstVisit(visitor, node->rangeExpr.end);
    if (node->rangeExpr.step) {
        format(context->state, ", ", NULL);
        astConstVisit(visitor, node->rangeExpr.step);
    }
    format(context->state, ")", NULL);
}

static void printCastExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    format(context->state, "<", NULL);
    astConstVisit(visitor, node->castExpr.to);
    format(context->state, ">", NULL);
    astConstVisit(visitor, node->castExpr.expr);
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

static void printOptionalType(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->optionalType.type);
    format(context->state, "?", NULL);
}

static void printArrayType(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    format(context->state, "[", NULL);
    astConstVisit(visitor, node->arrayType.elementType);
    if (node->arrayType.dim != NULL)
        printManyAstsWithDelim(visitor, "", ", ", "", node->arrayType.dim);
    format(context->state, "]", NULL);
}

static void printPointerType(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    format(context->state, "&", NULL);
    if (node->flags & flgConst) {
        printKeyword(context->state, "const");
        format(context->state, " ", NULL);
    }
    astConstVisit(visitor, node->pointerType.pointed);
}

static void printFuncType(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    if (node->flags & flgAsync) {
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
    if (node->attrs) {
        printAttributes(visitor, node->attrs);
        format(context->state, " ", NULL);
    }

    if (node->flags & flgVariadic)
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

static void printProgram(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    format(context->state,
           "{$}/*\n"
           "* Generated from {s}\n"
           "*/{$}\n\n",
           (FormatArg[]){{.style = commentStyle},
                         {.s = node->loc.fileName},
                         {.style = resetStyle}});
    printManyAstsWithDelim(visitor, "", "\n\n", "", node->program.decls);
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

static void printGenericDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    context->parent = node;
    astConstVisit(visitor, node->genericDecl.decl);
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
    if (node->attrs) {
        printAttributes(visitor, node->attrs);
        format(context->state, "\n", NULL);
    }

    if (context->isWithinStruct && !(node->flags & flgPublic))
        format(context->state, "- ", NULL);

    if (node->flags & flgPublic) {
        printKeyword(context->state, "async");
        format(context->state, " ", NULL);
    }

    printKeyword(context->state, "func");
    format(context->state, " {s}", (FormatArg[]){{.s = node->funcDecl.name}});
    if (context->parent && nodeIs(context->parent, GenericDecl) &&
        context->parent->genericDecl.params) {
        printManyAstsWithDelim(
            visitor, "[", ",", "]", context->parent->genericDecl.params);
    }
    printManyAstsWithinParen(visitor, node->funcDecl.params);

    if (node->funcDecl.ret) {
        format(context->state, " : ", NULL);
        astConstVisit(visitor, node->funcDecl.ret);
    }
    format(context->state, " ", NULL);

    if (node->funcDecl.body) {
        if (node->funcDecl.body->tag != astBlockStmt)
            format(context->state, " => ", NULL);
        astConstVisit(visitor, node->funcDecl.body);
    }
}

static void printMacroDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);

    printKeyword(context->state, "macro");
    format(context->state, " {s}", (FormatArg[]){{.s = node->macroDecl.name}});
    printManyAstsWithinParen(visitor, node->macroDecl.params);

    if (node->macroDecl.ret) {
        format(context->state, " : ", NULL);
        astConstVisit(visitor, node->macroDecl.ret);
    }

    format(context->state, " ", NULL);
    astConstVisit(visitor, node->macroDecl.body);
}

static void printVariableDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);

    if (node->flags & flgConst)
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
    if (node->attrs) {
        printAttributes(visitor, node->attrs);
        format(context->state, "\n", NULL);
    }

    if (context->isWithinStruct && !(node->flags & flgPublic))
        format(context->state, "- ", NULL);

    printKeyword(context->state, "type");
    format(context->state, " {s}", (FormatArg[]){{.s = node->typeDecl.name}});
    if (context->parent && nodeIs(context->parent, GenericDecl) &&
        context->parent->genericDecl.params) {
        printManyAstsWithDelim(
            visitor, "[", ",", "]", context->parent->genericDecl.params);
    }

    if (node->typeDecl.aliased) {
        format(context->state, " = ", NULL);
        astConstVisit(visitor, node->typeDecl.aliased);
    }
}

static void printUnionDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    if (node->attrs) {
        printAttributes(visitor, node->attrs);
        format(context->state, "\n", NULL);
    }

    if (context->isWithinStruct && !(node->flags & flgPublic))
        format(context->state, "- ", NULL);

    printKeyword(context->state, "type");
    format(context->state, " {s}", (FormatArg[]){{.s = node->unionDecl.name}});
    if (context->parent && nodeIs(context->parent, GenericDecl) &&
        context->parent->genericDecl.params) {
        printManyAstsWithDelim(
            visitor, "[", ",", "]", context->parent->genericDecl.params);
    }

    if (node->unionDecl.members) {
        format(context->state, " = ", NULL);
        printManyAstsWithDelim(visitor, "", " | ", "", node->unionDecl.members);
    }
}

static void printEnumDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    if (node->attrs) {
        printAttributes(visitor, node->attrs);
        format(context->state, "\n", NULL);
    }

    if (context->isWithinStruct && !(node->flags & flgPublic))
        format(context->state, "- ", NULL);

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
    if (node->attrs) {
        printAttributes(visitor, node->attrs);
        format(context->state, "\n", NULL);
    }

    format(context->state, " {s}", (FormatArg[]){{.s = node->enumOption.name}});
    if (node->enumOption.value) {
        format(context->state, " = ", NULL);
        astConstVisit(visitor, node->enumOption.value);
    }
}

static void printStructField(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    if (node->attrs) {
        printAttributes(visitor, node->attrs);
        format(context->state, "\n", NULL);
    }

    if (node->flags & flgPrivate)
        format(context->state, "- ", NULL);

    format(
        context->state, "{s}: ", (FormatArg[]){{.s = node->structField.name}});
    astConstVisit(visitor, node->structField.type);
    if (node->structField.value) {
        format(context->state, " = ", NULL);
        astConstVisit(visitor, node->structField.value);
    }
}

static void printStructDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    AstPrintContext *context = getConstAstVisitorContext(visitor);
    if (node->attrs) {
        printAttributes(visitor, node->attrs);
        format(context->state, "\n", NULL);
    }

    if (context->isWithinStruct && !(node->flags & flgPublic))
        format(context->state, "- ", NULL);
    context->isWithinStruct++;

    printKeyword(context->state, "struct");
    format(context->state, " {s}", (FormatArg[]){{.s = node->structDecl.name}});
    if (context->parent && nodeIs(context->parent, GenericDecl) &&
        context->parent->genericDecl.params) {
        printManyAstsWithDelim(
            visitor, "[", ",", "]", context->parent->genericDecl.params);
    }

    if (node->structDecl.base) {
        format(context->state, " : ", NULL);
        astConstVisit(visitor, node->structDecl.base);
    }
    format(context->state, " ", NULL);

    if (node->structDecl.members) {
        printManyAstsWithinBlock(visitor, "\n", node->structDecl.members, true);
    }

    context->isWithinStruct--;
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
    printManyAstsWithinBlock(visitor, "\n", node->blockStmt.stmts, true);
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
    if (node->flags & flgDefault) {
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

AstNode *copyAstNode(MemPool *pool, const AstNode *node)
{
    AstNode *copy = allocFromMemPool(pool, sizeof(AstNode));
    memcpy(copy, node, sizeof(AstNode));
    copy->next = NULL;
    copy->parentScope = NULL;
    return copy;
}

void astVisit(AstVisitor *visitor, AstNode *node)
{
    if (visitor->visitors[node->tag]) {
        visitor->visitors[node->tag](visitor, node);
    }
    else if (visitor->fallback) {
        visitor->fallback(visitor, node);
    }
}

void astConstVisit(ConstAstVisitor *visitor, const AstNode *node)
{
    if (visitor->visitors[node->tag]) {
        visitor->visitors[node->tag](visitor, node);
    }
    else if (visitor->fallback) {
        visitor->fallback(visitor, node);
    }
}

void printAst(FormatState *state, const AstNode *node)
{
    AstPrintContext context = {.state = state};
    // clang-format off
    ConstAstVisitor visitor = makeConstAstVisitor(&context, {
        [astError] = printError,
        [astProgram] = printProgram,
        [astAttr] = printAttribute,
        [astPathElem] = printPathElement,
        [astPath] = printPath,
        [astGenericParam] = printGenericParam,
        [astGenericDecl] = printGenericDecl,
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
        [astMacroDecl] = printMacroDecl,
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
        [astRangeExpr] = printRangeExpr,
        [astCastExpr] = printCastExpr,
        [astTupleExpr] = printTupleType,
        [astOptionalType] = printOptionalType,
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
    csAssert(node->type, "expression must have been key-checked first");
    return false;
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

AstNode *getNodeAtIndex(AstNode *node, u64 index)
{
    u64 i = 0;
    while (node) {
        if (i == index)
            return node;
        node = node->next;
        i++;
    }
    return NULL;
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

const AstNode *getConstNodeAtIndex(const AstNode *node, u64 index)
{
    u64 i = 0;
    while (node) {
        if (i == index)
            return node;
        node = node->next;
        i++;
    }
    return NULL;
}

const AstNode *getParentScopeWithTagConst(const AstNode *node, AstTag tag)
{
    const AstNode *parentScope = node->parentScope;
    while (parentScope && parentScope->tag != tag)
        parentScope = parentScope->parentScope;
    return parentScope;
}

AstNode *cloneManyAstNodes(MemPool *pool, const AstNode *nodes)
{
    AstNode *first = NULL, *node = NULL;
    while (nodes) {
        if (!first) {
            first = cloneAstNode(pool, nodes);
            node = first;
        }
        else {
            node->next = cloneAstNode(pool, nodes);
            node = node->next;
        }
        nodes = nodes->next;
    }
    return first;
}

AstNode *cloneAstNode(MemPool *pool, const AstNode *node)
{
    if (node == NULL)
        return NULL;

    AstNode *clone = copyAstNode(pool, node);
    clone->attrs = cloneManyAstNodes(pool, node->attrs);

#define CLONE_MANY(AST, MEMBER)                                                \
    clone->AST.MEMBER = cloneManyAstNodes(pool, node->AST.MEMBER);
#define CLONE_ONE(AST, MEMBER)                                                 \
    clone->AST.MEMBER = cloneAstNode(pool, node->AST.MEMBER);

    switch (clone->tag) {
    case astProgram:
        CLONE_MANY(program, decls);
        break;
    case astCastExpr:
        CLONE_ONE(castExpr, expr);
        break;
    case astPathElem:
        CLONE_ONE(pathElement, args);
        break;
    case astPath:
        CLONE_MANY(path, elements);
        break;
    case astGenericParam:
        CLONE_MANY(genericParam, constraints);
        break;
    case astGenericDecl:
        CLONE_MANY(genericDecl, params);
        CLONE_ONE(genericDecl, decl);
        break;
    case astTupleType:
        CLONE_MANY(tupleType, args);
        break;
    case astArrayType:
        CLONE_ONE(arrayType, elementType);
        CLONE_MANY(arrayType, dim);
        break;
    case astPointerType:
        CLONE_MANY(pointerType, pointed);
        break;
    case astFuncType:
        CLONE_ONE(funcType, ret);
        CLONE_MANY(funcType, params);
        break;
    case astFuncParam:
        CLONE_ONE(funcParam, type);
        CLONE_ONE(funcParam, def);
        break;
    case astFuncDecl:
        CLONE_MANY(funcDecl, params);
        CLONE_ONE(funcDecl, ret);
        CLONE_ONE(funcDecl, body);
        break;
    case astMacroDecl:
        CLONE_MANY(macroDecl, params);
        CLONE_ONE(macroDecl, ret);
        CLONE_ONE(macroDecl, body);
        break;
    case astVarDecl:
        CLONE_ONE(varDecl, type);
        CLONE_ONE(varDecl, init);
        CLONE_MANY(varDecl, names);
        break;
    case astTypeDecl:
        CLONE_ONE(typeDecl, aliased);
        break;
    case astUnionDecl:
        CLONE_MANY(unionDecl, members);
        break;

    case astEnumOption:
    case astEnumDecl:
    case astStructField:
    case astStructDecl:
        break; // TODO

    case astGroupExpr:
        CLONE_ONE(groupExpr, expr);
        break;
    case astUnaryExpr:
    case astAddressOf:
        CLONE_ONE(unaryExpr, operand);
        break;
    case astBinaryExpr:
        CLONE_ONE(binaryExpr, lhs);
        CLONE_ONE(binaryExpr, rhs);
        break;
    case astAssignExpr:
        CLONE_ONE(assignExpr, lhs);
        CLONE_ONE(assignExpr, rhs);
        break;
    case astTernaryExpr:
        CLONE_ONE(ternaryExpr, cond);
        CLONE_ONE(ternaryExpr, body);
        CLONE_ONE(ternaryExpr, otherwise);
        break;
    case astStmtExpr:
        CLONE_ONE(stmtExpr, stmt);
        break;
    case astStringExpr:
        CLONE_MANY(stringExpr, parts);
        break;
    case astTypedExpr:
        CLONE_ONE(typedExpr, expr);
        CLONE_ONE(typedExpr, type);
        break;
    case astCallExpr:
    case astMacroCallExpr:
        CLONE_ONE(callExpr, callee);
        CLONE_MANY(callExpr, args);
        break;
    case astClosureExpr:
        CLONE_ONE(closureExpr, ret);
        CLONE_MANY(closureExpr, params);
        CLONE_ONE(closureExpr, body);
        break;
    case astArrayExpr:
        CLONE_MANY(arrayExpr, elements);
        break;
    case astIndexExpr:
        CLONE_ONE(indexExpr, target);
        CLONE_MANY(indexExpr, index);
        break;
    case astTupleExpr:
        CLONE_MANY(tupleExpr, args);
        break;
    case astFieldExpr:
    case astStructExpr:
        break; // TODO

    case astMemberExpr:
        CLONE_ONE(memberExpr, target);
        CLONE_ONE(memberExpr, member);
        break;
    case astExprStmt:
        CLONE_ONE(exprStmt, expr);
        break;
    case astDeferStmt:
        CLONE_ONE(deferStmt, expr);
        break;
    case astReturnStmt:
        CLONE_ONE(returnStmt, expr);
        break;
    case astBlockStmt:
        CLONE_MANY(blockStmt, stmts);
        break;
    case astIfStmt:
        CLONE_ONE(ifStmt, cond);
        CLONE_ONE(ifStmt, body);
        CLONE_ONE(ifStmt, otherwise);
        break;
    case astForStmt:
        CLONE_ONE(forStmt, var);
        CLONE_ONE(forStmt, range);
        CLONE_ONE(forStmt, body);
        break;
    case astWhileStmt:
        CLONE_ONE(whileStmt, cond);
        CLONE_ONE(whileStmt, body);
        break;
    case astSwitchStmt:
        CLONE_ONE(switchStmt, cond);
        CLONE_MANY(switchStmt, cases);
        break;
    case astCaseStmt:
        CLONE_ONE(caseStmt, match);
        CLONE_ONE(caseStmt, match);
        break;

    case astError:
    case astIdentifier:
    case astVoidType:
    case astStringType:
    case astPrimitiveType:
    case astNullLit:
    case astBoolLit:
    case astCharLit:
    case astIntegerLit:
    case astFloatLit:
    case astStringLit:
    case astBreakStmt:
    case astContinueStmt:
    default:
        break;
    }

    return clone;
}

void insertAstNodeAfter(AstNode *before, AstNode *after)
{
    getLastAstNode(after)->next = before->next;
    before->next = after;
}

void insertAstNode(AstNodeList *list, AstNode *node)
{
    if (list->first == NULL) {
        list->first = node;
    }
    else {
        list->last->next = node;
    }
    list->last = node;
}

void unlinkAstNode(AstNode **head, AstNode *prev, AstNode *node)
{
    if (prev == node)
        *head = node->next;
    else
        prev->next = node->next;
}

const char *getPrimitiveTypeName(PrtId tag)
{
    switch (tag) {
#define f(name, str, ...)                                                      \
    case prt##name:                                                            \
        return str;
        PRIM_TYPE_LIST(f)
#undef f
    default:
        csAssert0(false);
    }
}

u64 getPrimitiveTypeSize(PrtId tag)
{
    switch (tag) {
#define f(name, str, size)                                                     \
    case prt##name:                                                            \
        return size;
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
        return "op_" fn;
        // NOLINTBEGIN
        AST_BINARY_EXPR_LIST(f)
        // NOLINTEND
#undef f
    case opNew:
        return "op_new";
    case opDelete:
        return "op_delete";
    case opCallOverload:
        return "op_call";
    case opStringOverload:
        return "op_str";
    case opIndexAssignOverload:
        return "op_idx_assign";
    case opIndexOverload:
        return "op_idx";
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
#define f(O, P, T, ...) case tok##T##Equal:
        AST_ASSIGN_EXPR_LIST(f)
#undef f
        return true;
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
    case tok##T##Equal:                                                        \
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
