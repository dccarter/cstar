// Credits https://github.com/madmann91/fu/blob/master/src/fu/lang/ast.h

#pragma once

#include "core/format.h"
#include "core/log.h"
#include "core/mempool.h"
#include "lang/types.h"

// clang-format off

#define AST_ARITH_EXPR_LIST(f)                     \
    f(Add, 3, Plus, "+", "add")                    \
    f(Sub, 3, Minus,"-", "sub")                   \
    f(Mul, 2, Mult, "*", "mul")                    \
    f(Div, 2, Div,  "/", "div")                   \
    f(Mod, 2, Mod,  "%", "rem")

#define AST_BIT_EXPR_LIST(f)                        \
    f(BAnd, 7, BAnd, "&", "and")                    \
    f(BOr,  9, BOr,  "|", "or")                     \
    f(BXor, 8, BXor, "^", "xor")

#define AST_SHIFT_EXPR_LIST(f)         \
    f(Shl, 4, Shl, "<<", "lshift")     \
    f(Shr, 4, Shr, ">>", "rshift")

#define AST_CMP_EXPR_LIST(f)                   \
    f(Eq,  6, Equal,        "==", "eq")        \
    f(Ne,  6, NotEqual,     "!=", "neq")       \
    f(Gt,  5, Greater,      ">", "gt")         \
    f(Lt,  5, Less,         "<", "lt")         \
    f(Geq, 5, GreaterEqual, ">=", "geq")       \
    f(Leq, 5, LessEqual,    "<=", "leq")

#define AST_LOGIC_EXPR_LIST(f)                \
    f(LAnd, 10, LAnd, "&&", NULL)             \
    f(LOr,  11, LOr,  "||", NULL)

#define AST_BINARY_EXPR_LIST(f)           \
    AST_ARITH_EXPR_LIST(f)                \
    AST_BIT_EXPR_LIST(f)                  \
    AST_SHIFT_EXPR_LIST(f)                \
    AST_CMP_EXPR_LIST(f)                  \
    AST_LOGIC_EXPR_LIST(f)

#define AST_ASSIGN_EXPR_LIST(f)          \
    AST_ARITH_EXPR_LIST(f)               \
    AST_BIT_EXPR_LIST(f)                 \
    AST_SHIFT_EXPR_LIST(f)

#define AST_PREFIX_EXPR_LIST(f)         \
    f(PreDec, MinusMinus, "--")         \
    f(PreInc, PlusPlus, "++")           \
    f(AddrOf, BAnd, "&")                \
    f(Move,   LAnd, "&&")               \
    f(Deref,  Mult, "*")                \
    f(Minus,  Minus, "-")               \
    f(Plus,   Plus, "+")                \
    f(Not,    Bang, "!")                \
    f(New,    New,  "new")              \
    f(Await,  Await, "await")           \
    f(Delete, Delete,"delete")          \

#define AST_POSTFIX_EXPR_LIST(f)        \
    f(PostDec, Minus, "--")             \
    f(PostInc, Plus,  "++")

#define AST_UNARY_EXPR_LIST(f)                                                 \
    AST_PREFIX_EXPR_LIST(f)                                                    \
    AST_POSTFIX_EXPR_LIST(f)

typedef enum {
#define f(name, ...) op##name,
    AST_BINARY_EXPR_LIST(f)
    AST_UNARY_EXPR_LIST(f)
#undef f
#define f(name, ...) op##name##Equal,
    AST_ASSIGN_EXPR_LIST(f)
#undef f
} Operator;

// clang-format on

