//
// Created by Carter Mbotho on 2024-02-02.
//

#include "lang/frontend/ast.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/visitor.h"

#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"
#include "lang/operations.h"

#include <unistd.h>

typedef struct DumpContext {
    FormatState *state;
    const AstNode *currentFunction;
    bool isSimplified;
} DumpContext;

static void dumpManyAstNodesEnclosed(ConstAstVisitor *visitor,
                                     const AstNode *node,
                                     cstring open,
                                     cstring sep,
                                     cstring close)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    if (open)
        format(ctx->state, open, NULL);
    const AstNode *it = node;
    for (; it; it = it->next) {
        astConstVisit(visitor, it);
        if (it->next) {
            format(ctx->state, sep, NULL);
        }
    }

    if (close)
        format(ctx->state, close, NULL);
}

static void dumpManyAstNodes(ConstAstVisitor *visitor,
                             const AstNode *node,
                             cstring sep)
{
    dumpManyAstNodesEnclosed(visitor, node, NULL, sep, NULL);
}

#define AddNewLine() format(ctx->state, "\n", NULL)
#define AddSpace() format(ctx->state, " ", NULL)

#define AddComptime()                                                          \
    if (hasFlag(node, Comptime))                                               \
        format(ctx->state, "#", NULL);

#define AddConst()                                                             \
    if (hasFlag(node, Const)) {                                                \
        printKeyword(ctx->state, "const");                                     \
        format(ctx->state, " ", NULL);                                         \
    }

static void dumpFunctionName(DumpContext *ctx, const AstNode *node)
{
    if (ctx->isSimplified && isMemberFunction(node)) {
        const Type *type = stripAll(node->funcDecl.signature->params->type);
        format(ctx->state, "{s}_", (FormatArg[]){{.s = type->name}});
    }
    format(ctx->state, "{s}", (FormatArg[]){{.s = node->funcDecl.name}});
}

static void dumpImportEntity(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "{s}", (FormatArg[]){{.s = node->importEntity.name}});
    if (node->importEntity.alias) {
        AddSpace();
        printKeyword(ctx->state, "as");
        format(
            ctx->state, " {s}", (FormatArg[]){{.s = node->importEntity.alias}});
    }
}

static void dumpBackendCall(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    printKeyword(ctx->state, "__bc");
    format(ctx->state,
           "({u64}",
           (FormatArg[]){{.u64 = node->backendCallExpr.func}});
    dumpManyAstNodesEnclosed(visitor,
                             node->backendCallExpr.args,
                             node->backendCallExpr.args ? ", " : NULL,
                             ", ",
                             ")");
}

static void dumpTypeRef(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    printType_(ctx->state, node->type, false);
}

static void dumpPathElement(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    if (nodeIs(node->pathElement.resolvesTo, GenericParam) && node->type)
        format(ctx->state, "{t}", (FormatArg[]){{.t = node->type}});
    else if (node->pathElement.isKeyword)
        printKeyword(ctx->state, node->pathElement.name);
    else
        format(ctx->state, "{s}", (FormatArg[]){{.s = node->pathElement.name}});

    if (node->pathElement.args)
        dumpManyAstNodesEnclosed(
            visitor, node->pathElement.args, "[", ", ", "]");
}

static void dumpPath(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *base = node->path.elements;
    if (hasFlag(ctx->currentFunction, Generated) && hasFlag(node, AddThis) &&
        base->pathElement.name != S_this) {
        printKeyword(ctx->state, "this");
        printEscapedChar(ctx->state, '.');
    }
    dumpManyAstNodes(visitor, node->path.elements, ".");
}

static void dumpIdentifier(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    if (nodeIs(node->ident.resolvesTo, GenericParam) && node->type)
        format(ctx->state, "{t}", (FormatArg[]){{.t = node->type}});
    else
        format(ctx->state, "{s}", (FormatArg[]){{.s = node->ident.value}});
}

