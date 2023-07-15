/**
 * Copyright (c) 2023 suilteam, Carter
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 *
 * @author Mpho Mbotho
 * @date 2023-04-17
 */

#include "lang/operator.h"

#include <yaml.h>

#include "lang/flag.h"
#include "lang/operations.h"
#include "lang/visitor.h"

#include "lang/ast.h"

typedef struct {
    yaml_emitter_t *emitter;
    Log *L;

    struct {
        bool withLocation;
        bool withoutAttrs;
        bool withNamedEnums;
    } config;
} YamlDumpContext;

#define Return(CTX, NODE) (CTX)->emitter = (NODE)

static int checkEmitStatus(YamlDumpContext *ctx,
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

static void emitMapKey(YamlDumpContext *ctx, const AstNode *value, cstring str)
{
    yaml_event_t event;

    yaml_scalar_event_initialize(&event,
                                 NULL,
                                 NULL,
                                 (yaml_char_t *)str,
                                 (int)strlen(str),
                                 1,
                                 1,
                                 YAML_ANY_SCALAR_STYLE);

    EMIT_VALUE(ctx, value, &event);
}

static void emitBool(YamlDumpContext *ctx, const AstNode *node, bool value)
{
    yaml_event_t event;
    if (value) {
        yaml_scalar_event_initialize(&event,
                                     NULL,
                                     NULL,
                                     (yaml_char_t *)"true",
                                     4,
                                     1,
                                     1,
                                     YAML_ANY_SCALAR_STYLE);
    }
    else {
        yaml_scalar_event_initialize(&event,
                                     NULL,
                                     NULL,
                                     (yaml_char_t *)"false",
                                     5,
                                     1,
                                     1,
                                     YAML_ANY_SCALAR_STYLE);
    }

    EMIT_VALUE(ctx, node, &event);
}

static void emitNull(YamlDumpContext *ctx, const AstNode *node)
{
    yaml_event_t event;
    yaml_scalar_event_initialize(&event,
                                 NULL,
                                 NULL,
                                 (yaml_char_t *)"null",
                                 4,
                                 1,
                                 1,
                                 YAML_ANY_SCALAR_STYLE);

    EMIT_VALUE(ctx, node, &event);
}

static void emitEmpty(YamlDumpContext *ctx, const AstNode *node)
{
    yaml_event_t event;
    yaml_scalar_event_initialize(
        &event, NULL, NULL, (yaml_char_t *)"", 0, 1, 1, YAML_ANY_SCALAR_STYLE);

    EMIT_VALUE(ctx, node, &event);
}

#define GET_INTEGER(VALUE)                                                     \
    ((VALUE)->intLiteral.hasMinus ? -(VALUE)->intLiteral.value                 \
                                  : (VALUE)->intLiteral.value)

static void emitInteger(YamlDumpContext *ctx, const AstNode *node, i64 value)
{
    char str[64];
    yaml_event_t event;
    int num = snprintf(str, sizeof(str), "%lli", value);
    csAssert0(num >= 0);

    yaml_scalar_event_initialize(&event,
                                 NULL,
                                 NULL,
                                 (yaml_char_t *)str,
                                 num,
                                 1,
                                 1,
                                 YAML_ANY_SCALAR_STYLE);

    EMIT_VALUE(ctx, node, &event);
}

static void emitUInteger(YamlDumpContext *ctx, const AstNode *node, u64 value)
{
    char str[64];
    yaml_event_t event;
    int num = snprintf(str, sizeof(str), "%llu", value);
    csAssert0(num >= 0);

    yaml_scalar_event_initialize(&event,
                                 NULL,
                                 NULL,
                                 (yaml_char_t *)str,
                                 num,
                                 1,
                                 1,
                                 YAML_ANY_SCALAR_STYLE);

    EMIT_VALUE(ctx, node, &event);
}

static void emitCharacter(YamlDumpContext *ctx, const AstNode *node, u32 value)
{
    yaml_event_t event;
    FormatState state = newFormatState("", false);

    format(&state, "{c}", (FormatArg[]){{.c = value}});
    char *str = formatStateToString(&state);
    freeFormatState(&state);

    yaml_scalar_event_initialize(&event,
                                 NULL,
                                 NULL,
                                 (yaml_char_t *)str,
                                 (int)strlen(str),
                                 1,
                                 1,
                                 YAML_SINGLE_QUOTED_SCALAR_STYLE);

    EMIT_VALUE(ctx, node, &event);

    free(str);
}

static void emitFloat(YamlDumpContext *ctx, const AstNode *node, f64 value)
{
    char str[64];
    yaml_event_t event;
    int num = snprintf(str, sizeof(str), "%f", value);
    csAssert0(num >= 0);

    yaml_scalar_event_initialize(&event,
                                 NULL,
                                 NULL,
                                 (yaml_char_t *)str,
                                 num,
                                 1,
                                 1,
                                 YAML_ANY_SCALAR_STYLE);

    EMIT_VALUE(ctx, node, &event);
}

static void emitStringLiteral(YamlDumpContext *ctx,
                              const AstNode *value,
                              cstring lit)
{
    yaml_event_t event;
    yaml_scalar_event_initialize(&event,
                                 NULL,
                                 NULL,
                                 (yaml_char_t *)lit,
                                 (int)strlen(lit),
                                 1,
                                 1,
                                 YAML_DOUBLE_QUOTED_SCALAR_STYLE);

    EMIT_VALUE(ctx, value, &event);
}

static void emitStartMap(YamlDumpContext *ctx, const AstNode *node)
{
    yaml_event_t event;

    yaml_mapping_start_event_initialize(
        &event, NULL, NULL, 1, YAML_BLOCK_MAPPING_STYLE);

    EMIT_VALUE(ctx, node, &event);
}

static void emitEndMap(YamlDumpContext *ctx, const AstNode *value)
{
    yaml_event_t event;

    yaml_mapping_end_event_initialize(&event);
    EMIT_VALUE(ctx, value, &event);
}

static void emitStartArray(YamlDumpContext *ctx, const AstNode *value)
{
    yaml_event_t event;

    yaml_sequence_start_event_initialize(
        &event, NULL, NULL, 0, YAML_ANY_SEQUENCE_STYLE);

    EMIT_VALUE(ctx, value, &event);
}

static void emitEndArray(YamlDumpContext *ctx, const AstNode *value)
{
    yaml_event_t event;

    yaml_sequence_end_event_initialize(&event);
    EMIT_VALUE(ctx, value, &event);
}

static void nodeToYaml(ConstAstVisitor *visitor,
                       cstring name,
                       const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    if (node == NULL)
        return;

    if (name)
        emitMapKey(ctx, node, name);

    emitStartMap(ctx, node);
    emitMapKey(ctx, node, getAstNodeName(node));
    emitEmpty(ctx, node);

    astConstVisit(visitor, node);
    emitEndMap(ctx, node);
}

static void manyNodesToJson(ConstAstVisitor *visitor,
                            cstring name,
                            const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    if (node == NULL)
        return;
    emitMapKey(ctx, node, name);
    emitStartArray(ctx, node);
    for (const AstNode *it = node; it; it = it->next) {
        nodeToYaml(visitor, NULL, it);
    }
    emitEndArray(ctx, node);
}

static void emitNodeFilePosition(YamlDumpContext *ctx,
                                 const AstNode *node,
                                 const FilePos *pos)
{
    emitStartMap(ctx, node);
    emitMapKey(ctx, node, "row");
    emitInteger(ctx, node, pos->row);
    emitMapKey(ctx, node, "col");
    emitInteger(ctx, node, pos->col);
    emitMapKey(ctx, node, "byteOffset");
    emitUInteger(ctx, node, pos->byteOffset);
    emitEndMap(ctx, node);
}

static void emitNodeFileLocation(YamlDumpContext *ctx, const AstNode *node)
{
    const FileLoc *loc = &node->loc;
    emitStartMap(ctx, node);
    emitMapKey(ctx, node, "fileName");
    emitStringLiteral(ctx, node, loc->fileName);
    {
        emitMapKey(ctx, node, "begin");
        emitNodeFilePosition(ctx, node, &loc->begin);
    }
    {
        emitMapKey(ctx, node, "end");
        emitNodeFilePosition(ctx, node, &loc->end);
    }
    emitEndMap(ctx, node);
}

static void nodeAddHeader(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    if (node->flags) {
        emitMapKey(ctx, node, "flags");
        if (ctx->config.withNamedEnums) {
            char *str = flagsToString(node->flags);
            emitMapKey(ctx, node, str);
            free(str);
        }
        else {
            emitUInteger(ctx, node, node->flags);
        }
    }

    if (ctx->config.withLocation) {
        emitMapKey(ctx, node, "loc");
        emitNodeFileLocation(ctx, node);
    }

    if (!ctx->config.withoutAttrs) {
        manyNodesToJson(visitor, "loc", node->attrs);
    }
}

static void visitProgram(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "module", node->program.module);
    manyNodesToJson(visitor, "top", node->program.top);
    manyNodesToJson(visitor, "decls", node->program.decls);
}

