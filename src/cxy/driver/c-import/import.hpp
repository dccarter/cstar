//
// Created by Carter Mbotho on 2024-04-27.
//

#pragma once

#include "driver/driver.h"

#include <clang/Basic/TargetInfo.h>
#include <llvm/ADT/DenseMap.h>

#include <set>
#include <unordered_map>

namespace cxy {
class CImporter {
public:
    const bool isAlpine{false};
    CImporter(bool isAlpine = false) : isAlpine(isAlpine) {}

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
    std::unordered_map<std::string_view, AstNode *> modules{};
};
} // namespace cxy
