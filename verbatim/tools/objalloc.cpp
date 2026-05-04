#include "tools/print.hpp"
#include "tools/tools.hpp"

#include "objalloc.hpp"

#include <cstdlib>
#include <cstring>
#include <cassert>

/* For allocating objects with stable, known sizes. Assumes single-threadedness. */

namespace objalloc {

struct Link {
    Link *next;
};

static_assert(sizeof(Link) <= STEP_SIZE);

static Link *links[MAX_SIZE / STEP_SIZE] = { NULL };


static unsigned getcls(unsigned size) {
    return (size + STEP_SIZE - 1) / STEP_SIZE;
}

static void create_links(unsigned cls)
{
    char *batch = (char *)std::malloc(BATCH_SIZE);
    if (!batch) print::oom();

    Link *prev = NULL;
    unsigned step = cls * STEP_SIZE;
    for (unsigned i = 0; i < (BATCH_SIZE / step); i++)
    {
        Link *link = (Link *)(batch + i * step);
        link->next = prev;
        prev = link;
    }

    links[cls] = prev;
}


void *malloc(size_t size)
{
    assert(size > 0);

    if (size > MAX_SIZE)
    {
        void *ptr = std::malloc(size);
        if (!ptr) print::oom();
        return ptr;
    }

    unsigned cls = getcls(size);
    Link *link = links[cls];

    if (!link)
    {
        create_links(cls);
        link = links[cls];
    }

    links[cls] = link->next;
    return link;
}


void free(void *ptr, size_t size)
{
    if (!ptr)
        return;

    if (size > MAX_SIZE)
    {
        free_sized(ptr, size);
        return;
    }

    Link *ptr_link = (Link *)ptr;
    unsigned cls = getcls(size);

    ptr_link->next = links[cls];
    links[cls] = ptr_link;
}


// Allows NULL reallocs
void *realloc(void *ptr, size_t old_size, size_t new_size)
{
    void *newptr = objalloc::malloc(new_size);
    if (ptr)
    {
        std::memcpy(newptr, ptr, tools::min(old_size, new_size));
        objalloc::free(ptr, old_size);
    }

    return newptr;
}


}

