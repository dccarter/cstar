//
// Created by Carter Mbotho on 2023-07-26.
//

#pragma once

#include <lang/ast.h>
#include <lang/visitor.h>

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
        } stack;

        struct {
            AstNode *currentCall;
            AstNode *currentStruct;
            AstNode *currentClass;
        };
    };
} TypingContext;

void addTopLevelDeclaration(TypingContext *ctx, AstNode *node);
void addBlockLevelDeclaration(TypingContext *ctx, AstNode *node);

const FileLoc *manyNodesLoc_(FileLoc *dst, AstNode *nodes);
#define manyNodesLoc(nodes) manyNodesLoc_(&(FileLoc){}, (nodes))

const FileLoc *lastNodeLoc_(FileLoc *dst, AstNode *nodes);
#define lastNodeLoc(nodes) lastNodeLoc_(&(FileLoc){}, (nodes))

bool transformToTruthyOperator(AstVisitor *visitor, AstNode *node);
bool transformToDerefOperator(AstVisitor *visitor, AstNode *node);
bool transformOptionalType(AstVisitor *visitor,
                           AstNode *node,
                           const Type *type);
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

void checkImplements(AstVisitor *visitor,
                     AstNode *node,
                     const Type **implements,
                     u64 count);

AstNode *makeDropReferenceCall(TypingContext *ctx,
                               AstNode *member,
                               const FileLoc *loc);

AstNode *makeNewClassCall(TypingContext *ctx, AstNode *node);

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

const Type *matchOverloadedFunction(TypingContext *ctx,
                                    const Type *callee,
                                    const Type **argTypes,
                                    u64 argsCount,
                                    const FileLoc *loc,
                                    u64 flags);

const Type *checkPathElement(AstVisitor *visitor,
                             const Type *parent,
                             AstNode *node);

const Type *checkMaybeComptime(AstVisitor *visitor, AstNode *node);
const Type *checkType(AstVisitor *visitor, AstNode *node);
const Type *checkFunctionSignature(AstVisitor *visitor, AstNode *node);
const Type *checkFunctionBody(AstVisitor *visitor, AstNode *node);

void checkLiteral(AstVisitor *visitor, AstNode *node);
void checkVarDecl(AstVisitor *visitor, AstNode *node);
void checkPath(AstVisitor *visitor, AstNode *node);
void checkFunctionParam(AstVisitor *visitor, AstNode *node);
void checkField(AstVisitor *visitor, AstNode *node);
void checkStructDecl(AstVisitor *visitor, AstNode *node);
void checkClassDecl(AstVisitor *visitor, AstNode *node);
void checkFunctionDecl(AstVisitor *visitor, AstNode *node);
void checkEnumDecl(AstVisitor *visitor, AstNode *node);
void checkTypeDecl(AstVisitor *visitor, AstNode *node);
void checkUnionDecl(AstVisitor *visitor, AstNode *node);

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
void checkTernaryExpr(AstVisitor *visitor, AstNode *node);

void checkForStmt(AstVisitor *visitor, AstNode *node);
void checkCaseStmt(AstVisitor *visitor, AstNode *node);
void checkSwitchStmt(AstVisitor *visitor, AstNode *node);
void checkIfStmt(AstVisitor *visitor, AstNode *node);

void checkTupleType(AstVisitor *visitor, AstNode *node);
void checkArrayType(AstVisitor *visitor, AstNode *node);
void checkFunctionType(AstVisitor *visitor, AstNode *node);
void checkBuiltinType(AstVisitor *visitor, AstNode *node);
void checkOptionalType(AstVisitor *visitor, AstNode *node);
void checkPointerType(AstVisitor *visitor, AstNode *node);