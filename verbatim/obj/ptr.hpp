#pragma once

#include "tools/objalloc.hpp"
#include "tools/structs.hpp"

#include <cstddef>
#include <utility>

namespace obj {

// A pointer that automatically allocates and frees itself
template<typename T>
struct Ptr {
private:

    T *ptr;

    void alloc_ptr() {
        ptr = (T *)objalloc::malloc(sizeof(T));
    }

public:

    Ptr()
    {
        alloc_ptr();
        new (ptr) T();   
    }

    ~Ptr()
    {
        if (!ptr) return;

        ptr->~T();
        objalloc::free(ptr, sizeof(T));
    }


    Ptr(const T &val)
    {
        alloc_ptr();
        new (ptr) T(val);
    }

    Ptr(T &&val)
    {
        alloc_ptr();
        new (ptr) T(std::move(val));
    }


    Ptr(const Ptr &other)
    {
        alloc_ptr();
        new (ptr) T(*other.ptr);
    }
    Ptr &operator=(const Ptr &other) {
        return tools::assign_copy_method(this, other);
    }

    Ptr(Ptr &&other)
    {
        ptr = other.ptr;
        other.ptr = NULL;
    }
    Ptr &operator=(Ptr &&other) {
        return tools::assign_move_method(this, other);
    }


    T &operator*()  { return *ptr; }
    T *operator->() { return ptr; }
    const T &operator*()  const { return *ptr; }
    const T *operator->() const { return ptr; }

};

}

