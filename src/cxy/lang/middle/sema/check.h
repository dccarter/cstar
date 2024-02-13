//
// Created by Carter Mbotho on 2023-07-26.
//

#pragma once

#include <lang/frontend/ast.h>
#include <lang/frontend/ttable.h>
#include <lang/frontend/visitor.h>

#include <core/strpool.h>

#define ERROR_TYPE(CTX) makeErrorType((CTX)->types)

typedef struct {
    Log *L;
    MemPool *pool;
    StrPool *strings;
    TypeTable *types;
    AstVisitor *evaluator;
    struct {
        AstNode *program;
        AstNode *previous;
        AstNode *current;
    } root;

    struct {
        AstNode *self;
        AstNode *previous;
        AstNode *current;
    } block;

    union {
        struct {
            AstNode *currentCall;
            AstNode *currentStruct;
            AstNode *currentClass;
            bool shallow;
        } stack;

        struct {
            AstNode *currentCall;
            AstNode *currentStruct;
            AstNode *currentClass;
            AstNode *currentFunction;
            bool shallow;
        };
    };
} TypingContext;

void addTopLevelDeclaration(TypingContext *ctx, AstNode *node);
void addTopLevelDeclarationAsNext(TypingContext *ctx, AstNode *node);
void addBlockLevelDeclaration(TypingContext *ctx, AstNode *node);

const FileLoc *manyNodesLoc_(FileLoc *dst, AstNode *nodes);
#define manyNodesLoc(nodes) manyNodesLoc_(&(FileLoc){}, (nodes))

const FileLoc *lastNodeLoc_(FileLoc *dst, AstNode *nodes);
#define lastNodeLoc(nodes) lastNodeLoc_(&(FileLoc){}, (nodes))

AstNode *makeDefaultValue(MemPool *pool, const Type *type, FileLoc *loc);

bool transformToTruthyOperator(AstVisitor *visitor, AstNode *node);
bool transformToAwaitOperator(AstVisitor *visitor, AstNode *node);
bool transformToDerefOperator(AstVisitor *visitor, AstNode *node);
bool transformOptionalType(AstVisitor *visitor,
                           AstNode *node,
                           const Type *type);

AstNode *transformToUnionValue(TypingContext *ctx,
                               AstNode *right,
                               const Type *lhs,
                               const Type *rhs);

bool transformOptionalSome(AstVisitor *visitor, AstNode *node, AstNode *value);
bool transformOptionalNone(AstVisitor *visitor,
                           AstNode *node,
                           const Type *type);

bool isExplicitConstructableFrom(TypingContext *ctx,
                                 const Type *type,
                                 const Type *from);
bool evalExplicitConstruction(AstVisitor *visitor,
                              const Type *type,
                              AstNode *node);

bool checkTypeImplementsAllMembers(TypingContext *ctx, AstNode *node);

bool checkMemberFunctions(AstVisitor *visitor,
                          AstNode *node,
                          NamedTypeMember *members);

void implementClassOrStructBuiltins(AstVisitor *visitor, AstNode *node);

void checkBaseDecl(AstVisitor *visitor, AstNode *node);
void checkImplements(AstVisitor *visitor,
                     AstNode *node,
                     const Type **implements,
                     u64 count);

AstNode *makeDropReferenceCall(TypingContext *ctx,
                               AstNode *member,
                               const FileLoc *loc);

AstNode *makeAllocateCall(TypingContext *ctx, AstNode *node);

AstNode *makeGetReferenceCall(TypingContext *ctx,
                              AstNode *member,
                              const FileLoc *loc);

AstNode *makeSliceConstructor(TypingContext *ctx,
                              const Type *slice,
                              AstNode *init);

AstNode *transformArrayExprToSliceCall(TypingContext *ctx,
                                       const Type *slice,
                                       AstNode *expr);

void transformToMemberCallExpr(AstVisitor *visitor,
                               AstNode *node,
                               AstNode *target,
                               cstring member,
                               AstNode *args);

const Type *resolveGenericDecl(AstVisitor *visitor,
                               const AstNode *generic,
                               AstNode *node);

const Type *transformToConstructCallExpr(AstVisitor *visitor, AstNode *node);
AstNode *transformClosureArgument(AstVisitor *visitor, AstNode *node);

