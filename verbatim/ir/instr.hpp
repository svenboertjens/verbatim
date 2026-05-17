#pragma once

#include "tools/structs.hpp"

#include "shared/primitive.hpp"
#include "shared/defs.hpp"
#include "shared/ops.hpp"

#include "obj/rawstr.hpp"
#include "obj/array.hpp"
#include "obj/link.hpp"
#include "obj/str.hpp"

#include "types.hpp"
#include "defs.hpp"

#include <cstddef>
#include <cassert>
#include <utility>
#include <new>


// TODO: Variadic function args


/* Also contains `Section` */

namespace ir {

// Forward decls
struct Instr;
struct SectionBase;
using  Section = obj::Link<SectionBase>;


/***************\
     Section
\***************/

struct SectionEnd : NoCreate {
enum Enum {
    JUMP,
    BRANCH,
    SWITCH,
    RETURN,
};
};

struct SectionBase : NoCopyMove {
private:

    struct {
        Instr *first = NULL; // If the first instr is a JunctionInstr, it must remain so
        Instr *last  = NULL; // Last instr must be a terminator
    } _instrs;

    // Sections that precede this one
    obj::Array<Section *> _preceding;

public:

    Instr *last_instr()  { return _instrs.last;  }
    Instr *first_instr() { return _instrs.first; }

    void precedes(Section *section)  { _preceding.push(section);   }
    void unprecede(Section *section) { _preceding.remove(section); }

    // For use in Instr struct
    void _instr_set_last(Instr *instr)  { _instrs.last  = instr; }
    void _instr_set_first(Instr *instr) { _instrs.first = instr; }


    void append_instr(Instr *);

    // Build an array of the sections that succeed this one
    obj::Array<Section *> succeeding_sections();
    // Get the array of the preceding sections
    const obj::Array<Section *> &preceding_sections() { return _preceding; }

};


/*****************\
    Instruction
\*****************/

struct InstrKind : NoCreate {
enum Enum {
    INT_OP,
    FP_OP,
    TRANSFORM,
    IMMEDIATE,

    LOAD,
    STORE,
    STACKALLOC,
    GLOBALPTR,

    ATOMIC_LOAD,
    ATOMIC_STORE,
    ATOMIC_OP,
    ATOMIC_CAS,
    ATOMIC_FENCE,

    SYMCALL,
    PTRCALL,

    JUMP,
    BRANCH,
    SWITCH,
    RETURN,

    ASM,
    ASM_OUT,

    JUNCTION,
    JUNCT_OUT,

    FNPARAMS,
    FNPARAM_OUT,
};
static constexpr InstrKind::Enum MAX_VALUE = FNPARAM_OUT;
};


struct InstrFns {
    // For destroying this instruction by dropping its references and freeing it.
    // Instructions that must persist may do so instead.
    void (*destroy)(Instr *instr);

    // For replacing uses within the struct.
    // Should return true if TO_REPLACE was destroyed.
    bool (*replace_uses_in)(Instr *replace_in, Instr *to_replace, Instr *replacement);
};

extern InstrFns instr_fns[];


struct Instr : NoCopyMove {
private:

    Instr *_prev;
    Instr *_next;
    Section *_section;

    obj::Array<Instr *> _users;

    InstrKind::Enum _kind     : 16;
    Primitive::Enum _out_type : 16;

    // Pointers MUST have this ID set accurately
    PtrID _ptr_id = PTR_ID_UNSET;

public:

    InstrKind::Enum kind()     { return _kind;     }
    Primitive::Enum out_type() { return _out_type; }
    Section *section()         { return _section;  }
    Instr *next() { return _next; }
    Instr *prev() { return _prev; }

    void set_ptr_id(PtrID ptr_id) { _ptr_id = ptr_id; }
    PtrID ptr_id()
    {
        assert(_ptr_id != PTR_ID_UNSET);
        return _ptr_id;
    }


    // Returns true if this is the first instruction of the section,
    // or if the next instruction is a JunctionInstr (so the first instruction of the section).
    bool is_first()
    {
        if (!_prev) return true;
        return _prev->kind() == InstrKind::JUNCTION;
    }
    

    void ref(Instr *to_ref) {
        to_ref->_users.push(this);
    }

    // Pops/destroys the reference if empty, and returns true if so
    bool unref(Instr *refd_instr)
    {
        refd_instr->_users.remove(this);

        if (refd_instr->_users.size() == 0)
        {
            refd_instr->pop();
            return true;
        }

        return false;
    }


    obj::Array<Instr *> copy_users() {
        return _users;
    }

    ObjSize nusers() {
        return _users.size();
    }

    
    // Replace all uses of TO_REPLACE by REPLACEMENT inside REPLACE_IN.
    bool replace_uses_in(Instr *to_replace, Instr *replacement) {
        return instr_fns[_kind].replace_uses_in(this, to_replace, replacement);
    }

