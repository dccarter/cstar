/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-17
 */

#include "codegen.h"

#include "ttable.h"

#include "core/alloc.h"

#define CXY_ANONYMOUS_FUNC "cxy_anonymous_func"
#define CXY_ANONYMOUS_TUPLE "cxy_anonymous_tuple"
#define CXY_ANONYMOUS_STRUCT "cxy_anonymous_struct"
#define CXY_ANONYMOUS_ARRAY "cxy_anonymous_array"
#define CXY_ANONYMOUS_SLICE "cxy_anonymous_slice"
#define CXY_ANONYMOUS_ENUM "cxy_anonymous_enum"

#define CXY_EPILOGUE_SRC_FILE CXY_SOURCE_LANG_DIR "/native/epilogue.cxy.c"
#define CXY_PROLOGUE_SRC_FILE CXY_SOURCE_LANG_DIR "/native/prologue.cxy.c"

static void generateType(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    if (hasFlags(type, flgBuiltin))
        return;

    switch (type->tag) {
    case typArray:
        generateArrayDeclaration(context, type);
        break;
    case typFunc:
        generateFunctionTypedef(context, type);
        break;
    case typTuple:
        generateTupleDefinition(context, type);
        break;
    case typEnum:
        generateEnumDefinition(context, type);
        break;
    case typStruct:
        generateStructDefinition(context, type);
        break;
    default:
        return;
    }

    format(state, ";\n", NULL);
}

static void generateAllTypes(CodegenContext *ctx)
{
    u64 typesCount = getTypesCount(ctx->types);
    const Type **types = callocOrDie(1, sizeof(Type *) * typesCount);
    u64 sorted = sortedByInsertionOrder(ctx->types, types, typesCount);

    u64 empty = 0;
    for (u64 i = 0; i < sorted; i++) {
        if (types[i] && types[i]->tag == typStruct)
            generateStructTypedef(ctx, types[i]);
    }

    for (u64 i = 0; i < sorted; i++) {
        if (types[i])
            generateType(ctx, types[i]);
        else
            empty++;
    }

    free(types);
}

static void generateIdentifier(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "{s}", (FormatArg[]){{.s = node->ident.value}});
}

static void generateStatementExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "(", NULL);
    astConstVisit(visitor, node->stmtExpr.stmt);
    format(ctx->state, ")", NULL);
}

static void generateGroupExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "(", NULL);
    astConstVisit(visitor, node->groupExpr.expr);
    format(ctx->state, ")", NULL);
}

static void generateTypedExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "(", NULL);
    generateTypeUsage(ctx, node->type);
    format(ctx->state, ")", NULL);
    astConstVisit(visitor, node->typedExpr.expr);
}

static void generateCastExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "(", NULL);
    generateTypeUsage(ctx, node->castExpr.to->type);
    format(ctx->state, ")", NULL);
    astConstVisit(visitor, node->castExpr.expr);
}

static void generateExpressionStmt(ConstAstVisitor *visitor,
                                   const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->exprStmt.expr);
    format(ctx->state, ";", NULL);
}

static void generateBreakContinue(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (node->tag == astBreakStmt)
        format(ctx->state, "break;", NULL);
    else
        format(ctx->state, "continue;", NULL);
}

static void epilogue(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    size_t bytes = 0;
    format(ctx->state,
           "\n"
           "/* --------------------- Generated EPILOGUE --------------*/\n"
           "\n",
           NULL);

    generateManyAsts(visitor, "\n", node->program.decls);

    format(ctx->state,
           "\n"
           "/* --------------------- epilogue.cxy.c --------------*/\n"
           "\n",
           NULL);

    append(ctx->state, readFile(CXY_EPILOGUE_SRC_FILE, &bytes), bytes);
    format(ctx->state, "\n", NULL);
}

