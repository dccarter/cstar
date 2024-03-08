//
// Created by Carter Mbotho on 2024-01-25.
//

#include "check.h"

#include "lang/frontend/flag.h"

static AstNode *makeDefaultPrimitiveValue(MemPool *pool,
                                          const Type *type,
                                          FileLoc *loc)
{
    switch (type->primitive.id) {
    case prtBool:
        return makeBoolLiteral(pool, loc, false, NULL, type);
#define f(ID, ...)                                                             \
    case prt##ID:                                                              \
        return makeIntegerLiteral(pool, loc, 0, NULL, type);
        INTEGER_TYPE_LIST(f)
#undef f
    case prtCChar:
    case prtChar:
        return makeCharLiteral(pool, loc, '\0', NULL, type);
    case prtF32:
    case prtF64:
        return makeFloatLiteral(pool, loc, 0.0, NULL, type);
    default:
        unreachable("not a primitive type");
    }
}

static inline AstNode *makeDefaultEnumValue(MemPool *pool,
                                            const Type *type,
                                            FileLoc *loc)
{
    return makeIntegerLiteral(
        pool, loc, type->tEnum.options->value, NULL, type->tEnum.base);
}

static inline AstNode *makeDefaultUnionValue(MemPool *pool,
                                             const Type *type,
                                             FileLoc *loc)
{
    return makeUnionValueExpr(
        pool,
        loc,
        flgNone,
        makeDefaultValue(pool, type->tUnion.members[0].type, loc),
        0,
        NULL,
        type);
}

static AstNode *makeDefaultTupleValue(MemPool *pool,
                                      const Type *type,
                                      FileLoc *loc)
{
    AstNodeList nodes = {NULL};
    for (u64 i = 0; i < type->tuple.count; i++) {
        insertAstNode(&nodes,
                      makeDefaultValue(pool, type->tuple.members[i], loc));
    }
    return makeTupleExpr(pool, loc, flgNone, nodes.first, NULL, type);
}

static AstNode *makeDefaultStructValue(MemPool *pool,
                                       const Type *type,
                                       FileLoc *loc)
{
    AstNodeList nodes = {NULL};
    AstNode *decl = type->tStruct.decl, *member = decl->structDecl.members;
    for (; member; member = member->next) {
        if (!nodeIs(member, FieldDecl))
            continue;

        if (member->structField.value)
            insertAstNode(&nodes,
                          makeFieldExpr(pool,
                                        loc,
                                        member->structField.name,
                                        member->flags & flgConst,
                                        shallowCloneAstNode(
                                            pool, member->structField.value),
                                        NULL));
        else
            insertAstNode(
                &nodes,
                makeFieldExpr(pool,
                              loc,
                              member->structField.name,
                              member->flags & flgConst,
                              makeDefaultValue(pool, member->type, loc),
                              NULL));
    }

    return makeStructExpr(
        pool,
        loc,
        flgNone,
        makeResolvedIdentifier(pool, loc, type->name, 0, decl, NULL, type),
        nodes.first,
        NULL,
        type);
}

AstNode *makeDefaultValue(MemPool *pool, const Type *type, FileLoc *loc)
{
    switch (type->tag) {
    case typNull:
    case typFunc:
    case typPointer:
    case typString:
    case typClass:
    case typOpaque:
        return makeNullLiteral(pool, loc, NULL, type);
    case typPrimitive:
        return makeDefaultPrimitiveValue(pool, type, loc);
    case typEnum:
        return makeDefaultEnumValue(pool, type, loc);
    case typTuple:
        return makeDefaultTupleValue(pool, type, loc);
    case typStruct:
        return makeDefaultStructValue(pool, type, loc);
    case typWrapped:
        return makeDefaultValue(pool, unwrapType(type, NULL), loc);
    case typUnion:
        return makeDefaultUnionValue(pool, type, loc);
    case typAlias:
        return makeDefaultValue(pool, resolveType(type), loc);
    default:
        unreachable("not supported");
    }
}