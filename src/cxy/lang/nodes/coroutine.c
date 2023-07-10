//
// Created by Carter Mbotho on 2023-06-21.
//

#include "lang/codegen.h"

void generateCoroutineFunctions(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getAstVisitorContext(visitor);
    const AstNode *param = node->funcDecl.params;
    cstring name = node->funcDecl.name;

    format(ctx->state, "typedef struct {{{>}\n", NULL);
    for (; param; param = param->next) {
        generateTypeUsage(ctx, param->type);
        format(
            ctx->state, " {s};", (FormatArg[]){{.s = param->funcParam.name}});
        if (param->next)
            format(ctx->state, "\n", NULL);
    }

    format(ctx->state, "{<}\n} ", NULL);
    writeNamespace(ctx, NULL);
    format(ctx->state, "{s}__goCoroutineArgs;\n", (FormatArg[]){{.s = name}});

    format(ctx->state, "\nvoid ", NULL);
    writeNamespace(ctx, NULL);
    format(ctx->state,
           "{s}__goCoroutine(void *ptr) {{{>}\n",
           (FormatArg[]){{.s = name}});
    writeNamespace(ctx, NULL);
    format(ctx->state,
           "{s}__goCoroutineArgs *args = ptr; \n",
           (FormatArg[]){{.s = node->funcDecl.name}});
    writeNamespace(ctx, NULL);
    format(ctx->state, "{s}(", (FormatArg[]){{.s = name}});

    param = node->funcDecl.params;
    for (; param; param = param->next) {
        format(ctx->state,
               "args->{s}",
               (FormatArg[]){{.s = param->funcParam.name}});
        if (param->next)
            format(ctx->state, ", ", NULL);
    }
    format(ctx->state, ");{<}\n}\n", NULL);
}

void generateCoroutineLaunch(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getAstVisitorContext(visitor);
    const Type *func = node->callExpr.callee->type;
    cstring name = func->name;
    const AstNode *args = node->callExpr.args, *arg = args,
                  *param = func->func.decl->funcDecl.params;

    format(ctx->state, "{{{>}\n", NULL);
    writeNamespace(ctx, func->namespace);
    format(ctx->state,
           "{s}__goCoroutineArgs args = {{",
           (FormatArg[]){{.s = name}});

    for (; arg; arg = arg->next, param = param->next) {
        format(
            ctx->state, ".{s} = ", (FormatArg[]){{.s = param->funcParam.name}});
        astConstVisit(visitor, arg);

        if (param->next)
            format(ctx->state, ", ", NULL);
    }
    format(ctx->state, "};\n", NULL);

    format(ctx->state, "launchCoroutine(", NULL);
    writeNamespace(ctx, func->namespace);
    format(ctx->state,
           "{s}__goCoroutine, &args, \"{s}Coroutine@({s}:{u})\", "
           "CXY_DEFAULT_CORO_STACK_SIZE);",
           (FormatArg[]){{.s = name},
                         {.s = name},
                         {.s = node->loc.fileName},
                         {.u = node->loc.begin.row}});

    format(ctx->state, "{<}\n}\n", NULL);
}
