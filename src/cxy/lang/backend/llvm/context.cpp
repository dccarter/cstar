//
// Created by Carter Mbotho on 2024-01-09.
//

#include "context.h"
#include "llvm.h"

extern "C" {
#include "lang/frontend/flag.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/visitor.h"
}

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
        if (node->type) {
            ss << node->type->name;
        }
        if (isMemberFunction(node) && node->funcDecl.index)
            ss << node->funcDecl.index;
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
    std::vector<llvm::Type *> members{};
    for (u64 i = 0; i < type->tClass.members->count; i++) {
        NamedTypeMember *member = &type->tClass.members->members[i];
        if (!nodeIs(member->decl, FieldDecl))
            continue;

        if (typeIs(member->type, Func))
            members.push_back(getLLVMType(member->type)->getPointerTo());
        else
            members.push_back(getLLVMType(member->type));
    }

    csAssert0(type->tClass.decl);
    AstNode *node = type->tClass.decl;
    auto classType =
        llvm::StructType::create(context, members, makeTypeName(node));
    node->codegen = classType;
    return classType->getPointerTo();
}

llvm::Type *LLVMContext::createStructType(const Type *type)
{
    std::vector<llvm::Type *> members{};
    for (u64 i = 0; i < type->tStruct.members->count; i++) {
        NamedTypeMember *member = &type->tStruct.members->members[i];
        if (!nodeIs(member->decl, FieldDecl))
            continue;

        if (typeIs(member->type, Func))
            members.push_back(getLLVMType(member->type)->getPointerTo());
        else
            members.push_back(getLLVMType(member->type));
    }

    if (type->tStruct.decl == nullptr) {
        return llvm::StructType::create(
            context, members, makeTypeName(type, "Struct."));
    }
    else {
        return llvm::StructType::create(
            context, members, makeTypeName(type->tStruct.decl));
    }
}

llvm::Type *LLVMContext::createFunctionType(const Type *type)
{
    std::vector<llvm::Type *> params;
    if (type->func.decl && type->func.decl->funcDecl.this_)
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

void LLVMContext::addFunctionDecl(AstNode *decl, llvm::Function *func)
{
    if (decl == nullptr || _functions.contains(decl))
        return;

    _functions.try_emplace(decl, func);
}

} // namespace cxy