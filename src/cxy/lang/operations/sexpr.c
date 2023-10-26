//
// Created by Carter Mbotho on 2023-10-24.
//
#include "lang/operator.h"

#include "lang/flag.h"
#include "lang/operations.h"
#include "lang/visitor.h"

#include "lang/ast.h"

#include <inttypes.h>

typedef struct {
    FormatState state;
    Log *L;

    struct {
        bool withLocation;
        bool withoutAttrs;
        bool withNamedEnums;
    } config;
} SExprDumpContext;

#define Return(CTX, NODE) (CTX)->emitter = (NODE)

static int checkEmitStatus(SExprDumpContext *ctx,
                           const AstNode *node,
                           int status)
{
    if (status)
        return status;

    switch (ctx->emitter->error) {
    case YAML_MEMORY_ERROR:
        logError(ctx->L,
                 node ? &node->loc : builtinLoc(),
                 "YAML dump error: out of memory",
                 NULL);
        break;

    case YAML_WRITER_ERROR:
    case YAML_EMITTER_ERROR:
        logError(ctx->L,
                 node ? &node->loc : builtinLoc(),
                 "YAML dump error: {s}",
                 (FormatArg[]){{.s = ctx->emitter->problem}});
        break;

    default:
        logError(ctx->L,
                 node ? &node->loc : builtinLoc(),
                 "YAML dump error: unknown",
                 NULL);
        break;
    }
    return status;
}

#define EMIT_VALUE(ctx, node, event)                                           \
    checkEmitStatus((ctx), (node), (yaml_emitter_emit((ctx)->emitter, event)))

static void emitIdentifier(SExprDumpContext *ctx,
                           const AstNode *value,
                           cstring str)
{
    format(&ctx->state, "\"{s}\"", (FormatArg[]){{.s = str}});
}

static inline void emitBool(SExprDumpContext *ctx,
                            const AstNode *node,
                            bool value)
{
    format(&ctx->state, "{b}", (FormatArg[]){{.b = value}});
}

static inline void emitNull(SExprDumpContext *ctx, const AstNode *node)
{
    printKeyword(&ctx->state, "null");
}

static inline void emitEmpty(SExprDumpContext *ctx, const AstNode *node)
{
    format(&ctx->state, "()", NULL);
}

#define GET_INTEGER(VALUE)                                                     \
    ((VALUE)->intLiteral.hasMinus ? -(VALUE)->intLiteral.value                 \
                                  : (VALUE)->intLiteral.value)

static inline void emitInteger(SExprDumpContext *ctx,
                               const AstNode *node,
                               i64 value)
{
    format(&ctx->state, "{i64}", (FormatArg[]){{.i64 = value)}});
}

static inline void emitUInteger(SExprDumpContext *ctx,
                                const AstNode *node,
                                u64 value)
{
    format(&ctx->state, "{u64}", (FormatArg[]){{.u64 = value}});
}

static inline void emitCharacter(SExprDumpContext *ctx,
                                 const AstNode *node,
                                 u32 value)
{
    format(&ctx->state, "'{cE}'", (FormatArg[]){{.c = value}});
}

static inline void emitFloat(SExprDumpContext *ctx,
                             const AstNode *node,
                             f64 value)
{
    format(&ctx->state, "{f64}", (FormatArg[]){{.i64 = value}});
}

static inline void emitStringLiteral(SExprDumpContext *ctx,
                                     const AstNode *value,
                                     cstring lit)
{
    format(&ctx->state, "\"{s}\"", (FormatArg[]){{.s = lit}});
}

static void emitStartMap(SExprDumpContext *ctx, const AstNode *node)
{
    yaml_event_t event;

    yaml_mapping_start_event_initialize(
        &event, NULL, NULL, 1, YAML_BLOCK_MAPPING_STYLE);

    EMIT_VALUE(ctx, node, &event);
}

static void emitEndMap(SExprDumpContext *ctx, const AstNode *value)
{
    yaml_event_t event;

    yaml_mapping_end_event_initialize(&event);
    EMIT_VALUE(ctx, value, &event);
}

