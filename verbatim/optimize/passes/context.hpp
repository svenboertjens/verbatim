#pragma once

#include "shared/config.hpp"
#include "shared/defs.hpp"

#include "ir/symbols.hpp"
#include "ir/unit.hpp"

struct Context {

    cfg::Config *cfg;
    ir::Function *fn;
    ir::GlobalUnit *unit;
    TUID tuid;

};

