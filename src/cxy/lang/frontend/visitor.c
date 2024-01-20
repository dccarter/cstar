//
// Created by Carter on 2023-06-29.
//

#include "visitor.h"
#include "flag.h"

#define AST_VISIT_ALL_NODES(MODE)                                              \
    switch (node->tag) {                                                       \
    case astTypeRef:                                                           \
    case astCCode:                                                             \
    case astNop:                                                               \
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
        break;                                                                 \
    case astAttr:                                                              \
        MODE##VisitManyNodes(visitor, node->attr.args);                        \
        break;                                                                 \
    case astDefine:                                                            \
        MODE##VisitManyNodes(visitor, node->define.names);                     \
        MODE##Visit(visitor, node->define.type);                               \
        MODE##Visit(visitor, node->define.container);                          \
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
        MODE##Visit(visitor, node->macroDecl.ret);                             \
        MODE##Visit(visitor, node->macroDecl.body);                            \
        break;                                                                 \
    case astFuncParam:                                                         \
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
    case astUnionValue:                                                        \
        MODE##Visit(visitor, node->unionValue.value);                          \
        break;                                                                 \
    case astEnumOption:                                                        \
        MODE##Visit(visitor, node->enumOption.value);                          \
        break;                                                                 \
    case astEnumDecl:                                                          \
        MODE##Visit(visitor, node->enumDecl.base);                             \
        MODE##VisitManyNodes(visitor, node->enumDecl.options);                 \
        break;                                                                 \
    case astField:                                                             \
        MODE##Visit(visitor, node->structField.type);                          \
        MODE##Visit(visitor, node->structField.value);                         \
        break;                                                                 \
    case astStructDecl:                                                        \
        MODE##VisitManyNodes(visitor, node->structDecl.implements);            \
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
    case astBinaryExpr:                                                        \
    case astAssignExpr:                                                        \
        MODE##Visit(visitor, node->binaryExpr.lhs);                            \
        MODE##Visit(visitor, node->binaryExpr.rhs);                            \
        break;                                                                 \
    case astUnaryExpr:                                                         \
    case astAddressOf:                                                         \
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
        MODE##Visit(visitor, node->switchStmt.cases);                          \
        break;                                                                 \
    case astMatchStmt:                                                         \
        MODE##Visit(visitor, node->matchStmt.expr);                            \
        MODE##Visit(visitor, node->matchStmt.cases);                           \
        break;                                                                 \
    case astCaseStmt:                                                          \
        MODE##Visit(visitor, node->caseStmt.match);                            \
        MODE##Visit(visitor, node->caseStmt.body);                             \
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
    AST_VISIT_ALL_NODES(ast)
}

void astConstVisitFallbackVisitAll(ConstAstVisitor *visitor,
                                   const AstNode *node)
{
    AST_VISIT_ALL_NODES(astConst)
}

void astVisitSkip(AstVisitor *visitor, AstNode *node) {}
void astConstVisitSkip(ConstAstVisitor *visitor, const AstNode *node) {}

void astModifierAdd(AstModifier *ctx, AstNode *node)
{
    csAssert0(ctx->current);

    node->next = ctx->current;
    if (ctx->previous)
        ctx->previous->next = node;
    else
        ctx->parent->program.decls = node;
    ctx->previous = node;
}

void astModifierAddAsNext(AstModifier *ctx, AstNode *node)
{
    csAssert0(ctx->current);
    node->next = ctx->current->next;
    ctx->current->next = node;
}