static void emitStartArray(SExprDumpContext *ctx, const AstNode *value)
{
    yaml_event_t event;

    yaml_sequence_start_event_initialize(
        &event, NULL, NULL, 0, YAML_ANY_SEQUENCE_STYLE);

    EMIT_VALUE(ctx, value, &event);
}

static void emitEndArray(SExprDumpContext *ctx, const AstNode *value)
{
    yaml_event_t event;

    yaml_sequence_end_event_initialize(&event);
    EMIT_VALUE(ctx, value, &event);
}

static void nodeToYaml(ConstAstVisitor *visitor,
                       cstring name,
                       const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    if (node == NULL) {
        return;
    }

    format(&ctx->state, "({s} ", (FormatArg[]){{.s = getAstNodeName(node)}});
    astConstVisit(visitor, node);
    format(&ctx->state, ")", NULL);
}

static void manyNodesToYaml(ConstAstVisitor *visitor,
                            cstring name,
                            const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    if (node == NULL)
        return;
    emitIdentifier(ctx, node, name);
    emitStartArray(ctx, node);
    for (const AstNode *it = node; it; it = it->next) {
        nodeToYaml(visitor, NULL, it);
    }
    emitEndArray(ctx, node);
}

static void emitNodeFilePosition(SExprDumpContext *ctx,
                                 const AstNode *node,
                                 const FilePos *pos)
{
    format(&ctx->state,
           "{u32}:{u32}/{u64}",
           (FormatArg[]){
               {.u32 = pos->row}, {.u32 = pos->col}, {.u64 = pos->byteOffset}});
}

static void emitNodeFileLocation(SExprDumpContext *ctx, const AstNode *node)
{
    const FileLoc *loc = &node->loc;
    format(&ctx->state, " ", NULL);
    emitNodeFilePosition(ctx, node, &loc->begin);
    format(&ctx->state, " - ", NULL);
    emitNodeFilePosition(ctx, node, &loc->end);
}

static void nodeAddHeader(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    if (node->flags) {
        format(&ctx->state, " ", NULL);
        if (ctx->config.withNamedEnums)
            appendFlagsAsString(&ctx->state, node->flags);
        else
            emitUInteger(ctx, node, node->flags);
    }

    if (ctx->config.withLocation)
        emitNodeFileLocation(ctx, node);

    if (!ctx->config.withoutAttrs) {
        format(&ctx->state, " @[", NULL);
        manyNodesToYaml(visitor, ", ", node->attrs);
        format(&ctx->state, "]", NULL);
    }
}

static void visitProgram(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);
    format(&ctx->state, "{>}\n", NULL);
    nodeToYaml(visitor, NULL, node->program.module);
    manyNodesToYaml(visitor, "\n", node->program.top);
    manyNodesToYaml(visitor, "\n", node->program.decls);
    format(&ctx->state, "{<}", NULL);
}

static void visitError(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);
    format(&ctx->state, " ", NULL);
    emitStringLiteral(ctx, node, node->error.message);
}

static void visitNoop(ConstAstVisitor *visitor, const AstNode *node)
{
    nodeAddHeader(visitor, node);
}

static void visitLiteral(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);
    emitIdentifier(ctx, node, " ");
    switch (node->tag) {
    case astNullLit:
        emitNull(ctx, node);
        break;
    case astBoolLit:
        emitBool(ctx, node, node->boolLiteral.value);
        break;
    case astCharLit:
        emitCharacter(ctx, node, node->charLiteral.value);
        break;
    case astIntegerLit:
        emitInteger(ctx, node, GET_INTEGER(node));
        break;
    case astFloatLit:
        emitFloat(ctx, node, node->floatLiteral.value);
        break;
    case astStringLit:
        emitStringLiteral(ctx, node, node->stringLiteral.value);
        break;
    default:
        csAssert0(false);
    }
}

#define EMIT_SPACE() format(&ctx->state, " ", NULL)

static void visitAttr(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    emitIdentifier(ctx, node, node->attr.name);
    // DO emit arguments
    // manyNodesToYaml(visitor, "args", node->attr.args);
}

static void visitStrExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);
    manyNodesToYaml(visitor, ", ", node->stringExpr.parts);
}

