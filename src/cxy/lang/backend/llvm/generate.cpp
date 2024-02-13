//
// Created by Carter Mbotho on 2024-01-09.
//

#include "context.h"
#include "llvm.h"

#include "llvm/IR/Verifier.h"

#include <vector>

extern "C" {
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/visitor.h"
}

static llvm::GlobalValue::LinkageTypes getLinkageType(const AstNode *node)
{
    if (hasFlag(node, Public))
        return llvm::GlobalValue::ExternalLinkage;
    return llvm::GlobalValue::InternalLinkage;
}

static void dispatch(Visitor func, AstVisitor *visitor, AstNode *node)
{
    if (!hasFlag(node, Comptime)) {
        auto &ctx = cxy::LLVMContext::from(visitor);
        auto stack = ctx.stack();
        func(visitor, node);
        ctx.setStack(stack);
    }
}

static llvm::Function *generateFunctionProto(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    Type *type = const_cast<Type *>(node->type);
    std::vector<llvm::Type *> params;

    if (auto this_ = node->funcDecl.this_) {
        // add this parameter
        node->funcDecl.signature->params = this_;
    }

    AstNode *param = node->funcDecl.signature->params;
    for (u64 i = 0; param; param = param->next, i++) {
        if (hasFlag(node, Variadic) && hasFlag(param, Variadic))
            break;
        auto paramType = ctx.getLLVMType(param->type);
        if (typeIs(param->type, Func))
            paramType = paramType->getPointerTo();
        params.push_back(paramType);
    }

    auto funcType = llvm::FunctionType::get(
        ctx.getLLVMType(type->func.retType), params, hasFlag(node, Variadic));

    auto func = llvm::Function::Create(
        funcType, getLinkageType(node), ctx.makeTypeName(node), &ctx.module());

    cxy::updateType(type, funcType);
    node->codegen = func;

    param = node->funcDecl.signature->params;
    for (auto &arg : func->args()) {
        arg.setName(param->funcParam.name);
        param = param->next;
    }

    return func;
}

static llvm::Value *castFromUnion(AstVisitor *visitor,
                                  const Type *to,
                                  AstNode *node,
                                  u64 idx)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    auto load = ctx.stack().loadVariable(false);
    auto value = cxy::codegen(visitor, node);
    ctx.stack().loadVariable(load);
    auto type = static_cast<llvm::Type *>(
        unwrapType(node->type, nullptr)->tUnion.members[idx].codegen);
    // TODO assert/throw if type mis-match
    value = ctx.builder.CreateBitCast(value, type->getPointerTo());
    value = ctx.builder.CreateStructGEP(type, value, 1);
    if (load)
        value = ctx.builder.CreateLoad(ctx.getLLVMType(to), value);
    return value;
}

static void visitPrefixUnaryExpr(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    auto op = node->unaryExpr.op;
    if (op == opPreInc || op == opPreDec) {
        // transform to assignment operator
        node->tag = astAssignExpr;
        node->assignExpr.lhs = node->unaryExpr.operand;
        node->assignExpr.rhs =
            makeIntegerLiteral(ctx.pool, &node->loc, 1, nullptr, node->type);
        node->assignExpr.op = op == opPreInc ? opAdd : opSub;
        cxy::codegen(visitor, node);
        ctx.returnValue(cxy::codegen(visitor, node->assignExpr.lhs));
        return;
    }

    auto value = cxy::codegen(visitor, node->unaryExpr.operand);
    switch (node->unaryExpr.op) {
    case opPlus:
        ctx.returnValue(value);
        break;
    case opMinus:
        if (isFloatType(node->type))
            ctx.returnValue(ctx.builder.CreateFNeg(value));
        else
            ctx.returnValue(ctx.builder.CreateNeg(value));
        break;
    case opNot:
    case opCompl:
        ctx.returnValue(ctx.builder.CreateNot(value));
        break;
    default:
        logError(ctx.L,
                 &node->loc,
                 "unsupported prefix unary operator `{s}`",
                 (FormatArg[]){{.s = getUnaryOpString(node->unaryExpr.op)}});
        ctx.returnValue(nullptr);
        break;
    }
}