static void prologue(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);

    size_t bytes = 0;
    format(ctx->state,
           "/**\n"
           " * Generated from cxy compile\n"
           " */\n"
           "\n"
           "/* --------------------- epilogue.cxy.c --------------*/\n"
           "\n",
           NULL);

    format(ctx->state,
           "#ifndef cxy_alloc\n"
           "#define cxy_alloc cxy_default_alloc\n"
           "#define cxy_free  cxy_default_dealloc\n"
           "#endif\n",
           NULL);

    append(ctx->state, readFile(CXY_PROLOGUE_SRC_FILE, &bytes), bytes);

    format(ctx->state,
           "\n"
           "/* --------------------- Generated PROLOGUE --------------*/\n"
           "\n",
           NULL);

    generateAllTypes(ctx);
}

void generateFallback(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state,
           "/* <unsupported AST tag {u32}> */",
           (FormatArg[]){{.u32 = node->tag}});
}

void writeNamespace(CodegenContext *ctx, cstring sep)
{
    if (ctx->namespace) {
        format(ctx->state,
               "{s}{s}",
               (FormatArg[]){{.s = ctx->namespace}, {.s = sep ?: "__"}});
    }
}

void writeEnumPrefix(CodegenContext *ctx, const Type *type)
{
    FormatState *state = ctx->state;
    csAssert0(type->tag == typEnum);

    writeNamespace(ctx, NULL);
    if (type->name) {
        format(state, "{s}", (FormatArg[]){{.s = type->name}});
    }
    else {
        format(state,
               CXY_ANONYMOUS_ENUM "{u64}",
               (FormatArg[]){{.u64 = type->index}});
    }
}

void writeTypename(CodegenContext *ctx, const Type *type)
{
    FormatState *state = ctx->state;

    writeNamespace(ctx, NULL);

    if (type->name) {
        if (type->tag == typFunc)
            format(state, "{s}_t", (FormatArg[]){{.s = type->name}});
        else
            format(state, "{s}", (FormatArg[]){{.s = type->name}});
    }
    else {
        switch (type->tag) {
        case typFunc:
            format(state,
                   CXY_ANONYMOUS_FUNC "{u64}_t",
                   (FormatArg[]){{.u64 = type->index}});
            break;
        case typTuple:
            format(state,
                   CXY_ANONYMOUS_TUPLE "{u64}_t",
                   (FormatArg[]){{.u64 = type->index}});
            break;
        case typStruct:
            format(state,
                   CXY_ANONYMOUS_STRUCT "{u64}_t",
                   (FormatArg[]){{.u64 = type->index}});
            break;
        case typArray:
            if (isSliceType(type))
                format(state,
                       CXY_ANONYMOUS_SLICE "{u64}_t",
                       (FormatArg[]){{.u64 = type->index}});
            else
                format(state,
                       CXY_ANONYMOUS_ARRAY "{u64}_t",
                       (FormatArg[]){{.u64 = type->index}});
            break;
        case typEnum:
            format(state,
                   CXY_ANONYMOUS_ENUM "{u64}_t",
                   (FormatArg[]){{.u64 = type->index}});
            break;
        default:
            unreachable();
        }
    }
}

void generateTypeUsage(CodegenContext *ctx, const Type *type)
{
    FormatState *state = ctx->state;

    switch (type->tag) {
    case typVoid:
        format(state, "void", NULL);
        break;
    case typString:
        format(state, "string", NULL);
        break;

    case typPrimitive:
        format(state,
               "{s}",
               (FormatArg[]){{.s = getPrimitiveTypeName(type->primitive.id)}});
        break;
    case typPointer:
        if (type->flags & flgConst)
            format(state, "const ", NULL);
        generateTypeUsage(ctx, type->pointer.pointed);
        format(state, "*", NULL);
        break;
    case typEnum:
    case typOpaque:
    case typArray:
    case typTuple:
    case typStruct:
    case typFunc:
    case typThis:
        writeTypename(ctx, type);
        break;
    default:
        break;
    }
}