static void visitDefine(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);
    format(&ctx->state, " (");
    manyNodesToYaml(visitor, ", ", node->define.names);

    nodeToYaml(visitor, "defineType", node->define.type);

    nodeToYaml(visitor, "args", node->define.container);
}

static void visitImport(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "alias", node->import.alias);

    nodeToYaml(visitor, "module", node->import.module);
    manyNodesToYaml(visitor, "entities", node->import.entities);
}

static void visitImportEntity(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    emitIdentifier(ctx, node, "name");
    emitIdentifier(ctx, node, node->importEntity.name);
    emitIdentifier(ctx, node, "alias");
    emitIdentifier(ctx, node, node->importEntity.alias);
}

static void visitModuleDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    if (node->moduleDecl.name) {
        emitIdentifier(ctx, node, "name");
        emitIdentifier(ctx, node, node->moduleDecl.name);
    }
}

static void visitIdentifier(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);
    if (node->ident.alias) {
        emitIdentifier(ctx, node, "alias");
        emitIdentifier(ctx, node, node->ident.alias);
    }

    emitIdentifier(ctx, node, "value");
    emitIdentifier(ctx, node, node->ident.value);
}

static void visitTuple(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    manyNodesToYaml(visitor, "members", node->tupleType.elements);
}

static void visitArrayType(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "element", node->arrayType.elementType);

    nodeToYaml(visitor, "dim", node->arrayType.dim);
}

static void visitFuncType(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "ret", node->funcType.ret);
    manyNodesToYaml(visitor, "params", node->funcType.params);
}

static void visitOptionalType(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "optType", node->optionalType.type);
}

static void visitPrimitiveType(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    emitIdentifier(ctx, node, "name");
    emitIdentifier(ctx, node, getPrimitiveTypeName(node->primitiveType.id));
}

static void visitHeaderOnly(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);
}

static void visitPointerType(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "pointed", node->pointerType.pointed);
}

static void visitArrayExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    manyNodesToYaml(visitor, "elements", node->arrayExpr.elements);
}

static void visitMemberExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "target", node->memberExpr.target);

    nodeToYaml(visitor, "member", node->memberExpr.member);
}

static void visitRangeExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "start", node->rangeExpr.start);

    nodeToYaml(visitor, "end", node->rangeExpr.end);

    nodeToYaml(visitor, "step", node->rangeExpr.step);
}

static void visitNewExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "newType", node->newExpr.type);

    nodeToYaml(visitor, "init", node->newExpr.init);
}

static void visitCastExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "to", node->castExpr.to);
    nodeToYaml(visitor, "exp", node->castExpr.expr);
}

static void visitIndexExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "target", node->indexExpr.target);
    if (node->indexExpr.index)
        nodeToYaml(visitor, "index", node->indexExpr.index);
}

static void visitGenericParam(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    emitIdentifier(ctx, node, "name");
    emitIdentifier(ctx, node, node->genericParam.name);
    manyNodesToYaml(visitor, "constraints", node->genericParam.constraints);
}

static void visitGenericDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "decl", node->genericDecl.decl);
    manyNodesToYaml(visitor, "params", node->genericDecl.params);
}

static void visitPathElement(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    manyNodesToYaml(visitor, "args", node->pathElement.args);

    emitIdentifier(ctx, node, "name");
    emitIdentifier(ctx, node, node->pathElement.name);

    if (node->pathElement.alt) {
        emitIdentifier(ctx, node, "alt");
        emitIdentifier(ctx, node, node->pathElement.alt);
    }
}

static void visitPath(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    manyNodesToYaml(visitor, "elements", node->path.elements);
    emitIdentifier(ctx, node, "isType");
    emitBool(ctx, node, node->path.isType);
}

