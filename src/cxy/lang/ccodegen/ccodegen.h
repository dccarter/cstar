//
// Created by Carter on 2023-03-29.
//

#include <core/strpool.h>
#include <lang/codegen.h>

typedef struct {
    CodegenContext base;
    TypeTable *table;
    cstring namespace;
    StrPool *strPool;
} CCodegenContext;

void writeEnumPrefix(FormatState *state, const Type *type);
void writeTypename(FormatState *state, const Type *type);
void generateTypeUsage(CCodegenContext *ctx, const Type *type);
void generateCCodeFallback(ConstAstVisitor *visitor, const AstNode *node);
void cCodegenPrologue(CCodegenContext *context, const AstNode *prog);
void cCodegenEpilogue(CCodegenContext *context, const AstNode *prog);
void cCodegenStringExpr(ConstAstVisitor *visitor, const AstNode *node);