static llvm::Value *generateCastExpr(AstVisitor *visitor,
                                     const Type *to,
                                     AstNode *expr,
                                     u64 idx = 0)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    auto from = unwrapType(expr->type, nullptr);

    if (to == from)
        return cxy::codegen(visitor, expr);

    if (typeIs(from, Union))
        return castFromUnion(visitor, to, expr, idx);

    auto value = cxy::codegen(visitor, expr);
    if (isFloatType(to)) {
        auto s1 = getPrimitiveTypeSize(to), s2 = getPrimitiveTypeSize(from);
        if (isFloatType(from)) {
            if (s1 < s2)
                return ctx.builder.CreateFPTrunc(value, ctx.getLLVMType(to));
            else
                return ctx.builder.CreateFPExt(value, ctx.getLLVMType(to));
        }
        else if (isUnsignedType(from) || isCharacterType(from)) {
            return ctx.builder.CreateSIToFP(value, ctx.getLLVMType(to));
        }
        else {
            return ctx.builder.CreateUIToFP(value, ctx.getLLVMType(to));
        }
    }
    else if (isIntegerType(to) || isCharacterType(to)) {
        if (isUnsignedType(from) || isCharacterType(from)) {
            return ctx.builder.CreateZExtOrTrunc(value, ctx.getLLVMType(to));
        }
        else if (isSignedIntegerType(from)) {
            return ctx.builder.CreateSExtOrTrunc(value, ctx.getLLVMType(to));
        }
        else if (isFloatType(from)) {
            return isUnsignedType(to)
                       ? ctx.builder.CreateFPToUI(value, ctx.getLLVMType(to))
                       : ctx.builder.CreateFPToSI(value, ctx.getLLVMType(to));
        }
        else if (isPointerType(from)) {
            csAssert0(to->primitive.id == prtU64);
            return ctx.builder.CreatePtrToInt(value, ctx.getLLVMType(to));
        }
        else {
            unreachable("Unsupported cast")
        }
    }
    else if (isPointerType(to)) {
        if (isIntegralType(from)) {
            csAssert0(from->primitive.id == prtU64);
            return ctx.builder.CreateIntToPtr(value, ctx.getLLVMType(to));
        }
        else {
            csAssert0(isPointerType(from));
            auto fromPointed = from->pointer.pointed,
                 toPointed = to->pointer.pointed;
            if (isUnionType(fromPointed) &&
                findUnionTypeIndex(fromPointed, toPointed) != UINT32_MAX) {
                // Union pointer cast weird!
                value = ctx.builder.CreateStructGEP(
                    ctx.getLLVMType(fromPointed), value, 1);
            }
            return ctx.builder.CreatePointerCast(value, ctx.getLLVMType(to));
        }
    }
    else {
        unreachable("Unsupported cast");
    }
}

static void visitNullLit(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    AstNode *parent = node->parentScope;
    if (nodeIs(parent, AssignExpr))
        ctx.returnValue(llvm::ConstantPointerNull::get(
            llvm::dyn_cast<llvm::PointerType>(ctx.getLLVMType(parent->type))));
    else if (nodeIs(parent, BinaryExpr)) {
        if (parent->binaryExpr.rhs == node)
            ctx.returnValue(llvm::ConstantPointerNull::get(
                llvm::dyn_cast<llvm::PointerType>(
                    ctx.getLLVMType(parent->binaryExpr.lhs->type))));
        else
            ctx.returnValue(llvm::ConstantPointerNull::get(
                llvm::dyn_cast<llvm::PointerType>(
                    ctx.getLLVMType(parent->binaryExpr.rhs->type))));
    }
    else {
        ctx.returnValue(llvm::ConstantPointerNull::get(
            llvm::dyn_cast<llvm::PointerType>(ctx.getLLVMType(node->type))));
    }
}

static void visitBoolLit(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    ctx.returnValue(ctx.builder.getInt1(node->boolLiteral.value));
}

static void visitCharLit(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    ctx.returnValue(ctx.builder.getInt32(node->charLiteral.value));
}

static void visitIntegerLit(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    ctx.returnValue(llvm::ConstantInt::get(ctx.getLLVMType(node->type),
                                           node->intLiteral.uValue,
                                           node->intLiteral.isNegative));
}

static void visitFloatLit(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    ctx.returnValue(llvm::ConstantFP::get(
        ctx.context, llvm::APFloat(node->floatLiteral.value)));
}

static void visitStringLit(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    ctx.returnValue(
        ctx.builder.CreateGlobalStringPtr(node->stringLiteral.value));
}

static void visitIdentifierExpr(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    auto target = node->ident.resolvesTo;
    auto value = static_cast<llvm::Value *>(target->codegen);

    if (ctx.stack().loadVariable() && !nodeIs(target, FuncDecl) &&
        !nodeIs(target, ExternDecl)) {
        value = ctx.createLoad(node->type, value);
    }

    ctx.returnValue(value);
}

