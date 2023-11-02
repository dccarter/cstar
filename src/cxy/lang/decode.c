//
// Created by Carter on 2023-06-30.
//

#include "codec.h"
#include "core/strpool.h"

#include "msgpack.h"

typedef struct {
    size_t off, size;
    const void *packed;
    const char *fname;
    msgpack_unpacked msg;
    StrPool *strPool;
    MemPool *pool;
} AstNodeUnpackContext;

static AstNode *unpackManyNodes(AstNodeUnpackContext *ctx, u64 *len);
static AstNode *unpackNode(AstNodeUnpackContext *ctx);

#define unpackPositiveInteger(msg, NAME)                                       \
    ({                                                                         \
        csAssert0((msg)->data.type == MSGPACK_OBJECT_POSITIVE_INTEGER);        \
        (msg)->data.via.NAME;                                                  \
    })

attr(always_inline) static u64 unpackU64(AstNodeUnpackContext *ctx)
{
    msgpack_unpack_next(&ctx->msg, ctx->packed, ctx->size, &ctx->off);
    csAssert0(ctx->msg.data.type == MSGPACK_OBJECT_POSITIVE_INTEGER);
    return ctx->msg.data.via.u64;
}

attr(always_inline) static f64 unpackFloat(AstNodeUnpackContext *ctx)
{
    msgpack_unpack_next(&ctx->msg, ctx->packed, ctx->size, &ctx->off);
    csAssert0(ctx->msg.data.type == MSGPACK_OBJECT_FLOAT);
    return ctx->msg.data.via.f64;
}

attr(always_inline) static u64 unpackI64(AstNodeUnpackContext *ctx)
{
    msgpack_unpack_next(&ctx->msg, ctx->packed, ctx->size, &ctx->off);
    csAssert0(ctx->msg.data.type == MSGPACK_OBJECT_NEGATIVE_INTEGER);
    return ctx->msg.data.via.i64;
}

attr(always_inline) static bool unpackBool(AstNodeUnpackContext *ctx)
{
    msgpack_unpack_next(&ctx->msg, ctx->packed, ctx->size, &ctx->off);
    csAssert0(ctx->msg.data.type == MSGPACK_OBJECT_BOOLEAN);
    return ctx->msg.data.via.boolean;
}

attr(always_inline) static void unpackNull(AstNodeUnpackContext *ctx)
{
    msgpack_unpack_next(&ctx->msg, ctx->packed, ctx->size, &ctx->off);
    csAssert0(ctx->msg.data.type == MSGPACK_OBJECT_NIL);
}

attr(always_inline) static const char *unpackString(AstNodeUnpackContext *ctx)
{
    msgpack_unpack_next(&ctx->msg, ctx->packed, ctx->size, &ctx->off);
    csAssert0(ctx->msg.data.type == MSGPACK_OBJECT_STR);

    return makeStringSized(
        ctx->strPool, ctx->msg.data.via.str.ptr, ctx->msg.data.via.str.size);
}

static void unpackPosition(AstNodeUnpackContext *ctx, FilePos *loc)
{
    loc->row = unpackU64(ctx);
    loc->col = unpackU64(ctx);
    loc->byteOffset = unpackU64(ctx);
}