const Type *matchOverloadedFunctionPerfectMatch(TypingContext *ctx,
                                                const Type *callee,
                                                const Type **argTypes,
                                                u64 argsCount,
                                                const FileLoc *loc,
                                                u64 flags,
                                                bool perfectMatch);

static inline const Type *matchOverloadedFunction(TypingContext *ctx,
                                                  const Type *callee,
                                                  const Type **argTypes,
                                                  u64 argsCount,
                                                  const FileLoc *loc,
                                                  u64 flags)
{
    return matchOverloadedFunctionPerfectMatch(
        ctx, callee, argTypes, argsCount, loc, flags, false);
}

// const Type *makeCoroutineEntry(AstVisitor *visitor, AstNode *node);
// const Type *makeAsyncLaunchCall(AstVisitor *visitor,
//                                 const Type *callee,
//                                 AstNode *node);
AstNode *makeEnumGetName(TypingContext *ctx, AstNode *node);
const Type *checkMember(AstVisitor *visitor, const Type *parent, AstNode *node);
const Type *checkMaybeComptime(AstVisitor *visitor, AstNode *node);
const Type *checkTypeShallow(AstVisitor *visitor, AstNode *node, bool shallow);

static inline const Type *checkType(AstVisitor *visitor, AstNode *node)
{
    return checkTypeShallow(visitor, node, true);
}

const Type *checkFunctionSignature(AstVisitor *visitor, AstNode *node);
const Type *checkFunctionBody(AstVisitor *visitor, AstNode *node);

void buildModuleType(TypingContext *ctx, AstNode *node, bool isBuiltinModule);

void checkVarDecl(AstVisitor *visitor, AstNode *node);
void checkPath(AstVisitor *visitor, AstNode *node);
void checkFunctionParam(AstVisitor *visitor, AstNode *node);
void checkStructDecl(AstVisitor *visitor, AstNode *node);
void checkClassDecl(AstVisitor *visitor, AstNode *node);
void checkInterfaceDecl(AstVisitor *visitor, AstNode *node);
void checkFunctionDecl(AstVisitor *visitor, AstNode *node);
void checkEnumDecl(AstVisitor *visitor, AstNode *node);
void checkTypeDecl(AstVisitor *visitor, AstNode *node);
void checkUnionDecl(AstVisitor *visitor, AstNode *node);
void checkImportDecl(AstVisitor *visitor, attr(unused) AstNode *node);
void checkGenericDecl(AstVisitor *visitor, AstNode *node);

void checkBinaryExpr(AstVisitor *visitor, AstNode *node);
void checkUnaryExpr(AstVisitor *visitor, AstNode *node);
void checkAddressOfExpr(AstVisitor *visitor, AstNode *node);
void checkAssignExpr(AstVisitor *visitor, AstNode *node);
void checkIndexExpr(AstVisitor *visitor, AstNode *node);
void checkStructExpr(AstVisitor *visitor, AstNode *node);
void checkNewExpr(AstVisitor *visitor, AstNode *node);
void checkClosureExpr(AstVisitor *visitor, AstNode *node);
void checkArrayExpr(AstVisitor *visitor, AstNode *node);
void checkCallExpr(AstVisitor *visitor, AstNode *node);
void checkTupleExpr(AstVisitor *visitor, AstNode *node);
void checkMemberExpr(AstVisitor *visitor, AstNode *node);
void checkCastExpr(AstVisitor *visitor, AstNode *node);
void checkTypedExpr(AstVisitor *visitor, AstNode *node);

void checkForStmt(AstVisitor *visitor, AstNode *node);
void checkRangeExpr(AstVisitor *visitor, AstNode *node);
void checkCaseStmt(AstVisitor *visitor, AstNode *node);
void checkSwitchStmt(AstVisitor *visitor, AstNode *node);
void checkMatchCaseStmt(AstVisitor *visitor, AstNode *node);
void checkMatchStmt(AstVisitor *visitor, AstNode *node);
void checkIfStmt(AstVisitor *visitor, AstNode *node);

void checkTupleType(AstVisitor *visitor, AstNode *node);
void checkArrayType(AstVisitor *visitor, AstNode *node);
void checkFunctionType(AstVisitor *visitor, AstNode *node);
void checkBuiltinType(AstVisitor *visitor, AstNode *node);
void checkOptionalType(AstVisitor *visitor, AstNode *node);
void checkPointerType(AstVisitor *visitor, AstNode *node);
