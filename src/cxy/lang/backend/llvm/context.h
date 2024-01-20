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

struct LLVMContext {
    struct Stack {
        bool loadVariable{true};
        void *loopUpdate{nullptr};
        void *loopCondition{nullptr};
        void *loopEnd{nullptr};
        void *result{nullptr};
        void *funcEnd{nullptr};
    };
    bool unreachable{false};

    Log *L{nullptr};
    MemPool *pool{nullptr};
    TypeTable *types{nullptr};
    StrPool *strings{nullptr};

    inline llvm::LLVMContext &context() { return *_context; }
    inline llvm::Module &module() { return *_module; }
    inline llvm::IRBuilder<> &builder() { return *_builder; }

    inline void returnValue(llvm::Value *value) { _value = value; }
    inline llvm::Value *value() { return _value; }
    inline Stack &stack() { return _stack; }
    inline void setStack(Stack stack) { _stack = stack; }

    explicit LLVMContext(CompilerDriver *driver, const char *fname);

    void dumpIR();
    void dumpIR(std::string fname);
    llvm::Type *getLLVMType(const Type *type);

    llvm::AllocaInst *createStackVariable(const Type *type,
                                          const char *name = "");

    static LLVMContext &from(AstVisitor *visitor);

private:
    llvm::Type *convertToLLVMType(const Type *type);
    std::unique_ptr<llvm::LLVMContext> _context{nullptr};
    std::unique_ptr<llvm::Module> _module{nullptr};
    std::unique_ptr<llvm::IRBuilder<>> _builder{nullptr};
    llvm::Value *_value = {nullptr};
    Stack _stack{};
};

llvm::Value *codegen(AstVisitor *visitor, AstNode *node);
void visitBinaryExpr(AstVisitor *visitor, AstNode *node);
void visitArrayExpr(AstVisitor *visitor, AstNode *node);
void visitForStmt(AstVisitor *visitor, AstNode *node);

static inline void *updateType(const Type *type, void *codegen)
{
    return const_cast<Type *>(type)->codegen = codegen;
}