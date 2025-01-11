
#include "ast.h"
#include "capture.h"
#include "flag.h"
#include "strings.h"
#include "ttable.h"

#include "lang/middle/builtins.h"

typedef struct {
    const AstNode *from;
    AstNode *to;
} AstNodeMapping;

AstNode *cloneManyAstNodes(CloneAstConfig *config, const AstNode *nodes)
{
    AstNode *first = NULL, *prev = NULL;
    while (nodes) {
        if (!first) {
            first = cloneAstNode(config, nodes);
            prev = first;
        }
        else {
            prev->next = cloneAstNode(config, nodes);
            prev = prev->next;
        }
        nodes = nodes->next;
    }
    return first;
}

static bool compareAstNodes(const void *lhs, const void *rhs)
{
    return ((AstNodeMapping *)lhs)->from == ((AstNodeMapping *)rhs)->from;
}

static AstNode *findCorrespondingNode(HashTable *mapping, const AstNode *node)
{
    AstNodeMapping *found = findInHashTable(mapping,
                                            &(AstNodeMapping){.from = node},
                                            hashPtr(hashInit(), node),
                                            sizeof(AstNodeMapping),
                                            compareAstNodes);
    return found ? found->to : NULL;
}

static void replaceWithCorrespondingNode(HashTable *mapping, AstNode **node)
{
    if (*node) {
        AstNode *found = findCorrespondingNode(mapping, *node);
        if (found) {
            *node = found;
        }
    }
}

static void recordClonedAstNode(CloneAstConfig *config,
                                const AstNode *from,
                                AstNode *to)
{
    switch (from->tag) {
    case astFuncDecl:
    case astFuncParamDecl:
    case astStructDecl:
    case astClassDecl:
    case astFieldDecl:
    case astInterfaceDecl:
    case astUnionDecl:
    case astTypeDecl:
    case astGenericParam:
    case astDefine:
    case astGenericDecl:
    case astEnumDecl:
    case astEnumOptionDecl:
    case astVarDecl:
    case astIdentifier:
    case astClosureExpr:
    case astMatchStmt:
    case astBlockStmt:
        mapAstNode(&config->mapping, from, to);
        break;
    default:
        break;
    }

    if (from->parentScope) {
        to->parentScope = from->parentScope;
        replaceWithCorrespondingNode(&config->mapping, &to->parentScope);
    }
}

static void postCloneAstNode(CloneAstConfig *config,
                             const AstNode *from,
                             AstNode *to)
{
    switch (from->tag) {
    case astFuncDecl:
        if (from->list.first) {
            if (from->list.first == from) {
                to->list.first = to;
            }
            else {
                replaceWithCorrespondingNode(&config->mapping, &to->list.first);
                AstNode *prev = to->list.first, *node = prev->list.link;
                while (node) {
                    if (node == from) {
                        prev->list.link = to;
                        break;
                    }
                    prev = node;
                    node = node->list.link;
                }
            }
        }
        break;
    case astIdentifier:
        replaceWithCorrespondingNode(&config->mapping, &to->ident.resolvesTo);
        break;
    case astPathElem:
        if (from->pathElement.resolvesTo != config->root ||
            from->pathElement.args == NULL) {
            replaceWithCorrespondingNode(&config->mapping,
                                         &to->pathElement.resolvesTo);
        }
        else if (config->root) {
            // TODO proper fix for this
            to->pathElement.resolvesTo = config->root->parentScope;
        }
        break;
    case astReturnStmt:
        replaceWithCorrespondingNode(&config->mapping, &to->returnStmt.func);
        break;
    default:
        break;
    }
}

static Capture *cloneClosureCapture(CloneAstConfig *config,
                                    const Capture *from,
                                    u64 count)
{
    Capture *capture = allocFromMemPool(config->pool, sizeof(Capture) * count);
    for (u64 i = 0; i < count; i++) {
        capture[i] = from[i];
        capture[i].field = cloneAstNode(config, from[i].field);
        replaceWithCorrespondingNode(&config->mapping, &capture[i].node);
    }
    return capture;
}

static void unmapAstNode(HashTable *mapping, const AstNode *node)
{
    AstNodeMapping *found = findInHashTable(mapping,
                                            &(AstNodeMapping){.from = node},
                                            hashPtr(hashInit(), node),
                                            sizeof(AstNodeMapping),
                                            compareAstNodes);
    if (found)
        removeFromHashTable(mapping, found, sizeof(AstNodeMapping));
}

static SortedNodes *copySortedNodes(CloneAstConfig *config,
                                    const SortedNodes *src)
{
    if (src == NULL)
        return NULL;

    SortedNodes *sortedNodes = allocFromMemPool(
        config->pool, sizeof(SortedNodes) + (sizeof(AstNode *) * src->count));
    csAssert0(sortedNodes);

    memcpy(sortedNodes, src, sizeof(SortedNodes));

    for (u64 i = 0; i < src->count; i++) {
        replaceWithCorrespondingNode(&config->mapping, &sortedNodes->nodes[i]);
    }

    return sortedNodes;
}

AstNode *makeAstNode(MemPool *pool, const FileLoc *loc, const AstNode *init)
{
    AstNode *node = allocFromCacheOrPool(pool, memAstNode, sizeof(AstNode));
    memcpy(node, init, sizeof(AstNode));
    node->loc = *loc;
    return node;
}

AstNode *makeIntegerLiteral(MemPool *pool,
                            const FileLoc *loc,
                            i64 value,
                            AstNode *next,
                            const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){.tag = astIntegerLit,
                   .flags = flgNone,
                   .next = next,
                   .type = type,
                   .intLiteral = {.value = value, .isNegative = value < 0}});
}

AstNode *makeUnsignedIntegerLiteral(MemPool *pool,
                                    const FileLoc *loc,
                                    u64 value,
                                    AstNode *next,
                                    const Type *type)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astIntegerLit,
                                  .flags = flgNone,
                                  .next = next,
                                  .type = type,
                                  .intLiteral = {.uValue = value}});
}

AstNode *makeCharLiteral(MemPool *pool,
                         const FileLoc *loc,
                         i32 value,
                         AstNode *next,
                         const Type *type)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astCharLit,
                                  .flags = flgNone,
                                  .next = next,
                                  .type = type,
                                  .charLiteral = {.value = value}});
}

AstNode *makeBoolLiteral(MemPool *pool,
                         const FileLoc *loc,
                         bool value,
                         AstNode *next,
                         const Type *type)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astBoolLit,
                                  .flags = flgNone,
                                  .next = next,
                                  .type = type,
                                  .boolLiteral = {.value = value}});
}

AstNode *makeNullLiteral(MemPool *pool,
                         const FileLoc *loc,
                         AstNode *next,
                         const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){
            .tag = astNullLit, .flags = flgNone, .next = next, .type = type});
}

AstNode *makeFloatLiteral(MemPool *pool,
                          const FileLoc *loc,
                          f64 value,
                          AstNode *next,
                          const Type *type)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astFloatLit,
                                  .flags = flgNone,
                                  .next = next,
                                  .type = type,
                                  .floatLiteral = {.value = value}});
}

AstNode *makeStringLiteral(MemPool *pool,
                           const FileLoc *loc,
                           cstring value,
                           AstNode *next,
                           const Type *type)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astStringLit,
                                  .flags = flgNone,
                                  .next = next,
                                  .type = type,
                                  .stringLiteral = {.value = value}});
}

AstNode *makeSymbol(MemPool *pool,
                    const FileLoc *loc,
                    cstring value,
                    AstNode *next)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astSymbol,
                                  .flags = flgNone,
                                  .next = next,
                                  .symbol = {.value = value}});
}

AstNode *makeIdentifier(MemPool *pool,
                        const FileLoc *loc,
                        cstring name,
                        u32 super,
                        AstNode *next,
                        const Type *type)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astIdentifier,
                                  .flags = flgNone,
                                  .next = next,
                                  .type = type,
                                  .ident = {.value = name, .super = super}});
}

AstNode *makeResolvedIdentifier(MemPool *pool,
                                const FileLoc *loc,
                                cstring name,
                                u32 super,
                                AstNode *resolvesTo,
                                AstNode *next,
                                const Type *type)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astIdentifier,
                                  .flags = flgNone,
                                  .next = next,
                                  .type = type,
                                  .ident = {.value = name,
                                            .super = super,
                                            .resolvesTo = resolvesTo}});
}

AstNode *makePointerAstNode(MemPool *pool,
                            const FileLoc *loc,
                            u64 flags,
                            AstNode *pointed,
                            AstNode *next,
                            const Type *type)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astPointerType,
                                  .flags = flags,
                                  .next = next,
                                  .type = type,
                                  .pointerType = {.pointed = pointed}});
}