static void visitMemberExpr(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    auto target = node->memberExpr.target;
    auto type = target->type;

    auto load =
        ctx.stack().loadVariable(typeIs(type, Pointer) || isClassType(type));
    auto value = cxy::codegen(visitor, target);
    ctx.stack().loadVariable(load);

    auto member = node->memberExpr.member;
    if (nodeIs(member, Identifier)) {
        auto resolvesTo = member->ident.resolvesTo;

        if (nodeIs(resolvesTo, FieldDecl)) {
            value = ctx.builder.CreateStructGEP(
                isClassType(type) ? ctx.classType(type)
                                  : ctx.getLLVMType(stripPointer(type)),
                value,
                resolvesTo->fieldExpr.index,
                resolvesTo->fieldExpr.name);
        }
        else if (nodeIs(resolvesTo, EnumOptionDecl)) {
            value = static_cast<llvm::ConstantInt *>(resolvesTo->codegen);
            ctx.returnValue(value);
            return;
        }
    }
    else {
        csAssert0(nodeIs(member, IntegerLit));
        value = ctx.builder.CreateStructGEP(
            ctx.getLLVMType(stripPointer(target->type)),
            value,
            member->intLiteral.value);
    }

    value = ctx.createLoad(node->type, value);
    ctx.returnValue(value);
}

static void visitCastExpr(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    auto to = unwrapType(node->castExpr.to->type, nullptr);
    auto value =
        generateCastExpr(visitor, to, node->castExpr.expr, node->castExpr.idx);
    ctx.returnValue(value);
}

static void visitAddrOfExpr(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    AstNode *operand = node->unaryExpr.operand;
    if (nodeIs(operand, StructExpr)) {
        auto ptr = ctx.createStackVariable(operand->type);
        auto value = cxy::codegen(visitor, operand);
        ctx.builder.CreateStore(value, ptr);
        ctx.returnValue(ptr);
    }
    else {
        auto load = ctx.stack().loadVariable(false);
        auto value = cxy::codegen(visitor, operand);
        ctx.returnValue(value);
        ctx.stack().loadVariable(load);
    }
}

static void visitIndexExpr(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    auto type = node->indexExpr.target->type;

    auto load =
        ctx.stack().loadVariable(typeIs(type, String) || typeIs(type, Pointer));
    auto target = cxy::codegen(visitor, node->indexExpr.target);

    if (target == nullptr)
        return;

    ctx.stack().loadVariable(true);
    auto index = cxy::codegen(visitor, node->indexExpr.index);
    ctx.stack().loadVariable(load);

    if (index == nullptr)
        return;

    llvm::Value *variable;
    if (typeIs(type, String))
        variable =
            ctx.builder.CreateGEP(ctx.builder.getInt8Ty(), target, {index});
    else if (typeIs(type, Array))
        variable = ctx.builder.CreateGEP(
            ctx.getLLVMType(type->array.elementType), target, {index});
    else if (typeIs(type, Pointer)) {
        variable = ctx.builder.CreateGEP(
            ctx.getLLVMType(type->pointer.pointed), target, {index});
    }

    if (ctx.stack().loadVariable())
        ctx.returnValue(
            ctx.builder.CreateLoad(ctx.getLLVMType(node->type), variable));
    else
        ctx.returnValue(variable);
}

static void visitTypedExpr(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    auto to = node->typedExpr.type->type, from = node->typedExpr.expr->type;
    auto load = ctx.stack().loadVariable(ctx.stack().loadVariable() &
                                         !typeIs(from, Array));
    llvm::Value *value = cxy::codegen(visitor, node->typedExpr.expr);
    ctx.stack().loadVariable(load);
    if (typeIs(from, Pointer)) {
        if (typeIs(to, Pointer)) {
            value = ctx.builder.CreatePointerCast(value, ctx.getLLVMType(to));
        }
        else if (isIntegerType(to)) {
            csAssert0(to->primitive.id == prtU64);
            value = ctx.builder.CreatePtrToInt(value, ctx.getLLVMType(to));
        }
    }
    else if (typeIs(from, Array) && typeIs(to, Pointer)) {
        value = ctx.builder.CreateInBoundsGEP(
            ctx.getLLVMType(from),
            value,
            {ctx.builder.getInt64(0), ctx.builder.getInt64(0)});
    }
    else {
        value->mutateType(ctx.getLLVMType(node->type));
    }
    ctx.returnValue(value);
}