static void unpackNodeBody(AstNodeUnpackContext *ctx, AstNode *node)
{

    u64 len = 0;
    switch (node->tag) {
    case astNop:
    case astStringType:
    case astAutoType:
    case astContinueStmt:
    case astBreakStmt:
        break;
    case astNullLit:
        unpackNull(ctx);
        break;
    case astBoolLit:
        node->boolLiteral.value = unpackBool(ctx);
        break;
    case astCharLit:
        node->charLiteral.value = unpackU64(ctx);
        break;
    case astIntegerLit:
        node->intLiteral.hasMinus = unpackBool(ctx);
        node->intLiteral.value = unpackU64(ctx);
        break;
    case astFloatLit:
        node->floatLiteral.value = unpackFloat(ctx);
        break;
    case astStringLit:
        node->stringLiteral.value = unpackString(ctx);
        break;
    case astError:
        node->error.message = unpackString(ctx);
        break;
    case astAttr:
        node->attr.name = unpackString(ctx);
        node->attr.args = unpackManyNodes(ctx, NULL);
        break;
    case astDefine:
        node->define.names = unpackManyNodes(ctx, NULL);
        node->define.type = unpackNode(ctx);
        node->define.container = unpackNode(ctx);
        break;
    case astImportDecl:
        node->import.alias = unpackNode(ctx);
        node->import.module = unpackNode(ctx);
        node->import.entities = unpackNode(ctx);
        break;
    case astImportEntity:
        node->importEntity.name = unpackString(ctx);
        node->importEntity.alias = unpackString(ctx);
        break;
    case astModuleDecl:
        node->moduleDecl.name = unpackString(ctx);
        break;
    case astIdentifier:
        node->ident.value = unpackString(ctx);
        node->ident.alias = unpackString(ctx);
        break;
    case astTupleType:
    case astTupleExpr:
        node->tupleType.elements = unpackManyNodes(ctx, &len);
        node->tupleType.len = len;
        break;
    case astArrayType:
        node->arrayType.elementType = unpackNode(ctx);
        node->arrayType.dim = unpackNode(ctx);
        break;
    case astFuncType:
        node->funcType.ret = unpackNode(ctx);
        node->funcType.params = unpackManyNodes(ctx, NULL);
        break;
    case astOptionalType:
        node->optionalType.type = unpackNode(ctx);
        break;
    case astPrimitiveType:
        node->primitiveType.id = unpackU64(ctx);
        break;
    case astPointerType:
        node->pointerType.pointed = unpackNode(ctx);
        break;
    case astArrayExpr:
        node->arrayExpr.elements = unpackManyNodes(ctx, &len);
        node->arrayExpr.len = len;
        break;
    case astMemberExpr:
        node->memberExpr.target = unpackNode(ctx);
        node->memberExpr.member = unpackNode(ctx);
        break;
    case astRangeExpr:
        node->rangeExpr.start = unpackNode(ctx);
        node->rangeExpr.end = unpackNode(ctx);
        node->rangeExpr.step = unpackNode(ctx);
        break;
    case astNewExpr:
        node->newExpr.type = unpackNode(ctx);
        node->newExpr.init = unpackNode(ctx);
        break;
    case astCastExpr:
        node->castExpr.to = unpackNode(ctx);
        node->castExpr.expr = unpackNode(ctx);
        break;
    case astIndexExpr:
        node->indexExpr.target = unpackNode(ctx);
        node->indexExpr.index = unpackNode(ctx);
        break;
    case astGenericParam:
        node->genericParam.name = unpackString(ctx);
        node->genericParam.constraints = unpackManyNodes(ctx, NULL);
        break;
    case astGenericDecl:
        node->genericDecl.decl = unpackNode(ctx);
        node->genericDecl.params = unpackManyNodes(ctx, NULL);
    case astPathElem:
        node->pathElement.args = unpackManyNodes(ctx, NULL);
        node->pathElement.name = unpackString(ctx);
        node->pathElement.alt = unpackString(ctx);
        node->pathElement.index = unpackU64(ctx);
        break;
    case astPath:
        node->path.elements = unpackManyNodes(ctx, NULL);
        node->path.isType = unpackBool(ctx);
        break;
    case astFuncDecl:
        node->funcDecl.operatorOverload = unpackU64(ctx);
        node->funcDecl.index = unpackU64(ctx);
        node->funcDecl.name = unpackString(ctx);
        node->funcDecl.signature = makeFunctionSignature(
            ctx->pool,
            &(FunctionSignature){.params = unpackManyNodes(ctx, NULL),
                                 .ret = unpackNode(ctx)});
        node->funcDecl.body = unpackNode(ctx);
        break;
    case astMacroDecl:
        node->macroDecl.name = unpackString(ctx);
        node->macroDecl.params = unpackManyNodes(ctx, NULL);
        node->macroDecl.ret = unpackNode(ctx);
        node->macroDecl.body = unpackNode(ctx);
        break;
    case astFuncParam:
        node->funcParam.name = unpackString(ctx);
        node->funcParam.type = unpackNode(ctx);
        node->funcParam.def = unpackNode(ctx);
        break;
    case astVarDecl:
        node->varDecl.names = unpackManyNodes(ctx, NULL);
        node->varDecl.type = unpackNode(ctx);
        node->varDecl.init = unpackNode(ctx);
        break;
    case astTypeDecl:
        node->typeDecl.name = unpackString(ctx);
        node->typeDecl.aliased = unpackNode(ctx);
        break;
    case astUnionDecl:
        node->unionDecl.members = unpackManyNodes(ctx, NULL);
        break;
    case astEnumOption:
        node->enumOption.name = unpackString(ctx);
        node->enumOption.value = unpackNode(ctx);
        break;
    case astEnumDecl:
        node->enumDecl.name = unpackString(ctx);
        node->enumDecl.base = unpackNode(ctx);
        node->enumDecl.options = unpackManyNodes(ctx, &len);
        node->enumDecl.len = len;
        break;
    case astField:
        node->structField.name = unpackString(ctx);
        node->structField.type = unpackNode(ctx);
        node->structField.value = unpackNode(ctx);
        break;
    case astStructDecl:
        node->structDecl.name = unpackString(ctx);
        node->structDecl.members = unpackManyNodes(ctx, NULL);
        node->structDecl.implements = unpackManyNodes(ctx, NULL);
        break;
    case astClassDecl:
        node->classDecl.name = unpackString(ctx);
        node->classDecl.base = unpackNode(ctx);
        node->classDecl.members = unpackManyNodes(ctx, NULL);
        node->classDecl.implements = unpackManyNodes(ctx, NULL);
        break;
    case astBinaryExpr:
        node->binaryExpr.op = unpackU64(ctx);
        node->binaryExpr.lhs = unpackNode(ctx);
        node->binaryExpr.lhs = unpackNode(ctx);
        break;
    case astUnaryExpr:
        node->unaryExpr.isPrefix = unpackU64(ctx);
        node->unaryExpr.op = unpackU64(ctx);
        node->unaryExpr.operand = unpackNode(ctx);
        break;
    case astTernaryExpr:
    case astIfStmt:
        node->ternaryExpr.cond = unpackNode(ctx);
        node->ternaryExpr.body = unpackNode(ctx);
        node->ternaryExpr.otherwise = unpackNode(ctx);
        break;
    case astStmtExpr:
        node->stmtExpr.stmt = unpackNode(ctx);
        break;
    case astTypedExpr:
        node->typedExpr.type = unpackNode(ctx);
        node->typedExpr.expr = unpackNode(ctx);
        break;
    case astCallExpr:
        node->callExpr.overload = unpackU64(ctx);
        node->callExpr.callee = unpackNode(ctx);
        node->callExpr.args = unpackManyNodes(ctx, NULL);
        break;
    case astClosureExpr:
        node->closureExpr.ret = unpackNode(ctx);
        node->closureExpr.params = unpackManyNodes(ctx, NULL);
        node->closureExpr.body = unpackNode(ctx);
        break;
    case astFieldExpr:
        node->fieldExpr.name = unpackString(ctx);
        node->fieldExpr.value = unpackNode(ctx);
        break;
    case astStructExpr:
        node->structExpr.left = unpackNode(ctx);
        node->structExpr.fields = unpackManyNodes(ctx, NULL);
        break;
    case astExprStmt:
    case astDeferStmt:
    case astGroupExpr:
        node->exprStmt.expr = unpackNode(ctx);
        break;
    case astReturnStmt:
        node->returnStmt.expr = unpackNode(ctx);
        break;
    case astBlockStmt:
        node->blockStmt.stmts = unpackManyNodes(ctx, NULL);
        node->blockStmt.last = getLastAstNode(node->blockStmt.last);
        break;
    case astForStmt:
        node->forStmt.var = unpackNode(ctx);
        node->forStmt.range = unpackNode(ctx);
        node->forStmt.body = unpackNode(ctx);
        break;
    case astWhileStmt:
        node->whileStmt.cond = unpackNode(ctx);
        node->whileStmt.body = unpackNode(ctx);
        break;
    case astSwitchStmt:
        node->switchStmt.cond = unpackNode(ctx);
        node->switchStmt.cases = unpackNode(ctx);
        break;
    case astCaseStmt:
        node->caseStmt.match = unpackNode(ctx);
        node->caseStmt.body = unpackNode(ctx);
        break;
    default:
        unreachable("UNKNOWN NODE ast%s", getAstNodeName(node));
    }
}

