#include "shared/types.hpp"

#include "tools/structs.hpp"
#include "tools/tools.hpp"

#include "obj/ptrmap.hpp"
#include "obj/array.hpp"

#include "ir/symbols.hpp"
#include "ir/instr.hpp"

#include "context.hpp"


// Memory optimizations


namespace opt {

using namespace ir;


using PtrAllowance = obj::Array<Ssize>; // The allowed offsets for a pointer
using SecAllowance = obj::PtrMap<Instr *, PtrAllowance>;
using SecAllowanceMap = obj::PtrMap<Section *, SecAllowance>;


// Walk to the next memory access (or access-influencing instr), starting below LAST.
// Also compares whether the next instruction is equal to CMP1 or CMP2.
// Returns NULL when we can't walk further in this section.
static always_inline Instr *walk_to_access(Instr *last, Instr *cmp1, Instr *cmp2, bool &matched_cmp, bool walk_to_next)
{
    static const bool kind_is_access[InstrKind::MAX_VALUE] = {
        [0 ... InstrKind::MAX_VALUE - 1] = false,

        [InstrKind::LOAD ] = true,
        [InstrKind::STORE] = true,

        [InstrKind::ATOMIC_LOAD ] = true,
        [InstrKind::ATOMIC_STORE] = true,
        [InstrKind::ATOMIC_OP   ] = true,
        [InstrKind::ATOMIC_CAS  ] = true,
        [InstrKind::ATOMIC_FENCE] = true,

        [InstrKind::SYMCALL] = true,
        [InstrKind::PTRCALL] = true,
        [InstrKind::ASM    ] = true 
    };


    matched_cmp = false;

    Instr *instr = last;
    while (!instr->is_first())
    {
        if (walk_to_next)
            instr = instr->next();
        else
            instr = instr->prev();

        if (instr == cmp1 || instr == cmp2)
        {
            matched_cmp = true;
            return instr;
        }

        InstrKind::Enum kind = instr->kind();

        if (kind_is_access[kind])
            return instr;
    }

    return NULL;
}

static bool offsets_overlap(Ssize off1, Ssize off2, Ssize size1, Ssize size2) {
    return off1 < off2 + size2 && off2 < off1 + size1;
}

// Check if INNER sits within OUTER
static bool access_sits_within(Ssize outer_off, Ssize inner_off, Ssize outer_size, Ssize inner_size) {
    return inner_off >= outer_off && inner_off + inner_size <= outer_off + outer_size;
}

static bool asm_touches_ptr(AsmInstr *assembly) {
    return assembly->taints_memory();
}

// Does not check for full pointer equality, only type-based and rule-based
static bool aliases_access(Instr *main_instr, Instr *other_instr, MemInfoBase *main_mem, MemInfoBase *other_mem, PtrID main_ptr_id, PtrID other_ptr_id)
{
    if (main_mem->ptr_exclusive() || other_mem->ptr_exclusive())
    {
        if (main_ptr_id != other_ptr_id)
            return false;

        // If PTR_IDs equal while exclusive, continue checking as usual, same rules apply as otherwise
    }

    if (main_mem->aliases_all() || other_mem->aliases_all())
    {
        // If both are structs, the offsets need to overlap for them to alias, otherwise they can't alias even with ALIASES_ALL
        if (main_mem->struct_access() && other_mem->struct_access())
        {
            return offsets_overlap(
                main_mem->offset(), other_mem->offset(),
                Primitive::sizes[main_instr->out_type()], Primitive::sizes[other_instr->out_type()]
            );
        }

        return true;
    }

    if (main_instr->out_type() == other_instr->out_type())
    {
        // If both are structs, their offsets need to be equal, not just overlap, for aliasing to be allowed
        if (main_mem->struct_access() && other_mem->struct_access())
            return main_mem->offset() == other_mem->offset();

        return true;
    }

    return false;
}

static bool aliases_call(obj::Str fn_symbol, Context &ctx, bool is_load)
{
    Symbol *symbol = ctx.unit->get_symbol(fn_symbol, ctx.tuid);
    Function *fn = symbol->cast<Function>();

    if (fn->flags() & FnFlag::INTERPOSABLE || fn->linkage() == Linkage::EXTERNAL)
        return true;

    AccessState::Enum state = fn->access_state();

    if (state == AccessState::PURE)
        return false;

    if (is_load && state == AccessState::READS)
        return false;

    return true;
}

struct AliasesHow : NoCreate {
enum Enum {
    NOT,
    ALIASES,
    ATOMIC_ORDER,

