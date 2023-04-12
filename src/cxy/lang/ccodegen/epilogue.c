//
// Created by Carter on 2023-03-29.
//

#include "ccodegen.h"
#include "lang/ttable.h"

#include "core/alloc.h"

#include <string.h>

#define CXY_EPILOGUE_SRC_FILE CXY_SOURCE_LANG_DIR "/ccodegen/epilogue.cxy.c"

static void programEpilogue(ConstAstVisitor *visitor, const AstNode *node)
{
    CCodegenContext *ctx = getConstAstVisitorContext(visitor);
    size_t bytes = 0;
    format(ctx->base.state,
           "\n"
           "/* --------------------- Generated EPILOGUE --------------*/\n"
           "\n",
           NULL);
    generateManyAsts(visitor, "\n\n", node->program.decls);

    format(ctx->base.state,
           "\n"
           "/* --------------------- epilogue.cxy.c --------------*/\n"
           "\n",
           NULL);

    append(ctx->base.state, readFile(CXY_EPILOGUE_SRC_FILE, &bytes), bytes);
    format(ctx->base.state, "\n", NULL);
}

static void generatePathElement(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (node->flags & flgCapture) {
        if (node->type->tag == typPrimitive || node->type->tag == typPointer)
            format(ctx->state,
                   "self->_{u64}",
                   (FormatArg[]){{.u64 = node->pathElement.index}});
        else
            format(ctx->state,
                   "(*self->_{u64})",
                   (FormatArg[]){{.u64 = node->pathElement.index}});
    }
    else
        format(ctx->state, "{s}", (FormatArg[]){{.s = node->pathElement.name}});
}

static void generatePath(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    generateManyAstsWithDelim(visitor, "", ".", "", node->path.elements);
}

static void generateIdentifier(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "{s}", (FormatArg[]){{.s = node->ident.value}});
}

static void generateFuncParam(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    generateTypeUsage((CCodegenContext *)ctx, node->type);
    format(ctx->state, " {s}", (FormatArg[]){{.s = node->funcParam.name}});
}

static void generateClosureForward(ConstAstVisitor *visitor,
                                   const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *params = node->funcDecl.params;

    generateTypeUsage((CCodegenContext *)ctx, node->type->func.retType);
    format(ctx->state,
           " {s}_fwd(void *self",
           (FormatArg[]){{.s = node->funcDecl.name}});
    if (params)
        format(ctx->state, ", ", NULL);
    generateManyAstsWithDelim(visitor, "", ", ", ") {{{>}\n", params->next);

    if (node->type->func.retType->tag != typVoid) {
        format(ctx->state, "return ", NULL);
    }
    format(ctx->state, "{s}((", (FormatArg[]){{.s = node->funcDecl.name}});
    generateTypeUsage((CCodegenContext *)ctx, node->funcDecl.params->type);
    format(ctx->state, ")self", NULL);

    for (const AstNode *param = params->next; param; param = param->next) {
        format(
            ctx->state, ", {s}", (FormatArg[]){{.s = param->funcParam.name}});
    }
    format(ctx->state, ");{<}\n}", (FormatArg[]){{.s = node->funcDecl.name}});
}

