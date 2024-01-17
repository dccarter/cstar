//
// Created by Carter Mbotho on 2024-01-09.
//

#include "context.h"

extern "C" {
#include "lang/frontend/ttable.h"
#include "lang/frontend/visitor.h"
}

LLVMContext::LLVMContext(CompilerDriver *driver, const char *fname)
{
    this->L = driver->L;
    this->types = driver->typeTable;
    this->pool = &driver->pool;
    this->strings = &driver->strPool;

    _context = std::make_unique<llvm::LLVMContext>();
    _module = std::make_unique<llvm::Module>(fname, *_context);
    _builder = std::make_unique<llvm::IRBuilder<>>(*_context);

    updateType(types->primitiveTypes[prtBool],
               llvm::Type::getInt1Ty(*_context));
    updateType(types->primitiveTypes[prtI8], llvm::Type::getInt8Ty(*_context));
    updateType(types->primitiveTypes[prtI16],
               llvm::Type::getInt16Ty(*_context));
    updateType(types->primitiveTypes[prtI32],
               llvm::Type::getInt32Ty(*_context));
    updateType(types->primitiveTypes[prtI64],
               llvm::Type::getInt64Ty(*_context));
    updateType(types->primitiveTypes[prtU8], llvm::Type::getInt8Ty(*_context));
    updateType(types->primitiveTypes[prtU16],
               llvm::Type::getInt16Ty(*_context));
    updateType(types->primitiveTypes[prtU32],
               llvm::Type::getInt32Ty(*_context));
    updateType(types->primitiveTypes[prtU64],
               llvm::Type::getInt64Ty(*_context));
    updateType(types->primitiveTypes[prtCChar],
               llvm::Type::getInt8Ty(*_context));
    updateType(types->primitiveTypes[prtChar],
               llvm::Type::getInt32Ty(*_context));
    updateType(types->primitiveTypes[prtF32],
               llvm::Type::getFloatTy(*_context));
    updateType(types->primitiveTypes[prtF64],
               llvm::Type::getDoubleTy(*_context));
    updateType(types->stringType,
               llvm::Type::getInt8Ty(*_context)->getPointerTo());
    updateType(types->voidType, llvm::Type::getVoidTy(*_context));
}

void LLVMContext::dumpIR() { _module->print(llvm::errs(), nullptr); }

void LLVMContext::dumpIR(std::string fname)
{
    std::error_code ec;
    llvm::raw_fd_ostream os(fname, ec);
    _module->print(os, nullptr);
    os.close();
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
    case typPointer:
        return getLLVMType(type->pointer.pointed)->getPointerTo();
    case typArray:
        return llvm::ArrayType::get(getLLVMType(type->array.elementType),
                                    type->array.len);
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

llvm::AllocaInst *LLVMContext::createStackVariable(const Type *type,
                                                   const char *name)
{
    auto func = builder().GetInsertBlock()->getParent();
    llvm::IRBuilder<> tmpBuilder(&func->getEntryBlock(),
                                 func->getEntryBlock().begin());
    return tmpBuilder.CreateAlloca(getLLVMType(type), nullptr, name);
}