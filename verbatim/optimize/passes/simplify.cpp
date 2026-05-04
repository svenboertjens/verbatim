#include "shared/primitive.hpp"
#include "shared/types.hpp"
#include "shared/defs.hpp"
#include "shared/ops.hpp"

#include "tools/tools.hpp"

#include "ir/symbols.hpp"
#include "ir/instr.hpp"

#include "context.hpp"

#include <cstring>

namespace opt {

using namespace ir;


/*******************\
    Deduplication
\*******************/

/* Basic */

static bool cmp_int_op(IntInstr *a, IntInstr *b, Context &ctx)
{
    (void)ctx;

    // Whether operands may be reordered
    static bool reorderable[IntOp::MAX_VALUE + 1] = {
        [0 ... IntOp::MAX_VALUE] = false,

        [IntOp::ADD] = true, [IntOp::MUL] = true,

        [IntOp::AND] = true, [IntOp::OR ] = true,
        [IntOp::XOR] = true,

        [IntOp::EQ]  = true, [IntOp::NEQ] = true,
    };

    bool may_reorder = reorderable[a->op()];

    if (a->op() != b->op())
        return false;

    if (
        a->left()  == b->left()  &&
        a->right() == b->right()
    ) return true;
    if (
        may_reorder &&
        a->left() == b->right() &&
        a->right() == b->left()
    ) return true;

    return false;
}

static bool cmp_fp_op(FpInstr *a, FpInstr *b, Context &ctx)
{
    (void)ctx;

    // Whether operands may be reordered
    static bool reorderable[FpOp::MAX_VALUE + 1] = {
        [0 ... FpOp::MAX_VALUE] = false,

        [FpOp::ADD] = true, [FpOp::MUL] = true,
        [FpOp::EQ]  = true, [FpOp::NEQ] = true,
    };

    bool may_reorder = reorderable[a->op()];

    if (a->op() != b->op())
        return false;

    if (
        a->left()  == b->left()  &&
        a->right() == b->right()
    ) return true;
    if (
        may_reorder &&
        a->left()  == b->right() &&
        a->right() == b->left()
    ) return true;

    return false;
}

static bool cmp_transform(TransformInstr *a, TransformInstr *b, Context &ctx)
{
    (void)ctx;

    return (
        a->kind() == b->kind()   &&
        a->out_type() == b->out_type() &&
        a->in() == b->in()
    );
}

static bool cmp_symcall(SymbolCallInstr *a, SymbolCallInstr *b, Context &ctx)
{
    Function *fn = (Function *)ctx.unit->get_symbol(a->symbol(), ctx.tuid);

    return (
        a->symbol() == b->symbol() &&
        (fn->flags() & FnFlag::PURE) // Function must be pure to be eligible for deduplication
    );
}


/* Function table */

typedef bool (*CmpInstrFn)(Instr *a, Instr *b, Context &ctx);

static bool (*cmp_instr_fns[InstrKind::MAX_VALUE + 1])(Instr *a, Instr *b, Context &ctx) = {
    [0 ... InstrKind::MAX_VALUE] = NULL,

    [InstrKind::INT_OP]    = (CmpInstrFn)cmp_int_op,
    [InstrKind::FP_OP]     = (CmpInstrFn)cmp_fp_op,
    [InstrKind::TRANSFORM] = (CmpInstrFn)cmp_transform,
    [InstrKind::SYMCALL]   = (CmpInstrFn)cmp_symcall,
};


/* Dedupe instr */

static bool dedupe_instr(Instr *instr, Context &ctx)
{
    bool deduped = false;
    obj::Array<Instr *> users = instr->copy_users();

    for (ObjSize i = 0; i < users.size(); i++)
    {
        Instr *cur_use = users[i];
        if (!cur_use) continue;

        for (ObjSize j = i + 1; j < users.size(); j++)
        {
            Instr *next_use = users[j];
            if (!next_use) continue;

            if (
                cur_use->kind() == next_use->kind() &&
                cmp_instr_fns[cur_use->kind()](cur_use, next_use, ctx)
            ) {
                Instr *to_replace = next_use, *replace_with = cur_use;

                Section *intersect = ctx.fn->intersect(cur_use->section(), next_use->section());

                bool cur_on_intersect = intersect == cur_use->section();
                bool nxt_on_intersect = intersect == next_use->section();
                if (!cur_on_intersect && !nxt_on_intersect)
                {
                    // Move REPLACE_WITH to the intersect, neither are in that section
                    intersect->top_instr()->place_below(replace_with);
                }
                else if (cur_on_intersect && nxt_on_intersect)
                {
                    // Need to figure out which is earlier
                    Instr *iter = intersect->bottom_instr();
                    while (iter)
                    {
                        if (iter == cur_use)
                            break;

                        if (iter == next_use)
                        {
                            // Need to flip these, NEXT_USE is earlier
                            to_replace = cur_use;
                            replace_with = next_use;
                            break;
                        }

                        iter = iter->next();
                    }

                    assert(iter); // Must not have ended with NULL
                }
                else if (nxt_on_intersect)
                {
                    // Need to flip these, NEXT_USE is earlier
                    to_replace = cur_use;
                    replace_with = next_use;
                }

                to_replace->replace_uses_with(replace_with);

                users[j] = NULL; // Set to NULL to avoid checking it for dedupes
                deduped = true;
            }
        }
    }

    return deduped;
}


/********************\
    Simplification
\********************/

/* Simplify int op */

template<typename Int>
static bool simplify_int_constfold(IntInstr *instr, ImmediateInstr *left, ImmediateInstr *right)
{
    Int leftval = left->imm<Int>();
    Int rightval = right->imm<Int>();
    Int result;

    bool comparison;
    switch (instr->op())
    {
    case IntOp::ADD: { result = leftval + rightval;  comparison = false; break; }
    case IntOp::SUB: { result = leftval - rightval;  comparison = false; break; }
    case IntOp::MUL: { result = leftval * rightval;  comparison = false; break; }
    case IntOp::DIV: { result = leftval / rightval;  comparison = false; break; }
    case IntOp::MOD: { result = leftval % rightval;  comparison = false; break; }

    case IntOp::AND: { result = leftval &  rightval; comparison = false; break; }
    case IntOp::OR:  { result = leftval |  rightval; comparison = false; break; }
    case IntOp::XOR: { result = leftval ^  rightval; comparison = false; break; }
    case IntOp::SHL: { result = leftval << rightval; comparison = false; break; }
    case IntOp::SHR: { result = leftval >> rightval; comparison = false; break; }

    case IntOp::EQ:  { result = leftval == rightval; comparison = true;  break; }
    case IntOp::NEQ: { result = leftval != rightval; comparison = true;  break; }
    case IntOp::GT:  { result = leftval > rightval;  comparison = true;  break; }
    case IntOp::GE:  { result = leftval >= rightval; comparison = true;  break; }
    case IntOp::LT:  { result = leftval < rightval;  comparison = true;  break; }
    case IntOp::LE:  { result = leftval <= rightval; comparison = true;  break; }
    }

    ImmediateInstr *imm_instr;
    if (comparison)
        imm_instr = ImmediateInstr::create(Primitive::i1, result == 1);
    else
        imm_instr = ImmediateInstr::create(instr->out_type(), result);

    instr->place_above(imm_instr);
    instr->replace_uses_with(imm_instr);

    return true;
}

template<typename Int>
static bool simplify_int_comparison(IntInstr *op, Instr *left, Instr *right)
{
    // TODO

    return false;
}

template<typename Int>
static bool simplify_int_mul_oneimm(IntInstr *instr, Instr *left, Int immval)
{
    // Check if power of 2, optionally plus/minus 1
    bool pow2        = tools::is_pow2(immval    );
    bool pow2_plus1  = tools::is_pow2(immval - 1);
    bool pow2_minus1 = tools::is_pow2(immval + 1);

    if (!pow2 && !pow2_plus1 && !pow2_minus1)
        return false;

    // x * imm ->
    // pow2:        x << nth_power(imm)
    // pow2_plus1:  (x << nth_power(imm - 1)) + x
    // pow2_minus1: (x << nth_power(imm + 1)) - x

    Int pow2_val = (
        pow2       ? immval     :
        pow2_plus1 ? immval - 1 :
                     immval + 1
    );
    u32 shift = tools::ctz(pow2_val);

    // imm = shift_amount
    ImmediateInstr *imm_instr = ImmediateInstr::create(Primitive::i32, shift);

    // shifted = x << imm
    IntInstr *shift_instr = IntInstr::create(
        instr->out_type(),
        left, imm_instr, IntOp::SHL, IntOp::NOFLAGS
    );

    instr->place_above(imm_instr);
    imm_instr->place_above(shift_instr);
    
    Instr *result = shift_instr;
    if (pow2_plus1 || pow2_minus1)
    {
        // shifted +/- x
        IntInstr *correction_instr = IntInstr::create(
            instr->out_type(),
            shift_instr, left, pow2_plus1 ? IntOp::ADD : IntOp::SUB, IntOp::NOFLAGS
        );

        shift_instr->place_above(correction_instr);
        result = correction_instr;
    }

    instr->replace_uses_with(result);
    return true;
}

template<typename Int>
static bool simplify_int_div_oneimm(IntInstr *instr, Instr *left, Int immval)
{
    if (tools::is_pow2(immval))
    {
        // x * imm ->
        // unsigned: x >> nth_power(imm)
        // signed:   (x + (
        //             ((x >> (nbits(Int) - 1)) & (imm - 1))
        //           )) >> nth_power(imm)

        u32 shift = tools::ctz(immval);

        // Apply correction for signed integers
        IntInstr *shiftable = instr;
        if (instr->flags() & IntOp::SIGNED)
        {
            // nbits(Int) - 1
            ImmediateInstr *shift_imm = ImmediateInstr::create(Primitive::i32, sizeof(Int) * 8 - 1);
            instr->place_above(shift_imm);

            // imm - 1
            ImmediateInstr *mask_imm = ImmediateInstr::create(instr->out_type(), immval - 1);
            shift_imm->place_above(mask_imm);


            // x >> shift_imm
            IntInstr *shifted_x = IntInstr::create(
                instr->out_type(),
                instr, shift_imm, IntOp::SHR, instr->flags() & IntOp::SIGNED // Preserve signedness, SHR differs depending on it
            );
            mask_imm->place_above(shifted_x);

            // shifted_x & mask_imm
            IntInstr *masked_x = IntInstr::create(
                instr->out_type(),
                shifted_x, mask_imm, IntOp::AND, IntOp::NOFLAGS
            );
            shifted_x->place_above(masked_x);


            // x + masked_x
            shiftable = IntInstr::create(
                instr->out_type(),
                left, masked_x, IntOp::ADD, IntOp::NOFLAGS
            );
            masked_x->place_above(shiftable);
        }

        // imm = shift_amount
        ImmediateInstr *imm_instr = ImmediateInstr::create(Primitive::i32, shift);

        IntInstr *shift_instr = IntInstr::create(
            instr->out_type(),
            shiftable, imm_instr, IntOp::SHR, instr->flags() & IntOp::SIGNED // Preserve signedness, SHR differs depending on it
        );

        instr->place_above(imm_instr);
        imm_instr->place_above(shift_instr);
        instr->replace_uses_with(shift_instr);

        return true;
    }

    // Otherwise, turn into a mul+shift
    // TODO

    return false;
}

template<typename Int>
static bool simplify_int_mod_oneimm(IntInstr *instr, Instr *left, Int immval)
{
    // TODO

    return false;
}

// Doesn't expect comparisons
template<typename Int>
static bool simplify_int_oneimm(IntInstr *instr, Instr *left, Instr *right, bool left_is_imm)
{
    ImmediateInstr *imm_instr = left_is_imm ? (ImmediateInstr *)left : (ImmediateInstr *)right;
    Instr *nonimm_instr = left_is_imm ? right : left;
    Int immval = imm_instr->imm<Int>();

    if (immval == (Int)0)
    {
        switch (instr->op())
        {
        case IntOp::ADD:
        case IntOp::SUB:
        case IntOp::OR:
        case IntOp::XOR:  {
            instr->replace_uses_with(nonimm_instr); // the non-immediate doesn't change
            return true;
        }
        case IntOp::AND:
        case IntOp::MUL: {
            instr->replace_uses_with(imm_instr); // always turns into zero; reuses the zero immediate
            return true;
        }
        // TODO: What to do with division by zero?
        default:
        }
    }
    else if (immval == ~(Int)0)
    {
        switch (instr->op())
        {
        case IntOp::AND: {
            instr->replace_uses_with(nonimm_instr); // the non-immediate doesn't change
            return true;
        }
        case IntOp::OR:
        {
            ImmediateInstr *all_ones_instr = ImmediateInstr::create(instr->out_type(), (Int)-1);

            instr->place_above(all_ones_instr);
            instr->replace_uses_with(all_ones_instr);

            return true;
        }
        default:
        }
    }
    
    
    if (left_is_imm)
        return false;

    // Mul/div/mod optimizations not supported for 128-bit (yet)
    if constexpr (sizeof(Int) <= sizeof(u64))
    {
        switch (instr->op())
        {
        case IntOp::MUL: return simplify_int_mul_oneimm(instr, left, immval);
        case IntOp::DIV: return simplify_int_div_oneimm(instr, left, immval);
        case IntOp::MOD: return simplify_int_mod_oneimm(instr, left, immval);
        default:
        }
    }

    return false;
}

static bool int_op_is_comparison[IntOp::MAX_VALUE + 1] {
    [IntOp::EQ] = true, [IntOp::NEQ] = true,
    [IntOp::GE] = true, [IntOp::GT ] = true,
    [IntOp::LE] = true, [IntOp::LT ] = true,
};

template<typename Int>
static bool simplify_int_op_t(IntInstr *instr, Instr *left, Instr *right)
{
    bool left_is_imm  = left->kind()  == InstrKind::IMMEDIATE;
    bool right_is_imm = right->kind() == InstrKind::IMMEDIATE;

    if (left_is_imm && right_is_imm)
        return simplify_int_constfold<Int>(instr, (ImmediateInstr *)left, (ImmediateInstr *)right);

    else if (int_op_is_comparison[instr->op()])
        return simplify_int_comparison<Int>(instr, left, right);

    else if (left_is_imm || right_is_imm)
        return simplify_int_oneimm<Int>(instr, left, right, left_is_imm);

    return false;
}

static bool simplify_int_op(IntInstr *instr, Context &ctx)
{
    Instr *left = instr->left();
    Instr *right = instr->right();

    switch (left->out_type()) // Not OP's type, since it might be i1 when left/right are different
    {
    case Primitive::i1:
    case Primitive::i8:   return simplify_int_op_t<u8  >(instr, left, right);
    case Primitive::i16:  return simplify_int_op_t<u16 >(instr, left, right);
    case Primitive::i32:  return simplify_int_op_t<u32 >(instr, left, right);
    case Primitive::i64:  return simplify_int_op_t<u64 >(instr, left, right);
    case Primitive::i128: return simplify_int_op_t<u128>(instr, left, right);
    default: assert(0);
    }
    return false;
}


/* Simplify fp op */

template<typename Float>
static bool simplify_fp_constfold(FpInstr *instr, ImmediateInstr *left, ImmediateInstr *right)
{
    Float leftval = left->imm<Float>();
    Float rightval = right->imm<Float>();
    Float result;

    bool comparison;
    switch (instr->op())
    {
    case FpOp::ADD: { result = leftval + rightval;  comparison = false; break; }
    case FpOp::SUB: { result = leftval - rightval;  comparison = false; break; }
    case FpOp::MUL: { result = leftval * rightval;  comparison = false; break; }
    case FpOp::DIV: { result = leftval / rightval;  comparison = false; break; }

    case FpOp::EQ:  { result = leftval == rightval; comparison = true;  break; }
    case FpOp::NEQ: { result = leftval != rightval; comparison = true;  break; }
    case FpOp::GT:  { result = leftval > rightval;  comparison = true;  break; }
    case FpOp::GE:  { result = leftval >= rightval; comparison = true;  break; }
    case FpOp::LT:  { result = leftval < rightval;  comparison = true;  break; }
    case FpOp::LE:  { result = leftval <= rightval; comparison = true;  break; }
    }

    ImmediateInstr *imm_instr;
    if (comparison)
        imm_instr = ImmediateInstr::create(Primitive::i1, result == (Float)1.0);
    else
        imm_instr = ImmediateInstr::create(instr->out_type(), result);

    instr->place_above(imm_instr);
    instr->replace_uses_with(imm_instr);

    return true;
}

template<typename Float>
static bool simplify_fp_oneimm(FpInstr *instr, Instr *left, Instr *right, Context &ctx)
{
    if (!ctx.cfg->opt.fast_fp)
        return false;

    // TODO

    return false;
}

template<typename Float>
static bool simplify_fp_op_t(FpInstr *instr, Instr *left, Instr *right, Context &ctx)
{
    bool left_is_imm  = left->kind()  == InstrKind::IMMEDIATE;
    bool right_is_imm = right->kind() == InstrKind::IMMEDIATE;

    if (left_is_imm && right_is_imm)
        return simplify_fp_constfold<Float>(instr, (ImmediateInstr *)left, (ImmediateInstr *)right);

    else if (left_is_imm || right_is_imm)
        return simplify_fp_oneimm<Float>(instr, left, right, ctx);

    return false;
}

static bool simplify_fp_op(FpInstr *instr, Context &ctx)
{
    Instr *left  = instr->left();
    Instr *right = instr->right();

    switch (instr->out_type())
    {
    case Primitive::f16:  return simplify_fp_op_t<f16 >(instr, left, right, ctx);
    case Primitive::f32:  return simplify_fp_op_t<f32 >(instr, left, right, ctx);
    case Primitive::f64:  return simplify_fp_op_t<f64 >(instr, left, right, ctx);
    case Primitive::f128: return simplify_fp_op_t<f128>(instr, left, right, ctx);
    default: assert(0);
    }

    return false;
}




/* Function table */

typedef bool (*SimplifyInstrFn)(Instr *instr, Context &ctx);

static bool (*simplify_instr_fns[InstrKind::MAX_VALUE + 1])(Instr *instr, Context &ctx) = {
    [0 ... InstrKind::MAX_VALUE] = NULL,

    [InstrKind::INT_OP] = (SimplifyInstrFn)simplify_int_op,
    [InstrKind::FP_OP ] = (SimplifyInstrFn)simplify_fp_op,
};


/* Simplify instr */

static bool simplify_instr(Instr *instr, Context &ctx)
{
    auto simplify_fn = simplify_instr_fns[instr->kind()];
    return simplify_fn && simplify_fn(instr, ctx);
}


/*****************\
    Main Method
\*****************/

void deduplicate(Context &ctx)
{
    const obj::LinkList<Section> &sections = ctx.fn->sections();

    bool progressed;
    do {
        progressed = false;
        
        Section *sec = sections.bottom();
        while (sec)
        {
            // Per-instr
            Instr *next = sec->bottom_instr();
            while (next)
            {
                Instr *instr = next;
                next = next->next();

                // Check if unused
                if (instr->nusers() == 0)
                {
                    instr->pop();
                    continue;
                }

                progressed |= dedupe_instr(instr, ctx);
                progressed |= simplify_instr(instr, ctx);
            }

            sec = sec->next();
        }
    } while (progressed);
}


}

