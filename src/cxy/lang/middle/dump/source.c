//
// Created by Carter Mbotho on 2024-02-02.
//

#include "lang/frontend/ast.h"
#include "lang/frontend/flag.h"
#include "lang/frontend/visitor.h"

typedef struct DumpContext {
    FormatState *state;
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

static void dumpPathElement(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    if (node->pathElement.isKeyword)
        printKeyword(ctx->state, node->pathElement.name);
    else
        format(ctx->state, "{s}", (FormatArg[]){{.s = node->pathElement.name}});

    if (node->pathElement.args)
        dumpManyAstNodesEnclosed(
            visitor, node->pathElement.args, "[", ", ", "]");
}

static void dumpPath(ConstAstVisitor *visitor, const AstNode *node)
{
    dumpManyAstNodes(visitor, node->path.elements, ".");
}

static void dumpIdentifier(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
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

    format(ctx->state, "{s}: ", (FormatArg[]){{.s = node->funcParam.name}});
    if (node->funcParam.type)
        astConstVisit(visitor, node->funcParam.type);
    else if (node->type)
        format(ctx->state, "{t}", (FormatArg[]){{.t = node->type}});
    else
        printKeyword(ctx->state, "auto");

    if (node->funcParam.def) {
        format(ctx->state, " = ", NULL);
        astConstVisit(visitor, node->funcParam.def);
    }
}

static void dumpNullLit(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    printKeyword(ctx->state, NULL);
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
           "{$}{c}{$}",
           (FormatArg[]){{.style = keywordStyle},
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
    format(ctx->state,
           "{$}\"{s}\"{$}",
           (FormatArg[]){{.style = stringStyle},
                         {.s = node->stringLiteral.value},
                         {.style = resetStyle}});
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
    dumpManyAstNodesEnclosed(visitor, node->tupleType.elements, "(", ",", ")");
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

static void dumpBinaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->binaryExpr.lhs);
    format(ctx->state,
           " {s} ",
           (FormatArg[]){{.s = getBinaryOpString(node->binaryExpr.op)}});
    astConstVisit(visitor, node->binaryExpr.rhs);
}

static void dumpUnaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    if (node->unaryExpr.isPrefix) {
        format(ctx->state,
               " {s}",
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
    astConstVisit(visitor, node->callExpr.callee);
    dumpManyAstNodesEnclosed(visitor, node->callExpr.args, "(", ",", ")");
}

static void dumpMacroCallExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    astConstVisit(visitor, node->callExpr.callee);
    dumpManyAstNodesEnclosed(visitor, node->callExpr.args, "!(", ",", ")");
}

static void dumpBlockStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    if (node->blockStmt.stmts) {
        format(ctx->state, "{{{>}\n", NULL);
        dumpManyAstNodes(visitor, node->blockStmt.stmts, "\n");
        format(ctx->state, "{<}\n}", NULL);
    }
    else {
        format(ctx->state, "{{ }\n", NULL);
    }
}

static void dumpIfStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    AddComptime();
    printKeyword(ctx->state, "if");
    format(ctx->state, " (", NULL);
    astConstVisit(visitor, node->ifStmt.cond);
    format(ctx->state, ")\n", NULL);
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
        astConstVisit(visitor, node->ifStmt.body);
    }
    else if (nodeIs(node->ifStmt.otherwise, IfStmt)) {
        AddSpace();
        astConstVisit(visitor, node->ifStmt.otherwise);
    }
    else {
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
        if (hasFlag(node->caseStmt.variable, Reference))
            format(ctx->state, "&", NULL);
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
    astConstVisitManyNodes(visitor, node->switchStmt.cases);
    format(ctx->state, "}\n", NULL);
}

