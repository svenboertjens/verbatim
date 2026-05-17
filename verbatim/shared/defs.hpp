#pragma once

#include <unistd.h>

// For sizes in objects
using ObjSize = unsigned int;

// For sizes related to types in the IR
using Size  = size_t;
using Ssize = ssize_t;

// Common struct for a size/align pair
struct SizeData {
    Size size;
    Size align;
};


using Variable = ObjSize;
static constexpr Variable VARIABLE_NONE = (Variable)-1; // Use this for NONE inputs when allowed


// IDs assigned to TUs in a GlobalUnit
using TUID = ObjSize;