    // Replace all uses of this instr by REPLACEMENT.
    // Automatically pops this instr.
    // This may only be called on instructions that directly output a value.
    void replace_uses_with(Instr *replacement)
    {
        assert(_out_type != Primitive::unset);

        if (_users.size() == 0)
        {
            this->pop();
            return;
        }

        // Keep iterating until this instr is destroyed.
        // We can keep indexing at zero, to avoid relying on the moving `_users.size`.
        // That index should be popped by `replace_uses_in()` every time.
        Instr *prev_user = NULL;
        while (true)
        {
            assert(_users[0] != prev_user);
            prev_user = _users[0];

            bool destroyed = _users[0]->replace_uses_in(this, replacement);
            if (destroyed) return;
        }
    }

    // Pop an instr by clearing its references and destroying its link.
    // This doesn't actually pop instructions that must not be popped.
    void pop() {
        instr_fns[_kind].destroy(this);
    }


    // Place an instruction on this one's `next`
    void place_next(Instr *instr)
    {
        instr->_section = _section;

        instr->_prev = this;
        instr->_next = _next;

        if (_next) _next->_prev = instr;
        else _section->_instr_set_last(instr);

        _next = instr;
    }

    // Place an instruction on this one's `prev
    void place_prev(Instr *instr)
    {
        instr->_section = _section;

        instr->_next = this;
        instr->_prev = _prev;

        if (_prev) _prev->_next = instr;
        else _section->_instr_set_first(instr);

        _prev = instr;
    }

    // Move an already-placed instruction to this one's next
    void move_next(Instr *instr)
    {
        if (_next == instr)
            return;

        instr->pull();
        place_next(instr);
    }


    // Move an already-placed instruction to this one's prev
    void move_prev(Instr *instr)
    {
        if (_prev == instr)
            return;

        instr->pull();
        place_prev(instr);
    }

    // Pull an instruction from its section without popping it, so that it can be placed again later.
    // For immediate move operations, use `move_*()` instead.
    void pull()
    {
        if (_next) _next->_prev = _prev;
        else _section->_instr_set_last(_prev);

        if (_prev) _prev->_next = _next;
        else _section->_instr_set_first(_next);
    }


    // For use in InstrBase.
    // Setup of the section must happen on placement of the instruction.
    static void setup(Instr *instr, InstrKind::Enum kind, Primitive::Enum out_type)
    {
        instr->_kind     = kind;
        instr->_out_type = out_type;
    }

    // For use in Section
    void _section_set_next(Instr *instr) { _next = instr; }
    void _section_set_prev(Instr *instr) { _prev = instr; }
    void _section_set_self(Section *section) { _section = section; }


    template<typename T>
    T *cast()
    {
        assert(T::is_kind(_kind));
        return (T *)this;
    }

};


/* # Instruction Base
 * 
 * Data: the data struct of the instruction kind.
 * Kind: the kind's enum.
 * 
 * # Expected methods
 * 
 * `static void create(Primitive::Enum out_type, Section *section, ...)`:
 * Create an instance of the struct. This method isn't called by the base class,
 * and its params should be specific to the struct itself.
 * This method should use the base class's `construct()` method for getting an object to setup.
 * 
 * `bool cleanup()`:
 * If the struct must persist, must return false.
 * Otherwise must clear references and return true.
 * 
 * `bool replace_uses_in(Instr *to_replace, Instr *replacement)`:
 * For replacing uses of TO_REPLACE inside us with REPLACEMENT.
 * Should return true if TO_REPLACE was destroyed.
 * 
 * # Other expectations
 * 
 * Instructions that must not be auto-popped should store a reference to themselves.
 * This is to only have them be destroyed by an explicit pop.
 * This reference doesn't have to be cleared on cleanup.
 * 
 * Instructions with explicit OutInstrs for their output need special referencing/cleanup behavior.
 * The OutInstr must use the OutInstrBase base class. This class creates a `_destroy()` method for
 * unlinking and freeing the OutInstr. The parent Instr, on cleanup, is responsible for calling
 * that `_destroy()` method on its OutInstrs.
 * 
 */
template<typename Data, InstrKind::Enum Kind>
struct InstrBase : Instr {

    static Data *construct(Primitive::Enum out_type)
    {
        Data *data = objalloc::malloc<Data>();
        new (data) Data();
        Instr::setup(data, Kind, out_type);
        return data;
    }

    static bool is_kind(InstrKind::Enum kind) { return kind == Kind; }

private:

    static void destroy(Instr *instr)
    {
        Data *data = (Data *)instr;
        bool can_destroy = data->cleanup();

        if (!can_destroy)
            return;

        data->pull();
        data->~Data();
        objalloc::free<Data>(data);
        return;
    }

