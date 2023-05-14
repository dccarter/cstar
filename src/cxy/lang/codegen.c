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
#include "main.inc.h"
#include "setup.inc.h"
#include "tgc.inc.h"

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
        if (hasFlags(type, flgNative))
            return;
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
    case typOpaque:
        if (type->namespace == NULL)
            break;
        format(state, "#ifndef ", NULL);
        writeTypename(context, type);
        format(state, "\n#define ", NULL);
        writeTypename(context, type);
        format(state, " {s} *\n#endif\n", (FormatArg[]){{.s = type->name}});

        return;
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
        if (types[i] == NULL)
            continue;

        if (typeIs(types[i], Struct)) {
            if (!hasFlag(types[i], CodeGenerated))
                generateStructTypedef(ctx, types[i]);
        }
    }

    for (u64 i = 0; i < sorted; i++) {
        if (types[i] == NULL)
            continue;

        if (!hasFlag(types[i], CodeGenerated)) {
            generateType(ctx, types[i]);
            ((Type *)types[i])->flags |= flgCodeGenerated;
        }
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

static void generateImportDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state,
           "#include <{s}.c>",
           (FormatArg[]){{.s = node->import.module->stringLiteral.value}});
}

static void epilogue(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    generateManyAsts(visitor, "\n", node->program.decls);
    format(ctx->state,
           "\n"
           "\n",
           NULL);

    if (!ctx->importedFile) {
        append(ctx->state, CXY_MAIN_CODE, CXY_MAIN_CODE_SIZE);
    }

    format(ctx->state, "\n", NULL);
}

static void prologue(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (!ctx->importedFile) {
        format(ctx->state,
               "/**\n"
               " * Generated from cxy compile\n"
               " */\n"
               "\n",
               NULL);
        format(ctx->state, "#define CXY_GC_ENABLED\n", NULL);
        append(ctx->state, CXY_TGC_CODE, CXY_TGC_CODE_SIZE);
        format(ctx->state, "\n\n", NULL);
        append(ctx->state, CXY_SETUP_CODE, CXY_SETUP_CODE_SIZE);
        format(ctx->state, "\n\n", NULL);
    }
    else
        format(ctx->state, "#pragma once\n\n", NULL);

    generateManyAsts(visitor, "\n", node->program.top);

    format(ctx->state, "\n", NULL);

    generateAllTypes(ctx);
}

void generateFallback(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state,
           "/* <unsupported AST tag {u32}> */",
           (FormatArg[]){{.u32 = node->tag}});
}

void generateDestructor(CodegenContext *context, const Type *type)
{
    format(context->state, "\nvoid ", NULL);
    writeTypename(context, type);
    format(context->state, "__op_delete_fwd(void *obj) {{ ", NULL);
    writeTypename(context, type);
    format(context->state, "__op_delete((", NULL);
    writeTypename(context, type);
    format(context->state, "*)obj); }", NULL);
}

void generateDestructorRef(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    writeTypename(ctx, node->destructorRef.target);
    format(ctx->state, "__op_delete_fwd", NULL);
}
void writeNamespace(CodegenContext *ctx, cstring sep)
{
    if (ctx->namespace) {
        format(ctx->state,
               "{s}{s}",
               (FormatArg[]){{.s = ctx->namespace}, {.s = sep ?: "__"}});
    }
}

void writeDeclNamespace(CodegenContext *ctx, cstring namespace, cstring sep)
{
    if (namespace) {
        format(ctx->state,
               "{s}{s}",
               (FormatArg[]){{.s = namespace}, {.s = sep ?: "__"}});
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

    if (!isBuiltinType(type))
        writeDeclNamespace(ctx, type->namespace, NULL);

    if (type->name) {
        if (type->tag == typFunc) {
            u32 index = type->func.decl ? type->func.decl->funcDecl.index : 0;
            if (index)
                format(state,
                       "{s}{u32}_t",
                       (FormatArg[]){{.s = type->name}, {.u32 = index}});
            else
                format(state, "{s}_t", (FormatArg[]){{.s = type->name}});
        }
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
    case typAuto:
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
    case typWrapped:
        if (type->flags & flgConst)
            format(state, "const ", NULL);
        generateTypeUsage(ctx, type->wrapped.target);
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

cstring getNativeDeclarationAliasName(const AstNode *node)
{
    if (!hasFlag(node, Native))
        return NULL;
    const AstNode *alias = findAttribute(node, "alias");

    if (alias == NULL)
        return NULL;

    const AstNode *name = findAttributeArgument(alias, "name");

    return (name && nodeIs(name, StringLit)) ? name->stringLiteral.value : NULL;
}

void generateCode(FormatState *state,
                  TypeTable *table,
                  StrPool *strPool,
                  const AstNode *prog,
                  bool isImported)
{
    CodegenContext context = {.state = state,
                              .types = table,
                              .strPool = strPool,
                              .importedFile = isImported};

    // clang-format off
    ConstAstVisitor visitor = makeConstAstVisitor(&context,
    {
        [astCCode] = generateCCode,
        [astImportDecl] = generateImportDecl,
        [astPathElem] = generatePathElement,
        [astPath] = generatePath,
        [astDestructorRef] = generateDestructorRef,
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
        [astSwitchStmt] = generateSwitchStmt,
        [astCaseStmt] = generateCaseStmt,
        [astFuncParam] = generateFuncParam,
        [astFuncDecl] = generateFunctionDefinition,
        [astVarDecl] = generateVariableDecl,
        [astTypeDecl] = generateTypeDecl
    },

    .fallback = generateFallback);

    if (prog->program.module)
        context.namespace = prog->program.module->moduleDecl.name;

    prologue(&visitor, prog);
    epilogue(&visitor, prog);
}