static void dumpAttribute(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "@[{s}", (FormatArg[]){{.s = node->attr.name}});
    if (node->attr.args)
        dumpManyAstNodesEnclosed(visitor, node->attr.args, "(", ", ", ")");

    format(ctx->state, "]\n", NULL);
}

static void dumpFuncParam(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    if (hasFlag(node, Variadic))
        printWithStyle(ctx->state, "...", ellipsisStyle);

    if (node->funcParam.name == S_this) {
        printKeyword(ctx->state, "this");
        append(ctx->state, ": ", 2);
    }
    else
        format(ctx->state, "{s}: ", (FormatArg[]){{.s = node->funcParam.name}});

    if (node->funcParam.type)
        astConstVisit(visitor, node->funcParam.type);
    else if (node->type)
        printType_(ctx->state, node->type, false);
    else
        printKeyword(ctx->state, "auto");

    if (node->funcParam.def) {
        format(ctx->state, " = ", NULL);
        astConstVisit(visitor, node->funcParam.def);
    }
}

static void dumpGenericParam(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "{s}", (FormatArg[]){{.s = node->genericParam.name}});
    if (node->genericParam.constraints) {
        format(ctx->state, ": ", NULL);
        dumpManyAstNodes(visitor, node->genericParam.constraints, " | ");
    }
}

static void dumpStringLiteral(DumpContext *ctx, cstring value, bool quotes)
{
    if (quotes)
        format(ctx->state, "{$}\"", (FormatArg[]){{.style = stringStyle}});
    else
        format(ctx->state, "{$}", (FormatArg[]){{.style = stringStyle}});

    cstring p = value;
    while (*p) {
        printEscapedChar(ctx->state, *p++);
    }

    if (quotes)
        format(ctx->state, "\"{$}", (FormatArg[]){{.style = resetStyle}});
    else
        format(ctx->state, "{$}", (FormatArg[]){{.style = resetStyle}});
}

static void dumpStructField(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    if (hasFlag(node, Private))
        format(ctx->state, "- ", NULL);

    format(ctx->state, "{s}", (FormatArg[]){{.s = node->structField.name}});

    if (node->structField.type) {
        format(ctx->state, ": ", NULL);
        astConstVisit(visitor, node->structField.type);
    }

    if (node->structField.value) {
        format(ctx->state, " = ", NULL);
        astConstVisit(visitor, node->structField.value);
    }
}

static void dumpNullLit(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    printKeyword(ctx->state, "null");
}

static void dumpBoolLit(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state,
           "{$}{b}{$}",
           (FormatArg[]){{.style = keywordStyle},
                         {.b = node->boolLiteral.value},
                         {.style = resetStyle}});
}

static void dumpCharLit(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state,
           "{$}'{cE}'{$}",
           (FormatArg[]){{.style = stringStyle},
                         {.c = node->charLiteral.value},
                         {.style = resetStyle}});
}

static void dumpIntegerLit(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    if (node->intLiteral.isNegative)
        format(ctx->state,
               "{$}{i64}{$}",
               (FormatArg[]){{.style = literalStyle},
                             {.u64 = node->intLiteral.value},
                             {.style = resetStyle}});
    else
        format(ctx->state,
               "{$}{u64}{$}",
               (FormatArg[]){{.style = literalStyle},
                             {.u64 = node->intLiteral.uValue},
                             {.style = resetStyle}});
}

static void dumpFloatLit(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state,
           "{$}{f64}{$}",
           (FormatArg[]){{.style = literalStyle},
                         {.f64 = node->floatLiteral.value},
                         {.style = resetStyle}});
}

static void dumpStringLit(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    dumpStringLiteral(ctx, node->stringLiteral.value, true);
}

static void dumpPrimitiveType(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    AddConst();

    switch (node->primitiveType.id) {
#define ff(ID, NAME, ...)                                                      \
    case prt##ID:                                                              \
        printKeyword(ctx->state, NAME);                                        \
        break;
        PRIM_TYPE_LIST(ff)
    default:
        unreachable("Not a primitive type");
    }
}

