#pragma once

#include <cassert>
#include <cstdlib>
#include "print.hpp"

/* Basic alloc wrappers that memerrors on errors */

namespace alloc {

inline void *malloc(size_t sz)
{
    void *p = std::malloc(sz);
    if (!p && sz != 0) print::oom();

    return p;
}

inline void *calloc(size_t n, size_t sz)
{
    assert(sz > 0);

    void *p = std::calloc(n, sz);
    if (!p && n > 0) print::oom();

    return p;
}

inline void *realloc(void *p, size_t newsz)
{
    p = std::realloc(p, newsz);
    if (!p && newsz != 0) print::oom();

    return p;
}

inline void free(void *p)
{
    std::free(p);
}

}
