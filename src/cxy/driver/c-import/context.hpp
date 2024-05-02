//
// Created by Carter Mbotho on 2024-04-29.
//

#pragma once

#include "driver/driver.h"

#include <clang/Frontend/CompilerInstance.h>
#include <llvm/ADT/StringRef.h>

namespace cxy {
class CImporter;

struct IncludeContext {
    IncludeContext(CompilerDriver *driver, clang::CompilerInstance &ci);

    void addDeclaration(AstNode *node);
    AstNode *buildModules(llvm::StringRef mainModulePath);

    Log *L{nullptr};
    MemPool *pool{nullptr};
    StrPool *strings{nullptr};
    TypeTable *types{nullptr};
    CompilerPreprocessor *preprocessor{nullptr};
    clang::TargetInfo &target;
    clang::SourceManager &SM;
    llvm::DenseMap<llvm::StringRef, AstNodeList> modules{};
    cxy::CImporter &importer;
    FileLoc loc{};

private:
    AstNodeList *enterFile(clang::StringRef path);
};
} // namespace cxy