static void visitError(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);
    emitMapKey(ctx, node, "message");
    emitStringLiteral(ctx, node, node->error.message);
}

static void visitNoop(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);
}

static void visitLiteral(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);
    emitMapKey(ctx, node, "value");
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

static void visitAttr(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    emitMapKey(ctx, node, "name");
    emitMapKey(ctx, node, node->attr.name);
    manyNodesToJson(visitor, "args", node->attr.args);
}

static void visitStrExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    manyNodesToJson(visitor, "parts", node->stringExpr.parts);
}

static void visitDefine(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    manyNodesToJson(visitor, "names", node->define.names);

    nodeToYaml(visitor, "defineType", node->define.type);

    nodeToYaml(visitor, "args", node->define.container);
}

static void visitImport(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "alias", node->import.alias);

    nodeToYaml(visitor, "module", node->import.module);
    manyNodesToJson(visitor, "entities", node->import.entities);
}

static void visitImportEntity(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    emitMapKey(ctx, node, "name");
    emitMapKey(ctx, node, node->importEntity.name);
    emitMapKey(ctx, node, "alias");
    emitMapKey(ctx, node, node->importEntity.alias);
}

static void visitModuleDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    if (node->moduleDecl.name) {
        emitMapKey(ctx, node, "name");
        emitMapKey(ctx, node, node->moduleDecl.name);
    }
}