static void generateFunc(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    TypeTable *table = ((CCodegenContext *)ctx)->table;

    if (node->flags & flgClosure)
        format(ctx->state, "attr(always_inline)\n", NULL);

    if (node->flags & flgMain) {
        if (isIntegerType(table, node->type->func.retType)) {
            format(ctx->state,
                   "#define __CXY_MAIN_INVOKE(...) return "
                   "__cxy_main(__VA_ARGS__)\n\n",
                   NULL);
        }
        else {
            format(ctx->state,
                   "#define __CXY_MAIN_INVOKE(...) __cxy_main(__VA_ARGS__); "
                   "return EXIT_SUCCESS\n\n",
                   NULL);
        }
    }

    if (node->flags & flgNative)
        format(ctx->state, "extern ", NULL);

    generateTypeUsage((CCodegenContext *)ctx, node->type->func.retType);
    if (node->flags & flgMain)
        format(ctx->state, " __cxy_main", NULL);
    else
        format(ctx->state, " {s}", (FormatArg[]){{.s = node->funcDecl.name}});

    generateManyAstsWithDelim(visitor, "(", ", ", ")", node->funcDecl.params);

    if (node->flags & flgNative) {
        format(ctx->state, ";", NULL);
    }
    else {
        format(ctx->state, " ", NULL);
        if (node->funcDecl.body->tag == astBlockStmt) {
            astConstVisit(visitor, node->funcDecl.body);
        }
        else {
            format(ctx->state, "{{{>}\n", NULL);
            if (node->type->func.retType != makeVoidType(table)) {
                format(ctx->state, "return ", NULL);
            }
            astConstVisit(visitor, node->funcDecl.body);
            format(ctx->state, ";", NULL);
            format(ctx->state, "{<}\n}", NULL);
        }
    }

    if (node->flags & flgClosure) {
        format(ctx->state, "\n", NULL);
        generateClosureForward(visitor, node);
    }
}

static void generateTypeDecl(ConstAstVisitor *visitor, const AstNode *node)
{
    CCodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (!(node->flags & flgNative))
        generateTypeUsage(ctx, node->type);
}

static void generateVariable(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);

    if (node->flags & flgNative)
        format(ctx->state, "extern ", NULL);

    if ((node->flags & flgConst) && !(node->type->flags & flgConst))
        format(ctx->state, "const ", NULL);

    // if (node->varDecl.init == NULL || node->varDecl.)
    generateTypeUsage((CCodegenContext *)ctx, node->type);

    format(ctx->state, " ", NULL);
    astConstVisit(visitor, node->varDecl.names);

    if (node->varDecl.init) {
        format(ctx->state, " = ", NULL);
        astConstVisit(visitor, node->varDecl.init);
    }
    format(ctx->state, ";", NULL);
}

static void generateLiteral(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);

    switch (node->tag) {
    case astNullLit:
        format(ctx->state, "nullptr", NULL);
        break;
    case astBoolLit:
        format(
            ctx->state,
            "{s}",
            (FormatArg[]){{.s = node->boolLiteral.value ? "true" : "false"}});
        break;
    case astCharLit:
        format(ctx->state,
               "{u32}",
               (FormatArg[]){{.u32 = node->charLiteral.value}});
        break;
    case astIntegerLit:
        format(ctx->state,
               "{s}{u64}",
               (FormatArg[]){{.s = node->intLiteral.hasMinus ? "-" : ""},
                             {.u64 = node->intLiteral.value}});
        break;
    case astFloatLit:
        format(ctx->state,
               "{f64}",
               (FormatArg[]){{.f64 = node->floatLiteral.value}});
        break;
    case astStringLit:
        format(ctx->state,
               "\"{s}\"",
               (FormatArg[]){{.s = node->stringLiteral.value}});
        break;
    default:
        break;
    }
}

static void generateAddressOf(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "&", NULL);
    astConstVisit(visitor, node->unaryExpr.operand);
}

static void generateStatementExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    astConstVisit(visitor, node->stmtExpr.stmt);
}

static void generateBinaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->binaryExpr.lhs);
    format(ctx->state,
           " {s} ",
           (FormatArg[]){{.s = getBinaryOpString(node->binaryExpr.op)}});
    astConstVisit(visitor, node->binaryExpr.rhs);
}

