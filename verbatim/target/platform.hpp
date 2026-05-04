#pragma once

#include "tools/structs.hpp"
#include "defs.hpp"


namespace target {


/* ----------------- *
 |   ARCHITECTURES   |
 * ----------------- */

struct Arch : NoCreate {

enum Enum {
    x86_64,
};

static constexpr unsigned NARCHES = x86_64 + 1;

};


/* ------------- *
 |   PLATFORMS   |
 * ------------- */

struct Platform : NoCreate {

enum Enum {
    POSIX,
};

static constexpr unsigned NPLATFORMS = POSIX + 1;

};


/* ------------- *
 |   PTR SIZES   |
 * ------------- */

static const Size ptr_sizes[Arch::NARCHES] = {
    [Arch::x86_64] = 8,
};


/* ------------- *
 |   REGISTERS   |
 * ------------- */

struct Register : NoCreate {


struct x86_64 : NoCreate {

    enum Enum {
        /* INT */

        RAX, RBX, RCX, RDX,
        RSI, RDI, RSP, RBP,
        R8,  R9,  R10, R11,
        R12, R13, R14, R15,

        /* FP / VECTOR */

        XMM0,  XMM1,  XMM2,  XMM3,
        XMM4,  XMM5,  XMM6,  XMM7,
        XMM8,  XMM9,  XMM10, XMM11,
        XMM12, XMM13, XMM14, XMM15,

        YMM0  = XMM0,  YMM1  = XMM1,
        YMM2  = XMM2,  YMM3  = XMM3,
        YMM4  = XMM4,  YMM5  = XMM5,
        YMM6  = XMM6,  YMM7  = XMM7,
        YMM8  = XMM8,  YMM9  = XMM9,
        YMM10 = XMM10, YMM11 = XMM11,
        YMM12 = XMM12, YMM13 = XMM13,
        YMM14 = XMM14, YMM15 = XMM15,

        ZMM0  = XMM0,  ZMM1  = XMM1,
        ZMM2  = XMM2,  ZMM3  = XMM3,
        ZMM4  = XMM4,  ZMM5  = XMM5,
        ZMM6  = XMM6,  ZMM7  = XMM7,
        ZMM8  = XMM8,  ZMM9  = XMM9,
        ZMM10 = XMM10, ZMM11 = XMM11,
        ZMM12 = XMM12, ZMM13 = XMM13,
        ZMM14 = XMM14, ZMM15 = XMM15,

        /* ZMM16-ZMM32 are not supported, unnecessary complexity to support with
         * their irregularity compared to XMM/YMM being 0-15.
         */
        ZMM16, ZMM17, ZMM18, ZMM19,
        ZMM20, ZMM21, ZMM22, ZMM23,
        ZMM24, ZMM25, ZMM26, ZMM27,
        ZMM28, ZMM29, ZMM30, ZMM31,
    };

    static constexpr Reg NINT = R15 - RAX + 1;
    static constexpr Reg INT_OFF = RAX;

    static constexpr Reg NFP = XMM15 - XMM0 + 1;
    static constexpr Reg FP_OFF = XMM0;

    static constexpr Reg NVEC = XMM15 - XMM0 + 1;
    static constexpr Reg VEC_OFF = XMM0;

};


};


}

