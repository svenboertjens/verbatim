#include "shared/types.hpp"
#include "obj/basemap.hpp"
#include "objalloc.hpp"
#include "murmur3.hpp"

#include <cstring>
#include <cstdint>
#include <cassert>

namespace strcache {

/* Key for the cache map, only stores the length and hash.
 * The pointer to the cached string is located at `Key + 1` in entries, as the value.
 */
struct Key {
    union {
        struct {
            u32 len;
            u32 hash;
        };
        u64 compare; // For comparing the length and hash together
    };

    Key(const char *_str, u32 _len)
    {
        len  = _len;
        hash = murmur3::hash(_str, _len);
    }

    // Helper for getting the string through the Key inside an Entry
    char *get_str() const { return *(char **)(this + 1); }

    bool operator==(const Key &other) const
    {
        return compare == other.compare && std::memcmp(this->get_str(), other.get_str(), len) == 0;
    }

    struct MapKey {
        static u32 hash(Key obj) {
            return obj.hash;
        }

        static bool is_empty(Key obj) { return obj.len == 0; }
        static void set_empty(Key &obj)      { obj.len =  0; }

        static bool is_tomb(Key obj) { return obj.len == (u32)-1; }
        static void set_tomb(Key &obj)      { obj.len =  (u32)-1; }
    };
};


using CacheMap = obj::BaseMap<Key, char *>;
static CacheMap cache;

const char *cache_str(const char *str, u32 len)
{
    // Empty strings not allowed
    assert(len > 0);

    // Create the key as an entry with the string in the value spot,
    // for compares to work because they expect keys from full entries.
    CacheMap::Entry key = { Key(str, len), (char *)str };

    CacheMap::Entry *empty;
    CacheMap::Entry *entry = cache.lookup(key.key, empty);

    if (entry)
        return entry->val;

    // String is not yet cached
    char *&cached_str = cache.insert(empty, key.key);

    cached_str = (char *)objalloc::malloc(len + 1);
    std::memcpy(cached_str, str, len);
    cached_str[len] = '\0';

    return cached_str;
}

}

