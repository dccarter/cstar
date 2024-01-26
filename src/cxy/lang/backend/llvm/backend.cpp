//
// Created by Carter Mbotho on 2024-01-25.
//

#include "backend.h"

#include <llvm/IR/Verifier.h>

extern "C" {
#include "lang/frontend/ttable.h"
}

LLVMBackend::LLVMBackend(CompilerDriver *driver)
    : _context{std::make_shared<llvm::LLVMContext>()}, driver{driver}
{
    auto types = driver->typeTable;

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

bool LLVMBackend::addModule(std::unique_ptr<llvm::Module> module)
{
    csAssert0(module);
    if (not llvm::verifyModule(*module, &llvm::errs())) {
        modules.emplace_back(std::move(module));
        return true;
    }

    return false;
}

void LLVMBackend::dumpMainModuleIR(cstring output_path)
{
    auto &mainModule = modules.back();
    if (output_path) {
        std::error_code ec;
        llvm::raw_fd_stream fs(output_path, ec);
        if (ec) {
            logError(driver->L,
                     nullptr,
                     "error opening dump output file: {s}",
                     (FormatArg[]){{.s = ec.message().c_str()}});
            return;
        }
        mainModule->print(fs, nullptr);
    }
    else
        mainModule->print(llvm::outs(), nullptr);
}

void *initCompilerBackend(CompilerDriver *driver)
{
    auto backend = new LLVMBackend(driver);
    return backend;
}

AstNode *backendDumpIR(CompilerDriver *driver, AstNode *node)
{
    auto backend = static_cast<LLVMBackend *>(driver->backend);
    backend->dumpMainModuleIR(driver->options.output);
    return node;
}