//
// Created by Carter Mbotho on 2024-11-01.
//

#include <core/alloc.h>

#include <driver/driver.h>
#include <lang/frontend/ttable.h>
#include <lang/frontend/visitor.h>

#include "context.h"

#include "lang/frontend/strings.h"
#include "node.h"

typedef struct AstToMirContext {
    MirContext *mir;
    MirNode *ret;
    MirNodeList globals;

    union {
        struct {
            MirNode *func;
            MirNode *returnVar;
            MirNode *exitBasicBlock;
            MirNode *currentBasicBlock;
            MirNode *loopUpdateBasicBlock;
            MirNode *endBasicBlock;
            MirNodeList basicBlocks;
            HashTable uniqueNames;
        };
        struct {
            MirNode *func;
            MirNode *returnVar;
            MirNode *exitBasicBlock;
            MirNode *currentBasicBlock;
            MirNode *loopUpdateBasicBlock;
            MirNode *endBasicBlock;
            MirNodeList basicBlocks;
            HashTable uniqueNames;
        } mirFnCtx;
    };
} AstToMirContext;

typedef typeof((AstToMirContext){}.mirFnCtx) MirFunctionContext;
typedef struct {
    cstring prefix;
    u64 idx;
} Label;

static bool stringEqual(const void *lhs, const void *rhs)
{
    return compareStrings(((Label *)lhs)->prefix, ((Label *)rhs)->prefix) == 0;
}

static inline cstring makeUniqueString(AstToMirContext *ctx, cstring prefix)
{
    Label elem = {.prefix = prefix};
    u64 idx = 0;
    HashCode hash = hashStr(hashInit(), prefix);
    Label *found = findInHashTable(
        &ctx->uniqueNames, &elem, hash, sizeof(elem), stringEqual);
    if (found) {
        idx = found->idx++;
    }
    else {
        elem.idx = 1;
        insertInHashTable(
            &ctx->uniqueNames, &elem, hash, sizeof(elem), stringEqual);
    }
    if (idx)
        return makeStringf(ctx->mir->strings, "%s%d", prefix, idx);
    else
        return makeString(ctx->mir->strings, prefix);
}

const Type *typeGetElement(MirContext *ctx, const Type *type)
{
    type = resolveUnThisUnwrapType(type);
    if (typeIs(type, Array))
        return resolveUnThisUnwrapType(type->array.elementType);
    else if (typeIs(type, Pointer))
        return resolveUnThisUnwrapType(type->pointer.pointed);
    else if (typeIs(type, String))
        return ctx->types->primitiveTypes[prtCChar];
    unreachable("Unknown type");
}

const Type *typeReturnType(const Type *type)
{
    csAssert0(typeIs(type, Func));
    return resolveUnThisUnwrapType(type->func.retType);
}

const Type *typeGetMember(const Type *type, u64 index)
{
    type = resolveUnThisUnwrapType(type);
    type = stripAll(type);
    TypeMembersContainer *members = NULL;
    switch (type->tag) {
    case typTuple:
        csAssert0(index < type->tuple.count);
        return resolveUnThisUnwrapType(type->tuple.members[index]);
    case typUnion:
        csAssert0(index < type->tUnion.count);
        return resolveUnThisUnwrapType(type->tUnion.members[index].type);
    case typStruct:
        members = type->tStruct.members;
        break;
    case typClass:
        members = type->tClass.members;
        break;
    case typUntaggedUnion:
        members = type->untaggedUnion.members;
        break;
    default:
        csAssert0(false);
    }

    csAssert0(index < members->count);
    return resolveUnThisUnwrapType(members->members[index].type);
}

static MirNode *makeLocalVar(AstToMirContext *ctx,
                             const FileLoc *loc,
                             cstring name,
                             const Type *type)
{
    csAssert0(ctx->currentBasicBlock);
    return mirNodeListInsert(&ctx->currentBasicBlock->BasicBlock.nodes,
                             makeMirAllocaOp(ctx->mir, loc, name, type, NULL));
}

static inline MirNode *makeLocalTempVar(AstToMirContext *ctx,
                                        const FileLoc *loc,
                                        const Type *type)
{
    csAssert0(ctx->currentBasicBlock);
    return mirNodeListInsert(
        &ctx->currentBasicBlock->BasicBlock.nodes,
        makeMirAllocaOp(
            ctx->mir, loc, makeUniqueString(ctx, "tmp"), type, NULL));
}

static inline MirNode *insertInCurrentBlock(AstToMirContext *ctx, MirNode *node)
{
    csAssert0(ctx->currentBasicBlock);
    return mirNodeListInsert(&ctx->currentBasicBlock->BasicBlock.nodes, node);
}

static inline MirNode *insertGlobalVariable(AstToMirContext *ctx, MirNode *node)
{
    csAssert0(mirIs(node, GlobalVariable));
    return mirNodeListInsert(&ctx->globals, node);
}

static inline MirNode *insertBasicBlock(AstToMirContext *ctx, MirNode *bb)
{
    return mirNodeListInsert(&ctx->basicBlocks, bb);
}

static bool isMirBasicBlockTerminated(const MirNode *node)
{
    MirNode *last = node->BasicBlock.nodes.last;
    if (mirIs(last, JumpOp) || mirIs(last, IfOp) || mirIs(last, PanicOp) ||
        mirIs(last, ReturnOp))
        return true;
    return false;
}

#define astToMirRet(MIR)                                                       \
    ({                                                                         \
        MirNode *LINE_VAR(_mir) = MIR;                                         \
        ctx->ret = LINE_VAR(_mir);                                             \
        node->mir = LINE_VAR(_mir);                                            \
    })

static MirNode *convertAstToMir(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    MirNode *mir = NULL;
    ctx->ret = NULL;
    if (node) {
        astVisit(visitor, node);
        mir = ctx->ret;
    }
    ctx->ret = NULL;
    return mir;
}

static u64 getStructMemberInfo(const AstNode *member, u64 *flags)
{
    csAssert0(nodeIs(member, Identifier));
    member = member->ident.resolvesTo;
    if (isReferenceType(member->type))
        *flags |= mifReference;
    else if (isPointerType(member->type))
        *flags |= mifPointer;

    if (nodeIs(member, FieldDecl))
        return member->structField.index;
    else if (nodeIs(member, FieldExpr))
        return member->fieldExpr.index;
    unreachable("")
}

static void visitNullLit(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    astToMirRet(makeMirNullConst(ctx->mir, &node->loc, node->type, NULL));
}

static void visitBoolLit(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    astToMirRet(
        makeMirBoolConst(ctx->mir, &node->loc, node->boolLiteral.value, NULL));
}

static void visitCharLit(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    if (node->type->primitive.id == prtChar)
        astToMirRet(makeMirCharConst(
            ctx->mir, &node->loc, (char)node->charLiteral.value, NULL));
    else
        astToMirRet(
            makeMirUnsignedConst(ctx->mir,
                                 &node->loc,
                                 node->charLiteral.value,
                                 ctx->mir->types->primitiveTypes[prtU32],
                                 NULL));
}