static void visitIdentifier(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);
    if (node->ident.alias) {
        emitMapKey(ctx, node, "alias");
        emitMapKey(ctx, node, node->ident.alias);
    }

    emitMapKey(ctx, node, "value");
    emitMapKey(ctx, node, node->ident.value);
}

static void visitTuple(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    manyNodesToJson(visitor, "members", node->tupleType.args);
}

static void visitArrayType(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "element", node->arrayType.elementType);

    nodeToYaml(visitor, "dim", node->arrayType.dim);
}

static void visitFuncType(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "ret", node->funcType.ret);
    manyNodesToJson(visitor, "params", node->funcType.params);
}

static void visitOptionalType(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "optType", node->optionalType.type);
}

static void visitPrimitiveType(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    emitMapKey(ctx, node, "prtId");
    emitUInteger(ctx, node, node->primitiveType.id);
}

static void visitHeaderOnly(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);
}

static void visitPointerType(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "pointed", node->pointerType.pointed);
}

static void visitArrayExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    manyNodesToJson(visitor, "elements", node->arrayExpr.elements);
}

static void visitMemberExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "target", node->memberExpr.target);

    nodeToYaml(visitor, "member", node->memberExpr.member);
}

static void visitRangeExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "start", node->rangeExpr.start);

    nodeToYaml(visitor, "end", node->rangeExpr.end);

    nodeToYaml(visitor, "step", node->rangeExpr.step);
}

static void visitNewExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "newType", node->newExpr.type);

    nodeToYaml(visitor, "init", node->newExpr.init);
}

static void visitCastExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "to", node->castExpr.to);
    nodeToYaml(visitor, "exp", node->castExpr.expr);
}

static void visitIndexExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "target", node->indexExpr.target);
    if (node->indexExpr.index)
        nodeToYaml(visitor, "index", node->indexExpr.index);
}

static void visitGenericParam(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    emitMapKey(ctx, node, "name");
    emitMapKey(ctx, node, node->genericParam.name);
    manyNodesToJson(visitor, "constraints", node->genericParam.constraints);
}

