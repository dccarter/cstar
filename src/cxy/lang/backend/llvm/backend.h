//
// Created by Carter Mbotho on 2024-01-25.
//

#pragma once

#include "context.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

class LLVMBackend {
public:
    LLVMBackend(CompilerDriver *driver);

    bool addModule(std::unique_ptr<llvm::Module> module);
    std::shared_ptr<llvm::LLVMContext> context() { return _context; }
    void dumpMainModuleIR(cstring output_path);

private:
    std::vector<std::unique_ptr<llvm::Module>> modules{};
    std::shared_ptr<llvm::LLVMContext> _context{nullptr};
    CompilerDriver *driver{nullptr};
};