    LOAD_OVERLAPS,
    STORE_OVERLAPS,
    RMW_OVERLAPS, // From atomics who modify
};
};

// How to consider aliasing with atomic ordering
struct AtomicAliasing : NoCreate {
enum Enum {
    NONE, // ATOMIC_ORDER is never returned
    UP,   // ATOMIC_ORDER is returned if ordering blocks moving through upwards
    DOWN, // ATOMIC_ORDER is returned if ordering blocks moving through downwards
};
};

static always_inline bool aliases_ordering(AtomicOrder::Enum order, AtomicAliasing::Enum atomic_aliasing)
{
    switch (atomic_aliasing)
    {
    case AtomicAliasing::NONE: return false;
    case AtomicAliasing::UP:   return (order != AtomicOrder::RELAXED && order != AtomicOrder::RELEASE);
    case AtomicAliasing::DOWN: return (order != AtomicOrder::RELAXED && order != AtomicOrder::ACQUIRE);
    }
}

static always_inline AliasesHow::Enum access_aliases_how(Instr *main, MemInfoBase *main_mem, Instr *main_ptr, Instr *other, Context &ctx, AtomicAliasing::Enum atomic_aliasing)
{
    // Variables for `aliases_access()`
    MemInfoBase *access_mem = NULL;
    PtrID access_ptr_id;

    switch (other->kind())
    {

    case InstrKind::LOAD:
    {
        LoadInstr *load = other->cast<LoadInstr>();

        access_mem = load->info_base();
        access_ptr_id = load->ptr()->ptr_id();

        if (load->ptr() == main_ptr)
        {
            if (offsets_overlap(load->offset(), main_mem->offset(), Primitive::sizes[load->out_type()], Primitive::sizes[main->out_type()]))
                return AliasesHow::LOAD_OVERLAPS;

            return AliasesHow::NOT;
        }

        break;
    }
    case InstrKind::ATOMIC_LOAD:
    {
        AtomicLoadInstr *load = other->cast<AtomicLoadInstr>();

        access_mem = load->info_base();
        access_ptr_id = load->ptr()->ptr_id();

        if (aliases_ordering(load->order(), atomic_aliasing))
            return AliasesHow::ATOMIC_ORDER;

        if (load->ptr() == main_ptr)
        {
            if (offsets_overlap(load->offset(), main_mem->offset(), Primitive::sizes[load->out_type()], Primitive::sizes[main->out_type()]))
                return AliasesHow::LOAD_OVERLAPS;

            return AliasesHow::NOT;
        }

        break;
    }

    case InstrKind::STORE:
    {
        StoreInstr *store = other->cast<StoreInstr>();

        access_mem = store->info_base();
        access_ptr_id = store->ptr()->ptr_id();

        if (store->ptr() == main_ptr)
        {
            if (offsets_overlap(store->offset(), main_mem->offset(), Primitive::sizes[store->out_type()], Primitive::sizes[main->out_type()]))
                return AliasesHow::STORE_OVERLAPS;

            return AliasesHow::NOT;
        }

        break;
    }
    case InstrKind::ATOMIC_STORE:
    {
        AtomicStoreInstr *store = other->cast<AtomicStoreInstr>();

        access_mem = store->info_base();
        access_ptr_id = store->ptr()->ptr_id();

        if (aliases_ordering(store->order(), atomic_aliasing))
            return AliasesHow::ATOMIC_ORDER;

        if (store->ptr() == main_ptr)
        {
            if (offsets_overlap(store->offset(), main_mem->offset(), Primitive::sizes[store->out_type()], Primitive::sizes[main->out_type()]))
                return AliasesHow::STORE_OVERLAPS;

            return AliasesHow::NOT;
        }

        break;
    }

    case InstrKind::ATOMIC_OP:
    {
        AtomicOpInstr *op = other->cast<AtomicOpInstr>();

        access_mem = op->info_base();
        access_ptr_id = op->ptr()->ptr_id();

        if (aliases_ordering(op->order(), atomic_aliasing))
            return AliasesHow::ATOMIC_ORDER;

        if (
            op->ptr() == main_ptr &&
            !offsets_overlap(op->offset(), main_mem->offset(), Primitive::sizes[op->out_type()], Primitive::sizes[main->out_type()])
        ) return AliasesHow::NOT;

        break;
    }
    case InstrKind::ATOMIC_CAS:
    {
        AtomicCASInstr *cas = other->cast<AtomicCASInstr>();

        access_mem = cas->info_base();
        access_ptr_id = cas->ptr()->ptr_id();

        if (aliases_ordering(cas->order(), atomic_aliasing))
            return AliasesHow::ATOMIC_ORDER;

        if (
            cas->ptr() == main_ptr &&
            !offsets_overlap(cas->offset(), main_mem->offset(), Primitive::sizes[cas->out_type()], Primitive::sizes[main->out_type()])
        ) return AliasesHow::NOT;

        break;
    }

    case InstrKind::ATOMIC_FENCE:
    {
        AtomicFenceInstr *fence = other->cast<AtomicFenceInstr>();

        if (aliases_ordering(fence->order(), atomic_aliasing))
            return AliasesHow::ATOMIC_ORDER;

        return AliasesHow::NOT;
    }

    case InstrKind::SYMCALL:
    {
        SymbolCallInstr *call = other->cast<SymbolCallInstr>();
        
        if (aliases_call(call->symbol(), ctx, false))
            return AliasesHow::ALIASES;

        return AliasesHow::NOT;
    }
    case InstrKind::PTRCALL: return AliasesHow::ALIASES;

    case InstrKind::ASM:
    {
        if (asm_touches_ptr(other->cast<AsmInstr>()))
            return AliasesHow::ALIASES;

        return AliasesHow::NOT;
    }
    default: assert(0);
    }

    assert(access_mem != NULL); // Only paths that set ACCESS_MEM should reach this
    if (aliases_access(main, other, main_mem, access_mem, main_ptr->ptr_id(), access_ptr_id))
        return AliasesHow::ALIASES;

    return AliasesHow::NOT;
}


static void insert_into_allowance(Instr *ptr, Section *section, Ssize offset, SecAllowanceMap &allowance)
{
    SecAllowance &secalw = allowance[section];
    PtrAllowance &ptralw = secalw[ptr];

    if (!ptralw.find(offset))
        ptralw.push(offset);
}

static bool section_allows_access(Instr *ptr, Ssize offset, Section *section, SecAllowanceMap &allowance)
{
    SecAllowance &secalw = allowance[section];
    PtrAllowance *ptralw = secalw.get(ptr);

    if (!ptralw) return false;
    return ptralw->find(offset);
}


static void aaib_populate_secs(Section *sec, Section *finish, obj::PtrSet<Section *> &sections)
{
    // Early exit if we already inserted this one
    if (!sections.insert(sec))
        return;

    if (sec == finish)
        return;

    const obj::Array<Section *> preds = sec->preceding_sections();

    for (ObjSize i = 0; i < preds.size(); i++)
        aaib_populate_secs(preds[i], finish, sections);
}

// Check if there's an access that aliases with ACCESS in the path from section START through section FINISH.
// Returns NULL if not, otherwise returns the aliasing access.
// This function assumes that section FINISH is a dominator over START.
// The instruction we got ACCESS from must be pulled, as to not collide with it here.
static Instr *aliases_access_in_backedge(Instr *main_instr, MemInfoBase *main_mem, Instr *main_ptr, Section *start, Section *finish, Context &ctx)
{
    obj::PtrSet<Section *> sections;
    aaib_populate_secs(start, finish, sections);

    ObjSize iter = 0;
    obj::PtrKey<Section *> *key;
    while (sections.next(iter, key))
    {
        Section *sec = key->ptr();

        Instr *instr = sec->last_instr(); // This grabs a terminator, so we grab its PREV at start of loop
        while (true)
        {
            if (instr->is_first())
                break;

            instr = instr->prev();

            AliasesHow::Enum aliashow = access_aliases_how(main_instr, main_mem, main_ptr, instr, ctx, AtomicAliasing::NONE);

            if (aliashow == AliasesHow::NOT)
                continue;

            if (aliashow == AliasesHow::STORE_OVERLAPS)
                return instr;

            // Loads don't count, the rest stores (aside AtomicFence, which should only return NOT)
            if (instr->kind() == InstrKind::LOAD || instr->kind() == InstrKind::ATOMIC_LOAD)
                continue;

            return instr;
        }
    }

    return NULL;
}


struct CanHoistHow : NoCreate {
enum Enum {
    NOT,
    CONTINUE_INTO_PRED,   // One predecessor present; can hoist into it
    MULTI_SUCCESSOR_PRED, // One predecessor present with multiple successors to handle; unknown if we can continue
    MULTI_PRED_HOIST,     // Multiple predecessors present; can hoist into all of them
};
};

// RELEVANT_PREDS should have at least enough entries to fit all of CUR_SEC's predecessors.
// RELEVANT_PREDS will hold the predecessors of CUR_SEC to hoist into.
static always_inline CanHoistHow::Enum can_hoist_how(Instr *instr, Instr *ptr, MemInfoBase *mem, Ssize offset, Section **relevant_preds, Size &n_relevant_preds, Section *cur_sec, SecAllowanceMap &allowance, Context &ctx)
{
    const obj::Array<Section *> &preds = cur_sec->preceding_sections();

    const DominanceMap &dominance = ctx.fn->dominance();
    ObjSize cur_rdst = dominance[cur_sec].rdst;

    // Guard for the entry section
    if (preds.size() == 0)
        return CanHoistHow::NOT;


    n_relevant_preds = 0;

    for (ObjSize i = 0; i < preds.size(); i++)
    {
        Section *pred = preds[i];
        ObjSize pred_rdst = dominance[pred].rdst;

        // Check if we dominate PRED
        if (pred_rdst <= cur_rdst)
        {
            // Check if it's a back-edge from something irreducible to CUR_SEC
            if (ctx.fn->intersect(cur_sec, pred) != cur_sec)
                return CanHoistHow::NOT; // If so, be conservative and stop here

            // Otherwise check if there's a store aliasing with ours.
            // No need to check if it can be eliminated; if it can be, it should still be walked and eliminates us instead.
            if (aliases_access_in_backedge(instr, mem, ptr, pred, cur_sec, ctx))
                return CanHoistHow::NOT;
        }
        else {
            // Not a back-edge, so we can hoist into it
            relevant_preds[n_relevant_preds++] = pred;
        }
    }

    if (n_relevant_preds == 1)
    {
        Section *pred = relevant_preds[0];
        Instr *terminator = pred->last_instr();

        // Check if it's a jump, or otherwise is already cleared for hoisting
        if (terminator->kind() == InstrKind::JUMP || section_allows_access(ptr, offset, pred, allowance))
            return CanHoistHow::CONTINUE_INTO_PRED;

        // Otherwise it must be a branch or switch-case
        assert(terminator->kind() == InstrKind::BRANCH || terminator->kind() == InstrKind::SWITCH);

        return CanHoistHow::MULTI_SUCCESSOR_PRED;
    }

    // With more than 1 predecessor, all of them need to simply jump here.
    // Don't check for allowance; code that passes that check shouldn't be created naturally even with labels
    for (Size i = 0; i < n_relevant_preds; i++)
    {
        Section *pred = relevant_preds[i];
        Instr *terminator = pred->last_instr();

        if (terminator->kind() != InstrKind::JUMP)
            return CanHoistHow::NOT;
    }

    return CanHoistHow::MULTI_PRED_HOIST;
}


static StoreInstr *duplicate_store(StoreInstr *orig)
{
    AccessData access_data = {
        .ptr           = orig->ptr(),
        .offset        = orig->offset(),
        .aliases_all   = orig->aliases_all(),
        .ptr_exclusive = orig->ptr_exclusive(),
        .struct_access = orig->struct_access()
    };

    return StoreInstr::create(orig->value(), access_data, orig->is_volatile());
}

static LoadInstr *duplicate_load(LoadInstr *orig)
{
    AccessData access_data = {
        .ptr           = orig->ptr(),
        .offset        = orig->offset(),
        .aliases_all   = orig->aliases_all(),
        .ptr_exclusive = orig->ptr_exclusive(),
        .struct_access = orig->struct_access()
    };

    return LoadInstr::create(orig->out_type(), access_data, orig->is_volatile());
}

static bool stores_equal(StoreInstr *a, StoreInstr *b)
{
    return (
        a->ptr()           == b->ptr()           &&
        a->offset()        == b->offset()        &&
        a->aliases_all()   == b->aliases_all()   &&
        a->ptr_exclusive() == b->ptr_exclusive() &&
        a->struct_access() == b->struct_access() &&

        a->value()         == b->value()         &&
        a->is_volatile()   == b->is_volatile()
    );
}

static bool loads_equal(LoadInstr *a, LoadInstr *b)
{
    return (
        a->ptr()           == b->ptr()           &&
        a->offset()        == b->offset()        &&
        a->aliases_all()   == b->aliases_all()   &&
        a->ptr_exclusive() == b->ptr_exclusive() &&
        a->struct_access() == b->struct_access() &&

        a->out_type()      == b->out_type()      &&
        a->is_volatile()   == b->is_volatile()
    );
}


// Returns the StoreInstr that matches, or NULL if we aliased the access or it wasn't present
static StoreInstr *pred_successor_has_store(StoreInstr *instr, Section *succ, Context &ctx)
{
    Instr *access = succ->first_instr();

    while (true)
    {
        bool matched_cmp;
        access = walk_to_access(access, NULL, NULL, matched_cmp, true);

        if (!access)
            break;

        if (access->kind() == InstrKind::STORE)
        {
            StoreInstr *store = access->cast<StoreInstr>();
            if (stores_equal(instr, store))
                return store;
        }

        if (access_aliases_how(instr, instr->info_base(), instr->ptr(), access, ctx, AtomicAliasing::UP) != AliasesHow::NOT)
            return NULL;

        access = access->next();
    }

    return NULL;
}

// Expects an INSTR that we can freely place
static void store_walk(StoreInstr *instr, Instr *walk_start, SecAllowanceMap &allowance, Context &ctx)
{
    // Insert our instruction in the allowance map
    insert_into_allowance(instr->ptr(), walk_start->section(), instr->offset(), allowance);

    /* Walk the instruction down the section */

    Instr *last = walk_start; // LAST must end on the instruction to place INSTR above if we reach after the walk loop
    while (true)
    {
        bool matched_cmp;
        Instr *access = walk_to_access(last, instr->ptr(), instr->value(), matched_cmp, false);
        if (!access) break;

        last = access;

        if (matched_cmp)
        {
            access->place_next(instr);
            return;
        }


        AliasesHow::Enum aliashow = access_aliases_how(instr, instr->info_base(), instr->ptr(), access, ctx, AtomicAliasing::UP);

        if (aliashow == AliasesHow::NOT)
            continue;

        if (aliashow == AliasesHow::STORE_OVERLAPS && access->kind() == InstrKind::STORE)
        {
            StoreInstr *store = access->cast<StoreInstr>();

            // Check if the store is entirely covered by ours
            if (
                (instr->offset() <= store->offset()) &&
                (instr->offset() + Primitive::sizes[instr->out_type()] >= store->offset() + Primitive::sizes[store->out_type()])
            ) {
                // We cover the access, so remove it
                access->pop();
                continue;
            }

            // Otherwise we can't progress further
        }

        access->place_next(instr);
        return;
    }


    /* Check how we can continue depending on predecessors */

    Section *cur_sec = walk_start->section();

    Section *relevant_preds[cur_sec->preceding_sections().size()];
    Size n_relevant_preds;

    CanHoistHow::Enum hoisthow = can_hoist_how(instr, instr->ptr(), instr->info_base(), instr->offset(), relevant_preds, n_relevant_preds, cur_sec, allowance, ctx);

    switch (hoisthow)
    {
    case CanHoistHow::NOT:
    {
        last->place_next(instr);
        return;
    }

    case CanHoistHow::CONTINUE_INTO_PRED:
        return store_walk(instr, relevant_preds[0]->last_instr(), allowance, ctx);

    case CanHoistHow::MULTI_PRED_HOIST:
    {
        // For all but the last, duplicate the store to give it a fresh one to place
        for (Size i = 0; i < n_relevant_preds - 1; i++)
        {
            StoreInstr *duplicate = duplicate_store(instr);
            store_walk(duplicate, relevant_preds[i]->last_instr(), allowance, ctx);
        }

        store_walk(instr, relevant_preds[n_relevant_preds - 1]->last_instr(), allowance, ctx);
        return;
    }

    case CanHoistHow::MULTI_SUCCESSOR_PRED:
    {
        Section *pred = relevant_preds[0];
        obj::Array<Section *> succs = pred->succeeding_sections();

        // The stores to deduplicate
        StoreInstr *stores[succs.size() - 1];

        // Check if all predecessor's successors also do this exact store in a non-aliasing manner
        ObjSize stores_iter = 0;
        for (ObjSize i = 0; i < succs.size(); i++)
        {
            Section *succ = succs[i];
            
            if (succ == cur_sec)
                continue;

            StoreInstr *store = pred_successor_has_store(instr, succs[i], ctx);

            if (!store)
            {
                last->place_next(instr);
                return;
            }

            stores[stores_iter++] = store;
        }

        for (ObjSize i = 0; i < succs.size() - 1; i++)
            stores[i]->pop();

        store_walk(instr, cur_sec->last_instr(), allowance, ctx);
        return;
    }
    }
}


// Expects an INSTR that we can freely place
static void load_walk(LoadInstr *instr, Instr *walk_start, SecAllowanceMap &allowance, Context &ctx)
{
    // Insert our instruction into the allowance map
    insert_into_allowance(instr->ptr(), walk_start->section(), instr->offset(), allowance);

    /* Walk the instruction down the section */

    Instr *last = walk_start; // LAST must end on the instruction to place INSTR above if we reach after the walk loop
    while (true)
    {
        bool matched_cmp;
        Instr *access = walk_to_access(last, instr->ptr(), NULL, matched_cmp, false);
        if (!access) break;

        last = access;

        if (matched_cmp)
        {
            access->place_next(instr);
            return;
        }


        AliasesHow::Enum aliashow = access_aliases_how(instr, instr->info_base(), instr->ptr(), access, ctx, AtomicAliasing::UP);

        if (aliashow == AliasesHow::NOT)
            continue;

        if (aliashow == AliasesHow::LOAD_OVERLAPS)
        {
            // Only forward if both are either an integer or a float
            if (Primitive::is_int(instr->out_type()) == Primitive::is_int(access->out_type()))
                continue;

            MemInfoBase *mem;
            bool access_is_immutable;
            if (access->kind() == InstrKind::LOAD)
            {
                LoadInstr *load = access->cast<LoadInstr>();
                mem = load->info_base();
                access_is_immutable = load->is_volatile();
            }
            else
            {
                mem = access->cast<AtomicLoadInstr>()->info_base();
                access_is_immutable = true;
            }

            // No need to optimize loads at different offsets; a shift introduces more latency than just keeping the load
            if (instr->offset() != mem->offset())
                continue;

            // If out types are equal, replacing is straightforward
            if (instr->out_type() == access->out_type())
            {
                instr->replace_uses_with(access);
                return;
            }

            // Check which to keep and which to replace depending on which is larger
            Instr *keep, *replace;
            if (Primitive::sizes[instr->out_type()] < Primitive::sizes[access->out_type()])
            {
                keep = access;
                replace = instr;
            }
            else
            {
                // Can't replace the access if it's immutable 
                if (access_is_immutable)
                    continue;

                keep = instr;
                replace = access;
            }

            // Place INSTR so that both are in the right place, and we can use KEEP/REPLACE going further
            access->place_next(instr);

            // Signedness set to false but is irrelevant; this operation shrinks, no sign extension can occur
            TransformInstr *transform = TransformInstr::create(replace->out_type(), keep, false);

            keep->place_next(transform);
            replace->replace_uses_with(keep);

            return;
        }

        // TODO: allow store-to-load forwarding for atomic stores that returned ATOMIC_ORDER instead of STORE_OVERLAPS
        if (aliashow == AliasesHow::STORE_OVERLAPS)
        {
            // Only forward if both are either an integer or a float
            if (Primitive::is_int(instr->out_type()) == Primitive::is_int(access->out_type()))
                continue;

            MemInfoBase *mem;
            Instr *store_value;
            if (access->kind() == InstrKind::STORE)
            {
                StoreInstr *store = access->cast<StoreInstr>();
                mem = store->info_base();
                store_value = store->value();
            }
            else
            {
                AtomicStoreInstr *store = access->cast<AtomicStoreInstr>();
                mem = store->info_base();
                store_value = store->value();
            }

            // Check if our load fits within the store, rather than just overlapping
            if (!access_sits_within(mem->offset(), instr->offset(), Primitive::sizes[access->out_type()], Primitive::sizes[instr->out_type()]))
                continue;

            Instr *forward_value = store_value;

            // Check if we need to shift the store value.
            // We can only do this for integers, not for floats.
            if (instr->offset() != mem->offset())
            {
                if (Primitive::is_fp(instr->out_type()))
                    continue;

                u32 shift_value = (instr->offset() - mem->offset()) * 8;
                ImmediateInstr *shift_imm = ImmediateInstr::create(Primitive::i32, shift_value);
                IntInstr *shifted_value = IntInstr::create(access->out_type(), forward_value, shift_imm, IntOp::SHR, IntOp::NOFLAGS);

                forward_value->place_next(shift_imm);
                shift_imm->place_next(shifted_value);
                forward_value = shifted_value;
            }

            // Check if we need to transform the value to a smaller size
            if (instr->out_type() != access->out_type())
            {
                // Signedness set to false but irrelevant
                TransformInstr *transformed = TransformInstr::create(instr->out_type(), forward_value, false);
                
                forward_value->place_next(transformed);
                forward_value = transformed;
            }

            instr->replace_uses_with(forward_value);
            return;
        }

        access->place_next(instr);
        return;
    }


    /* Check how we can continue depending on predecessors */

    Section *cur_sec = walk_start->section();

    Section *relevant_preds[cur_sec->preceding_sections().size()];
    Size n_relevant_preds;

    CanHoistHow::Enum hoisthow = can_hoist_how(instr, instr->ptr(), instr->info_base(), instr->offset(), relevant_preds, n_relevant_preds, cur_sec, allowance, ctx);

    switch (hoisthow)
    {
    case CanHoistHow::NOT:
    {
        last->place_next(instr);
        return;
    }

    case CanHoistHow::CONTINUE_INTO_PRED:
        return load_walk(instr, relevant_preds[0]->last_instr(), allowance, ctx);

    case CanHoistHow::MULTI_PRED_HOIST:
    {
        // TODO
    }

    case CanHoistHow::MULTI_SUCCESSOR_PRED:
    {
        // TODO
    }
    }
}



/*****************\
    Main Method
\*****************/

void memopt(Context &ctx)
{
    const obj::Array<Section *> &postorder = ctx.fn->postorder();

    SecAllowanceMap allowance;

    for (ObjSize i = postorder.size(); i > 0; i--)
    {
        Section *sec = postorder[i - 1];
        Instr *instr = sec->first_instr();

        do {
            if (instr->kind() == InstrKind::STORE)
            {
                StoreInstr *store = instr->cast<StoreInstr>();

                if (store->is_volatile())
                    goto next_iter;

                Instr *start_at = instr->next(); // Start at NEXT because START_AT itself isn't checked
                instr->pull();
                store_walk(store, start_at, allowance, ctx);
            }
            else if (instr->kind() == InstrKind::LOAD)
            {
                LoadInstr *load = instr->cast<LoadInstr>();

                if (load->is_volatile())
                    goto next_iter;

                Instr *start_at = instr->next();
                instr->pull();
                load_walk(load, start_at, allowance, ctx);
            }
            
            next_iter:
            instr = instr->next();
        } while (instr);
    }

    return;
}


}