AstNode *makeReferenceTypeAstNode(MemPool *pool,
                                  const FileLoc *loc,
                                  u64 flags,
                                  AstNode *referred,
                                  AstNode *next,
                                  const Type *type)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astReferenceType,
                                  .flags = flags,
                                  .next = next,
                                  .type = type,
                                  .referenceType = {.referred = referred}});
}

AstNode *makeVoidAstNode(MemPool *pool,
                         const FileLoc *loc,
                         u64 flags,
                         AstNode *next,
                         const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){
            .tag = astVoidType, .flags = flags, .next = next, .type = type});
}

AstNode *makeVoidPointerAstNode(MemPool *pool,
                                const FileLoc *loc,
                                u64 flags,
                                AstNode *next)
{
    return makePointerAstNode(pool,
                              loc,
                              flags,
                              makeVoidAstNode(pool, loc, flgNone, NULL, NULL),
                              next,
                              NULL);
}

AstNode *makePath(MemPool *pool,
                  const FileLoc *loc,
                  cstring name,
                  u64 flags,
                  const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){
            .tag = astPath,
            .flags = flags,
            .type = type,
            .path.elements = makeAstNode(
                pool,
                loc,
                &(AstNode){.flags = flags,
                           .type = type,
                           .tag = astPathElem,
                           .pathElement = {.name = name,
                                           .isKeyword = (name == S_this ||
                                                         name == S_This ||
                                                         name == S_super)}})});
}

AstNode *makeResolvedPath(MemPool *pool,
                          const FileLoc *loc,
                          cstring name,
                          u64 flags,
                          AstNode *resolvesTo,
                          AstNode *next,
                          const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){
            .tag = astPath,
            .flags = flags,
            .type = type,
            .next = next,
            .path.elements = makeAstNode(
                pool,
                loc,
                &(AstNode){.flags = flags,
                           .type = type,
                           .tag = astPathElem,
                           .pathElement = {.name = name,
                                           .isKeyword = (name == S_this ||
                                                         name == S_This ||
                                                         name == S_super),
                                           .resolvesTo = resolvesTo}})});
}

AstNode *makeResolvedPathWithArgs(MemPool *pool,
                                  const FileLoc *loc,
                                  cstring name,
                                  u64 flags,
                                  AstNode *resolvesTo,
                                  AstNode *genericArgs,
                                  const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){
            .tag = astPath,
            .flags = flags,
            .type = type,
            .path.elements = makeAstNode(
                pool,
                loc,
                &(AstNode){.flags = flags,
                           .type = type,
                           .tag = astPathElem,
                           .pathElement = {.name = name,
                                           .isKeyword = (name == S_this ||
                                                         name == S_This ||
                                                         name == S_super),
                                           .resolvesTo = resolvesTo,
                                           .args = genericArgs}})});
}

AstNode *makePathWithElements(MemPool *pool,
                              const FileLoc *loc,
                              u64 flags,
                              AstNode *elements,
                              AstNode *next)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astPath,
                                  .flags = flags,
                                  .type = getLastAstNode(elements)->type,
                                  .next = next,
                                  .path.elements = elements});
}

AstNode *makeResolvedPathElement(MemPool *pool,
                                 const FileLoc *loc,
                                 cstring name,
                                 u64 flags,
                                 AstNode *resolvesTo,
                                 AstNode *next,
                                 const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){
            .flags = flags,
            .type = type,
            .tag = astPathElem,
            .next = next,
            .pathElement = {.name = name,
                            .isKeyword = (name == S_this || name == S_This ||
                                          name == S_super),
                            .resolvesTo = resolvesTo}});
}

AstNode *makeResolvedPathElementWithArgs(MemPool *pool,
                                         const FileLoc *loc,
                                         cstring name,
                                         u64 flags,
                                         AstNode *resolvesTo,
                                         AstNode *next,
                                         AstNode *genericArgs,
                                         const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){
            .flags = flags,
            .type = type,
            .tag = astPathElem,
            .next = next,
            .pathElement = {.name = name,
                            .isKeyword = (name == S_this || name == S_This ||
                                          name == S_super),
                            .resolvesTo = resolvesTo,
                            .args = genericArgs}});
}

AstNode *makeFieldExpr(MemPool *pool,
                       const FileLoc *loc,
                       cstring name,
                       u64 flags,
                       AstNode *value,
                       const AstNode *structField,
                       AstNode *next)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){.tag = astFieldExpr,
                   .flags = flags,
                   .type = value->type,
                   .next = next,
                   .fieldExpr = {.name = name,
                                 .value = value,
                                 .index = structField
                                              ? structField->structField.index
                                              : 0,
                                 .structField = structField}});
}

AstNode *makeStructField(MemPool *pool,
                         const FileLoc *loc,
                         cstring name,
                         u64 flags,
                         AstNode *type,
                         AstNode *def,
                         AstNode *next)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){.tag = astFieldDecl,
                   .flags = flags,
                   .type = type ? type->type : NULL,
                   .next = next,
                   .structField = {.name = name, .type = type, .value = def}});
}

AstNode *makeStructDecl(MemPool *pool,
                        const FileLoc *loc,
                        u64 flags,
                        cstring name,
                        AstNode *members,
                        AstNode *next,
                        const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){.tag = astStructDecl,
                   .flags = flags,
                   .type = type,
                   .next = next,
                   .structDecl = {.name = name, .members = members}});
}

AstNode *makeClassDecl(MemPool *pool,
                       const FileLoc *loc,
                       u64 flags,
                       cstring name,
                       AstNode *members,
                       AstNode *base,
                       AstNode *interfaces,
                       AstNode *next,
                       const Type *type)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astClassDecl,
                                  .flags = flags,
                                  .type = type,
                                  .next = next,
                                  .classDecl = {.name = name,
                                                .base = base,
                                                .implements = interfaces,
                                                .members = members}});
}

AstNode *makeGroupExpr(
    MemPool *pool, const FileLoc *loc, u64 flags, AstNode *exprs, AstNode *next)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astGroupExpr,
                                  .flags = flags,
                                  .type = exprs->type,
                                  .next = next,
                                  .groupExpr = {.expr = exprs}});
}

AstNode *makeUnionValueExpr(MemPool *pool,
                            const FileLoc *loc,
                            u64 flags,
                            AstNode *value,
                            u32 idx,
                            AstNode *next,
                            const Type *type)
{
    csAssert0(!type || typeIs(type, Union));
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astUnionValueExpr,
                                  .flags = flags,
                                  .type = type,
                                  .next = next,
                                  .unionValue = {.value = value, .idx = idx}});
}

AstNode *makeCastExpr(MemPool *pool,
                      const FileLoc *loc,
                      u64 flags,
                      AstNode *expr,
                      AstNode *target,
                      AstNode *next,
                      const Type *type)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astCastExpr,
                                  .flags = flags,
                                  .type = type,
                                  .next = next,
                                  .castExpr = {.expr = expr, .to = target}});
}

AstNode *makePointerOfExpr(MemPool *pool,
                           const FileLoc *loc,
                           u64 flags,
                           AstNode *operand,
                           AstNode *next,
                           const Type *type)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astPointerOf,
                                  .flags = flags,
                                  .type = type,
                                  .next = next,
                                  .unaryExpr = {.operand = operand,
                                                .op = opPtrof,
                                                .isPrefix = true}});
}

AstNode *makeReferenceOfExpr(MemPool *pool,
                             const FileLoc *loc,
                             u64 flags,
                             AstNode *operand,
                             AstNode *next,
                             const Type *type)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astReferenceOf,
                                  .flags = flags,
                                  .type = type,
                                  .next = next,
                                  .unaryExpr = {.operand = operand,
                                                .op = opRefof,
                                                .isPrefix = true}});
}

AstNode *makeTypedExpr(MemPool *pool,
                       const FileLoc *loc,
                       u64 flags,
                       AstNode *expr,
                       AstNode *target,
                       AstNode *next,
                       const Type *type)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astTypedExpr,
                                  .flags = flags,
                                  .type = type,
                                  .next = next,
                                  .typedExpr = {.expr = expr, .type = target}});
}

AstNode *makeTupleExpr(MemPool *pool,
                       const FileLoc *loc,
                       u64 flags,
                       AstNode *members,
                       AstNode *next,
                       const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){
            .tag = astTupleExpr,
            .flags = flags,
            .type = type,
            .next = next,
            .tupleExpr = {.elements = members, .len = countAstNodes(members)}});
}

AstNode *makeArrayExpr(MemPool *pool,
                       const FileLoc *loc,
                       u64 flags,
                       AstNode *members,
                       AstNode *next,
                       const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){
            .tag = astArrayExpr,
            .flags = flags,
            .type = type,
            .next = next,
            .arrayExpr = {.elements = members, .len = countAstNodes(members)}});
}

