#pragma once

#include "tools/structs.hpp"

struct Linkage : NoCreate {
enum Enum {
    STRONG,
    WEAK,
    EXTERNAL,
};
};

struct Visibility : NoCreate {
enum Enum {
    /* Default equals VISIBLE by default, or the user-defined default.
     * The Unit automatically replaces DEFAULT with the configured default upon adding the symbol.
     */
    DEFAULT,

    PUBLIC,
    HIDDEN,
    PRIVATE,
};
};

