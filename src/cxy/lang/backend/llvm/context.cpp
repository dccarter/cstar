//
// Created by Carter Mbotho on 2024-01-09.
//

#include "context.h"
#include "debug.h"
#include "llvm.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/visitor.h"
#include "lang/middle/builtins.h"

namespace cxy {
LLVMContext::LLVMContext(llvm::LLVMContext &context,
                         CompilerDriver *driver,
                         const char *fileName,
                         llvm::TargetMachine *TM)
    : context{context}, L{driver->L}, types{driver->types},
      strings{driver->strings}, pool{driver->pool}, _sourceFilename{fileName},
      builder{context}
{
    _module = std::make_unique<llvm::Module>(fileName, context);
    _module->setTargetTriple(TM->getTargetTriple().getTriple());
    _module->setDataLayout(TM->createDataLayout());
    /* Skip generating DI for builtins */
    if (isBuiltinsInitialized() && driver->options.debug) {
        _debugCtx.reset(new DebugContext(*_module, driver, _sourceFilename));
    }
}

LLVMContext &LLVMContext::from(AstVisitor *visitor)
{
    auto ctx = static_cast<LLVMContext *>(getAstVisitorContext(visitor));
    return *ctx;
}

llvm::Type *LLVMContext::convertToLLVMType(const Type *type)
{
    switch (type->tag) {
    case typPrimitive:
    case typVoid:
    case typString:
        unreachable("Already converted in constructor");
        break;
    case typEnum:
        return getLLVMType(type->tEnum.base);
    case typPointer: {
        auto typ = getLLVMType(type->pointer.pointed);
        return typ->getPointerTo();
    }
    case typArray:
        return llvm::ArrayType::get(getLLVMType(type->array.elementType),
                                    type->array.len);
    case typThis:
        return getLLVMType(type->_this.that);
    case typTuple:
        return createTupleType(type);
    case typFunc:
        return createFunctionType(type);
    case typUnion:
        return createUnionType(type);
    case typClass:
        return createClassType(type);
    case typStruct:
        return createStructType(type);
    case typInterface:
        return createInterfaceType(type);
    case typWrapped: {
        auto unwrapped = unwrapType(type, NULL);
        return getLLVMType(unwrapped);
    }
    default:
        return nullptr;
    }
}

llvm::Type *LLVMContext::getLLVMType(const Type *type)
{
    type = unwrapType(type, nullptr);
    csAssert0(type != nullptr);

    return static_cast<llvm::Type *>(
        type->codegen ?: updateType(type, convertToLLVMType(type)));
}

llvm::Type *LLVMContext::classType(const Type *type)
{
    auto llvmType = getLLVMType(type);
    if (isClassType(type))
        llvmType = static_cast<llvm::Type *>(getTypeDecl(type)->codegen);
    return llvmType;
}

llvm::AllocaInst *LLVMContext::createStackVariable(llvm::Type *type,
                                                   const char *name)
{
    auto func = builder.GetInsertBlock()->getParent();
    llvm::IRBuilder<> tmpBuilder(&func->getEntryBlock(),
                                 func->getEntryBlock().begin());
    return tmpBuilder.CreateAlloca(type, nullptr, name);
}

llvm::Value *LLVMContext::createLoad(const Type *type, llvm::Value *value)
{
    if (!_stack.loadVariable())
        return value;
    else if (typeIs(type, Func))
        return builder.CreateLoad(getLLVMType(type)->getPointerTo(), value);
    else
        return builder.CreateLoad(getLLVMType(type), value);
}

llvm::Value *LLVMContext::getUnionTag(u32 tag, const llvm::Type *type)
{
    csAssert0(type->isStructTy() && type->getNumContainedTypes() > 0);
    auto tagType = type->getContainedType(0);
    return llvm::ConstantInt::get(tagType, tag);
}

std::string LLVMContext::makeTypeName(const AstNode *node)
{
    std::string mangled{};
    llvm::raw_string_ostream ss(mangled);
    makeTypeName(ss, node);
    return mangled;
}

void LLVMContext::makeTypeName(llvm::raw_string_ostream &ss,
                               const AstNode *node)
{
    if (node == nullptr)
        return;
    if (!hasFlag(node, Extern)) {
        makeTypeName(ss, node->parentScope);

        if (node->parentScope && node->parentScope->type) {
            ss << "_";
        }

        if (isMemberFunction(node))
            ss << node->funcDecl.name;
        else if (node->type) {
            ss << node->type->name;
        }
    }
    else {
        ss << node->type->name;
    }
}

std::string LLVMContext::makeTypeName(const Type *type, const char *alt)
{
    std::string str;
    llvm::raw_string_ostream ss{str};
    if (type->ns) {
        ss << type->ns << "_";
    }
    if (type->name)
        ss << type->name;
    else
        ss << alt << type->index;
    return str;
}

llvm::Type *LLVMContext::createTupleType(const Type *type)
{
    std::vector<llvm::Type *> members{};
    for (u64 i = 0; i < type->tuple.count; i++) {
        if (typeIs(type->tuple.members[i], Func))
            members.push_back(
                getLLVMType(type->tuple.members[i])->getPointerTo());
        else
            members.push_back(getLLVMType(type->tuple.members[i]));
    }

    auto structType = llvm::StructType::create(
        context, members, makeTypeName(type, "tuple."));

    return structType;
}

llvm::Type *LLVMContext::createClassType(const Type *type)
{
    csAssert0(type->tClass.decl);
    AstNode *node = type->tClass.decl;
    auto classType = llvm::StructType::create(context, makeTypeName(node));
    node->codegen = classType;
    updateType(type, classType->getPointerTo());

    std::vector<llvm::Type *> members{};
    const Type *base = type->tClass.inheritance->base;

    for (u64 i = 0; i < type->tClass.members->count; i++) {
        NamedTypeMember *member = &type->tClass.members->members[i];
        if (!nodeIs(member->decl, FieldDecl))
            continue;

        if (typeIs(member->type, Func))
            members.push_back(getLLVMType(member->type)->getPointerTo());
        else
            members.push_back(getLLVMType(member->type));
    }
    classType->setBody(members);

    return classType->getPointerTo();
}

llvm::Type *LLVMContext::createStructType(const Type *type)
{
    std::vector<llvm::Type *> members{};
    AstNode *node = type->tStruct.decl;
    llvm::StructType *structType = nullptr;
    if (type->tStruct.decl == nullptr) {
        structType =
            llvm::StructType::create(context, makeTypeName(type, "Struct."));
    }
    else {
        structType =
            llvm::StructType::create(context, makeTypeName(type->tStruct.decl));
    }
    node->codegen = structType;
    updateType(type, structType);

    for (u64 i = 0; i < type->tStruct.members->count; i++) {
        NamedTypeMember *member = &type->tStruct.members->members[i];
        if (!nodeIs(member->decl, FieldDecl))
            continue;

        if (typeIs(member->type, Func))
            members.push_back(getLLVMType(member->type)->getPointerTo());
        else
            members.push_back(getLLVMType(member->type));
    }

    structType->setBody(members);
    return structType;
}

llvm::Type *LLVMContext::createInterfaceType(const Type *type)
{
    csAssert0(type->tInterface.decl);
    AstNode *node = type->tInterface.decl;
    auto interfaceType = llvm::StructType::create(context, makeTypeName(node));
    node->codegen = interfaceType;
    updateType(type, interfaceType->getPointerTo());

    std::vector<llvm::Type *> members{};
    for (u64 i = 0; i < type->tInterface.members->count; i++) {
        NamedTypeMember *member = &type->tInterface.members->members[i];
        if (!nodeIs(member->decl, FuncDecl))
            continue;

        members.push_back(getLLVMType(member->type)->getPointerTo());
    }

    interfaceType->setBody(members);
    return interfaceType->getPointerTo();
}

llvm::Type *LLVMContext::createFunctionType(const Type *type)
{
    std::vector<llvm::Type *> params;
    if (nodeIs(type->func.decl, FuncDecl) && type->func.decl->funcDecl.this_)
        params.push_back(getLLVMType(type->func.decl->funcDecl.this_->type));

    for (u64 i = 0; i < type->func.paramsCount; i++) {
        params.push_back(getLLVMType(type->func.params[i]));
    }

    return llvm::FunctionType::get(
        getLLVMType(type->func.retType), params, false);
}

llvm::Type *LLVMContext::createUnionType(const Type *type)
{
    u64 maxSize = 0, alignment = 1;
    auto str = makeTypeName(type, "Union.");
    for (u64 i = 0; i < type->tUnion.count; i++) {
        auto member = getLLVMType(type->tUnion.members[i].type);
        auto size = module().getDataLayout().getTypeAllocSize(member);
        if (size >= maxSize) {
            maxSize = size;
            alignment =
                module().getDataLayout().getABITypeAlign(member).value();
        }
    }

    llvm::Type *tag = nullptr;
    if (alignment == 1)
        tag = llvm::Type::getInt8Ty(context);
    else if (alignment == 2)
        tag = llvm::Type::getInt16Ty(context);
    else if (alignment == 4)
        tag = llvm::Type::getInt32Ty(context);
    else
        tag = llvm::Type::getInt64Ty(context);

    ((Type *)type)->tUnion.codegenTag = tag;
    auto unionType = llvm::StructType::create(
        context,
        {tag, llvm::ArrayType::get(llvm::Type::getInt8Ty(context), maxSize)},
        str);

    for (u64 i = 0; i < type->tUnion.count; i++) {
        auto member = getLLVMType(type->tUnion.members[i].type);
        type->tUnion.members[i].codegen = llvm::StructType::create(
            context, {tag, member}, str + "." + std::to_string(i));
    }

    return unionType;
}

llvm::Value *LLVMContext::castFromUnion(AstVisitor *visitor,
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

llvm::Value *LLVMContext::generateCastExpr(AstVisitor *visitor,
                                           const Type *to,
                                           AstNode *expr,
                                           u64 idx)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    auto from = unwrapType(expr->type, nullptr);

    to = unThisType(resolveType(to));
    if (to == unThisType(from))
        return cxy::codegen(visitor, expr);

    if (typeIs(from, Union))
        return castFromUnion(visitor, to, expr, idx);

    if (isFloatType(to)) {
        auto value = cxy::codegen(visitor, expr);
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
        auto value = cxy::codegen(visitor, expr);
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
            auto value = cxy::codegen(visitor, expr);
            csAssert0(from->primitive.id == prtU64);
            return ctx.builder.CreateIntToPtr(value, ctx.getLLVMType(to));
        }
        else if (isArrayType(from)) {
            auto load = ctx.stack().loadVariable(false);
            auto value = cxy::codegen(visitor, expr);
            ctx.stack().loadVariable(load);

            return ctx.builder.CreateInBoundsGEP(
                ctx.getLLVMType(from), value, {ctx.builder.getInt64(0)});
        }
        else {
            csAssert0(isPointerType(from));
            auto value = cxy::codegen(visitor, expr);
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
    else if (typeIsBaseOf(to, from)) {
        auto value = cxy::codegen(visitor, expr);
        return ctx.builder.CreatePointerCast(value, ctx.getLLVMType(to));
    }
    else if (isClassType(to)) {
        if (typeIs(from, Pointer) && typeIs(from->pointer.pointed, Null)) {
            auto value = cxy::codegen(visitor, expr);
            return ctx.builder.CreatePointerCast(value, ctx.getLLVMType(to));
        }
    }
    else if (from->tag == to->tag) {
        return cxy::codegen(visitor, expr);
    }

    unreachable("Unsupported cast");
}

llvm::Value *LLVMContext::withDebugLoc(const AstNode *node, llvm::Value *value)
{
    if (auto dbgContext = debugCtx()) {
        dbgContext->emitDebugLoc(node, value);
    }
    return value;
}

void LLVMContext::emitDebugLocation(const AstNode *node)
{
    if (_debugCtx == nullptr)
        return;
    if (node == nullptr) {
        builder.SetCurrentDebugLocation(llvm::DebugLoc{});
    }
    else {
        builder.SetCurrentDebugLocation(_debugCtx->getDebugLoc(node));
    }
}

} // namespace cxy