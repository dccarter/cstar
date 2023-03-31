//
// Created by Carter on 2023-03-29.
//

#include "codegen.h"

void generateManyAsts(ConstAstVisitor *visitor,
                      const char *sep,
                      const AstNode *nodes)
{
    CodegenContext *context = getConstAstVisitorContext(visitor);
    for (const AstNode *node = nodes; node; node = node->next) {
        astConstVisit(visitor, node);
        if (node->next)
            format(context->state, sep, NULL);
    }
}

void generateManyAstsWithDelim(ConstAstVisitor *visitor,
                               const char *open,
                               const char *sep,
                               const char *close,
                               const AstNode *nodes)
{
    CodegenContext *context = getConstAstVisitorContext(visitor);
    format(context->state, open, NULL);
    generateManyAsts(visitor, sep, nodes);
    format(context->state, close, NULL);
}

void generateAstWithDelim(ConstAstVisitor *visitor,
                          const char *open,
                          const char *close,
                          const AstNode *node)
{
    CodegenContext *context = getConstAstVisitorContext(visitor);
    format(context->state, open, NULL);
    astConstVisit(visitor, node);
    format(context->state, close, NULL);
}

void generateManyAstsWithinBlock(ConstAstVisitor *visitor,
                                 const char *sep,
                                 const AstNode *nodes,
                                 bool newLine)
{
    CodegenContext *context = getConstAstVisitorContext(visitor);
    if (!nodes)
        format(context->state, "{{}", NULL);
    else if (!newLine && !nodes->next)
        generateAstWithDelim(visitor, "{{ ", " }", nodes);
    else
        generateManyAstsWithDelim(visitor, "{{{>}\n", sep, "{<}\n}", nodes);
}
