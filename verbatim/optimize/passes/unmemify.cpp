#include "tools/objalloc.hpp"
#include "tools/structs.hpp"
#include "shared/types.hpp"

#include "obj/ptrmap.hpp"

#include "ir/symbols.hpp"
#include "ir/instr.hpp"

#include "context.hpp"


namespace opt {

using namespace ir;


struct Access {
enum Enum : u8 {
    R, W, RW,
    NONE,
};
};

// Parameters' memory accessing inside a function.
// This must contain all function parameters in order, including non-pointers.
struct ParamAccesses {
    u32 naccesses;
    Access::Enum *accesses;

    ParamAccesses(Access::Enum *param_accesses, u32 nparams)
    {
        naccesses = nparams;
        accesses = (Access::Enum *)objalloc::malloc(nparams * sizeof(Access::Enum));

        for (u32 i = 0; i < nparams; i++)
            accesses[i] = param_accesses[i];
    }

    ~ParamAccesses() {
        objalloc::free(accesses, naccesses * sizeof(Access::Enum));
    }

    ParamAccesses(ParamAccesses &&other)
    {
        naccesses = other.naccesses;
        accesses  = other.accesses;

        other.naccesses = 0;
        other.accesses  = NULL;
    }

    ParamAccesses &operator=(ParamAccesses &&other) {
        return tools::assign_move_method(this, other);
    }
};

// Map of what each function accesses per parameter
using AccessesMap = obj::PtrMap<Function *, ParamAccesses>;


/********************\
    Local unmemify
\********************/

static bool local_unmemify(Context &ctx, AccessesMap &accesses)
{
    Section *section = ctx.fn->sections().bottom();

    
}


/*****************\
    Main Method
\*****************/

void unmemify()
{
    
}


}

