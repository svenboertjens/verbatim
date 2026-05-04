#pragma once

#include "tools/structs.hpp"

#include <algorithm>
#include <cassert>

namespace obj {


template <typename ... Ts>
struct Multi {
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

private:

    static constexpr unsigned MAX_SIZE  = std::max({ sizeof(Ts)... });
    static constexpr unsigned MAX_ALIGN = std::max({ alignof(Ts)... });
    unsigned _tag = EMPTY_TAG;

    alignas(MAX_ALIGN) char storage[MAX_SIZE];

public:

    static constexpr unsigned EMPTY_TAG = (unsigned)-1;

    unsigned tag() { return _tag; }


    Multi() = default;
    ~Multi() { destruct(); }

    Multi(const Multi &other)
    {
        _tag = other._tag;
        if (_tag == EMPTY_TAG) return;

        [&]<unsigned... Is>(std::index_sequence<Is...>)
        {
            ((_tag == Is
                ? (void)(new (storage) Ts(*(const Ts *)other.storage))
                : void()),
            ...);
        }
        (std::index_sequence_for<Ts...>{});
    }
    Multi &operator=(const Multi &other) {
        return tools::assign_copy_method(this, other);
    }

    Multi(Multi &&other)
    {
        _tag = other._tag;
        if (_tag == EMPTY_TAG) return;
        
        [&]<unsigned... Is>(std::index_sequence<Is...>)
        {
            ((_tag == Is
                ? (void)(new (storage) Ts(std::move(*(Ts *)other.storage)))
                : void()),
            ...);
        }
        (std::index_sequence_for<Ts...>{});

        other.destruct();
        other._tag = EMPTY_TAG;
    }
    Multi &operator=(Multi &&other) {
        return tools::assign_move_method(this, other);
    }


    template<typename T>
    T &get()
    {
        static_assert(contains<T, Ts...>);
        if (_tag != index_of<T, Ts...>::value) assert(0);
        
        return *(T *)storage;
    }

    template<typename T>
    void set(const T &v)
    {
        static_assert(contains<T, Ts...>);
        destruct();

        new ((T *)storage) T(v);
        _tag = index_of<T, Ts...>::value;
    }

    template<typename T>
    void set(T &&v)
    {
        static_assert(contains<T, Ts...>);
        destruct();

        new ((T *)storage) T(std::move(v));
        _tag = index_of<T, Ts...>::value;
    }


    template<typename T>
    operator T*() const
    {
        static_assert(contains<T, Ts...>);
        return (T *)storage;
    }

    template<typename T>
    operator T&() const
    {
        static_assert(contains<T, Ts...>);
        return *(T *)storage;
    }


    template<typename T>
    bool isa() const {
        return index_of<T, Ts...>::value == _tag;
    }


private:

    void destruct()
    {
        if (_tag == EMPTY_TAG) return;

        [&]<unsigned ... Is>(std::index_sequence<Is...>)
        {
            ((_tag == Is ? ((Ts *)storage)->~Ts() : void()), ...);
        }
        (std::index_sequence_for<Ts...>{});
    }

};


}

