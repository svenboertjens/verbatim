#pragma once

#include "tools/structs.hpp"
#include "tools/tools.hpp"
#include "tools/alloc.hpp"

#include "shared/defs.hpp"

#include <cstddef>
#include <utility>


namespace obj {


// Set base, requires the MapKey struct from `obj::BaseMap`.
template <typename KeyType>
struct BaseSet {

private:

    static constexpr double LOAD_FACTOR = 0.75;
    static constexpr ObjSize MIN_CAP = 4;

    ObjSize _cap = MIN_CAP;
    ObjSize _size = 0;
    KeyType *_entries = NULL;

private:

    void construct_entries()
    {
        _entries = alloc::malloc(_cap * sizeof(KeyType));

        for (ObjSize i = 0; i < _cap; ++i)
            KeyType::MapKey::set_empty(_entries[i].key);
    }

    void resize()
    {
        ObjSize old_cap = _cap;
        KeyType *old_entries = _entries;
        ObjSize todo = _size;

        _cap *= 2;
        construct_entries();

        for (ObjSize i = 0; i < old_cap; i++)
        {
            KeyType *old_entry = &old_entries[i];

            if (KeyType::MapKey::is_empty(old_entry->key) || KeyType::MapKey::is_tomb(old_entry->key))
                continue;

            KeyType *new_entry = lookup(old_entry->key);
            new (new_entry) KeyType(std::move(*old_entry));
            old_entry->~Entry();

            // Early exit if we moved everything
            if (--todo == 0)
                break;
        }

        alloc::free(old_entries);
    }



    /* Returns the first match, or NULL if no match.
     * EMPTY is set to the first empty/tombstone entry.
     */
    KeyType *lookup(KeyType key, KeyType *&empty)
    {
        if (!_entries)
            construct_entries();

        empty = NULL;

        ObjSize hash = KeyType::MapKey::hash(key);
        ObjSize cursor = hash;
        while (cursor < (hash + _cap)) // Iterate until we did a full loop, against bad tombstone cases
        {
            unsigned idx = cursor++ & (_cap - 1);
            KeyType *key2 = &_entries[idx];

            if (KeyType::MapKey::is_empty(key2))
            {
                if (!empty) empty = key2;
                return NULL;
            }

            else if (KeyType::MapKey::is_tomb(key2))
                { if (!empty) empty = key2; }

            else if (key == key2)
                return key2;
        }

        // EMPTY should be set when this is reached
        return NULL;
    }

    // Convenience function when EMPTY isn't required.
    KeyType *lookup(KeyType key)
    {
        KeyType *unused;
        return lookup(key, unused);
    }

public:

    ObjSize size() { return _size; }


    BaseSet() = default;
    ~BaseSet()
    {
        if (!_entries) return;

        for (ObjSize i = 0; i < _cap; i++)
        {
            KeyType &entry = _entries[i];

            if (!KeyType::MapKey::is_empty(entry.key) && !KeyType::MapKey::is_tomb(entry.key))
                entry.~Entry();
        }

        alloc::free(_entries);
    }

    BaseSet(ObjSize init_cap)
    {
        _cap = tools::max(init_cap, MIN_CAP);
        _cap = tools::round_pow2(_cap);
    }


    BaseSet(const BaseSet &other)
    {
        _cap = other._cap;
        _size = other._size;
        _entries = alloc::malloc(other._cap * sizeof(KeyType));

        for (ObjSize i = 0; i < _cap; i++)
        {
            KeyType &other_entry = other._entries[i];
            KeyType &entry = _entries[i];

            if (KeyType::MapKey::is_empty(other_entry.key) || KeyType::MapKey::is_tomb(other_entry.key))
                KeyType::MapKey::set_empty(entry.key);
            else
                new (&entry) KeyType(other_entry);
        }
    }

    BaseSet &operator=(const BaseSet &other) {
        return tools::assign_copy_method(this, other);
    }

    BaseSet(BaseSet &&other)
    {
        _cap = other._cap;
        _size = other._size;
        _entries = other._entries;

        other._cap = MIN_CAP;
        other._size = 0;
        other._entries = NULL;
    }

    BaseSet &operator=(BaseSet &&other) {
        return tools::assign_move_method(this, other);
    }


    // Returns TRUE if it's a new insertion
    bool insert(KeyType key)
    {
        KeyType *empty;
        KeyType *entry = lookup(key, empty);

        if (entry)
            return false;

        new (&entry->key) KeyType(key);
        
        // Check if we need to resize
        if (++_size >= _cap * LOAD_FACTOR)
        {
            resize();
            entry = lookup(key); // Shouldn't return NULL
            assert(entry != NULL);
        }

        return true;
    }


    void remove(KeyType key)
    {
        KeyType *entry = lookup(key);
        if (!entry) return;

        _size--;
        KeyType::MapKey::set_tomb(entry);
        entry->val.~ValType();
    } 

    
    // Iterate over the set (not in insertion order).
    // ITER must be initialized to 0.
    // FALSE is returned on iteration end.
    // Set must not be mutated during iteration.
    bool next(ObjSize &iter, KeyType *&key) const
    {
        while (iter < _cap)
        {
            key = &_entries[iter];

            if (!KeyType::MapKey::is_empty(*key) && !KeyType::MapKey::is_tomb(*key))
                return true;

            iter++;
        }

        return false;
    }

};

}

