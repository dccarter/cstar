//
// Created by Carter on 2023-08-30.
//

#include "codegen.h"
#include <string.h>

#include "lang/flag.h"
#include "lang/strings.h"
#include "lang/ttable.h"
#include "lang/visitor.h"

#include "core/alloc.h"

#include "driver/driver.h"

#include "driver/cc.h"
#include "epilogue.h"
#include "prologue.h"

#define CXY_ANONYMOUS_FUNC "AnonymousFunc"
#define CXY_ANONYMOUS_TUPLE "AnonymousTuple"
#define CXY_ANONYMOUS_STRUCT "AnonymousStruct"
#define CXY_ANONYMOUS_ARRAY "AnonymousArray"
#define CXY_ANONYMOUS_SLICE "AnonymousSlice"
#define CXY_ANONYMOUS_ENUM "AnonymousEnum"
#define CXY_ANONYMOUS_UNION "AnonymousUnion"

static void generateType(CodegenContext *context, const Type *type)
{
    FormatState *state = context->state;
    if (hasFlags(type, flgBuiltin | flgCodeGenerated))
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
    case typUnion:
        generateUnionDefinition(context, type);
        break;
    case typEnum:
        generateEnumDefinition(context, type);
        break;
    case typStruct:
        generateStructDefinition(context, type);
        break;
    case typClass:
        generateClassDefinition(context, type);
        break;
    case typOpaque:
    case typAlias:
        generateTypeDeclDefinition(context, type);
        break;
    case typThis:
        if (typeIs(type->this.that, Alias))
            generateType(context, type->this.that);
        break;
    default:
        return;
    }

    format(state, "\n", NULL);
}

static void generateAllTypes(CodegenContext *ctx)
{
    u64 typesCount = getTypesCount(ctx->types);
    const Type **types = callocOrDie(1, sizeof(Type *) * typesCount);
    u64 sorted = sortedByInsertionOrder(ctx->types, types, typesCount);

    u64 empty = 0;
    for (u64 i = 0; i < sorted; i++) {
        if (types[i] == NULL || hasFlag(types[i], CodeGenerated))
            continue;

        if (typeIs(types[i], Struct))
            generateStructTypedef(ctx, types[i]);
        else if (typeIs(types[i], Class))
            generateClassTypedef(ctx, types[i]);
    }

    format(ctx->state, "\n", NULL);

    for (u64 i = 0; i < sorted; i++) {
        if (types[i] == NULL)
            continue;
        if (types[i]->name &&
            strcmp(types[i]->name, "destructorForward_32") == 0) {
            printf("it");
            continue;
        }

        if (!hasFlag(types[i], CodeGenerated)) {
            generateType(ctx, types[i]);
            ((Type *)types[i])->flags |= flgCodeGenerated;
        }
        else
            empty++;
    }

    free(types);
}

static void generateUnionCast(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const Type *to = node->castExpr.to->type;
    u32 idx = node->castExpr.idx;

    if (!hasFlag(node, UnsafeCast)) {
        format(ctx->state, "({{ ", NULL);
        generateTypeUsage(ctx, node->castExpr.expr->type);
        format(ctx->state, " tmp = ", NULL);
        astConstVisit(visitor, node->castExpr.expr);
        format(ctx->state, "; ", NULL);
        format(ctx->state,
               "CXY__builtins_assert2(tmp.tag == {u32}, \"{s}\", {u64}, "
               "{u64}, "
               "\"UnionCastPanic: cast from {t} to {t}, current idx = %d\", "
               "tmp.tag); (",
               (FormatArg[]){{.u32 = idx},
                             {.s = node->loc.fileName},
                             {.u64 = node->loc.begin.row},
                             {.u64 = node->loc.begin.col},
                             {.t = node->castExpr.expr->type},
                             {.t = to}});
        generateTypeUsage(ctx, to);
        format(
            ctx->state,
            "){s}tmp._{u32}; })",
            (FormatArg[]){{.s = typeIs(to, Pointer) ? "&" : ""}, {.u32 = idx}});
    }
    else {
        if (typeIs(to, Pointer))
            format(ctx->state, "&", NULL);
        astConstVisit(visitor, node->castExpr.expr);
        format(ctx->state, "._{u32}", (FormatArg[]){{.u32 = idx}});
    }
}