    static bool replace_uses_in(Instr *replace_in, Instr *to_replace, Instr *replacement) {
        return ((Data *)replace_in)->replace_uses_in(to_replace, replacement);
    }

public:

    static constexpr InstrFns fns = {
        .destroy = destroy,
        .replace_uses_in = replace_uses_in,
    };

};

template<typename Data, InstrKind::Enum Kind>
struct OutInstrBase : InstrBase<Data, Kind> {
private:

    bool _unrefd = false;
    Instr *_parent;

public:

    Instr *parent() { return _parent; }

    static Data *create(Primitive::Enum out_type) {
        return InstrBase<Data, Kind>::construct(out_type);
    }

    bool replace_uses_in(Instr *to_replace, Instr *replacement) {
        (void)to_replace; (void)replacement;
        return false;
    }

    bool cleanup() {
        if (!_unrefd)
            ((Data *)this)->unref(_parent);
        return false;
    }

    // For use in the OutInstr's parent
    static void _destroy(Data *instr)
    {
        instr->pull();
        instr->~Data();
        objalloc::free<Data>(instr);
    }

    void _set_parent(Instr *parent)
    {
        _parent = parent;
        ((Data *)this)->ref(parent);
    }

};


/**************\
    Variants
\**************/

/* Helpers */

static void array_add_refs(Instr *instr, const obj::Array<Instr *> &arr)
{
    for (ObjSize i = 0; i < arr.size(); i++)
        instr->ref(arr[i]);
}

static void replace_uses(Instr *&var, Instr *to_replace, Instr *replacement, bool &replaced)
{
    if (var == to_replace)
    {
        var = replacement;
        replaced = true;
    }
}

static void replace_uses(obj::Array<Instr *>&arr, Instr *to_replace, Instr *replacement, bool &replaced)
{
    for (ObjSize i = 0; i < arr.size(); i++)
        replace_uses(arr[i], to_replace, replacement, replaced);
}

static bool replaced_fix_refs(Instr *self, Instr *to_replace, Instr *replacement, bool replaced)
{
    if (!replaced)
        return false;

    self->ref(replacement);
    return self->unref(to_replace);
}

#define REPLACE(var) replace_uses(var, to_replace, replacement, replaced)

#define REPLACE_USES_IN_FN(code) \
    bool replace_uses_in(Instr *to_replace, Instr *replacement) \
    { \
        bool replaced = false; \
        code \
        return replaced_fix_refs(this, to_replace, replacement, replaced); \
    }


/* "basic" ops */

struct IntInstr : InstrBase<IntInstr, InstrKind::INT_OP> {
private:

    Instr *_left, *_right;

    IntOp::Enum _op;
    int _flags;

public:

    Instr *left()    { return _left;  }
    Instr *right()   { return _right; }
    IntOp::Enum op() { return _op;    }
    int flags()      { return _flags; }

    static IntInstr *create(Primitive::Enum out_type, Instr *left, Instr *right, IntOp::Enum op, int flags)
    {
        IntInstr *instr = construct(out_type);

        instr->_left  = left;
        instr->_right = right;
        instr->_op    = op;
        instr->_flags = flags;
        
        instr->ref(left);
        instr->ref(right);

        return instr;
    }

    REPLACE_USES_IN_FN(
        REPLACE(_left);
        REPLACE(_right);
    )

    bool cleanup() {
        unref(_left);
        unref(_right);
        return true;
    }

};


struct FpInstr : InstrBase<FpInstr, InstrKind::FP_OP> {
private:

    Instr *_left, *_right;

    FpOp::Enum _op;
    int _flags;

public:

    Instr *left()   { return _left;  }
    Instr *right()  { return _right; }
    FpOp::Enum op() { return _op;    }
    int flags()     { return _flags; }

    static FpInstr *create(Primitive::Enum out_type, Instr *left, Instr *right, FpOp::Enum op, int flags)
    {
        FpInstr *instr = construct(out_type);

        instr->_left  = left;
        instr->_right = right;
        instr->_op    = op;
        instr->_flags = flags;

        instr->ref(left);
        instr->ref(right);

        return instr;
    }

    REPLACE_USES_IN_FN(
        REPLACE(_left);
        REPLACE(_right);
    )

    bool cleanup() {
        unref(_left);
        unref(_right);
        return true;
    }

};


// For transforming variables
struct TransformInstr : InstrBase<TransformInstr, InstrKind::TRANSFORM> {
private:

    Instr *_in; // Value to transform. Transformation type depends on the in/out type, and for integer extension, on signedness
    bool _signed;

public:

    Instr *in()      { return _in;     }
    bool is_signed() { return _signed; }

