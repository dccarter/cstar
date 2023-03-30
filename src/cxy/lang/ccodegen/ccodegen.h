//
// Created by Carter on 2023-03-29.
//

#include <lang/codegen.h>

typedef struct {
    CodeGenContext base;
    TypeTable *table;
} CCodegenContext;

void generateCCodeFallback(ConstAstVisitor *visitor, const AstNode *node);
void cCodegenPrologue(CCodegenContext *context, const AstNode *prog);
void cCodegenEpilogue(CCodegenContext *context, const AstNode *prog);