#pragma once

#include "tools/structs.hpp"
#include "shared/symbol.hpp"

#include "obj/ptrmap.hpp"
#include "obj/link.hpp"

#include "types.hpp"
#include "instr.hpp"

#include <cassert>
#include <utility>

namespace ir {

// Forward decl
struct Symbol;


/**************\
     Symbol
\**************/

struct SymbolKind {
enum Enum {
    FUNCTION,
    SYMBOL_GLOBAL,
    DATA_GLOBAL
};
static constexpr Enum MAX_VALUE = DATA_GLOBAL;
};


struct SymbolFns {
    void (*destruct)(Symbol *);
};

extern SymbolFns symbol_fns[];


struct Symbol : NoCopyMove {
private:

    unsigned _vis : 3;
    unsigned _linkage : 2;

    SymbolKind::Enum _kind : 2;

    // Whether the symbol escaped to a pointer anywhere
    unsigned _escapes : 1 = false;

    // ID of the TU this symbol belongs to.
    // Only relevant when located in the GlobalUnit.
    TUID _tuid;

public:

    Visibility::Enum visibility() { return (Visibility::Enum)_vis;  }
    Linkage::Enum linkage()       { return (Linkage::Enum)_linkage; }

    SymbolKind::Enum kind() { return _kind;    }
    bool escapes()          { return _escapes; }
    TUID tuid()             { return _tuid;    }

    // For use in SymbolBase
    void _setup(Visibility::Enum visibility, Linkage::Enum linkage, SymbolKind::Enum kind)
    {
        _vis     = visibility;
        _linkage = linkage;
        _kind    = kind;

        _escapes = false;
    }

    void escaped() { _escapes = true; }

    // For use in GlobalUnit
    void _globalunit_set_tuid(TUID tuid) { _tuid = tuid; }
    void _globalunit_set_visibility(Visibility::Enum vis) { _vis = vis; }


    template<typename T>
    T *cast()
    {
        assert(T::is_kind(_kind));
        return (T *)this;
    }

};


template<typename Data, SymbolKind::Enum Kind>
struct SymbolBase : Symbol {

    static Data *construct(Visibility::Enum visibility, Linkage::Enum linkage)
    {
        Data *data = objalloc::malloc<Data>();
        new (data) Data();
        data->Symbol::_setup(visibility, linkage, Kind);
        return data;
    }

    static void destruct(Symbol *symbol)
    {
        Data *data = (Data *)symbol;
        data->~Data();
        objalloc::free<Data>(data);
    }

    static constexpr SymbolFns fns = SymbolFns{
        .destruct = destruct,
    };

    static bool is_kind(SymbolKind::Enum kind) { return kind == Kind; }

};


struct SymbolPtr {
private:

    Symbol *_ptr = NULL;

public:

    Symbol *ptr() { return _ptr; }

    SymbolPtr() = default;
    SymbolPtr(Symbol *ptr) : _ptr(ptr) {}

    ~SymbolPtr() {
        if (_ptr) symbol_fns[_ptr->kind()].destruct(_ptr);
    }

    SymbolPtr(SymbolPtr &&other)
    {
        _ptr = other._ptr;
        other._ptr = NULL;
    }
    SymbolPtr &operator=(SymbolPtr &&other) {
        return tools::assign_move_method(this, other);
    }

    Symbol &operator*()  { return *_ptr; }
    Symbol *operator->() { return _ptr;  }
    const Symbol &operator*()  const { return *_ptr; }
    const Symbol *operator->() const { return _ptr;  }

};

using SymbolMap = obj::StrMap<SymbolPtr>;


/****************\
     Function
\****************/

struct Dominance {
    Section *dom = NULL;
    ObjSize rdst; // Reverse distance, higher = closer to the entry
};

using DominanceMap = obj::PtrMap<Section *, Dominance>;

using PtrIDMap = obj::PtrMap<Instr *, unsigned>;

struct FnFlag : NoCreate {
enum Enum {
    NOFLAGS = 0,
    FORCEINLINE  = 1 << 0,
    NOINLINE     = 1 << 1,
    INTERPOSABLE = 1 << 2,
};
};

struct AccessState : NoCreate {
enum Enum {
    PURE, READS, READWRITE
};
};

struct Function : SymbolBase<Function, SymbolKind::FUNCTION> {
private:

    obj::Array<ValueType>  _params;
    obj::LinkList<Section> _sections;
    PtrIDMap _ptr_id_map;

    // The functions called by this function
    obj::Array<obj::Str> _calls;