static void visitIntegerLit(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    if (isSignedType(node->type)) {
        astToMirRet(makeMirSignedConst(
            ctx->mir, &node->loc, node->intLiteral.value, node->type, NULL));
    }
    else {
        astToMirRet(makeMirUnsignedConst(
            ctx->mir, &node->loc, node->intLiteral.uValue, node->type, NULL));
    }
}

static void visitFloatLit(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    astToMirRet(makeMirFloatConst(
        ctx->mir, &node->loc, node->floatLiteral.value, node->type, NULL));
}

static void visitStringLit(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    astToMirRet(makeMirStringConst(
        ctx->mir, &node->loc, node->stringLiteral.value, NULL));
}

static void visitIdentifier(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    csAssert0(node->ident.resolvesTo);
    AstNode *resolved = node->ident.resolvesTo;
    csAssert0(resolved->mir);

    astToMirRet(withMirFlags(
        makeMirLoadOp(
            ctx->mir, &node->loc, node->ident.value, resolved->mir, NULL),
        resolved->mir->flags));
}

static void visitArrayExpr(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    AstNode *elem = node->arrayExpr.elements;
    MirNodeList elems = {};
    u64 i = 0;
    for (; elem; elem = elem->next, i++) {
        mirNodeListInsert(&elems, convertAstToMir(visitor, elem));
    }

    if (node->arrayExpr.isLiteral) {
        astToMirRet(makeMirArrayConst(
            ctx->mir, &node->loc, elems.first, i, node->type, NULL));
    }
    else {
        astToMirRet(makeMirArrayInitOp(
            ctx->mir, &node->loc, elems.first, i, node->type, NULL));
    }
}

static void visitTupleExpr(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    AstNode *elem = node->tupleExpr.elements;
    MirNodeList elems = {};
    u64 i = 0;
    for (; elem; elem = elem->next, i++) {
        mirNodeListInsert(&elems, convertAstToMir(visitor, elem));
    }

    if (node->tupleExpr.isLiteral) {
        astToMirRet(makeMirStructConst(
            ctx->mir, &node->loc, elems.first, i, node->type, NULL));
    }
    else {
        astToMirRet(makeMirStructInitOp(
            ctx->mir, &node->loc, elems.first, i, node->type, NULL));
    }
}

static void visitFieldExpr(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    astToMirRet(convertAstToMir(visitor, node->fieldExpr.value));
}

static void visitStructExpr(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    AstNode *elem = node->structExpr.fields;
    MirNodeList elems = {};
    u64 i = 0;
    for (; elem; elem = elem->next, i++) {
        mirNodeListInsert(&elems, convertAstToMir(visitor, elem));
    }

    if (node->structExpr.isLiteral) {
        astToMirRet(makeMirStructConst(
            ctx->mir, &node->loc, elems.first, i, node->type, NULL));
    }
    else {
        astToMirRet(makeMirStructInitOp(
            ctx->mir, &node->loc, elems.first, i, node->type, NULL));
    }
}

static void visitUnaryExpr(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    Operator op = node->unaryExpr.op;
    AstNode *operand = node->unaryExpr.operand;

    switch (op) {
    case opMinus:
    case opPlus:
    case opNot:
    case opCompl:
        astToMirRet(
            makeMirUnaryOp(ctx->mir,
                           &node->loc,
                           node->unaryExpr.op,
                           convertAstToMir(visitor, node->unaryExpr.operand),
                           NULL));
        break;
    case opPreInc:
    case opPreDec: {
        // ++a => a = a + 1; a
        MirNode *mirOperand = convertAstToMir(visitor, operand);
        insertInCurrentBlock(
            ctx,
            makeMirAssignOp(
                ctx->mir,
                &operand->loc,
                mirOperand,
                makeMirBinaryOp(
                    ctx->mir,
                    &node->loc,
                    cloneMirNode(ctx->mir, mirOperand),
                    op == opPreDec ? opSub : opAdd,
                    makeMirSignedConst(
                        ctx->mir, &node->loc, 1, operand->type, NULL),
                    operand->type,
                    NULL),
                NULL));
        astToMirRet(cloneMirNode(ctx->mir, mirOperand));
        break;
    }
    case opPostInc:
    case opPostDec: {
        // a++ => tmp = a; a = a+1
        MirNode *tmp = makeLocalTempVar(ctx, &operand->loc, operand->type);
        MirNode *mirOperand = convertAstToMir(visitor, operand);
        insertInCurrentBlock(
            ctx,
            makeMirAssignOp(
                ctx->mir,
                &node->loc,
                makeMirLoadOp(
                    ctx->mir, &operand->loc, tmp->AllocaOp.name, tmp, NULL),
                mirOperand,
                NULL));
        // a =  a + 1
        insertInCurrentBlock(
            ctx,
            makeMirAssignOp(
                ctx->mir,
                &operand->loc,
                cloneMirNode(ctx->mir, mirOperand),
                makeMirBinaryOp(
                    ctx->mir,
                    &node->loc,
                    cloneMirNode(ctx->mir, mirOperand),
                    op == opPreDec ? opSub : opAdd,
                    makeMirSignedConst(
                        ctx->mir, &node->loc, 1, operand->type, NULL),
                    operand->type,
                    NULL),
                NULL));
        astToMirRet(
            makeMirLoadOp(ctx->mir, &tmp->loc, tmp->AllocaOp.name, tmp, NULL));
        break;
    }
    case opMove:
        astToMirRet(makeMirMoveOp(
            ctx->mir, &node->loc, convertAstToMir(visitor, operand), NULL));
        break;
    default:
        break;
    }
}

static void visitBinaryExpr(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    Operator op = node->binaryExpr.op;
    AstNode *lhs = node->binaryExpr.lhs, *rhs = node->binaryExpr.rhs;
    astToMirRet(makeMirBinaryOp(ctx->mir,
                                &node->loc,
                                convertAstToMir(visitor, lhs),
                                op,
                                convertAstToMir(visitor, rhs),
                                node->type,
                                NULL));
}

static void visitAssignExpr(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    Operator op = node->assignExpr.op;
    AstNode *lhs = node->assignExpr.lhs, *rhs = node->assignExpr.rhs;
    MirNode *mLhs = convertAstToMir(visitor, lhs),
            *mRhs = convertAstToMir(visitor, rhs);
    withMirFlags(mLhs, mRhs->flags);

    if (op != opAssign) {
        mRhs = makeMirBinaryOp(
            ctx->mir, &node->loc, mLhs, op, mRhs, node->type, NULL);
    }
    astToMirRet(withMirFlags(
        makeMirAssignOp(ctx->mir, &node->loc, mLhs, mRhs, NULL), mLhs->flags));
}