    static TransformInstr *create(Primitive::Enum out_type, Instr *in, bool is_signed)
    {
        TransformInstr *instr = construct(out_type);

        instr->_in     = in;
        instr->_signed = is_signed;

        instr->ref(in);

        return instr;
    }

    REPLACE_USES_IN_FN(
        REPLACE(_in);
    )

    bool cleanup() {
        unref(_in);
        return true;
    }

};


struct ImmediateInstr : InstrBase<ImmediateInstr, InstrKind::IMMEDIATE> {
private:

    // Immediate is stored starting at the base.
    // Size and type is that of the Instr's ValueType.
    unsigned char _immbuf[Primitive::LARGEST_SCALAR];

public:

    template<typename T>
    T imm()
    {
        T immval;
        memcpy(&immval, _immbuf, sizeof(T));
        return immval;
    }

    template<typename T>
    static ImmediateInstr *create(Primitive::Enum out_type, T immval)
    {
        ImmediateInstr *instr = construct(out_type);
        memcpy(instr->_immbuf, &immval, sizeof(T));

        return instr;
    }

    REPLACE_USES_IN_FN()

    bool cleanup() {
        return true;
    }

};


/* Memory */

struct AccessData {
    Instr *ptr;
    Ssize offset;
    bool aliases_all;   // Whether the access aliases everything instead of just its own type
    bool ptr_exclusive; // Pointer-exclusive aliasing, only aliases with pointers that have the same PTR_ID
    bool struct_access; // Whether this is a struct access
};

struct MemInfoBase {
private:

    // Keep the access pointer outside of this struct, otherwise we complicate referencing it

    Ssize _offset; // The offset of the access relative to the pointer

    bool _aliases_all;
    bool _ptr_exclusive;
    bool _struct_access;

public:

    MemInfoBase *info_base() { return this;       }

    Ssize offset()       { return _offset;        }
    bool aliases_all()   { return _aliases_all;   }
    bool ptr_exclusive() { return _ptr_exclusive; }
    bool struct_access() { return _struct_access; }

    void _setup(const AccessData &info)
    {
        _offset        = info.offset;
        _aliases_all   = info.aliases_all;
        _ptr_exclusive = info.ptr_exclusive;
        _struct_access = info.struct_access;
    }

};


struct LoadInstr : InstrBase<LoadInstr, InstrKind::LOAD>, MemInfoBase {
private:

    Instr *_ptr;
    unsigned _is_volatile : 1;

public:

    Instr *ptr()       { return _ptr;         }
    bool is_volatile() { return _is_volatile; }

    static LoadInstr *create(Primitive::Enum out_type, const AccessData &data, bool is_volatile)
    {
        LoadInstr *instr = construct(out_type);

        instr->MemInfoBase::_setup(data);
        instr->_ptr = data.ptr;
        instr->_is_volatile = is_volatile;

        if (is_volatile) instr->ref(instr);
        instr->ref(data.ptr);

        return instr;
    }

    REPLACE_USES_IN_FN(
        REPLACE(_ptr);
    )

    bool cleanup()
    {
        if (_is_volatile)
            return false;

        unref(_ptr);
        return true;
    }

};


struct StoreInstr : InstrBase<StoreInstr, InstrKind::STORE>, MemInfoBase {
private:

    Instr *_ptr;
    unsigned _is_volatile : 1;
    Instr *_value; // Value to store

public:

    Instr *ptr()       { return _ptr;         }
    Instr *value()     { return _value;       }
    bool is_volatile() { return _is_volatile; }

    static StoreInstr *create(Instr *value, const AccessData &data, bool is_volatile)
    {
        StoreInstr *instr = construct(Primitive::unset);

        instr->MemInfoBase::_setup(data);
        instr->_ptr   = data.ptr;
        instr->_value = value;
        instr->_is_volatile = is_volatile;

        instr->ref(instr); // Stores have side effects, must persist even if not volatile
        instr->ref(value);
        instr->ref(data.ptr);

        return instr;
    }

    REPLACE_USES_IN_FN(
        REPLACE(_value);
        REPLACE(_ptr);
    )

    bool cleanup()
    {
        if (_is_volatile)
            return false;

        unref(_value);
        unref(_ptr);
        return true;
    }

};


struct StackAllocInstr : InstrBase<StackAllocInstr, InstrKind::STACKALLOC> {
private:

    Instr *_size; // Space to allocate
    Size _align;

public:

    Instr *size() { return _size;  }
    Size align()  { return _align; }

    static StackAllocInstr *create(Primitive::Enum out_type, Instr *size, Size align)
    {
        StackAllocInstr *instr = construct(out_type);

        instr->_size  = size;
        instr->_align = align;

        instr->ref(size);

        return instr;
    }