    int _flags; // FnFlag flags
    AccessState::Enum _access_state = AccessState::READWRITE; // Default to the most conservative; only analysis may update this


    DominanceMap _dominance;
    obj::Array<Section *> _postorder;

    void order_visit(Section *sec, obj::PtrMap<Section *, int> &visited);
    void build_postorder();
    void build_dominance(); // This should always do `build_postorder()`, and is the proxy for whether `_postorder` itself exists

public:

    const obj::Array<ValueType> &params()  { return _params;     }
    const obj::Array<obj::Str> &calls()    { return _calls;      }
    const PtrIDMap &ptr_id_map()           { return _ptr_id_map; }

    // Don't modify this array! It's not marked const because I'm not adding const to every damn function
    obj::LinkList<Section> &sections() { return _sections; }

    int flags() { return _flags; }

    AccessState::Enum access_state() { return _access_state; }
    void update_access_state(AccessState::Enum access_state) {
        _access_state = access_state;
    }


    const DominanceMap &dominance()
    {
        // Lazy-build the dominance map
        if (_dominance.size() == 0)
            build_dominance();

        return _dominance;
    }
    const obj::Array<Section *> &postorder()
    {
        if (_dominance.size() == 0)
            build_dominance();

        return _postorder;
    }
    void invalidate_dominance()
    {
        _dominance = DominanceMap();
        _postorder = obj::Array<Section *>();
    }



    // Automatically adds itself to the symbol map
    static Function *create(SymbolMap &map, obj::Str symbol, Visibility::Enum visibility, Linkage::Enum linkage, obj::Array<ValueType> &&params, int flags)
    {
        assert(map.get(symbol) == NULL);

        Function *fn = construct(visibility, linkage);
        map[symbol] = fn;

        fn->_params = std::move(params);
        fn->_flags  = flags;

        return fn;
    }


    // Get the opening section.
    // Automatically places the function params at the start.
    Section *open(Types &types)
    {
        assert(_sections.size() == 0);
        Section *sec = _sections.push();

        obj::Array<Instr *> outs;
        for (ObjSize i = 0; i < _params.size(); i++)
        {
            outs.push(
                FnParamOutInstr::create(types.as_primitive(_params[i]))
            );
        }

        sec->append_instr(
            FnParamsInstr::create(std::move(outs))
        );

        return sec;
    }

    // Automatically sets PREDECESSOR as the section's (first) predecessor
    Section *new_section(Section *predecessor)
    {
        Section *sec = _sections.push();
        sec->precedes(predecessor);
        return sec;
    }


    // Get the intersect between two sections
    Section *intersect(Section *sec1, Section *sec2);

};


/***************\
     Globals
\***************/

struct GlobalSymbolVar : SymbolBase<GlobalSymbolVar, SymbolKind::SYMBOL_GLOBAL> {
private:

    obj::Str _symbol;

public:

    obj::Str symbol() { return _symbol; }

    // Automatically adds itself to the symbol map
    static GlobalSymbolVar *create(SymbolMap &map, obj::Str symbol, Visibility::Enum visibility, Linkage::Enum linkage, obj::Str symbol_of_ptr)
    {
        assert(map.get(symbol) == NULL);

        GlobalSymbolVar *global = construct(visibility, linkage);
        map[symbol] = global;

        global->_symbol = symbol_of_ptr;

        return global;
    }

};


struct GlobalDataVar : SymbolBase<GlobalDataVar, SymbolKind::DATA_GLOBAL> {
private:

    SizeData _sizedata;
    void *_data; // May be NULL for zero-init

public:

    SizeData sizedata() { return _sizedata; }
    void *data()        { return _data;     } // May be NULL for zero-init

    // Automatically adds itself to the symbol map
    static GlobalDataVar *create(SymbolMap &map, obj::Str symbol, Visibility::Enum visibility, Linkage::Enum linkage)
    {
        assert(map.get(symbol) == NULL);

        GlobalDataVar *global = construct(visibility, linkage);
        map[symbol] = global;

        return global;
    }

};


/****************\
    Symbol Fns
\****************/

inline SymbolFns symbol_fns[] = {
    [SymbolKind::FUNCTION]      = Function::fns,
    [SymbolKind::SYMBOL_GLOBAL] = GlobalSymbolVar::fns,
    [SymbolKind::DATA_GLOBAL]   = GlobalDataVar::fns,
};

static_assert((sizeof(symbol_fns) / sizeof(SymbolFns)) == SymbolKind::MAX_VALUE + 1, "Forgot to add a functions entry?");


}
