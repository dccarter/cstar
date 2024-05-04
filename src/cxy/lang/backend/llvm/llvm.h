//
// Created by Carter Mbotho on 2024-02-09.
//

#pragma once

#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/StandardInstrumentations.h>
#include <llvm/Transforms/IPO.h>

#include "driver/driver.h"

namespace cxy {

class LLVMBackend {
public:
    LLVMBackend(CompilerDriver *driver, llvm::TargetMachine *TM);

    bool addModule(std::unique_ptr<llvm::Module> module);
    llvm::LLVMContext &context() { return *_context; }
    void dumpMainModuleIR(cstring output_path);
    bool linkModules();
    bool makeExecutable();
    llvm::TargetMachine *getTargetMachine() { return TM; }

    ~LLVMBackend()
    {
        _linkedModule = nullptr;
        _context = nullptr;
    }

private:
    std::string getCCompiler();
    bool emitMachineCode(llvm::SmallString<128> outputPath,
                         llvm::CodeGenFileType fileType,
                         llvm::Reloc::Model model);
    bool moveFile(llvm::Twine src, llvm::Twine dst);
    bool linkGeneratedOutput(llvm::SmallString<128> generatedOutputPath);
    void optimizeModule(llvm::Module &module);

    std::unique_ptr<llvm::Module> _linkedModule{nullptr};
    std::vector<std::unique_ptr<llvm::Module>> modules{};
    std::shared_ptr<llvm::LLVMContext> _context{nullptr};
    CompilerDriver *driver{nullptr};
    llvm::TargetMachine *TM{nullptr};
};

static inline void *updateType(const Type *type, void *codegen)
{
    const_cast<Type *>(type)->codegen = codegen;
    return codegen;
}

static inline void *updateDebug(const Type *type, void *codegen)
{
    const_cast<Type *>(type)->dbg = codegen;
    return codegen;
}

} // namespace cxy