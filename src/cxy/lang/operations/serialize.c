//
// Created by Carter on 2023-06-30.
//

#include "codec.h"
#include <lang/visitor.h>

#include <msgpack.h>

typedef struct {
    msgpack_sbuffer *buffer;
    msgpack_packer packer;
} AstNodeUnpackContext;

attr(always_inline) static void packString(msgpack_packer *packer, cstring str)
{
    size_t len = strlen(str);
    msgpack_pack_str(packer, len);
    msgpack_pack_str_body(packer, str, len);
}

attr(always_inline) static void nodeToBinary(ConstAstVisitor *visitor,
                                             const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    if (node)
        astConstVisit(visitor, node);
    else
        msgpack_pack_nil(&ctx->packer);
}

static void manyNodesToBinary(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    u64 count = countAstNodes(node);
    msgpack_pack_array(&ctx->packer, count);
    for (const AstNode *it = node; it; it = it->next)
        astConstVisit(visitor, it);
}

static void nodePopulateFilePos(msgpack_packer *packer, const FilePos *pos)
{
    msgpack_pack_int32(packer, pos->row);
    msgpack_pack_int32(packer, pos->col);
    msgpack_pack_int64(packer, pos->byteOffset);
}

static void nodePopulateLocation(msgpack_packer *packer, const FileLoc *loc)
{
    nodePopulateFilePos(packer, &loc->begin);
    nodePopulateFilePos(packer, &loc->end);
}

static void nodePackHeader(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    msgpack_pack_int32(&ctx->packer, node->tag);
    msgpack_pack_int64(&ctx->packer, node->flags);
    nodePopulateLocation(&ctx->packer, &node->loc);
    manyNodesToBinary(visitor, node->attrs);
}

static void visitProgram(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);
    nodeToBinary(visitor, node->program.module);
    manyNodesToBinary(visitor, node->program.top);
    manyNodesToBinary(visitor, node->program.decls);
}

static void visitError(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);
    packString(&ctx->packer, node->error.message);
}

static void visitNoop(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);
}

static void visitLiteral(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);
    switch (node->tag) {
    case astNullLit:
        break;
    case astBoolLit:
        msgpack_pack_uint8(&ctx->packer, node->boolLiteral.value);
        break;
    case astCharLit:
        msgpack_pack_uint32(&ctx->packer, node->charLiteral.value);
        break;
    case astIntegerLit:
        msgpack_pack_uint8(&ctx->packer, node->intLiteral.hasMinus);
        msgpack_pack_uint64(&ctx->packer, node->intLiteral.value);
        break;
    case astFloatLit:
        msgpack_pack_double(&ctx->packer, node->floatLiteral.value);
        break;
    case astStringLit:
        packString(&ctx->packer, node->stringLiteral.value);
        break;
    default:
        csAssert0(false);
    }
}

static void visitAttr(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    packString(&ctx->packer, node->attr.name);
    manyNodesToBinary(visitor, node->attr.args);
}

static void visitStrExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    manyNodesToBinary(visitor, node->stringExpr.parts);
}

static void visitDefine(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    manyNodesToBinary(visitor, node->define.names);
    nodeToBinary(visitor, node->define.type);
    nodeToBinary(visitor, node->define.container);
}

static void visitImport(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);
    nodeToBinary(visitor, node->import.alias);
    nodeToBinary(visitor, node->import.module);
    manyNodesToBinary(visitor, node->import.entities);
}

static void visitImportEntity(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    packString(&ctx->packer, node->importEntity.name);
    packString(&ctx->packer, node->importEntity.alias);
    packString(&ctx->packer, node->importEntity.module);
    packString(&ctx->packer, node->importEntity.path);
}

static void visitModuleDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    packString(&ctx->packer, node->moduleDecl.name);
}

static void visitIdentifier(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    packString(&ctx->packer, node->ident.value);
    packString(&ctx->packer, node->ident.alias);
}

static void visitTuple(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    manyNodesToBinary(visitor, node->tupleType.args);
}

