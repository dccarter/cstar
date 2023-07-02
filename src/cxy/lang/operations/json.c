//
// Created by Carter on 2023-06-29.
//

#include "json.h"
#include "lang/flag.h"

typedef struct {
    cJSON *value;
    MemPool *pool;
    AstNodeToJsonConfig config;
} JsonConverterContext;

#define Return(CTX, NODE) (CTX)->value = (NODE)

static cJSON *nodeToJson(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    if (node)
        astConstVisit(visitor, node);
    else
        ctx->value = NULL;

    return ctx->value;
}

static cJSON *manyNodesToJson(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *array;
    if (node) {
        array = cJSON_CreateArray();
        for (const AstNode *it = node; it; it = it->next) {
            astConstVisit(visitor, it);
            cJSON_AddItemToArray(array, ctx->value);
        }
    }
    else
        array = NULL;

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
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);

    cJSON *jsonNode = cJSON_CreateObject();
    if (ctx->config.withNamedEnums) {
        cJSON_AddStringToObject(jsonNode, "tag", getAstNodeName(node));
    }
    else {
        cJSON_AddNumberToObject(jsonNode, "tag", node->tag);
    }

    if (node->flags) {
        if (ctx->config.withNamedEnums) {
            char *str = flagsToString(node->flags);
            cJSON_AddStringToObject(jsonNode, "flags", str);
            free(str);
        }
        else {
            cJSON_AddNumberToObject(jsonNode, "flags", node->flags);
        }
    }

    if (ctx->config.includeLocation) {
        cJSON *loc = NULL;
        cJSON_AddItemToObject(jsonNode, "loc", loc = cJSON_CreateObject());

        nodePopulateLocation(loc, &node->loc);
    }

    if (!ctx->config.withoutAttrs) {
        cJSON_AddItemToObject(
            jsonNode, "attrs", manyNodesToJson(visitor, node->attrs));
    }

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

static void visitImport(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "alias", nodeToJson(visitor, node->import.alias));

    cJSON_AddItemToObject(
        jsonNode, "module", nodeToJson(visitor, node->import.module));
    cJSON_AddItemToObject(
        jsonNode, "entities", manyNodesToJson(visitor, node->import.entities));

    Return(ctx, jsonNode);
}

static void visitImportEntity(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddStringToObject(jsonNode, "name", node->importEntity.name);
    cJSON_AddStringToObject(jsonNode, "alias", node->importEntity.alias);
    cJSON_AddStringToObject(jsonNode, "module", node->importEntity.module);
    cJSON_AddStringToObject(jsonNode, "path", node->importEntity.path);

    Return(ctx, jsonNode);
}

static void visitModuleDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddStringToObject(jsonNode, "name", node->moduleDecl.name);

    Return(ctx, jsonNode);
}

static void visitIdentifier(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddStringToObject(jsonNode, "alias", node->ident.alias);
    cJSON_AddStringToObject(jsonNode, "value", node->ident.value);

    Return(ctx, jsonNode);
}

static void visitTuple(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "members", manyNodesToJson(visitor, node->tupleType.args));

    if (node->tupleType.len)
        cJSON_AddNumberToObject(jsonNode, "len", node->tupleType.len);

    Return(ctx, jsonNode);
}

static void visitArrayType(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "element", nodeToJson(visitor, node->arrayType.elementType));

    cJSON_AddItemToObject(
        jsonNode, "dim", nodeToJson(visitor, node->arrayType.dim));

    Return(ctx, jsonNode);
}

static void visitFuncType(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "ret", nodeToJson(visitor, node->funcType.ret));
    cJSON_AddItemToObject(
        jsonNode, "params", manyNodesToJson(visitor, node->funcType.params));

    Return(ctx, jsonNode);
}

static void visitOptionalType(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "optType", nodeToJson(visitor, node->optionalType.type));

    Return(ctx, jsonNode);
}

static void visitPrimitiveType(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddNumberToObject(jsonNode, "prtId", node->primitiveType.id);

    Return(ctx, jsonNode);
}

static void visitStringType(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    Return(ctx, jsonNode);
}

static void visitPointerType(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "pointed", nodeToJson(visitor, node->pointerType.pointed));

    Return(ctx, jsonNode);
}