static void dumpBuiltinType(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    AddConst();

    if (nodeIs(node, StringType))
        printKeyword(ctx->state, "string");
    else if (nodeIs(node, VoidType))
        printKeyword(ctx->state, "void");
    else if (nodeIs(node, AutoType))
        printKeyword(ctx->state, "auto");
    else
        unreachable("not a builtin type");
}

static void dumpFuncType(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    AddConst();

    printKeyword(ctx->state, "func");
    dumpManyAstNodesEnclosed(visitor, node->funcType.params, "(", ", ", ")");
    format(ctx->state, " -> ", NULL);
    astConstVisit(visitor, node->funcType.ret);
}

static void dumpArrayType(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    AddConst();
    format(ctx->state, "[", NULL);
    astConstVisit(visitor, node->arrayType.elementType);
    if (node->arrayType.dim) {
        format(ctx->state, ", ", NULL);
        astConstVisit(visitor, node->arrayType.dim);
    }
    format(ctx->state, "]", NULL);
}

static void dumpUnionType(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    AddConst();
    dumpManyAstNodes(visitor, node->unionDecl.members, " | ");
}

static void dumpTupleType(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    AddConst();
    dumpManyAstNodesEnclosed(visitor, node->tupleType.elements, "(", ", ", ")");
}

static void dumpOptionalType(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    AddConst();
    astConstVisit(visitor, node->optionalType.type);
    format(ctx->state, "?", NULL);
}

static void dumpPointerType(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "&", NULL);
    AddConst();
    astConstVisit(visitor, node->optionalType.type);
}

static void dumpStringExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    AstNode *part = node->stringExpr.parts;
    printWithStyle(ctx->state, "\"", stringStyle);
    for (; part; part = part->next) {
        if (nodeIs(part, StringLit))
            dumpStringLiteral(ctx, part->stringLiteral.value, false);
        else {
            format(ctx->state, "${{", NULL);
            astConstVisit(visitor, part);
            format(ctx->state, "}", NULL);
        }
    }
    printWithStyle(ctx->state, "\"", stringStyle);
}

static void dumpBinaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->binaryExpr.lhs);
    format(ctx->state,
           " {s} ",
           (FormatArg[]){{.s = getBinaryOpString(node->binaryExpr.op)}});
    astConstVisit(visitor, node->binaryExpr.rhs);
}

static void dumpAssignExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->assignExpr.lhs);
    format(ctx->state,
           " {s} ",
           (FormatArg[]){{.s = getAssignOpString(node->binaryExpr.op)}});
    astConstVisit(visitor, node->assignExpr.rhs);
}

static void dumpUnaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    if (node->unaryExpr.isPrefix) {
        format(ctx->state,
               "{s}",
               (FormatArg[]){{.s = getUnaryOpString(node->unaryExpr.op)}});
        astConstVisit(visitor, node->unaryExpr.operand);
    }
    else {
        astConstVisit(visitor, node->unaryExpr.operand);
        format(ctx->state,
               "{s}",
               (FormatArg[]){{.s = getUnaryOpString(node->unaryExpr.op)}});
    }
}

static void dumpTernaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->ternaryExpr.cond);
    format(ctx->state, "?", NULL);
    if (node->ternaryExpr.body) {
        format(ctx->state, " ", NULL);
        astConstVisit(visitor, node->ternaryExpr.body);
    }
    format(ctx->state, ": ", NULL);
    astConstVisit(visitor, node->ternaryExpr.otherwise);
}

static void dumpGroupExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "(", NULL);
    astConstVisit(visitor, node->groupExpr.expr);
    format(ctx->state, ")", NULL);
}

static void dumpStmtExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    astConstVisit(visitor, node->stmtExpr.stmt);
}

static void dumpCastExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    printEscapedChar(ctx->state, '<');
    astConstVisit(visitor, node->castExpr.to);
    printEscapedChar(ctx->state, '>');
    astConstVisit(visitor, node->castExpr.expr);
}