static void visitTernaryExpr(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    AstNode *ifTrue = node->ternaryExpr.body,
            *ifFalse = node->ternaryExpr.otherwise;
    MirNode *tmp = makeLocalTempVar(ctx, &node->loc, node->type);
    cstring name = makeUniqueString(ctx, "if");
    MirNode *phi =
        makeMirBasicBlock(ctx->mir,
                          &node->ternaryExpr.body->loc,
                          makeStringf(ctx->mir->strings, "%s.end", name),
                          NULL);
    // if then
    MirNode *bbTrue =
        makeMirBasicBlock(ctx->mir,
                          &node->ternaryExpr.body->loc,
                          makeStringf(ctx->mir->strings, "%s.then", name),
                          NULL);
    // if otherwise
    MirNode *bbFalse =
        makeMirBasicBlock(ctx->mir,
                          &node->ternaryExpr.body->loc,
                          makeStringf(ctx->mir->strings, "%s.otherwise", name),
                          NULL);

    insertInCurrentBlock(
        ctx,
        makeMirIfOp(ctx->mir,
                    &node->loc,
                    convertAstToMir(visitor, node->ternaryExpr.cond),
                    bbTrue,
                    bbFalse,
                    NULL));

    insertBasicBlock(ctx, bbTrue);
    ctx->currentBasicBlock = bbTrue;
    MirNode *then = convertAstToMir(visitor, ifTrue);
    withMirFlags(tmp, then->flags);

    insertInCurrentBlock(
        ctx,
        makeMirAssignOp(
            ctx->mir,
            &ifTrue->loc,
            makeMirLoadOp(
                ctx->mir, &ifTrue->loc, tmp->AllocaOp.name, tmp, NULL),
            then,
            makeMirJumpOp(ctx->mir, &ifTrue->loc, phi, NULL)));

    insertBasicBlock(ctx, bbFalse);
    ctx->currentBasicBlock = bbFalse;
    insertInCurrentBlock(
        ctx,
        makeMirAssignOp(
            ctx->mir,
            &ifFalse->loc,
            makeMirLoadOp(
                ctx->mir, &ifFalse->loc, tmp->AllocaOp.name, tmp, NULL),
            convertAstToMir(visitor, ifFalse),
            makeMirJumpOp(ctx->mir, &ifFalse->loc, phi, NULL)));

    insertBasicBlock(ctx, phi);
    ctx->currentBasicBlock = phi;
    astToMirRet(
        makeMirLoadOp(ctx->mir, &tmp->loc, tmp->AllocaOp.name, tmp, NULL));
}

static void visitCastExpr(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    AstNode *from = node->castExpr.expr, *to = node->castExpr.to;
    MirNode *expr = convertAstToMir(visitor, from);
    csAssert0(expr);
    if (expr->type == to->type) {
        astToMirRet(expr);
        return;
    }

    u64 flags = hasFlag(to, Const) ? mifConst : mifNone;
    if (isReferenceType(to->type))
        flags |= mifReference;
    else if (isPointerType(to->type))
        flags |= mifPointer;

    if (hasFlag(node, UnionCast) || typeIs(from->type, Union)) {
        astToMirRet(withMirFlags(
            makeMirMemberOp(
                ctx->mir,
                &node->loc,
                makeMirMemberOp(ctx->mir, &node->loc, expr, 1, NULL),
                node->castExpr.idx,
                NULL),
            flags));
    }
    else {
        astToMirRet(withMirFlags(
            makeMirCastOp(ctx->mir,
                          &node->loc,
                          expr,
                          makeMirTypeInfo(ctx->mir, &to->loc, to->type, NULL),
                          NULL),
            flags));
    }
}

static void visitTypedExpr(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    AstNode *from = node->castExpr.expr, *to = node->castExpr.to;
    MirNode *expr = convertAstToMir(visitor, from);
    csAssert0(expr);
    if (expr->type == to->type) {
        astToMirRet(expr);
        return;
    }

    u64 flags = hasFlag(to, Const) ? mifConst : mifNone;
    if (isReferenceType(to->type))
        flags |= mifReference;
    else if (isPointerType(to->type))
        flags |= mifPointer;
    astToMirRet(withMirFlags(
        makeMirCastOp(ctx->mir,
                      &node->loc,
                      expr,
                      makeMirTypeInfo(ctx->mir, &to->loc, to->type, NULL),
                      NULL),
        flags));
}

static void visitGroupExpr(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    astToMirRet(convertAstToMir(visitor, node->groupExpr.expr));
}

static void visitStmtExpr(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    astToMirRet(convertAstToMir(visitor, node->stmtExpr.stmt));
}

static void visitIndexExpr(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    AstNode *target = node->indexExpr.target, *index = node->indexExpr.index;
    u64 flags = hasFlag(node, Const) ? mifConst : mifNone;
    if (isReferenceType(target->type))
        flags |= mifReference;
    else if (isPointerType(target->type))
        flags |= mifPointer;

    astToMirRet(withMirFlags(makeMirIndexOp(ctx->mir,
                                            &node->loc,
                                            convertAstToMir(visitor, target),
                                            convertAstToMir(visitor, index),
                                            NULL),
                             flags));
}

static void visitPointerOfExpr(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    u64 flags = hasFlag(node, Const) ? mifConst : mifNone;
    astToMirRet(withMirFlags(
        makeMirPointerOfOp(ctx->mir,
                           &node->loc,
                           convertAstToMir(visitor, node->unaryExpr.operand),
                           node->type,
                           NULL),
        flags));
}

static void visitReferenceOfExpr(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    u64 flags = hasFlag(node, Const) ? mifConst : mifNone;
    astToMirRet(withMirFlags(
        makeMirReferenceOfOp(ctx->mir,
                             &node->loc,
                             convertAstToMir(visitor, node->unaryExpr.operand),
                             node->type,
                             NULL),
        flags));
}

static void visitMemberExpr(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    AstNode *target = node->memberExpr.target,
            *member = node->memberExpr.member;
    if (nodeIsMemberFunctionReference(node)) {
        AstNode *decl = member->ident.resolvesTo;
        csAssert0(decl && decl->mir);
        astToMirRet(makeMirLoadOp(
            ctx->mir, &node->loc, decl->mir->Function.name, decl->mir, NULL));
    }
    else if (nodeIsEnumOptionReference(node)) {
        member = member->ident.resolvesTo;
        csAssert0(nodeIs(member, EnumOptionDecl));
        const Type *base = member->type->tEnum.base;
        if (isUnsignedType(base))
            astToMirRet(makeMirUnsignedConst(
                ctx->mir,
                &node->loc,
                member->enumOption.value->intLiteral.uValue,
                base,
                NULL));
        else
            astToMirRet(
                makeMirSignedConst(ctx->mir,
                                   &node->loc,
                                   member->enumOption.value->intLiteral.value,
                                   base,
                                   NULL));
    }
    else if (typeIs(target->type, Module)) {
        csAssert0(nodeIs(member, Identifier));
        u64 flags = hasFlag(member, Const) ? mifConst : mifNone;
        AstNode *decl = member->ident.resolvesTo;
        csAssert0(decl && mirIs(decl->mir, GlobalVariable));
        astToMirRet(withMirFlags(makeMirLoadOp(ctx->mir,
                                               &node->loc,
                                               decl->mir->GlobalVariable.name,
                                               decl->mir,
                                               NULL),
                                 flags));
    }
    else {
        u64 flags = hasFlag(member, Const) ? mifConst : mifNone;
        u64 index = nodeIs(member, IntegerLit)
                        ? member->intLiteral.uValue
                        : getStructMemberInfo(member, &flags);

        astToMirRet(makeMirMemberOp(ctx->mir,
                                    &node->loc,
                                    convertAstToMir(visitor, target),
                                    index,
                                    NULL));
    }
}