static void generateNewExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const char *name = makeAnonymousVariable(((CCodegenContext *)ctx)->strPool,
                                             "__cxy_new_temp");
    const Type *type = node->newExpr.type->type;

    format(ctx->state, "({{ ", NULL);
    generateTypeUsage((CCodegenContext *)ctx, node->type);
    format(
        ctx->state, " {s} = __cxy_alloc(sizeof(", (FormatArg[]){{.s = name}});
    generateTypeUsage((CCodegenContext *)ctx, node->newExpr.type->type);
    format(ctx->state, "));", NULL);
    if (node->newExpr.init) {
        if (type->tag == typArray) {
            format(ctx->state, " memcpy(*{s}, &(", (FormatArg[]){{.s = name}});
            generateTypeUsage((CCodegenContext *)ctx, type);
            format(ctx->state, ")", NULL);
            astConstVisit(visitor, node->newExpr.init);
            format(ctx->state, ", sizeof(", NULL);
            generateTypeUsage((CCodegenContext *)ctx, node->newExpr.type->type);
            format(ctx->state, "))", NULL);
        }
        else {
            format(ctx->state, " *{s} = ", (FormatArg[]){{.s = name}});
            astConstVisit(visitor, node->newExpr.init);
        }
        format(ctx->state, ";", NULL);
    }
    format(ctx->state, " {s}; })", (FormatArg[]){{.s = name}});
}

static void generateUnaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (node->unaryExpr.isPrefix) {
        switch (node->unaryExpr.op) {
        case opDelete:
            format(ctx->state, "__cxy_free((void *)", NULL);
            astConstVisit(visitor, node->unaryExpr.operand);
            format(ctx->state, ")", NULL);
            break;

        default:
            format(ctx->state,
                   "{s}",
                   (FormatArg[]){{.s = getUnaryOpString(node->unaryExpr.op)}});
            astConstVisit(visitor, node->unaryExpr.operand);
        }
    }
    else {
        astConstVisit(visitor, node->unaryExpr.operand);
        format(ctx->state,
               "{s}",
               (FormatArg[]){{.s = getUnaryOpString(node->unaryExpr.op)}});
    }
}

static void generateAssignExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->assignExpr.lhs);
    format(ctx->state,
           " {s} ",
           (FormatArg[]){{.s = getAssignOpString(node->assignExpr.op)}});
    astConstVisit(visitor, node->assignExpr.rhs);
}

static void generateTupleExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *arg = node->tupleExpr.args;

    format(ctx->state, "(", NULL);
    generateTypeUsage((CCodegenContext *)ctx, node->type);
    format(ctx->state, ")", NULL);

    format(ctx->state, "{{", NULL);
    for (u64 i = 0; arg; arg = arg->next, i++) {
        if (i != 0)
            format(ctx->state, ", ", NULL);
        format(ctx->state, "._{u64} = ", (FormatArg[]){{.u64 = i}});
        astConstVisit(visitor, arg);
    }

    format(ctx->state, "}", NULL);
}

static void generateArrayExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    generateManyAstsWithDelim(
        visitor, "{{", ", ", "}", node->arrayExpr.elements);
}

static void generateMemberExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *target = node->memberExpr.target,
                  *member = node->memberExpr.member;

    astConstVisit(visitor, target);
    if (target->type->tag == typPointer)
        format(ctx->state, "->", NULL);
    else
        format(ctx->state, ".", NULL);
    if (member->tag == astIntegerLit) {
        format(ctx->state,
               "_{u64}",
               (FormatArg[]){{.u64 = member->intLiteral.value}});
    }
    else {
        format(ctx->state, "{s}", (FormatArg[]){{.s = member->ident.value}});
    }
}

static void generateClosureCapture(CodegenContext *ctx,
                                   const Type *type,
                                   cstring name,
                                   u64 index)
{
    CCodegenContext *cctx = (CCodegenContext *)ctx;
    generateTypeUsage(cctx, stripPointer(cctx->table, type->func.params[0]));

    format(ctx->state,
           " {s}{u64} = {{",
           (FormatArg[]){{.s = name}, {.u64 = index}});
    for (u64 i = 0; i < type->func.capturedNamesCount; i++) {
        const Type *captureType =
            stripPointer(cctx->table, type->func.params[0]);
        if (i != 0)
            format(ctx->state, ", ", NULL);
        if (captureType->tuple.members[i]->flags & flgCapturePointer)
            format(
                ctx->state,
                "._{u64} = &{s}",
                (FormatArg[]){{.u64 = i}, {.s = type->func.captureNames[i]}});
        else
            format(
                ctx->state,
                "._{u64} = {s}",
                (FormatArg[]){{.u64 = i}, {.s = type->func.captureNames[i]}});
    }
    format(ctx->state, "}; ", NULL);
}

