#pragma once

#include <cstdlib>
#include <cstdio>


// Print and exit
#define VBM_PRINT(msg, ...) std::fprintf(stderr, "verbatim: fatal: " msg "\n", ##__VA_ARGS__); std::exit(1)


namespace print {

// Prints MSG and exits
__attribute__((noreturn)) inline void exit(const char *msg) {
    VBM_PRINT("%s", msg);
}

__attribute__((noreturn)) inline void oom() {
    VBM_PRINT("failed to allocate memory");
}

__attribute__((noreturn)) inline void missing_symbol(const char *symbol) {
    VBM_PRINT("missing symbol '%s'", symbol);
}

}

#undef VBM_PRINT

