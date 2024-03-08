//
// Created by Carter Mbotho on 2024-02-09.
//
#include "llvm.h"

#include <llvm/CodeGen/CommandFlags.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Linker/Linker.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/TargetParser/Host.h>

extern "C" {
#include "lang/frontend/ttable.h"
}

namespace {
llvm::TargetMachine *createTargetMachine(Log *L)
{
    llvm::Triple triple{llvm::sys::getDefaultTargetTriple()};

    //    llvm::TargetOptions targetOptions =
    //        llvm::codegen::InitTargetOptionsFromCodeGenFlags(triple);
    //    std::string cpuStr = llvm::codegen::getCPUStr();
    //    std::string featureStr = llvm::codegen::getFeaturesStr();

    std::string error;
    const llvm::Target *target =
        llvm::TargetRegistry::lookupTarget(triple.getTriple(), error);

    if (!target) {
        logError(L, nullptr, "{s}", (FormatArg[]){{.s = error.c_str()}});
        return nullptr;
    }

    llvm::TargetMachine *TM = target->createTargetMachine(
        triple.getTriple(),
        "generic",
        "",
        llvm::TargetOptions{},
        std::optional<llvm::Reloc::Model>(llvm::codegen::getRelocModel()));
    return TM;
}

} // namespace

namespace cxy {

LLVMBackend::LLVMBackend(CompilerDriver *driver, llvm::TargetMachine *TM)
    : _context{std::make_shared<llvm::LLVMContext>()}, driver{driver}, TM{TM}
{
    auto types = driver->types;

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
    updateType(types->nullType,
               llvm::Type::getVoidTy(*_context)->getPointerTo());
}

bool LLVMBackend::addModule(std::unique_ptr<llvm::Module> module)
{
    csAssert0(module);
    if (not llvm::verifyModule(*module, &llvm::errs())) {
        modules.emplace_back(std::move(module));
        return true;
    }
    module->print(llvm::errs(), nullptr);
    return false;
}

bool LLVMBackend::linkModules()
{
    if (_linkedModule)
        return true;

    _linkedModule = std::make_unique<llvm::Module>("", *_context);
    llvm::Linker linker(*_linkedModule);

    for (auto &module : modules) {
        bool error = linker.linkInModule(std::move(module));
        if (error) {
            logError(driver->L, nullptr, "linking modules failed", nullptr);
            _linkedModule = nullptr;
            return false;
        }
    }
    modules.clear();

    return true;
}

void LLVMBackend::dumpMainModuleIR(cstring output_path)
{
    if (!linkModules())
        return;

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
        _linkedModule->print(fs, nullptr);
    }
    else

        _linkedModule->print(llvm::outs(), nullptr);
}

} // namespace cxy

void *initCompilerBackend(CompilerDriver *driver, int argc, char **argv)
{
    llvm::InitLLVM X(argc, argv);
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmPrinters();
    llvm::InitializeAllAsmParsers();

    llvm::TargetMachine *TM = createTargetMachine(driver->L);
    if (!TM) {
        return nullptr;
    }

    auto backend = new cxy::LLVMBackend(driver, TM);
    return backend;
}

void deinitCompilerBackend(CompilerDriver *driver)
{
    if (driver->backend == nullptr)
        return;
    auto backend = static_cast<cxy::LLVMBackend *>(driver->backend);
    // delete backend;
    driver->backend = nullptr;
}

extern "C" AstNode *backendDumpIR(CompilerDriver *driver, AstNode *node)
{
    auto backend = static_cast<cxy::LLVMBackend *>(driver->backend);
    csAssert0(backend);
    backend->dumpMainModuleIR(driver->options.output);
    return node;
}