static void dumpMatchStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    DumpContext *ctx = getConstAstVisitorContext(visitor);
    printKeyword(ctx->state, "switch");
    AddSpace();
    format(ctx->state, "(", NULL);
    astConstVisit(visitor, node->matchStmt.expr);
    format(ctx->state, ") {{\n", NULL);
    astConstVisitManyNodes(visitor, node->matchStmt.cases);
    format(ctx->state, "}\n", NULL);
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
    printKeyword(ctx->state, "func");
    format(ctx->state, " {s}", (FormatArg[]){{.s = node->funcDecl.name}});
    if (params)
        dumpManyAstNodesEnclosed(visitor, params, "[", ", ", "]");

    dumpManyAstNodesEnclosed(
        visitor, node->funcDecl.signature->params, "(", ",", ")");

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
    AddNewLine();
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

static void dumpGenericDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    const AstNode *decl = node->genericDecl.decl;
    if (nodeIs(decl, FuncDecl))
        dumpFuncDeclWithParams(visitor, decl, node->genericDecl.params);
    else if (nodeIs(decl, TypeDecl)) {
        dumpTypeDeclWithParams(visitor, node, node->genericDecl.params);
    }
    else if (nodeIs(decl, StructDecl)) {
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

    astConstVisitManyNodes(visitor, node->program.decls);
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

void dumpCxySource(FormatState *output, AstNode *node)
{
    DumpContext context = {.state = output};

    // clang-format off
    ConstAstVisitor visitor = makeConstAstVisitor(&context, {
        [astProgram] = dumpProgram,
        [astPath] = dumpPath,
        [astPathElem] = dumpPathElement,
        [astIdentifier] = dumpIdentifier,
        [astAttr] = dumpAttribute,
        [astFuncParam] = dumpFuncParam,
        [astImportEntity] = dumpImportEntity,
        [astNullLit] = dumpNullLit,
        [astBoolLit] = dumpBoolLit,
        [astCharLit] = dumpCharLit,
        [astIntegerLit] = dumpIntegerLit,
        [astFloatLit] = dumpFloatLit,
        [astStringLit] = dumpStringLit,
        [astUnaryExpr] = dumpUnaryExpr,
        [astBinaryExpr] = dumpBinaryExpr,
        [astTernaryExpr] = dumpTernaryExpr,
        [astGroupExpr] = dumpGroupExpr,
        [astStmtExpr] = dumpStmtExpr,
        [astMemberExpr] = dumpMemberExpr,
        [astIndexExpr] = dumpIndexExpr,
        [astCallExpr] = dumpCallExpr,
        [astMacroCallExpr] = dumpMacroCallExpr,
        [astPrimitiveType] = dumpPrimitiveType,
        [astStringType] = dumpBuiltinType,
        [astVoidType] = dumpBuiltinType,
        [astAutoType] = dumpBuiltinType,
        [astFuncType] = dumpFuncType,
        [astOptionalType] = dumpOptionalType,
        [astArrayType] = dumpArrayType,
        [astTupleType] = dumpTupleType,
        [astPointerType] = dumpPointerType,
        [astUnionDecl] = dumpUnionType,
        [astBlockStmt] = dumpBlockStmt,
        [astIfStmt] = dumpIfStmt,
        [astForStmt] = dumpForStmt,
        [astWhileStmt] = dumpWhileStmt,
        [astExprStmt] = dumpExprStmt,
        [astDeferStmt] = dumpDeferStmt,
        [astBreakStmt] = dumpBreakStmt,
        [astContinueStmt] = dumpContinueStmt,
        [astReturnStmt] = dumpReturnStmt,
        [astCaseStmt] = dumpCaseStmt,
        [astSwitchStmt] = dumpSwitchStmt,
        [astMatchStmt] = dumpMatchStmt,
        [astVarDecl] = dumpVarDecl,
        [astTypeDecl] = dumpTypeDecl,
        [astModuleDecl] = dumpModuleDecl,
        [astImportDecl] = dumpImportDecl,
        [astFuncDecl] = dumpFuncDecl,
        [astGenericDecl] = dumpGenericDecl,
    });

    // clang-format on
    astConstVisit(&visitor, node);
}
