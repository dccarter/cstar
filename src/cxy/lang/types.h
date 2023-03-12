//
// Created by Carter on 2023-03-11.
//

#pragma once

/*
 * Front-end types, including a simple module system based on M. Lillibridge's translucent
 * sums, and HM-style polymorphism. Types should always be created via a `TypeTable` object.
 */

#define PRIM_TYPE_LIST(f) \
    f(Bool, "bool")       \
    f(Char, "char")       \
    f(I8,   "i8")         \
    f(I16,  "i16")        \
    f(I32,  "i32")        \
    f(I64,  "i64")        \
    f(U8,   "u8")         \
    f(U16,  "u16")        \
    f(U32,  "u32")        \
    f(U64,  "u64")        \
    f(F32,  "f32")        \
    f(F64,  "f64")