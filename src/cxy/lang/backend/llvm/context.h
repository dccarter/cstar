//
// Created by Carter Mbotho on 2024-01-09.
//

#pragma once

extern "C" {
#include "lang/operations.h"
}

#undef make
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <set>

struct Stack {
private:
#define DEFINE_STACK_VAR(TYPE, NAME, ...)                                      \
    TYPE _##NAME{__VA_ARGS__};                                                 \
    TYPE NAME(TYPE value) { return std::exchange(_##NAME, value); }            \
    TYPE NAME() const { return _##NAME; }

public:
    DEFINE_STACK_VAR(bool, loadVariable, true)
    DEFINE_STACK_VAR(void *, loopUpdate, nullptr)
    DEFINE_STACK_VAR(void *, loopCondition, nullptr)
    DEFINE_STACK_VAR(void *, loopEnd, nullptr)
    DEFINE_STACK_VAR(void *, result, nullptr)
    DEFINE_STACK_VAR(void *, funcEnd, nullptr)
    DEFINE_STACK_VAR(AstNode *, currentFunction, nullptr);

#undef DEFINE_STACK_VAR
};

struct LLVMContext {
    bool unreachable{false};
    Log *L{nullptr};
    MemPool *pool{nullptr};
    TypeTable *types{nullptr};
    StrPool *strings{nullptr};

    explicit LLVMContext(std::shared_ptr<llvm::LLVMContext> context,
                         CompilerDriver *driver,
                         const char *fname);

    inline llvm::LLVMContext &context() { return *_context; }
    inline llvm::Module &module() { return *_module; }
    inline llvm::IRBuilder<> &builder() { return *_builder; }

    inline void returnValue(llvm::Value *value) { _value = value; }
    inline llvm::Value *value() { return _value; }
    inline Stack &stack() { return _stack; }
    inline void setStack(Stack stack) { _stack = stack; }

    llvm::Type *getLLVMType(const Type *type);
    inline llvm::Value *createUndefined(const Type *type)
    {
        return llvm::UndefValue::get(getLLVMType(type));
    }

    inline llvm::Value *createUndefined(llvm::Type *type)
    {
        return llvm::UndefValue::get(type);
    }

    inline llvm::AllocaInst *createStackVariable(const Type *type,
                                                 const char *name = "")
    {
        return createStackVariable(getLLVMType(type), name);
    }

    llvm::AllocaInst *createStackVariable(llvm::Type *type,
                                          const char *name = "");

    llvm::Value *createLoad(const Type *type, llvm::Value *value);

    std::string makeTypeName(const AstNode *node);

    std::unique_ptr<llvm::Module> moveModule()
    {
        return std::exchange(_module, nullptr);
    }

    llvm::Function *findOrCreateFunctionDecl(const Type *type);

    void addFunctionDecl(AstNode *decl, llvm::Function *func);

    static LLVMContext &from(AstVisitor *visitor);

private:
    llvm::Type *createTupleType(const Type *type);
    llvm::Type *createFunctionType(const Type *type);
    llvm::Type *createUnionType(const Type *type);
    llvm::Type *createClassType(const Type *type);

    void makeTypeName(llvm::raw_string_ostream &ss, const AstNode *node);
    static std::string makeTypeName(const Type *type, const char *alt = "");
    llvm::Type *convertToLLVMType(const Type *type);

    std::shared_ptr<llvm::LLVMContext> _context{nullptr};
    std::unique_ptr<llvm::Module> _module{nullptr};
    std::unique_ptr<llvm::IRBuilder<>> _builder{nullptr};
    llvm::DenseMap<AstNode *, llvm::Function *> _functions{};
    llvm::Value *_value = {nullptr};
    Stack _stack{};
    const char *_sourceFilename{nullptr};
};

llvm::Value *codegen(AstVisitor *visitor, AstNode *node);
void visitBinaryExpr(AstVisitor *visitor, AstNode *node);
void visitArrayExpr(AstVisitor *visitor, AstNode *node);
void visitForStmt(AstVisitor *visitor, AstNode *node);

static inline void *updateType(const Type *type, void *codegen)
{
    return const_cast<Type *>(type)->codegen = codegen;
}
