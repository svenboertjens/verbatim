#pragma once

#include "tools/structs.hpp"
#include "defs.hpp"

struct Primitive : NoCreate {

// `is_*()` methods rely on order, don't randomly change
enum Enum {
    // For unset output types
    unset,

    // Int
    i1,
    i8,
    i16,
    i32,
    i64,
    i128,

    // Float
    f16,
    f32,
    f64,
    f128,

    // Ptr
    ptr,

    // Vector
    vector
};

// Value of the last Primitive enum
static constexpr Primitive::Enum MAX_VALUE = Primitive::vector;


// Primitives' names
static inline const char *names[MAX_VALUE + 1] = {
    "unset",
    "i1", "i8", "i16", "i32", "i64", "i128",
    "f16", "f32", "f64", "f128",
    "ptr", "vector"
};

// Name to use in Types for struct type strings.
// This name mustn't collide with any of the names for primitives.
static constexpr const char *struct_typestr_prefix = "struct";


// Value for invalid type sizes
static constexpr unsigned VALUETYPE_SIZE_INVALID = (unsigned)-1;

// The size of the largest scalar primitive
static constexpr Size LARGEST_SCALAR = 16;

// Size of each Primitive, in bytes
static constexpr Size sizes[MAX_VALUE + 1] = {
    VALUETYPE_SIZE_INVALID, // Unset

    1, 1, 2, 4, 8, 16, // Ints
    2, 4, 8, 16,       // Floats

    VALUETYPE_SIZE_INVALID, // Ptr (size is platform-dependent, TypeData sets correct size on initialization)
    VALUETYPE_SIZE_INVALID  // Vector
};


static inline bool is_int(Primitive::Enum type) {
    return type >= i1 && type <= i128;
}

static inline bool is_fp(Primitive::Enum type) {
    return type >= f16 && type <= f128;
}

static inline bool is_scalar(Primitive::Enum type) {
    return type >= i1 && type <= ptr;
}

};

