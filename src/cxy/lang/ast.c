
#include "ast.h"
#include "flag.h"

#include <memory.h>

AstNode *makeAstNode(MemPool *pool, const FileLoc *loc, const AstNode *init)
{
    AstNode *node = allocFromMemPool(pool, sizeof(AstNode));
    memcpy(node, init, sizeof(AstNode));
    node->loc = *loc;
    return node;
}

AstNode *makePath(MemPool *pool,
                  const FileLoc *loc,
                  cstring name,
                  u64 flags,
                  const Type *type)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astPath,
                                  .flags = flags,
                                  .type = type,
                                  .path.elements = makeAstNode(
                                      pool,
                                      loc,
                                      &(AstNode){.flags = flags,
                                                 .type = type,
                                                 .tag = astPathElem,
                                                 .pathElement.name = name})});
}

void clearAstBody(AstNode *node)
{
    memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);
}

AstNode *copyAstNode(MemPool *pool, const AstNode *node)
{
    AstNode *copy = allocFromMemPool(pool, sizeof(AstNode));
    memcpy(copy, node, sizeof(AstNode));
    copy->next = NULL;
    copy->parentScope = NULL;
    return copy;
}

AstNode *duplicateAstNode(MemPool *pool, const AstNode *node)
{
    AstNode *copy = allocFromMemPool(pool, sizeof(AstNode));
    memcpy(copy, node, sizeof(AstNode));
    return copy;
}

bool isTuple(const AstNode *node)
{
    if (node->tag != astTupleExpr)
        return false;
    if (node->tupleExpr.args->next == NULL)
        return false;
    return true;
}

bool isAssignableExpr(attr(unused) const AstNode *node)
{
    csAssert(node->type, "expression must have been type-checked first");
    return false;
}

bool isLiteralExpr(const AstNode *node)
{
    if (node == NULL)
        return false;
    switch (node->tag) {
    case astStringLit:
    case astIntegerLit:
    case astBoolLit:
    case astFloatLit:
    case astCharLit:
    case astNullLit:
        return true;
    default:
        return isEnumLiteral(node);
    }
}

bool isEnumLiteral(const AstNode *node)
{
    if (!typeIs(node->type, Enum) || !nodeIs(node, Path) ||
        node->path.elements->next == NULL)
        return false;

    return (node->path.elements->next->flags & flgEnumLiteral) ==
           flgEnumLiteral;
}

bool isIntegralLiteral(const AstNode *node)
{
    switch (node->tag) {
    case astIntegerLit:
    case astBoolLit:
    case astFloatLit:
    case astCharLit:
        return true;
    default:
        return isEnumLiteral(node);
    }
}

bool isTypeExpr(const AstNode *node)
{
    switch (node->tag) {
    case astVoidType:
    case astAutoType:
    case astStringType:
    case astTupleType:
    case astArrayType:
    case astPointerType:
    case astFuncType:
    case astPrimitiveType:
    case astOptionalType:
    case astStructDecl:
    case astEnumDecl:
    case astUnionDecl:
    case astFuncDecl:
        return true;
    default:
        return false;
    }
}

bool isBuiltinTypeExpr(const AstNode *node)
{
    switch (node->tag) {
    case astVoidType:
    case astAutoType:
    case astStringType:
    case astPrimitiveType:
    case astOptionalType:
        return true;
    default:
        return false;
    }
}

u64 countAstNodes(const AstNode *node)
{
    u64 len = 0;
    for (; node; node = node->next)
        len++;
    return len;
}

AstNode *getLastAstNode(AstNode *node)
{
    while (node && node->next)
        node = node->next;
    return node;
}

AstNode *getNodeAtIndex(AstNode *node, u64 index)
{
    u64 i = 0;
    while (node) {
        if (i == index)
            return node;
        node = node->next;
        i++;
    }
    return NULL;
}

AstNode *findEnumOptionByName(AstNode *node, cstring name)
{
    AstNode *member = node->enumDecl.options;
    while (member) {
        if (member->enumOption.name == name)
            return member;
        member = member->next;
    }
    return NULL;
}

AstNode *findStructMemberByName(AstNode *node, cstring name)
{
    AstNode *member = node->structDecl.members;
    while (member) {
        if ((nodeIs(member, StructField) && member->structField.name == name) ||
            (nodeIs(member, FuncDecl) && member->structField.name == name))
            return member;
        member = member->next;
    }
    return NULL;
}

