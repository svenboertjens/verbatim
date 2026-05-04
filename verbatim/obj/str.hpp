#pragma once

#include "tools/strcache.hpp"
#include "basemap.hpp"
#include "rawstr.hpp"
#include "array.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cassert>

namespace obj {

// Caches input strings and holds a pointer to the cached version.
struct Str {
private:

    const char *_str = NULL;

public:

    const char *str() { return _str; }

    Str() = default;

    Str(const char *str) {
        if (str == NULL) return;
        _str = strcache::cache_str(str, std::strlen(str));
    }

    Str(const char *str, size_t _len) {
        if (str == NULL) return;
        _str = strcache::cache_str(str, _len);
    }

    Str(const RawStr &raw) {
        _str = strcache::cache_str(raw.str, raw.len);
    }

    bool operator==(const Str &other) const {
        return _str == other._str;
    }

    operator const char *() const {
        return _str;
    };


    bool is_null() { return _str == NULL; }


    static constexpr size_t HASH_SHIFT = 4;

    struct MapKey {
        static unsigned hash(Str str) {
            return (uintptr_t)str._str >> HASH_SHIFT;
        }

        static bool is_empty(Str str) {
            return str._str == NULL;
        }

        static void set_empty(Str &str) {
            str._str = NULL;
        }

        static bool is_tomb(Str str) {
            return str._str == (const char *)-1;
        }

        static void set_tomb(Str &str) {
            str._str = (const char *)-1;
        }
    };
};

template<typename ValType>
using StrMap = obj::BaseMap<Str, ValType>;

using StrArray = obj::Array<Str>;

}