static void generateIdentifier(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    for (u16 i = 0; i < node->ident.super; i++) {
        format(ctx->state, "super.", NULL);
    }
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
    if (typeIs(node->typedExpr.expr->type, Union)) {
        generateUnionCast(visitor, node);
    }
    else {
        CodegenContext *ctx = getConstAstVisitorContext(visitor);
        format(ctx->state, "(", NULL);
        generateTypeUsage(ctx, node->type);
        format(ctx->state, ")", NULL);
        astConstVisit(visitor, node->typedExpr.expr);
    }
}

static void generateCastExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    if (typeIs(node->castExpr.expr->type, Union)) {
        generateUnionCast(visitor, node);
    }
    else {
        CodegenContext *ctx = getConstAstVisitorContext(visitor);
        format(ctx->state, "(", NULL);
        generateTypeUsage(ctx, node->castExpr.to->type);
        format(ctx->state, ")", NULL);
        astConstVisit(visitor, node->castExpr.expr);
    }
}

static void generateExpressionStmt(ConstAstVisitor *visitor,
                                   const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->exprStmt.expr);
    format(ctx->state, ";", NULL);
}

static void generateBlockEpilogueOnReturn(ConstAstVisitor *visitor,
                                          const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    AstNode *parent = node->parentScope;
    if (parent == NULL || nodeIs(parent, FuncDecl)) {
        return;
    }

    if (nodeIs(parent, BlockStmt)) {
        const AstNode *epilogue = parent->blockStmt.epilogue.first;
        for (; epilogue; epilogue = epilogue->next) {
            astConstVisit(visitor, epilogue);
            if (hasFlag(epilogue, Deferred) && !nodeIs(epilogue, BlockStmt))
                format(ctx->state, ";", NULL);
            format(ctx->state, "\n", NULL);
        }
    }
    generateBlockEpilogueOnReturn(visitor, parent);
}

void generateBlock(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *ret = NULL;
    const AstNode *epilogue = node->blockStmt.epilogue.first;

    format(ctx->state, "{{{>}\n", NULL);
    for (const AstNode *stmt = node->blockStmt.stmts; stmt; stmt = stmt->next) {
        if (nodeIs(stmt, ReturnStmt)) {
            ret = stmt;
            break;
        }

        if (hasFlag(stmt, CodeGenerated))
            continue;

        astConstVisit(visitor, stmt);
        if (nodeIs(stmt, CallExpr))
            format(ctx->state, ";", NULL);
        if (epilogue || stmt->next)
            format(ctx->state, "\n", NULL);
    }

    for (; epilogue; epilogue = epilogue->next) {
        astConstVisit(visitor, epilogue);
        if (hasFlag(epilogue, Deferred) && !nodeIs(epilogue, BlockStmt))
            format(ctx->state, ";", NULL);

        if (ret || epilogue->next)
            format(ctx->state, "\n", NULL);
    }

    if (ret) {
        generateBlockEpilogueOnReturn(visitor, node);
        astConstVisit(visitor, ret);
    }
    format(ctx->state, "{<}\n}", NULL);
}

void generateReturnStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "return", NULL);
    if (node->returnStmt.expr) {
        format(ctx->state, " ", NULL);
        astConstVisit(visitor, node->returnStmt.expr);
    }
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
    cstring module = node->import.module->stringLiteral.value;
    if (module[0] == '.' && module[1] == '/') {
        format(ctx->state, "#include \"{s}.c\"", (FormatArg[]){{.s = module}});
    }
    else {
        format(ctx->state, "#include <{s}.c>", (FormatArg[]){{.s = module}});
    }
}