static void generateCallExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    CCodegenContext *cctx = (CCodegenContext *)ctx;

    const Type *type = node->callExpr.callee->type;

    const char *name =
        (type->flags & (flgClosureStyle | flgClosure))
            ? makeAnonymousVariable(cctx->strPool, "__closure_capture")
            : "";

    format(ctx->state, "({{ ", NULL);
    if (type->flags & flgClosureStyle) {
        const AstNode *arg = node->callExpr.args;
        for (u64 i = 1; arg; arg = arg->next, i++) {
            if (arg->type->flags & flgClosure) {
                generateClosureCapture(ctx, arg->type, name, i);
            }
        }
    }
    else if (type->flags & flgClosure) {
        generateClosureCapture(ctx, type, name, 0);
    }

    astConstVisit(visitor, node->callExpr.callee);
    if (type->flags & flgClosure) {
        format(ctx->state, "(&{s}0", (FormatArg[]){{.s = name}});
        if (type->func.paramsCount > 1)
            format(ctx->state, ", ", NULL);
    }
    else {
        format(ctx->state, "(", NULL);
    }
    {
        const AstNode *arg = node->callExpr.args;
        for (u64 i = 0; arg; arg = arg->next, i++) {
            const Type *param = type->func.params[i];
            if (i != 0)
                format(ctx->state, ", ", NULL);

            if (arg->type->flags & flgClosure) {
                format(ctx->state, "(", NULL);
                generateTypeUsage(cctx, param);
                format(ctx->state,
                       "){{._0 = &{s}{u64}, ._1 = ",
                       (FormatArg[]){{.s = name}, {.u64 = i + 1}});
                astConstVisit(visitor, arg->type->func.decl);
                format(ctx->state, "_fwd}", NULL);
            }
            else {
                astConstVisit(visitor, arg);
            }
        }
    }
    format(ctx->state, "); })", NULL);
}

static void generateGroupExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "(", NULL);
    astConstVisit(visitor, node->groupExpr.expr);
    format(ctx->state, ")", NULL);
}

static void generateTypedExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "(", NULL);
    generateTypeUsage((CCodegenContext *)ctx, node->type);
    format(ctx->state, ")", NULL);
    astConstVisit(visitor, node->typedExpr.expr);
}

static void generateCastExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "(", NULL);
    generateTypeUsage((CCodegenContext *)ctx, node->castExpr.to->type);
    format(ctx->state, ")", NULL);
    astConstVisit(visitor, node->castExpr.expr);
}

static void generateIndexExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const Type *target = node->indexExpr.target->type;
    const Type *stripped = target->pointer.pointed;
    if (target->tag == typPointer && stripped->tag == typArray) {
        format(ctx->state, "(*", NULL);
        astConstVisit(visitor, node->indexExpr.target);
        format(ctx->state, ")", NULL);
    }
    else {
        astConstVisit(visitor, node->indexExpr.target);
    }
    format(ctx->state, "[", NULL);
    astConstVisit(visitor, node->indexExpr.index);
    format(ctx->state, "]", NULL);
}

static void generateTernaryExpr(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->ternaryExpr.cond);
    format(ctx->state, "? ", NULL);
    astConstVisit(visitor, node->ternaryExpr.body);
    format(ctx->state, ": ", NULL);
    astConstVisit(visitor, node->ternaryExpr.otherwise);
}