static void visitArrayExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(jsonNode,
                          "elements",
                          manyNodesToJson(visitor, node->arrayExpr.elements));
    cJSON_AddNumberToObject(jsonNode, "len", node->arrayExpr.len);

    Return(ctx, jsonNode);
}

static void visitMemberExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "target", nodeToJson(visitor, node->memberExpr.target));
    cJSON_AddItemToObject(
        jsonNode, "member", nodeToJson(visitor, node->memberExpr.member));
    Return(ctx, jsonNode);
}

static void visitRangeExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "start", nodeToJson(visitor, node->rangeExpr.start));
    cJSON_AddItemToObject(
        jsonNode, "end", nodeToJson(visitor, node->rangeExpr.end));
    cJSON_AddItemToObject(
        jsonNode, "step", nodeToJson(visitor, node->rangeExpr.step));
    Return(ctx, jsonNode);
}

static void visitNewExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "newType", nodeToJson(visitor, node->newExpr.type));
    cJSON_AddItemToObject(
        jsonNode, "init", nodeToJson(visitor, node->newExpr.init));

    Return(ctx, jsonNode);
}

static void visitCastExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "to", nodeToJson(visitor, node->castExpr.to));
    cJSON_AddItemToObject(
        jsonNode, "exp", nodeToJson(visitor, node->castExpr.expr));

    Return(ctx, jsonNode);
}

static void visitIndexExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "target", nodeToJson(visitor, node->indexExpr.target));
    cJSON_AddItemToObject(
        jsonNode, "index", nodeToJson(visitor, node->indexExpr.index));

    Return(ctx, jsonNode);
}

static void visitGenericParam(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddStringToObject(jsonNode, "name", node->genericParam.name);
    cJSON_AddItemToObject(jsonNode,
                          "constraints",
                          nodeToJson(visitor, node->genericParam.constraints));

    Return(ctx, jsonNode);
}

static void visitGenericDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "decl", nodeToJson(visitor, node->genericDecl.decl));
    cJSON_AddItemToObject(
        jsonNode, "params", manyNodesToJson(visitor, node->genericDecl.params));

    Return(ctx, jsonNode);
}

static void visitPathElement(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "args", manyNodesToJson(visitor, node->pathElement.args));

    cJSON_AddStringToObject(jsonNode, "name", node->pathElement.name);
    cJSON_AddStringToObject(jsonNode, "alt", node->pathElement.alt);
    cJSON_AddStringToObject(jsonNode, "alt2", node->pathElement.alt2);
    cJSON_AddNumberToObject(jsonNode, "index", node->pathElement.index);

    Return(ctx, jsonNode);
}

static void visitPath(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "elements", manyNodesToJson(visitor, node->path.elements));
    cJSON_AddBoolToObject(jsonNode, "isType", node->path.isType);

    Return(ctx, jsonNode);
}

static void visitFuncDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddNumberToObject(
        jsonNode, "overload", node->funcDecl.operatorOverload);
    cJSON_AddNumberToObject(jsonNode, "index", node->funcDecl.index);
    cJSON_AddStringToObject(jsonNode, "name", node->funcDecl.name);
    cJSON_AddItemToObject(
        jsonNode, "params", manyNodesToJson(visitor, node->funcDecl.params));
    cJSON_AddItemToObject(
        jsonNode, "ret", nodeToJson(visitor, node->funcDecl.ret));
    cJSON_AddItemToObject(
        jsonNode, "body", nodeToJson(visitor, node->funcDecl.body));

    Return(ctx, jsonNode);
}

static void visitMacroDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddStringToObject(jsonNode, "name", node->macroDecl.name);
    cJSON_AddItemToObject(
        jsonNode, "params", manyNodesToJson(visitor, node->macroDecl.params));
    cJSON_AddItemToObject(
        jsonNode, "ret", nodeToJson(visitor, node->macroDecl.ret));
    cJSON_AddItemToObject(
        jsonNode, "body", nodeToJson(visitor, node->macroDecl.body));

    Return(ctx, jsonNode);
}