static void visitFuncDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    if (node->funcDecl.operatorOverload != opInvalid) {
        emitIdentifier(ctx, node, "overload");
        emitIdentifier(
            ctx, node, getOpOverloadName(node->funcDecl.operatorOverload));
    }

    if (node->funcDecl.index != 0) {
        emitIdentifier(ctx, node, "index");
        emitUInteger(ctx, node, node->funcDecl.index);
    }

    emitIdentifier(ctx, node, "name");
    emitIdentifier(ctx, node, node->funcDecl.name);
    manyNodesToYaml(visitor, "params", node->funcDecl.signature->params);

    nodeToYaml(visitor, "ret", node->funcDecl.signature->ret);
    nodeToYaml(visitor, "body", node->funcDecl.body);
}

static void visitMacroDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    emitIdentifier(ctx, node, "name");
    emitIdentifier(ctx, node, node->macroDecl.name);
    manyNodesToYaml(visitor, "params", node->macroDecl.params);

    nodeToYaml(visitor, "ret", node->macroDecl.ret);

    nodeToYaml(visitor, "body", node->macroDecl.body);
}

static void visitFuncParam(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    emitIdentifier(ctx, node, "name");
    emitIdentifier(ctx, node, node->funcParam.name);
    emitIdentifier(ctx, node, "index");
    emitUInteger(ctx, node, node->funcParam.index);

    nodeToYaml(visitor, "paramType", node->funcParam.type);

    nodeToYaml(visitor, "default", node->funcParam.def);
}

static void visitVarDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    manyNodesToYaml(visitor, "names", node->varDecl.names);

    nodeToYaml(visitor, "varType", node->varDecl.type);

    nodeToYaml(visitor, "init", node->varDecl.init);
}

static void visitTypeDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    emitIdentifier(ctx, node, "name");
    emitIdentifier(ctx, node, node->typeDecl.name);

    nodeToYaml(visitor, "aliased", node->typeDecl.aliased);
}

static void visitUnionDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    emitIdentifier(ctx, node, "name");
    emitIdentifier(ctx, node, node->unionDecl.name);
    manyNodesToYaml(visitor, "members", node->unionDecl.members);
}

static void visitEnumOption(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    emitIdentifier(ctx, node, "name");
    emitIdentifier(ctx, node, node->enumOption.name);

    nodeToYaml(visitor, "value", node->enumOption.value);
    emitIdentifier(ctx, node, "index");
    emitUInteger(ctx, node, node->enumOption.index);
}

static void visitEnumDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    emitIdentifier(ctx, node, "name");
    emitIdentifier(ctx, node, node->enumDecl.name);

    nodeToYaml(visitor, "base", node->enumDecl.base);
    manyNodesToYaml(visitor, "options", node->enumDecl.options);
}

static void visitStructField(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    emitIdentifier(ctx, node, "name");
    emitIdentifier(ctx, node, node->structField.name);

    nodeToYaml(visitor, "type", node->structField.type);

    nodeToYaml(visitor, "value", node->structField.value);
    emitIdentifier(ctx, node, "index");
    emitUInteger(ctx, node, node->structField.index);
}

static void visitClassOrStructDecl(ConstAstVisitor *visitor,
                                   const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    emitIdentifier(ctx, node, "name");
    emitIdentifier(ctx, node, node->structDecl.name);
    if (nodeIs(node, ClassDecl))
        nodeToYaml(visitor, "base", node->classDecl.base);
    manyNodesToYaml(visitor, "implements", node->structDecl.implements);
    manyNodesToYaml(visitor, "members", node->structDecl.members);
}

static void visitInterfaceDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    emitIdentifier(ctx, node, "name");
    emitIdentifier(ctx, node, node->interfaceDecl.name);
    manyNodesToYaml(visitor, "members", node->interfaceDecl.members);
}

static void visitBinaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    if (!ctx->config.withNamedEnums) {
        emitIdentifier(ctx, node, "op");
        emitUInteger(ctx, node, node->binaryExpr.op);
    }
    else {
        emitIdentifier(ctx, node, "op");
        emitIdentifier(ctx,
                       node,
                       (nodeIs(node, BinaryExpr)
                            ? getBinaryOpString(node->binaryExpr.op)
                            : getAssignOpString(node->assignExpr.op)));
    }

    nodeToYaml(visitor, "lhs", node->binaryExpr.lhs);

    nodeToYaml(visitor, "rhs", node->binaryExpr.rhs);
}

