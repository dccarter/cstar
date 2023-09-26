//
// Created by Carter on 2023-08-27.
//
#include "../check.h"
#include "../codegen.h"

#include "lang/capture.h"
#include "lang/flag.h"
#include "lang/operations.h"
#include "lang/strings.h"
#include "lang/ttable.h"
#include "lang/visitor.h"

#include "core/alloc.h"

static inline u64 getCalleeContextFlags(const AstNode *node)
{
    if (nodeIs(node, Path) && node->path.elements->next) {
        return node->path.elements->flags & flgConst;
    }
    else if (nodeIs(node, MemberExpr)) {
        return node->memberExpr.target->flags & flgConst;
    }
    return flgNone;
}

static void checkFunctionCallEpilogue(AstVisitor *visitor,
                                      const Type *func,
                                      AstNode *node,
                                      u64 flags)
{
    TypingContext *ctx = getAstVisitorContext(visitor);
    AstNode *callee = node->callExpr.callee, *args = node->callExpr.args;
    const Type *callee_ = NULL;

    u64 argsCount = countAstNodes(args);
    const Type **argTypes = mallocOrDie(sizeof(Type *) * argsCount);
    AstNode *arg = args;
    for (u64 i = 0; arg; arg = arg->next, i++) {
        argTypes[i] = arg->type ?: checkType(visitor, arg);
        if (typeIs(argTypes[i], Error)) {
            node->type = ERROR_TYPE(ctx);
            continue;
        }

        if (hasFlag(argTypes[i], Closure)) {
            argTypes[i] = findStructMemberType(argTypes[i], S_CallOverload);
            if (argTypes[i] == NULL) {
                node->type = ERROR_TYPE(ctx);
                logError(ctx->L,
                         &arg->loc,
                         "call argument is marked as closure but it is not a "
                         "closure",
                         NULL);
            }
        }
    }

    if (typeIs(node->type, Error)) {
        free(argTypes);
        return;
    }

    callee_ = matchOverloadedFunction(
        ctx, func, argTypes, argsCount, &node->loc, flags);
    free(argTypes);

    if (callee_ == NULL || typeIs(callee_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    callee->type = callee_;
    node->type = callee_->func.retType;
    if (typeIs(node->type, Auto)) {
        logError(ctx->L,
                 &callee->loc,
                 "call of function '{t}' returning a deducted type before the "
                 "return type is deduced",
                 (FormatArg[]){{.t = callee_}});
        logNote(ctx->L,
                &callee_->func.decl->loc,
                "maybe add a return type to function '{s}' or declare it "
                "before call site",
                (FormatArg[]){{.s = callee_->name}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    u64 paramsCount = callee_->func.paramsCount;
    arg = node->callExpr.args;
    argsCount = countAstNodes(arg);

    if (paramsCount > argsCount) {
        // Add default parameters to function call
        AstNode *param = getNodeAtIndex(
            callee_->func.decl->funcDecl.signature->params, argsCount);
        csAssert0(param);

        if (node->callExpr.args == NULL) {
            node->callExpr.args = copyAstNode(ctx->pool, param->funcParam.def);
            arg = node->callExpr.args;
            param = param->next;
        }
        else {
            arg = getLastAstNode(node->callExpr.args);
        }

        for (; param; param = param->next) {
            arg->next = copyAstNode(ctx->pool, param->funcParam.def);
            arg = arg->next;
        }
    }
}

void generateCallExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);

    const Type *type = resolveType(node->callExpr.callee->type);
    const AstNode *parent =
        type->func.decl ? getParentScope(type->func.decl) : NULL;
    u32 index =
        nodeIs(type->func.decl, FuncDecl) ? type->func.decl->funcDecl.index : 0;

    if (nodeIs(parent, StructDecl) || nodeIs(parent, ClassDecl)) {
        writeTypename(ctx, parent->type);
        format(ctx->state, "__", NULL);
    }
    else if (hasFlag(type->func.decl, Generated) ||
             !hasFlag(node->callExpr.callee, Define))
        writeDeclNamespace(ctx, type->namespace, NULL);

    if (type->name)
        format(ctx->state, "{s}", (FormatArg[]){{.s = type->name}});
    else
        astConstVisit(visitor, node->callExpr.callee);

    if (index)
        format(ctx->state, "_{u32}", (FormatArg[]){{.u32 = index}});

    bool isMember = nodeIs(parent, StructDecl) || nodeIs(parent, ClassDecl);
    if (isMember) {
        const AstNode *callee = node->callExpr.callee;
        bool needsThis =
            (hasFlag(callee, Member) && !hasFlag(callee, ClosureStyle)) ||
            (nodeIs(callee, Path) && callee->path.elements->next == NULL);

        if (needsThis) {
            if (hasFlag(node->callExpr.callee, AddSuper))
                format(ctx->state, "(&(this->super)", NULL);
            else
                format(ctx->state, "(this", NULL);
        }
        else {
            format(ctx->state, "(", NULL);
            const AstNode *target, *elem;
            if (nodeIs(callee, Path)) {
                target = callee->path.elements, elem = callee->path.elements;
                while (true) {
                    if (target->next == NULL || target->next->next == NULL)
                        break;
                    target = target->next;
                }

                if (!typeIs(target->type, Pointer) &&
                    !typeIs(target->type, Class))
                    format(ctx->state, "&", NULL);

                for (;; elem = elem->next) {
                    astConstVisit(visitor, elem);
                    if (elem == target)
                        break;
                    if (typeIs(elem->type, Pointer))
                        format(ctx->state, "->", NULL);
                    else
                        format(ctx->state, ".", NULL);
                }
            }
            else {
                target = callee->memberExpr.target;
                if (!typeIs(target->type, Class) &&
                    !typeIs(target->type, Pointer)) {
                    format(ctx->state, "&", NULL);
                }
                astConstVisit(visitor, target);
            }
        }
    }
    else {
        format(ctx->state, "(", NULL);
    }
    {
        const AstNode *arg = node->callExpr.args;
        for (u64 i = 0; arg; arg = arg->next, i++) {
            const Type *param = type->func.params[i];
            if (isMember || i != 0)
                format(ctx->state, ", ", NULL);

            if (isSliceType(param) && !isSliceType(arg->type)) {
                generateArrayToSlice(visitor, param, arg);
            }
            else if (nodeIs(arg, ArrayExpr)) {
                format(ctx->state, "(", NULL);
                writeTypename(ctx, arg->type->array.elementType);
                format(ctx->state, "[])", NULL);
                astConstVisit(visitor, arg);
            }
            else {
                astConstVisit(visitor, arg);
            }
        }
    }

    format(ctx->state, ")", NULL);
}

void checkCallExpr(AstVisitor *visitor, AstNode *node)
{
    TypingContext *ctx = getAstVisitorContext(visitor);

    AstNode *currentCall = ctx->currentCall;
    ctx->currentCall = node;
    AstNode *callee = node->callExpr.callee;
    u64 flags = getCalleeContextFlags(callee);

    const Type *callee_ = checkType(visitor, callee);

    ctx->currentCall = currentCall;

    if (callee_ == NULL || typeIs(callee_, Error)) {
        node->type = ERROR_TYPE(ctx);
        return;
    }

    callee_ = flattenWrappedType(callee_, &flags);
    if (isClassOrStructType(callee_)) {
        AstNode *symbol = callee->path.elements->pathElement.resolvesTo;
        if (isClassOrStructAstNode(symbol)) {
            node->type = transformToConstructCallExpr(visitor, node);
            return;
        }

        flags &= ~flgConst;
        const Type *overload = findStructMemberType(callee_, S_CallOverload);
        if (overload)
            callee_ = overload;
    }

    if (!typeIs(callee_, Func)) {
        logError(ctx->L,
                 &callee->loc,
                 "type '{t}' is not a callable type",
                 (FormatArg[]){{.t = callee_}});
        node->type = ERROR_TYPE(ctx);
        return;
    }

    checkFunctionCallEpilogue(visitor, callee_, node, flags);
    if (typeIs(node->type, Error))
        return;

    callee_ = node->callExpr.callee->type;
    AstNode *arg = node->callExpr.args;
    for (u64 i = 0; arg; arg = arg->next, i++) {
        const Type *type = callee_->func.params[i], *expr = arg->type;
        if (!evalExplicitConstruction(visitor, type, arg)) {
            logError(ctx->L,
                     &arg->loc,
                     "incompatible argument types, expecting '{t}' but got "
                     "'{t}'",
                     (FormatArg[]){{.t = type}, {.t = type}});
            continue;
        }

        if (!hasFlag(type, Optional) || hasFlag(expr, Optional))
            continue;

        type = getOptionalTargetType(type);
        if (nodeIs(arg, NullLit)) {
            if (!transformOptionalNone(visitor, arg, type))
                node->type = ERROR_TYPE(ctx);
        }
        else {
            arg->type = type;
            if (!transformOptionalSome(
                    visitor, arg, copyAstNode(ctx->pool, arg)))
                node->type = ERROR_TYPE(ctx);
        }

        if (typeIs(node->type, Error))
            return;
    }
}