static void visitCallExpr(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    MirNodeList args = {};

    MirNode *callee = convertAstToMir(visitor, node->callExpr.callee);
    AstNode *arg = node->callExpr.args;
    for (; arg; arg = arg->next) {
        mirNodeListInsert(&args, convertAstToMir(visitor, arg));
    }
    const Type *ret = node->callExpr.callee->type->func.retType;
    u64 flags = mifNone;
    if (!typeIs(ret, Void)) {
        flags |= hasFlag(node, Const) ? mifConst : mifNone;
        if (isReferenceType(ret))
            flags |= mifReference;
        else if (isPointerType(ret))
            flags |= mifPointer;
    }

    astToMirRet(withMirFlags(
        makeMirCallOp(ctx->mir, &node->loc, callee, args.first, NULL, NULL),
        flags));
}

static void visitUnionValue(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    const Type *type = makeStructType(
        ctx->mir->types,
        NULL,
        (NamedTypeMember[]){{.name = makeString(ctx->mir->strings, "tag"),
                             .type = ctx->mir->types->primitiveTypes[prtU32]},
                            {.name = makeString(ctx->mir->strings, "value"),
                             .type = node->type}},
        2,
        NULL,
        flgNone);
    MirNode *tmp = makeLocalTempVar(ctx, &node->unionValue.value->loc, type);
    // tmp.0 = tag
    insertInCurrentBlock(
        ctx,
        makeMirAssignOp( // tmp.0 = tag
            ctx->mir,
            &node->loc,
            makeMirMemberOp(
                ctx->mir,
                &node->loc,
                makeMirLoadOp(
                    ctx->mir, &node->loc, tmp->AllocaOp.name, tmp, NULL),
                0,
                NULL),
            makeMirUnsignedConst(ctx->mir,
                                 &node->loc,
                                 node->unionValue.idx,
                                 ctx->mir->types->primitiveTypes[prtU64],
                                 NULL),
            makeMirAssignOp( // tmp.1.{idx} = value
                ctx->mir,
                &node->loc,
                makeMirMemberOp( // tmp.1.{idx}
                    ctx->mir,
                    &node->loc,
                    makeMirMemberOp( // tmp.1
                        ctx->mir,
                        &node->loc,
                        makeMirLoadOp(ctx->mir,
                                      &node->loc,
                                      tmp->AllocaOp.name,
                                      tmp,
                                      NULL),
                        1,
                        NULL),
                    node->unionValue.idx,
                    NULL),
                convertAstToMir(visitor, node->unionValue.value),
                NULL)));
    astToMirRet(
        makeMirLoadOp(ctx->mir, &tmp->loc, tmp->AllocaOp.name, tmp, NULL));
}

static void visitBackendCallExpr(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    switch (node->backendCallExpr.func) {
    case bfiAlloca:
        astToMirRet(makeLocalTempVar(
            ctx, &node->loc, node->backendCallExpr.args->type));
        break;
    case bfiSizeOf:
        astToMirRet(makeMirSizeofOp(
            ctx->mir, &node->loc, node->backendCallExpr.args->type, NULL));
        break;
    case bfiZeromem:
        astToMirRet(makeMirZeromemOp(
            ctx->mir,
            &node->loc,
            convertAstToMir(visitor, node->backendCallExpr.args),
            NULL));
        break;
    }
}

static void visitAsmOperand(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    astToMirRet(
        makeMirAsmOperand(ctx->mir,
                          &node->loc,
                          node->asmOperand.constraint,
                          convertAstToMir(visitor, node->asmOperand.operand),
                          NULL));
}

static void visitInlineAssembly(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    MirNodeList outputs = {}, inputs = {}, clobbers = {};
    AstNode *output = node->inlineAssembly.outputs,
            *input = node->inlineAssembly.inputs,
            *clobber = node->inlineAssembly.clobbers;

    for (; output; output = output->next) {
        mirNodeListInsert(&outputs, convertAstToMir(visitor, output));
    }

    for (; input; input = input->next) {
        mirNodeListInsert(&inputs, convertAstToMir(visitor, input));
    }

    for (; clobber; clobber = clobber->next) {
        mirNodeListInsert(&clobbers, convertAstToMir(visitor, clobber));
    }
    astToMirRet(
        insertInCurrentBlock(ctx,
                             makeMirInlineAssembly(ctx->mir,
                                                   &node->loc,
                                                   node->inlineAssembly.text,
                                                   outputs.first,
                                                   inputs.first,
                                                   clobbers.first,
                                                   node->type,
                                                   NULL)));
}

static void visitReturnStmt(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->returnStmt.expr;
    if (expr) {
        csAssert0(ctx->returnVar);
        insertInCurrentBlock(
            ctx,
            makeMirAssignOp(ctx->mir,
                            &node->loc,
                            makeMirLoadOp(ctx->mir,
                                          &expr->loc,
                                          ctx->returnVar->AllocaOp.name,
                                          ctx->returnVar,
                                          NULL),
                            convertAstToMir(visitor, expr),
                            NULL));
    }
    astToMirRet(insertInCurrentBlock(
        ctx, makeMirJumpOp(ctx->mir, &node->loc, ctx->exitBasicBlock, NULL)));
}

static void visitBlockStmt(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    AstNode *stmt = node->blockStmt.stmts;
    MirNode *last = NULL;
    for (; stmt; stmt = stmt->next) {
        if (!hasFlag(node, BlockReturns) || stmt->next)
            last = convertAstToMir(visitor, stmt);
        else
            last = convertAstToMir(visitor, stmt->exprStmt.expr);
    }
    astToMirRet(last);
}

static void visitExprStmt(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    AstNode *expr = node->exprStmt.expr;
    MirNode *exprMir = convertAstToMir(visitor, node->exprStmt.expr);
    if (nodeIs(expr, BlockStmt) || nodeIs(expr, ExprStmt))
        astToMirRet(exprMir);
    else
        astToMirRet(insertInCurrentBlock(ctx, exprMir));
}