static void visitUnaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    if (ctx->config.withNamedEnums) {
        emitIdentifier(ctx, node, "op");
        emitUInteger(ctx, node, node->unaryExpr.op);
    }
    else {
        emitIdentifier(ctx, node, "op");
        emitIdentifier(ctx, node, getUnaryOpString(node->unaryExpr.op));
    }
    emitIdentifier(ctx, node, "isPrefix");
    emitBool(ctx, node, node->unaryExpr.isPrefix);

    nodeToYaml(visitor, "lhs", node->unaryExpr.operand);
}

static void visitTernaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "cond", node->ternaryExpr.cond);

    nodeToYaml(visitor, "body", node->ternaryExpr.body);

    nodeToYaml(visitor, "otherwise", node->ternaryExpr.otherwise);
}

static void visitStmtExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "stmt", node->stmtExpr.stmt);
}

static void visitTypedExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "teType", node->typedExpr.type);

    nodeToYaml(visitor, "expr", node->typedExpr.expr);
}

static void visitCallExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "callee", node->callExpr.callee);
    manyNodesToYaml(visitor, "args", node->callExpr.args);
    if (node->callExpr.overload != 0) {
        emitIdentifier(ctx, node, "overload");
        emitUInteger(ctx, node, node->callExpr.overload);
    }
}

static void visitClosureExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "ret", node->closureExpr.ret);
    manyNodesToYaml(visitor, "params", node->closureExpr.params);
    nodeToYaml(visitor, "body", node->closureExpr.body);
}

static void visitFieldExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    emitIdentifier(ctx, node, "name");
    emitIdentifier(ctx, node, node->fieldExpr.name);

    nodeToYaml(visitor, "value", node->fieldExpr.value);
}

static void visitStructExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "left", node->structExpr.left);
    manyNodesToYaml(visitor, "members", node->structExpr.fields);
}

static void visitExpressionStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "expr", node->exprStmt.expr);
}

static void visitContinueStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    nodeAddHeader(visitor, node);
}

static void visitReturnStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "expr", node->returnStmt.expr);
}

static void visitBlockStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    manyNodesToYaml(visitor, "stmts", node->blockStmt.stmts);
}

static void visitForStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "stmts", node->forStmt.var);

    nodeToYaml(visitor, "range", node->forStmt.range);

    nodeToYaml(visitor, "body", node->forStmt.body);
}

static void visitWhileStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "cond", node->whileStmt.cond);

    nodeToYaml(visitor, "body", node->whileStmt.body);
}

static void visitSwitchStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "cond", node->switchStmt.cond);
    manyNodesToYaml(visitor, "cases", node->switchStmt.cases);
}

static void visitCaseStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "match", node->caseStmt.match);
    manyNodesToYaml(visitor, "body", node->caseStmt.body);
}

static void visitFallback(ConstAstVisitor *visitor, const AstNode *node)
{
    SExprDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);
}

