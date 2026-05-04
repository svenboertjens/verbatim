#pragma once

#include <concepts>

#define always_inline inline __attribute__((always_inline))
#define no_inline __attribute__((noinline))

namespace tools {


template <std::integral T>
always_inline constexpr int ctz(T x)
{
    if constexpr (sizeof(T) == 8)
        return __builtin_ctzll((unsigned long long)x);
    else
        return __builtin_ctz((unsigned)x);
}

template <std::integral T>
always_inline constexpr int clz(T x)
{
    if constexpr (sizeof(T) == 8)
        return __builtin_clzll((unsigned long long)x);
    else
        return __builtin_clz((unsigned)x);
}


template<std::integral T>
always_inline constexpr T min(T a, T b) {
    return a < b ? a : b;
}

template<std::integral T>
always_inline constexpr T max(T a, T b) {
    return a > b ? a : b;
}


template<std::unsigned_integral T>
always_inline constexpr T round_pow2(T x)
{
    if (x <= 1) return 1;
    return (T)1 << ((sizeof(T) * 8) - clz(x - 1));
}

template<typename T>
always_inline constexpr T is_pow2(T x) {
    return (x & (x - 1)) == 0;
}


template<std::integral T>
always_inline bool mul_overflow_check(T a, T b, T &instr) {
    return __builtin_mul_overflow(a, b, &instr);
}

template<std::integral T>
always_inline bool mul_overflow_check(T a, T b)
{
    T _unused;
    return __builtin_mul_overflow(a, b, _unused);
}


template<class ... Types>
struct overload : Types... { using Types::operator() ...; };


}