static void visitAssignExpr(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    if (node->binaryExpr.op != opAssign) {
        node->binaryExpr.rhs =
            makeBinaryExpr(ctx.pool,
                           &node->loc,
                           node->flags,
                           shallowCloneAstNode(ctx.pool, node->assignExpr.lhs),
                           node->binaryExpr.op,
                           node->binaryExpr.rhs,
                           nullptr,
                           node->binaryExpr.rhs->type);
        node->binaryExpr.op = opAssign;
    }

    ctx.stack().loadVariable(false);
    auto variable = cxy::codegen(visitor, node->binaryExpr.lhs);
    ctx.stack().loadVariable(true);
    node->codegen = variable;

    if (variable == nullptr)
        return;

    auto value = cxy::codegen(visitor, node->binaryExpr.rhs);
    if (value == nullptr)
        return;

    auto lhs = node->binaryExpr.lhs->type;
    if (lhs != node->binaryExpr.rhs->type) {
        // change the type of the right hand side
        if (isFloatType(lhs))
            value = ctx.builder.CreateSIToFP(value, ctx.getLLVMType(lhs));
        else if (isUnsignedType(lhs))
            value = ctx.builder.CreateZExt(value, ctx.getLLVMType(lhs));
        else
            value = ctx.builder.CreateSExt(value, ctx.getLLVMType(lhs));
    }

    ctx.returnValue(ctx.builder.CreateStore(value, variable));
}

static void visitTernaryExpr(AstVisitor *visitor, AstNode *node)
{
    // This is ok to do since the structure of a ternary node is the same as
    // that of an if
    node->tag = astIfStmt;
    node->ifStmt.isTernary = true;
    astVisit(visitor, node);
}

static void visitUnaryExpr(AstVisitor *visitor, AstNode *node)
{
    if (node->unaryExpr.isPrefix) {
        visitPrefixUnaryExpr(visitor, node);
        return;
    }

    auto &ctx = cxy::LLVMContext::from(visitor);
    auto operand = node->unaryExpr.operand;
    auto op = node->unaryExpr.op;
    auto value = cxy::codegen(visitor, operand);

    node->tag = astAssignExpr;
    node->assignExpr.lhs = operand;
    node->assignExpr.rhs =
        makeIntegerLiteral(ctx.pool, &node->loc, 1, nullptr, node->type);
    node->assignExpr.op = op == opPostDec ? opSub : opAdd;

    cxy::codegen(visitor, node);
    ctx.returnValue(value);
}

static void visitCallExpr(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    auto type = node->callExpr.callee->type;

    auto callee = cxy::codegen(visitor, node->callExpr.callee);
    llvm::FunctionType *funcType = nullptr;
    if (auto func = llvm::dyn_cast<llvm::Function>(callee)) {
        funcType = func->getFunctionType();
    }
    else {
        funcType = llvm::dyn_cast<llvm::FunctionType>(ctx.getLLVMType(type));
    }

    csAssert0(callee);

    std::vector<llvm::Value *> args;
    AstNode *arg = node->callExpr.args;
    for (; arg; arg = arg->next) {
        auto value = cxy::codegen(visitor, arg);
        if (value == nullptr)
            return;
        args.push_back(value);
    }

    ctx.returnValue(ctx.builder.CreateCall(funcType, callee, args));
}

static void visitStructExpr(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    if (node->structExpr.fields) {
        auto obj = ctx.createUndefined(node->type);

        auto field = node->structExpr.fields;
        for (u64 i = 0; field; field = field->next, i++) {
            auto value = cxy::codegen(visitor, field->fieldExpr.value);
            obj = ctx.builder.CreateInsertValue(obj, value, i);
        }
        ctx.returnValue(obj);
    }
    else {
        ctx.returnValue(ctx.createUndefined(node->type));
    }
}

static void visitTupleExpr(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    if (node->tupleExpr.elements) {
        auto obj = ctx.createUndefined(node->type);

        auto elem = node->tupleExpr.elements;
        for (u64 i = 0; elem; elem = elem->next, i++) {
            auto value = cxy::codegen(visitor, elem);
            obj = ctx.builder.CreateInsertValue(obj, value, i);
        }
        ctx.returnValue(obj);
    }
}

