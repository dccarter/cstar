//
// Created by Carter Mbotho on 2024-01-09.
//

#include "context.h"
#include "debug.h"
#include "llvm.h"

#include <llvm/IR/Attributes.h>
#include <llvm/IR/Verifier.h>

#include <llvm/IR/InlineAsm.h>
#include <vector>

static void moveVariable(cxy::LLVMContext &context, AstNode *pNode);
extern "C" {
#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/visitor.h"
}

static llvm::GlobalValue::LinkageTypes getLinkageType(cxy::LLVMContext &ctx,
                                                      const AstNode *node)
{
    if (hasFlag(node, Public))
        return llvm::GlobalValue::ExternalLinkage;

    if (auto attr = findAttribute(node, S_linkage)) {
        auto linkage = getAttributeArgument(ctx.L, &node->loc, attr, 0);
        if (nodeIs(linkage, StringLit)) {
            if (linkage->stringLiteral.value == S_External) {
                return llvm::GlobalValue::ExternalLinkage;
            }
            else if (linkage->stringLiteral.value == S_Appending) {
                return llvm::GlobalValue::AppendingLinkage;
            }
        }
    }

    return llvm::GlobalValue::InternalLinkage;
}

static inline cstring getGlobalVariableSection(const AstNode *node)
{
    if (auto attr = findAttribute(node, S_section)) {
        auto section = getAttributeArgument(NULL, NULL, attr, 0);
        if (nodeIs(section, StringLit))
            return section->stringLiteral.value;
    }
    return nullptr;
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

    auto func = llvm::Function::Create(funcType,
                                       getLinkageType(ctx, node),
                                       ctx.makeTypeName(node),
                                       &ctx.module());

    cxy::updateType(type, funcType);
    node->codegen = func;

    param = node->funcDecl.signature->params;
    for (auto &arg : func->args()) {
        arg.setName(param->funcParam.name);
        param = param->next;
    }

    return func;
}

static void visitPrefixUnaryExpr(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    ctx.emitDebugLocation(node);

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
    node->codegen = value;
    switch (node->unaryExpr.op) {
    case opPlus:
    case opMove:
        break;
    case opMinus:
        if (isFloatType(node->type))
            value = ctx.builder.CreateFNeg(value);
        else
            value = ctx.builder.CreateNeg(value);
        break;
    case opNot:
    case opCompl:
        value = ctx.builder.CreateNot(value);
        break;
    default:
        logError(ctx.L,
                 &node->loc,
                 "unsupported prefix unary operator `{s}`",
                 (FormatArg[]){{.s = getUnaryOpString(node->unaryExpr.op)}});
        ctx.returnValue(nullptr);
        return;
    }

    ctx.returnValue(value);
}

static void visitNullLit(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    AstNode *parent = node->parentScope;
    ctx.emitDebugLocation(node);

    if (nodeIs(parent, AssignExpr)) {
        ctx.returnValue(llvm::ConstantPointerNull::get(
            llvm::dyn_cast<llvm::PointerType>(ctx.getLLVMType(parent->type))));
    }
    else if (nodeIs(parent, BinaryExpr)) {
        if (parent->binaryExpr.rhs == node) {
            auto lhs = parent->binaryExpr.lhs->type;
            auto type = ctx.getLLVMType(parent->binaryExpr.lhs->type);
            ctx.returnValue(llvm::ConstantPointerNull::get(
                llvm::dyn_cast<llvm::PointerType>(
                    typeIs(lhs, Func) ? type->getPointerTo() : type)));
        }
        else {
            ctx.returnValue(llvm::ConstantPointerNull::get(
                llvm::dyn_cast<llvm::PointerType>(
                    ctx.getLLVMType(parent->binaryExpr.rhs->type))));
        }
    }
    else {
        ctx.returnValue(llvm::ConstantPointerNull::get(
            llvm::dyn_cast<llvm::PointerType>(ctx.getLLVMType(node->type))));
    }
}

static void visitBoolLit(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    ctx.emitDebugLocation(node);
    ctx.returnValue(ctx.builder.getInt1(node->boolLiteral.value));
}

static void visitCharLit(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    ctx.emitDebugLocation(node);
    if (node->type->primitive.id == prtChar)
        ctx.returnValue(ctx.builder.getInt32(node->charLiteral.value));
    else
        ctx.returnValue(ctx.builder.getInt8(node->charLiteral.value));
}

static void visitIntegerLit(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    ctx.emitDebugLocation(node);
    ctx.returnValue(llvm::ConstantInt::get(ctx.getLLVMType(node->type),
                                           node->intLiteral.uValue,
                                           node->intLiteral.isNegative));
}

