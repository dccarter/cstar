//
// Created by Carter Mbotho on 2024-01-09.
//

#include "check.h"

#include "lang/frontend/flag.h"
#include "lang/frontend/strings.h"
#include "lang/middle/builtins.h"

#include <core/alloc.h>

static const Type *implementTupleOverload(AstVisitor *visitor,
                                          cstring overload,
                                          const Type *type)
{
    AstNode *func = findBuiltinDecl(overload);
    AstNode args = {.tag = astTypeRef, .type = type};
    AstNode path = {
        .tag = astPath,
        .path = {.elements = &(AstNode){.tag = astPathElem,
                                        .pathElement = {.name = overload,
                                                        .args = &args,
                                                        .resolvesTo = func}}}};

    return checkType(visitor, &path);
}

void implementTupleTypeCopyAndDestructor(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    bool hasCopy = node->type->tuple.copyFunc != NULL;
    if (!isBuiltinsInitialized() || hasCopy || !hasReferenceMembers(node->type))
        return;

    const Type *func =
        implementTupleOverload(visitor, S___tuple_copy, node->type);
    if (typeIs(func, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }
    ((Type *)node->type)->tuple.copyFunc = func;

    func = implementTupleOverload(visitor, S___tuple_dctor, node->type);
    if (typeIs(func, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }
    ((Type *)node->type)->tuple.destructorFunc = func;
}

void checkTupleExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *elements = node->tupleExpr.elements, *element = elements;

    AstNode *prev = NULL;
    for (u64 i = 0; element; element = element->next, i++) {
        if (nodeIs(element, Noop)) {
            if (prev == NULL)
                node->tupleExpr.elements = element->next;
            else
                prev->next = element->next;
            continue;
        }
        prev = element;

        const Type *type = checkType(visitor, element);
        if (typeIs(type, Error)) {
            node->type = element->type;
            continue;
        }

        type = unwrapType(type, NULL);
        if (isClassType(type)) {
            node->flags |= flgReferenceMembers;
        }
        else if (isStructType(type)) {
            AstNode *decl = getTypeDecl(type);
            node->flags |= (decl->flags & flgReferenceMembers);
        }
        else if (isTupleType(type)) {
            node->flags |= (type->flags & flgReferenceMembers);
        }
    }

    if (typeIs(node->type, Error))
        return;

    node->tupleExpr.len = countAstNodes(node->tupleExpr.elements);
    const Type **elements_ = mallocOrDie(sizeof(Type *) * node->tupleExpr.len);
    element = node->tupleExpr.elements;
    for (u64 i = 0; element; element = element->next, i++) {
        elements_[i] = element->type;
    }

    node->type = makeTupleType(
        ctx->types,
        elements_,
        node->tupleExpr.len,
        node->flags & (flgReferenceMembers | flgConst | flgReference));

    free(elements_);

    implementTupleTypeCopyAndDestructor(visitor, node);
}

void checkTupleType(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *elems = node->tupleType.elements, *elem = elems;
    u64 count = node->tupleType.len ?: countAstNodes(node->tupleType.elements);
    const Type **elems_ = mallocOrDie(sizeof(Type *) * count);

    for (u64 i = 0; elem; elem = elem->next, i++) {
        elems_[i] = checkType(visitor, elem);
        const Type *type = unwrapType(elems_[i], NULL);
        if (typeIs(elems_[i], Error)) {
            node->type = ERROR_TYPE(ctx);
        }
        else if (isClassType(type)) {
            node->flags |= flgReferenceMembers;
        }
        else if (isTupleType(type)) {
            node->flags |= (type->flags & flgReferenceMembers);
        }
        else if (isStructType(type) && !hasFlag(type, Closure)) {
            AstNode *decl = getTypeDecl(type);
            node->flags |= (decl->flags & flgReferenceMembers);
        }
    }

    if (typeIs(node->type, Error)) {
        free(elems_);
        return;
    }

    node->type = makeTupleType(
        ctx->types,
        elems_,
        count,
        node->flags & (flgReferenceMembers | flgConst | flgReference));

    free(elems_);

    implementTupleTypeCopyAndDestructor(visitor, node);
}