static void visitUnionValue(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    auto unionType = ctx.getLLVMType(node->type);
    auto type = static_cast<llvm::Type *>(
        node->type->tUnion.members[node->unionValue.idx].codegen);
    auto tmp = ctx.createStackVariable(type);

    auto obj = ctx.createUndefined(type);
    type->getContainedType(0);
    obj = ctx.builder.CreateInsertValue(
        obj, cxy::LLVMContext::getUnionTag(node->unionValue.idx, unionType), 0);
    obj = ctx.builder.CreateInsertValue(
        obj, cxy::codegen(visitor, node->unionValue.value), 1);
    ctx.builder.CreateStore(obj, tmp);

    obj = ctx.builder.CreateBitCast(tmp, unionType->getPointerTo());
    if (ctx.stack().loadVariable()) {
        obj = ctx.builder.CreateLoad(unionType, obj);
    }
    ctx.returnValue(obj);
}

void visitReturnStmt(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    if (node->returnStmt.expr) {
        auto expr = node->returnStmt.expr, func = ctx.stack().currentFunction();
        auto funcReturnType = unwrapType(func->type->func.retType, nullptr);
        auto value = generateCastExpr(visitor, funcReturnType, expr);
        ctx.builder.CreateStore(
            value, static_cast<llvm::AllocaInst *>(ctx.stack().result()));
    }

    ctx.builder.CreateBr(
        static_cast<llvm::BasicBlock *>(ctx.stack().funcEnd()));
    ctx.unreachable = true;
}

void visitIfStmt(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    auto &builder = ctx.builder;
    auto cond = cxy::codegen(visitor, node->ifStmt.cond);
    if (cond == nullptr)
        return;

    ctx.unreachable = false;
    auto func = builder.GetInsertBlock()->getParent();
    auto then = llvm::BasicBlock::Create(ctx.context, "then", func);
    auto otherwise = llvm::BasicBlock::Create(ctx.context, "else");
    auto merge = llvm::BasicBlock::Create(ctx.context, "phi");

    builder.CreateCondBr(cond, then, otherwise);

    builder.SetInsertPoint(then);
    auto thenRetVal = cxy::codegen(visitor, node->ifStmt.body);
    if (thenRetVal == nullptr)
        return;

    if (!ctx.unreachable)
        builder.CreateBr(merge);
    ctx.unreachable = false;
    // Codegen of `Then` can change the current block, update the `then`
    // declaration
    then = builder.GetInsertBlock();

    // emit the else block
    func->insert(func->end(), otherwise);
    builder.SetInsertPoint(otherwise);
    llvm::Value *otherwiseRetVal = nullptr;
    if (node->ifStmt.otherwise) {
        otherwiseRetVal = cxy::codegen(visitor, node->ifStmt.otherwise);
        if (otherwiseRetVal == nullptr)
            csAssert0(!node->ifStmt.isTernary);
    }

    if (!ctx.unreachable)
        builder.CreateBr(merge);
    ctx.unreachable = false;
    // codegen of `else` can change the current block
    otherwise = builder.GetInsertBlock();

    // generate merge block
    func->insert(func->end(), merge);
    builder.SetInsertPoint(merge);
    if (node->ifStmt.isTernary) {
        auto phi = builder.CreatePHI(thenRetVal->getType(), 2, "iftmp");
        phi->addIncoming(thenRetVal, then);
        phi->addIncoming(otherwiseRetVal, otherwise);

        ctx.returnValue(phi);
    }
}

void visitBreakStmt(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    auto &builder = ctx.builder;
    csAssert0(ctx.stack().loopEnd());
    ctx.returnValue(builder.CreateBr(
        static_cast<llvm::BasicBlock *>(ctx.stack().loopEnd())));
    ctx.unreachable = true;
}

void visitContinueStmt(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    auto &builder = ctx.builder;
    csAssert0(ctx.stack().loopCondition());
    if (ctx.stack().loopUpdate()) {
        ctx.returnValue(builder.CreateBr(
            static_cast<llvm::BasicBlock *>(ctx.stack().loopUpdate())));
    }
    else {
        ctx.returnValue(builder.CreateBr(
            static_cast<llvm::BasicBlock *>(ctx.stack().loopEnd())));
    }
    ctx.unreachable = true;
}