static void visitArrayType(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    nodeToBinary(visitor, node->arrayType.elementType);
    nodeToBinary(visitor, node->arrayType.dim);
}

static void visitFuncType(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    nodeToBinary(visitor, node->funcType.ret);
    manyNodesToBinary(visitor, node->funcType.params);
}

static void visitOptionalType(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    nodeToBinary(visitor, node->optionalType.type);
}

static void visitPrimitiveType(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    msgpack_pack_uint8(&ctx->packer, node->primitiveType.id);
}

static void visitStringType(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);
}

static void visitPointerType(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    nodeToBinary(visitor, node->pointerType.pointed);
}

static void visitArrayExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    manyNodesToBinary(visitor, node->arrayExpr.elements);
}

static void visitMemberExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    nodeToBinary(visitor, node->memberExpr.target);
    nodeToBinary(visitor, node->memberExpr.member);
}

static void visitRangeExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    nodeToBinary(visitor, node->rangeExpr.start);
    nodeToBinary(visitor, node->rangeExpr.end);
    nodeToBinary(visitor, node->rangeExpr.step);
}

static void visitNewExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    nodeToBinary(visitor, node->newExpr.type);
    nodeToBinary(visitor, node->newExpr.init);
}

static void visitCastExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    nodeToBinary(visitor, node->castExpr.to);
    nodeToBinary(visitor, node->castExpr.expr);
}

static void visitIndexExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    nodeToBinary(visitor, node->indexExpr.target);
    nodeToBinary(visitor, node->indexExpr.index);
}

static void visitGenericParam(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    packString(&ctx->packer, node->genericParam.name);
    nodeToBinary(visitor, node->genericParam.constraints);
}

static void visitGenericDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    nodeToBinary(visitor, node->genericDecl.decl);
    manyNodesToBinary(visitor, node->genericDecl.params);
}

static void visitPathElement(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    manyNodesToBinary(visitor, node->pathElement.args);

    packString(&ctx->packer, node->pathElement.name);
    packString(&ctx->packer, node->pathElement.alt);
    packString(&ctx->packer, node->pathElement.alt2);
    msgpack_pack_uint64(&ctx->packer, node->pathElement.index);
}

static void visitPath(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    manyNodesToBinary(visitor, node->path.elements);
    msgpack_pack_uint8(&ctx->packer, node->path.isType);
}

static void visitFuncDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    msgpack_pack_uint32(&ctx->packer, node->funcDecl.operatorOverload);
    msgpack_pack_uint32(&ctx->packer, node->funcDecl.index);
    packString(&ctx->packer, node->funcDecl.name);
    manyNodesToBinary(visitor, node->funcDecl.params);
    nodeToBinary(visitor, node->funcDecl.ret);
    nodeToBinary(visitor, node->funcDecl.body);
}

static void visitMacroDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    packString(&ctx->packer, node->macroDecl.name);
    manyNodesToBinary(visitor, node->macroDecl.params);
    nodeToBinary(visitor, node->macroDecl.ret);
    nodeToBinary(visitor, node->macroDecl.body);
}

static void visitFuncParam(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    packString(&ctx->packer, node->funcParam.name);

    nodeToBinary(visitor, node->funcParam.type);
    nodeToBinary(visitor, node->funcParam.def);
}

static void visitVarDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    manyNodesToBinary(visitor, node->varDecl.names);
    nodeToBinary(visitor, node->varDecl.type);
    nodeToBinary(visitor, node->varDecl.init);
}

static void visitTypeDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    packString(&ctx->packer, node->typeDecl.name);
    nodeToBinary(visitor, node->typeDecl.aliased);
}

static void visitUnionDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    packString(&ctx->packer, node->unionDecl.name);
    manyNodesToBinary(visitor, node->unionDecl.members);
}

static void visitEnumOption(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    packString(&ctx->packer, node->enumOption.name);
    nodeToBinary(visitor, node->enumOption.value);
}