AstNode *getParentScopeWithTag(AstNode *node, AstTag tag)
{
    AstNode *parentScope = node->parentScope;
    while (parentScope && parentScope->tag != tag)
        parentScope = parentScope->parentScope;
    return parentScope;
}

const AstNode *getLastAstNodeConst(const AstNode *node)
{
    while (node->next)
        node = node->next;
    return node;
}

const AstNode *getConstNodeAtIndex(const AstNode *node, u64 index)
{
    u64 i = 0;
    while (node) {
        if (i == index)
            return node;
        node = node->next;
        i++;
    }
    return NULL;
}

const AstNode *getParentScopeWithTagConst(const AstNode *node, AstTag tag)
{
    const AstNode *parentScope = node->parentScope;
    while (parentScope && parentScope->tag != tag)
        parentScope = parentScope->parentScope;
    return parentScope;
}

AstNode *cloneManyAstNodes(MemPool *pool, const AstNode *nodes)
{
    AstNode *first = NULL, *node = NULL;
    while (nodes) {
        if (!first) {
            first = cloneAstNode(pool, nodes);
            node = first;
        }
        else {
            node->next = cloneAstNode(pool, nodes);
            node = node->next;
        }
        nodes = nodes->next;
    }
    return first;
}

AstNode *replaceAstNode(AstNode *node, const AstNode *with)
{
    AstNode *next = node->next, *parent = node->parentScope;
    *node = *with;
    node->parentScope = parent;
    getLastAstNode(node)->next = next;

    return node;
}

