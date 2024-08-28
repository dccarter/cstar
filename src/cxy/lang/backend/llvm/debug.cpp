//
// Created by Carter Mbotho on 2024-05-02.
//

#include "debug.h"
#include "context.h"
#include "llvm.h"

#include <lang/frontend/flag.h>
#include <lang/frontend/ttable.h>

#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>

namespace cxy {

DebugContext::DebugContext(llvm::Module &module,
                           CompilerDriver *driver,
                           cstring fileName)
    : builder(module), module{module}, types{driver->types}
{
    llvm::DIFile *diFile = getFile(fileName);
    bool isOptimized = driver->options.optimizationLevel > O0;
    auto emissionKind = llvm::DICompileUnit::DebugEmissionKind::FullDebug;
    compileUnit =
        builder.createCompileUnit(llvm::dwarf::DW_LANG_C,
                                  diFile,
                                  "cxy-v" CXY_VERSION_STR /* Producer */,
                                  isOptimized,
                                  llvm::StringRef(),
                                  0 /* RV */,
                                  llvm::StringRef(),
                                  emissionKind);

    module.addModuleFlag(llvm::Module::Warning, "Dwarf Version", 2);

    module.addModuleFlag(llvm::Module::Error,
                         "Debug Info Version",
                         llvm::DEBUG_METADATA_VERSION);
}

llvm::DIType *DebugContext::getDIType(const Type *type)
{
    type = unwrapType(type, nullptr);
    csAssert0(type != nullptr);

    return static_cast<llvm::DIType *>(
        type->dbg ?: updateDebug(type, convertToDIType(type)));
}

llvm::Type *getLLVMType(const Type *type)
{
    csAssert0(type->codegen);
    return static_cast<llvm::Type *>(type->codegen);
}

llvm::DIType *DebugContext::convertToDIType(const Type *type)
{
    switch (type->tag) {
    case typPrimitive:
        unreachable("Already converted in constructor");
        break;
    case typVoid:
        return llvm::DIBasicType::get(
            module.getContext(), llvm::dwarf::DW_TAG_base_type, type->name, 0);
    case typString:
        return builder.createPointerType(
            getDIType(getPrimitiveType(types, prtChar)),
            module.getDataLayout()
                    .getABITypeAlign(getLLVMType(types->stringType))
                    .value() *
                8);
    case typEnum:
        return createEnumType(type);
    case typPointer:
        return pointerToDIType(type);
    case typReference:
        if (isClassReferenceType(type))
            return getDIType(stripReference(type));
        return pointerToDIType(type);
    case typArray:
        return arrayToDIType(type);
    case typThis:
        return getDIType(type->_this.that);
    case typTuple:
        return createTupleType(type);
    case typFunc:
        return createFunctionType(type);
    case typUnion:
        return createUnionType(type);
    case typUntaggedUnion:
        return createUntaggedUnionType(type);
    case typClass:
        return createClassType(type);
    case typStruct:
        return createStructType(type);
    default:
        return nullptr;
    }
}

llvm::DIType *DebugContext::pointerToDIType(const Type *type)
{
    return builder.createPointerType(
        getDIType(type->pointer.pointed),
        module.getDataLayout().getPointerSizeInBits());
}

llvm::DIType *DebugContext::arrayToDIType(const Type *type)
{
    llvm::SmallVector<llvm::Metadata *, 4> subscripts;
    subscripts.push_back(builder.getOrCreateSubrange(0, (i64)type->array.len));
    return builder.createArrayType(
        type->array.len,
        8 * module.getDataLayout()
                .getABITypeAlign((llvm::Type *)type->codegen)
                .value(),
        getDIType(type->array.elementType),
        builder.getOrCreateArray(subscripts));
}

llvm::DIType *DebugContext::createStructType(const Type *type)
{
    auto &layout = module.getDataLayout();
    auto structLayout = layout.getStructLayout(
        llvm::dyn_cast_or_null<llvm::StructType>(getLLVMType(type)));
    auto decl = type->tStruct.decl;

    auto diStructType = builder.createReplaceableCompositeType(
        llvm::dwarf::DW_TAG_structure_type,
        type->name,
        currentScope(),
        currentScope()->getFile(),
        decl->loc.begin.row);
    updateDebug(type, diStructType);

    std::vector<llvm::Metadata *> membersMetadata;
    addFields(membersMetadata, *structLayout, *type->tStruct.members);

    auto members = builder.getOrCreateArray(membersMetadata);
    auto ret =
        builder.createStructType(currentScope(),
                                 type->name,
                                 currentScope()->getFile(),
                                 type->tStruct.decl->loc.begin.row,
                                 structLayout->getSizeInBits(),
                                 structLayout->getAlignment().value() * 8,
                                 llvm::DINode::FlagZero,
                                 nullptr,
                                 members);

    return builder.replaceTemporary(llvm::TempMDNode(diStructType), ret);
}

llvm::DIType *DebugContext::createClassType(const Type *type)
{
    auto &layout = module.getDataLayout();
    auto decl = type->tClass.decl;
    auto classLayout =
        layout.getStructLayout(llvm::dyn_cast_or_null<llvm::StructType>(
            static_cast<llvm::Type *>(decl->codegen)));

    llvm::DIType *derivedFrom = nullptr;
    auto diClassType = builder.createReplaceableCompositeType(
        llvm::dwarf::DW_TAG_structure_type,
        type->name,
        currentScope(),
        currentScope()->getFile(),
        decl->loc.begin.row);
    updateDebug(type, diClassType);

    std::vector<llvm::Metadata *> membersMetadata;
    if (type->tClass.inheritance->base) {
        derivedFrom = getDIType(type->tClass.inheritance->base);
        auto fwd =
#if LLVM_VERSION_MAJOR > 17
            builder.createClassType(currentScope(),
                                    type->name,
                                    currentScope()->getFile(),
                                    decl->loc.begin.row,
                                    classLayout->getSizeInBits(),
                                    classLayout->getAlignment().value() * 8,
                                    0 /* OffsetInBits */,
                                    llvm::DINode::FlagFwdDecl,
                                    nullptr /* DerivedFrom */,
                                    nullptr /* Elements */,
                                    0 /* RunTimeLang */,
                                    nullptr /* VTableHolder */,
                                    nullptr /* TemplateParms */,
                                    llvm::StringRef() /* UniqueIdentifier */);
#else
            builder.createClassType(currentScope(),
                                    type->name,
                                    currentScope()->getFile(),
                                    decl->loc.begin.row,
                                    classLayout->getSizeInBits(),
                                    classLayout->getAlignment().value() * 8,
                                    0 /* OffsetInBits */,
                                    llvm::DINode::FlagFwdDecl,
                                    nullptr /* DerivedFrom */,
                                    nullptr /* Elements */,
                                    nullptr /* VTableHolder */,
                                    nullptr /* TemplateParms */,
                                    llvm::StringRef() /* UniqueIdentifier */);
#endif
        auto dt = builder.createInheritance(fwd,
                                            derivedFrom,
                                            0 /* BaseOffset */,
                                            0 /* VBPtrOffset */,
                                            llvm::DINode::FlagPublic);
        membersMetadata.push_back(dt);
    }
    addFields(membersMetadata, *classLayout, *type->tClass.members);

    auto ret =
#if LLVM_VERSION_MAJOR > 17
        builder.createClassType(currentScope(),
                                type->name,
                                currentScope()->getFile(),
                                decl->loc.begin.row,
                                classLayout->getSizeInBits(),
                                classLayout->getAlignment().value() * 8,
                                0 /* OffsetInBits */,
                                llvm::DINode::FlagZero,
                                derivedFrom,
                                builder.getOrCreateArray(membersMetadata),
                                0 /* RunTimeLang */,
                                nullptr /* VTableHolder  */,
                                nullptr /* TemplateParms */,
                                llvm::StringRef() /* UniqueIdentifier */);
#else
        builder.createClassType(currentScope(),
                                type->name,
                                currentScope()->getFile(),
                                decl->loc.begin.row,
                                classLayout->getSizeInBits(),
                                classLayout->getAlignment().value() * 8,
                                0 /* OffsetInBits */,
                                llvm::DINode::FlagZero,
                                derivedFrom,
                                builder.getOrCreateArray(membersMetadata),
                                nullptr /* VTableHolder  */,
                                nullptr /* TemplateParms */,
                                llvm::StringRef() /* UniqueIdentifier */);
#endif

    return builder.createPointerType(
        builder.replaceTemporary(llvm::TempMDNode(diClassType), ret),
        layout.getPointerSize());
}

llvm::DIType *DebugContext::createTupleType(const Type *type)
{
    auto &layout = module.getDataLayout();
    auto llvmType = getLLVMType(type);
    auto structLayout = layout.getStructLayout(
        llvm::dyn_cast_or_null<llvm::StructType>(llvmType));
    std::vector<llvm::Metadata *> membersMetadata;
    for (u64 i = 0; i < type->tuple.count; i++) {
        auto member = type->tuple.members[i];
        auto diType = getDIType(member);
        auto diTypeSize = diType->getSizeInBits();
        auto elLlvmType = getLLVMType(member);
        auto alignment = layout
                             .getABITypeAlign(typeIs(member, Func)
                                                  ? elLlvmType->getPointerTo()
                                                  : elLlvmType)
                             .value() *
                         8;

        membersMetadata.push_back(
            builder.createMemberType(currentScope(),
                                     std::to_string(i),
                                     currentScope()->getFile(),
                                     1 /* LineNo */,
                                     diTypeSize,
                                     alignment,
                                     structLayout->getElementOffsetInBits(i),
                                     llvm::DINode::FlagZero,
                                     diType));
    }

    auto members = builder.getOrCreateArray(membersMetadata);
    return builder.createStructType(currentScope(),
                                    llvmType->getStructName(),
                                    currentScope()->getFile(),
                                    0,
                                    structLayout->getSizeInBits(),
                                    structLayout->getAlignment().value() * 8,
                                    llvm::DINode::FlagZero,
                                    nullptr,
                                    members);
}

llvm::DIType *DebugContext::createUnionType(const Type *type)
{
    auto &layout = module.getDataLayout();
    auto llvmType = getLLVMType(type);
    auto unionLayout = layout.getStructLayout(
        llvm::dyn_cast_or_null<llvm::StructType>(llvmType));
    std::vector<llvm::Metadata *> metadata;

    for (u64 i = 0; i < type->tUnion.count; i++) {
        auto elLlvmType = getLLVMType(type->tUnion.members[i].type);
        auto diType = getDIType(type->tUnion.members[i].type);
        auto diTypeSize = layout.getTypeSizeInBits(elLlvmType);
        auto diTypeAlign = layout.getABITypeAlign(elLlvmType).value() * 8;
        metadata.push_back(builder.createMemberType(currentScope(),
                                                    std::to_string(i),
                                                    currentScope()->getFile(),
                                                    1 /* LineNo */,
                                                    diTypeSize,
                                                    diTypeAlign,
                                                    0 /* OffsetInBits */,
                                                    llvm::DINode::FlagZero,
                                                    diType));
        auto size = layout.getTypeAllocSize(elLlvmType);
    }

    auto unionType = llvmType->getContainedType(hasFlag(type, Native) ? 0 : 1);
    auto diUnionType =
        builder.createUnionType(currentScope(),
                                llvm::StringRef() /* Name */,
                                currentScope()->getFile(),
                                1 /* LineNumber */,
                                layout.getTypeSizeInBits(unionType),
                                layout.getABITypeAlign(unionType).value() * 8,
                                llvm::DINode::FlagZero,
                                builder.getOrCreateArray(metadata));

    if (!hasFlag(type, Native)) {
        metadata.clear();
        llvm::DIType *tag = nullptr;
        auto diTagAlign =
            layout.getABITypeAlign(llvmType->getContainedType(0)).value();
        if (diTagAlign == 1)
            tag = getDIType(getPrimitiveType(types, prtU8));
        else if (diTagAlign == 2)
            tag = getDIType(getPrimitiveType(types, prtU16));
        else if (diTagAlign == 4)
            tag = getDIType(getPrimitiveType(types, prtU32));
        else
            tag = getDIType(getPrimitiveType(types, prtU64));

        metadata.push_back(builder.createMemberType(
            currentScope(),
            "tag" /* Name */,
            currentScope()->getFile(),
            1 /* LineNo */,
            layout.getTypeSizeInBits(llvmType->getContainedType(0)),
            diTagAlign * 8,
            0 /* OffsetInBits */,
            llvm::DINode::FlagZero,
            tag));

        metadata.push_back(builder.createMemberType(
            currentScope(),
            "value" /* Name */,
            currentScope()->getFile(),
            1 /* LineNo */,
            layout.getTypeSizeInBits(unionType),
            layout.getABITypeAlign(unionType).value() * 8,
            unionLayout->getElementOffsetInBits(1),
            llvm::DINode::FlagZero,
            diUnionType));
    }
    else {
        metadata.push_back(builder.createMemberType(
            currentScope(),
            "value" /* Name */,
            currentScope()->getFile(),
            1 /* LineNo */,
            layout.getTypeSizeInBits(unionType),
            layout.getABITypeAlign(unionType).value() * 8,
            unionLayout->getElementOffsetInBits(0),
            llvm::DINode::FlagZero,
            diUnionType));
    }

    return builder.createStructType(currentScope(),
                                    llvmType->getStructName(),
                                    compileUnit->getFile(),
                                    1 /* LineNumber */,
                                    layout.getTypeSizeInBits(llvmType),
                                    layout.getABITypeAlign(llvmType).value() *
                                        8,
                                    llvm::DINode::FlagZero,
                                    nullptr /* DerivedFrom */,
                                    builder.getOrCreateArray(metadata));
}

llvm::DIType *DebugContext::createUntaggedUnionType(const Type *type)
{
    auto &layout = module.getDataLayout();
    auto llvmType = getLLVMType(type);
    auto unionLayout = layout.getStructLayout(
        llvm::dyn_cast_or_null<llvm::StructType>(llvmType));
    std::vector<llvm::Metadata *> metadata;

    for (u64 i = 0; i < type->untaggedUnion.members->count; i++) {
        auto elLlvmType =
            getLLVMType(type->untaggedUnion.members->members[i].type);
        auto diType = getDIType(type->untaggedUnion.members->members[i].type);
        auto diTypeSize = layout.getTypeSizeInBits(elLlvmType);
        auto diTypeAlign = layout.getABITypeAlign(elLlvmType).value() * 8;
        metadata.push_back(builder.createMemberType(
            currentScope(),
            type->untaggedUnion.members->members[i].name,
            currentScope()->getFile(),
            1 /* LineNo */,
            diTypeSize,
            diTypeAlign,
            0 /* OffsetInBits */,
            llvm::DINode::FlagZero,
            diType));
    }

    auto unionType = llvmType->getContainedType(0);
    return builder.createUnionType(currentScope(),
                                   type->name /* Name */,
                                   currentScope()->getFile(),
                                   1 /* LineNumber */,
                                   layout.getTypeSizeInBits(unionType),
                                   layout.getABITypeAlign(unionType).value() *
                                       8,
                                   llvm::DINode::FlagZero,
                                   builder.getOrCreateArray(metadata));
}

llvm::DIType *DebugContext::createEnumType(const Type *type)
{
    auto llvmBaseType = getLLVMType(type->tEnum.base);
    auto &layout = module.getDataLayout();
    auto decl = type->tEnum.decl;
    std::vector<llvm::Metadata *> optionsDescriptors;
    for (u64 i = 0; i < type->tEnum.optionsCount; i++) {
        auto &option = type->tEnum.options[i];
        optionsDescriptors.push_back(builder.createEnumerator(
            option.name, option.value, isUnsignedType(type->tEnum.base)));
    }

    auto enumeratorArray = builder.getOrCreateArray(optionsDescriptors);
    return builder.createEnumerationType(
        currentScope(),
        type->name,
        currentScope()->getFile(),
        decl->loc.begin.row,
        layout.getTypeSizeInBits(llvmBaseType),
        layout.getABITypeAlign(llvmBaseType).value() * 8,
        enumeratorArray,
        getDIType(type->tEnum.base));
}

llvm::DIType *DebugContext::createFunctionType(const Type *type)
{
    std::vector<llvm::Metadata *> paramsMetadata;
    auto decl = type->func.decl;

    paramsMetadata.push_back(getDIType(type->func.retType));
    if (decl && decl->funcDecl.this_) {
        paramsMetadata.push_back(getDIType(decl->funcDecl.this_->type));
    }

    for (u64 i = 0; i < type->func.paramsCount; i++) {
        paramsMetadata.push_back(getDIType(type->func.params[i]));
    }

    return builder.createSubroutineType(
        builder.getOrCreateTypeArray(paramsMetadata));
}

void DebugContext::addFields(std::vector<llvm::Metadata *> &elements,
                             const llvm::StructLayout &structLayout,
                             TypeMembersContainer &members)
{
    auto layout = module.getDataLayout();
    u64 j = 0;
    for (u64 i = 0; i < members.count; i++) {
        auto &member = members.members[i];
        if (!nodeIs(member.decl, FieldDecl))
            continue;

        auto diType = getDIType(member.type);
        auto diTypeSize = diType->getSizeInBits();
        auto llvmType = getLLVMType(member.type);
        auto alignment = layout
                             .getABITypeAlign(typeIs(member.type, Func)
                                                  ? llvmType->getPointerTo()
                                                  : llvmType)
                             .value() *
                         8;
        auto flags = hasFlag(member.decl, Private) ? llvm::DINode::FlagPrivate
                                                   : llvm::DINode::FlagPublic;

        elements.push_back(
            builder.createMemberType(currentScope(),
                                     member.name,
                                     currentScope()->getFile(),
                                     member.decl->loc.begin.row,
                                     diTypeSize,
                                     alignment,
                                     structLayout.getElementOffsetInBits(j++),
                                     flags,
                                     diType));
    }
}

llvm::DebugLoc DebugContext::getDebugLoc(const AstNode *node)
{
    llvm::DILocation *DILoc = llvm::DILocation::get(module.getContext(),
                                                    node->loc.begin.row,
                                                    node->loc.begin.col,
                                                    currentScope());
    return llvm::DebugLoc(DILoc);
}

llvm::DILocalVariable *DebugContext::emitParamDecl(const AstNode *node,
                                                   u64 index,
                                                   llvm::BasicBlock *block)
{
    auto value = getLlvmValue(node);
    csAssert0(llvm::isa<llvm::DILocalScope>(currentScope()));
    auto diType = getDIType(node->type);
    if (typeIs(node->type, Func))
        diType = builder.createPointerType(
            diType, module.getDataLayout().getPointerSize());

    llvm::DILocalVariable *diLocalVar =
        builder.createParameterVariable(currentScope(),
                                        node->funcParam.name,
                                        index,
                                        currentScope()->getFile(),
                                        node->loc.begin.row,
                                        diType,
                                        true);
    builder.insertDeclare(value,
                          diLocalVar,
                          builder.createExpression(),
                          getDebugLoc(node),
                          block);

    return diLocalVar;
}

void DebugContext::emitDebugLoc(const AstNode *node, llvm::Value *value)
{
    auto instr = llvm::dyn_cast_or_null<llvm::Instruction>(value);
    if (instr != nullptr) {
        llvm::DebugLoc debugLoc = getDebugLoc(node);
        instr->setDebugLoc(debugLoc);
    }
}

llvm::DILocalVariable *DebugContext::emitLocalVariable(const AstNode *node,
                                                       llvm::BasicBlock *block)
{
    auto value = getLlvmValue(node);
    csAssert0(llvm::isa<llvm::DILocalScope>(currentScope()));
    auto diType = getDIType(node->type);
    auto diLocalVar = builder.createAutoVariable(currentScope(),
                                                 node->funcParam.name,
                                                 currentScope()->getFile(),
                                                 node->loc.begin.row,
                                                 diType);
    builder.insertDeclare(value,
                          diLocalVar,
                          builder.createExpression(),
                          getDebugLoc(node),
                          block);

    return diLocalVar;
}

void DebugContext::emitGlobalVariable(const AstNode *node)
{
    auto value = llvm::dyn_cast<llvm::GlobalVariable>(getLlvmValue(node));
    llvm::DIGlobalVariableExpression *diGlobalVar =
        builder.createGlobalVariableExpression(currentScope(),
                                               node->varDecl.name,
                                               value->getName(),
                                               currentScope()->getFile(),
                                               node->loc.begin.row,
                                               getDIType(node->type),
                                               false);
    value->addDebugInfo(diGlobalVar);
}

void DebugContext::emitFunctionDecl(const AstNode *node)
{
    auto func = getLlvmFunction(node);
    auto flags = llvm::DINode::FlagPrototyped;
    auto spFlags = llvm::DISubprogram::SPFlagDefinition;

    if (hasFlag(node, Public))
        flags |= llvm::DINode::FlagPublic;
    if (hasFlag(node, Virtual)) {
        if (node->funcDecl.body == nullptr)
            spFlags |= llvm::DISubprogram::SPFlagPureVirtual;
        else
            spFlags |= llvm::DISubprogram::SPFlagVirtual;
    }

    llvm::DISubroutineType *diType =
        llvm::dyn_cast_or_null<llvm::DISubroutineType>(getDIType(node->type));

    llvm::DISubprogram *sub;
    if (node->funcDecl.this_) {
        sub = builder.createMethod(currentScope(),
                                   node->funcDecl.name,
                                   func->getName(),
                                   getFile(node->loc.fileName),
                                   node->loc.begin.row,
                                   diType,
                                   0 /* VTableIndex */,
                                   0 /* ThisAdjustment */,
                                   nullptr /* VTableHolder */,
                                   flags,
                                   spFlags);
    }
    else {
        sub = builder.createFunction(currentScope(),
                                     node->funcDecl.name,
                                     func->getName(),
                                     getFile(node->loc.fileName),
                                     node->loc.begin.row,
                                     diType,
                                     node->loc.begin.row,
                                     flags,
                                     spFlags);
    }
    scopes.push_back(sub);
    func->setSubprogram(sub);
}

void DebugContext::sealFunctionDecl(const AstNode *node)
{
    auto func = getLlvmFunction(node);
    if (auto sub = func->getSubprogram()) {
        builder.finalizeSubprogram(sub);
    }
    scopes.pop_back();
}

void DebugContext::finalize() { builder.finalize(); }

llvm::DIFile *DebugContext::getFile(llvm::StringRef filePath)
{
    if (filePath.empty())
        return compileUnit->getFile();

    auto it = files.find(filePath);
    if (it != files.end()) {
        return it->second;
    }
    llvm::SmallString<128> path(filePath);
    llvm::sys::fs::make_absolute(path);

    llvm::DIFile *diFile =
        builder.createFile(path, llvm::sys::path::parent_path(path));
    files.insert({filePath, diFile});
    return diFile;
}

} // namespace cxy