void visitWhileStmt(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    auto &builder = ctx.builder;
    auto func = builder.GetInsertBlock()->getParent();

    auto cond = llvm::BasicBlock::Create(ctx.context, "while.cond", func);
    auto body = llvm::BasicBlock::Create(ctx.context, "while.body");
    auto end = llvm::BasicBlock::Create(ctx.context, "while.end");
    llvm::BasicBlock *update{nullptr};
    if (node->whileStmt.update) {
        update = llvm::BasicBlock::Create(ctx.context, "while.update");
        node->whileStmt.update->codegen = update;
    }

    ctx.stack().loopCondition(cond);
    ctx.stack().loopUpdate(update);
    ctx.stack().loopEnd(end);

    builder.CreateBr(cond);

    // generate condition
    builder.SetInsertPoint(cond);
    auto condition = cxy::codegen(visitor, node->whileStmt.cond);
    if (condition == nullptr)
        return;
    builder.CreateCondBr(condition, body, end);
    cond = builder.GetInsertBlock();

    // generate body
    func->insert(func->end(), body);
    builder.SetInsertPoint(body);
    cxy::codegen(visitor, node->whileStmt.body);

    // generate update
    if (node->whileStmt.update) {
        builder.CreateBr(update);
        func->insert(func->end(), update);
        builder.SetInsertPoint(update);
        cxy::codegen(visitor, node->whileStmt.update);
    }
    builder.CreateBr(cond);

    // generate end of while loop
    func->insert(func->end(), end);
    builder.SetInsertPoint(end);

    ctx.returnValue(builder.getInt32(0));
}

static void visitSwitchStmt(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    auto func = ctx.builder.GetInsertBlock()->getParent();
    auto current = ctx.builder.GetInsertBlock();
    auto condition = cxy::codegen(visitor, node->switchStmt.cond);
    AstNode *case_ = node->switchStmt.cases;

    std::vector<std::pair<llvm::ConstantInt *, llvm::BasicBlock *>> cases{};
    llvm::BasicBlock *defaultBB = nullptr;
    auto end = llvm::BasicBlock::Create(ctx.context, "switch.end");
    for (u64 i = 0; case_; case_ = case_->next, i++) {
        llvm::ConstantInt *value = nullptr;
        llvm::BasicBlock *bb = nullptr;
        if (case_->caseStmt.match) {
            value = llvm::cast<llvm::ConstantInt>(
                cxy::codegen(visitor, case_->caseStmt.match));
            bb = llvm::BasicBlock::Create(ctx.context,
                                          "switch.case." + std::to_string(i));
            cases.emplace_back(value, bb);
        }
        else {
            csAssert0(defaultBB == nullptr);
            bb = llvm::BasicBlock::Create(ctx.context, "switch.default");
            defaultBB = bb;
        }

        func->insert(func->end(), bb);
        ctx.builder.SetInsertPoint(bb);
        cxy::codegen(visitor, case_->caseStmt.body);
        // branch to the end
        ctx.builder.CreateBr(end);
    }

    ctx.builder.SetInsertPoint(current);
    auto switchInst = ctx.builder.CreateSwitch(condition, defaultBB ?: end);
    for (auto &[value, block] : cases) {
        switchInst->addCase(value, block);
    }

    func->insert(func->end(), end);
    ctx.builder.SetInsertPoint(end);
    ctx.returnValue(switchInst);
}

static void visitMatchStmt(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    auto func = ctx.builder.GetInsertBlock()->getParent();
    auto current = ctx.builder.GetInsertBlock();

    auto load = ctx.stack().loadVariable(false);
    auto unionValue = cxy::codegen(visitor, node->matchStmt.expr);
    ctx.stack().loadVariable(load);
    auto condition = ctx.builder.CreateStructGEP(
        ctx.getLLVMType(node->matchStmt.expr->type), unionValue, 0);
    condition =
        ctx.builder.CreateLoad(llvm::Type::getInt8Ty(ctx.context), condition);

    AstNode *case_ = node->matchStmt.cases;

    std::vector<std::pair<llvm::ConstantInt *, llvm::BasicBlock *>> cases{};
    llvm::BasicBlock *defaultBB = nullptr;
    auto end = llvm::BasicBlock::Create(ctx.context, "match.end");
    for (u64 i = 0; case_; case_ = case_->next, i++) {
        llvm::ConstantInt *value = nullptr;
        llvm::BasicBlock *bb = nullptr;
        if (case_->caseStmt.match) {
            value = ctx.builder.getInt8(case_->caseStmt.idx);
            bb = llvm::BasicBlock::Create(ctx.context,
                                          "match.case." + std::to_string(i));
            cases.emplace_back(value, bb);
        }
        else {
            csAssert0(defaultBB == nullptr);
            bb = llvm::BasicBlock::Create(ctx.context, "match.default");
            defaultBB = bb;
        }

        func->insert(func->end(), bb);
        ctx.builder.SetInsertPoint(bb);
        if (case_->caseStmt.variable) {
            // visit the variable before generate code
            cxy::codegen(visitor, case_->caseStmt.variable);
        }
        cxy::codegen(visitor, case_->caseStmt.body);
        // branch to the end
        ctx.builder.CreateBr(end);
    }

    ctx.builder.SetInsertPoint(current);
    auto switchInst = ctx.builder.CreateSwitch(condition, defaultBB ?: end);
    for (auto &[value, block] : cases) {
        switchInst->addCase(value, block);
    }

    func->insert(func->end(), end);
    ctx.builder.SetInsertPoint(end);
    ctx.returnValue(switchInst);
}

