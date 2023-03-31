//
// Created by Carter on 2023-03-29.
//

#include <lang/codegen.h>

typedef struct {
    CodegenContext base;
    TypeTable *table;
    cstring namespace;
} CCodegenContext;

void writeTypename(FormatState *state, const Type *type);
void generateTypeUsage(CCodegenContext *ctx, const Type *type);
void generateCCodeFallback(ConstAstVisitor *visitor, const AstNode *node);
void cCodegenPrologue(CCodegenContext *context, const AstNode *prog);
void cCodegenEpilogue(CCodegenContext *context, const AstNode *prog);