static void dumpTypedExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    printEscapedChar(ctx->state, '(');
    astConstVisit(visitor, node->castExpr.expr);
    append(ctx->state, " : ", 3);
    astConstVisit(visitor, node->castExpr.to);
    printEscapedChar(ctx->state, ')');
}

static void dumpRangeExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    printKeyword(ctx->state, "range");
    format(ctx->state, "(", NULL);
    astConstVisit(visitor, node->rangeExpr.start);
    format(ctx->state, ", ", NULL);
    astConstVisit(visitor, node->rangeExpr.end);
    if (node->rangeExpr.step) {
        format(ctx->state, ", ", NULL);
        astConstVisit(visitor, node->rangeExpr.step);
        if (node->rangeExpr.down) {
            format(ctx->state, ", ", NULL);
            printKeyword(ctx->state, "true");
        }
    }
    format(ctx->state, ")", NULL);
}

static void dumpMemberExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);

    astConstVisit(visitor, node->memberExpr.target);
    format(ctx->state, ".", NULL);
    astConstVisit(visitor, node->memberExpr.member);
}

static void dumpIndexExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);

    astConstVisit(visitor, node->indexExpr.target);
    format(ctx->state, ".[", NULL);
    astConstVisit(visitor, node->indexExpr.index);
    format(ctx->state, "]", NULL);
}

static void dumpCallExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *func = node->callExpr.callee->type
                              ? node->callExpr.callee->type->func.decl
                              : NULL;
    if (isMemberFunction(func) && nodeIs(node->callExpr.callee, Identifier))
        dumpFunctionName(ctx, func);
    else
        astConstVisit(visitor, node->callExpr.callee);

    dumpManyAstNodesEnclosed(visitor, node->callExpr.args, "(", ", ", ")");
}

static void dumpMacroCallExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    astConstVisit(visitor, node->callExpr.callee);
    dumpManyAstNodesEnclosed(visitor, node->callExpr.args, "!(", ", ", ")");
}

static void dumpUnionValueExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    astConstVisit(visitor, node->unionValue.value);
}

static void dumpTupleExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    dumpManyAstNodesEnclosed(visitor, node->tupleExpr.elements, "(", ", ", ")");
}

static void dumpArrayExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    dumpManyAstNodesEnclosed(visitor, node->tupleExpr.elements, "[", ", ", "]");
}

static void dumpFieldExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "{s}", (FormatArg[]){{.s = node->fieldExpr.name}});
    format(ctx->state, " = ", NULL);
    astConstVisit(visitor, node->fieldExpr.value);
}

static void dumpStructExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    astConstVisit(visitor, node->structExpr.left);
    dumpManyAstNodesEnclosed(visitor, node->structExpr.fields, "{{", ", ", "}");
}

static void dumpBlockStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    if (hasFlag(node, BlockReturns))
        printEscapedChar(ctx->state, '(');

    if (node->blockStmt.stmts) {
        format(ctx->state, "{{{>}\n", NULL);
        dumpManyAstNodes(visitor, node->blockStmt.stmts, "\n");
        format(ctx->state, "{<}\n}", NULL);
    }
    else {
        format(ctx->state, "{{ }\n", NULL);
    }

    if (hasFlag(node, BlockReturns))
        printEscapedChar(ctx->state, ')');
}