    REPLACE_USES_IN_FN(
        REPLACE(_size);
    )

    bool cleanup() {
        unref(_size);
        return true;
    }

};


struct GlobalPtrInstr : InstrBase<GlobalPtrInstr, InstrKind::GLOBALPTR> {
private:

    obj::Str _symbol; // Symbol of the global to get the pointer from

public:

    obj::Str symbol() { return _symbol; }

    static GlobalPtrInstr *create(Primitive::Enum out_type, obj::Str symbol)
    {
        GlobalPtrInstr *instr = construct(out_type);
        instr->_symbol = symbol;
        return instr;
    }

    REPLACE_USES_IN_FN()

    bool cleanup() {
        return true;
    }

};


/* Atomics */

// Atomics should never be cleaned up

struct AtomicOrder : NoCreate {
enum Enum {
    RELAXED,
    ACQUIRE,
    RELEASE,
    ACQREL,
    SEQCST,
};

static constexpr unsigned MAX_VALUE = SEQCST;
};

struct AtomicLoadInstr : InstrBase<AtomicLoadInstr, InstrKind::ATOMIC_LOAD>, MemInfoBase {
private:

    Instr *_ptr;
    AtomicOrder::Enum _order;

public:

    Instr *ptr()              { return _ptr;   }
    AtomicOrder::Enum order() { return _order; }

    static AtomicLoadInstr *create(Primitive::Enum out_type, const AccessData &data, AtomicOrder::Enum order)
    {
        AtomicLoadInstr *instr = construct(out_type);

        instr->MemInfoBase::_setup(data);
        instr->_ptr   = data.ptr;
        instr->_order = order;

        instr->ref(instr);
        instr->ref(data.ptr);

        return instr;
    }

    REPLACE_USES_IN_FN(
        REPLACE(_ptr);
    )

    bool cleanup() {
        return false;
    }

};


struct AtomicStoreInstr : InstrBase<AtomicStoreInstr, InstrKind::ATOMIC_STORE>, MemInfoBase {
private:

    Instr *_ptr;
    Instr *_value;
    AtomicOrder::Enum _order;

public:

    Instr *ptr()              { return _ptr;   }
    Instr *value()            { return _value; }
    AtomicOrder::Enum order() { return _order; }

    static AtomicStoreInstr *create(Instr *value, const AccessData &data, AtomicOrder::Enum order)
    {
        AtomicStoreInstr *instr = construct(Primitive::unset);

        instr->MemInfoBase::_setup(data);
        instr->_ptr   = data.ptr;
        instr->_order = order;
        instr->_value = value;

        instr->ref(instr);
        instr->ref(value);
        instr->ref(data.ptr);

        return instr;
    }

    REPLACE_USES_IN_FN(
        REPLACE(_value);
        REPLACE(_ptr);
    )

    bool cleanup() {
        return false;
    }

};


struct AtomicOpInstr : InstrBase<AtomicOpInstr, InstrKind::ATOMIC_OP>, MemInfoBase {
private:

    Instr *_ptr;
    Instr *_value;
    AtomicOrder::Enum _order;
    AtomicOp::Enum    _op;

public:

    Instr *ptr()   { return _ptr;   }
    Instr *value() { return _value; }

    AtomicOrder::Enum order() { return _order; }
    AtomicOp::Enum    op()    { return _op;    }

    static AtomicOpInstr *create(Primitive::Enum out_type, Instr *value, const AccessData &data, AtomicOrder::Enum order, AtomicOp::Enum op)
    {
        AtomicOpInstr *instr = construct(out_type);

        instr->MemInfoBase::_setup(data);
        instr->_ptr   = data.ptr;
        instr->_value = value;
        instr->_order = order;
        instr->_op    = op;

        instr->ref(instr);
        instr->ref(value);
        instr->ref(data.ptr);

        return instr;
    }

    REPLACE_USES_IN_FN(
        REPLACE(_value);
        REPLACE(_ptr);
    )

    bool cleanup() {
        return false;
    }

};


struct AtomicCASInstr : InstrBase<AtomicCASInstr, InstrKind::ATOMIC_CAS>, MemInfoBase {
private:

    Instr *_ptr;
    Instr *_value;

    AtomicOrder::Enum _order;

public:

    Instr *ptr()   { return _ptr;   }
    Instr *value() { return _value; }

    AtomicOrder::Enum order() { return _order; }

    static AtomicCASInstr *create(Primitive::Enum out_type, Instr *value, const AccessData &data, AtomicOrder::Enum order)
    {
        AtomicCASInstr *instr = construct(out_type);

        instr->MemInfoBase::_setup(data);
        instr->_ptr   = data.ptr;
        instr->_value = value;
        instr->_order = order;

        instr->ref(instr);
        instr->ref(value);
        instr->ref(data.ptr);

        return instr;
    }