static void visitFuncParam(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddStringToObject(jsonNode, "name", node->funcParam.name);
    cJSON_AddNumberToObject(jsonNode, "index", node->funcParam.index);

    cJSON_AddItemToObject(
        jsonNode, "paramType", nodeToJson(visitor, node->funcParam.type));
    cJSON_AddItemToObject(
        jsonNode, "default", nodeToJson(visitor, node->funcParam.def));

    Return(ctx, jsonNode);
}

static void visitVarDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "names", manyNodesToJson(visitor, node->varDecl.names));
    cJSON_AddItemToObject(
        jsonNode, "varType", nodeToJson(visitor, node->varDecl.type));
    cJSON_AddItemToObject(
        jsonNode, "init", nodeToJson(visitor, node->varDecl.init));

    Return(ctx, jsonNode);
}

static void visitTypeDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddStringToObject(jsonNode, "name", node->typeDecl.name);
    cJSON_AddItemToObject(
        jsonNode, "aliased", manyNodesToJson(visitor, node->typeDecl.aliased));

    Return(ctx, jsonNode);
}

static void visitUnionDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddStringToObject(jsonNode, "name", node->unionDecl.name);
    cJSON_AddItemToObject(
        jsonNode, "members", manyNodesToJson(visitor, node->unionDecl.members));

    Return(ctx, jsonNode);
}

static void visitEnumOption(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddStringToObject(jsonNode, "name", node->enumOption.name);
    cJSON_AddItemToObject(
        jsonNode, "value", nodeToJson(visitor, node->enumOption.value));
    cJSON_AddNumberToObject(jsonNode, "index", node->enumOption.index);

    Return(ctx, jsonNode);
}

static void visitEnumDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddStringToObject(jsonNode, "name", node->enumDecl.name);
    cJSON_AddItemToObject(
        jsonNode, "base", nodeToJson(visitor, node->enumDecl.base));
    cJSON_AddItemToObject(
        jsonNode, "options", manyNodesToJson(visitor, node->enumDecl.options));
    Return(ctx, jsonNode);
}

static void visitStructField(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddStringToObject(jsonNode, "name", node->structField.name);
    cJSON_AddItemToObject(
        jsonNode, "type", nodeToJson(visitor, node->structField.type));
    cJSON_AddItemToObject(
        jsonNode, "value", nodeToJson(visitor, node->structField.value));
    cJSON_AddNumberToObject(jsonNode, "index", node->structField.index);

    Return(ctx, jsonNode);
}

static void visitStructDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddStringToObject(jsonNode, "name", node->structDecl.name);
    cJSON_AddItemToObject(
        jsonNode, "base", nodeToJson(visitor, node->structDecl.base));
    cJSON_AddItemToObject(jsonNode,
                          "members",
                          manyNodesToJson(visitor, node->structDecl.members));

    Return(ctx, jsonNode);
}

static void visitBinaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    if (ctx->config.withNamedEnums) {
        cJSON_AddNumberToObject(jsonNode, "op", node->binaryExpr.op);
    }
    else {
        cJSON_AddStringToObject(jsonNode,
                                "op",
                                (nodeIs(node, BinaryExpr)
                                     ? getBinaryOpString(node->binaryExpr.op)
                                     : getAssignOpString(node->assignExpr.op)));
    }

    cJSON_AddItemToObject(
        jsonNode, "lhs", nodeToJson(visitor, node->binaryExpr.lhs));
    cJSON_AddItemToObject(
        jsonNode, "rhs", nodeToJson(visitor, node->binaryExpr.lhs));

    Return(ctx, jsonNode);
}

static void visitUnaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    if (ctx->config.withNamedEnums) {
        cJSON_AddNumberToObject(jsonNode, "op", node->unaryExpr.op);
    }
    else {
        cJSON_AddStringToObject(
            jsonNode, "op", getUnaryOpString(node->unaryExpr.op));
    }
    cJSON_AddBoolToObject(jsonNode, "isPrefix", node->unaryExpr.isPrefix);

    cJSON_AddItemToObject(
        jsonNode, "lhs", nodeToJson(visitor, node->unaryExpr.operand));

    Return(ctx, jsonNode);
}