AstNode *cloneAstNode(MemPool *pool, const AstNode *node)
{
    if (node == NULL)
        return NULL;

    AstNode *clone = copyAstNode(pool, node);

#define CLONE_MANY(AST, MEMBER)                                                \
    clone->AST.MEMBER = cloneManyAstNodes(pool, node->AST.MEMBER);
#define CLONE_ONE(AST, MEMBER)                                                 \
    clone->AST.MEMBER = cloneAstNode(pool, node->AST.MEMBER);

    switch (clone->tag) {
    case astProgram:
        CLONE_MANY(program, decls);
        break;
    case astCastExpr:
        CLONE_ONE(castExpr, expr);
        CLONE_ONE(castExpr, to);
        break;
    case astPathElem:
        CLONE_MANY(pathElement, args);
        break;
    case astPath:
        CLONE_MANY(path, elements);
        break;
    case astGenericParam:
        CLONE_MANY(genericParam, constraints);
        break;
    case astGenericDecl:
        CLONE_MANY(genericDecl, params);
        CLONE_ONE(genericDecl, decl);
        break;
    case astTupleType:
        CLONE_MANY(tupleType, args);
        break;
    case astArrayType:
        CLONE_ONE(arrayType, elementType);
        CLONE_MANY(arrayType, dim);
        break;
    case astPointerType:
        CLONE_MANY(pointerType, pointed);
        break;
    case astFuncType:
        CLONE_ONE(funcType, ret);
        CLONE_MANY(funcType, params);
        break;
    case astFuncParam:
        CLONE_ONE(funcParam, type);
        CLONE_ONE(funcParam, def);
        break;
    case astFuncDecl:
        CLONE_MANY(funcDecl, params);
        CLONE_ONE(funcDecl, ret);
        CLONE_ONE(funcDecl, body);
        break;
    case astMacroDecl:
        CLONE_MANY(macroDecl, params);
        CLONE_ONE(macroDecl, ret);
        CLONE_ONE(macroDecl, body);
        break;
    case astVarDecl:
        CLONE_ONE(varDecl, type);
        CLONE_ONE(varDecl, init);
        CLONE_MANY(varDecl, names);
        break;
    case astTypeDecl:
        CLONE_ONE(typeDecl, aliased);
        break;
    case astUnionDecl:
        CLONE_MANY(unionDecl, members);
        break;

    case astStructDecl:
        CLONE_MANY(structDecl, members);
        CLONE_ONE(structDecl, base);
        break;

    case astEnumOption:
        CLONE_ONE(enumOption, value);
        break;

    case astEnumDecl:
        CLONE_MANY(enumDecl, options);
        CLONE_ONE(enumDecl, base);
        break;

    case astStructField:
        CLONE_ONE(structField, value)
        CLONE_ONE(structField, type)
        break;

    case astGroupExpr:
        CLONE_ONE(groupExpr, expr);
        break;
    case astUnaryExpr:
    case astAddressOf:
        CLONE_ONE(unaryExpr, operand);
        break;
    case astBinaryExpr:
        CLONE_ONE(binaryExpr, lhs);
        CLONE_ONE(binaryExpr, rhs);
        break;
    case astAssignExpr:
        CLONE_ONE(assignExpr, lhs);
        CLONE_ONE(assignExpr, rhs);
        break;
    case astTernaryExpr:
        CLONE_ONE(ternaryExpr, cond);
        CLONE_ONE(ternaryExpr, body);
        CLONE_ONE(ternaryExpr, otherwise);
        break;
    case astStmtExpr:
        CLONE_ONE(stmtExpr, stmt);
        break;
    case astStringExpr:
        CLONE_MANY(stringExpr, parts);
        break;
    case astTypedExpr:
        CLONE_ONE(typedExpr, expr);
        CLONE_ONE(typedExpr, type);
        break;
    case astCallExpr:
    case astMacroCallExpr:
        CLONE_ONE(callExpr, callee);
        CLONE_MANY(callExpr, args);
        break;
    case astClosureExpr:
        CLONE_ONE(closureExpr, ret);
        CLONE_MANY(closureExpr, params);
        CLONE_ONE(closureExpr, body);
        break;
    case astArrayExpr:
        CLONE_MANY(arrayExpr, elements);
        break;
    case astIndexExpr:
        CLONE_ONE(indexExpr, target);
        CLONE_MANY(indexExpr, index);
        break;
    case astTupleExpr:
        CLONE_MANY(tupleExpr, args);
        break;

    case astFieldExpr:
        CLONE_ONE(fieldExpr, value);
        break;
    case astStructExpr:
        CLONE_MANY(structExpr, fields);
        CLONE_ONE(structExpr, left);
        break;

    case astMemberExpr:
        CLONE_ONE(memberExpr, target);
        CLONE_ONE(memberExpr, member);
        break;
    case astExprStmt:
        CLONE_ONE(exprStmt, expr);
        break;
    case astDeferStmt:
        CLONE_ONE(deferStmt, expr);
        break;
    case astReturnStmt:
        CLONE_ONE(returnStmt, expr);
        break;
    case astBlockStmt:
        CLONE_MANY(blockStmt, stmts);
        break;
    case astIfStmt:
        CLONE_ONE(ifStmt, cond);
        CLONE_MANY(ifStmt, body);
        CLONE_MANY(ifStmt, otherwise);
        break;
    case astForStmt:
        CLONE_ONE(forStmt, var);
        CLONE_ONE(forStmt, range);
        CLONE_ONE(forStmt, body);
        break;
    case astWhileStmt:
        CLONE_ONE(whileStmt, cond);
        CLONE_ONE(whileStmt, body);
        break;
    case astSwitchStmt:
        CLONE_ONE(switchStmt, cond);
        CLONE_MANY(switchStmt, cases);
        break;
    case astCaseStmt:
        CLONE_ONE(caseStmt, match);
        CLONE_ONE(caseStmt, match);
        break;

    case astError:
    case astIdentifier:
    case astVoidType:
    case astAutoType:
    case astStringType:
    case astPrimitiveType:
    case astNullLit:
    case astBoolLit:
    case astCharLit:
    case astIntegerLit:
    case astFloatLit:
    case astStringLit:
    case astBreakStmt:
    case astContinueStmt:
    default:
        break;
    }

    return clone;
}

void insertAstNodeAfter(AstNode *before, AstNode *after)
{
    getLastAstNode(after)->next = before->next;
    before->next = after;
}

void insertAstNode(AstNodeList *list, AstNode *node)
{
    if (list->first == NULL) {
        list->first = node;
    }
    else {
        list->last->next = node;
    }
    list->last = node;
}

void unlinkAstNode(AstNode **head, AstNode *prev, AstNode *node)
{
    if (prev == node)
        *head = node->next;
    else
        prev->next = node->next;
}

const char *getDeclKeyword(AstTag tag)
{
    switch (tag) {
    case astFuncDecl:
        return "func";
    case astTypeDecl:
    case astUnionDecl:
        return "type";
    case astEnumDecl:
        return "enum";
    case astStructDecl:
        return "struct";
    default:
        return false;
    }
}

const char *getDeclName(const AstNode *node)
{
    switch (node->tag) {
    case astFuncDecl:
        return node->funcDecl.name;
    case astTypeDecl:
        return node->typeDecl.name;
    case astEnumDecl:
        return node->enumDecl.name;
    case astUnionDecl:
        return node->unionDecl.name;
    case astStructDecl:
        return node->structDecl.name;
    default:
        return false;
    }
}
