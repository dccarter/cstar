//
// Created by Carter on 2023-06-29.
//

#include "json.h"

typedef struct {
    cJSON *value;
    MemPool *pool;
} JsonConverterContext;

#define Return(CTX, NODE) (CTX)->value = (NODE)

static cJSON *nodeToJson(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    if (node)
        astConstVisit(visitor, node);
    else
        ctx->value = cJSON_CreateNull();

    return ctx->value;
}

static cJSON *manyNodesToJson(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *array;
    if (node) {
        array = cJSON_CreateArray();
        for (const AstNode *it = node; it; it = it->next) {
            astConstVisit(visitor, node);
            cJSON_AddItemToArray(array, ctx->value);
        }
    }
    else
        array = cJSON_CreateNull();

    return (ctx->value = array);
}

static void nodePopulateFilePos(cJSON *loc, const FilePos *pos)
{
    cJSON_AddNumberToObject(loc, "row", pos->row);
    cJSON_AddNumberToObject(loc, "col", pos->col);
    cJSON_AddNumberToObject(loc, "byteOffset", pos->byteOffset);
}

static void nodePopulateLocation(cJSON *jsonNode, const FileLoc *loc)
{
    cJSON_AddStringToObject(jsonNode, "filename", loc->fileName);
    {
        cJSON *begin = NULL;
        cJSON_AddItemToObject(jsonNode, "begin", begin = cJSON_CreateObject());
        nodePopulateFilePos(begin, &loc->begin);
    }
    {
        cJSON *end = NULL;
        cJSON_AddItemToObject(jsonNode, "end", end = cJSON_CreateObject());
        nodePopulateFilePos(end, &loc->end);
    }
}

static cJSON *nodeCreateJSON(ConstAstVisitor *visitor, const AstNode *node)
{
    cJSON *jsonNode = cJSON_CreateObject();
    cJSON_AddNumberToObject(jsonNode, "tag", node->tag);
    cJSON_AddNumberToObject(jsonNode, "flags", node->flags);
    cJSON *loc = NULL;
    cJSON_AddItemToObject(jsonNode, "loc", loc = cJSON_CreateObject());
    nodePopulateLocation(loc, &node->loc);

    cJSON_AddItemToObject(
        jsonNode, "attrs", manyNodesToJson(visitor, node->attrs));

    return jsonNode;
}

static void visitProgram(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);
    cJSON_AddItemToObject(
        jsonNode, "module", nodeToJson(visitor, node->program.module));
    cJSON_AddItemToObject(
        jsonNode, "top", manyNodesToJson(visitor, node->program.top));
    cJSON_AddItemToObject(
        jsonNode, "decls", manyNodesToJson(visitor, node->program.decls));
    Return(ctx, jsonNode);
}

static void visitError(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);
    cJSON_AddStringToObject(jsonNode, "message", node->error.message);
    Return(ctx, jsonNode);
}

static void visitNoop(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);
    Return(ctx, jsonNode);
}

static void visitLiteral(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);
    switch (node->tag) {
    case astNullLit:
        cJSON_AddNullToObject(jsonNode, "value");
        break;
    case astBoolLit:
        cJSON_AddNumberToObject(jsonNode, "value", node->boolLiteral.value);
        break;
    case astCharLit:
        cJSON_AddNumberToObject(jsonNode, "value", node->charLiteral.value);
        break;
    case astIntegerLit:
        cJSON_AddNumberToObject(jsonNode, "value", node->intLiteral.value);
        break;
    case astFloatLit:
        cJSON_AddNumberToObject(jsonNode, "value", node->floatLiteral.value);
        break;
    case astStringLit:
        cJSON_AddStringToObject(jsonNode, "value", node->stringLiteral.value);
        break;
    default:
        csAssert0(false);
    }
    Return(ctx, jsonNode);
}

static void visitAttr(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddStringToObject(jsonNode, "name", node->attr.name);
    cJSON_AddItemToObject(
        jsonNode, "args", manyNodesToJson(visitor, node->attr.args));

    Return(ctx, jsonNode);
}

static void visitStrExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "parts", manyNodesToJson(visitor, node->stringExpr.parts));

    Return(ctx, jsonNode);
}

static void visitDefine(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "names", manyNodesToJson(visitor, node->define.names));

    cJSON_AddItemToObject(
        jsonNode, "defineType", nodeToJson(visitor, node->define.type));
    cJSON_AddItemToObject(
        jsonNode, "args", manyNodesToJson(visitor, node->define.container));

    Return(ctx, jsonNode);
}

static void visitFallback(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);
    cJSON_AddNullToObject(jsonNode, "__fallback");

    Return(ctx, jsonNode);
}

cJSON *convertToJson(MemPool *pool, const AstNode *node)
{
    JsonConverterContext ctx = {.pool = pool};
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
        [astDefine] = visitDefine
    }, .fallback = visitFallback );

    // clang-format on
    astConstVisit(&visitor, node);
    return ctx.value;
}