static void generateBlock(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *ret = NULL;
    const AstNode *epilogue = node->blockStmt.epilogue.first;

    format(ctx->state, "{{{>}\n", NULL);
    for (const AstNode *stmt = node->blockStmt.stmts; stmt; stmt = stmt->next) {
        if (epilogue && stmt->tag == astReturnStmt) {
            ret = stmt;
            continue;
        }
        astConstVisit(visitor, stmt);
        if (stmt->tag == astCallExpr)
            format(ctx->state, ";", NULL);
        if (epilogue || stmt->next)
            format(ctx->state, "\n", NULL);
    }

    for (; epilogue; epilogue = epilogue->next) {
        astConstVisit(visitor, epilogue);
        if ((epilogue->flags & flgDeferred) && epilogue->tag != astBlockStmt)
            format(ctx->state, ";", NULL);

        if (ret || epilogue->next)
            format(ctx->state, "\n", NULL);
    }

    if (ret)
        astConstVisit(visitor, ret);
    format(ctx->state, "{<}\n}", NULL);
}

static void generateExpressionStmt(ConstAstVisitor *visitor,
                                   const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    astConstVisit(visitor, node->exprStmt.expr);
    format(ctx->state, ";", NULL);
}

static void generateReturn(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    format(ctx->state, "return", NULL);
    if (node->returnStmt.expr) {
        format(ctx->state, " ", NULL);
        astConstVisit(visitor, node->returnStmt.expr);
    }
    format(ctx->state, ";", NULL);
}

static void generateBreakContinue(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    if (node->tag == astBreakStmt)
        format(ctx->state, "break;", NULL);
    else
        format(ctx->state, "continue;", NULL);
}

static void generateIfStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *cond = node->ifStmt.cond;
    if (cond->tag == astVarDecl) {
        format(ctx->state, "{{{>}\n", NULL);
        astConstVisit(visitor, cond);
        format(ctx->state,
               "\nif ({s}) ",
               (FormatArg[]){{.s = cond->varDecl.names->ident.value}});
    }
    else {
        format(ctx->state, "if (", NULL);
        astConstVisit(visitor, cond);
        format(ctx->state, ") ", NULL);
    }
    astConstVisit(visitor, node->ifStmt.body);
    if (node->ifStmt.otherwise) {
        format(ctx->state, " else ", NULL);
        astConstVisit(visitor, node->ifStmt.otherwise);
    }

    if (cond->tag == astVarDecl) {
        format(ctx->state, "{<}\n}", NULL);
    }
}

static void generateWhileStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    const AstNode *cond = node->whileStmt.cond;
    if (cond->tag == astVarDecl) {
        format(ctx->state, "{{{>}\n", NULL);
        astConstVisit(visitor, cond);
        format(ctx->state,
               "\nwhile ({s}) ",
               (FormatArg[]){{.s = cond->varDecl.names->ident.value}});
    }
    else {
        format(ctx->state, "while (", NULL);
        astConstVisit(visitor, cond);
        format(ctx->state, ") ", NULL);
    }
    astConstVisit(visitor, node->whileStmt.body);

    if (cond->tag == astVarDecl) {
        format(ctx->state, "{<}\n}", NULL);
    }
}

