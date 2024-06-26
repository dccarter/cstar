//
// Created by Carter Mbotho on 2024-02-09.
//
#include "llvm.h"

#undef hasFlags

#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/CodeGen/CommandFlags.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/IRPrintingPasses.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Linker/Linker.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Scalar/GVN.h>

#include "lang/frontend/flag.h"
#include "lang/frontend/ttable.h"

namespace {

template <typename T>
static auto for_each(DynArray &arr, std::function<void(T &)> func)
{
    for (int i = 0; i < arr.size; i++) {
        func(dynArrayAt(T *, &arr, i));
    }
}

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

std::string getDsymutil()
{
    if (auto path = llvm::sys::findProgramByName("dsymutil")) {
        return std::move(*path);
    }
    if (auto llvmDIR = std::getenv("LLVM_PATH")) {
        return std::string{llvmDIR} + "/bin/dsymutil";
    }

    return "";
}

template <typename T>
struct AppendCommandLineComponentCtx {
    std::function<void(T &)> fn;
};

template <typename T>
static bool enumerate_hash(void *ctx, const void *value)
{
    auto context = static_cast<AppendCommandLineComponentCtx<T> *>(ctx);
    auto concrete = (T *)value;
    context->fn(*concrete);
    return true;
}

template <typename T>
static void for_each(HashTable &table, std::function<void(T &)> fn)
{
    auto ctx = AppendCommandLineComponentCtx<T>{fn};
    enumerateHashTable(&table, &ctx, enumerate_hash<T>, sizeof(T));
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

    // DI types
    updateDebug(types->primitiveTypes[prtBool],
                llvm::DIBasicType::get(*_context,
                                       llvm::dwarf::DW_TAG_base_type,
                                       types->primitiveTypes[prtBool]->name,
                                       1 /* SizeInBits */,
                                       0 /* AlignInBits */,
                                       llvm::dwarf::DW_ATE_signed,
                                       llvm::DINode::FlagZero));
    updateDebug(types->primitiveTypes[prtI8],
                llvm::DIBasicType::get(*_context,
                                       llvm::dwarf::DW_TAG_base_type,
                                       types->primitiveTypes[prtI8]->name,
                                       8 /* SizeInBits */,
                                       0 /* AlignInBits */,
                                       llvm::dwarf::DW_ATE_signed,
                                       llvm::DINode::FlagZero));
    updateDebug(types->primitiveTypes[prtI16],
                llvm::DIBasicType::get(*_context,
                                       llvm::dwarf::DW_TAG_base_type,
                                       types->primitiveTypes[prtI16]->name,
                                       16 /* SizeInBits */,
                                       0 /* AlignInBits */,
                                       llvm::dwarf::DW_ATE_signed,
                                       llvm::DINode::FlagZero));
    updateDebug(types->primitiveTypes[prtI32],
                llvm::DIBasicType::get(*_context,
                                       llvm::dwarf::DW_TAG_base_type,
                                       types->primitiveTypes[prtI32]->name,
                                       32 /* SizeInBits */,
                                       0 /* AlignInBits */,
                                       llvm::dwarf::DW_ATE_signed,
                                       llvm::DINode::FlagZero));
    updateDebug(types->primitiveTypes[prtI64],
                llvm::DIBasicType::get(*_context,
                                       llvm::dwarf::DW_TAG_base_type,
                                       types->primitiveTypes[prtI64]->name,
                                       64 /* SizeInBits */,
                                       0 /* AlignInBits */,
                                       llvm::dwarf::DW_ATE_signed,
                                       llvm::DINode::FlagZero));
    updateDebug(types->primitiveTypes[prtU8],
                llvm::DIBasicType::get(*_context,
                                       llvm::dwarf::DW_TAG_base_type,
                                       types->primitiveTypes[prtU8]->name,
                                       8 /* SizeInBits */,
                                       0 /* AlignInBits */,
                                       llvm::dwarf::DW_ATE_unsigned,
                                       llvm::DINode::FlagZero));
    updateDebug(types->primitiveTypes[prtU16],
                llvm::DIBasicType::get(*_context,
                                       llvm::dwarf::DW_TAG_base_type,
                                       types->primitiveTypes[prtU16]->name,
                                       16 /* SizeInBits */,
                                       0 /* AlignInBits */,
                                       llvm::dwarf::DW_ATE_unsigned,
                                       llvm::DINode::FlagZero));
    updateDebug(types->primitiveTypes[prtU32],
                llvm::DIBasicType::get(*_context,
                                       llvm::dwarf::DW_TAG_base_type,
                                       types->primitiveTypes[prtU32]->name,
                                       32 /* SizeInBits */,
                                       0 /* AlignInBits */,
                                       llvm::dwarf::DW_ATE_unsigned,
                                       llvm::DINode::FlagZero));
    updateDebug(types->primitiveTypes[prtU64],
                llvm::DIBasicType::get(*_context,
                                       llvm::dwarf::DW_TAG_base_type,
                                       types->primitiveTypes[prtU64]->name,
                                       64 /* SizeInBits */,
                                       0 /* AlignInBits */,
                                       llvm::dwarf::DW_ATE_unsigned,
                                       llvm::DINode::FlagZero));
    updateDebug(types->primitiveTypes[prtCChar],
                llvm::DIBasicType::get(*_context,
                                       llvm::dwarf::DW_TAG_base_type,
                                       types->primitiveTypes[prtCChar]->name,
                                       8 /* SizeInBits */,
                                       0 /* AlignInBits */,
                                       llvm::dwarf::DW_ATE_signed_char,
                                       llvm::DINode::FlagZero));
    updateDebug(types->primitiveTypes[prtChar],
                llvm::DIBasicType::get(*_context,
                                       llvm::dwarf::DW_TAG_base_type,
                                       types->primitiveTypes[prtChar]->name,
                                       32 /* SizeInBits */,
                                       0 /* AlignInBits */,
                                       llvm::dwarf::DW_ATE_unsigned_char,
                                       llvm::DINode::FlagZero));
    updateDebug(types->primitiveTypes[prtF32],
                llvm::DIBasicType::get(*_context,
                                       llvm::dwarf::DW_TAG_base_type,
                                       types->primitiveTypes[prtF32]->name,
                                       32 /* SizeInBits */,
                                       0 /* AlignInBits */,
                                       llvm::dwarf::DW_ATE_float,
                                       llvm::DINode::FlagZero));
    updateDebug(types->primitiveTypes[prtF64],
                llvm::DIBasicType::get(*_context,
                                       llvm::dwarf::DW_TAG_base_type,
                                       types->primitiveTypes[prtF64]->name,
                                       64 /* SizeInBits */,
                                       0 /* AlignInBits */,
                                       llvm::dwarf::DW_ATE_float,
                                       llvm::DINode::FlagZero));

    updateDebug(types->voidType,
                llvm::DIBasicType::get(*_context,
                                       llvm::dwarf::DW_TAG_base_type,
                                       types->voidType->name,
                                       0 /* SizeInBits */,
                                       0 /* AlignInBits */,
                                       llvm::dwarf::DW_ATE_address,
                                       llvm::DINode::FlagZero));
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

    optimizeModule(*_linkedModule);
    return true;
}

void LLVMBackend::optimizeModule(llvm::Module &module)
{
    // Register analysis passes used in these transform passes.

    Options &options = driver->options;

    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    llvm::PassInstrumentationCallbacks PIC;
    llvm::PrintPassOptions PrintPassOpts;
    PrintPassOpts.Verbose = options.debugPassManager;
    PrintPassOpts.SkipAnalyses = false;
    llvm::StandardInstrumentations SI(
        module.getContext(), options.debugPassManager, true, PrintPassOpts);
    SI.registerCallbacks(PIC, &MAM);

    llvm::PipelineTuningOptions PTO;
    std::optional<llvm::PGOOptions> P{std::nullopt};
    llvm::PassBuilder PB(TM, PTO, P, &PIC);
    // Load requested pass plugins and let them register pass builder callbacks
    for_each<cstring>(options.loadPassPlugins, [&](auto pluginName) {
        auto plugin = llvm::PassPlugin::Load(pluginName);
        if (!plugin) {
            logWarning(driver->L,
                       builtinLoc(),
                       "failed not load plugin {s}",
                       (FormatArg[]){{.s = pluginName}});
        }
        else {
            plugin->registerPassBuilderCallbacks(PB);
        }
    });

    FAM.registerPass([&] { return PB.buildDefaultAAPipeline(); });
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    PB.registerPipelineStartEPCallback(
        [](llvm::ModulePassManager &MPM, llvm::OptimizationLevel level) {});

    llvm::ModulePassManager MPM;

    if (options.passes) {
        if (auto error = PB.parsePassPipeline(MPM, options.passes)) {
            auto errMessage = toString(std::move(error));
            logError(driver->L, builtinLoc(), errMessage.c_str(), nullptr);
            return;
        }
    }
    else {
        llvm::StringRef defaultPass;
        switch (driver->options.optimizationLevel) {
        case O1:
            defaultPass = "default<O1>";
            break;
        case O2:
            defaultPass = "default<O2>";
            break;
        case O3:
            defaultPass = "default<O3>";
            break;
        case Os:
            defaultPass = "default<Os>";
            break;
        case O0:
            defaultPass = "default<O0>";
            break;
        }

        if (auto error = PB.parsePassPipeline(MPM, defaultPass)) {
            auto errMessage = toString(std::move(error));
            logError(driver->L, builtinLoc(), errMessage.c_str(), nullptr);
            return;
        }
    }

    MPM.run(module, MAM);
}

bool LLVMBackend::emitMachineCode(llvm::SmallString<128> outputPath,
                                  llvm::CodeGenFileType fileType,
                                  llvm::Reloc::Model model)
{
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
    const std::string &targetTriple = triple.str();
    _linkedModule->setTargetTriple(TM->getTargetTriple().getTriple());
    _linkedModule->setDataLayout(TM->createDataLayout());

    std::error_code error;
    llvm::raw_fd_ostream file(outputPath, error, llvm::sys::fs::OF_None);
    if (error) {
        logError(driver->L,
                 nullptr,
                 "error opening file {s}: {s}",
                 (FormatArg[]){{.s = outputPath.data()},
                               {.s = error.message().data()}});
        return false;
    }

    llvm::legacy::PassManager passManager;
    passManager.add(
        llvm::createTargetTransformInfoWrapperPass(TM->getTargetIRAnalysis()));

    if (TM->addPassesToEmitFile(passManager, file, nullptr, fileType)) {
        logError(driver->L,
                 nullptr,
                 "current LLVM target machine can't emit a file of this type",
                 nullptr);
        return false;
    }

    passManager.run(*_linkedModule);
    file.flush();
    return true;
}

bool LLVMBackend::linkGeneratedOutput(
    llvm::SmallString<128> generatedOutputPath)
{
    Options &options = driver->options;
    auto cc = getCCompiler();

    llvm::SmallString<128> temporaryExecutablePath;
    llvm::sys::fs::createUniquePath(
        "cxy-%%%%%%%%", temporaryExecutablePath, true);
    std::vector<const char *> ccArgs = {
        cc.data(),
        generatedOutputPath.c_str(),
    };

    for_each<cstring>(driver->nativeSources,
                      [&](auto source) { ccArgs.push_back(source); });

    ccArgs.push_back("-o");
    ccArgs.push_back(temporaryExecutablePath.c_str());
    for (int i = 0; i < options.cflags.size; i++) {
        ccArgs.push_back(dynArrayAt(const char **, &options.cflags, i));
    }

    for (int i = 0; i < options.librarySearchPaths.size; i++) {
        ccArgs.push_back("-L");
        ccArgs.push_back(
            dynArrayAt(const char **, &options.librarySearchPaths, i));
    }

    for (int i = 0; i < options.libraries.size; i++) {
        ccArgs.push_back("-l");
        ccArgs.push_back(dynArrayAt(const char **, &options.libraries, i));
    }

    for_each<cstring>(driver->linkLibraries, [&](auto lib) {
        ccArgs.push_back("-l");
        ccArgs.push_back(lib);
    });

    std::vector<llvm::StringRef> ccArgStringRefs(ccArgs.begin(), ccArgs.end());
    int ccExitStatus = llvm::sys::ExecuteAndWait(ccArgs[0], ccArgStringRefs);

    if (ccExitStatus == 0 && options.debug) {
        // generate debug symbols
        auto dsymutil = getDsymutil();
        std::vector<llvm::StringRef> dsymArgs{dsymutil.c_str(),
                                              temporaryExecutablePath.c_str()};
        ccExitStatus = llvm::sys::ExecuteAndWait(dsymArgs[0], dsymArgs);
    }

    if (auto error = llvm::sys::fs::remove(generatedOutputPath)) {
        logWarning(driver->L,
                   nullptr,
                   "removing temporary file '{s}' failed: {s}",
                   (FormatArg[]){{.s = generatedOutputPath.c_str()},
                                 {.s = error.message().c_str()}});
    }

    if (ccExitStatus != 0)
        return false;

    llvm::SmallString<128> outputFilePath{driver->options.buildDir};
    if (driver->options.output) {
        llvm::sys::path::append(outputFilePath,
                                llvm::Twine(driver->options.output));
    }
    else {
        llvm::sys::path::append(outputFilePath, llvm::Twine("app"));
    }

    if (options.debug) {
        llvm::SmallString<255> temporaryDebugSYMS{temporaryExecutablePath};
        llvm::SmallString<255> outputDebugSYMS{outputFilePath};
        temporaryDebugSYMS.append(".dSYM");
        outputDebugSYMS.append(".dSYM");
        // ignore errors
        moveDirectory(temporaryDebugSYMS, outputDebugSYMS);
    }

    return moveFile(temporaryExecutablePath, outputFilePath);
}

bool LLVMBackend::makeExecutable()
{
    if (!linkModules())
        return false;

    bool emitAssembly =
        (driver->options.cmd == cmdDev) && driver->options.dev.emitAssembly;
    bool compileOnly = false;

    llvm::SmallString<128> temporaryOutputFilePath;
    auto *outputFileExtension = emitAssembly ? "s" : "o";
    if (auto error = llvm::sys::fs::createTemporaryFile(
            "cxy", outputFileExtension, temporaryOutputFilePath)) {
        logError(driver->L,
                 nullptr,
                 "{s}",
                 (FormatArg[]){{.s = error.message().c_str()}});
        return false;
    }
#if LLVM_VERSION_MAJOR > 17
    auto fileType = driver->options.dev.emitAssembly
                        ? llvm::CodeGenFileType::AssemblyFile
                        : llvm::CodeGenFileType::ObjectFile;
#else
    auto fileType = driver->options.dev.emitAssembly ? llvm::CGFT_AssemblyFile
                                                     : llvm::CGFT_ObjectFile;
#endif

    auto relocationModel = driver->options.noPIE ? llvm::Reloc::Model::Static
                                                 : llvm::Reloc::Model::PIC_;
    if (!emitMachineCode(temporaryOutputFilePath, fileType, relocationModel))
        return false;

    if (driver->options.buildDir != nullptr) {
        auto error =
            llvm::sys::fs::create_directories(driver->options.buildDir);
        if (error) {
            logError(driver->L,
                     nullptr,
                     "creating build directory failed: {s}",
                     (FormatArg[]){{.s = error.message().c_str()}});
            return false;
        }
    }

    bool treatAsLibrary = hasFlag(driver->mainModule, Executable) &&
                          (driver->options.cmd != cmdRun);
    if (treatAsLibrary) {
        compileOnly = true;
    }

    if (compileOnly || emitAssembly) {
        llvm::SmallString<128> outputFilePath{driver->options.buildDir};
        if (driver->options.output) {
            if (llvm::sys::path::extension(driver->options.output).empty())
                llvm::sys::path::append(outputFilePath,
                                        llvm::Twine("output.") +
                                            outputFileExtension);
            else
                llvm::sys::path::append(outputFilePath,
                                        llvm::Twine(driver->options.output));
        }
        else {
            llvm::sys::path::append(outputFilePath,
                                    llvm::Twine("out.") + outputFileExtension);
        }

        return moveFile(temporaryOutputFilePath, outputFilePath);
    }

    if (!linkGeneratedOutput(temporaryOutputFilePath))
        return false;

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

std::string LLVMBackend::getCCompiler()
{
#ifdef _WIN32
    auto compilers = {"cl.exe", "clang-cl.exe"};
#else
    auto compilers = {"cc", "clang", "gcc"};
#endif
    for (const char *compiler : compilers) {
        if (auto path = llvm::sys::findProgramByName(compiler)) {
            return std::move(*path);
        }
    }
    return "";
}

bool LLVMBackend::moveFile(llvm::Twine src, llvm::Twine dst)
{
    if (auto error = llvm::sys::fs::copy_file(src, dst)) {
        logError(driver->L,
                 nullptr,
                 "error creating output file '{s}': {s}",
                 (FormatArg[]){{.s = src.str().data()},
                               {.s = error.message().data()}});
        return false;
    }

    if (auto error = llvm::sys::fs::remove(src)) {
        logWarning(driver->L,
                   nullptr,
                   "deleting temporary output file '{s}' failed: {s}",
                   (FormatArg[]){{.s = dst.str().data()},
                                 {.s = error.message().data()}});
    }
    return true;
}

bool LLVMBackend::moveDirectory(llvm::Twine src, llvm::Twine dst)
{
    // delete destination directory if it exists
    llvm::sys::fs::remove_directories(dst);
    if (auto error = llvm::sys::fs::copy_file(src, dst)) {
        logError(driver->L,
                 nullptr,
                 "error copying directory '{s}' to '{s}': {s}",
                 (FormatArg[]){{.s = src.str().data()},
                               {.s = dst.str().data()},
                               {.s = error.message().data()}});
        return false;
    }

    if (auto error = llvm::sys::fs::remove_directories(src)) {
        logWarning(driver->L,
                   nullptr,
                   "deleting directory '{s}' failed: {s}",
                   (FormatArg[]){{.s = dst.str().data()},
                                 {.s = error.message().data()}});
    }
    return true;
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
    driver->backend = nullptr;
    // delete backend;
}

bool compilerBackendMakeExecutable(CompilerDriver *driver)
{
    if (driver->backend == nullptr)
        return false;
    auto backend = static_cast<cxy::LLVMBackend *>(driver->backend);
    return backend->makeExecutable();
}

extern "C" AstNode *backendDumpIR(CompilerDriver *driver, AstNode *node)
{
    auto backend = static_cast<cxy::LLVMBackend *>(driver->backend);
    csAssert0(backend);
    backend->dumpMainModuleIR(driver->options.output);
    return node;
}