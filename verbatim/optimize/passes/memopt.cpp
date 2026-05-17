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
static always_inline Instr *walk_to_access(Instr *last, Instr *cmp1, Instr *cmp2, bool &matched_cmp)
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

static bool asm_touches_ptr(AsmInstr *assembly) {
    return assembly->taints_memory();
}

static bool offsets_overlap(Ssize off1, Ssize off2, Ssize tsize1, Ssize tsize2) {
    return off1 < off2 + tsize2 && off2 < off1 + tsize1;
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
};
};

static always_inline AliasesHow::Enum access_aliases_how(Instr *main, MemInfoBase *main_mem, Instr *main_ptr, Instr *other, Context &ctx, bool consider_atomic_order)
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
            // Loads that overlap are a boundary
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

        if (load->order() != AtomicOrder::RELAXED && load->order() != AtomicOrder::RELEASE && consider_atomic_order)
            return AliasesHow::ATOMIC_ORDER;

        if (load->ptr() == main_ptr)
        {
            // Loads that overlap are a boundary
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

        if (store->order() != AtomicOrder::RELAXED && store->order() != AtomicOrder::RELEASE && consider_atomic_order)
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

        if (op->order() != AtomicOrder::RELAXED && op->order() != AtomicOrder::RELEASE && consider_atomic_order)
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

        if (cas->order() != AtomicOrder::RELAXED && cas->order() != AtomicOrder::RELEASE && consider_atomic_order)
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

        // Relaxed is not applicable for a fence, only need to check if the fence releases, all others block us
        if (fence->order() != AtomicOrder::RELEASE && consider_atomic_order)
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

static bool section_allows_access(Instr *ptr, Section *section, Ssize offset, SecAllowanceMap &allowance)
{
    SecAllowance &secalw = allowance[section];
    PtrAllowance *ptralw = secalw.get(ptr);

    if (!ptralw) return false;
    return ptralw->find(offset);
}


static void aaib_populate_secs(Section *sec, Section *finish, obj::PtrSet<Section *> &sections)
{
    sections.insert(sec);

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

        Instr *instr = sec->last_instr(); // Start is a terminator, so begin the loop with getting prev
        while (true)
        {
            if (instr->is_first())
                break;

            instr = instr->prev();

            AliasesHow::Enum aliashow = access_aliases_how(main_instr, main_mem, main_ptr, instr, ctx, false);

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


// Returns the instruction we ended at.
// Sets BLOCKED to true if there wasn't a boundary stopping us from continuing into a further section.
static Instr *store_walk(StoreInstr *instr, Instr *walk_start, SecAllowanceMap &allowance, Context &ctx, bool &blocked)
{
    blocked = true;

    /* Insert our instruction in the allowance map */
    insert_into_allowance(instr->ptr(), instr->section(), instr->offset(), allowance);

    /* Walk the instruction down the section */

    Instr *last = walk_start; // LAST must end on the instruction to place INSTR above if we reach after the walk loop
    while (true)
    {
        bool matched_cmp;
        Instr *access = walk_to_access(last, instr->ptr(), instr->value(), matched_cmp);
        if (!access) break;

        last = access;

        if (matched_cmp)
            return access; // Can't walk further, we hit our ptr or value


        AliasesHow::Enum aliashow = access_aliases_how(instr, instr->info_base(), instr->ptr(), access, ctx, true);

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

        return access;
    }


    /* Check how we can continue depending on predecessors */

    Section *cur_sec = walk_start->section();
    const obj::Array<Section *> &preds = cur_sec->preceding_sections();

    const DominanceMap &dominance = ctx.fn->dominance();
    ObjSize cur_rdst = dominance[cur_sec].rdst;

    // Guard for the entry section
    if (preds.size() == 0)
        return last;


    Section *intersect = cur_sec;
    Section *cont_preds[preds.size()]; // Predecessors to continue with (back-edges are excluded from here)
    Size n_cont_preds = 0;

    for (ObjSize i = 0; i < preds.size(); i++)
    {
        Section *pred = preds[i];
        ObjSize pred_rdst = dominance[pred].rdst;

        // Check if we dominate PRED
        if (pred_rdst <= cur_rdst)
        {
            // Check if it's a back-edge from something irreducible to CUR_SEC
            if (ctx.fn->intersect(cur_sec, pred) != cur_sec)
                return last; // If so, be conservative and stop here

            // Otherwise check if there's a store aliasing with ours.
            // No need to check if it can be eliminated; if it can be, it should still be walked and eliminates us instead.
            if (aliases_access_in_backedge(instr, instr->info_base(), instr->ptr(), pred, cur_sec, ctx))
                return last;
        }
        else {
            // Not a back-edge, so we can hoist into it
            cont_preds[n_cont_preds++] = pred;
        }
    }

    // LEFT OFF HERE
}


static Instr *load_walk(LoadInstr *instr, Instr *walk_start, SecAllowanceMap &allowance, Context &ctx, bool &blocked)
{
    // TODO
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
        Section *sec = postorder[i];
        Instr *instr = sec->first_instr();

        do {
            if (instr->kind() == InstrKind::STORE || instr->kind() == InstrKind::STORE)
            {
                bool blocked;
                Instr *ends_at;

                // NEXT must be valid as this isn't a terminator, and PREV isn't checked by the walks
                Instr *start_at = instr->next();

                // To not collide with ourselves during the walk
                instr->pull();

                if (instr->kind() == InstrKind::STORE)
                    ends_at = store_walk(instr->cast<StoreInstr>(), start_at, allowance, ctx, blocked);
                else
                    ends_at = load_walk(instr->cast<LoadInstr>(), start_at, allowance, ctx, blocked);
                
                ends_at->place_next(instr); // Place, not move, since we pulled the instr
            }
            
        } while (instr);
    }

    return;
}


}