    REPLACE_USES_IN_FN(
        REPLACE(_value);
        REPLACE(_ptr);
    )

    bool cleanup() {
        return false;
    }

};


struct AtomicFenceInstr : InstrBase<AtomicFenceInstr, InstrKind::ATOMIC_FENCE> {
private:

    AtomicOrder::Enum _order;

public:

    AtomicOrder::Enum order() { return _order; }

    static AtomicFenceInstr *create(AtomicOrder::Enum order)
    {
        AtomicFenceInstr *instr = construct(Primitive::unset);
        instr->_order = order;
        return instr;
    }

    REPLACE_USES_IN_FN()

    bool cleanup() {
        return false;
    }

};


/* Calls */

struct SymbolCallInstr : InstrBase<SymbolCallInstr, InstrKind::SYMCALL> {
private:

    obj::Str _symbol;
    obj::Array<Instr *> _in;
    ValueType _return_type;

public:

    obj::Str symbol()       { return _symbol; }
    ValueType return_type() { return _return_type; }
    const obj::Array<Instr *> &in_args() { return _in; }

    // Pass UNSET for RETURN_TYPE if no return value.
    // For struct returns, the out type must be PTR.
    static SymbolCallInstr *create(Primitive::Enum out_type, ValueType return_type, obj::Str symbol, obj::Array<Instr *> in)
    {
        SymbolCallInstr *instr = construct(out_type);
        
        instr->_symbol = symbol;
        instr->_in     = in;
        instr->_return_type = return_type;

        return instr;
    }

    REPLACE_USES_IN_FN()

    bool cleanup() {
        return false; // TODO: pureness consideration
    }

};


struct PtrCallInstr : InstrBase<PtrCallInstr, InstrKind::PTRCALL> {
private:

    Instr *_ptr; // Pointer to call
    obj::Array<Instr *> _in;
    ValueType _return_type;

public:

    Instr *ptr() { return _ptr; }
    ValueType return_type()     { return _return_type; }
    const obj::Array<Instr *> &in_args() { return _in; }

    // Pass UNSET for RETURN_TYPE if no return value.
    // For struct returns, the out type must be PTR.
    static PtrCallInstr *create(Primitive::Enum out_type, ValueType return_type, Instr *ptr, obj::Array<Instr *> in)
    {
        PtrCallInstr *instr = construct(out_type);

        instr->_ptr = ptr;
        instr->_in  = in;
        instr->_return_type = return_type;

        instr->ref(instr); // Pointer calls have unknown side-effects, mustn't be removed
        instr->ref(ptr);

        return instr;
    }

    REPLACE_USES_IN_FN(
        REPLACE(_ptr);
    )

    bool cleanup() {
        return false;
    }

};


/* Section terminators */

struct JumpInstr : InstrBase<JumpInstr, InstrKind::JUMP> {
private:

    Section *_next;

    // For jumps to junctions, the junction values
    obj::Array<Instr *> _junction_vals;

public:

    Section *next() { return _next; }
    const obj::Array<Instr *> &junction_vals() { return _junction_vals; }

    static JumpInstr *create(Section *next, obj::Array<Instr *> &&junction_vals)
    {
        JumpInstr *instr = construct(Primitive::unset);

        instr->_next = next;
        instr->_junction_vals = std::move(junction_vals);

        instr->ref(instr);
        array_add_refs(instr, instr->_junction_vals);

        return instr;
    }

    REPLACE_USES_IN_FN(
        REPLACE(_junction_vals);
    )

    bool cleanup() {
        return false;
    }

};


struct BranchInstr : InstrBase<BranchInstr, InstrKind::BRANCH> {
private:

    Instr *_cond; // Must be an i1

    Section *_then_case;
    Section *_else_case;

public:

    Instr   *cond()      { return _cond;      }
    Section *then_case() { return _then_case; }
    Section *else_case() { return _else_case; }

    static BranchInstr *create(Instr *cond, Section *then_case, Section *else_case)
    {
        BranchInstr *instr = construct(Primitive::unset);

        instr->_cond = cond;
        instr->_then_case = then_case;
        instr->_else_case = else_case;

        instr->ref(instr);
        instr->ref(cond);

        return instr;
    }

    REPLACE_USES_IN_FN(
        REPLACE(_cond);
    )

    bool cleanup() {
        return false;
    }

};


struct SwitchCase {
    Instr *val; // Must lead to a constant

    bool fallthrough;
    Section *section;
};

struct SwitchInstr : InstrBase<SwitchInstr, InstrKind::SWITCH> {
private:

    Instr *_value; // Value to switch on
    obj::Array<SwitchCase> _cases; // Must be ordered as written in code

public:

    Instr *value() { return _value; }
    const obj::Array<SwitchCase> &cases() { return _cases; }

    static SwitchInstr *create(Instr *value, obj::Array<SwitchCase> &&cases)
    {
        SwitchInstr *instr = construct(Primitive::unset);

        instr->_value = value;
        instr->_cases = std::move(cases);

        instr->ref(instr);
        instr->ref(value);

        return instr;
    }

    REPLACE_USES_IN_FN(
        REPLACE(_value);
    )

    bool cleanup() {
        return false;
    }

};


struct ReturnInstr : InstrBase<ReturnInstr, InstrKind::RETURN> {
private:
    
    Instr *_to_return;
    ValueType _return_type;

public:

    Instr *to_return()      { return _to_return;   }
    ValueType return_type() { return _return_type; }

    // Pass UNSET for RETURN_TYPE if no return value.
    static ReturnInstr *create(ValueType return_type, Instr *to_return)
    {
        ReturnInstr *instr = construct(Primitive::unset);
        
        instr->_to_return   = to_return;
        instr->_return_type = return_type;

        instr->ref(instr);
        instr->ref(to_return);

        return instr;
    }

    REPLACE_USES_IN_FN(
        REPLACE(_to_return);
    )

    bool cleanup() {
        return false;
    }

};


/* Assembly */

struct AsmOutInstr : OutInstrBase<AsmOutInstr, InstrKind::ASM_OUT> { };

struct AsmOperand {
    obj::RawStr name; // Name of the value inside the text

    // NULL if not in/out
    Instr *in;
    AsmOutInstr *out;

    // For out-only, whether it clobbers early
    bool early_clobber;
};

struct AsmInstr : InstrBase<AsmInstr, InstrKind::ASM> {
private:

    obj::Array<AsmOperand> _operands;
    obj::Array<obj::RawStr> _reg_clobs;

    obj::RawStr _text;

    bool _is_volatile;
    bool _taints_memory; // For if memory is touched beyond strictly the input pointers

    // Goto assembly not supported, unknown if it will be later on

public:

    const obj::Array<AsmOperand>  &operands()  { return _operands;  }
    const obj::Array<obj::RawStr> &reg_clobs() { return _reg_clobs; }
    const obj::RawStr &text() { return _text;     }
    bool is_volatile()   { return _is_volatile;   }
    bool taints_memory() { return _taints_memory; }

    // Sets the AsmOutInstrs' references and pointers
    static AsmInstr *create(obj::Array<AsmOperand> &&operands, obj::Array<obj::RawStr> &&reg_clobs, obj::RawStr &&text, bool is_volatile, bool taints_memory)
    {
        AsmInstr *instr = construct(Primitive::unset);

        instr->_operands  = std::move(operands);
        instr->_reg_clobs = std::move(reg_clobs);
        instr->_text      = std::move(text);
        instr->_is_volatile   = is_volatile;
        instr->_taints_memory = taints_memory;

        for (ObjSize i = 0; i < instr->_operands.size(); i++)
        {
            const AsmOperand &operand = instr->_operands[i];

            if (operand.in)
                instr->ref(operand.in);

            if (operand.out)
            {
                AsmOutInstr *asmout = (AsmOutInstr *)operand.out;
                asmout->_set_parent(instr);
                asmout->ref(instr);
            }
        }

        return instr;
    }

    REPLACE_USES_IN_FN(
        for (ObjSize i = 0; i < _operands.size(); i++)
        {
            if (_operands[i].in)
                REPLACE(_operands[i].in);
        }
    )

    bool cleanup()
    {
        if (_is_volatile)
            return false;

        for (ObjSize i = 0; i < _operands.size(); i++)
        {
            const AsmOperand &operand = _operands[i];

            if (operand.in)
                unref(operand.in);

            if (operand.out)
                AsmOutInstr::_destroy(operand.out);
        }

        return true;
    }

};


/* Junction */

struct JunctOutInstr : OutInstrBase<JunctOutInstr, InstrKind::JUNCT_OUT> { };

struct JunctionInstr : InstrBase<JunctionInstr, InstrKind::JUNCTION> {
private:

    obj::Array<JunctOutInstr *> _outs;

public:

    const obj::Array<JunctOutInstr *> &outs() { return _outs; }

    // Sets the JunctOutResults' references and pointers
    static JunctionInstr *create(obj::Array<JunctOutInstr *> &&outs)
    {
        JunctionInstr *instr = construct(Primitive::unset);

        instr->_outs = std::move(outs);

        for (ObjSize i = 0; i < instr->_outs.size(); i++)
        {
            JunctOutInstr *out = (JunctOutInstr *)instr->_outs[i];
            out->_set_parent(instr);
            out->ref(instr);
        }

        return instr;
    }

    REPLACE_USES_IN_FN()