static void generateCCode(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (node->cCode.kind == cInclude)
        format(ctx->state,
               "#include {s}\n",
               (FormatArg[]){{.s = node->cCode.what->stringLiteral.value}});
    else if (node->cCode.kind == cDefine)
        format(ctx->state,
               "#define {s}\n",
               (FormatArg[]){{.s = node->cCode.what->stringLiteral.value}});
    else {
        cstring cxySource = node->loc.fileName;
        AstNode *nativeSource = node->cCode.what;
        for (; nativeSource; nativeSource = nativeSource->next) {
            addNativeSourceFile(ctx->nativeSources,
                                ctx->strPool,
                                cxySource,
                                nativeSource->stringLiteral.value);
        }
    }
}

void generateWhileStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *cond = node->whileStmt.cond;
    if (cond->tag == astVarDecl) {
        format(ctx->state, "{{{>}\n", NULL);
        astConstVisit(visitor, cond);
        format(ctx->state,
               "\nwhile ({s}) ",
               (FormatArg[]){{.s = cond->varDecl.names->ident.value}});
    }
    else {
        format(ctx->state, "while (", NULL);
        astConstVisit(visitor, cond);
        format(ctx->state, ") ", NULL);
    }
    astConstVisit(visitor, node->whileStmt.body);

    if (cond->tag == astVarDecl) {
        format(ctx->state, "{<}\n}", NULL);
    }
}
void generateDestructorRef(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    writeTypename(ctx, node->destructorRef.target);
    format(ctx->state, "__builtin_destructor", NULL);
}

void generateTypeRef(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    writeTypename(ctx, node->type);
}

static void epilogue(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    generateManyAsts(visitor, "", node->program.decls);
    format(ctx->state,
           "\n"
           "\n",
           NULL);

    if (!ctx->importedFile && !hasFlag(node, BuiltinsModule)) {
        append(ctx->state, CXY_EPILOGUE_SOURCE, CXY_EPILOGUE_SOURCE_SIZE);
    }

    format(ctx->state, "\n", NULL);
}

static void prologue(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (!ctx->importedFile) {
        format(ctx->state,
               "/**\n"
               " * Generated by cxy compiler from `{s}`\n"
               " */\n"
               "\n",
               (FormatArg[]){{.s = node->loc.fileName ?: "<unknown>"}});
        format(ctx->state, "\n", NULL);
        if (!hasFlag(node, BuiltinsModule)) {
            format(ctx->state, "#include <runtime/prologue.h>\n", NULL);
            format(ctx->state, "#include \"__builtins.cxy.c\"\n", NULL);
            format(ctx->state, "\n", NULL);
        }
    }
    else
        format(ctx->state, "#pragma once\n\n", NULL);

    if (node->program.top) {
        generateManyAsts(visitor, "\n", node->program.top);
        format(ctx->state, "\n", NULL);
    }

    format(ctx->state, "\n", NULL);
    generateAllTypes(ctx);
}