static void visitFloatLit(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    ctx.emitDebugLocation(node);
    ctx.returnValue(llvm::ConstantFP::get(
        ctx.context, llvm::APFloat(node->floatLiteral.value)));
}

static void visitStringLit(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    ctx.emitDebugLocation(node);
    ctx.returnValue(
        ctx.builder.CreateGlobalStringPtr(node->stringLiteral.value));
}

static void visitIdentifierExpr(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    auto target = node->ident.resolvesTo;
    auto value = static_cast<llvm::Value *>(target->codegen);
    ctx.emitDebugLocation(node);

    target = nodeIs(target, ExternDecl) ? target->externDecl.func : target;
    if (ctx.stack().loadVariable() && !nodeIs(target, FuncDecl)) {
        value = ctx.createLoad(node->type, value);
    }
    if (value == nullptr && hasFlag(target, Abstract))
        value = ctx.createUndefined(target->type);
    ctx.returnValue(value);
}

static void visitMemberExpr(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    auto target = node->memberExpr.target;
    auto type = target->type;
    ctx.emitDebugLocation(node);

    auto load =
        ctx.stack().loadVariable(typeIs(type, Pointer) || isClassType(type));
    auto value = cxy::codegen(visitor, target);
    ctx.stack().loadVariable(load);

    auto member = node->memberExpr.member;
    if (nodeIs(member, Identifier)) {
        auto resolvesTo = member->ident.resolvesTo;

        if (nodeIs(resolvesTo, FieldDecl)) {
            llvm::Type *llvmType = isClassType(type)
                                       ? ctx.classType(type)
                                       : ctx.getLLVMType(stripPointer(type));

            value = ctx.builder.CreateStructGEP(llvmType,
                                                value,
                                                resolvesTo->fieldExpr.index,
                                                resolvesTo->fieldExpr.name);
        }
        else if (nodeIs(resolvesTo, EnumOptionDecl)) {
            if (hasFlag(resolvesTo->type, Extern) &&
                resolvesTo->codegen == nullptr) //
            {
                resolvesTo->codegen =
                    cxy::codegen(visitor, resolvesTo->enumOption.value);
            }
            value = static_cast<llvm::ConstantInt *>(resolvesTo->codegen);
            ctx.returnValue(value);
            return;
        }
        else if (nodeIs(resolvesTo, FuncDecl)) {
            value = static_cast<llvm::Function *>(resolvesTo->codegen);
            csAssert0(value != NULL);
            ctx.returnValue(value);
            return;
        }
        else if (nodeIs(resolvesTo, ExternDecl)) {
            AstNode *func = resolvesTo->externDecl.func;
            if (hasFlag(func, Abstract)) {
                value = ctx.createUndefined(
                    ctx.getLLVMType(resolvesTo->type)->getPointerTo());
            }
            else {
                value = llvm::cast<llvm::Function>(
                            static_cast<llvm::Value *>(resolvesTo->codegen))
                            ?: generateFunctionProto(visitor, func);
            }
            ctx.returnValue(value);
            return;
        }
        else {
            printf("What the func\n");
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
    ctx.emitDebugLocation(node);

    auto value = ctx.generateCastExpr(
        visitor, to, node->castExpr.expr, node->castExpr.idx);
    ctx.returnValue(value);
}

static void visitAddrOfExpr(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    ctx.emitDebugLocation(node);

    AstNode *operand = node->unaryExpr.operand;
    if (!nodeIsLeftValue(operand)) {
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
    ctx.emitDebugLocation(node);

    auto type = unwrapType(node->indexExpr.target->type, nullptr);

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
    ctx.emitDebugLocation(node);

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
    ctx.emitDebugLocation(node);

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

    llvm::Value *value = NULL;
    auto lhs = node->binaryExpr.lhs->type;
    if (lhs != node->binaryExpr.rhs->type) {
        value = ctx.generateCastExpr(visitor, lhs, node->binaryExpr.rhs);
    }
    else {
        value = cxy::codegen(visitor, node->binaryExpr.rhs);
    }

    if (value == nullptr)
        return;
    ctx.builder.CreateStore(value, variable);
    ctx.returnValue(variable);
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
    ctx.emitDebugLocation(node);

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
    ctx.emitDebugLocation(node);

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
    auto load = ctx.stack().loadVariable(true);
    AstNode *arg = node->callExpr.args;
    for (; arg; arg = arg->next) {
        auto value = cxy::codegen(visitor, arg);
        if (value == nullptr)
            return;
        args.push_back(value);
    }
    ctx.stack().loadVariable(load);
    auto value = ctx.builder.CreateCall(funcType, callee, args);
    ctx.returnValue(value);
}

static void visitStructExpr(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    ctx.emitDebugLocation(node);
    if (node->structExpr.fields) {
        auto obj = ctx.createUndefined(node->type);

        auto field = node->structExpr.fields;
        for (u64 i = 0; field; field = field->next, i++) {
            auto value = cxy::codegen(visitor, field->fieldExpr.value);
            obj = ctx.builder.CreateInsertValue(obj, value, i);
        }
        ctx.returnValue(obj);
    }
    else if (nodeIs(node->parentScope, VarDecl)) {
        ctx.returnValue(ctx.createUndefined(node->type));
    }
    else {
        ctx.returnValue(ctx.createStackVariable(node->type));
    }
}

static void visitTupleExpr(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    ctx.emitDebugLocation(node);

    if (node->tupleExpr.elements) {
        auto obj = ctx.createUndefined(node->type);

        auto elem = node->tupleExpr.elements;
        for (u64 i = 0; elem; elem = elem->next, i++) {
            auto value = cxy::codegen(visitor, elem);
            obj = ctx.builder.CreateInsertValue(obj, value, i);
        }
        ctx.returnValue(obj);
    }
    else {
        ctx.returnValue(ctx.createUndefined(node->type));
    }
}

static void visitUnionValue(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    ctx.emitDebugLocation(node);

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

static void visitReturnStmt(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    ctx.emitDebugLocation(node);

    if (node->returnStmt.expr) {
        auto expr = node->returnStmt.expr, func = ctx.stack().currentFunction();
        auto funcReturnType = unwrapType(func->type->func.retType, nullptr);
        auto value = ctx.generateCastExpr(visitor, funcReturnType, expr);
        ctx.builder.CreateStore(
            value, static_cast<llvm::AllocaInst *>(ctx.stack().result()));
    }

    ctx.builder.CreateBr(
        static_cast<llvm::BasicBlock *>(ctx.stack().funcEnd()));
    ctx.unreachable = true;
}

static void visitIfStmt(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    ctx.emitDebugLocation(node);

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
    //    if (thenRetVal == nullptr)
    //        return;

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
        auto phi = builder.CreatePHI(thenRetVal->getType(), 2, "if.tmp");
        phi->addIncoming(thenRetVal, then);
        phi->addIncoming(otherwiseRetVal, otherwise);

        ctx.returnValue(phi);
    }
}

static void visitBreakStmt(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    ctx.emitDebugLocation(node);

    auto &builder = ctx.builder;
    csAssert0(ctx.stack().loopEnd());
    ctx.returnValue(builder.CreateBr(
        static_cast<llvm::BasicBlock *>(ctx.stack().loopEnd())));
    ctx.unreachable = true;
}

static void visitContinueStmt(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    ctx.emitDebugLocation(node);

    auto &builder = ctx.builder;
    csAssert0(ctx.stack().loopCondition());
    if (ctx.stack().loopUpdate()) {
        ctx.returnValue(builder.CreateBr(
            static_cast<llvm::BasicBlock *>(ctx.stack().loopUpdate())));
    }
    else {
        ctx.returnValue(builder.CreateBr(
            static_cast<llvm::BasicBlock *>(ctx.stack().loopCondition())));
    }
    ctx.unreachable = true;
}

static void visitWhileStmt(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    ctx.emitDebugLocation(node);

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
    // cond = builder.GetInsertBlock();

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
    ctx.emitDebugLocation(node);

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
        auto &lastBB = func->back();
        if (lastBB.empty() || !lastBB.back().isTerminator())
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
    ctx.emitDebugLocation(node);

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
        auto &lastBB = func->back();
        if (lastBB.empty() || !lastBB.back().isTerminator())
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

static void visitVariableDecl(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    if (hasFlag(node, TopLevelDecl)) {
        // global variable
        ctx.module().getOrInsertGlobal(node->varDecl.name,
                                       ctx.getLLVMType(node->type));
        auto var = ctx.module().getGlobalVariable(node->varDecl.name);
        var->setConstant(hasFlag(node, Const));
        var->setLinkage(getLinkageType(ctx, node));
        if (auto section = getGlobalVariableSection(node)) {
            var->setSection(section);
        }

        if (node->varDecl.init) {
            var->setExternallyInitialized(false);
            auto value = cxy::codegen(visitor, node->varDecl.init);
            var->setInitializer(llvm::dyn_cast<llvm::Constant>(value));
        }
        else if (!hasFlag(node, Extern)) {
            var->setExternallyInitialized(false);
            if (isPointerType(node->type) || isClassType(node->type))
                var->setInitializer(llvm::ConstantPointerNull::get(
                    llvm::dyn_cast<llvm::PointerType>(
                        ctx.getLLVMType(node->type))));
            else
                var->setInitializer(llvm::dyn_cast<llvm::Constant>(
                    ctx.createUndefined(node->type)));
        }
        else {
            var->setExternallyInitialized(true);
        }
        node->codegen = var;
        if (!hasFlag(node, DiDisable)) {
            if (auto debugCtx = ctx.debugCtx())
                debugCtx->emitGlobalVariable(node);
        }
        ctx.returnValue(var);
        return;
    }

    auto func = ctx.builder.GetInsertBlock()->getParent();
    llvm::IRBuilder<> tmpBuilder(&func->getEntryBlock(),
                                 func->getEntryBlock().begin());
    auto variable =
        ctx.createStackVariable(node->type, node->varDecl.name ?: "");
    node->codegen = variable;
    if (auto debugCtx = ctx.debugCtx())
        debugCtx->emitLocalVariable(node, ctx.builder.GetInsertBlock());

    ctx.emitDebugLocation(node);
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

static void visitInlineAssembly(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    std::string constraints{};
    llvm::raw_string_ostream ss(constraints);

    llvm::Type *outputType = nullptr;
    ctx.emitDebugLocation(node);

    if (node->inlineAssembly.outputs) {
        AstNode *output = node->inlineAssembly.outputs;
        llvm::SmallVector<llvm::Type *, 4> outputs{};
        for (; output; output = output->next) {
            ss << output->asmOperand.constraint;
            if (output->next)
                ss << ",";
            outputs.push_back(ctx.getLLVMType(output->type));
        }

        if (outputs.size() == 1) {
            outputType = outputs[0];
        }
        else {
            outputType = llvm::StructType::get(ctx.context, outputs);
        }
    }
    else {
        outputType = llvm::Type::getVoidTy(ctx.context);
    }

    llvm::SmallVector<llvm::Type *, 4> inputTypes;
    llvm::SmallVector<llvm::Value *, 4> inputValues;
    if (node->inlineAssembly.inputs) {
        if (!constraints.empty())
            ss << ",";

        AstNode *input = node->inlineAssembly.inputs;
        for (; input; input = input->next) {
            ss << input->asmOperand.constraint;
            if (input->next)
                ss << ",";
            inputValues.push_back(
                cxy::codegen(visitor, input->asmOperand.operand));
            inputTypes.push_back(ctx.getLLVMType(input->type));
        }
    }

    if (node->inlineAssembly.clobbers) {
        if (!constraints.empty())
            ss << ",";
        AstNode *clobber = node->inlineAssembly.clobbers;
        for (; clobber; clobber = clobber->next) {
            ss << "~{" << clobber->stringLiteral.value << "}";
            if (clobber->next)
                ss << ",";
        }
    }

    auto funcType = llvm::FunctionType::get(outputType, inputTypes, false);
    auto func = llvm::InlineAsm::get(
        funcType, node->inlineAssembly.text, constraints, false);
    auto value = ctx.builder.CreateCall(funcType, func, inputValues);
    if (node->inlineAssembly.outputs) {
        auto load = ctx.stack().loadVariable(false);
        AstNode *output = node->inlineAssembly.outputs;
        if (output->next == nullptr) {
            auto variable = cxy::codegen(visitor, output->asmOperand.operand);
            ctx.builder.CreateStore(value, variable);
            return;
        }

        for (u32 i = 1; output; output = output->next, i++) {
            auto variable = cxy::codegen(visitor, output->asmOperand.operand);
            auto extracted = ctx.builder.CreateExtractValue(value, {i});
            ctx.builder.CreateStore(extracted, variable);
        }
    }
}

static void visitFuncDecl(AstVisitor *visitor, AstNode *node)
{
    if (hasFlag(node, Abstract))
        return;

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

    if (auto debugCtx = ctx.debugCtx())
        debugCtx->emitFunctionDecl(node);

    if (findAttribute(node, S_inline) != nullptr)
        func->addFnAttr(llvm::Attribute::AlwaysInline);

    auto bb = llvm::BasicBlock::Create(ctx.context, "entry", func);
    auto end = llvm::BasicBlock::Create(ctx.context, "end");
    ctx.stack().funcEnd(end);
    ctx.builder.SetInsertPoint(bb);

    ctx.emitDebugLocation(nullptr);
    AstNode *param = node->funcDecl.signature->params;
    u64 index = 0;
    for (auto &arg : func->args()) {
        // Allocate a local variable for each argument
        auto binding =
            ctx.createStackVariable(arg.getType(), param->funcParam.name);

        param->codegen = binding;
        if (auto debugCtx = ctx.debugCtx()) {
            debugCtx->emitParamDecl(
                param, ++index, ctx.builder.GetInsertBlock());
        }

        ctx.builder.CreateStore(&arg, binding);

        param = param->next;
    }

    llvm::AllocaInst *result{nullptr};
    ctx.emitDebugLocation(node->funcDecl.body);
    if (!typeIs(type->func.retType, Void)) {
        result = ctx.createStackVariable(type->func.retType, "res");
        ctx.stack().result(result);
    }

    ctx.stack().currentFunction(node);

    auto value = cxy::codegen(visitor, node->funcDecl.body);
    if (result && !nodeIs(node->funcDecl.body, BlockStmt)) {
        ctx.builder.CreateStore(value, result);
    }

    if (!ctx.unreachable)
        ctx.builder.CreateBr(end);
    ctx.unreachable = false;

    if (func->back().getTerminator() == nullptr) {
        // terminate last block if not terminated
        ctx.builder.CreateBr(end);
    }

    // generate return statement
    func->insert(func->end(), end);
    ctx.builder.SetInsertPoint(end);
    if (result) {
        value =
            ctx.builder.CreateLoad(ctx.getLLVMType(type->func.retType), result);
        ctx.builder.CreateRet(value);
    }
    else {
        ctx.builder.CreateRetVoid();
    }

    if (auto debugCtx = ctx.debugCtx())
        debugCtx->sealFunctionDecl(node);

    llvm::verifyFunction(*func);
    ctx.returnValue(func);
}

static void visitExternDecl(AstVisitor *visitor, AstNode *node)
{
    if (hasFlag(node, Abstract)) {
        return;
    }

    auto target = node->externDecl.func;
    auto &ctx = cxy::LLVMContext::from(visitor);
    if (nodeIs(target, FuncDecl))
        node->codegen = generateFunctionProto(visitor, target);
    else {
        // global variable
        ctx.module().getOrInsertGlobal(target->varDecl.name,
                                       ctx.getLLVMType(target->type));
        auto var = ctx.module().getGlobalVariable(target->varDecl.name);
        var->setConstant(hasFlag(target, Const));
        node->codegen = var;
    }
}

static void visitStructDecl(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    ctx.getLLVMType(node->type);
}

static void visitClassDecl(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    ctx.getLLVMType(node->type);
}

static void visitInterfaceDecl(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    ctx.getLLVMType(node->type);
}

static void visitUnionDecl(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    auto load = ctx.stack().loadVariable(false);
    astVisitManyNodes(visitor, node->unionDecl.members);
    ctx.stack().loadVariable(load);
    node->codegen = ctx.getLLVMType(node->type);
}

static void visitTypeDecl(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    auto load = ctx.stack().loadVariable(false);
    astVisitManyNodes(visitor, node->typeDecl.aliased);
    ctx.stack().loadVariable(load);
    node->codegen = ctx.getLLVMType(node->type);
}

static void visitTypeRef(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    auto load = ctx.stack().loadVariable(false);
    astVisit(visitor, node->reference.target);
    ctx.stack().loadVariable(load);
    node->codegen = ctx.getLLVMType(node->type);
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
        if (isClassType(type))
            value = llvm::ConstantInt::get(
                ctx.getLLVMType(node->type),
                ctx.module().getDataLayout().getTypeAllocSize(
                    ctx.classType(type)));
        else
            value = llvm::ConstantInt::get(
                ctx.getLLVMType(node->type),
                ctx.module().getDataLayout().getTypeAllocSize(
                    ctx.getLLVMType(type)));
        break;
    }
    case bfiAlloca: {
        const Type *type = node->backendCallExpr.args->type;
        value = ctx.builder.CreateAlloca(ctx.getLLVMType(type));
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

    if (auto debugCtx = ctx.debugCtx())
        debugCtx->finalize();
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
        [astAsm] = visitInlineAssembly,
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
        [astInterfaceDecl] = visitInterfaceDecl,
        [astUnionDecl] = visitUnionDecl,
        [astTypeDecl] = visitTypeDecl,
        [astEnumDecl] = visitEnumDecl,
        [astTypeRef] = visitTypeRef,
        [astGenericDecl] = astVisitSkip,
        [astImportDecl] = astVisitSkip,
        [astMacroDecl] = astVisitSkip
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