static void dumpIfStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    AddComptime();
    printKeyword(ctx->state, "if");
    format(ctx->state, " (", NULL);
    astConstVisit(visitor, node->ifStmt.cond);
    format(ctx->state, ") ", NULL);
    if (!nodeIs(node->ifStmt.body, BlockStmt)) {
        format(ctx->state, "{{{>}\n", NULL);
        astConstVisit(visitor, node->ifStmt.body);
        format(ctx->state, "{<}\n}", NULL);
    }
    else
        astConstVisit(visitor, node->ifStmt.body);

    if (!node->ifStmt.otherwise)
        return;

    if (nodeIs(node->ifStmt.otherwise, BlockStmt)) {
        AddNewLine();
        printKeyword(ctx->state, "else");
        AddSpace();
        astConstVisit(visitor, node->ifStmt.otherwise);
    }
    else if (nodeIs(node->ifStmt.otherwise, IfStmt)) {
        AddSpace();
        printKeyword(ctx->state, "else");
        AddSpace();
        astConstVisit(visitor, node->ifStmt.otherwise);
    }
    else {
        AddNewLine();
        printKeyword(ctx->state, "else");
        AddSpace();
        format(ctx->state, "{{{>}\n", NULL);
        astConstVisit(visitor, node->ifStmt.otherwise);
        format(ctx->state, "{<}\n}", NULL);
    }
}

static void dumpWhileStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    AddComptime();
    printKeyword(ctx->state, "while");
    format(ctx->state, " (", NULL);
    astConstVisit(visitor, node->whileStmt.cond);
    format(ctx->state, ")\n", NULL);
    if (!nodeIs(node->whileStmt.body, BlockStmt)) {
        format(ctx->state, "{{{>}\n", NULL);
        astConstVisit(visitor, node->whileStmt.body);
        format(ctx->state, "{<}\n}", NULL);
    }
    else
        astConstVisit(visitor, node->whileStmt.body);
}

static void dumpForStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    AddComptime();
    printKeyword(ctx->state, "for");
    format(ctx->state, " (", NULL);
    astConstVisit(visitor, node->forStmt.var);
    format(ctx->state, ": ", NULL);
    astConstVisit(visitor, node->forStmt.range);
    format(ctx->state, ")\n", NULL);
    if (!nodeIs(node->forStmt.body, BlockStmt)) {
        format(ctx->state, "{{{>}\n", NULL);
        astConstVisit(visitor, node->forStmt.body);
        format(ctx->state, "{<}\n}", NULL);
    }
    else
        astConstVisit(visitor, node->forStmt.body);
}

static void dumpExprStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->exprStmt.expr);
}

static void dumpDeferStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    printKeyword(ctx->state, "defer");
    AddSpace();
    astConstVisit(visitor, node->deferStmt.expr);
}

static void dumpBreakStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    printKeyword(ctx->state, "break");
}

static void dumpContinueStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    printKeyword(ctx->state, "continue");
}

static void dumpReturnStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    printKeyword(ctx->state, "return");
    if (node->returnStmt.expr) {
        AddSpace();
        astConstVisit(visitor, node->returnStmt.expr);
    }
}

static void dumpCaseStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    if (node->caseStmt.match) {
        printKeyword(ctx->state, "case");
        AddSpace();
        astConstVisit(visitor, node->caseStmt.match);
    }
    else {
        printKeyword(ctx->state, "default");
    }

    if (node->caseStmt.variable) {
        AddSpace();
        printKeyword(ctx->state, "as");
        AddSpace();
        astConstVisit(visitor, node->caseStmt.variable);
    }
    if (node->caseStmt.body) {
        format(ctx->state, " => ", NULL);
        astConstVisit(visitor, node->caseStmt.body);
    }
}

static void dumpSwitchStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    printKeyword(ctx->state, "switch");
    AddSpace();
    format(ctx->state, "(", NULL);
    astConstVisit(visitor, node->switchStmt.cond);
    format(ctx->state, ") {{\n", NULL);
    dumpManyAstNodes(visitor, node->switchStmt.cases, "\n");
    format(ctx->state, "\n}\n", NULL);
}

static void dumpMatchStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    printKeyword(ctx->state, "switch");
    AddSpace();
    format(ctx->state, "(", NULL);
    astConstVisit(visitor, node->matchStmt.expr);
    format(ctx->state, ") {{\n", NULL);
    dumpManyAstNodes(visitor, node->matchStmt.cases, "\n");
    format(ctx->state, "\n}\n", NULL);
}