static void visitGenericDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "decl", node->genericDecl.decl);
    manyNodesToJson(visitor, "params", node->genericDecl.params);
}

static void visitPathElement(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    manyNodesToJson(visitor, "args", node->pathElement.args);

    emitMapKey(ctx, node, "name");
    emitMapKey(ctx, node, node->pathElement.name);

    if (node->pathElement.alt) {
        emitMapKey(ctx, node, "alt");
        emitMapKey(ctx, node, node->pathElement.alt);
    }

    if (node->pathElement.alt2) {
        emitMapKey(ctx, node, "alt2");
        emitMapKey(ctx, node, node->pathElement.alt2);
    }
}

static void visitPath(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    manyNodesToJson(visitor, "elements", node->path.elements);
    emitMapKey(ctx, node, "isType");
    emitBool(ctx, node, node->path.isType);
}

static void visitFuncDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    if (node->funcDecl.operatorOverload != opInvalid) {
        emitMapKey(ctx, node, "overload");
        emitMapKey(
            ctx, node, getOpOverloadName(node->funcDecl.operatorOverload));
    }

    if (node->funcDecl.index != 0) {
        emitMapKey(ctx, node, "index");
        emitUInteger(ctx, node, node->funcDecl.index);
    }

    emitMapKey(ctx, node, "name");
    emitMapKey(ctx, node, node->funcDecl.name);
    manyNodesToJson(visitor, "params", node->funcDecl.params);

    nodeToYaml(visitor, "ret", node->funcDecl.ret);
    nodeToYaml(visitor, "body", node->funcDecl.body);
}

static void visitMacroDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    emitMapKey(ctx, node, "name");
    emitMapKey(ctx, node, node->macroDecl.name);
    manyNodesToJson(visitor, "params", node->macroDecl.params);

    nodeToYaml(visitor, "ret", node->macroDecl.ret);

    nodeToYaml(visitor, "body", node->macroDecl.body);
}

static void visitFuncParam(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    emitMapKey(ctx, node, "name");
    emitMapKey(ctx, node, node->funcParam.name);
    emitMapKey(ctx, node, "index");
    emitUInteger(ctx, node, node->funcParam.index);

    nodeToYaml(visitor, "paramType", node->funcParam.type);

    nodeToYaml(visitor, "default", node->funcParam.def);
}

static void visitVarDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    manyNodesToJson(visitor, "names", node->varDecl.names);

    nodeToYaml(visitor, "varType", node->varDecl.type);

    nodeToYaml(visitor, "init", node->varDecl.init);
}

static void visitTypeDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    emitMapKey(ctx, node, "name");
    emitMapKey(ctx, node, node->typeDecl.name);

    nodeToYaml(visitor, "aliased", node->typeDecl.aliased);
}

static void visitUnionDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    emitMapKey(ctx, node, "name");
    emitMapKey(ctx, node, node->unionDecl.name);
    manyNodesToJson(visitor, "members", node->unionDecl.members);
}

static void visitEnumOption(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    emitMapKey(ctx, node, "name");
    emitMapKey(ctx, node, node->enumOption.name);

    nodeToYaml(visitor, "value", node->enumOption.value);
    emitMapKey(ctx, node, "index");
    emitUInteger(ctx, node, node->enumOption.index);
}

static void visitEnumDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    emitMapKey(ctx, node, "name");
    emitMapKey(ctx, node, node->enumDecl.name);

    nodeToYaml(visitor, "base", node->enumDecl.base);
    manyNodesToJson(visitor, "options", node->enumDecl.options);
}

static void visitStructField(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    emitMapKey(ctx, node, "name");
    emitMapKey(ctx, node, node->structField.name);

    nodeToYaml(visitor, "type", node->structField.type);

    nodeToYaml(visitor, "value", node->structField.value);
    emitMapKey(ctx, node, "index");
    emitUInteger(ctx, node, node->structField.index);
}

static void visitStructDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    emitMapKey(ctx, node, "name");
    emitMapKey(ctx, node, node->structDecl.name);

    nodeToYaml(visitor, "base", node->structDecl.base);
    manyNodesToJson(visitor, "members", node->structDecl.members);
}