void visitVariableDecl(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    if (hasFlag(node, TopLevelDecl)) {
        // global variable
        ctx.module().getOrInsertGlobal(node->varDecl.name,
                                       ctx.getLLVMType(node->type));
        auto var = ctx.module().getGlobalVariable(node->varDecl.name);
        var->setAlignment(llvm::Align(4));
        var->setConstant(hasFlag(node, Const));
        if (node->varDecl.init) {
            auto value = cxy::codegen(visitor, node->varDecl.init);
            var->setInitializer(llvm::dyn_cast<llvm::Constant>(value));
        }
        node->codegen = var;
        ctx.returnValue(var);
        return;
    }

    auto func = ctx.builder.GetInsertBlock()->getParent();
    llvm::IRBuilder<> tmpBuilder(&func->getEntryBlock(),
                                 func->getEntryBlock().begin());
    auto variable = ctx.createStackVariable(node->type, node->varDecl.name);
    node->codegen = variable;

    if (!node->varDecl.init) {
        ctx.returnValue(variable);
        return;
    }

    auto init = node->varDecl.init;
    auto value = cxy::codegen(visitor, init);
    if (value) {
        if (node->type != node->varDecl.init->type) {
            // change the type of the right hand side
            if (isFloatType(node->type))
                value = ctx.builder.CreateSIToFP(value,
                                                 variable->getAllocatedType());
            else if (isUnsignedType(node->type))
                value =
                    ctx.builder.CreateZExt(value, variable->getAllocatedType());
            else
                value =
                    ctx.builder.CreateSExt(value, variable->getAllocatedType());
        }
        ctx.builder.CreateStore(value, variable);
    }
    ctx.returnValue(variable);
}

void visitFuncDecl(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    auto type = node->type;
    llvm::Function *func = ctx.module().getFunction(ctx.makeTypeName(node))
                               ?: generateFunctionProto(visitor, node);

    if (func == nullptr)
        return;

    if (hasFlag(node, Extern)) {
        ctx.returnValue(func);
        return;
    }

    auto bb = llvm::BasicBlock::Create(ctx.context, "entry", func);
    auto end = llvm::BasicBlock::Create(ctx.context, "end");
    ctx.stack().funcEnd(end);
    ctx.builder.SetInsertPoint(bb);

    AstNode *param = node->funcDecl.signature->params;
    for (auto &arg : func->args()) {
        // Allocate a local variable for each argument
        auto binding = ctx.builder.CreateAlloca(
            arg.getType(), nullptr, param->funcParam.name);
        ctx.builder.CreateStore(&arg, binding);
        param->codegen = binding;
        param = param->next;
    }

    llvm::AllocaInst *result{nullptr};
    if (!typeIs(type->func.retType, Void)) {
        result = ctx.builder.CreateAlloca(
            ctx.getLLVMType(type->func.retType), nullptr, "res");
        ctx.stack().result(result);
    }

    ctx.stack().currentFunction(node);
    cxy::codegen(visitor, node->funcDecl.body);
    if (!ctx.unreachable)
        ctx.builder.CreateBr(end);
    ctx.unreachable = false;

    // generate return statement
    func->insert(func->end(), end);
    ctx.builder.SetInsertPoint(end);
    if (result) {
        auto value =
            ctx.builder.CreateLoad(ctx.getLLVMType(type->func.retType), result);
        ctx.builder.CreateRet(value);
    }
    else {
        ctx.builder.CreateRetVoid();
    }

    llvm::verifyFunction(*func);
    ctx.returnValue(func);
}

