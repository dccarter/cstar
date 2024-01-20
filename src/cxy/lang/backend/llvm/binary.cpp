//
// Created by Carter Mbotho on 2024-01-11.
//

#include "context.h"

extern "C" {
#include "lang/frontend/flag.h"
#include "lang/frontend/visitor.h"
}

#define CREATE_BINARY_OP(ctx, OP, lhs, rhs, ...)                               \
    do {                                                                       \
        ctx.returnValue(ctx.builder().Create##OP(lhs, rhs, #__VA_ARGS__));     \
    } while (false)

static void generateLogicAndOr(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = LLVMContext::from(visitor);

    auto &builder = ctx.builder();
    auto func = builder.GetInsertBlock()->getParent();
    auto op = node->binaryExpr.op;

    auto logicLHS = llvm::BasicBlock::Create(ctx.context(),
                                             op == opLAnd ? "andlhs" : "orlhs");
    auto logicRHS = llvm::BasicBlock::Create(ctx.context(),
                                             op == opLAnd ? "andrhs" : "orrhs");
    auto logicPHI = llvm::BasicBlock::Create(ctx.context(),
                                             op == opLAnd ? "andphi" : "orphi");
    builder.CreateBr(logicLHS);

    func->insert(func->end(), logicLHS);
    builder.SetInsertPoint(logicLHS);
    auto lhs = codegen(visitor, node->binaryExpr.lhs);
    if (lhs == nullptr)
        return;

    if (op == opLOr)
        ctx.builder().CreateCondBr(lhs, logicPHI, logicRHS);
    else
        ctx.builder().CreateCondBr(lhs, logicRHS, logicPHI);
    logicLHS = builder.GetInsertBlock();

    // Generate right hand side and branch to phi
    func->insert(func->end(), logicRHS);
    builder.SetInsertPoint(logicRHS);
    auto rhs = codegen(visitor, node->binaryExpr.rhs);
    if (rhs == nullptr)
        return;
    ctx.builder().CreateBr(logicPHI);
    logicRHS = builder.GetInsertBlock();

    // Generate phi
    func->insert(func->end(), logicPHI);
    builder.SetInsertPoint(logicPHI);
    auto phi = builder.CreatePHI(lhs->getType(), 2, "logicphi");
    phi->addIncoming(lhs, logicLHS);
    phi->addIncoming(rhs, logicRHS);

    ctx.returnValue(phi);
}

void visitBinaryExpr(AstVisitor *visitor, AstNode *node)
{
    auto op = node->binaryExpr.op;
    if (op == opLAnd || op == opLOr) {
        generateLogicAndOr(visitor, node);
        return;
    }

    auto &ctx = LLVMContext::from(visitor);
    AstNode *left = node->binaryExpr.lhs, *right = node->binaryExpr.rhs;
    const Type *type = node->type;

    auto lhs = codegen(visitor, left);
    auto rhs = codegen(visitor, right);

    if (left->type != right->type) {
        // implicitly cast to the bigger type
        if (isPrimitiveTypeBigger(left->type, right->type)) {
            if (isFloatType(left->type))
                rhs = ctx.builder().CreateSIToFP(rhs, lhs->getType());
            else if (isUnsignedType(left->type))
                rhs = ctx.builder().CreateZExt(rhs, lhs->getType());
            else
                rhs = ctx.builder().CreateSExt(rhs, lhs->getType());
            type = left->type;
        }
        else {
            if (isFloatType(right->type))
                lhs = ctx.builder().CreateSIToFP(lhs, rhs->getType());
            else if (isUnsignedType(left->type))
                lhs = ctx.builder().CreateZExt(lhs, rhs->getType());
            else
                lhs = ctx.builder().CreateSExt(lhs, rhs->getType());
            type = right->type;
        }
    }

#define CREATE_TYPE_SPECIFIC_OP(Op)                                            \
    do {                                                                       \
        if (isFloatType(type))                                                 \
            CREATE_BINARY_OP(ctx, F##Op, lhs, rhs);                            \
        else                                                                   \
            CREATE_BINARY_OP(ctx, Op, lhs, rhs);                               \
    } while (false)

    switch (node->binaryExpr.op) {
    case opAdd:
        CREATE_TYPE_SPECIFIC_OP(Add);
        break;
    case opSub:
        CREATE_TYPE_SPECIFIC_OP(Sub);
        break;
    case opMul:
        CREATE_TYPE_SPECIFIC_OP(Mul);
        break;
    case opDiv:
        if (isFloatType(type))
            CREATE_BINARY_OP(ctx, FDiv, lhs, rhs);
        else if (isSignedType(type))
            CREATE_BINARY_OP(ctx, SDiv, lhs, rhs);
        else
            CREATE_BINARY_OP(ctx, UDiv, lhs, rhs);
        break;
    case opMod:
        if (isFloatType(type))
            CREATE_BINARY_OP(ctx, FRem, lhs, rhs);
        else if (isSignedType(type))
            CREATE_BINARY_OP(ctx, SRem, lhs, rhs);
        else
            CREATE_BINARY_OP(ctx, URem, lhs, rhs);
        break;
    case opBAnd:
        CREATE_BINARY_OP(ctx, And, lhs, rhs);
        break;
    case opBOr:
        CREATE_BINARY_OP(ctx, And, lhs, rhs);
        break;
    case opBXor:
        CREATE_BINARY_OP(ctx, Xor, lhs, rhs);
        break;
    case opShl:
        CREATE_BINARY_OP(ctx, Shl, lhs, rhs);
        break;
    case opShr:
        CREATE_BINARY_OP(ctx, LShr, lhs, rhs);
        break;

#undef CREATE_TYPE_SPECIFIC_OP
#define CREATE_TYPE_SPECIFIC_OP(Op)                                            \
    do {                                                                       \
        if (isFloatType(type))                                                 \
            CREATE_BINARY_OP(ctx, FCmpU##Op, lhs, rhs);                        \
        else if (isSignedType(type))                                           \
            CREATE_BINARY_OP(ctx, ICmpS##Op, lhs, rhs);                        \
        else                                                                   \
            CREATE_BINARY_OP(ctx, ICmpU##Op, lhs, rhs);                        \
    } while (false)

    case opLt:
        CREATE_TYPE_SPECIFIC_OP(LT);
        break;
    case opGt:
        CREATE_TYPE_SPECIFIC_OP(GT);
        break;
    case opLeq:
        CREATE_TYPE_SPECIFIC_OP(LE);
        break;
    case opGeq:
        CREATE_TYPE_SPECIFIC_OP(GE);
        break;
    case opEq:
        if (isFloatType(type))
            CREATE_BINARY_OP(ctx, FCmpUEQ, lhs, rhs);
        else
            CREATE_BINARY_OP(ctx, ICmpEQ, lhs, rhs);
        break;
    case opNe:
        if (isFloatType(type))
            CREATE_BINARY_OP(ctx, FCmpUNE, lhs, rhs);
        else
            CREATE_BINARY_OP(ctx, ICmpNE, lhs, rhs);
        break;
    default:
        logError(ctx.L,
                 &node->loc,
                 "unsupported binary operator `{s}`",
                 (FormatArg[]){{.s = getBinaryOpString(node->binaryExpr.op)}});
        ctx.returnValue(nullptr);
        break;
    }
}