static void dumpModuleDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    printKeyword(ctx->state, "module");
    format(ctx->state, " {s}\n\n", (FormatArg[]){{.s = node->moduleDecl.name}});
}

static void dumpImportDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    printKeyword(ctx->state, "import");
    AddSpace();
    if (node->import.entities) {
        if (node->import.entities->next)
            dumpManyAstNodesEnclosed(
                visitor, node->import.entities, "{{", ", ", "}");
        else
            astConstVisit(visitor, node->import.entities);
        AddSpace();
        printKeyword(ctx->state, "from");
        AddSpace();
    }

    astConstVisit(visitor, node->import.module);

    if (node->import.alias) {
        AddSpace();
        printKeyword(ctx->state, "as");
        AddSpace();
        astConstVisit(visitor, node->import.alias);
    }
    AddNewLine();
}

static void dumpVarDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    if (hasFlag(node, Const)) {
        AddComptime();
        printKeyword(ctx->state, "const");
    }
    else
        printKeyword(ctx->state, "var");
    AddSpace();
    dumpManyAstNodes(visitor, node->varDecl.names, ", ");

    if (node->varDecl.type) {
        format(ctx->state, ": ", NULL);
        astConstVisit(visitor, node->varDecl.type);
    }

    if (node->varDecl.init) {
        format(ctx->state, " = ", NULL);
        astConstVisit(visitor, node->varDecl.init);
    }
}

static void dumpFuncDeclWithParams(ConstAstVisitor *visitor,
                                   const AstNode *node,
                                   const AstNode *params)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    ctx->currentFunction = node;
    if (hasFlag(node, Public)) {
        printKeyword(ctx->state, "pub");
        AddSpace();
    }

    if (hasFlag(node, Async)) {
        printKeyword(ctx->state, "async");
        AddSpace();
    }
    if (hasFlag(node, Extern)) {
        printKeyword(ctx->state, "extern");
        AddSpace();
    }
    if (hasFlag(node, Virtual)) {
        printKeyword(ctx->state, "virtual");
        AddSpace();
    }

    printKeyword(ctx->state, "func");
    AddSpace();
    dumpFunctionName(ctx, node);

    if (params)
        dumpManyAstNodesEnclosed(visitor, params, "[", ", ", "]");

    dumpManyAstNodesEnclosed(
        visitor, node->funcDecl.signature->params, "(", ", ", ")");

    if (node->funcDecl.signature->ret) {
        format(ctx->state, ": ", NULL);
        astConstVisit(visitor, node->funcDecl.signature->ret);
    }

    if (node->funcDecl.body) {
        if (nodeIs(node->funcDecl.body, BlockStmt))
            AddSpace();
        else
            format(ctx->state, " => ", NULL);

        astConstVisit(visitor, node->funcDecl.body);
    }

    if (node->next)
        AddNewLine();

    ctx->currentFunction = NULL;
}

static void dumpFuncDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    dumpFuncDeclWithParams(visitor, node, NULL);
}

static void dumpTypeDeclWithParams(ConstAstVisitor *visitor,
                                   const AstNode *node,
                                   const AstNode *params)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    if (hasFlag(node, Public)) {
        printKeyword(ctx->state, "pub");
        AddSpace();
    }

    if (hasFlag(node, Extern)) {
        printKeyword(ctx->state, "extern");
        AddSpace();
    }

    format(ctx->state, "{s}", (FormatArg[]){{.s = node->typeDecl.name}});
    if (params)
        dumpManyAstNodesEnclosed(visitor, params, "[", ", ", "]");

    if (node->typeDecl.aliased) {
        format(ctx->state, " = ", NULL);
        astConstVisit(visitor, node->typeDecl.aliased);
    }
    AddNewLine();
}

static void dumpTypeDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    dumpTypeDeclWithParams(visitor, node, NULL);
}