typedef enum {
    astError,
    astImplicitCast,
    astAttr,
    astPathElem,
    astPath,
    /* Types */
    astTupleType,
    astArrayType,
    astPointerType,
    astFuncType,
    astPrimitiveType,
    /* Literals */
    astBoolLit,
    astCharLit,
    astIntegerLit,
    astFloatLit,
    astStringLit,
    /* Declarations */
    astFuncParam,
    astFuncDecl,
    astConstDecl,
    astVarDecl,
    astTypeDecl,
    astUnionDecl,
    astEnumOption,
    astEnumDecl,
    astStructField,
    astStructDecl,
    /* Expressions */
    astUnaryExpr,
    astBinaryExpr,
    astAssignExpr,
    astTernaryExpr,
    astStmtExpr,
    astStringExpr,
    astTypedExpr,
    astCallExpr,
    astClosureExpr,
    astArrayExpr,
    astIndexExpr,
    astTupleExpr,
    astFieldExpr,
    astStructExpr,
    astMemberExpr,
    /* Statements */
    astExprStmt,
    astBreakStmt,
    astContinueStmt,
    astReturnStmt,
    astBlockStmt,
    astIfStmt,
    astForStmt,
    astWhileStmt,
    astSwitchStmt,
    astCaseStmt,
    COUNT
} AstTag;

typedef struct AstNode {
    AstTag tag;
    FileLoc loc;
    const Type *type;
    struct AstNode *parentScope;
    struct AstNode *next;
    struct AstNode *attrs;
    union {
        struct {
            struct AstNode *expr;
        } implicitCast;

        struct {
            bool value;
        } boolLiteral;

        struct {
            u64 value;
            bool hasMinus;
        } intLiteral;

        struct {
            f64 value;
        } floatLiteral;

        struct {
            u32 value;
        } charLiteral;

        struct {
            const char *value;
        } stringLiteral;

        struct {
            const char *name;
            struct AstNode *values;
        } attr;

        struct {
            struct AstNode *args;
        } tupleType, tupleExpr;

        struct {
            struct AstNode *elementType;
            struct AstNode *size;
        } arrayType;

        struct {
            struct AstNode *genericParams;
            struct AstNode *paramTypes;
            struct AstNode *returnType;
        } funcType;

        struct {
            PrtId id;
        } primitiveType;

        struct {
            struct AstNode *pointed;
            bool isConst;
        } pointerType;

        struct {
            struct AstNode *elements;
        } arrayExpr;

        struct {
            struct AstNode *target;
            struct AstNode *member;
        } memberExpr;

        struct {
            struct AstNode *target;
            struct AstNode *index;
        } indexExpr;

        struct {
            const char *name;
            struct AstNode *constraints;
        } genericParam;

        struct {
            const char *name;
            struct AstNode *genericArgs;
            u64 index;
            bool isType;
        } pathElement;

        struct {
            struct AstNode *elements;
            struct AstNode *declSite;
        } path;

        struct {
            const char *name;
            bool isPublic : 1;
            bool isAsync : 1;
            struct AstNode *genericParams;
            struct AstNode *params;
            struct AstNode *returnType;
            struct AstNode *body;
        } funcDecl;

        struct {
            bool isVariadic;
            const char *name;
            struct AstNode *type;
        } funcParam;

        struct {
            bool isPublic;
            struct AstNode *name;
            struct AstNode *type;
            struct AstNode *init;
        } constDecl, varDecl;

        struct {
            bool isPublic;
            bool isOpaque;
            const char *name;
            struct AstNode *genericParams;
            struct AstNode *aliased;
        } typeDecl;

        struct {
            bool isPublic;
            const char *name;
            struct AstNode *genericParams;
            struct AstNode *members;
        } unionDecl;

        struct {
            const char *name;
            struct AstNode *value;
        } enumOption;

        struct {
            const char *name;
            bool isPublic : 1;
            bool isOpaque : 1;
            struct AstNode *base;
            struct AstNode *options;
        } enumDecl;

        struct {
            const char *name;
            u64 index;
            struct AstNode *type;
            struct AstNode *value;
        } structField;

        struct {
            const char *name;
            bool isPublic : 1;
            bool isOpaque : 1;
            bool isTupleLike : 1;
            struct AstNode *base;
            struct AstNode *genericParams;
            struct AstNode *fields;
        } structDecl;

        struct {
            Operator op;
            struct AstNode *lhs;
            struct AstNode *rhs;
        } binaryExpr, assignExpr;

        struct {
            Operator op;
            struct AstNode *operand;
        } unaryExpr;

        struct {
            struct AstNode *cond;
            struct AstNode *ifTrue;
            struct AstNode *ifFalse;
        } ternaryExpr, ifStmt;

        struct {
            struct AstNode *stmt;
        } stmtExpr;

        struct {
            struct AstNode *parts;
        } stringExpr;

        struct {
            struct AstNode *expr;
            struct AstNode *type;
        } typedExpr;

        struct {
            struct AstNode *callee;
            struct AstNode *args;
        } callExpr;

        struct {
            bool isAsync;
            struct AstNode **capture;
            struct AstNode *params;
            struct AstNode *ret;
            struct AstNode *body;
        } closureExpr;

        struct {
            const char *name;
            u64 index;
            struct AstNode *val;
        } fieldExpr;

        struct {
            struct AstNode *left;
            struct AstNode *fields;
        } structExpr;

        struct {
            struct AstNode *expr;
        } exprStmt;

        struct {
            struct AstNode *loop;
        } breakExpr, continueExpr;

        struct {
            struct AstNode *func;
            struct AstNode *expr;
        } returnStmt;

        struct {
            struct AstNode *stmts;
        } blockStmt;

        struct {
            struct AstNode *var;
            struct AstNode *range;
            struct AstNode *body;
        } forStmt;

        struct {
            struct AstNode *cond;
            struct AstNode *body;
        } whileStmt;

        struct {
            struct AstNode *cond;
            struct AstNode *cases;
        } switchStmt;

        struct {
            struct AstNode *match;
            struct AstNode *body;
        } caseStmt;
    };
} AstNode;