    bool cleanup() {
        return false;
    }

};


/* Fn Params */

struct FnParamOutInstr : OutInstrBase<FnParamOutInstr, InstrKind::FNPARAM_OUT> { };

struct FnParamsInstr : InstrBase<FnParamsInstr, InstrKind::FNPARAMS> {
private:

    obj::Array<Instr *> _outs; // Points to FnParamOutInstrs

public:

    const obj::Array<Instr *> &outs() { return _outs; }

    static FnParamsInstr *create(obj::Array<Instr *> &&outs)
    {
        FnParamsInstr *instr = construct(Primitive::unset);

        instr->_outs = std::move(outs);

        for (ObjSize i = 0; i < instr->_outs.size(); i++)
        {
            FnParamOutInstr *out = (FnParamOutInstr *)instr->_outs[i];
            out->_set_parent(instr);
            out->ref(instr);
        }

        return instr;
    }

    REPLACE_USES_IN_FN()

    bool cleanup() {
        return false;
    }

};


/*****************\
    Section Fns
\*****************/

// We have to declare this one later because of dependence on the Instr struct
inline void SectionBase::append_instr(Instr *instr)
{
    if (_instrs.last)
    {
        _instrs.last->_section_set_next(instr);
        instr->_section_set_prev(_instrs.last);
        instr->_section_set_next(NULL);
        _instrs.last = instr;
    } 
    else
    {
        // First instruction we append
        _instrs.last = _instrs.first = instr;
        instr->_section_set_next(NULL);
        instr->_section_set_prev(NULL);
    }

    instr->_section_set_self(Section::from_regular(this));
}

// Get an array of the sections that follow this one
inline obj::Array<Section *> SectionBase::succeeding_sections()
{
    obj::Array<Section *> following;
    Instr *term = _instrs.last;

    switch (term->kind())
    {
    case InstrKind::JUMP:
    {
        JumpInstr *jump = (JumpInstr *)term;
        following.push(jump->next());
        break;
    }
    case InstrKind::BRANCH:
    {
        BranchInstr *branch = (BranchInstr *)term;
        following.push(branch->then_case());
        following.push(branch->else_case());
        break;
    }
    case InstrKind::SWITCH:
    {
        auto cases = ((SwitchInstr *)term)->cases();

        for (ObjSize i = 0; i < cases.size(); i++)
            following.push(cases[i].section);

        break;
    }
    case InstrKind::RETURN:
    {
        // No following sections
        break;
    }

    default: assert(0); // Top instr must be a terminator
    }

    return following;
}


/*********************\
    Setup instr_fns
\*********************/

inline InstrFns instr_fns[] = {
    [InstrKind::INT_OP]       = IntInstr::fns,
    [InstrKind::FP_OP]        = FpInstr::fns,
    [InstrKind::TRANSFORM]    = TransformInstr::fns,
    [InstrKind::IMMEDIATE]    = ImmediateInstr::fns,

    [InstrKind::LOAD]         = LoadInstr::fns,
    [InstrKind::STORE]        = StoreInstr::fns,
    [InstrKind::STACKALLOC]   = StackAllocInstr::fns,
    [InstrKind::GLOBALPTR]    = GlobalPtrInstr::fns,

    [InstrKind::ATOMIC_LOAD]  = AtomicLoadInstr::fns,
    [InstrKind::ATOMIC_STORE] = AtomicStoreInstr::fns,
    [InstrKind::ATOMIC_OP]    = AtomicOpInstr::fns,
    [InstrKind::ATOMIC_CAS]   = AtomicCASInstr::fns,
    [InstrKind::ATOMIC_FENCE] = AtomicFenceInstr::fns,

    [InstrKind::SYMCALL]      = SymbolCallInstr::fns,
    [InstrKind::PTRCALL]      = PtrCallInstr::fns,

    [InstrKind::JUMP]         = JumpInstr::fns,
    [InstrKind::BRANCH]       = BranchInstr::fns,
    [InstrKind::SWITCH]       = SwitchInstr::fns,
    [InstrKind::RETURN]       = ReturnInstr::fns,

    [InstrKind::ASM]          = AsmInstr::fns,
    [InstrKind::ASM_OUT]      = AsmOutInstr::fns,

    [InstrKind::JUNCTION]     = JunctionInstr::fns,
    [InstrKind::JUNCT_OUT]    = JunctOutInstr::fns,

    [InstrKind::FNPARAMS]     = FnParamsInstr::fns,
    [InstrKind::FNPARAM_OUT]  = FnParamOutInstr::fns,
};

static_assert((sizeof(instr_fns) / sizeof(InstrFns)) == InstrKind::MAX_VALUE + 1);


}

#undef REPLACE
#undef REPLACE_USES_IN_FN