static void dumpStructDeclWithParams(ConstAstVisitor *visitor,
                                     const AstNode *node,
                                     const AstNode *params)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    if (hasFlag(node, Public)) {
        printKeyword(ctx->state, "pub");
        AddSpace();
    }

    if (hasFlag(node, Extern)) {
        printKeyword(ctx->state, "extern");
        AddSpace();
    }

    printKeyword(ctx->state, "struct");
    AddSpace();
    format(ctx->state, "{s}", (FormatArg[]){{.s = node->structDecl.name}});
    if (params)
        dumpManyAstNodesEnclosed(visitor, params, "[", ", ", "]");

    if (node->structDecl.members == NULL) {
        if (!hasFlag(node, Extern))
            format(ctx->state, "{{ }", NULL);
    }
    else {
        format(ctx->state, " {{{>}\n", NULL);
        dumpManyAstNodes(visitor, node->structDecl.members, "\n");
        format(ctx->state, "{<}\n}", NULL);
    }
    AddNewLine();
}

static void dumpStructDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    dumpStructDeclWithParams(visitor, node, NULL);
}

static void dumpGenericDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    const AstNode *decl = node->genericDecl.decl;
    if (nodeIs(decl, FuncDecl))
        dumpFuncDeclWithParams(visitor, decl, node->genericDecl.params);
    else if (nodeIs(decl, TypeDecl)) {
        dumpTypeDeclWithParams(visitor, decl, node->genericDecl.params);
    }
    else if (nodeIs(decl, StructDecl)) {
        dumpStructDeclWithParams(visitor, decl, node->genericDecl.params);
    }
    else if (nodeIs(decl, ClassDecl)) {
    }
}

static void dumpProgram(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state,
           "// Cxy dump - source: {s}\n\n",
           (FormatArg[]){{.s = node->loc.fileName ?: "<unknown>"}});

    if (node->program.module) {
        astConstVisit(visitor, node->program.module);
        AddNewLine();
    }

    if (node->program.top) {
        astConstVisitManyNodes(visitor, node->program.top);
        AddNewLine();
    }

    AstNode *decl = node->program.decls;
    for (; decl; decl = decl->next) {
        if (ctx->isSimplified && nodeIs(decl, GenericDecl))
            continue;
        astConstVisit(visitor, decl);
        AddNewLine();
    }

    AddNewLine();
}

static void dispatch(ConstVisitor func,
                     ConstAstVisitor *visitor,
                     const AstNode *node)
{
    if (node == NULL)
        return;

    if (node->attrs) {
        // dump attributes if any
        astConstVisitManyNodes(visitor, node->attrs);
    }

    func(visitor, node);
}

