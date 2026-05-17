#pragma once

#include "shared/defs.hpp"
#include "basemap.hpp"
#include "baseset.hpp"

#include <cstdint>

namespace obj {

template<typename Ptr>
struct PtrKey {
private:

    Ptr _ptr;

public:

    Ptr ptr() { return _ptr; }

    PtrKey(Ptr ptr) : _ptr(ptr) {}

    
    bool operator==(const PtrKey &other) {
        return _ptr == other._ptr;
    }

    struct MapKey {
        static ObjSize hash(const PtrKey key) {
            return (ObjSize)((uintptr_t)key._ptr >> 4);
        }

        static bool is_empty(PtrKey key) {
            return key._ptr == NULL;
        }
        static void set_empty(PtrKey &key) {
            key._ptr = NULL;
        }

        static bool is_tomb(PtrKey key) {
            return key._ptr == (Ptr)-1;
        }
        static void set_tomb(PtrKey &key) {
            key._ptr = (Ptr)-1;
        }
    };
};

template<typename Ptr, typename ValType>
using PtrMap = BaseMap<PtrKey<Ptr>, ValType>;

template<typename Ptr>
using PtrSet = BaseSet<PtrKey<Ptr>>;

}

