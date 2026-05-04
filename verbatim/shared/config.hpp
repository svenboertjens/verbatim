#pragma once

#include "target/platform.hpp"
#include "tools/structs.hpp"

#include "obj/str.hpp"

#include "symbol.hpp"


namespace cfg {

struct OutType : NoCreate {
enum Enum {
    EXEC,
    DYN_EXEC,
    SHARED,
};
};

struct InlineLevel : NoCreate {
enum Enum {
    CONSERVATIVE,
    STANDARD,
    AGGRESSIVE,
    NONE
};
};

struct Config {
    // Output-related
    struct {

        // Name to give the output object
        obj::Str out_name ;

        // The type of object to build
        OutType::Enum type;

        // Default visibility for symbols
        Visibility::Enum default_visibility;
    } out;

    // Optimization-related
    struct {
        // Whether to optimize anything at all
        bool optimize = true;

        // Enables unstrict FP optimizations
        bool fast_fp = false;

        // Whether to do global memory analysis
        bool global_mem_analysis = true;

        // Inline level
        InlineLevel::Enum inline_level = InlineLevel::STANDARD;
    } opt;

    // Target-related
    struct {

        // The target architecture
        target::Arch::Enum arch;

        // The target platform
        target::Platform::Enum platform;
    } target;

    // Error-related
    struct {
        // Max errors to emit before stopping early
        int max_errors = 20;
    } errors;
};

}

