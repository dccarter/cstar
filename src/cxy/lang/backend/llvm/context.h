//
// Created by Carter Mbotho on 2024-01-09.
//

#pragma once

#include "lang/operations.h"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Target/TargetMachine.h>

#include <set>

namespace cxy {
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

class DebugContext;

struct LLVMContext {
    bool unreachable{false};
    Log *L{nullptr};
    MemPool *pool{nullptr};
    TypeTable *types{nullptr};
    StrPool *strings{nullptr};
    llvm::IRBuilder<> builder;
    llvm::LLVMContext &context;

    explicit LLVMContext(llvm::LLVMContext &context,
                         CompilerDriver *driver,
                         const char *fileName,
                         llvm::TargetMachine *TM);

    inline llvm::Module &module() { return *_module; }

    inline DebugContext *debugCtx() { return _debugCtx.get(); }

    inline void returnValue(llvm::Value *value) { _value = value; }
    inline llvm::Value *value() { return _value; }
    inline Stack &stack() { return _stack; }
    inline void setStack(Stack stack) { _stack = stack; }

    llvm::TypeSize getTypeSize(const Type *type);
    llvm::MaybeAlign getTypeAlignment(const Type *type);

    llvm::Type *getLLVMType(const Type *type);
    llvm::Type *classType(const Type *type);
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

    llvm::Value *generateCastExpr(AstVisitor *visitor,
                                  const Type *to,
                                  AstNode *expr,
                                  u64 idx = 0);

    static llvm::Value *getUnionTag(u32 tag, const llvm::Type *type);

    std::string makeTypeName(const AstNode *node);

    std::unique_ptr<llvm::Module> moveModule()
    {
        return std::exchange(_module, nullptr);
    }

    static LLVMContext &from(AstVisitor *visitor);

    llvm::Type *createClassType(const Type *type);
    llvm::Type *createStructType(const Type *type);
    llvm::Type *createInterfaceType(const Type *type);

    llvm::Value *withDebugLoc(const AstNode *node, llvm::Value *value);
    void emitDebugLocation(const AstNode *node);

private:
    llvm::Type *createTupleType(const Type *type);
    llvm::Type *createFunctionType(const Type *type);
    llvm::Type *createUnionType(const Type *type);
    llvm::Type *createUntaggedUnionType(const Type *type);
    llvm::Value *castFromUnion(AstVisitor *visitor,
                               const Type *to,
                               AstNode *node,
                               u64 idx);
    void makeTypeName(llvm::raw_string_ostream &ss, const AstNode *node);
    static std::string makeTypeName(const Type *type, const char *alt = "");
    llvm::Type *convertToLLVMType(const Type *type);
    void appendStructAlignment(std::vector<llvm::Type *> &members,
                               const AstNode *node,
                               bool isPacked);

private:
    std::unique_ptr<llvm::Module> _module{nullptr};
    llvm::DenseMap<AstNode *, llvm::Function *> _functions{};
    llvm::Value *_value = {nullptr};
    Stack _stack{};
    const char *_sourceFilename{nullptr};
    std::unique_ptr<DebugContext> _debugCtx{nullptr};
};

llvm::Value *codegen(AstVisitor *visitor, AstNode *node);
} // namespace cxy

void generateArrayExpr(AstVisitor *visitor, AstNode *node);
void generateBinaryExpr(AstVisitor *visitor, AstNode *node);