static void visitIfStmt(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    AstNode *ifTrue = node->ternaryExpr.body,
            *ifFalse = node->ternaryExpr.otherwise;
    cstring name = makeUniqueString(ctx, "if");
    MirNode *phi =
        makeMirBasicBlock(ctx->mir,
                          &node->ternaryExpr.body->loc,
                          makeStringf(ctx->mir->strings, "%s.end", name),
                          NULL);

    MirNode *bbTrue =
        makeMirBasicBlock(ctx->mir,
                          &node->ternaryExpr.body->loc,
                          makeStringf(ctx->mir->strings, "%s.then", name),
                          NULL);
    MirNode *bbFalse =
        ifFalse ? makeMirBasicBlock(
                      ctx->mir,
                      &node->ternaryExpr.body->loc,
                      makeStringf(ctx->mir->strings, "%s.otherwise", name),
                      NULL)
                : phi;
    MirNode *cond = convertAstToMir(visitor, node->ifStmt.cond);
    MirNode *ifMir = insertInCurrentBlock(
        ctx, makeMirIfOp(ctx->mir, &node->loc, cond, bbTrue, bbFalse, NULL));

    insertBasicBlock(ctx, bbTrue);
    ctx->currentBasicBlock = bbTrue;
    astVisit(visitor, ifTrue);
    if (!isMirBasicBlockTerminated(ctx->currentBasicBlock))
        insertInCurrentBlock(ctx,
                             makeMirJumpOp(ctx->mir, &ifTrue->loc, phi, NULL));
    if (ifFalse) {
        insertBasicBlock(ctx, bbFalse);
        ctx->currentBasicBlock = bbFalse;
        astVisit(visitor, ifFalse);
        if (!isMirBasicBlockTerminated(ctx->currentBasicBlock))
            insertInCurrentBlock(
                ctx, makeMirJumpOp(ctx->mir, &ifFalse->loc, phi, NULL));
    }
    insertBasicBlock(ctx, phi);
    ctx->currentBasicBlock = phi;
    astToMirRet(ifMir);
}

static void visitWhileStmt(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    AstNode *cond = node->whileStmt.cond, *body = node->whileStmt.body,
            *update = node->whileStmt.update;
    cstring name = makeUniqueString(ctx, "while");
    MirNode *condBb =
        makeMirBasicBlock(ctx->mir,
                          &cond->loc,
                          makeStringf(ctx->mir->strings, "%s.cond", name),
                          NULL);
    MirNode *updateBb =
        update ? makeMirBasicBlock(
                     ctx->mir,
                     &update->loc,
                     makeStringf(ctx->mir->strings, "%s.update", name),
                     NULL)
               : condBb;
    MirNode *bodyBb =
        makeMirBasicBlock(ctx->mir,
                          &body->loc,
                          makeStringf(ctx->mir->strings, "%s.body", name),
                          NULL);
    MirNode *endBb =
        makeMirBasicBlock(ctx->mir,
                          &body->loc,
                          makeStringf(ctx->mir->strings, "%s.end", name),
                          NULL);

    MirNode *currentLoopUpdateBb = ctx->loopUpdateBasicBlock,
            *currentEndBasicBlock = ctx->endBasicBlock;
    ctx->loopUpdateBasicBlock = updateBb;
    ctx->endBasicBlock = endBb;

    // jump into cond bb
    insertInCurrentBlock(ctx,
                         makeMirJumpOp(ctx->mir, &cond->loc, condBb, NULL));

    insertBasicBlock(ctx, condBb);
    ctx->currentBasicBlock = condBb;
    insertInCurrentBlock(ctx,
                         makeMirIfOp(ctx->mir,
                                     &cond->loc,
                                     convertAstToMir(visitor, cond),
                                     bodyBb,
                                     endBb,
                                     NULL));
    // Body
    insertBasicBlock(ctx, bodyBb);
    ctx->currentBasicBlock = bodyBb;
    astVisit(visitor, body);

    if (update) {
        if (!isMirBasicBlockTerminated(ctx->currentBasicBlock))
            insertInCurrentBlock(
                ctx, makeMirJumpOp(ctx->mir, &body->loc, updateBb, NULL));
        // update
        insertBasicBlock(ctx, updateBb);
        ctx->currentBasicBlock = updateBb;
        astVisit(visitor, update);
    }

    if (!isMirBasicBlockTerminated(ctx->currentBasicBlock)) {
        insertInCurrentBlock(ctx,
                             makeMirJumpOp(ctx->mir, &body->loc, condBb, NULL));
    }

    insertBasicBlock(ctx, endBb);
    ctx->currentBasicBlock = endBb;

    ctx->loopUpdateBasicBlock = currentLoopUpdateBb;
    ctx->endBasicBlock = currentEndBasicBlock;
}

static void visitSwitchStmt(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    MirNodeList cases = {};
    MirNode *cond = convertAstToMir(visitor, node->switchStmt.cond);
    cstring name = makeUniqueString(ctx, "switch");
    MirNode *endSwitch =
        makeMirBasicBlock(ctx->mir,
                          &node->ternaryExpr.body->loc,
                          makeStringf(ctx->mir->strings, "%s.cond", name),
                          NULL);

    MirNode *currentEndBb = ctx->endBasicBlock;
    ctx->endBasicBlock = endSwitch;

    MirNode *switchMir = insertInCurrentBlock(
        ctx, makeMirSwitchOp(ctx->mir, &node->loc, cond, NULL, NULL));
    for (AstNode *it = node->switchStmt.cases; it; it = it->next) {
        MirNode *bb = makeMirBasicBlock(
            ctx->mir,
            &node->ternaryExpr.body->loc,
            makeStringf(
                ctx->mir->strings, "%s.case%llu", name, it->caseStmt.idx),
            NULL);
        insertBasicBlock(ctx, bb);
        ctx->currentBasicBlock = bb;
        astVisit(visitor, it->caseStmt.body);
        if (!isMirBasicBlockTerminated(bb))
            insertInCurrentBlock(
                ctx, makeMirJumpOp(ctx->mir, &it->loc, endSwitch, NULL));

        if (it->caseStmt.match)
            mirNodeListInsert(
                &cases,
                makeMirCaseOp(ctx->mir,
                              &it->loc,
                              convertAstToMir(visitor, it->caseStmt.match),
                              bb,
                              NULL));
        else
            switchMir->SwitchOp.def =
                makeMirCaseOp(ctx->mir,
                              &it->loc,
                              convertAstToMir(visitor, it->caseStmt.match),
                              bb,
                              NULL);
    }
    // patch the switch op
    switchMir->SwitchOp.cases = cases.first;
    ctx->currentBasicBlock = endSwitch;

    ctx->endBasicBlock = currentEndBb;

    astToMirRet(switchMir);
}

static void visitBreakStmt(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    csAssert0(ctx->endBasicBlock);
    astToMirRet(insertInCurrentBlock(
        ctx, makeMirJumpOp(ctx->mir, &node->loc, ctx->endBasicBlock, NULL)));
}

static void visitContinueStmt(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    csAssert0(ctx->loopUpdateBasicBlock);
    astToMirRet(insertInCurrentBlock(
        ctx,
        makeMirJumpOp(ctx->mir, &node->loc, ctx->loopUpdateBasicBlock, NULL)));
}

static void visitVariableDecl(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    if (hasFlag(node, Comptime)) {
        return;
    }

    if (hasFlag(node, TopLevelDecl)) {
        astToMirRet(insertGlobalVariable(
            ctx,
            makeMirGlobalVariable(ctx->mir,
                                  &node->loc,
                                  node->varDecl.name,
                                  convertAstToMir(visitor, node->varDecl.init),
                                  node->type,
                                  NULL)));
    }
    else {
        MirNode *mirVar =
            makeLocalVar(ctx, &node->loc, node->varDecl.name, node->type);
        node->mir = mirVar;
        if (node->varDecl.init) {
            AstNode *init = node->varDecl.init;
            insertInCurrentBlock(
                ctx,
                makeMirAssignOp(ctx->mir,
                                &init->loc,
                                makeMirLoadOp(ctx->mir,
                                              &node->loc,
                                              mirVar->AllocaOp.name,
                                              mirVar,
                                              NULL),
                                convertAstToMir(visitor, init),
                                NULL));
        }
        astToMirRet(mirVar);
    }
}

