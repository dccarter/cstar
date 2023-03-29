// Credits https://github.com/madmann91/fu/blob/master/src/fu/lang/ast.h

#pragma once

#include "core/format.h"
#include "core/log.h"
#include "core/mempool.h"
#include "lang/token.h"
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
    f(Range, 13,DotDot, "..", "range")    \
    AST_ARITH_EXPR_LIST(f)                \
    AST_BIT_EXPR_LIST(f)                  \
    AST_SHIFT_EXPR_LIST(f)                \
    AST_CMP_EXPR_LIST(f)                  \
    AST_LOGIC_EXPR_LIST(f)

#define AST_ASSIGN_EXPR_LIST(f)          \
    f(Assign, 0, Assign, "",  "assign")  \
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
    f(Not,    LNot, "!")                \
    f(New,    New,  "new")              \
    f(Await,  Await, "await")           \
    f(Delete, Delete,"delete")          \

#define AST_POSTFIX_EXPR_LIST(f)        \
    f(PostDec, MinusMinus, "--")        \
    f(PostInc, PlusPlus,  "++")

#define AST_UNARY_EXPR_LIST(f)                                                 \
    AST_PREFIX_EXPR_LIST(f)                                                    \
    AST_POSTFIX_EXPR_LIST(f)

typedef enum {
#define f(name, ...) op##name,
    AST_BINARY_EXPR_LIST(f)
    AST_UNARY_EXPR_LIST(f)
#undef f
#define f(name, ...) op##name##Equal,
    opAssign,
    AST_ASSIGN_EXPR_LIST(f)
#undef f
    opCallOverload,
    opIndexOverload,
    opIndexAssignOverload,
    opInvalid
} Operator;

// clang-format on

typedef enum {
    astError,
    astProgram,
    astImplicitCast,
    astAttr,
    astPathElem,
    astPath,
    astGenericParam,
    astIdentifier,
    /* Types */
    astTupleType,
    astArrayType,
    astPointerType,
    astFuncType,
    astPrimitiveType,
    /* Literals */
    astNullLit,
    astBoolLit,
    astCharLit,
    astIntegerLit,
    astFloatLit,
    astStringLit,
    /* Declarations */
    astFuncParam,
    astFuncDecl,
    astMacroDecl,
    astConstDecl,
    astVarDecl,
    astTypeDecl,
    astUnionDecl,
    astEnumOption,
    astEnumDecl,
    astStructField,
    astStructDecl,
    /* Expressions */
    astGroupExpr,
    astUnaryExpr,
    astBinaryExpr,
    astAssignExpr,
    astTernaryExpr,
    astStmtExpr,
    astStringExpr,
    astTypedExpr,
    astCallExpr,
    astMacroCallExpr,
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
    astDeferStmt,
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
            struct AstNode *decls;
        } program;
        struct {
            struct AstNode *expr;
        } implicitCast;

        struct {
            cstring value;
        } ident;

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
            struct AstNode *args;
        } attr;

        struct {
            struct AstNode *args;
        } tupleType, tupleExpr;

        struct {
            struct AstNode *elementType;
            struct AstNode *size;
        } arrayType;

        struct {
            bool isAsync;
            struct AstNode *genericParams;
            struct AstNode *params;
            struct AstNode *ret;
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
            struct AstNode *indices;
        } indexExpr;

        struct {
            const char *name;
            struct AstNode *constraints;
        } genericParam;

        struct {
            const char *name;
            struct AstNode *args;
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
            bool isNative : 1;
            bool isAsync : 1;
            Operator operatorOverload;
            struct AstNode *genericParams;
            struct AstNode *params;
            struct AstNode *ret;
            struct AstNode *body;
        } funcDecl;

        struct {
            const char *name;
            bool isPublic;
            struct AstNode *params;
            struct AstNode *ret;
            struct AstNode *body;
        } macroDecl;

        struct {
            bool isVariadic;
            const char *name;
            struct AstNode *type;
            struct AstNode *def;
        } funcParam;

        struct {
            bool isPublic : 1;
            bool isNative : 1;
            struct AstNode *names;
            struct AstNode *type;
            struct AstNode *init;
        } constDecl, varDecl;

        struct {
            bool isPublic;
            bool isNative;
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
            struct AstNode *base;
            struct AstNode *options;
        } enumDecl;

        struct {
            const char *name;
            bool isPrivate;
            u64 index;
            struct AstNode *type;
            struct AstNode *value;
        } structField;

        struct {
            const char *name;
            bool isPublic : 1;
            struct AstNode *base;
            struct AstNode *genericParams;
            struct AstNode *members;
        } structDecl;

        struct {
            Operator op;
            struct AstNode *lhs;
            struct AstNode *rhs;
        } binaryExpr, assignExpr;

        struct {
            Operator op;
            bool isPrefix;
            struct AstNode *operand;
        } unaryExpr;

        struct {
            struct AstNode *cond;
            struct AstNode *body;
            struct AstNode *otherwise;
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
        } callExpr, macroCallExpr;

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
            struct AstNode *value;
        } fieldExpr;

        struct {
            struct AstNode *left;
            struct AstNode *fields;
        } structExpr;

        struct {
            struct AstNode *expr;
        } exprStmt, deferStmt, groupExpr;

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
            bool isDefault;
            struct AstNode *match;
            struct AstNode *body;
        } caseStmt;
    };
} AstNode;

typedef struct AstVisitor {
    void *context;

    void (*visitors[COUNT])(struct AstVisitor *, AstNode *node);
    void (*fallback)(struct AstVisitor *, AstNode *);
} AstVisitor;

typedef struct ConstAstVisitor {
    void *context;

    void (*visitors[COUNT])(struct ConstAstVisitor *, const AstNode *node);
    void (*fallback)(struct ConstAstVisitor *, const AstNode *);
} ConstAstVisitor;

// clang-format off
#define getAstVisitorContext(V) ((AstVisitor *)(V))->context
#define makeAstVisitor(C, ...) (AstVisitor){.context = (C), .visitors = __VA_ARGS__}
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

bool isNative(const AstNode *node);

u64 countAstNodes(const AstNode *node);

AstNode *getLastAstNode(AstNode *node);

AstNode *getParentScopeWithTag(AstNode *node, AstTag tag);

const AstNode *getLastAstNodeConst(const AstNode *node);

const AstNode *getParentScopeWithTagConst(const AstNode *node, AstTag tag);

void insertAstNodeAfter(AstNode *before, AstNode *after);

const char *getPrimitiveTypeName(PrtId tag);

const char *getUnaryOpString(Operator op);

const char *getBinaryOpString(Operator op);

const char *getAssignOpString(Operator op);

const char *getBinaryOpFuncName(Operator op);

const char *getDeclKeyword(AstTag tag);

const char *getDeclName(const AstNode *node);

int getMaxBinaryOpPrecedence(void);

int getBinaryOpPrecedence(Operator op);

Operator tokenToUnaryOperator(TokenTag tag);
Operator tokenToBinaryOperator(TokenTag tag);
Operator tokenToAssignmentOperator(TokenTag tag);
bool isPrefixOpKeyword(Operator op);