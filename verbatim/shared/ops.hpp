#pragma once

#include "tools/structs.hpp"


/* INT/FP */

struct IntOp : NoCreate {
enum Enum {
    ADD, SUB,
    MUL, DIV,
    MOD,

    AND, OR,
    XOR, SHL,
    SHR, // SHR is a SAR if signed

    // NEG -> `0 - x`
    // NOT -> `x ^ -1`

    EQ,  NEQ,
    LT,  LE,
    GT,  GE,
};
enum Flags {
    NOFLAGS = 0,
    SIGNED  = 1 << 0, // Set = signed, unset = unsigned
    NOWRAP  = 1 << 1,
    //EXACT = 1 << 2,
};

static constexpr unsigned MAX_VALUE = GE;
};

struct FpOp : NoCreate {
enum Enum {
    ADD, SUB,
    MUL, DIV,

    // NEG -> `0.0 - x`, transformed to a NEG by MCR for correctness

    EQ, NEQ,
    LT, LE,
    GT, GE,
};
enum Flags {
    NOFLAGS = 0,
    ORDERED = 1 << 0, // Set = ordered, unset = unordered
};

static constexpr unsigned MAX_VALUE = GE;
};


/* Atomics */

struct AtomicOp : NoCreate {
enum Enum {
    ADD, SUB,

    AND, NAND,
    OR,  XOR,

    MIN, MAX,

    SWAP,
};

static constexpr unsigned MAX_VALUE = SWAP;
};

