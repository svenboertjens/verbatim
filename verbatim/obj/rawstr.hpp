#pragma once

#include "tools/objalloc.hpp"
#include "tools/murmur3.hpp"
#include "basemap.hpp"

#include <cstddef>
#include <cstring>
#include <cstdint>
#include <cassert>

/* Raw string, mutable and doesn't cache itself */

namespace obj {

struct RawStr {

    unsigned len;
    unsigned cap;
    char *str;

private:

    void destruct() {
        objalloc::free(str, cap);
    }

    void copy(const RawStr &other)
    {
        len = other.len;
        cap = other.cap;

        str = (char *)objalloc::malloc(cap);
        std::memcpy(str, other.str, len + 1);
    }

    void move(RawStr &other)
    {
        len = other.len;
        cap = other.cap;
        str = other.str;

        other.len = 0;
        other.cap = calc_cap(other.len);
        other.str = NULL;
    }

    // Simply rounds up to the allocator's step size.
    // Adds 1 to LEN for the NUL
    unsigned calc_cap(unsigned len) {
        return ((len + 1) + objalloc::STEP_SIZE - 1) & ~(objalloc::STEP_SIZE - 1);
    }

public:

    RawStr(const char *_str, unsigned _len)
    {
        len = _len;
        cap = calc_cap(_len);

        str = (char *)objalloc::malloc(cap);
        std::memcpy(str, _str, _len);
        str[_len] = '\0';
    }

    RawStr() {
        RawStr(NULL, 0);
    }

    RawStr(const char *_str) {
        RawStr(_str, std::strlen(_str));
    }

    ~RawStr() { destruct(); }


    RawStr(const RawStr &other) { copy(other); }
    RawStr &operator=(const RawStr &other)
    {
        assert(this != &other);

        destruct();
        copy(other);
        return *this;
    }

    RawStr(RawStr &&other) { move(other); }
    RawStr &operator=(RawStr &&other)
    {
        assert(this != &other);

        destruct();
        move(other);
        return *this;
    }


    void append(const char *to_add, unsigned _len)
    {
        unsigned newlen = len + _len;
        if (newlen + 1 > cap)
        {
            cap = calc_cap(newlen);
            str = (char *)objalloc::realloc(str, len, cap);
        }

        memcpy(str + len, to_add, _len);
        str[newlen] = '\0';
        len = newlen;
    }

    void append(const char *to_add) {
        return append(to_add, strlen(to_add));
    }

    void append(const obj::RawStr &to_add) {
        append(to_add.str, to_add.len);
    }


    bool operator==(const RawStr &other) const {
        return len == other.len && std::memcmp(str, other.str, len) == 0;
    }

    struct MapKey {
        static unsigned hash(const RawStr str)
        {
            uint64_t hash64 = murmur3::hash(str.str, str.len);
            return (unsigned)hash64 + (unsigned)(hash64 >> 32);
        }

        static bool is_empty(const RawStr str) {
            return str.str == NULL;
        }
        static void set_empty(RawStr &str) {
            str.str = NULL;
        }

        static bool is_tomb(const RawStr str) {
            return str.str == (char *)-1;
        }
        static void set_tomb(RawStr &str) {
            str.str = (char *)-1;
        }
    };

};


template <typename ValueType>
using RawStrMap = BaseMap<RawStr, ValueType>;

}

