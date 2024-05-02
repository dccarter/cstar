//
// Created by Carter Mbotho on 2024-04-29.
//

#include "context.hpp"
#include "import.hpp"

#include "lang/frontend/flag.h"
#include "lang/frontend/ttable.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace cxy {

IncludeContext::IncludeContext(CompilerDriver *driver,
                               clang::CompilerInstance &ci)
    : L{driver->L}, pool{driver->pool}, strings{driver->strings},
      types{driver->types}, preprocessor{&driver->preprocessor},
      target{ci.getTarget()}, SM{ci.getSourceManager()},
      importer{*((cxy::CImporter *)driver->cImporter)}
{
}

AstNodeList *IncludeContext::enterFile(clang::StringRef path)
{
    auto module = modules.find(path);
    if (module == modules.end()) {
        modules.insert({path, AstNodeList{}});
        return &modules[path];
    }
    else {
        return &module->second;
    }
}

void IncludeContext::addDeclaration(AstNode *node)
{
    if (node == nullptr)
        return;

    auto path = clang::StringRef(node->loc.fileName ?: "__c_builtins.cxy");
    if (auto module = enterFile(path)) {
        insertAstNode(module, node);
    }
}

AstNode *IncludeContext::buildModules(llvm::StringRef mainModulePath)
{
    AstNode *mainModule = nullptr;
    for (auto &[path, nodes] : modules) {
        FileLoc moduleLoc = {
            .fileName = makeStringSized(strings, path.data(), path.size()),
            .begin = {.row = 1, .col = 1, .byteOffset = 0},
            .end = {.row = 1, .col = 1, .byteOffset = 0}};
        auto name = fs::path(path.data()).stem().string();
        auto program = makeProgramAstNode(
            pool,
            &moduleLoc,
            flgImportedModule | flgNative,
            makeModuleAstNode(
                pool,
                &loc,
                flgNative,
                makeStringSized(strings, name.data(), name.size()),
                nullptr,
                nullptr),
            NULL,
            nodes.first,
            NULL);

        buildModuleType(types, program, false);
        importer.add(path, program);
        if (path == mainModulePath)
            mainModule = program;
    }

    return mainModule;
}

} // namespace cxy
