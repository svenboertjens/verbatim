#pragma once

#include "tools/objalloc.hpp"
#include "tools/structs.hpp"

#include <algorithm>
#include <cassert>


namespace obj {


// Stores a pointer to the set object. Doesn't allow setting different objects.
template<typename ... Ts>
struct MultiPtr {
private:

    template<typename T, typename ... _Ts>
    static constexpr bool contains = (std::is_same_v<T, _Ts> || ...);


    template<unsigned I, typename ... _Ts>
    struct type_at;

    template<unsigned I, typename T, typename ... Rest>
    struct type_at<I, T, Rest...> {
        using type = typename type_at<I - 1, Rest...>::type;
    };

    template<typename T, typename ... Rest>
    struct type_at<0, T, Rest...> {
        using type = T;
    };

    
    template<typename T, typename... _Ts>
    struct index_of;

    template<typename T, typename... Rest>
    struct index_of<T, T, Rest...> {
        static constexpr unsigned value = 0;
    };

    template<typename T, typename U, typename... Rest>
    struct index_of<T, U, Rest...> {
        static constexpr unsigned value = 1 + index_of<T, Rest...>::value;
    };

public:

    static constexpr unsigned EMPTY_TAG = (unsigned)-1;
    unsigned tag = EMPTY_TAG;
    void *ptr = NULL;


    MultiPtr() = default;
    ~MultiPtr()
    {
        if (tag == EMPTY_TAG) return;

        [&]<unsigned ... Is>(std::index_sequence<Is...>)
        {
            ((tag == Is ?
                (((Ts *)ptr)->~Ts(), objalloc::free<Ts>(ptr))
            : void()), ...);
        }
        (std::index_sequence_for<Ts...>{});
    }

    MultiPtr(MultiPtr &&other)
    {
        tag = other.tag;
        if (tag == EMPTY_TAG) return;
        
        [&]<unsigned... Is>(std::index_sequence<Is...>)
        {
            ((tag == Is ?
                (void)(ptr = objalloc::malloc<Ts>(), new (ptr) Ts(std::move(*(Ts *)other.ptr)))
            : void()), ...);
        }
        (std::index_sequence_for<Ts...>{});

        other.ptr = NULL;
        other.tag = EMPTY_TAG;
    }
    MultiPtr &operator=(MultiPtr &&other) {
        return tools::assign_move_method(this, other);
    }


    template<typename T>
    T &get() const
    {
        static_assert(contains<T, Ts...>);
        if (tag != index_of<T, Ts...>::value) assert(0);
        
        return *(T *)ptr;
    }

    template<typename T>
    operator T&() const {
        return get();
    }


    template<typename T>
    void set(T &&v)
    {
        static_assert(contains<T, Ts...>);
        assert(tag == EMPTY_TAG);

        tag = index_of<T, Ts...>::value;
        ptr = objalloc::malloc<T>();
        new ((T *)ptr) T(std::move(v));
    }

    template<typename T>
    MultiPtr(T &&other) {
        set(std::move(other));
    }
    template<typename T>
    MultiPtr &operator=(T &&other) {
        return tools::assign_move_method(this, other);
    }
};


}

