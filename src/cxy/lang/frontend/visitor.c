//
// Created by Carter on 2023-06-29.
//

#include "visitor.h"
#include "flag.h"

#define AST_VISIT_ALL_NODES(MODE)                                              \
    switch (node->tag) {                                                       \
    case astTypeRef:                                                           \
    case astCCode:                                                             \
    case astNoop:                                                              \
    case astStringType:                                                        \
    case astAutoType:                                                          \
    case astVoidType:                                                          \
    case astContinueStmt:                                                      \
    case astBreakStmt:                                                         \
    case astNullLit:                                                           \
    case astBoolLit:                                                           \
    case astCharLit:                                                           \
    case astIntegerLit:                                                        \
    case astFloatLit:                                                          \
    case astStringLit:                                                         \
    case astError:                                                             \
    case astPrimitiveType:                                                     \
    case astImportEntity:                                                      \
    case astModuleDecl:                                                        \
    case astIdentifier:                                                        \
    case astDestructorRef:                                                     \
    case astExternDecl:                                                        \
    case astList:                                                              \
    case astBranch:                                                            \
    case astPhi:                                                               \
    case astComptimeOnly:                                                      \
        break;                                                                 \
    case astAttr:                                                              \
        MODE##VisitManyNodes(visitor, node->attr.args);                        \
        break;                                                                 \
    case astDefine:                                                            \
        MODE##VisitManyNodes(visitor, node->define.names);                     \
        MODE##Visit(visitor, node->define.type);                               \
        MODE##Visit(visitor, node->define.container);                          \
        break;                                                                 \
    case astAsmOperand:                                                        \
        MODE##Visit(visitor, node->asmOperand.operand);                        \
        break;                                                                 \
    case astAsm:                                                               \
        MODE##VisitManyNodes(visitor, node->inlineAssembly.outputs);           \
        MODE##VisitManyNodes(visitor, node->inlineAssembly.inputs);            \
        MODE##VisitManyNodes(visitor, node->inlineAssembly.clobbers);          \
        MODE##VisitManyNodes(visitor, node->inlineAssembly.flags);             \
        break;                                                                 \
    case astException:                                                         \
        MODE##VisitManyNodes(visitor, node->exception.params);                 \
        break;                                                                 \
    case astTupleXform:                                                        \
        MODE##Visit(visitor, node->xForm.target);                              \
        MODE##Visit(visitor, node->xForm.cond);                                \
        MODE##Visit(visitor, node->xForm.xForm);                               \
        break;                                                                 \
    case astProgram:                                                           \
        MODE##VisitManyNodes(visitor, node->program.top);                      \
        MODE##VisitManyNodes(visitor, node->program.decls);                    \
        break;                                                                 \
    case astMetadata:                                                          \
        MODE##Visit(visitor, node->metadata.node);                             \
        break;                                                                 \
    case astRef:                                                               \
        MODE##Visit(visitor, node->reference.target);                          \
        break;                                                                 \
    case astImportDecl:                                                        \
        MODE##Visit(visitor, node->import.alias);                              \
        MODE##Visit(visitor, node->import.module);                             \
        MODE##Visit(visitor, node->import.entities);                           \
        break;                                                                 \
    case astTupleType:                                                         \
    case astTupleExpr:                                                         \
        MODE##VisitManyNodes(visitor, node->tupleType.elements);               \
        break;                                                                 \
    case astArrayType:                                                         \
        MODE##Visit(visitor, node->arrayType.elementType);                     \
        MODE##Visit(visitor, node->arrayType.dim);                             \
        break;                                                                 \
    case astFuncType:                                                          \
        MODE##Visit(visitor, node->funcType.ret);                              \
        MODE##VisitManyNodes(visitor, node->funcType.params);                  \
        break;                                                                 \
    case astOptionalType:                                                      \
        MODE##Visit(visitor, node->optionalType.type);                         \
        break;                                                                 \
                                                                               \
        break;                                                                 \
    case astPointerType:                                                       \
        MODE##Visit(visitor, node->pointerType.pointed);                       \
        break;                                                                 \
    case astReferenceType:                                                     \
        MODE##Visit(visitor, node->referenceType.referred);                    \
        break;                                                                 \
    case astResultType:                                                        \
        MODE##Visit(visitor, node->resultType.target);                         \
        break;                                                                 \
    case astStringExpr:                                                        \
        MODE##VisitManyNodes(visitor, node->stringExpr.parts);                 \
        break;                                                                 \
    case astArrayExpr:                                                         \
        MODE##VisitManyNodes(visitor, node->arrayExpr.elements);               \
        break;                                                                 \
    case astMemberExpr:                                                        \
        MODE##Visit(visitor, node->memberExpr.target);                         \
        MODE##Visit(visitor, node->memberExpr.member);                         \
        break;                                                                 \
    case astRangeExpr:                                                         \
        MODE##Visit(visitor, node->rangeExpr.start);                           \
        MODE##Visit(visitor, node->rangeExpr.end);                             \
        MODE##Visit(visitor, node->rangeExpr.step);                            \
        break;                                                                 \
    case astNewExpr:                                                           \
        MODE##Visit(visitor, node->newExpr.type);                              \
        MODE##Visit(visitor, node->newExpr.init);                              \
        break;                                                                 \
    case astCastExpr:                                                          \
        MODE##Visit(visitor, node->castExpr.to);                               \
        MODE##Visit(visitor, node->castExpr.expr);                             \
        break;                                                                 \
    case astIndexExpr:                                                         \
        MODE##Visit(visitor, node->indexExpr.target);                          \
        MODE##Visit(visitor, node->indexExpr.index);                           \
        break;                                                                 \
    case astGenericParam:                                                      \
        MODE##VisitManyNodes(visitor, node->genericParam.constraints);         \
        break;                                                                 \
    case astGenericDecl:                                                       \
        MODE##Visit(visitor, node->genericDecl.decl);                          \
        MODE##VisitManyNodes(visitor, node->genericDecl.params);               \
    case astPathElem:                                                          \
        MODE##VisitManyNodes(visitor, node->pathElement.args);                 \
        break;                                                                 \
    case astPath:                                                              \
        MODE##VisitManyNodes(visitor, node->path.elements);                    \
        break;                                                                 \
    case astFuncDecl:                                                          \
        MODE##VisitManyNodes(visitor, node->funcDecl.signature->params);       \
        MODE##Visit(visitor, node->funcDecl.signature->ret);                   \
        MODE##Visit(visitor, node->funcDecl.opaqueParams);                     \
        MODE##Visit(visitor, node->funcDecl.body);                             \
        break;                                                                 \
    case astMacroDecl:                                                         \
        MODE##VisitManyNodes(visitor, node->macroDecl.params);                 \
        MODE##Visit(visitor, node->macroDecl.body);                            \
        break;                                                                 \
    case astFuncParamDecl:                                                     \
        MODE##Visit(visitor, node->funcParam.type);                            \
        MODE##Visit(visitor, node->funcParam.def);                             \
        break;                                                                 \
    case astVarDecl:                                                           \
        MODE##VisitManyNodes(visitor, node->varDecl.names);                    \
        MODE##Visit(visitor, node->varDecl.type);                              \
        MODE##Visit(visitor, node->varDecl.init);                              \
        break;                                                                 \
    case astTypeDecl:                                                          \
        if (!hasFlag(node, ForwardDecl))                                       \
            MODE##Visit(visitor, node->typeDecl.aliased);                      \
        break;                                                                 \
    case astUnionDecl:                                                         \
        MODE##VisitManyNodes(visitor, node->unionDecl.members);                \
        break;                                                                 \
    case astUnionValueExpr:                                                    \
        MODE##Visit(visitor, node->unionValue.value);                          \
        break;                                                                 \
    case astEnumOptionDecl:                                                    \
        MODE##Visit(visitor, node->enumOption.value);                          \
        break;                                                                 \
    case astEnumDecl:                                                          \
        MODE##Visit(visitor, node->enumDecl.base);                             \
        MODE##VisitManyNodes(visitor, node->enumDecl.options);                 \
        break;                                                                 \
    case astFieldDecl:                                                         \
        MODE##Visit(visitor, node->structField.type);                          \
        MODE##Visit(visitor, node->structField.value);                         \
        break;                                                                 \
    case astStructDecl:                                                        \
        MODE##VisitManyNodes(visitor, node->structDecl.members);               \
        break;                                                                 \
    case astClassDecl:                                                         \
        MODE##Visit(visitor, node->classDecl.base);                            \
        MODE##VisitManyNodes(visitor, node->classDecl.implements);             \
        MODE##VisitManyNodes(visitor, node->classDecl.members);                \
        break;                                                                 \
    case astInterfaceDecl:                                                     \
        MODE##VisitManyNodes(visitor, node->interfaceDecl.members);            \
        break;                                                                 \
    case astTestDecl:                                                          \
        MODE##Visit(visitor, node->testDecl.body);                             \
        break;                                                                 \
    case astBinaryExpr:                                                        \
    case astAssignExpr:                                                        \
        MODE##Visit(visitor, node->binaryExpr.lhs);                            \
        MODE##Visit(visitor, node->binaryExpr.rhs);                            \
        break;                                                                 \
    case astUnaryExpr:                                                         \
    case astReferenceOf:                                                       \
    case astPointerOf:                                                         \
        MODE##Visit(visitor, node->unaryExpr.operand);                         \
        break;                                                                 \
    case astTernaryExpr:                                                       \
    case astIfStmt:                                                            \
        MODE##Visit(visitor, node->ternaryExpr.cond);                          \
        MODE##Visit(visitor, node->ternaryExpr.body);                          \
        MODE##Visit(visitor, node->ternaryExpr.otherwise);                     \
        break;                                                                 \
    case astStmtExpr:                                                          \
        MODE##Visit(visitor, node->stmtExpr.stmt);                             \
        break;                                                                 \
    case astTypedExpr:                                                         \
        MODE##Visit(visitor, node->typedExpr.type);                            \
        MODE##Visit(visitor, node->typedExpr.expr);                            \
        break;                                                                 \
    case astCallExpr:                                                          \
        MODE##Visit(visitor, node->callExpr.callee);                           \
        MODE##VisitManyNodes(visitor, node->callExpr.args);                    \
        break;                                                                 \
    case astBackendCall:                                                       \
        MODE##VisitManyNodes(visitor, node->backendCallExpr.args);             \
        break;                                                                 \
    case astMacroCallExpr:                                                     \
        MODE##Visit(visitor, node->macroCallExpr.callee);                      \
        MODE##VisitManyNodes(visitor, node->macroCallExpr.args);               \
        break;                                                                 \
    case astClosureExpr:                                                       \
        MODE##Visit(visitor, node->closureExpr.ret);                           \
        MODE##VisitManyNodes(visitor, node->closureExpr.params);               \
        MODE##Visit(visitor, node->closureExpr.body);                          \
        break;                                                                 \
    case astFieldExpr:                                                         \
        MODE##Visit(visitor, node->fieldExpr.value);                           \
        break;                                                                 \
    case astStructExpr:                                                        \
        MODE##Visit(visitor, node->structExpr.left);                           \
        MODE##VisitManyNodes(visitor, node->structExpr.fields);                \
        break;                                                                 \
    case astExprStmt:                                                          \
    case astGroupExpr:                                                         \
    case astDeferStmt:                                                         \
    case astSpreadExpr:                                                        \
        MODE##Visit(visitor, node->exprStmt.expr);                             \
        break;                                                                 \
    case astReturnStmt:                                                        \
        MODE##Visit(visitor, node->returnStmt.expr);                           \
        break;                                                                 \
    case astYieldStmt:                                                         \
        MODE##Visit(visitor, node->yieldStmt.expr);                            \
        break;                                                                 \
    case astBlockStmt:                                                         \
        MODE##VisitManyNodes(visitor, node->blockStmt.stmts);                  \
        break;                                                                 \
    case astForStmt:                                                           \
        MODE##Visit(visitor, node->forStmt.var);                               \
        MODE##Visit(visitor, node->forStmt.range);                             \
        MODE##Visit(visitor, node->forStmt.body);                              \
        break;                                                                 \
    case astWhileStmt:                                                         \
        MODE##Visit(visitor, node->whileStmt.cond);                            \
        MODE##Visit(visitor, node->whileStmt.body);                            \
        break;                                                                 \
    case astSwitchStmt:                                                        \
        MODE##Visit(visitor, node->switchStmt.cond);                           \
        MODE##VisitManyNodes(visitor, node->switchStmt.cases);                 \
        break;                                                                 \
    case astMatchStmt:                                                         \
        MODE##Visit(visitor, node->matchStmt.expr);                            \
        MODE##VisitManyNodes(visitor, node->matchStmt.cases);                  \
        break;                                                                 \
    case astCaseStmt:                                                          \
        MODE##Visit(visitor, node->caseStmt.match);                            \
        MODE##Visit(visitor, node->caseStmt.variable);                         \
        MODE##Visit(visitor, node->caseStmt.body);                             \
        break;                                                                 \
    case astBasicBlock:                                                        \
        MODE##VisitManyNodes(visitor, node->basicBlock.stmts.first);           \
        break;                                                                 \
    case astBranchIf:                                                          \
        MODE##Visit(visitor, node->branchIf.cond);                             \
        break;                                                                 \
    case astSwitchIr:                                                          \
        MODE##Visit(visitor, node->switchIr.cond);                             \
        break;                                                                 \
    case astGep:                                                               \
        MODE##Visit(visitor, node->gep.value);                                 \
        break;                                                                 \
    default:                                                                   \
        unreachable("UNKNOWN NODE ast%s", getAstNodeName(node));               \
    }

