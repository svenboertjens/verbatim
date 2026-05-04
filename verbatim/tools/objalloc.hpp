#pragma once

#include "shared/defs.hpp"

#include <cstdlib>
#include <cstring>
#include <cassert>

/* For allocating objects with stable, known sizes. Assumes single-threadedness. */

namespace objalloc {

// Keep sizes public to allow tools to work with them
static constexpr Size MAX_SIZE = 128u;
static constexpr Size STEP_SIZE = 16u;
static constexpr Size BATCH_SIZE = 1024u * 4u;

static_assert((MAX_SIZE  & (MAX_SIZE  - 1)) == 0, "MAX_SIZE must be a power of 2");
static_assert((STEP_SIZE & (STEP_SIZE - 1)) == 0, "STEP_SIZE must be a power of 2");


void *malloc(size_t size);

template<typename T>
inline T *malloc() {
    return (T *)malloc(sizeof(T));
}


void free(void *ptr, size_t size);

template<typename T>
inline void free(T *ptr) {
    return free(ptr, sizeof(T));
}


// Allows NULL reallocs
void *realloc(void *ptr, size_t old_size, size_t new_size);


}