void visitExternDecl(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    auto type = static_cast<llvm::FunctionType *>(node->type->codegen);
    if (type == nullptr) {
        node->codegen = generateFunctionProto(visitor, node->externDecl.func);
    }
    else {
        auto func =
            static_cast<llvm::Function *>(node->externDecl.func->codegen);
        csAssert0(func);

        node->codegen =
            llvm::Function::Create(type,
                                   llvm::GlobalValue::ExternalLinkage,
                                   func->getName(),
                                   &ctx.module());
    }
}

void visitStructDecl(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    ctx.getLLVMType(node->type);
}

void visitClassDecl(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    ctx.getLLVMType(node->type);
}

static void visitEnumDecl(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    auto option = node->enumDecl.options;
    for (; option; option = option->next) {
        option->codegen = cxy::codegen(visitor, option->enumOption.value);
    }
}

static void generateBackendCall(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    llvm::Value *value{nullptr};
    switch (node->backendCallExpr.func) {
    case bfiSizeOf: {
        const Type *type = node->backendCallExpr.args->type;
        csAssert0(typeIs(type, Info));
        type = resolveType(type);
        csAssert0(isClassType(type));
        value = llvm::ConstantInt::get(
            ctx.getLLVMType(node->type),
            ctx.module().getDataLayout().getTypeAllocSize(ctx.classType(type)));
        break;
    }
    }

    ctx.returnValue(value);
}

static void generateProgram(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    astVisitManyNodes(visitor, node->program.top);
    AstNode *decl = node->program.decls;
    for (; decl; decl = decl->next) {
        astVisit(visitor, decl);
    }
}

llvm::Value *cxy::codegen(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    ctx.returnValue(nullptr);
    astVisit(visitor, node);
    return ctx.value();
}

AstNode *generateCode(CompilerDriver *driver, AstNode *node)
{
    auto backend = static_cast<cxy::LLVMBackend *>(driver->backend);
    csAssert0(driver->backend);

    auto context = cxy::LLVMContext(backend->context(),
                                    driver,
                                    node->metadata.filePath,
                                    backend->getTargetMachine());

    // clang-format off
    AstVisitor visitor = makeAstVisitor(&context, {
        [astProgram] = generateProgram,
        [astBackendCall] = generateBackendCall,
        [astIdentifier] = visitIdentifierExpr,
        [astIntegerLit] = visitIntegerLit,
        [astNullLit] = visitNullLit,
        [astBoolLit] = visitBoolLit,
        [astCharLit] = visitCharLit,
        [astFloatLit] = visitFloatLit,
        [astStringLit] = visitStringLit,
        [astArrayExpr] = generateArrayExpr,
        [astBinaryExpr] = generateBinaryExpr,
        [astAssignExpr] = visitAssignExpr,
        [astTernaryExpr] = visitTernaryExpr,
        [astUnaryExpr] = visitUnaryExpr,
        [astTypedExpr] = visitTypedExpr,
        [astCallExpr] = visitCallExpr,
        [astCastExpr] = visitCastExpr,
        [astMemberExpr] = visitMemberExpr,
        [astAddressOf] = visitAddrOfExpr,
        [astIndexExpr] = visitIndexExpr,
        [astStructExpr] = visitStructExpr,
        [astTupleExpr] = visitTupleExpr,
        [astUnionValueExpr] = visitUnionValue,
        [astReturnStmt] = visitReturnStmt,
        [astIfStmt] = visitIfStmt,
        [astWhileStmt] = visitWhileStmt,
        [astSwitchStmt] = visitSwitchStmt,
        [astMatchStmt] = visitMatchStmt,
        [astContinueStmt] = visitContinueStmt,
        [astBreakStmt] = visitBreakStmt,
        [astVarDecl] = visitVariableDecl,
        [astFuncDecl] = visitFuncDecl,
        [astExternDecl] = visitExternDecl,
        [astStructDecl] = visitStructDecl,
        [astClassDecl] = visitClassDecl,
        [astEnumDecl] = visitEnumDecl,
        [astGenericDecl] = astVisitSkip,
        [astImportDecl] = astVisitSkip,
    }, .fallback = astVisitFallbackVisitAll, .dispatch = dispatch);
    // clang-format on

    astVisit(&visitor, node->metadata.node);

    if (!hasErrors(driver->L)) {
        if (!backend->addModule(context.moveModule()))
            logError(
                driver->L, &node->loc, "module verification failed\n", nullptr);
        return node;
    }
    return node;
}
