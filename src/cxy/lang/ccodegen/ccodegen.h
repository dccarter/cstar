//
// Created by Carter on 2023-03-29.
//

#include <lang/codegen.h>

typedef struct {
    CodeGenContext base;
    HashTable *generated;
} CCodegenContext;