static void visitEnumDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    packString(&ctx->packer, node->enumDecl.name);
    nodeToBinary(visitor, node->enumDecl.base);
    manyNodesToBinary(visitor, node->enumDecl.options);
}

static void visitStructField(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    packString(&ctx->packer, node->structField.name);
    nodeToBinary(visitor, node->structField.type);
    nodeToBinary(visitor, node->structField.value);
}

static void visitStructDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    packString(&ctx->packer, node->structDecl.name);
    nodeToBinary(visitor, node->structDecl.base);
    manyNodesToBinary(visitor, node->structDecl.members);
}

static void visitBinaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    msgpack_pack_uint8(&ctx->packer, node->binaryExpr.op);

    nodeToBinary(visitor, node->binaryExpr.lhs);
    nodeToBinary(visitor, node->binaryExpr.lhs);
}

static void visitUnaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    msgpack_pack_uint8(&ctx->packer, node->unaryExpr.isPrefix);
    msgpack_pack_uint8(&ctx->packer, node->unaryExpr.op);

    nodeToBinary(visitor, node->unaryExpr.operand);
}

static void visitTernaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    nodeToBinary(visitor, node->ternaryExpr.cond);
    nodeToBinary(visitor, node->ternaryExpr.body);
    nodeToBinary(visitor, node->ternaryExpr.otherwise);
}

static void visitStmtExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    nodeToBinary(visitor, node->stmtExpr.stmt);
}

static void visitTypedExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    nodeToBinary(visitor, node->typedExpr.type);
    nodeToBinary(visitor, node->typedExpr.expr);
}

static void visitCallExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    msgpack_pack_uint32(&ctx->packer, node->callExpr.overload);
    nodeToBinary(visitor, node->callExpr.callee);
    manyNodesToBinary(visitor, node->callExpr.args);
}

static void visitClosureExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    nodeToBinary(visitor, node->closureExpr.ret);
    manyNodesToBinary(visitor, node->closureExpr.params);
    nodeToBinary(visitor, node->closureExpr.body);
}

static void visitFieldExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    packString(&ctx->packer, node->fieldExpr.name);
    nodeToBinary(visitor, node->fieldExpr.value);
}

static void visitStructExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    nodeToBinary(visitor, node->structExpr.left);
    manyNodesToBinary(visitor, node->structExpr.fields);
}

static void visitExpressionStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    nodeToBinary(visitor, node->exprStmt.expr);
}

static void visitContinueStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);
}

static void visitReturnStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    nodeToBinary(visitor, node->returnStmt.expr);
}

static void visitBlockStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    manyNodesToBinary(visitor, node->blockStmt.stmts);
}

static void visitForStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    nodeToBinary(visitor, node->forStmt.var);
    nodeToBinary(visitor, node->forStmt.range);
    nodeToBinary(visitor, node->forStmt.body);
}

static void visitWhileStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    nodeToBinary(visitor, node->whileStmt.cond);
    nodeToBinary(visitor, node->whileStmt.body);
}

static void visitSwitchStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    nodeToBinary(visitor, node->switchStmt.cond);
    manyNodesToBinary(visitor, node->switchStmt.cases);
}

static void visitCaseStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);

    nodeToBinary(visitor, node->caseStmt.match);
    manyNodesToBinary(visitor, node->caseStmt.body);
}

static void visitFallback(ConstAstVisitor *visitor, const AstNode *node)
{
    AstNodeUnpackContext *ctx = getConstAstVisitorContext(visitor);
    nodePackHeader(visitor, node);
    msgpack_pack_nil(&ctx->packer);
}

bool binaryEncodeAstNode(struct msgpack_sbuffer *sbuf,
                         MemPool *pool,
                         const AstNode *node)
{
    msgpack_sbuffer_init(sbuf);
    AstNodeUnpackContext ctx = {.buffer = sbuf};
    msgpack_packer_init(&ctx.packer, sbuf, msgpack_sbuffer_write);
    packString(&ctx.packer, node->loc.fileName);

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

    return true;
}