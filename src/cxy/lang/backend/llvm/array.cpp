//
// Created by Carter Mbotho on 2024-01-16.
//

#include "context.h"

#include "lang/frontend/flag.h"

static llvm::Constant *makeArrayLiteral(cxy::LLVMContext &ctx, AstNode *node)
{
    std::vector<llvm::Constant *> elements{};
    auto elem = node->arrayExpr.elements;
    auto elementType = ctx.getLLVMType(node->type->array.elementType);
    for (; elem; elem = elem->next) {
        ctx.emitDebugLocation(elem);
        if (isIntegerType(elem->type)) {
            elements.push_back(
                llvm::ConstantInt::get(elementType,
                                       elem->intLiteral.uValue,
                                       elem->intLiteral.isNegative));
        }
        else if (isFloatType(elem->type)) {
            elements.push_back(
                llvm::ConstantFP::get(elementType, elem->floatLiteral.value));
        }
        else if (isCharacterType(elem->type)) {
            elements.push_back(
                llvm::ConstantInt::get(elementType, elem->charLiteral.value));
        }
        else if (isBooleanType(elem->type)) {
            elements.push_back(
                llvm::ConstantInt::get(elementType, elem->boolLiteral.value));
        }
        else if (typeIs(elem->type, String)) {
            elements.push_back(
                ctx.builder.CreateGlobalStringPtr(elem->stringLiteral.value));
        }
        else if (typeIs(elem->type, Array)) {
            elements.push_back(makeArrayLiteral(ctx, elem));
        }
    }

    return llvm::ConstantArray::get(
        llvm::dyn_cast<llvm::ArrayType>(ctx.getLLVMType(node->type)), elements);
}

void generateArrayExpr(AstVisitor *visitor, AstNode *node)
{
    auto &ctx = cxy::LLVMContext::from(visitor);
    auto array = ctx.createUndefined(node->type);
    ctx.emitDebugLocation(node);

    auto elem = node->arrayExpr.elements;
    for (u64 i = 0; elem; elem = elem->next, i++) {
        auto value = cxy::codegen(visitor, elem);
        array = ctx.builder.CreateInsertValue(array, value, i);
    }
    ctx.returnValue(array);
}
