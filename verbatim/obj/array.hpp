#pragma once

#include "shared/defs.hpp"

#include "tools/structs.hpp"
#include "tools/tools.hpp"
#include "tools/alloc.hpp"

#include <cstddef>
#include <cassert>
#include <utility>


namespace obj {

template<typename ItemType>
struct Array {
private:

    static constexpr ObjSize MIN_CAP = 2;


    ObjSize cap = MIN_CAP;
    ObjSize _size = 0;
    ItemType *items = NULL;

private:

    void destruct()
    {
        for (ObjSize i = 0; i < _size; i++)
            items[i].~ItemType();

        alloc::free(items, cap * sizeof(ItemType));
    }

    // OLD_ITEMS may be null if SIZE == 0.
    // Frees OLD_ITEMS after moving all items.
    void construct_items(ItemType *old_items, ObjSize old_cap)
    {
        items = alloc::malloc(cap * sizeof(ItemType));

        for (ObjSize i = 0; i < _size; i++)
        {
            ItemType &old_item = old_items[i];

            new (&items[i]) ItemType(std::move(old_item));
            old_item.~ItemType();
        }

        alloc::free(old_items, sizeof(ItemType) * old_cap);
    }

    // Checks if there's enough space to add an item
    void check_space()
    {
        if ((_size + 1) > cap || !items)
        {
            ObjSize old_cap = cap;
            cap *= 2;
            construct_items(items, old_cap); // Automatically handles uninitialized array cases
        }
    }

    // Shift elements down by one, calling the destructor of SHIFT_DOWN_TO
    void shift_down(ObjSize shift_down_to)
    {
        items[shift_down_to].~ItemType();

        for (ObjSize i = shift_down_to + 1; i < _size; i++)
        {
            items[i - 1] = std::move(items[i]);
            items[i]->~ItemType();
        }
    }

public:

    ObjSize size() const { return _size; }


    Array() = default;
    ~Array() { destruct(); }

    Array(ObjSize init_cap)
    {
        cap = tools::max(init_cap, MIN_CAP);
        _size = 0;
        items = NULL;
    }

    Array(const Array &other)
    {
        cap = other.cap;
        _size = other._size;
        items = alloc::malloc(cap * sizeof(ItemType));

        for (ObjSize i = 0; i < _size; i++)
            new (&items[i]) ItemType(other.items[i]);
    }
    Array &operator=(const Array &other) {
        return tools::assign_copy_method(this, other);
    }

    Array(Array &&other)
    {
        cap = other.cap;
        _size = other._size;
        items = other.items;

        other.cap = MIN_CAP;
        other._size = 0;
        other.items = NULL;
    }
    Array &operator=(Array &&other) {
        return tools::assign_move_method(this, other);
    }


    ItemType &operator[](ObjSize idx) const
    {
        assert(idx < _size);
        return items[idx];
    }

    // Pop the top element
    void pop()
    {
        assert(_size > 0);
        items[--_size].~ItemType();
    }

    // Pop an element from a specific index
    void pop(ObjSize idx)
    {
        assert(_size > idx);
        shift_down(idx);
    }

    // Returns the index of the pushed item
    ObjSize push(const ItemType &item)
    {
        check_space();

        ObjSize idx = _size++;
        new (&items[idx]) ItemType(item);

        return idx;
    }

    // Returns the index of the pushed item
    ObjSize push(ItemType &&item)
    {
        check_space();

        ObjSize idx = _size++;
        new (&items[idx]) ItemType(std::move(item));

        return idx;
    }

    // Creates an empty-initialized element and returns its index
    ObjSize push_new() {
        return push(ItemType());
    }


    // Returns the index of a specific item, or -1 if not present
    ObjSize find(const ItemType &item)
    {
        for (ObjSize i = 0; i < _size; i++)
        {
            if (items[i] != item)
                continue;

            return i;
        }
    }

    // Remove a specific item
    void remove(const ItemType &item)
    {
        ObjSize idx = find(item);
        if (idx == -1) return;
        shift_down(idx);
    }

};

};

