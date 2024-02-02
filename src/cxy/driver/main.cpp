//
// Created by Carter Mbotho on 2024-02-01.
//

#include <llvm/Support/ErrorOr.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_os_ostream.h>

#include "driver.hpp"

int main(int argc, char *argv[])
{
    llvm::InitLLVM X(argc, argv);
    llvm::SmallVector<const char *, 256> args(argv + 1, argv + argc);

    cxy::Compiler compiler{};
    for (auto fileName : args) {
        compiler.compile(fileName);
    }

    return 0;
}