void generateFallback(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    switch (node->tag) {
    case astGenericDecl:
    case astDefine:
    case astNop:
        break;
    default:
        format(ctx->state,
               "/* <unsupported AST tag {s}> */",
               (FormatArg[]){{.s = getAstNodeName(node)}});
    }
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

void writeEnumWithoutNamespace(CodegenContext *ctx, const Type *type)
{
    FormatState *state = ctx->state;
    csAssert0(type->tag == typEnum);

    if (type->namespace) {
        format(state, "{s}__", (FormatArg[]){{.s = type->namespace}});
    }

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

    if (!isBuiltinType(type)) {
        if (typeIs(type, Module)) {
            writeDeclNamespace(ctx, type->namespace, "");
            return;
        }
        writeDeclNamespace(ctx, type->namespace, NULL);
    }

    if (type->name) {
        if (type->tag == typFunc) {
            u32 index = type->func.decl ? type->func.decl->funcDecl.index : 0;
            if (index)
                format(state,
                       "{s}_{u32}_t",
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
                   CXY_ANONYMOUS_FUNC "_{u64}_t",
                   (FormatArg[]){{.u64 = type->index}});
            break;
        case typTuple:
            format(state,
                   CXY_ANONYMOUS_TUPLE "_{u64}_t",
                   (FormatArg[]){{.u64 = type->index}});
            break;
        case typUnion:
            format(state,
                   CXY_ANONYMOUS_UNION "_{u64}_t",
                   (FormatArg[]){{.u64 = type->index}});
            break;
        case typStruct:
            format(state,
                   CXY_ANONYMOUS_STRUCT "_{u64}_t",
                   (FormatArg[]){{.u64 = type->index}});
            break;
        case typArray:
            if (isSliceType(type))
                format(state,
                       CXY_ANONYMOUS_SLICE "_{u64}_t",
                       (FormatArg[]){{.u64 = type->index}});
            else
                format(state,
                       CXY_ANONYMOUS_ARRAY "{u64}_t",
                       (FormatArg[]){{.u64 = type->index}});
            break;
        case typEnum:
            format(state,
                   CXY_ANONYMOUS_ENUM "_{u64}_t",
                   (FormatArg[]){{.u64 = type->index}});
            break;
        case typPointer:
            writeTypename(ctx, type->pointer.pointed);
            break;
        case typInfo:
            writeTypename(ctx, type->info.target);
            break;
        case typString:
            format(state, "string", NULL);
            break;
        case typWrapped: {
            u64 flags = flgNone;
            type = unwrapType(type, &flags);
            if (flags & flgConst)
                format(ctx->state, "const ", NULL);
            writeTypename(ctx, type);
        } break;
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
    case typAuto:
        format(state, "void *", NULL);
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
        if (hasFlag(type, Const))
            format(state, "const ", NULL);
        generateTypeUsage(ctx, getPointedType(type));
        format(state, "*", NULL);
        break;
    case typWrapped: {
        u64 flags = flgNone;
        type = unwrapType(type, &flags);
        if (flags & flgConst)
            format(state, "const ", NULL);
        generateTypeUsage(ctx, type);
        break;
    }

    case typEnum:
    case typArray:
    case typTuple:
    case typStruct:
    case typFunc:
    case typUnion:
        writeTypename(ctx, type);
        break;
    case typThis:
        if (typeIs(type->this.that, Alias)) {
            writeTypename(ctx, type->this.that);
            format(ctx->state, "*", NULL);
        }
        else {
            generateTypeUsage(ctx, type->this.that);
        }
        break;
    case typOpaque:
    case typClass:
        writeTypename(ctx, type);
        format(ctx->state, " *", NULL);
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
    const AstNode *alias = findAttribute(node, S_alias);

    if (alias == NULL)
        return NULL;

    const AstNode *name = findAttributeArgument(alias, S_name);

    return (nodeIs(name, StringLit)) ? name->stringLiteral.value : NULL;
}

AstNode *generateCode(CompilerDriver *driver, AstNode *node)
{
    AstNode *program = node->metadata.node;
    CodegenContext context = {.state = node->metadata.state,
                              .types = driver->typeTable,
                              .strPool = &driver->strPool,
                              .nativeSources = &driver->nativeSources,
                              .program = program,
                              .importedFile = hasFlag(program, ImportedModule),
                              .namespace =
                                  hasFlag(program, ImportedModule)
                                      ? program->program.module->moduleDecl.name
                                      : NULL};

    // clang-format off
    ConstAstVisitor visitor = makeConstAstVisitor(&context,
    {
        [astCCode] = generateCCode,
        [astImportDecl] = generateImportDecl,
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
        [astUnionValue] = generateUnionValueExpr,
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
        [astMatchStmt] = generateMatchStmt,
        [astCaseStmt] = generateCaseStmt,
        [astFuncParam] = generateFuncParam,
        [astFuncDecl] = generateFunctionDefinition,
        [astVarDecl] = generateVariableDecl,
        [astTypeDecl] = generateTypeDecl,
        [astStructDecl] = generateStructDecl,
        [astClassDecl] = generateClassDecl,
        [astDestructorRef] = generateDestructorRef,
        [astTypeRef] = generateTypeRef,
    }, .fallback = generateFallback);

    // clang-format on

    if (program->program.module)
        context.namespace = program->program.module->moduleDecl.name;

    prologue(&visitor, program);
    epilogue(&visitor, program);

    return node;
}