static void visitBinaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    if (ctx->config.withNamedEnums) {
        emitMapKey(ctx, node, "op");
        emitUInteger(ctx, node, node->binaryExpr.op);
    }
    else {
        emitMapKey(ctx, node, "op");
        emitMapKey(ctx,
                   node,
                   (nodeIs(node, BinaryExpr)
                        ? getBinaryOpString(node->binaryExpr.op)
                        : getAssignOpString(node->assignExpr.op)));
    }

    nodeToYaml(visitor, "lhs", node->binaryExpr.lhs);

    nodeToYaml(visitor, "rhs", node->binaryExpr.lhs);
}

static void visitUnaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    if (ctx->config.withNamedEnums) {
        emitMapKey(ctx, node, "op");
        emitUInteger(ctx, node, node->unaryExpr.op);
    }
    else {
        emitMapKey(ctx, node, "op");
        emitMapKey(ctx, node, getUnaryOpString(node->unaryExpr.op));
    }
    emitMapKey(ctx, node, "isPrefix");
    emitBool(ctx, node, node->unaryExpr.isPrefix);

    nodeToYaml(visitor, "lhs", node->unaryExpr.operand);
}

static void visitTernaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "cond", node->ternaryExpr.cond);

    nodeToYaml(visitor, "body", node->ternaryExpr.body);

    nodeToYaml(visitor, "otherwise", node->ternaryExpr.otherwise);
}

static void visitStmtExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "stmt", node->stmtExpr.stmt);
}

static void visitTypedExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "teType", node->typedExpr.type);

    nodeToYaml(visitor, "expr", node->typedExpr.expr);
}

static void visitCallExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "callee", node->callExpr.callee);
    manyNodesToJson(visitor, "args", node->callExpr.args);
    if (node->callExpr.overload != 0) {
        emitMapKey(ctx, node, "overload");
        emitUInteger(ctx, node, node->callExpr.overload);
    }
}

static void visitClosureExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "ret", node->closureExpr.ret);
    manyNodesToJson(visitor, "params", node->closureExpr.params);
    nodeToYaml(visitor, "body", node->closureExpr.body);
}

static void visitFieldExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    emitMapKey(ctx, node, "name");
    emitMapKey(ctx, node, node->fieldExpr.name);

    nodeToYaml(visitor, "value", node->fieldExpr.value);
}

static void visitStructExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "left", node->structExpr.left);
    manyNodesToJson(visitor, "fields", node->structExpr.fields);
}

static void visitExpressionStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "expr", node->exprStmt.expr);
}

static void visitContinueStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    nodeAddHeader(visitor, node);
}

static void visitReturnStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "expr", node->returnStmt.expr);
}

static void visitBlockStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    manyNodesToJson(visitor, "stmts", node->blockStmt.stmts);
}

static void visitForStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "stmts", node->forStmt.var);

    nodeToYaml(visitor, "range", node->forStmt.range);

    nodeToYaml(visitor, "body", node->forStmt.body);
}

static void visitWhileStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "cond", node->whileStmt.cond);

    nodeToYaml(visitor, "body", node->whileStmt.body);
}

static void visitSwitchStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "cond", node->switchStmt.cond);
    manyNodesToJson(visitor, "cases", node->switchStmt.cases);
}

static void visitCaseStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);

    nodeToYaml(visitor, "match", node->caseStmt.match);
    manyNodesToJson(visitor, "body", node->caseStmt.body);
}

static void visitFallback(ConstAstVisitor *visitor, const AstNode *node)
{
    YamlDumpContext *ctx = getConstAstVisitorContext(visitor);
    nodeAddHeader(visitor, node);
}

AstNode *dumpAstToYaml(CompilerDriver *driver, AstNode *node, FILE *file)
{
    yaml_event_t event;
    yaml_emitter_t emitter;
    YamlDumpContext ctx = {
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
        [astStructField] = visitStructField,
        [astStructDecl] = visitStructDecl,
        [astAssignExpr] = visitBinaryExpr,
        [astBinaryExpr] = visitBinaryExpr,
        [astUnaryExpr] = visitUnaryExpr,
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
