//
// Created by Carter Mbotho on 2024-04-27.
//

#pragma once

extern "C" {
#include "driver/driver.h"
#undef make
}

#include <clang/Basic/TargetInfo.h>
#include <clang/Frontend/CompilerInstance.h>
#include <llvm/ADT/DenseMap.h>

#include <set>
#include <unordered_map>

namespace cxy {

class CImporter {
public:
    AstNode *find(clang::StringRef path)
    {
        auto module = modules.find(path);
        return module == modules.end() ? nullptr : module->second;
    }

    void add(clang::StringRef path, AstNode *module)
    {
        modules.try_emplace(path, module);
    }

private:
    llvm::DenseMap<clang::StringRef, AstNode *> modules{};
};

} // namespace cxy