AstNode *makeTupleTypeAst(MemPool *pool,
                          const FileLoc *loc,
                          u64 flags,
                          AstNode *members,
                          AstNode *next,
                          const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){
            .tag = astTupleType,
            .flags = flags,
            .type = type,
            .next = next,
            .tupleType = {.elements = members, .len = countAstNodes(members)}});
}

AstNode *makeUnionDeclAst(MemPool *pool,
                          const FileLoc *loc,
                          u64 flags,
                          AstNode *members,
                          AstNode *next,
                          const Type *type)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astUnionDecl,
                                  .flags = flags,
                                  .type = type,
                                  .next = next,
                                  .unionDecl = {.members = members}});
}

AstNode *makeOptionalTypeAst(MemPool *pool,
                             const FileLoc *loc,
                             u64 flags,
                             AstNode *target,
                             AstNode *next,
                             const Type *type)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astOptionalType,
                                  .flags = flags,
                                  .type = type,
                                  .next = next,
                                  .optionalType = {.type = target}});
}

AstNode *makePrimitiveTypeAst(MemPool *pool,
                              const FileLoc *loc,
                              u64 flags,
                              PrtId id,
                              AstNode *next,
                              const Type *type)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astPrimitiveType,
                                  .flags = flags,
                                  .type = type,
                                  .next = next,
                                  .primitiveType = {.id = id}});
}

AstNode *makeStringTypeAst(MemPool *pool,
                           const FileLoc *loc,
                           u64 flags,
                           AstNode *next,
                           const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){
            .tag = astStringType, .flags = flags, .type = type, .next = next});
}

AstNode *makeCallExpr(MemPool *pool,
                      const FileLoc *loc,
                      AstNode *callee,
                      AstNode *args,
                      u64 flags,
                      AstNode *next,
                      const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){.tag = astCallExpr,
                   .flags = flags,
                   .type = type,
                   .next = next,
                   .callExpr = {.callee = callee, .args = args}});
}

AstNode *makeSpreadExpr(MemPool *pool,
                        const FileLoc *loc,
                        u64 flags,
                        AstNode *expr,
                        AstNode *next,
                        const Type *type)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astSpreadExpr,
                                  .flags = flags,
                                  .type = type,
                                  .next = next,
                                  .spreadExpr = {.expr = expr}});
}

AstNode *makeMemberExpr(MemPool *pool,
                        const FileLoc *loc,
                        u64 flags,
                        AstNode *target,
                        AstNode *member,
                        AstNode *next,
                        const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){.tag = astMemberExpr,
                   .flags = flags,
                   .type = type,
                   .next = next,
                   .memberExpr = {.target = target, .member = member}});
}

AstNode *makeExprStmt(MemPool *pool,
                      const FileLoc *loc,
                      u64 flags,
                      AstNode *expr,
                      AstNode *next,
                      const Type *type)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astExprStmt,
                                  .flags = flags,
                                  .type = type,
                                  .next = next,
                                  .exprStmt.expr = expr});
}

AstNode *makeDeferStmt(
    MemPool *pool, const FileLoc *loc, u64 flags, AstNode *stmt, AstNode *next)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astDeferStmt,
                                  .flags = flags,
                                  .next = next,
                                  .deferStmt.stmt = stmt});
}

AstNode *makeStmtExpr(MemPool *pool,
                      const FileLoc *loc,
                      u64 flags,
                      AstNode *stmt,
                      AstNode *next,
                      const Type *type)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astStmtExpr,
                                  .flags = flags,
                                  .type = type,
                                  .next = next,
                                  .stmtExpr.stmt = stmt});
}

AstNode *makeUnaryExpr(MemPool *pool,
                       const FileLoc *loc,
                       u64 flags,
                       bool isPrefix,
                       Operator op,
                       AstNode *operand,
                       AstNode *next,
                       const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){
            .tag = astUnaryExpr,
            .flags = flags,
            .type = type,
            .next = next,
            .unaryExpr = {.isPrefix = isPrefix, .op = op, .operand = operand}});
}

AstNode *makeBlockStmt(MemPool *pool,
                       const FileLoc *loc,
                       AstNode *stmts,
                       AstNode *next,
                       const Type *type)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astBlockStmt,
                                  .type = type,
                                  .next = next,
                                  .blockStmt.stmts = stmts});
}

AstNode *makeWhileStmt(MemPool *pool,
                       const FileLoc *loc,
                       u64 flags,
                       AstNode *condition,
                       AstNode *body,
                       AstNode *next,
                       AstNode *update)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){
            .tag = astWhileStmt,
            .type = body ? body->type : NULL,
            .next = next,
            .whileStmt = {.cond = condition, .body = body, .update = update}});
}

AstNode *makeIfStmt(MemPool *pool,
                    const FileLoc *loc,
                    u64 flags,
                    AstNode *condition,
                    AstNode *then,
                    AstNode *otherwise,
                    AstNode *next)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astIfStmt,
                                  .type = then ? then->type : NULL,
                                  .next = next,
                                  .ifStmt = {.cond = condition,
                                             .body = then,
                                             .otherwise = otherwise}});
}

AstNode *makeFunctionDecl(MemPool *pool,
                          const FileLoc *loc,
                          cstring name,
                          AstNode *params,
                          AstNode *returnType,
                          AstNode *body,
                          u64 flags,
                          AstNode *next,
                          const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){.tag = astFuncDecl,
                   .type = type,
                   .next = next,
                   .flags = flags,
                   .funcDecl = {.operatorOverload = opInvalid,
                                .name = name,
                                .signature = makeFunctionSignature(
                                    pool,
                                    &(FunctionSignature){.params = params,
                                                         .ret = returnType}),
                                .body = body}});
}

AstNode *makeFunctionType(MemPool *pool,
                          const FileLoc *loc,
                          AstNode *params,
                          AstNode *returnType,
                          u64 flags,
                          AstNode *next,
                          const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){.tag = astFuncType,
                   .type = type,
                   .next = next,
                   .flags = flags,
                   .funcType = {.params = params, .ret = returnType}});
}

AstNode *makeFunctionParam(MemPool *pool,
                           const FileLoc *loc,
                           cstring name,
                           AstNode *paramType,
                           AstNode *defaultValue,
                           u64 flags,
                           AstNode *next)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astFuncParamDecl,
                                  .type = paramType ? paramType->type : NULL,
                                  .next = next,
                                  .flags = flags,
                                  .funcParam = {.name = name,
                                                .def = defaultValue,
                                                .type = paramType}});
}

AstNode *makeOperatorOverload(MemPool *pool,
                              const FileLoc *loc,
                              Operator op,
                              AstNode *params,
                              AstNode *returnType,
                              AstNode *body,
                              u64 flags,
                              AstNode *next,
                              const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){.tag = astFuncDecl,
                   .type = type,
                   .next = next,
                   .flags = flags,
                   .funcDecl = {.operatorOverload = op,
                                .name = getOpOverloadName(op),
                                .signature = makeFunctionSignature(
                                    pool,
                                    &(FunctionSignature){.params = params,
                                                         .ret = returnType}),
                                .body = body}});
}

AstNode *makeNewExpr(MemPool *pool,
                     const FileLoc *loc,
                     u64 flags,
                     AstNode *target,
                     AstNode *init,
                     AstNode *next,
                     const Type *type)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astNewExpr,
                                  .type = type,
                                  .next = next,
                                  .flags = flags,
                                  .newExpr = {.type = target, .init = init}});
}

AstNode *makeStructExpr(MemPool *pool,
                        const FileLoc *loc,
                        u64 flags,
                        AstNode *left,
                        AstNode *fields,
                        AstNode *next,
                        const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){.tag = astStructExpr,
                   .type = type,
                   .next = next,
                   .flags = flags,
                   .structExpr = {.left = left, .fields = fields}});
}

AstNode *makeStructExprFromType(MemPool *pool,
                                const FileLoc *loc,
                                u64 flags,
                                AstNode *fields,
                                AstNode *next,
                                const Type *type)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astStructExpr,
                                  .type = type,
                                  .next = next,
                                  .flags = flags,
                                  .structExpr = {.left = makeTypeReferenceNode(
                                                     pool, type, loc),
                                                 .fields = fields}});
}

AstNode *makeVarDecl(MemPool *pool,
                     const FileLoc *loc,
                     u64 flags,
                     cstring name,
                     AstNode *varType,
                     AstNode *init,
                     AstNode *next,
                     const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){
            .tag = astVarDecl,
            .flags = flags,
            .type = type,
            .next = next,
            .varDecl = {.name = name,
                        .type = varType,
                        .names = makeAstNode(pool,
                                             loc,
                                             &(AstNode){.tag = astIdentifier,
                                                        .ident.value = name}),
                        .init = init}});
}

