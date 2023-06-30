//
// Created by Carter on 2023-06-29.
//

#include "visitor.h"

void astVisit(AstVisitor *visitor, AstNode *node)
{
    Visitor func = visitor->visitors[node->tag] ?: visitor->fallback;

    if (func == NULL)
        return;

    AstNode *stack = visitor->current;
    visitor->current = node;

    AstNode *ret = NULL, *next = node->next;
    if (visitor->dispatch) {
        ret = visitor->dispatch(func, visitor, node);
    }
    else {
        ret = func(visitor, node);
    }

    if (ret && ret != node) {
        // replace node
        getLastAstNode(ret)->next = next;
        *node = *ret;
    }

    visitor->current = stack;
}

void astConstVisit(ConstAstVisitor *visitor, const AstNode *node)
{
    ConstVisitor func = visitor->visitors[node->tag] ?: visitor->fallback;

    if (func == NULL)
        return;

    const AstNode *stack = visitor->current;
    visitor->current = node;

    if (visitor->dispatch)
        visitor->dispatch(func, visitor, node);
    else
        func(visitor, node);

    visitor->current = stack;
}