void generateManyAsts(ConstAstVisitor *visitor,
                      const char *sep,
                      const AstNode *nodes)
{
    CodegenContext *context = getConstAstVisitorContext(visitor);
    for (const AstNode *node = nodes; node; node = node->next) {
        astConstVisit(visitor, node);
        if (node->next) {
            format(context->state, sep, NULL);
        }
    }
}

void generateManyAstsWithDelim(ConstAstVisitor *visitor,
                               const char *open,
                               const char *sep,
                               const char *close,
                               const AstNode *nodes)
{
    CodegenContext *context = getConstAstVisitorContext(visitor);
    format(context->state, open, NULL);
    generateManyAsts(visitor, sep, nodes);
    format(context->state, close, NULL);
}

void generateAstWithDelim(ConstAstVisitor *visitor,
                          const char *open,
                          const char *close,
                          const AstNode *node)
{
    CodegenContext *context = getConstAstVisitorContext(visitor);
    format(context->state, open, NULL);
    astConstVisit(visitor, node);
    format(context->state, close, NULL);
}

void generateManyAstsWithinBlock(ConstAstVisitor *visitor,
                                 const char *sep,
                                 const AstNode *nodes,
                                 bool newLine)
{
    CodegenContext *context = getConstAstVisitorContext(visitor);
    if (!nodes)
        format(context->state, "{{}", NULL);
    else if (!newLine && !nodes->next)
        generateAstWithDelim(visitor, "{{ ", " }", nodes);
    else
        generateManyAstsWithDelim(visitor, "{{{>}\n", sep, "{<}\n}", nodes);
}

void generateCode(FormatState *state,
                  TypeTable *table,
                  StrPool *strPool,
                  const AstNode *prog)
{
    CodegenContext context = {
        .state = state, .types = table, .strPool = strPool};

    // clang-format off
    ConstAstVisitor visitor = makeConstAstVisitor(&context,
    {
        [astPathElem] = generatePathElement,
        [astPath] = generatePath,
        [astPrimitiveType] = generateTypeinfo,
        [astVoidType] = generateTypeinfo,
        [astArrayType] = generateTypeinfo,
        [astIdentifier] = generateIdentifier,
        [astNullLit] = generateLiteral,
        [astBoolLit] = generateLiteral,
        [astCharLit] = generateLiteral,
        [astIntegerLit] = generateLiteral,
        [astFloatLit] = generateLiteral,
        [astStringLit] = generateLiteral,
        [astAddressOf] = generateAddressOfExpr,
        [astStmtExpr] = generateStatementExpr,
        [astBinaryExpr] = generateBinaryExpr,
        [astUnaryExpr] = generateUnaryExpr,
        [astAssignExpr] = generateAssignExpr,
        [astTupleExpr] = generateTupleExpr,
        [astStructExpr] = generateStructExpr,
        [astArrayExpr] = generateArrayExpr,
        [astMemberExpr] = generateMemberExpr,
        [astCallExpr] = generateCallExpr,
        [astStringExpr] = generateStringExpr,
        [astGroupExpr] = generateGroupExpr,
        [astTypedExpr] = generateTypedExpr,
        [astCastExpr] = generateCastExpr,
        [astIndexExpr] = generateIndexExpr,
        [astTernaryExpr] = generateTernaryExpr,
        [astNewExpr] = generateNewExpr,
        [astBlockStmt] = generateBlock,
        [astExprStmt] = generateExpressionStmt,
        [astReturnStmt] = generateReturnStmt,
        [astBreakStmt] = generateBreakContinue,
        [astContinueStmt] = generateBreakContinue,
        [astIfStmt] = generateIfStmt,
        [astWhileStmt] = generateWhileStmt,
        [astForStmt] = generateForStmt,
        [astFuncParam] = generateFuncParam,
        [astFuncDecl] = generateFunctionDefinition,
        [astVarDecl] = generateVariableDecl,
        [astTypeDecl] = generateTypeDecl
    },

    .fallback = generateFallback);

    prologue(&visitor, prog);
    epilogue(&visitor, prog);
}