AstNode *makeArrayTypeAstNode(MemPool *pool,
                              const FileLoc *loc,
                              u64 flags,
                              AstNode *elementType,
                              u64 len,
                              AstNode *next,
                              const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){.tag = astArrayType,
                   .flags = flags,
                   .type = type,
                   .next = next,
                   .arrayType = {.elementType = elementType,
                                 .dim = makeIntegerLiteral(
                                     pool, loc, (i64)len, NULL, NULL)}});
}

AstNode *makeBinaryExpr(MemPool *pool,
                        const FileLoc *loc,
                        u64 flags,
                        AstNode *lhs,
                        Operator op,
                        AstNode *rhs,
                        AstNode *next,
                        const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){.tag = astBinaryExpr,
                   .flags = flags,
                   .type = type,
                   .next = next,
                   .binaryExpr = {.lhs = lhs, .op = op, .rhs = rhs}});
}

AstNode *makeAssignExpr(MemPool *pool,
                        const FileLoc *loc,
                        u64 flags,
                        AstNode *lhs,
                        Operator op,
                        AstNode *rhs,
                        AstNode *next,
                        const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){.tag = astAssignExpr,
                   .flags = flags,
                   .type = type,
                   .next = next,
                   .assignExpr = {.lhs = lhs, .op = op, .rhs = rhs}});
}

AstNode *makeIndexExpr(MemPool *pool,
                       const FileLoc *loc,
                       u64 flags,
                       AstNode *target,
                       AstNode *index,
                       AstNode *next,
                       const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){.tag = astIndexExpr,
                   .flags = flags,
                   .type = type,
                   .next = next,
                   .indexExpr = {.target = target, .index = index}});
}

AstNode *makeAttribute(MemPool *pool,
                       const FileLoc *loc,
                       cstring name,
                       AstNode *args,
                       AstNode *next)
{
    bool isKvp = nodeIs(args, FieldExpr);
    for (AstNode *arg = args; arg; arg = arg->next) {
        if (isKvp) {
            csAssert(
                nodeIs(arg, FieldExpr),
                "only key/value pairs can be used with this attribute express");
        }
        else {
            csAssert(isLiteralExpr(arg),
                     "only literals can be used in attribute expressions");
        }
    }

    return makeAstNode(
        pool,
        loc,
        &(AstNode){.tag = astAttr,
                   .next = next,
                   .attr = {.name = name, .args = args, .kvpArgs = isKvp}});
}

AstNode *makeBackendCallExpr(MemPool *pool,
                             const FileLoc *loc,
                             u64 flags,
                             BackendFuncId func,
                             AstNode *args,
                             const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){.tag = astBackendCall,
                   .flags = flags,
                   .type = type,
                   .next = NULL,
                   .backendCallExpr = {.func = func, .args = args}});
}

AstNode *makeEnumOptionAst(MemPool *pool,
                           const FileLoc *loc,
                           u64 flags,
                           cstring name,
                           AstNode *value,
                           AstNode *next,
                           const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){.tag = astEnumOptionDecl,
                   .flags = flags,
                   .type = type,
                   .next = NULL,
                   .enumOption = {.name = name, .value = value}});
}

AstNode *makeEnumAst(MemPool *pool,
                     const FileLoc *loc,
                     u64 flags,
                     cstring name,
                     AstNode *base,
                     AstNode *options,
                     AstNode *next,
                     const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){
            .tag = astEnumDecl,
            .flags = flags,
            .type = type,
            .next = NULL,
            .enumDecl = {.base = base, .name = name, .options = options}});
}

AstNode *makeProgramAstNode(MemPool *pool,
                            const FileLoc *loc,
                            u64 flags,
                            AstNode *module,
                            AstNode *top,
                            AstNode *decls,
                            const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){.tag = astProgram,
                   .flags = flags,
                   .type = type,
                   .next = NULL,
                   .program = {.module = module, .top = top, .decls = decls}});
}

AstNode *makeModuleAstNode(MemPool *pool,
                           const FileLoc *loc,
                           u64 flags,
                           cstring name,
                           Env *env,
                           const Type *type)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astModuleDecl,
                                  .flags = flags,
                                  .type = type,
                                  .next = NULL,
                                  .moduleDecl = {.name = name, .env = env}});
}

AstNode *makeTypeDeclAstNode(MemPool *pool,
                             const FileLoc *loc,
                             u64 flags,
                             cstring name,
                             AstNode *aliased,
                             AstNode *next,
                             const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){.tag = astTypeDecl,
                   .flags = flags,
                   .type = type,
                   .next = next,
                   .typeDecl = {.name = name, .aliased = aliased}});
}

AstNode *makeMacroDeclAstNode(MemPool *pool,
                              const FileLoc *loc,
                              u64 flags,
                              cstring name,
                              AstNode *params,
                              AstNode *body,
                              AstNode *next)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){
            .tag = astMacroDecl,
            .flags = flags,
            .next = next,
            .macroDecl = {.name = name, .params = params, .body = body}});
}

AstNode *makeMacroCallAstNode(MemPool *pool,
                              const FileLoc *loc,
                              u64 flags,
                              AstNode *callee,
                              AstNode *args,
                              AstNode *next)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){.tag = astMacroCallExpr,
                   .flags = flags,
                   .next = next,
                   .macroCallExpr = {.callee = callee, .args = args}});
}

AstNode *makeBasicBlockAstNode(MemPool *pool,
                               const FileLoc *loc,
                               u64 flags,
                               u32 index,
                               AstNode *func,
                               AstNodeList *stmts)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){.tag = astBasicBlock,
                   .flags = flags,
                   .parentScope = func,
                   .basicBlock = {.index = index, .stmts = *stmts}});
}

AstNode *makeReturnAstNode(MemPool *pool,
                           const FileLoc *loc,
                           u64 flags,
                           AstNode *expr,
                           AstNode *next,
                           const Type *type)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astReturnStmt,
                                  .flags = flags,
                                  .next = next,
                                  .type = type,
                                  .returnStmt = {.expr = expr}});
}

AstNode *makeYieldAstNode(MemPool *pool,
                          const FileLoc *loc,
                          u64 flags,
                          AstNode *expr,
                          AstNode *next,
                          const Type *type)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astYieldStmt,
                                  .flags = flags,
                                  .next = next,
                                  .type = type,
                                  .yieldStmt = {.expr = expr}});
}

AstNode *makeBranchIfAstNode(MemPool *pool,
                             const FileLoc *loc,
                             u64 flags,
                             AstNode *cond,
                             AstNode *trueBB,
                             AstNode *falseBB,
                             AstNode *next)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){
            .tag = astBranchIf,
            .flags = flags,
            .next = next,
            .branchIf = {.cond = cond, .trueBB = trueBB, .falseBB = falseBB}});
}

AstNode *makePhiAstNode(MemPool *pool,
                        const FileLoc *loc,
                        u64 flags,
                        AstNode **incoming,
                        u64 incomingCount,
                        AstNode *next)
{
    AstNode **incomingCopy =
        allocFromMemPool(pool, sizeof(AstNode *) * incomingCount);
    memcpy(incomingCopy, incoming, incomingCount);
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astPhi,
                                  .flags = flags,
                                  .next = next,
                                  .phi = {.incoming = incomingCopy,
                                          .incomingCount = incomingCount}});
}

AstNode *makeSwitchIrAstNode(MemPool *pool,
                             const FileLoc *loc,
                             u64 flags,
                             AstNode *cond,
                             AstNode *defaultBB,
                             SwitchIrCase *cases,
                             u64 casesCount,
                             AstNode *next)
{
    SwitchIrCase *casesCopy =
        allocFromMemPool(pool, sizeof(SwitchIrCase) * casesCount);
    memcpy(casesCopy, cases, sizeof(SwitchIrCase) * casesCount);
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astSwitchIr,
                                  .flags = flags,
                                  .next = next,
                                  .switchIr = {.cond = cond,
                                               .defaultBB = defaultBB,
                                               .cases = casesCopy,
                                               .casesCount = casesCount}});
}

AstNode *makeGepAstNode(MemPool *pool,
                        const FileLoc *loc,
                        u64 flags,
                        AstNode *value,
                        i64 index,
                        AstNode *next)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astGep,
                                  .flags = flags,
                                  .next = next,
                                  .gep = {.value = value, .index = index}});
}

AstNode *makeNodeArray(MemPool *pool,
                       const FileLoc *loc,
                       AstNode **nodes,
                       u64 nodesCount)
{
    AstNode **nodesCopy = allocFromMemPool(pool, sizeof(nodes) * nodesCount);
    memcpy(nodesCopy, nodes, sizeof(nodes) * nodesCount);
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astNodeArray,
                                  .flags = flgNone,
                                  .nodeArray = {.nodes = nodesCopy,
                                                .nodesCount = nodesCount}});
}

