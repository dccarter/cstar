//
// Created by Carter on 2023-06-29.
//

#pragma once

#include "ast.h"

typedef struct AstVisitor AstVisitor;

typedef void (*Visitor)(struct AstVisitor *, AstNode *);

typedef struct AstVisitor {
    void *context;
    AstNode *current;
    AstNode **currentList;
    void (*visitors[astCOUNT])(struct AstVisitor *, AstNode *node);
    void (*fallback)(struct AstVisitor *, AstNode *);
    void (*dispatch)(Visitor, struct AstVisitor *, AstNode *);
} AstVisitor;

typedef struct ConstAstVisitor ConstAstVisitor;

typedef void (*ConstVisitor)(struct ConstAstVisitor *, const AstNode *);

typedef struct ConstAstVisitor {
    void *context;
    const AstNode *current;
    AstNode *next;

    void (*visitors[astCOUNT])(struct ConstAstVisitor *, const AstNode *node);

    void (*fallback)(struct ConstAstVisitor *, const AstNode *);

    void (*dispatch)(ConstVisitor, struct ConstAstVisitor *, const AstNode *);
} ConstAstVisitor;

// clang-format off
#define getAstVisitorContext(V) ((AstVisitor *)(V))->context
#define makeAstVisitor(C, ...) (AstVisitor){.context = (C), .visitors = __VA_ARGS__}
#define getConstAstVisitorContext(V) ((ConstAstVisitor *)(V))->context
#define makeConstAstVisitor(C, ...) (ConstAstVisitor){.context = (C), .visitors = __VA_ARGS__}
// clang-format on

void astVisitFallbackVisitAll(AstVisitor *visitor, AstNode *node);
void astConstVisitFallbackVisitAll(ConstAstVisitor *visitor,
                                   const AstNode *node);

void astVisitSkip(AstVisitor *visitor, AstNode *node);
void astConstVisitSkip(ConstAstVisitor *visitor, const AstNode *node);

void astVisit(AstVisitor *visitor, AstNode *node);
void astVisitManyNodes(AstVisitor *visitor, AstNode *node);
void astConstVisit(ConstAstVisitor *visitor, const AstNode *node);
void astConstVisitManyNodes(ConstAstVisitor *visitor, const AstNode *node);
