// Credits https://github.com/madmann91/fu/blob/master/src/fu/lang/ast.h

#pragma once

#include "core/format.h"
#include "core/log.h"
#include "core/mempool.h"
#include "lang/token.h"

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
    f(LAnd, 10, LAnd, "&&", "land")             \
    f(LOr,  11, LOr,  "||", "lor")

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
    opStringOverload,
    opInvalid
} Operator;

// clang-format on

typedef enum {
    astError,
    astProgram,
    astAttr,
    astPathElem,
    astPath,
    astGenericParam,
    astGenericDecl,
    astIdentifier,
    /* Types */
    astVoidType,
    astStringType,
    astTupleType,
    astArrayType,
    astPointerType,
    astFuncType,
    astPrimitiveType,
    astOptionalType,
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
    astAddressOf,
    astBinaryExpr,
    astAssignExpr,
    astTernaryExpr,
    astStmtExpr,
    astStringExpr,
    astTypedExpr,
    astCastExpr,
    astCallExpr,
    astMacroCallExpr,
    astClosureExpr,
    astArrayExpr,
    astIndexExpr,
    astTupleExpr,
    astFieldExpr,
    astStructExpr,
    astMemberExpr,
    astRangeExpr,
    astNewExpr,
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

enum {
    flgNone = 0,
    flgNative = BIT(0),
    flgBuiltin = BIT(1),
    flgPublic = BIT(2),
    flgPrivate = BIT(3),
    flgAsync = BIT(4),
    flgTypeAst = BIT(5),
    flgMain = BIT(6),
    flgVariadic = BIT(7),
    flgConst = BIT(8),
    flgDefault = BIT(9),
    flgDeferred = BIT(10),
    flgCapture = BIT(11),
    flgClosure = BIT(12),
    flgCapturePointer = BIT(13),
    flgClosureStyle = BIT(14),
    flgFuncTypeParam = BIT(15),
    flgMember = BIT(16),
    flgAddThis = BIT(17),
    flgAddSuper = BIT(18),
    flgTypeinfo = BIT(19),
    flgNewAllocated = BIT(20),
};

typedef struct AstNode AstNode;

typedef struct AstNodeList {
    AstNode *first;
    AstNode *last;
} AstNodeList;

typedef struct CaptureSet {
    HashTable table;
    u64 index;
} ClosureCapture;

#define CXY_AST_NODE_HEAD                                                      \
    AstTag tag;                                                                \
    FileLoc loc;                                                               \
    u64 flags;                                                                 \
    const Type *type;                                                          \
    struct AstNode *parentScope;                                               \
    struct AstNode *next;                                                      \
    struct AstNode *attrs;

struct AstNode {
    union {
        struct {
            CXY_AST_NODE_HEAD
        };
        struct {
            CXY_AST_NODE_HEAD
        } _head;
    };

    union {
        struct {
        } _body;
        struct {
            struct AstNode *decls;
        } program;

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
            struct AstNode *dim;
        } arrayType;

        struct {
            struct AstNode *type;
        } optionalType;

        struct {
            struct AstNode *params;
            struct AstNode *ret;
        } funcType;

        struct {
            PrtId id;
        } primitiveType;

        struct {
            struct AstNode *pointed;
        } pointerType;

        struct {
            struct AstNode *elements;
        } arrayExpr;

        struct {
            struct AstNode *target;
            struct AstNode *member;
        } memberExpr;

        struct {
            struct AstNode *start;
            struct AstNode *end;
            struct AstNode *step;
        } rangeExpr;

        struct {
            struct AstNode *type;
            struct AstNode *init;
        } newExpr;

        struct {
            struct AstNode *to;
            struct AstNode *expr;
        } castExpr;

        struct {
            struct AstNode *target;
            struct AstNode *index;
        } indexExpr;

        struct {
            const char *name;
            struct AstNode *constraints;
        } genericParam;

        struct {
            struct AstNode *params;
            struct AstNode *decl;
        } genericDecl;

        struct {
            const char *name;
            struct AstNode *args;
            u64 index;
        } pathElement;

        struct {
            struct AstNode *elements;
            bool isType;
        } path;

        struct {
            Operator operatorOverload;
            const char *name;
            struct AstNode *params;
            struct AstNode *ret;
            struct AstNode *body;
        } funcDecl;

        struct {
            const char *name;
            struct AstNode *params;
            struct AstNode *ret;
            struct AstNode *body;
        } macroDecl;

        struct {
            const char *name;
            struct AstNode *type;
            struct AstNode *def;
        } funcParam;

        struct {
            struct AstNode *names;
            struct AstNode *type;
            struct AstNode *init;
        } varDecl;

        struct {
            const char *name;
            struct AstNode *aliased;
        } typeDecl;

        struct {
            const char *name;
            struct AstNode *members;
        } unionDecl;

        struct {
            const char *name;
            struct AstNode *value;
            u64 index;
        } enumOption;

        struct {
            const char *name;
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
            struct AstNode *base;
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
            ClosureCapture capture;
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
            struct AstNodeList epilogue;
            struct AstNode *stmts;
            struct AstNode *last;
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
};

#define CXY_AST_NODE_BODY_SIZE (sizeof(AstNode) - sizeof(((AstNode *)0)->_head))

typedef struct AstVisitor {
    void *context;
    AstNode *current;
    void (*visitors[COUNT])(struct AstVisitor *, AstNode *node);
    void (*fallback)(struct AstVisitor *, AstNode *);
} AstVisitor;

typedef struct ConstAstVisitor {
    void *context;
    const AstNode *current;

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

AstNode *copyAstNode(MemPool *pool, const AstNode *node);
AstNode *cloneAstNode(MemPool *pool, const AstNode *node);

void printAst(FormatState *state, const AstNode *node);

#define nodeIs(NODE, TAG) ((NODE)->tag == ast##TAG)
#define hasFlag(ITEM, FLG) ((ITEM)->flags & (flg##FLG))
#define hasFlags(ITEM, FLGS) ((ITEM)->flags & (FLGS))

bool isTuple(const AstNode *node);

bool isAssignableExpr(const AstNode *node);

u64 countAstNodes(const AstNode *node);

AstNode *getLastAstNode(AstNode *node);
AstNode *getNodeAtIndex(AstNode *node, u64 index);

AstNode *getParentScopeWithTag(AstNode *node, AstTag tag);

const AstNode *getLastAstNodeConst(const AstNode *node);
const AstNode *getConstNodeAtIndex(const AstNode *node, u64 index);
const AstNode *getParentScopeWithTagConst(const AstNode *node, AstTag tag);

void insertAstNodeAfter(AstNode *before, AstNode *after);
void insertAstNode(AstNodeList *list, AstNode *node);
void unlinkAstNode(AstNode **head, AstNode *prev, AstNode *node);

const char *getPrimitiveTypeName(PrtId tag);

u64 getPrimitiveTypeSize(PrtId tag);

const char *getUnaryOpString(Operator op);

const char *getBinaryOpString(Operator op);

const char *getAssignOpString(Operator op);

const char *getBinaryOpFuncName(Operator op);

const char *getDeclKeyword(AstTag tag);

const char *getDeclName(const AstNode *node);

int getMaxBinaryOpPrecedence(void);

int getBinaryOpPrecedence(Operator op);

const AstNode *findAttribute(const AstNode *node, cstring name);
const AstNode *findAttributeArgument(const AstNode *attr, cstring name);

Operator tokenToUnaryOperator(TokenTag tag);
Operator tokenToBinaryOperator(TokenTag tag);
Operator tokenToAssignmentOperator(TokenTag tag);
bool isPrefixOpKeyword(Operator op);
