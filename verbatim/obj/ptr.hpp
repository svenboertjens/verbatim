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

    T *_ptr;

    void alloc_ptr() {
        _ptr = (T *)objalloc::malloc(sizeof(T));
    }

public:

    T *ptr() { return _ptr; }

    Ptr()
    {
        alloc_ptr();
        new (_ptr) T();   
    }

    ~Ptr()
    {
        if (!_ptr) return;

        _ptr->~T();
        objalloc::free(_ptr, sizeof(T));
    }


    Ptr(const T &val)
    {
        alloc_ptr();
        new (_ptr) T(val);
    }

    Ptr(T &&val)
    {
        alloc_ptr();
        new (_ptr) T(std::move(val));
    }


    Ptr(const Ptr &other)
    {
        alloc_ptr();
        new (_ptr) T(*other._ptr);
    }
    Ptr &operator=(const Ptr &other) {
        return tools::assign_copy_method(this, other);
    }

    Ptr(Ptr &&other)
    {
        _ptr = other._ptr;
        other._ptr = NULL;
    }
    Ptr &operator=(Ptr &&other) {
        return tools::assign_move_method(this, other);
    }


    T &operator*()  { return *_ptr; }
    T *operator->() { return _ptr; }
    const T &operator*()  const { return *_ptr; }
    const T *operator->() const { return _ptr; }

};

}