AstNode *dumpAstToYaml(CompilerDriver *driver, AstNode *node, FILE *file)
{
    yaml_event_t event;
    yaml_emitter_t emitter;
    SExprDumpContext ctx = {
        .L = driver->L,
        .emitter = &emitter,
        .config = {.withNamedEnums = driver->options.dev.withNamedEnums,
                   .withoutAttrs = driver->options.dev.withoutAttrs,
                   .withLocation = driver->options.dev.withLocation}};

    yaml_emitter_initialize(&emitter);
    yaml_emitter_set_output_file(&emitter, stdout);

    yaml_stream_start_event_initialize(&event, YAML_UTF8_ENCODING);
    if (!EMIT_VALUE(&ctx, node, &event)) {
        node = NULL;
        goto dumpAstToYamlError;
    }

    yaml_document_start_event_initialize(&event, NULL, NULL, NULL, 1);
    if (!EMIT_VALUE(&ctx, node, &event)) {
        node = NULL;
        goto dumpAstToYamlError;
    }

    csAssert0(nodeIs(node, Metadata));

    // clang-format off
    ConstAstVisitor visitor = makeConstAstVisitor(&ctx, {
        [astProgram] = visitProgram,
        [astError] = visitError,
        [astNop] = visitNoop,
        [astAttr] = visitAttr,
        [astNullLit] = visitLiteral,
        [astBoolLit] = visitLiteral,
        [astCharLit] = visitLiteral,
        [astIntegerLit] = visitLiteral,
        [astFloatLit] = visitLiteral,
        [astStringLit] = visitLiteral,
        [astStringExpr] = visitStrExpr,
        [astDefine] = visitDefine,
        [astImportDecl] = visitImport,
        [astImportEntity] = visitImportEntity,
        [astModuleDecl] = visitModuleDecl,
        [astIdentifier] = visitIdentifier,
        [astTupleType] = visitTuple,
        [astTupleExpr] = visitTuple,
        [astArrayType] = visitArrayType,
        [astFuncType] = visitFuncType,
        [astOptionalType] = visitOptionalType,
        [astStringType] = visitHeaderOnly,
        [astAutoType] = visitHeaderOnly,
        [astPrimitiveType] = visitPrimitiveType,
        [astPointerType] = visitPointerType,
        [astArrayExpr] = visitArrayExpr,
        [astMemberExpr] = visitMemberExpr,
        [astRangeExpr] = visitRangeExpr,
        [astNewExpr] = visitNewExpr,
        [astCastExpr] = visitCastExpr,
        [astIndexExpr] = visitIndexExpr,
        [astGenericParam] = visitGenericParam,
        [astGenericDecl] = visitGenericDecl,
        [astPathElem] = visitPathElement,
        [astPath] = visitPath,
        [astFuncDecl] = visitFuncDecl,
        [astMacroDecl] = visitMacroDecl,
        [astFuncParam] = visitFuncParam,
        [astVarDecl] = visitVarDecl,
        [astTypeDecl] = visitTypeDecl,
        [astUnionDecl] = visitUnionDecl,
        [astEnumOption] = visitEnumOption,
        [astEnumDecl] = visitEnumDecl,
        [astField] = visitStructField,
        [astStructDecl] = visitClassOrStructDecl,
        [astClassDecl] = visitClassOrStructDecl,
        [astInterfaceDecl] = visitInterfaceDecl,
        [astAssignExpr] = visitBinaryExpr,
        [astBinaryExpr] = visitBinaryExpr,
        [astUnaryExpr] = visitUnaryExpr,
        [astAddressOf] = visitUnaryExpr,
        [astTernaryExpr] = visitTernaryExpr,
        [astIfStmt] = visitTernaryExpr,
        [astStmtExpr] = visitStmtExpr,
        [astTypedExpr] = visitTypedExpr,
        [astCallExpr] = visitCallExpr,
        [astMacroCallExpr] = visitCallExpr,
        [astClosureExpr] = visitClosureExpr,
        [astFieldExpr] = visitFieldExpr,
        [astStructExpr] = visitStructExpr,
        [astExprStmt] = visitExpressionStmt,
        [astDeferStmt] = visitExpressionStmt,
        [astGroupExpr] = visitExpressionStmt,
        [astSpreadExpr] = visitExpressionStmt,
        [astBreakStmt] = visitContinueStmt,
        [astContinueStmt] = visitContinueStmt,
        [astReturnStmt] = visitReturnStmt,
        [astBlockStmt] = visitBlockStmt,
        [astForStmt] = visitForStmt,
        [astWhileStmt] = visitWhileStmt,
        [astSwitchStmt] = visitSwitchStmt,
        [astCaseStmt] = visitCaseStmt
    }, .fallback = visitFallback );

    // clang-format on
    nodeToYaml(&visitor, NULL, node->metadata.node);

    yaml_document_end_event_initialize(&event, 1);
    if (!EMIT_VALUE(&ctx, node, &event)) {
        node = NULL;
        goto dumpAstToYamlError;
    }

    yaml_stream_end_event_initialize(&event);
    if (!EMIT_VALUE(&ctx, node, &event)) {
        node = NULL;
        goto dumpAstToYamlError;
    }

dumpAstToYamlError:
    yaml_emitter_delete(&emitter);
    return node;
}