typedef struct AstVisitor {
    void *context;

    void (*visitors[COUNT])(struct AstVisitor *, AstNode *node);
} AstVisitor;

typedef struct ConstAstVisitor {
    void *context;

    void (*visitors[COUNT])(struct ConstAstVisitor *, const AstNode *node);
} ConstAstVisitor;

// clang-format off
#define getConstAstVisitorContext(V) ((ConstAstVisitor *)(V))->context
#define makeConstAstVisitor(C, ...) (ConstAstVisitor){.context = (C), .visitors = __VA_ARGS__}
// clang-format on

void astVisit(AstVisitor *visitor, AstNode *node);

void astConstVisit(ConstAstVisitor *visitor, const AstNode *node);

AstNode *makeAstNode(MemPool *pool, const FileLoc *loc, const AstNode *node);

AstNode *copyAstNode(AstNode *dst, const AstNode *src, FileLoc *fileLoc);

void printAst(FormatState *state, const AstNode *node);

bool isTuple(const AstNode *node);

bool isAssignableExpr(const AstNode *node);

bool isPublicDecl(const AstNode *node);

bool isOpaqueDecl(const AstNode *node);

u64 countAstNodes(const AstNode *node);

AstNode *getLastAstNode(AstNode *node);

AstNode *getParentScopeWithTage(AstNode *node);

const AstNode *getLastAstNodeConst(const AstNode *node);

const AstNode *getParentScopeWithTageConst(const AstNode *node);

void insertAstNodeAfter(AstNode *before, AstNode *after);

Operator assignOpToBinaryOp(Operator op);

const char *getPrimitiveTypeName(PrtId tag);

const char *getUnaryOpString(Operator op);

const char *getBinaryOpString(Operator op);

const char *getAssignOpString(Operator op);

const char *getBinaryOpFuncName(Operator op);

const char *getDeclKeyword(AstTag tag);

const char *getDeclName(const AstNode *node);

int getMaxBinaryOpPrecedence(void);

int getBinaryOpPrecedence(Operator op);