static void visitExternDecl(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    AstNode *decl = node->externDecl.func;

    if (nodeIs(decl, FuncDecl)) {
        AstNode *param = decl->funcDecl.signature->params;
        const Type *ret = decl->type->func.retType;
        MirNodeList params = {};
        for (; param; param = param->next) {
            MirNode *mirParam = makeMirFunctionParam(
                ctx->mir,
                &param->loc,
                param->funcParam.name,
                makeMirTypeInfo(ctx->mir, &node->loc, param->type, NULL),
                NULL);
            param->mir = mirNodeListInsert(&params, mirParam);
        }

        astToMirRet(withMirFlags(makeMirFunction(ctx->mir,
                                                 &node->loc,
                                                 decl->funcDecl.name,
                                                 params.first,
                                                 ctx->basicBlocks.first,
                                                 node->type,
                                                 NULL),
                                 mifExtern));
    }
    else if (nodeIs(decl, VarDecl)) {
        astToMirRet(withMirFlags(makeMirGlobalVariable(ctx->mir,
                                                       &decl->loc,
                                                       decl->_namedNode.name,
                                                       NULL,
                                                       decl->type,
                                                       NULL),
                                 mifExtern));
    }
}

static void visitFuncDecl(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    if (hasFlag(node, Abstract))
        return;

    u64 flags = mifNone;
    if (hasFlag(node, Constructor))
        flags |= mifConstructor;
    if (findAttribute(node, S_inline))
        flags |= mifInline;
    else if (findAttribute(node, S_noinline))
        flags |= mifNoInline;
    const AstNode *opt = findAttribute(node, S_optimize);
    if (opt) {
        const AstNode *value = getAttributeArgument(NULL, NULL, opt, 0);
        if (!nodeIs(value, IntegerLit)) {
            logError(ctx->mir->L,
                     &opt->loc,
                     "invalid attribute argument expecting optimization level",
                     NULL);
        }
        // TODO
        flags |= mifNoOptimize;
    }

    const Type *ret = node->type->func.retType;
    AstNode *param = node->funcDecl.signature->params;
    MirNodeList params = {};
    for (; param; param = param->next) {
        param->mir = mirNodeListInsert(
            &params,
            makeMirFunctionParam(
                ctx->mir,
                &param->loc,
                param->funcParam.name,
                makeMirTypeInfo(ctx->mir, &node->loc, param->type, NULL),
                NULL));
    }

    ctx->mirFnCtx = (MirFunctionContext){};
    ctx->func = makeMirFunction(ctx->mir,
                                &node->loc,
                                node->funcDecl.name,
                                params.first,
                                ctx->basicBlocks.first,
                                node->type,
                                NULL);

    if (!hasFlag(node, Extern)) {
        ctx->mirFnCtx.uniqueNames = newHashTable(sizeof(Label));
        mirNodeListInit(&ctx->basicBlocks);
        MirNode *entry = insertBasicBlock(
            ctx, makeMirBasicBlock(ctx->mir, &node->loc, "entry", NULL));
        ctx->exitBasicBlock =
            makeMirBasicBlock(ctx->mir, &node->loc, "exit", NULL);

        ctx->currentBasicBlock = entry;

        if (!typeIs(ret, Void)) {
            ctx->returnVar = makeLocalVar(ctx, &node->loc, "_ret", ret);
        }

        node->mir = ctx->func;
        astVisit(visitor, node->funcDecl.body);
        if (!isMirBasicBlockTerminated(ctx->currentBasicBlock))
            insertInCurrentBlock(ctx,
                                 makeMirJumpOp(ctx->mir,
                                               &ctx->currentBasicBlock->loc,
                                               ctx->exitBasicBlock,
                                               NULL));
        ctx->currentBasicBlock = ctx->exitBasicBlock;
        insertBasicBlock(ctx, ctx->exitBasicBlock);
        insertInCurrentBlock(ctx, makeMirReturnOp(ctx->mir, &node->loc, ret));
        ctx->func->Function.body = ctx->basicBlocks.first;

        freeHashTable(&ctx->mirFnCtx.uniqueNames);
    }
    else {
        flags |= mifExtern;
    }

    ctx->func = withMirFlags(ctx->func, flags);
    astToMirRet(mirNodeListInsert(&ctx->globals, ctx->func));

    ctx->mirFnCtx = (MirFunctionContext){};
}

static void visitProgram(AstVisitor *visitor, AstNode *node)
{
    AstToMirContext *ctx = getAstVisitorContext(visitor);
    MirNodeList decls = {};
    AstNode *decl = node->program.decls;
    for (; decl; decl = decl->next) {
        mirNodeListInsert(&decls, convertAstToMir(visitor, decl));
    }
    cstring name =
        node->program.module ? node->program.module->moduleDecl.name : NULL;
    astToMirRet(makeMirModule(
        ctx->mir, &node->loc, name, node->program.path, decls.first));
}