AstNode *dumpCxySource(CompilerDriver *driver, AstNode *node, FILE *file)
{
    FormatState state = newFormatState("  ", !isatty(fileno(file)));
    DumpContext context = {.state = &state,
                           .isSimplified =
                               nodeIs(node, Metadata) &&
                               (node->metadata.stages & BIT(ccsSimplify))};

    // clang-format off
    // sb.op__lshift7(this.b).op__lshift7(" -> ").op__lshift10(this.x)
    ConstAstVisitor visitor = makeConstAstVisitor(&context,
                                                  {
                                                      [astProgram] = dumpProgram,
                                                      [astPath] = dumpPath,
                                                      [astPathElem] =
                                                      dumpPathElement,
                                                      [astIdentifier] =
                                                      dumpIdentifier,
                                                      [astAttr] = dumpAttribute,
                                                      [astFuncParamDecl] =
                                                      dumpFuncParam,
                                                      [astGenericParam] =
                                                      dumpGenericParam,
                                                      [astImportEntity] =
                                                      dumpImportEntity,
                                                      [astBackendCall] =
                                                      dumpBackendCall,
                                                      [astTypeRef] = dumpTypeRef,
                                                      [astNullLit] = dumpNullLit,
                                                      [astBoolLit] = dumpBoolLit,
                                                      [astCharLit] = dumpCharLit,
                                                      [astIntegerLit] =
                                                      dumpIntegerLit,
                                                      [astFloatLit] = dumpFloatLit,
                                                      [astStringLit] =
                                                      dumpStringLit,
                                                      [astStringExpr] =
                                                      dumpStringExpr,
                                                      [astUnaryExpr] =
                                                      dumpUnaryExpr,
                                                      [astBinaryExpr] =
                                                      dumpBinaryExpr,
                                                      [astAssignExpr] =
                                                      dumpAssignExpr,
                                                      [astTernaryExpr] =
                                                      dumpTernaryExpr,
                                                      [astGroupExpr] =
                                                      dumpGroupExpr,
                                                      [astStmtExpr] = dumpStmtExpr,
                                                      [astCastExpr] = dumpCastExpr,
                                                      [astTypedExpr] =
                                                      dumpTypedExpr,
                                                      [astRangeExpr] =
                                                      dumpRangeExpr,
                                                      [astMemberExpr] =
                                                      dumpMemberExpr,
                                                      [astIndexExpr] =
                                                      dumpIndexExpr,
                                                      [astCallExpr] = dumpCallExpr,
                                                      [astTupleExpr] =
                                                      dumpTupleExpr,
                                                      [astArrayExpr] =
                                                      dumpArrayExpr,
                                                      [astFieldExpr] =
                                                      dumpFieldExpr,
                                                      [astStructExpr] =
                                                      dumpStructExpr,
                                                      [astMacroCallExpr] =
                                                      dumpMacroCallExpr,
                                                      [astUnionValueExpr] =
                                                      dumpUnionValueExpr,
                                                      [astAddressOf] =
                                                      dumpUnaryExpr,
                                                      [astPrimitiveType] =
                                                      dumpPrimitiveType,
                                                      [astStringType] =
                                                      dumpBuiltinType,
                                                      [astVoidType] =
                                                      dumpBuiltinType,
                                                      [astAutoType] =
                                                      dumpBuiltinType,
                                                      [astFuncType] = dumpFuncType,
                                                      [astOptionalType] =
                                                      dumpOptionalType,
                                                      [astArrayType] =
                                                      dumpArrayType,
                                                      [astTupleType] =
                                                      dumpTupleType,
                                                      [astPointerType] =
                                                      dumpPointerType,
                                                      [astUnionDecl] =
                                                      dumpUnionType,
                                                      [astBlockStmt] =
                                                      dumpBlockStmt,
                                                      [astIfStmt] = dumpIfStmt,
                                                      [astForStmt] = dumpForStmt,
                                                      [astWhileStmt] =
                                                      dumpWhileStmt,
                                                      [astExprStmt] = dumpExprStmt,
                                                      [astDeferStmt] =
                                                      dumpDeferStmt,
                                                      [astBreakStmt] =
                                                      dumpBreakStmt,
                                                      [astContinueStmt] =
                                                      dumpContinueStmt,
                                                      [astReturnStmt] =
                                                      dumpReturnStmt,
                                                      [astCaseStmt] = dumpCaseStmt,
                                                      [astSwitchStmt] =
                                                      dumpSwitchStmt,
                                                      [astMatchStmt] =
                                                      dumpMatchStmt,
                                                      [astVarDecl] = dumpVarDecl,
                                                      [astTypeDecl] = dumpTypeDecl,
                                                      [astFieldDecl] =
                                                      dumpStructField,
                                                      [astStructDecl] =
                                                      dumpStructDecl,
                                                      [astModuleDecl] =
                                                      dumpModuleDecl,
                                                      [astImportDecl] =
                                                      dumpImportDecl,
                                                      [astFuncDecl] = dumpFuncDecl,
                                                      [astGenericDecl] =
                                                      dumpGenericDecl,
                                                  });

    // clang-format on
    astConstVisit(&visitor,
                  nodeIs(node, Metadata) ? node->metadata.node : node);

    writeFormatState(&state, file);
    freeFormatState(&state);

    return node;
}