void astVisitManyNodes(AstVisitor *visitor, AstNode *node)
{
    AstNode *curr = NULL, *next = node;
    for (; next;) {
        curr = next;
        next = next->next;
        astVisit(visitor, curr);
    }
}

void astConstVisitManyNodes(ConstAstVisitor *visitor, const AstNode *node)
{
    for (const AstNode *it = node; it; it = it->next) {
        astConstVisit(visitor, it);
    }
}

void astVisit(AstVisitor *visitor, AstNode *node)
{
    if (node == NULL)
        return;

    // Skip all the nodes marked as visited
    if (hasFlag(node, Visited)) {
        node->flags &= ~flgVisited;
    }

    Visitor func = visitor->visitors[node->tag] ?: visitor->fallback;

    if (func == NULL)
        return;

    AstNode *stack = visitor->current;
    visitor->current = node;

    if (visitor->dispatch) {
        visitor->dispatch(func, visitor, node);
    }
    else {
        func(visitor, node);
    }

    visitor->current = stack;
}

void astConstVisit(ConstAstVisitor *visitor, const AstNode *node)
{
    if (node == NULL)
        return;

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

void astVisitFallbackVisitAll(AstVisitor *visitor, AstNode *node)
{
    if (node == NULL)
        return;
    AST_VISIT_ALL_NODES(ast)
}

void astConstVisitFallbackVisitAll(ConstAstVisitor *visitor,
                                   const AstNode *node)
{
    if (node == NULL)
        return;
    AST_VISIT_ALL_NODES(astConst)
}

void astVisitSkip(AstVisitor *visitor, AstNode *node) {}

void astConstVisitSkip(ConstAstVisitor *visitor, const AstNode *node) {}

void astModifierInit(AstModifier *ctx, AstNode *node)
{
    ctx->parent = node;
    ctx->previous = NULL;
    ctx->current = NULL;
}

void astModifierNext(AstModifier *ctx, AstNode *node)
{
    ctx->previous = ctx->current ?: ctx->previous;
    ctx->current = node;
}

void astModifierRemoveCurrent(AstModifier *ctx)
{
    csAssert0(ctx->current);
    if (ctx->previous)
        ctx->previous->next = ctx->current->next;
    else {
        if (nodeIs(ctx->parent, Program)) {
            ctx->parent->program.decls = ctx->current->next;
        }
        else {
            csAssert0(nodeIs(ctx->parent, BlockStmt));
            ctx->parent->blockStmt.stmts = ctx->current->next;
        }
    }
    ctx->current = NULL;
}

void astModifierAdd(AstModifier *ctx, AstNode *node)
{
    if (node == NULL)
        return;

    AstNode *last = getLastAstNode(node);
    last->next = ctx->current;
    if (ctx->previous) {
        ctx->previous->next = node;
    }
    else if (nodeIs(ctx->parent, Program)) {
        ctx->parent->program.decls = node;
    }
    else {
        csAssert0(nodeIs(ctx->parent, BlockStmt));
        ctx->parent->blockStmt.stmts = node;
    }

    ctx->previous = last;
}

void astModifierAddAsNext(AstModifier *ctx, AstNode *node)
{
    if (ctx->current) {
        getLastAstNode(node)->next = ctx->current->next;
        ctx->current->next = node;
    }
    else if (nodeIs(ctx->parent, Program)) {
        csAssert0(ctx->parent->program.decls == NULL);
        ctx->parent->program.decls = node;
        ctx->current = node;
    }
    else if (nodeIs(ctx->parent, BlockStmt)) {
        csAssert0(ctx->parent->blockStmt.stmts == NULL);
        ctx->parent->blockStmt.stmts = node;
        ctx->current = node;
    }
    else {
        unreachable();
    }
}

void astModifierAddAsLast(AstModifier *ctx, AstNode *node)
{
    if (ctx->current) {
        getLastAstNode(ctx->current)->next = node;
    }
    else if (nodeIs(ctx->parent, Program)) {
        csAssert0(ctx->parent->program.decls == NULL);
        ctx->parent->program.decls = node;
        ctx->current = node;
    }
    else if (nodeIs(ctx->parent, BlockStmt)) {
        csAssert0(ctx->parent->blockStmt.stmts == NULL);
        ctx->parent->blockStmt.stmts = node;
        ctx->current = node;
    }
    else {
        unreachable();
    }
}

void astModifierAddHead(AstModifier *ctx, AstNode *node)
{
    if (node == NULL)
        return;
    AstNode *last = getLastAstNode(node);
    if (nodeIs(ctx->parent, Program)) {
        last->next = ctx->parent->program.decls;
        ctx->parent->program.decls = node;
    }
    else {
        csAssert0(nodeIs(ctx->parent, BlockStmt));
        ctx->parent->blockStmt.stmts = node;
        last->next = ctx->parent->blockStmt.stmts;
    }
}