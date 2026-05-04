#pragma once

#include "shared/defs.hpp"
#include "basemap.hpp"

#include <cstdint>

namespace obj {

template<typename Ptr>
struct PtrKey {

    Ptr ptr;


    PtrKey(Ptr ptr) : ptr(ptr) {}

    
    bool operator==(const PtrKey &other) {
        return ptr == other.ptr;
    }

    struct MapKey {
        static ObjSize hash(const PtrKey key) {
            return (ObjSize)((uintptr_t)key.ptr >> 4);
        }

        static bool is_empty(PtrKey key) {
            return key.ptr == NULL;
        }
        static void set_empty(PtrKey &key) {
            key.ptr = NULL;
        }

        static bool is_tomb(PtrKey key) {
            return key.ptr == (Ptr)-1;
        }
        static void set_tomb(PtrKey &key) {
            key.ptr = (Ptr)-1;
        }
    };
};

template<typename Ptr, typename ValType>
using PtrMap = BaseMap<PtrKey<Ptr>, ValType>;

}