static void visitTernaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "cond", nodeToJson(visitor, node->ternaryExpr.cond));
    cJSON_AddItemToObject(
        jsonNode, "body", nodeToJson(visitor, node->ternaryExpr.body));
    cJSON_AddItemToObject(jsonNode,
                          "otherwise",
                          nodeToJson(visitor, node->ternaryExpr.otherwise));

    Return(ctx, jsonNode);
}

static void visitStmtExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "stmt", nodeToJson(visitor, node->stmtExpr.stmt));

    Return(ctx, jsonNode);
}

static void visitTypedExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "teType", nodeToJson(visitor, node->typedExpr.type));
    cJSON_AddItemToObject(
        jsonNode, "expr", nodeToJson(visitor, node->typedExpr.expr));

    Return(ctx, jsonNode);
}

static void visitCallExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "callee", nodeToJson(visitor, node->callExpr.callee));
    cJSON_AddItemToObject(
        jsonNode, "args", manyNodesToJson(visitor, node->callExpr.args));
    cJSON_AddNumberToObject(jsonNode, "overload", node->callExpr.overload);

    Return(ctx, jsonNode);
}

static void visitClosureExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "ret", nodeToJson(visitor, node->closureExpr.ret));
    cJSON_AddItemToObject(
        jsonNode, "params", manyNodesToJson(visitor, node->closureExpr.params));
    cJSON_AddItemToObject(
        jsonNode, "body", nodeToJson(visitor, node->closureExpr.body));

    Return(ctx, jsonNode);
}

static void visitFieldExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddStringToObject(jsonNode, "name", node->fieldExpr.name);
    cJSON_AddItemToObject(
        jsonNode, "value", nodeToJson(visitor, node->fieldExpr.value));

    Return(ctx, jsonNode);
}

static void visitStructExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "left", nodeToJson(visitor, node->structExpr.left));
    cJSON_AddItemToObject(
        jsonNode, "fields", manyNodesToJson(visitor, node->structExpr.fields));

    Return(ctx, jsonNode);
}

static void visitExpressionStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "expr", nodeToJson(visitor, node->exprStmt.expr));

    Return(ctx, jsonNode);
}

static void visitContinueStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    Return(ctx, jsonNode);
}

static void visitReturnStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "expr", nodeToJson(visitor, node->returnStmt.expr));

    Return(ctx, jsonNode);
}

static void visitBlockStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "stmts", manyNodesToJson(visitor, node->blockStmt.stmts));

    Return(ctx, jsonNode);
}

static void visitForStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "stmts", nodeToJson(visitor, node->forStmt.var));
    cJSON_AddItemToObject(
        jsonNode, "range", nodeToJson(visitor, node->forStmt.range));
    cJSON_AddItemToObject(
        jsonNode, "body", nodeToJson(visitor, node->forStmt.body));

    Return(ctx, jsonNode);
}

static void visitWhileStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "cond", nodeToJson(visitor, node->whileStmt.cond));
    cJSON_AddItemToObject(
        jsonNode, "body", nodeToJson(visitor, node->whileStmt.body));

    Return(ctx, jsonNode);
}

static void visitSwitchStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "cond", nodeToJson(visitor, node->switchStmt.cond));
    cJSON_AddItemToObject(
        jsonNode, "cases", manyNodesToJson(visitor, node->switchStmt.cases));

    Return(ctx, jsonNode);
}

static void visitCaseStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);

    cJSON_AddItemToObject(
        jsonNode, "match", nodeToJson(visitor, node->caseStmt.match));
    cJSON_AddItemToObject(
        jsonNode, "body", manyNodesToJson(visitor, node->caseStmt.body));

    Return(ctx, jsonNode);
}

static void visitFallback(ConstAstVisitor *visitor, const AstNode *node)
{
    JsonConverterContext *ctx = getConstAstVisitorContext(visitor);
    cJSON *jsonNode = nodeCreateJSON(visitor, node);
    cJSON_AddNullToObject(jsonNode, "__fallback");

    Return(ctx, jsonNode);
}

cJSON *convertToJson(AstNodeToJsonConfig *config,
                     MemPool *pool,
                     const AstNode *node)
{
    JsonConverterContext ctx = {.pool = pool, .config = *config};
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
        [astStringType] = visitStringType,
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
    astConstVisit(&visitor, node);
    return ctx.value;
}