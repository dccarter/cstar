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
        AstNode *currentFunction{nullptr};
    };
    
    bool unreachable{false};
    Log *L{nullptr};
    MemPool *pool{nullptr};
    TypeTable *types{nullptr};
    StrPool *strings{nullptr};

    explicit LLVMContext(CompilerDriver *driver, const char *fname);

    inline llvm::LLVMContext &context() { return *_context; }
    inline llvm::Module &module() { return *_module; }
    inline llvm::IRBuilder<> &builder() { return *_builder; }

    inline void returnValue(llvm::Value *value) { _value = value; }
    inline llvm::Value *value() { return _value; }
    inline Stack &stack() { return _stack; }
    inline void setStack(Stack stack) { _stack = stack; }

    void dumpIR();
    void dumpIR(std::string fname);
    llvm::Type *getLLVMType(const Type *type);

    inline llvm::Value *createUndefined(const Type *type)
    {
        return llvm::UndefValue::get(getLLVMType(type));
    }

    llvm::AllocaInst *createStackVariable(const Type *type,
                                          const char *name = "");

    std::string makeTypeName(const AstNode *node);
    static LLVMContext &from(AstVisitor *visitor);

private:
    void makeTypeName(llvm::raw_string_ostream &ss, const AstNode *node);
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