static AstNode *unpackNode(AstNodeUnpackContext *ctx)
{

    msgpack_unpack_return status =
        msgpack_unpack_next(&ctx->msg, ctx->packed, ctx->size, &ctx->off);
    csAssert0(status == MSGPACK_UNPACK_SUCCESS);

    if (ctx->msg.data.type == MSGPACK_OBJECT_NIL)
        return NULL;
    AstNode *node = allocFromMemPool(ctx->pool, sizeof(AstNode));
    node->tag = unpackPositiveInteger(&ctx->msg, u64);
    node->flags = unpackU64(ctx);

    node->loc.fileName = ctx->fname;
    unpackPosition(ctx, &node->loc.begin);
    unpackPosition(ctx, &node->loc.end);

    node->attrs = unpackManyNodes(ctx, NULL);

    unpackNodeBody(ctx, node);

    return node;
}

static AstNode *unpackManyNodes(AstNodeUnpackContext *ctx, u64 *len)
{
    msgpack_unpack_next(&ctx->msg, ctx->packed, ctx->size, &ctx->off);
    csAssert0(ctx->msg.data.type == MSGPACK_OBJECT_POSITIVE_INTEGER);
    u64 count = ctx->msg.data.via.u64;

    AstNode *node = NULL, *it = NULL;
    for (u64 i = 0; i < count; i++) {
        AstNode *next = unpackNode(ctx);
        if (node == NULL) {
            node = it = next;
        }
        else {
            it = it->next = next;
        }
    }
    if (len)
        *len = count;

    return node;
}

AstNode *binaryDecodeAstNode(const void *encoded, size_t size)
{
    AstNodeUnpackContext context = {.packed = encoded, .size = size};

    msgpack_unpacked_init(&context.msg);

    context.fname = unpackString(&context);
    AstNode *node = unpackNode(&context);
    msgpack_unpacked_destroy(&context.msg);

    return node;
}