static void generateForStmt(ConstAstVisitor *visitor, const AstNode *node)
{
    CodegenContext *ctx = getConstAstVisitorContext(visitor);
    CCodegenContext *cctx = (CCodegenContext *)ctx;
    const AstNode *var = node->forStmt.var;
    const AstNode *range = node->forStmt.range;

    if (range->tag == astRangeExpr) {
        format(ctx->state, "for (", NULL);
        generateTypeUsage(cctx, var->type);
        format(ctx->state, " ", NULL);
        astConstVisit(visitor, var->varDecl.names);
        format(ctx->state, " = ", NULL);
        astConstVisit(visitor, range->rangeExpr.start);
        format(ctx->state, "; ", NULL);
        astConstVisit(visitor, var->varDecl.names);
        format(ctx->state, " < ", NULL);
        astConstVisit(visitor, range->rangeExpr.end);
        format(ctx->state, "; ", NULL);
        astConstVisit(visitor, var->varDecl.names);
        if (range->rangeExpr.step) {
            format(ctx->state, " += ", NULL);
            astConstVisit(visitor, range->rangeExpr.step);
        }
        else
            format(ctx->state, "++", NULL);
    }
    else if (range->type->tag == typArray) {
        cstring name = makeAnonymousVariable(cctx->strPool, "cyx_for");
        // create an array
        format(ctx->state, "{{{>}\n", NULL);
        if (range->tag == astArrayExpr)
            generateTypeUsage(cctx, range->type);
        else
            generateTypeUsage(
                cctx,
                &(const Type){
                    .tag = typPointer,
                    .flags = range->type->flags | node->forStmt.range->flags,
                    .pointer.pointed = range->type->array.elementType});

        format(ctx->state, " __arr_{s} = ", (FormatArg[]){{.s = name}});
        astConstVisit(visitor, range);
        format(ctx->state, ";\n", NULL);

        // create index variable
        format(ctx->state, "u64 __i_{s} = 0;\n", (FormatArg[]){{.s = name}});

        // Create actual loop variable
        generateTypeUsage(cctx, range->type->array.elementType);
        format(ctx->state, " ", NULL);
        astConstVisit(visitor, var->varDecl.names);
        format(ctx->state, " = __arr_{s}[0];\n", (FormatArg[]){{.s = name}});

        format(ctx->state,
               "for (; __i_{s} < {u64}; __i_{s}++, ",
               (FormatArg[]){
                   {.s = name}, {.u64 = range->type->array.size}, {.s = name}});
        astConstVisit(visitor, var->varDecl.names);
        format(ctx->state,
               " = __arr_{s}[__i_{s}]",
               (FormatArg[]){{.s = name}, {.s = name}});
    }
    else {
        unreachable("currently not supported");
    }

    format(ctx->state, ") ", NULL);
    astConstVisit(visitor, node->forStmt.body);

    if (range->type->tag == typArray) {
        format(ctx->state, "{<}\n}", NULL);
    }
}

void cCodegenEpilogue(CCodegenContext *context, const AstNode *prog)
{
    // clang-format off
    ConstAstVisitor visitor = makeConstAstVisitor(context,
    {
        [astProgram] = programEpilogue,
        [astPathElem] = generatePathElement,
        [astPath] = generatePath,
        [astIdentifier] = generateIdentifier,
        [astNullLit] = generateLiteral,
        [astBoolLit] = generateLiteral,
        [astCharLit] = generateLiteral,
        [astIntegerLit] = generateLiteral,
        [astFloatLit] = generateLiteral,
        [astStringLit] = generateLiteral,
        [astAddressOf] = generateAddressOf,
        [astStmtExpr] = generateStatementExpr,
        [astBinaryExpr] = generateBinaryExpr,
        [astUnaryExpr] = generateUnaryExpr,
        [astAssignExpr] = generateAssignExpr,
        [astTupleExpr] = generateTupleExpr,
        [astArrayExpr] = generateArrayExpr,
        [astMemberExpr] = generateMemberExpr,
        [astCallExpr] = generateCallExpr,
        [astStringExpr] = cCodegenStringExpr,
        [astGroupExpr] = generateGroupExpr,
        [astTypedExpr] = generateTypedExpr,
        [astCastExpr] = generateCastExpr,
        [astIndexExpr] = generateIndexExpr,
        [astTernaryExpr] = generateTernaryExpr,
        [astNewExpr] = generateNewExpr,
        [astBlockStmt] = generateBlock,
        [astExprStmt] = generateExpressionStmt,
        [astReturnStmt] = generateReturn,
        [astBreakStmt] = generateBreakContinue,
        [astContinueStmt] = generateBreakContinue,
        [astIfStmt] = generateIfStmt,
        [astWhileStmt] = generateWhileStmt,
        [astForStmt] = generateForStmt,
        [astFuncParam] = generateFuncParam,
        [astFuncDecl] = generateFunc,
        [astVarDecl] = generateVariable,
        [astTypeDecl] = generateTypeDecl
    },

    .fallback = generateCCodeFallback);

    astConstVisit(&visitor, prog);
}