#define newMirNode(TAG, BODY, ...)                                             \
    makeMirNode(ctx,                                                           \
                &(MirNode){.tag = mir##TAG,                                    \
                           .loc = *loc,                                        \
                           .TAG = BODY,                                        \
                           .next = next,                                       \
                           ##__VA_ARGS__})
#define __B(...) {__VA_ARGS__}

u64 mirCount(const MirNode *nodes)
{
    const MirNode *node = nodes;
    u64 i = 0;
    for (; node; node = node->next, i++)
        ;
    return i;
}

MirNode *makeMirNode(MirContext *ctx, MirNode *tmpl)
{
    MirNode *node = allocFromMemPool(ctx->pool, sizeof(MirNode));
    *node = *tmpl;
    return node;
}

MirNode *cloneMirNode(MirContext *ctx, const MirNode *node)
{
    MirNode *clone = allocFromMemPool(ctx->pool, sizeof(MirNode));
    *clone = *node;
    return clone;
}

MirNode *makeMirNullConst(MirContext *ctx,
                          const FileLoc *loc,
                          const Type *type,
                          MirNode *next)
{
    return newMirNode(NullConst, {}, .type = type);
}

MirNode *makeMirBoolConst(MirContext *ctx,
                          const FileLoc *loc,
                          bool value,
                          MirNode *next)
{
    return newMirNode(BoolConst,
                      {.value = value},
                      .type = ctx->types->primitiveTypes[prtBool]);
}

MirNode *makeMirCharConst(MirContext *ctx,
                          const FileLoc *loc,
                          char value,
                          MirNode *next)
{
    return newMirNode(CharConst,
                      {.value = value},
                      .type = ctx->types->primitiveTypes[prtCChar]);
}

MirNode *makeMirUnsignedConst(MirContext *ctx,
                              const FileLoc *loc,
                              u64 value,
                              const Type *type,
                              MirNode *next)
{
    return newMirNode(
        UnsignedConst, {.value = value}, .type = resolveUnThisUnwrapType(type));
}

MirNode *makeMirSignedConst(MirContext *ctx,
                            const FileLoc *loc,
                            i64 value,
                            const Type *type,
                            MirNode *next)
{
    return newMirNode(SignedConst, {.value = value}, .type = type);
}

MirNode *makeMirFloatConst(MirContext *ctx,
                           const FileLoc *loc,
                           f64 value,
                           const Type *type,
                           MirNode *next)
{
    return newMirNode(
        FloatConst, {.value = value}, .type = resolveUnThisUnwrapType(type));
}

MirNode *makeMirStringConst(MirContext *ctx,
                            const FileLoc *loc,
                            cstring value,
                            MirNode *next)
{
    return newMirNode(
        StringConst, {.value = value}, .type = ctx->types->stringType);
}

MirNode *makeMirArrayConst(MirContext *ctx,
                           const FileLoc *loc,
                           MirNode *elems,
                           u64 len,
                           const struct Type *type,
                           MirNode *next)
{
    return newMirNode(ArrayConst,
                      __B(.elems = elems, .len = len),
                      .type = resolveUnThisUnwrapType(type));
}

MirNode *makeMirStructConst(MirContext *ctx,
                            const FileLoc *loc,
                            MirNode *elems,
                            u64 len,
                            const struct Type *type,
                            MirNode *next)
{
    return newMirNode(StructConst,
                      __B(.elems = elems, .len = len),
                      .type = resolveUnThisUnwrapType(type));
}

MirNode *makeMirAllocaOp(MirContext *ctx,
                         const FileLoc *loc,
                         cstring name,
                         const Type *type,
                         MirNode *next)
{
    return newMirNode(
        AllocaOp, {.name = name}, .type = resolveUnThisUnwrapType(type));
}

MirNode *makeMirLoadOp(MirContext *ctx,
                       const FileLoc *loc,
                       cstring name,
                       MirNode *var,
                       MirNode *next)
{
    return newMirNode(LoadOp, __B(.name = name, .var = var), .type = var->type);
}
MirNode *makeMirAssignOp(MirContext *ctx,
                         const FileLoc *loc,
                         MirNode *lValue,
                         MirNode *rValue,
                         MirNode *next)
{
    return newMirNode(AssignOp,
                      __B(.lValue = lValue, .rValue = rValue),
                      .type = lValue->type);
}
MirNode *makeMirUnaryOp(MirContext *ctx,
                        const FileLoc *loc,
                        Operator op,
                        MirNode *operand,
                        MirNode *next)
{
    return newMirNode(
        UnaryOp, __B(.op = op, .operand = operand), .type = operand->type);
}

MirNode *makeMirBinaryOp(MirContext *ctx,
                         const FileLoc *loc,
                         MirNode *lhs,
                         Operator op,
                         MirNode *rhs,
                         const Type *type,
                         MirNode *next)
{
    return newMirNode(BinaryOp,
                      __B(.lhs = lhs, .op = op, .rhs = rhs),
                      .type = resolveUnThisUnwrapType(type));
}

MirNode *makeMirCastOp(MirContext *ctx,
                       const FileLoc *loc,
                       MirNode *expr,
                       MirNode *type,
                       MirNode *next)
{
    return newMirNode(
        CastOp, __B(.expr = expr, .type = type), .type = type->type);
}

MirNode *makeMirMoveOp(MirContext *ctx,
                       const FileLoc *loc,
                       MirNode *operand,
                       MirNode *next)
{
    return newMirNode(MoveOp, __B(.operand = operand), .type = operand->type);
}

MirNode *makeMirCopyOp(MirContext *ctx,
                       const FileLoc *loc,
                       MirNode *operand,
                       MirNode *next)
{
    return newMirNode(CopyOp, __B(.operand = operand), .type = operand->type);
}

MirNode *makeMirReferenceOfOp(MirContext *ctx,
                              const FileLoc *loc,
                              MirNode *operand,
                              const struct Type *type,
                              MirNode *next)
{
    return withMirFlags(newMirNode(ReferenceOfOp,
                                   __B(.operand = operand),
                                   .type = resolveUnThisUnwrapType(type)),
                        mifReference);
}

MirNode *makeMirPointerOfOp(MirContext *ctx,
                            const FileLoc *loc,
                            MirNode *operand,
                            const struct Type *type,
                            MirNode *next)
{
    return withMirFlags(newMirNode(PointerOfOp,
                                   __B(.operand = operand),
                                   .type = resolveUnThisUnwrapType(type)),
                        mifPointer);
}

MirNode *makeMirSizeofOp(MirContext *ctx,
                         const FileLoc *loc,
                         const struct Type *type,
                         MirNode *next)
{
    return newMirNode(SizeofOp,
                      __B(.operand = makeMirTypeInfo(
                              ctx, loc, resolveUnThisUnwrapType(type), NULL)),
                      .type = type);
}

MirNode *makeMirZeromemOp(MirContext *ctx,
                          const FileLoc *loc,
                          MirNode *operand,
                          MirNode *next)
{
    return newMirNode(
        ZeromemOp, __B(.operand = operand), .type = operand->type);
}

MirNode *makeMirArrayInitOp(MirContext *ctx,
                            const FileLoc *loc,
                            MirNode *elems,
                            u64 len,
                            const struct Type *type,
                            MirNode *next)
{
    return newMirNode(ArrayInitOp,
                      __B(.elems = elems, .len = len),
                      .type = resolveUnThisUnwrapType(type));
}

MirNode *makeMirStructInitOp(MirContext *ctx,
                             const FileLoc *loc,
                             MirNode *elems,
                             u64 len,
                             const struct Type *type,
                             MirNode *next)
{
    return newMirNode(StructInitOp,
                      __B(.elems = elems, .len = len),
                      .type = resolveUnThisUnwrapType(type));
}

MirNode *makeMirIndexOp(MirContext *ctx,
                        const FileLoc *loc,
                        MirNode *target,
                        MirNode *index,
                        MirNode *next)
{
    return newMirNode(IndexOp,
                      __B(.target = target, .index = index),
                      .type = typeGetElement(ctx, target->type));
}

MirNode *makeMirMemberOp(MirContext *ctx,
                         const FileLoc *loc,
                         MirNode *target,
                         u64 index,
                         MirNode *next)
{
    return newMirNode(MemberOp,
                      __B(.target = target, .index = index),
                      .type = typeGetMember(target->type, index));
}

MirNode *makeMirCallOp(MirContext *ctx,
                       const FileLoc *loc,
                       MirNode *func,
                       MirNode *args,
                       MirNode *panic,
                       MirNode *next)
{
    return newMirNode(CallOp,
                      __B(.func = func, .args = args, .panic = panic),
                      .type = typeReturnType(func->type));
}

MirNode *makeMirJumpOp(MirContext *ctx,
                       const FileLoc *loc,
                       MirNode *bb,
                       MirNode *next)
{
    return newMirNode(JumpOp, __B(.bb = bb), .type = ctx->types->voidType);
}

MirNode *makeMirIfOp(MirContext *ctx,
                     const FileLoc *loc,
                     MirNode *cond,
                     MirNode *ifTrue,
                     MirNode *ifFalse,
                     MirNode *next)
{
    return newMirNode(IfOp,
                      __B(.cond = cond, .ifTrue = ifTrue, .ifFalse = ifFalse),
                      .type = ifTrue->type);
}

MirNode *makeMirCaseOp(MirContext *ctx,
                       const FileLoc *loc,
                       MirNode *matches,
                       MirNode *body,
                       MirNode *next)
{
    return newMirNode(
        CaseOp, __B(.matches = matches, .body = body), .type = body->type);
}

MirNode *makeMirSwitchOp(MirContext *ctx,
                         const FileLoc *loc,
                         MirNode *cond,
                         MirNode *cases,
                         MirNode *next)
{
    return newMirNode(SwitchOp,
                      __B(.cond = cond, .cases = cases),
                      .type = ctx->types->voidType);
}

MirNode *makeMirReturnOp(MirContext *ctx, const FileLoc *loc, const Type *type)
{
    MirNode *next = NULL;
    return newMirNode(ReturnOp, {}, .type = type);
}

MirNode *makeMirPanicOp(MirContext *ctx, const FileLoc *loc)
{
    MirNode *next = NULL;
    return newMirNode(PanicOp, {});
}

MirNode *makeMirGlobalVariable(MirContext *ctx,
                               const FileLoc *loc,
                               cstring name,
                               MirNode *init,
                               const Type *type,
                               MirNode *next)
{
    return newMirNode(
        GlobalVariable, __B(.name = name, .init = init), .type = type);
}

MirNode *makeMirTypeInfo(MirContext *ctx,
                         const FileLoc *loc,
                         const Type *type,
                         MirNode *next)
{
    return newMirNode(TypeInfo, {.type = type}, .type = type);
}

MirNode *makeMirAsmOperand(MirContext *ctx,
                           const FileLoc *loc,
                           cstring constraint,
                           struct MirNode *operand,
                           MirNode *next)
{
    return newMirNode(AsmOperand,
                      __B(.constraint = constraint, .operand = operand),
                      .type = operand->type);
}

MirNode *makeMirInlineAssembly(MirContext *ctx,
                               const FileLoc *loc,
                               cstring text,
                               MirNode *outputs,
                               MirNode *inputs,
                               MirNode *clobbers,
                               const Type *type,
                               MirNode *next)
{
    return newMirNode(InlineAssembly,
                      __B(.text = text,
                          .outputs = outputs,
                          .inputs = inputs,
                          .clobbers = clobbers),
                      .type = type);
}

MirNode *makeMirBasicBlock(MirContext *ctx,
                           const FileLoc *loc,
                           cstring name,
                           MirNode *next)
{
    return newMirNode(
        BasicBlock, __B(.name = name), .type = ctx->types->voidType);
}

MirNode *makeMirFunctionParam(MirContext *ctx,
                              const FileLoc *loc,
                              cstring name,
                              MirNode *type,
                              MirNode *next)
{
    return newMirNode(
        FunctionParam, __B(.name = name, .type = type), .type = type->type);
}

MirNode *makeMirFunction(MirContext *ctx,
                         const FileLoc *loc,
                         cstring name,
                         MirNode *params,
                         MirNode *body,
                         const struct Type *type,
                         MirNode *next)
{
    return newMirNode(Function,
                      __B(.name = name, .params = params, .body = body),
                      .type = resolveUnThisUnwrapType(type));
}

MirNode *makeMirModule(MirContext *ctx,
                       const FileLoc *loc,
                       cstring name,
                       cstring path,
                       MirNode *decls)
{
    MirNode *next = NULL;
    return newMirNode(
        Module, __B(.name = name, .path = path, .decls = decls), .type = NULL);
}

#undef __B
#undef newMirNode

MirNode *mirNodeGetLastNode(MirNode *node)
{
    while (node && node->next)
        node = node->next;
    return node;
}

MirNode *mirNodeListInsert(MirNodeList *list, MirNode *node)
{
    if (node) {
        if (list->first == NULL)
            list->first = node;
        else
            list->last->next = node;
        list->last = mirNodeGetLastNode(node);
    }
    return node;
}

void mirNodeListInit(MirNodeList *list)
{
    list->first = NULL;
    list->last = NULL;
}

AstNode *astToMir(struct CompilerDriver *cc, AstNode *node)
{
    AstToMirContext context = {.mir = cc->mir};
    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        [astProgram] = visitProgram,
        [astAsmOperand] = visitAsmOperand,
        [astAsm] = visitInlineAssembly,
        [astBackendCall] = visitBackendCallExpr,
        [astIdentifier] = visitIdentifier,
        [astIntegerLit] = visitIntegerLit,
        [astNullLit] = visitNullLit,
        [astBoolLit] = visitBoolLit,
        [astCharLit] = visitCharLit,
        [astFloatLit] = visitFloatLit,
        [astStringLit] = visitStringLit,
        [astArrayExpr] = visitArrayExpr,
        [astBinaryExpr] = visitBinaryExpr,
        [astAssignExpr] = visitAssignExpr,
        [astTernaryExpr] = visitTernaryExpr,
        [astUnaryExpr] = visitUnaryExpr,
        [astTypedExpr] = visitTypedExpr,
        [astCallExpr] = visitCallExpr,
        [astCastExpr] = visitCastExpr,
        [astGroupExpr] = visitGroupExpr,
        [astStmtExpr] = visitStmtExpr,
        [astMemberExpr] = visitMemberExpr,
        [astPointerOf] = visitPointerOfExpr,
        [astReferenceOf] = visitReferenceOfExpr,
        [astIndexExpr] = visitIndexExpr,
        [astFieldExpr] = visitFieldExpr,
        [astStructExpr] = visitStructExpr,
        [astTupleExpr] = visitTupleExpr,
        [astUnionValueExpr] = visitUnionValue,
        [astBlockStmt] = visitBlockStmt,
        [astExprStmt] = visitExprStmt,
        [astReturnStmt] = visitReturnStmt,
        [astIfStmt] = visitIfStmt,
        [astWhileStmt] = visitWhileStmt,
        [astSwitchStmt] = visitSwitchStmt,
        [astContinueStmt] = visitContinueStmt,
        [astBreakStmt] = visitBreakStmt,
        [astVarDecl] = visitVariableDecl,
        [astExternDecl] = visitExternDecl,
        [astFuncDecl] = visitFuncDecl,
        [astMatchStmt] = astVisitSkip,
        [astFuncParamDecl] = astVisitSkip,
        [astCaseStmt] = astVisitSkip,
        [astStructDecl] = astVisitSkip,
        [astClassDecl] = astVisitSkip,
        [astInterfaceDecl] = astVisitSkip,
        [astUnionDecl] = astVisitSkip,
        [astEnumDecl] = astVisitSkip,
        [astTypeRef] = astVisitSkip,
        [astTypeDecl] = astVisitSkip,
        [astGenericDecl] = astVisitSkip,
        [astImportDecl] = astVisitSkip,
        [astMacroDecl] = astVisitSkip
    }, .fallback = astVisitFallbackVisitAll);
    // clang-format on

    astVisit(&visitor, node);
    if (hasErrors(cc->L))
        return NULL;

    return node;
}
