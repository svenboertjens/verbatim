#pragma once

#include <cassert>
#include <utility>

/* Tools for managing structs */

/*
 *  COPY/MOVE tools
 */

namespace tools {

/* Copy and move methods for assign operators, that reuse the dtor and copy/move ctor */

template<typename Self>
Self &assign_copy_method(Self *_this, const Self &other)
{
    if (_this == &other) return *_this;

    _this->~Self();
    new (_this) Self(other);
    
    return *_this;
}

template<typename Self>
Self &assign_move_method(Self *_this, Self &other)
{
    if (_this == &other) return *_this;

    _this->~Self();
    new (_this) Self(std::move(other));
    
    return *_this;
}

}

// To disable copying and moving in your object
struct NoCopyMove {
    NoCopyMove() {}
    ~NoCopyMove() {}

    NoCopyMove(const NoCopyMove &) = delete;
    NoCopyMove(NoCopyMove &&)      = delete;
    NoCopyMove &operator=(const NoCopyMove &) = delete;
    NoCopyMove &operator=(NoCopyMove &&)      = delete;
};


/*
 *  NO_CREATE, for structs that should not be used themselves.
 */

struct NoCreate {
    NoCreate() = delete;
    ~NoCreate() = delete;
};

