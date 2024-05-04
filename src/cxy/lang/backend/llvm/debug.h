//
// Created by Carter Mbotho on 2024-05-02.
//

#pragma once

#include <driver/driver.h>
#include <lang/frontend/ast.h>

#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/DebugLoc.h>
#include <llvm/IR/GlobalVariable.h>

namespace cxy {

struct LLVMContext;

class DebugContext {
public:
    DebugContext(llvm::Module &module,
                 CompilerDriver *driver,
                 cstring fileName);

    llvm::DIType *getDIType(const Type *type);

    llvm::DILocalVariable *emitParamDecl(const AstNode *node,
                                         u64 index,
                                         llvm::BasicBlock *block);
    llvm::DILocalVariable *emitLocalVariable(const AstNode *node,
                                             llvm::BasicBlock *block);

    llvm::DebugLoc getDebugLoc(const AstNode *node);

    void emitGlobalVariable(const AstNode *node);

    void emitFunctionDecl(const AstNode *node);
    void sealFunctionDecl(const AstNode *node);
    void emitDebugLoc(const AstNode *node, llvm::Value *value);
    void finalize();

private:
    llvm::DIType *convertToDIType(const Type *type);
    llvm::DIType *pointerToDIType(const Type *type);
    llvm::DIType *arrayToDIType(const Type *type);
    llvm::DIType *createStructType(const Type *type);
    llvm::DIType *createClassType(const Type *type);
    llvm::DIType *createTupleType(const Type *type);
    llvm::DIType *createFunctionType(const Type *type);
    llvm::DIType *createEnumType(const Type *type);
    llvm::DIType *createUnionType(const Type *type);
    void addFields(std::vector<llvm::Metadata *> &elements,
                   const llvm::StructLayout &structLayout,
                   TypeMemberContainer &members);

    llvm::DIScope *currentScope()
    {
        if (scopes.empty())
            scopes.push_back(compileUnit->getFile());
        return scopes.back();
    }

    llvm::Value *getLlvmValue(const AstNode *node)
    {
        return static_cast<llvm::Value *>(node->codegen);
    }

    llvm::Function *getLlvmFunction(const AstNode *node)
    {
        return llvm::dyn_cast<llvm::Function>(getLlvmValue(node));
    }

    llvm::Type *getLLVMType(const Type *type)
    {
        if (typeIs(type, Wrapped))
            type = type->wrapped.target;
        csAssert0(type->codegen);
        return static_cast<llvm::Type *>(type->codegen);
    }

private:
    llvm::DIBuilder builder;
    llvm::DICompileUnit *compileUnit{nullptr};
    llvm::Module &module;
    TypeTable *types{nullptr};
    llvm::SmallVector<llvm::DIScope *, 4> scopes{};
};

} // namespace cxy