AstNode *makeBranchAstNode(MemPool *pool,
                           const FileLoc *loc,
                           u64 flags,
                           AstNode *target,
                           AstNode *cond,
                           AstNode *next)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){.tag = astBranch,
                   .flags = flags,
                   .next = next,
                   .branch = {.target = target, .condition = cond}});
}

AstNode *makeAstClosureCapture(MemPool *pool, AstNode *captured)
{
    return makeAstNode(pool,
                       &captured->loc,
                       &(AstNode){.tag = astIndexExpr,
                                  .flags = captured->flags,
                                  .type = captured->type,
                                  .capture = {.captured = captured}});
}

AstNode *makeAstNop(MemPool *pool, const FileLoc *loc)
{
    return makeAstNode(pool, loc, &(AstNode){.tag = astNoop});
}

AstNode *makePathFromIdent(MemPool *pool, const AstNode *ident)
{
    return makePath(
        pool, &ident->loc, ident->ident.value, ident->flags, ident->type);
}

AstNode *makeGenIdent(MemPool *pool,
                      StrPool *strPool,
                      const FileLoc *loc,
                      const Type *type)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){.tag = astIdentifier,
                   .ident.value = makeAnonymousVariable(strPool, "_gi")});
}

void clearAstBody(AstNode *node)
{
    memset(&node->_body, 0, CXY_AST_NODE_BODY_SIZE);
}

AstNode *copyAstNode(MemPool *pool, const AstNode *node)
{
    if (node == NULL)
        return NULL;

    AstNode *copy = allocFromCacheOrPool(pool, memAstNode, sizeof(AstNode));
    memcpy(copy, node, sizeof(AstNode));
    copy->next = NULL;
    copy->parentScope = NULL;
    return copy;
}

AstNode *duplicateAstNode(MemPool *pool, const AstNode *node)
{
    if (node == NULL)
        return NULL;

    AstNode *copy = allocFromCacheOrPool(pool, memAstNode, sizeof(AstNode));
    memcpy(copy, node, sizeof(AstNode));
    return copy;
}

bool isTuple(const AstNode *node)
{
    if (node->tag != astTupleExpr)
        return false;
    if (node->tupleExpr.elements->next == NULL)
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
    case astTypedExpr:
    case astCastExpr:
        return isLiteralExpr(node->castExpr.expr);
    case astGroupExpr:
        return isLiteralExpr(node->groupExpr.expr);
    case astUnaryExpr:
        return isLiteralExpr(node->unaryExpr.operand);
    default:
        return isEnumLiteral(node);
    }
}

bool isLiteralExprExt(const AstNode *node)
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
    case astTypedExpr:
    case astCastExpr:
        return isLiteralExprExt(node->castExpr.expr);
    case astGroupExpr:
        return isLiteralExprExt(node->groupExpr.expr);
    case astUnaryExpr:
        return isLiteralExprExt(node->unaryExpr.operand);
    case astTupleExpr:
        for (AstNode *elem = node->tupleExpr.elements; elem;
             elem = elem->next) {
            if (!isLiteralExprExt(elem))
                return false;
        }
        return true;
    case astStructExpr:
        for (AstNode *elem = node->structExpr.fields; elem; elem = elem->next) {
            if (!nodeIs(elem, FieldDecl))
                continue;
            if (!isLiteralExprExt(elem->structField.value))
                return false;
        }
        return true;
    case astArrayExpr:
        for (AstNode *elem = node->arrayExpr.elements; elem;
             elem = elem->next) {
            if (!isLiteralExprExt(elem))
                return false;
        }
        return true;
    default:
        return isEnumLiteral(node);
    }
}

bool isSizeofExpr(const AstNode *node)
{
    if (node == NULL)
        return false;
    switch (node->tag) {
    case astBackendCall:
        return node->backendCallExpr.func == bfiSizeOf;
    case astTypedExpr:
    case astCastExpr:
        return isLiteralExpr(node->castExpr.expr);
    case astGroupExpr:
        return isLiteralExpr(node->groupExpr.expr);
    case astUnaryExpr:
        return isLiteralExpr(node->unaryExpr.operand);
    default:
        return false;
    }
}

bool isStaticExpr(const AstNode *node)
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
    case astIdentifier:
        return nodeIs(node->ident.resolvesTo, FuncDecl);
    case astMemberExpr:
        return isStaticExpr(node->memberExpr.target);
    case astTupleExpr:
        for (AstNode *elem = node->tupleExpr.elements; elem;
             elem = elem->next) {
            if (!isStaticExpr(elem))
                return false;
        }
        return true;
    case astStructExpr:
        for (AstNode *elem = node->structExpr.fields; elem; elem = elem->next) {
            if (!nodeIs(elem, FieldDecl))
                continue;
            if (!isStaticExpr(elem->structField.value))
                return false;
        }
        return true;
    case astArrayExpr:
        for (AstNode *elem = node->arrayExpr.elements; elem;
             elem = elem->next) {
            if (!isStaticExpr(elem))
                return false;
        }
        return true;
    case astCastExpr:
    case astTypedExpr:
        return isStaticExpr(node->castExpr.expr);
    case astGroupExpr:
        return isStaticExpr(node->groupExpr.expr);
    case astUnaryExpr:
        return isStaticExpr(node->unaryExpr.operand);
    default:
        return false;
    }
}

