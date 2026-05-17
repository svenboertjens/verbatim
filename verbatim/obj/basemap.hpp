#pragma once

#include "tools/structs.hpp"
#include "tools/tools.hpp"
#include "tools/alloc.hpp" // No objalloc, maps tend to grow big

#include "shared/defs.hpp"

#include <cstddef>
#include <cassert>
#include <utility>

namespace obj {

/* Map base.
 * 
 * Requires the KeyType to have an equals (operator==) method, and a MapKey struct with:
 * - static ObjSize hash(const KeyType &key)
 * - static bool is_empty(const KeyType &key)
 * - static void set_empty(KeyType &key)
 * - static bool is_tomb(const KeyType &key)
 * - static void set_tomb(KeyType &key)
 */
template <typename KeyType, typename ValType>
struct BaseMap {

    struct Entry {
        KeyType key;
        ValType val;
    };

private:

    static constexpr double LOAD_FACTOR = 0.75;
    static constexpr ObjSize MIN_CAP = 4;

    ObjSize _cap = MIN_CAP;
    ObjSize _size = 0;
    Entry *_entries = NULL;

private:

    void construct_entries()
    {
        _entries = alloc::malloc(_cap * sizeof(Entry));

        for (ObjSize i = 0; i < _cap; ++i)
            KeyType::MapKey::set_empty(_entries[i].key);
    }

    void resize()
    {
        ObjSize old_cap = _cap;
        Entry *old_entries = _entries;
        ObjSize todo = _size;

        _cap *= 2;
        construct_entries();

        for (ObjSize i = 0; i < old_cap; i++)
        {
            Entry *old_entry = &old_entries[i];

            if (KeyType::MapKey::is_empty(old_entry->key) || KeyType::MapKey::is_tomb(old_entry->key))
                continue;

            Entry *new_entry = lookup(old_entry->key);
            new (new_entry) Entry(std::move(*old_entry));
            old_entry->~Entry();

            // Early exit if we moved everything
            if (--todo == 0)
                break;
        }

        alloc::free(old_entries);
    }

    // Convenience function when EMPTY isn't required.
    // Private because `get()` should be used for such lookups outside the class.
    Entry *lookup(KeyType key)
    {
        Entry *unused;
        return lookup(key, unused);
    }

public:

    ObjSize size() { return _size; }


    BaseMap() = default;
    ~BaseMap()
    {
        if (!_entries) return;

        for (ObjSize i = 0; i < _cap; i++)
        {
            Entry &entry = _entries[i];

            if (!KeyType::MapKey::is_empty(entry.key) && !KeyType::MapKey::is_tomb(entry.key))
                entry.~Entry();
        }

        alloc::free(_entries);
    }

    BaseMap(ObjSize init_cap)
    {
        _cap = tools::max(init_cap, MIN_CAP);
        _cap = tools::round_pow2(_cap);
    }


    BaseMap(const BaseMap &other)
    {
        _cap = other._cap;
        _size = other._size;
        _entries = alloc::malloc(other._cap * sizeof(Entry));

        for (ObjSize i = 0; i < _cap; i++)
        {
            Entry &other_entry = other._entries[i];
            Entry &entry = _entries[i];

            if (KeyType::MapKey::is_empty(other_entry.key) || KeyType::MapKey::is_tomb(other_entry.key))
                KeyType::MapKey::set_empty(entry.key);
            else
                new (&entry) Entry(other_entry);
        }
    }

    BaseMap &operator=(const BaseMap &other) {
        return tools::assign_copy_method(this, other);
    }

    BaseMap(BaseMap &&other)
    {
        _cap = other._cap;
        _size = other._size;
        _entries = other._entries;

        other._cap = MIN_CAP;
        other._size = 0;
        other._entries = NULL;
    }

    BaseMap &operator=(BaseMap &&other) {
        return tools::assign_move_method(this, other);
    }


    /* Returns the first match, or NULL if no match.
     * EMPTY is set to the first empty/tombstone entry.
     */
    Entry *lookup(KeyType key, Entry *&empty)
    {
        if (!_entries)
            construct_entries();

        empty = NULL;

        ObjSize hash = KeyType::MapKey::hash(key);
        ObjSize cursor = hash;
        while (cursor < (hash + _cap)) // Iterate until we did a full loop, against bad tombstone cases
        {
            unsigned idx = cursor++ & (_cap - 1);
            Entry *entry = &_entries[idx];

            if (KeyType::MapKey::is_empty(entry->key))
            {
                if (!empty) empty = entry;
                return NULL;
            }

            else if (KeyType::MapKey::is_tomb(entry->key))
                { if (!empty) empty = entry; }

            else if (key == entry->key)
                return entry;
        }

        // EMPTY should be set when this is reached
        return NULL;
    }


    // Insert a new key through a known entry that was found using `lookup()`.
    // Returns the ValType of the entry; The ENTRY pointer cannot be relied on after insertion.
    ValType &insert(Entry *entry, KeyType key)
    {
        new (&entry->key) KeyType(key);
        
        // Check if we need to resize
        if (++_size >= _cap * LOAD_FACTOR)
        {
            resize();
            entry = lookup(key); // Shouldn't return NULL
            assert(entry != NULL);
        }

        new (&entry->val) ValType();
        return entry->val;
    }

    
    ValType &operator[](KeyType key)
    {
        Entry *empty;
        Entry *entry = lookup(key, empty);

        if (!entry)
            return insert(empty, key);

        return entry->val;
    }

    const ValType &operator[](KeyType key) const
    {
        Entry *empty;
        Entry *entry = lookup(key, empty);

        if (!entry)
            return insert(empty, key);

        return entry->val;
    }


    // Get a pointer to the value of a key.
    // Returns NULL if the key isn't present.
    ValType *get(KeyType key)
    {
        if (!_entries) return NULL;

        Entry *empty;
        Entry *entry = lookup(key, empty);
        
        if (empty)
        {
            // EMPTY will always be non-NULL if ENTRY is NULL
            if (!entry) return NULL;

            // Move our entry forward if there was a tombstone
            new (empty) Entry(std::move(*entry));
            entry->~Entry();
            entry = empty;
        }

        return &entry->val;
    }


    void remove(KeyType key)
    {
        Entry *entry = lookup(key);
        if (!entry) return;

        _size--;
        KeyType::MapKey::set_tomb(entry->key);
        entry->val.~ValType();
    }


    // Iterate over the map (not in insertion order).
    // ITER must be initialized to 0.
    // FALSE is returned on map end.
    // Map must not be mutated during iteration.
    bool next(ObjSize &iter, KeyType *&key, ValType *&val) const
    {
        while (iter < _cap)
        {
            Entry *entry = &_entries[iter];

            if (!KeyType::MapKey::is_empty(entry->key) && !KeyType::MapKey::is_tomb(entry->key))
            {
                key = &entry->key;
                val = &entry->val;
                return true;
            }

            iter++;
        }

        return false;
    }

};

}

