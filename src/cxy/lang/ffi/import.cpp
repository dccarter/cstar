//
// Created by Carter Mbotho on 2024-01-26.
//

extern "C" {
#include "driver/driver.h"
#include "lang/frontend/ast.h"
#include "lang/frontend/ttable.h"
#include "lang/frontend/types.h"

#undef make
#undef Pair
}

#include <clang/AST/Decl.h>
#include <clang/AST/DeclGroup.h>
#include <clang/AST/PrettyPrinter.h>
#include <clang/AST/Type.h>
#include <clang/Basic/Builtins.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Lex/HeaderSearch.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <clang/Parse/ParseAST.h>
#include <clang/Sema/Sema.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/Path.h>
#include <llvm/TargetParser/Host.h>

static clang::TargetInfo *targetInfo;

static const Type *getIntTypeByWidth(TypeTable *types,
                                     int widthInBits,
                                     bool asSigned)
{
    switch (widthInBits) {
    case 8:
        return asSigned ? getPrimitiveType(types, prtI8)
                        : getPrimitiveType(types, prtU8);
    case 16:
        return asSigned ? getPrimitiveType(types, prtI16)
                        : getPrimitiveType(types, prtU16);
    case 32:
        return asSigned ? getPrimitiveType(types, prtI32)
                        : getPrimitiveType(types, prtU32);
    case 64:
        return asSigned ? getPrimitiveType(types, prtI64)
                        : getPrimitiveType(types, prtU64);
    }
    unreachable("unsupported integer width");
}

static const Type *withFlags(TypeTable *types, const Type *type, u64 flags)
{
    if ((type->flags & flags) == flags)
        return type;
    return makeWrappedType(types, type, flags);
}

static const Type *toCx(TypeTable *types, const clang::BuiltinType &type)
{
    switch (type.getKind()) {
    case clang::BuiltinType::Void:
        return makeVoidType(types);
    case clang::BuiltinType::Bool:
        return getPrimitiveType(types, prtBool);
    case clang::BuiltinType::Char_S:
    case clang::BuiltinType::Char_U:
        return getPrimitiveType(types, prtCChar);
    case clang::BuiltinType::SChar:
    case clang::BuiltinType::UChar:
        return getPrimitiveType(types, prtChar);
    case clang::BuiltinType::Short:
        return getIntTypeByWidth(types, targetInfo->getShortWidth(), true);
    case clang::BuiltinType::UShort:
        return getIntTypeByWidth(types, targetInfo->getShortWidth(), false);
    case clang::BuiltinType::Int:
        return getPrimitiveType(types, prtI32);
    case clang::BuiltinType::UInt:
        return getPrimitiveType(types, prtU32);
    case clang::BuiltinType::Long:
        return getIntTypeByWidth(types, targetInfo->getLongWidth(), true);
    case clang::BuiltinType::ULong:
        return getIntTypeByWidth(types, targetInfo->getLongWidth(), false);
    case clang::BuiltinType::LongLong:
        return getIntTypeByWidth(types, targetInfo->getLongLongWidth(), true);
    case clang::BuiltinType::ULongLong:
        return getIntTypeByWidth(types, targetInfo->getLongLongWidth(), false);
    case clang::BuiltinType::Float:
        return getPrimitiveType(types, prtF32);
    case clang::BuiltinType::Double:
        return getPrimitiveType(types, prtF64);
    default:
        type.dump();
        llvm_unreachable("unsupported clang::BuiltinType");
    }
}

static const Type *toCxy(clang::QualType qualtype)
{
    auto &type = *qualtype.getTypePtr();
    switch (type.getTypeClass()) {
    case clang::Type::Builtin:
        return withFlags(types, toCxy(types, llvm::cast<clang::BuiltinType>(type));
    }
}

static std::vector<cstring> dynArrayToVector(DynArray *arr)
{
    std::vector<cstring> vec{};
    for (u64 i = 0; i < arr->size; i++)
        vec.push_back(dynArrayAt(cstring, arr, i));
    return vec;
}

bool importCHeader(CompilerDriver *driver,
                   cstring headerName,
                   AstNode *importDecl)
{
    clang::CompilerInstance ci;
    ci.createDiagnostics();
    auto &options = driver->options;

    clang::CompilerInvocation::CreateFromArgs(ci.getInvocation(),
                                              dynArrayToVector(&options.cflags),
                                              ci.getDiagnostics());

    std::shared_ptr<clang::TargetOptions> pto =
        std::make_shared<clang::TargetOptions>();
    pto->Triple = llvm::sys::getDefaultTargetTriple();
    targetInfo = clang::TargetInfo::CreateTargetInfo(ci.getDiagnostics(), pto);
    ci.setTarget(targetInfo);

    ci.createFileManager();
    ci.createSourceManager(ci.getFileManager());
    if (importDecl->loc.fileName) {
        ci.getHeaderSearchOpts().AddPath(
            llvm::sys::path::parent_path(importDecl->loc.fileName),
            clang::frontend::Quoted,
            false,
            true);
    }

    auto defines = dynArrayToVector(&options.defines);
    for (llvm::StringRef define : defines) {
        ci.getPreprocessorOpts().addMacroDef(define);
    }

    ci.createPreprocessor(clang::TU_Complete);
    auto &pp = ci.getPreprocessor();
    pp.getBuiltinInfo().initializeBuiltins(pp.getIdentifierTable(),
                                           pp.getLangOpts());

    clang::ConstSearchDirIterator *curDir = nullptr;
    clang::HeaderSearch &headerSearch =
        ci.getPreprocessor().getHeaderSearchInfo();
    auto fileEntry = headerSearch.LookupFile(headerName,
                                             {},
                                             false,
                                             nullptr,
                                             curDir,
                                             {},
                                             nullptr,
                                             nullptr,
                                             nullptr,
                                             nullptr,
                                             nullptr,
                                             nullptr);
    if (!fileEntry) {
        std::string searchDirs;
        for (auto searchDir = headerSearch.search_dir_begin(),
                  end = headerSearch.search_dir_end();
             searchDir != end;
             ++searchDir) {
            searchDirs += '\n';
            searchDirs += searchDir->getName();
        }
        logError(driver->L,
                 &importDecl->loc,
                 "couldn't find C header file '{s}' in the following locations",
                 (FormatArg[])({{.s = headerName}}));
        return false;
    }

    auto module = makeAstNode(driver->pool,
                              &importDecl->loc,
                              &(AstNode){.tag = astProgram, .program = {}});
    ci.setASTConsumer(
        std::make_unique<CToCxyConverter>(*module, ci.getSourceManager()));
    ci.createASTContext();
    ci.createSema(clang::TU_Complete, nullptr);
    pp.addPPCallbacks(std::make_unique<MacroImporter>(*module, ci));

    auto fileID = ci.getSourceManager().createFileID(
        *fileEntry, clang::SourceLocation(), clang::SrcMgr::C_System);
    ci.getSourceManager().setMainFileID(fileID);
    ci.getDiagnosticClient().BeginSourceFile(ci.getLangOpts(),
                                             &ci.getPreprocessor());
    clang::ParseAST(
        ci.getPreprocessor(), &ci.getASTConsumer(), ci.getASTContext());
    ci.getDiagnosticClient().EndSourceFile();
    ci.getDiagnosticClient().finish();

    if (ci.getDiagnosticClient().getNumErrors() > 0) {
        return false;
    }

    return true;
}