bool isEnumLiteral(const AstNode *node)
{
    if (!typeIs(node->type, Enum))
        return false;
    switch (node->tag) {
    case astPath:
        return hasFlag(node->path.elements->next, EnumLiteral);
    case astMemberExpr:
        return hasFlag(node->memberExpr.member, EnumLiteral);
    default:
        return false;
    }
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

bool isNumericLiteral(const AstNode *node)
{
    switch (node->tag) {
    case astIntegerLit:
    case astFloatLit:
    case astCharLit:
    case astBoolLit:
        return true;
    default:
        return false;
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
    case astReferenceType:
    case astFuncType:
    case astPrimitiveType:
    case astOptionalType:
    case astStructDecl:
    case astClassDecl:
    case astEnumDecl:
    case astUnionDecl:
    case astFuncDecl:
    case astTypeRef:
    case astGenericParam:
        return true;
    case astRef:
        return isTypeExpr(node->reference.target);
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

bool isMemberFunction(const AstNode *node)
{
    if (!nodeIs(node, FuncDecl))
        return false;
    if (node->funcDecl.this_)
        return true;
    return hasFlag(node, Generated) &&
           node->funcDecl.signature->params != NULL &&
           node->funcDecl.signature->params->funcParam.name == S_this;
}

bool isStaticMemberFunction(const AstNode *node)
{
    if (!nodeIs(node, FuncDecl))
        return false;
    return isClassOrStructAstNode(node->parentScope) && hasFlag(node, Static);
}

AstNode *getMemberFunctionThis(AstNode *node)
{
    if (!nodeIs(node, FuncDecl))
        return NULL;
    if (node->funcDecl.this_)
        return node->funcDecl.this_;
    return (hasFlag(node, Generated) &&
            node->funcDecl.signature->params != NULL &&
            node->funcDecl.signature->params->funcParam.name == S_this)
               ? node->funcDecl.signature->params
               : NULL;
}

u64 countAstNodes(const AstNode *node)
{
    u64 len = 0;
    for (; node; node = node->next)
        len++;
    return len;
}

u64 countProgramDecls(const AstNode *node)
{
    u64 len = 0;
    for (; node; node = node->next) {
        if (nodeIs(node, ImportDecl))
            continue;
        if (nodeIs(node, Define)) {
            len += (node->define.container ? 1
                                           : countAstNodes(node->define.names));
        }
        else
            len++;
    }
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

cstring getNamedNodeName(const AstNode *member)
{
    switch (member->tag) {
    case astFuncDecl:
    case astFieldDecl:
    case astGenericDecl:
    case astStructDecl:
    case astUnionDecl:
    case astTypeDecl:
    case astEnumDecl:
    case astAttr:
    case astAnnotation:
    case astFuncParamDecl:
        return member->_name;
    default:
        return NULL;
    }
}

AstNode *findMemberByName(AstNode *node, cstring name)
{
    AstNode *member = node;
    while (member) {
        if (getNamedNodeName(member) == name)
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

AstNode *replaceAstNode(AstNode *node, const AstNode *with)
{
    AstNode *next = node->next, *parent = node->parentScope;
    *node = *with;
    node->parentScope = parent;
    getLastAstNode(node)->next = next;

    return node;
}

void replaceAstNodeInList(AstNode **list, const AstNode *node, AstNode *with)
{
    if (*list == node) {
        getLastAstNode(with)->next = (*list)->next;
        *list = with;
    }
    else {
        AstNode *prev = *list;
        for (AstNode *it = (*list)->next; it; it = it->next) {
            if (it == node) {
                prev->next = with;
                break;
            }
            prev = it;
        }
    }
}

AstNode *replaceAstNodeWith(AstNode *node, const AstNode *with)
{
    __typeof(node->_head) head = node->_head;
    *node = *with;
    node->flags |= head.flags;
    getLastAstNode(node)->next = head.next;
    node->parentScope = head.parentScope;

    return node;
}

bool mapAstNode(HashTable *mapping, const AstNode *from, AstNode *to)
{
    return insertInHashTable(mapping,
                             &(AstNodeMapping){.from = from, .to = to},
                             hashPtr(hashInit(), from),
                             sizeof(AstNodeMapping),
                             compareAstNodes);
}

void initCloneAstNodeMapping(CloneAstConfig *config)
{
    if (config->createMapping) {
        config->mapping = newHashTable(sizeof(AstNodeMapping));
    }
}

void deinitCloneAstNodeConfig(CloneAstConfig *config)
{
    if (config->createMapping) {
        freeHashTable(&config->mapping);
    }
}

AstNode *cloneAstNode(CloneAstConfig *config, const AstNode *node)
{
    if (node == NULL)
        return NULL;

    AstNode *clone = copyAstNode(config->pool, node);
    if (config->createMapping)
        recordClonedAstNode(config, node, clone);

#define CLONE_MANY(AST, MEMBER)                                                \
    clone->AST.MEMBER = cloneManyAstNodes(config, node->AST.MEMBER);
#define CLONE_ONE(AST, MEMBER)                                                 \
    clone->AST.MEMBER = cloneAstNode(config, node->AST.MEMBER);

#define COPY_SORTED(AST, MEMBER)                                               \
    if (config->createMapping)                                                 \
    clone->AST.MEMBER = copySortedNodes(config, node->AST.MEMBER)

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
    case astBackendCall:
        CLONE_MANY(backendCallExpr, args);
        break;
    case astAsm:
        CLONE_MANY(inlineAssembly, outputs);
        CLONE_MANY(inlineAssembly, inputs);
        CLONE_MANY(inlineAssembly, clobbers);
        break;
    case astAsmOperand:
        CLONE_ONE(asmOperand, operand);
        break;
    case astGenericParam:
        CLONE_MANY(genericParam, constraints);
        break;
    case astGenericDecl:
        CLONE_MANY(genericDecl, params);
        CLONE_ONE(genericDecl, decl);
        break;
    case astTupleType:
        CLONE_MANY(tupleType, elements);
        break;
    case astArrayType:
        CLONE_ONE(arrayType, elementType);
        CLONE_MANY(arrayType, dim);
        break;
    case astPointerType:
        CLONE_ONE(pointerType, pointed);
        break;
    case astReferenceType:
        CLONE_ONE(referenceType, referred);
        break;
    case astOptionalType:
        CLONE_ONE(optionalType, type);
        break;
    case astFuncType:
        CLONE_ONE(funcType, ret);
        CLONE_MANY(funcType, params);
        break;
    case astFuncParamDecl:
        CLONE_ONE(funcParam, type);
        CLONE_ONE(funcParam, def);
        break;
    case astFuncDecl:
        if (clone->funcDecl.this_)
            CLONE_ONE(funcDecl, this_);
        clone->funcDecl.signature = makeFunctionSignature(
            config->pool,
            &(FunctionSignature){
                .ret = cloneAstNode(config, node->funcDecl.signature->ret),
                .params =
                    cloneManyAstNodes(config, node->funcDecl.signature->params),
                .typeParams = node->funcDecl.signature->typeParams});
        if (!config->signatureOnly) {
            CLONE_ONE(funcDecl, body);
        }
        else {
            pushOnDynArray(config->deferred, &clone);
        }
        CLONE_MANY(funcDecl, opaqueParams)
        if (clone->funcDecl.this_)
            clone->funcDecl.this_->next = clone->funcDecl.signature->params;
        break;
    case astMacroDecl:
        CLONE_MANY(macroDecl, params);
        CLONE_ONE(macroDecl, body);
        break;
    case astVarDecl:
        CLONE_ONE(varDecl, type);
        CLONE_ONE(varDecl, init);
        CLONE_MANY(varDecl, names);
        break;
    case astTypeDecl:
        if (!hasFlag(node, ForwardDecl))
            CLONE_ONE(typeDecl, aliased);
        break;
    case astUnionDecl:
        CLONE_MANY(unionDecl, members);
        COPY_SORTED(unionDecl, sortedMembers);

    case astStructDecl:
        CLONE_MANY(structDecl, members);
        break;

    case astClassDecl:
        CLONE_MANY(classDecl, members);
        CLONE_ONE(classDecl, base);
        CLONE_MANY(classDecl, implements);
        break;

    case astInterfaceDecl:
        CLONE_MANY(interfaceDecl, members);
        break;

    case astEnumOptionDecl:
        CLONE_ONE(enumOption, value);
        break;

    case astEnumDecl:
        CLONE_MANY(enumDecl, options);
        CLONE_ONE(enumDecl, base);
        COPY_SORTED(enumDecl, sortedOptions);
        break;

    case astFieldDecl:
        if (!config->signatureOnly) {
            CLONE_ONE(structField, value)
        }
        else {
            pushOnDynArray(config->deferred, &clone);
        }
        CLONE_ONE(structField, type)
        break;

    case astGroupExpr:
    case astSpreadExpr:
    case astExprStmt:
    case astDeferStmt:
        CLONE_ONE(groupExpr, expr);
        break;
    case astUnaryExpr:
    case astPointerOf:
    case astReferenceOf:
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
        CLONE_ONE(callExpr, callee);
        CLONE_MANY(callExpr, args);
        break;
    case astMacroCallExpr:
        CLONE_ONE(callExpr, callee);
        CLONE_MANY(callExpr, args);
        break;
    case astClosureExpr:
        if (node->closureExpr.captureCount)
            clone->closureExpr.capture =
                cloneClosureCapture(config,
                                    node->closureExpr.capture,
                                    node->closureExpr.captureCount);
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
        CLONE_MANY(tupleExpr, elements);
        break;

    case astUnionValueExpr:
        CLONE_MANY(unionValue, value);
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
    case astMatchStmt:
        CLONE_ONE(matchStmt, expr);
        CLONE_MANY(matchStmt, cases);
        break;
    case astCaseStmt:
        CLONE_ONE(caseStmt, match);
        CLONE_ONE(caseStmt, variable);
        CLONE_ONE(caseStmt, body);
        break;

    case astIdentifier:
    case astError:
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

    if (config->createMapping)
        postCloneAstNode(config, node, clone);

    return clone;
}

AstNode *cloneGenericDeclaration(MemPool *pool, const AstNode *node)
{
    AstNode *param = node->genericDecl.params;
    AstNode *decl = node->genericDecl.decl;
    AstNode *params = NULL, *it = NULL;
    CloneAstConfig config = {.pool = pool, .createMapping = true, .root = decl};

    initCloneAstNodeMapping(&config);

    for (; param; param = param->next) {
        AstNode *clone = cloneAstNode(&config, param);
        if (params == NULL)
            params = it = clone;
        else
            it = it->next = clone;
    }

    config.signatureOnly = isClassOrStructAstNode(decl);
    if (config.signatureOnly) {
        DynArray deferred = newDynArray(sizeof(AstNode *));
        config.deferred = &deferred;
        decl = cloneAstNode(&config, decl);
        for (int i = 0; i < deferred.size; i++) {
            AstNode *member = dynArrayAt(AstNode **, &deferred, i);
            if (nodeIs(member, FuncDecl)) {
                member->funcDecl.body =
                    cloneAstNode(&config, member->funcDecl.body);
            }
            else if (nodeIs(member, FieldDecl)) {
                member->structField.value =
                    cloneAstNode(&config, member->structField.value);
            }
        }
        freeDynArray(&deferred);
    }
    else {
        decl = cloneAstNode(&config, decl);
    }
    setGenericDeclarationParams(decl, params);
    decl->attrs = node->attrs;
    decl->parentScope = node->parentScope;
    deinitCloneAstNodeConfig(&config);
    return decl;
}

AstNode *deepCloneAstNode(MemPool *pool, const AstNode *node)
{
    CloneAstConfig config = {.pool = pool, .createMapping = true};
    initCloneAstNodeMapping(&config);
    AstNode *cloned = cloneAstNode(&config, node);
    deinitCloneAstNodeConfig(&config);
    return cloned;
}

AstNode *deepCloneManyAstNode(MemPool *pool, const AstNode *node)
{
    CloneAstConfig config = {.pool = pool, .createMapping = true};
    initCloneAstNodeMapping(&config);
    AstNodeList nodes = {};
    for (const AstNode *it = node; it; it = it->next) {
        insertAstNode(&nodes, cloneAstNode(&config, it));
    }
    deinitCloneAstNodeConfig(&config);
    return nodes.first;
}

void insertAstNodeAfter(AstNode *before, AstNode *after)
{
    getLastAstNode(after)->next = before->next;
    before->next = after;
}

AstNode *insertAstNode(AstNodeList *list, AstNode *node)
{
    if (node == NULL)
        return NULL;

    if (list->first == NULL) {
        list->first = node;
    }
    else {
        list->last->next = node;
    }
    list->last = getLastAstNode(node);
    return list->last;
}

void unlinkAstNode(AstNode **head, AstNode *prev, AstNode *node)
{
    if (prev == node)
        *head = node->next;
    else
        prev->next = node->next;
}

const AstNode *findAttribute(const AstNode *node, cstring name)
{
    const AstNode *attr = node->attrs;
    while (attr) {
        if (name == attr->attr.name)
            break;
        attr = attr->next;
    }

    return attr;
}

const AstNode *findAttributeArgument(const AstNode *attr, cstring name)
{
    const AstNode *arg = attr->attr.args;
    while (arg) {
        if (name == arg->fieldExpr.name)
            break;
        arg = arg->next;
    }
    return arg ? arg->fieldExpr.value : NULL;
}

const AstNode *getAttributeArgument(Log *L,
                                    const FileLoc *loc,
                                    const AstNode *attr,
                                    u32 index)
{
    if (attr->attr.kvpArgs) {
        if (L) {
            logError(L,
                     loc,
                     "attribute '{s}' is defined with key-value arguments",
                     (FormatArg[]){{.s = attr->attr.name}});
            logNote(L, &attr->loc, "attribute defined here", NULL);
        }
        return NULL;
    }

    AstNode *node = getNodeAtIndex(attr->attr.args, index);
    if (L && node == NULL) {
        logError(L,
                 loc,
                 "given index `{u32}` is out of range for attribute arguments",
                 (FormatArg[]){{.u32 = index}});
        logNote(L, &attr->loc, "attribute defined here", NULL);
    }
    return node;
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
    case astClassDecl:
        return "class";
    default:
        return false;
    }
}

const char *getDeclarationName(const AstNode *node)
{
    node = nodeIs(node, GenericDecl) ? node->genericDecl.decl : node;

    switch (node->tag) {
    case astFuncDecl:
        return node->funcDecl.name;
    case astMacroDecl:
        return node->macroDecl.name;
    case astTypeDecl:
        return node->typeDecl.name;
    case astEnumDecl:
        return node->enumDecl.name;
    case astStructDecl:
    case astClassDecl:
        return node->structDecl.name;
    case astInterfaceDecl:
        return node->interfaceDecl.name;
    case astDefine:
        return node->define.container->ident.value;
    case astVarDecl:
        return node->varDecl.name;
    case astFuncParamDecl:
        return node->funcParam.name;
    case astException:
        return node->exception.name;
    default:
        csAssert(false, "%s is not a declaration", getAstNodeName(node));
    }
}

void setDeclarationName(AstNode *node, cstring name)
{
    switch (node->tag) {
    case astFuncDecl:
        node->funcDecl.name = name;
        break;
    case astMacroDecl:
        node->macroDecl.name = name;
        break;
    case astTypeDecl:
        node->typeDecl.name = name;
        break;
    case astEnumDecl:
        node->enumDecl.name = name;
        break;
    case astStructDecl:
    case astClassDecl:
        node->structDecl.name = name;
        break;
    case astInterfaceDecl:
        node->interfaceDecl.name = name;
        break;
    default:
        csAssert(false, "%s is not a declaration", getAstNodeName(node));
    }
}

void setForwardDeclDefinition(AstNode *node, AstNode *definition)
{
    csAssert0(hasFlag(node, ForwardDecl));
    switch (node->tag) {
    case astFuncDecl:
        node->funcDecl.definition = definition;
        break;
    case astMacroDecl:
        node->macroDecl.definition = definition;
        break;
    case astTypeDecl:
        node->typeDecl.definition = definition;
        break;
    default:
        csAssert(false, "%s is not a declaration", getAstNodeName(node));
    }
}

AstNode *getForwardDeclDefinition(AstNode *node)
{
    switch (node->tag) {
    case astFuncDecl:
        return node->funcDecl.definition;
    case astMacroDecl:
        return node->macroDecl.definition;
    case astTypeDecl:
        return node->typeDecl.definition;
    default:
        return NULL;
    }
}

AstNode *getGenericDeclarationParams(AstNode *node)
{
    switch (node->tag) {
    case astFuncDecl:
        return node->funcDecl.signature->typeParams;
    case astTypeDecl:
        return node->typeDecl.typeParams;
    case astUnionDecl:
        return node->unionDecl.typeParams;
    case astStructDecl:
    case astClassDecl:
        return node->structDecl.typeParams;
    case astInterfaceDecl:
        return node->interfaceDecl.typeParams;
    default:
        csAssert(false, "%s is not a declaration", getAstNodeName(node));
    }
}

void setGenericDeclarationParams(AstNode *node, AstNode *params)
{
    switch (node->tag) {
    case astFuncDecl:
        node->funcDecl.signature->typeParams = params;
        break;
    case astTypeDecl:
        node->typeDecl.typeParams = params;
        break;
    case astUnionDecl:
        node->unionDecl.typeParams = params;
        break;
    case astStructDecl:
    case astClassDecl:
        node->structDecl.typeParams = params;
        break;
    case astInterfaceDecl:
        node->interfaceDecl.typeParams = params;
        break;
    default:
        csAssert(false, "%s is not a declaration", getAstNodeName(node));
    }
}

cstring getAstNodeName(const AstNode *node)
{
    switch (node->tag) {
#define f(name)                                                                \
    case ast##name:                                                            \
        return #name;
        CXY_LANG_AST_TAGS(f)
#undef f
    default:
        return "<max>";
    }
}

FunctionSignature *makeFunctionSignature(MemPool *pool,
                                         const FunctionSignature *from)
{
    FunctionSignature *signature = allocFromMemPool(pool, sizeof *from);
    *signature = *from;
    return signature;
}

AstNode *getParentScope(AstNode *node)
{
    if (nodeIs(node->parentScope, GenericDecl)) {
        return node->parentScope->parentScope;
    }
    return node->parentScope;
}

AstNode *getMemberParentScope(AstNode *node)
{
    AstNode *parent = node->parentScope;
    if (nodeIs(parent, BlockStmt)) {
        parent = parent->parentScope;
    }

    if (nodeIs(parent, IfStmt) || nodeIs(parent, ForStmt)) {
        if (!hasFlag(parent, Comptime))
            return false;
        parent = parent->parentScope;
    }

    if (nodeIs(parent, GenericDecl)) {
        parent = parent->parentScope;
    }

    return parent;
}

AstNode *makeTypeReferenceNode(MemPool *pool,
                               const Type *type,
                               const FileLoc *loc)
{
    return makeAstNode(
        pool,
        loc,
        &(AstNode){.tag = astTypeRef, .flags = type->flags, .type = type});
}

AstNode *makeTypeReferenceNode2(MemPool *pool,
                                const Type *type,
                                const FileLoc *loc,
                                AstNode *next)
{
    return makeAstNode(pool,
                       loc,
                       &(AstNode){.tag = astTypeRef,
                                  .flags = type->flags,
                                  .type = type,
                                  .next = next});
}

AstNode *findInAstNode(AstNode *node, cstring name)
{
    node = underlyingDeclaration(node);
    switch (node->tag) {
    case astStructDecl:
        return findMemberByName(node->structDecl.members, name);
    case astClassDecl:
        return findMemberByName(node->classDecl.members, name);
    case astInterfaceDecl:
        return findMemberByName(node->interfaceDecl.members, name);
    case astUnionDecl:
        return findMemberByName(node->unionDecl.members, name);
    case astEnumDecl:
        return findMemberByName(node->enumDecl.options, name);
    case astException:
        return findMemberByName(node->exception.params, name);
    default:
        unreachable("NOT SUPPORTED");
    }
}

AstNode *findInComptimeIterable(AstNode *node, cstring name)
{
    if (node == NULL || name == NULL)
        return NULL;
    AstNode *it = node->next;
    for (; it; it = it->next) {
        if (name == getNamedNodeName(it)) {
            return nodeIs(it, Annotation) ? it->annotation.value : it;
        }
    }
    return NULL;
}

AstNode *resolvePath(const AstNode *path)
{
    if (path == NULL)
        return NULL;

    AstNode *base = path->path.elements;
    if (base->pathElement.resolvesTo == NULL)
        return NULL;

    AstNode *resolved = base->pathElement.resolvesTo;

    AstNode *elem = base->next;
    for (; elem && resolved; elem = elem->next) {
        if (elem->pathElement.resolvesTo == NULL) {
            elem->pathElement.resolvesTo = findInAstNode(
                resolved, elem->pathElement.alt ?: elem->pathElement.name);
        }
        resolved = elem->pathElement.resolvesTo;
    }
    return resolved;
}

AstNode *resolveAstNode(AstNode *node)
{
    if (node == NULL)
        return node;

    switch (node->tag) {
    case astTypeDecl:
        return resolveAstNode(node->typeDecl.aliased);
    case astPath:
        return resolveAstNode(node->path.elements);
    case astPathElem:
        return resolveAstNode(node->pathElement.resolvesTo);
    case astIdentifier:
        return resolveAstNode(node->ident.resolvesTo);
    default:
        return node;
    }
}

AstNode *resolveIdentifier(AstNode *node)
{
    if (node == NULL)
        return NULL;
    switch (node->tag) {
    case astIdentifier:
        return node->ident.resolvesTo;
    case astCastExpr:
    case astTypedExpr:
        return resolveIdentifier(node->castExpr.expr);
    case astGroupExpr:
        return resolveIdentifier(node->groupExpr.expr);
    case astPointerOf:
    case astReferenceOf:
    case astUnaryExpr: {
        Operator op = node->unaryExpr.op;
        if (op == opMove || op == opPtrof || opRefof || op == opDeref)
            return resolveIdentifier(node->unaryExpr.operand);
    }
    default:
        return NULL;
    }
}

AstNode *getResolvedPath(const AstNode *path)
{
    if (!nodeIs(path, Path))
        return NULL;
    AstNode *elem = path->path.elements, *resolved = NULL;
    do {
        resolved = elem->pathElement.resolvesTo;
        elem = elem->next;
    } while (elem);

    return resolved;
}

static int isInInheritanceChain_(const AstNode *node,
                                 const AstNode *parent,
                                 int depth)
{
    if (!nodeIs(node, ClassDecl) || node->classDecl.base == NULL)
        return 0;
    const AstNode *base = resolvePath(node->classDecl.base);
    if (base == NULL)
        return 0;
    if (base == parent)
        return depth;
    return isInInheritanceChain_(base, parent, depth + 1);
}

int isInInheritanceChain(const AstNode *node, const AstNode *parent)
{
    return isInInheritanceChain_(node, parent, 1);
}

AstNode *getBaseClassAtLevel(AstNode *node, u64 level)
{
    int i = 0;
    do {
        if (i == level)
            return node;

        node = underlyingDeclaration(resolvePath(node->classDecl.base));
        i++;
    } while (node);
    return node;
}

AstNode *getBaseClassByName(AstNode *node, cstring name)
{
    for (;;) {
        node = underlyingDeclaration(resolvePath(node->classDecl.base));
        if (node == NULL)
            return NULL;

        if (strncmp(node->structDecl.name, name, strlen(name)) == 0)
            return node;
    }
}

int compareNamedAstNodes(const void *lhs, const void *rhs)
{
    cstring left = (*((const AstNode **)lhs))->_namedNode.name,
            right = (*((const AstNode **)rhs))->_namedNode.name;
    return left == right ? 0 : strcmp(left, right);
}

SortedNodes *makeSortedNodes(MemPool *pool,
                             AstNode *nodes,
                             int (*compare)(const void *, const void *))
{
    u64 count = countAstNodes(nodes);
    if (nodes == NULL)
        return NULL;
    SortedNodes *sortedNodes = allocFromMemPool(
        pool, sizeof(SortedNodes) + (sizeof(AstNode *) * count));

    csAssert0(sortedNodes);
    makeSortedNodesInMemory(sortedNodes, nodes, count, compare);
    return sortedNodes;
}

void makeSortedNodesInMemory(SortedNodes *sortedNodes,
                             AstNode *nodes,
                             u64 count,
                             int (*compare)(const void *, const void *))
{
    if (count == 0)
        return;
    sortedNodes->count = count;
    sortedNodes->compare = compare ?: compareNamedAstNodes;

    AstNode *node = nodes;
    for (u64 i = 0; node; node = node->next, i++)
        sortedNodes->nodes[i] = node;

    qsort(sortedNodes->nodes, count, sizeof(AstNode *), sortedNodes->compare);
}

AstNode *findInSortedNodes(SortedNodes *sorted, cstring name)
{
    if (sorted == NULL)
        return NULL;

    int found = binarySearchWithRef(sorted->nodes,
                                    sorted->count,
                                    &(AstNode){._namedNode.name = name},
                                    sizeof(struct AstNode *),
                                    sorted->compare);
    if (found < 0)
        return NULL;

    return sorted->nodes[found];
}

AstNode *nodeGetFuncParams(const AstNode *decl)
{
    if (nodeIs(decl, FuncDecl))
        return decl->funcDecl.signature->params;
    csAssert0(nodeIs(decl, FuncType));
    return decl->funcType.params;
}

CCodeKind getCCodeKind(TokenTag tag)
{
    switch (tag) {
    case tokCInclude:
        return cInclude;
    case tokCDefine:
        return cDefine;
    case tokCSources:
        return cSources;
    default:
        unreachable();
    }
}

cstring getBackendCallString(BackendFuncId bfi)
{
    switch (bfi) {
#define f(NAME)                                                                \
    case bfi##NAME:                                                            \
        return #NAME;
        BACKEND_FUNC_IDS(f)
    default:
        return "Unknown";
    }
}

bool nodeIsMemberFunctionReference(const AstNode *node)
{
    if (!nodeIs(node, MemberExpr) || !typeIs(node->type, Func))
        return false;
    AstNode *target = node->memberExpr.target;
    if (nodeIs(target, TypeRef))
        return true;
    if (nodeIs(target, Identifier))
        target = target->ident.resolvesTo;
    return isClassOrStructAstNode(target) || nodeIs(node, ModuleDecl);
}

bool nodeIsModuleFunctionRef(const AstNode *node)
{
    if (!nodeIs(node, MemberExpr) || !typeIs(node->type, Func))
        return false;
    AstNode *target = node->memberExpr.target;
    if (nodeIs(target, TypeRef))
        return true;
    if (nodeIs(target, Identifier))
        target = target->ident.resolvesTo;
    return typeIs(target->type, Module);
}

bool nodeIsEnumOptionReference(const AstNode *node)
{
    if (!nodeIs(node, MemberExpr) || !typeIs(node->type, Enum))
        return false;
    AstNode *target = node->memberExpr.target;
    if (nodeIs(target, MemberExpr))
        target = target->memberExpr.member;

    if (target == NULL || nodeIs(target, TypeRef))
        return true;

    if (nodeIs(target, Identifier))
        target = target->ident.resolvesTo;
    return nodeIs(target, EnumDecl) || nodeIs(target, TypeDecl) ||
           nodeIs(target, GenericParam);
}

bool nodeIsLeftValue(const AstNode *node)
{
    if (node == NULL)
        return false;
    switch (node->tag) {
    case astPath:
    case astIdentifier:
    case astIndexExpr:
        return true;
    case astGroupExpr:
        return nodeIsLeftValue(node->groupExpr.expr);
    case astUnaryExpr: {
        Operator op = node->unaryExpr.op;
        if (op == opPtrof || op == opRefof)
            return nodeIsLeftValue(node->unaryExpr.operand);
        return false;
    }
    case astCastExpr:
    case astTypedExpr:
        return nodeIsLeftValue(node->castExpr.expr);
    case astMemberExpr: {
        AstNode *member = node->memberExpr.member;
        return nodeIs(member, IntegerLit) || nodeIsLeftValue(member);
    }
    default:
        return false;
    }
}

bool nodeIsCallExpr(const AstNode *node)
{
    if (node == NULL)
        return false;
    switch (node->tag) {
    case astCallExpr:
        return true;
    case astGroupExpr:
        return nodeIsCallExpr(node->groupExpr.expr);
    case astUnaryExpr: {
        Operator op = node->unaryExpr.op;
        if (op == opMove || op == opRefof || op == opPtrof)
            return nodeIsCallExpr(node->unaryExpr.operand);
        return false;
    }
    case astCastExpr:
    case astTypedExpr:
        return nodeIsCallExpr(node->castExpr.expr);
    default:
        return false;
    }
}

bool nodeIsNoop(const AstNode *node)
{
    if (nodeIs(node, ExprStmt))
        return nodeIsNoop(node->exprStmt.expr);
    return nodeIs(node, Noop);
}

bool nodeIsThisParam(const AstNode *node)
{
    return nodeIs(node, FuncParamDecl) && node->_namedNode.name == S_this;
}

bool nodeIsThisArg(const AstNode *node)
{
    if (nodeIs(node, Path) && node->next == NULL)
        node = node->path.elements;
    return (nodeIs(node, Identifier) || nodeIs(node, PathElem)) &&
           node->_namedNode.name == S_this;
}
