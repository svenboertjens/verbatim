#pragma once


#include "shared/config.hpp"
#include "tools/print.hpp"
#include "obj/array.hpp"

#include "symbols.hpp"
#include "types.hpp"


namespace ir {

// Forward decls
struct Symbol;


// Local unit, for a single TU
struct LocalUnit {
private:

    SymbolMap _symbolmap;
    Types _types;

    obj::Str _path;

public:

    SymbolMap &symbolmap() { return _symbolmap; }
    Types     &types()     { return _types;     }
    obj::Str  &path()      { return _path;      }

    LocalUnit(obj::RawStr path, cfg::Config *cfg) :
        _symbolmap(),
        _types(cfg),
        _path(std::move(path))
    {}

    bool symbol_exists(obj::Str name) {
        return _symbolmap.get(name) != NULL;
    }

};


// Global unit, for all TUs to compile merged together
struct GlobalUnit {

    // Symbol collision data
    struct SymbolCollision {
        obj::Str original_path;  // Path of the original declaring file
        obj::Str duplicate_path; // Path of the duplicate declaring file
        obj::Str symbol;         // The duplicate symbol
    };

private:

    SymbolMap _symbols;
    Types _types;

    // Data of a single TU
    struct TUData {
        obj::Str path;
        SymbolMap symbols; // The TU's private symbols
    };

    obj::Array<TUData> _units;

    cfg::Config *_cfg;


    obj::Array<SymbolCollision> _collisions;

public:

    GlobalUnit(cfg::Config *cfg) :
        _types(cfg),
        _cfg(cfg)
    {}


    // This operation leaves the LocalUnit in an invalid state for further use.
    // Returns the TUID assigned to this LocalUnit.
    // All symbol collisions are tracked. `had_collision()` returns true if any occurred, and `get_collisions()` returns the array of collisions.
    // Adding units when a collision occurred is fine (and recommended) for error reporting.
    TUID add_local_unit(LocalUnit &unit)
    {
        _types.merge(unit.types());

        TUID tuid = _units.push_new();
        TUData &tudata = _units[tuid];
        tudata.path = unit.path();


        SymbolMap &symmap = unit.symbolmap();

        obj::Str *symbol;
        SymbolPtr *symptr;
        ObjSize iter = 0;
        while (symmap.next(iter, symbol, symptr))
        {
            // Copy to local for accessing, and so we can move it to the right SymbolMap after patching it
            SymbolPtr data = std::move(*symptr);
            
            // Set the symbol's TUID and patch its visibility if default
            data->_globalunit_set_tuid(tuid);
            if (data->visibility())
                data->_globalunit_set_visibility(_cfg->out.default_visibility);


            // Private symbols must go to the TU's private symbol map
            if (data->visibility() == Visibility::PRIVATE)
            {
                
                assert(tudata.symbols.get(*symbol) == NULL);
                tudata.symbols[*symbol] = std::move(data);
                continue;
            }

            // Non-existent symbols can be inserted directly
            SymbolMap::Entry *empty, *entry = _symbols.lookup(*symbol, empty);
            if (!entry)
            {
                _symbols.insert(empty, *symbol) = std::move(data);
                continue;
            }

            // Otherwise, act depending on strength

            SymbolPtr &existing = entry->val;

            if (data->linkage() == Linkage::STRONG)
            {
                // Strong and strong conflicts
                if (existing->linkage() == Linkage::STRONG)
                {
                    _collisions.push(SymbolCollision{
                        .symbol = *symbol,
                        .duplicate_path = tudata.path,
                        .original_path = _units[existing->tuid()].path
                    });
                    continue;
                }

                // Otherwise, strong overwrites
                entry->val = std::move(data);
                continue;
            }
            else if (data->linkage() == Linkage::WEAK)
            {
                // We yield to strong
                if (existing->linkage() == Linkage::STRONG)
                    continue;

                // External can be overwritten
                if (existing->linkage() == Linkage::EXTERNAL)
                {
                    entry->val = std::move(data);
                    continue;
                }

                // Otherwise nothing to do
                continue;
            }
            else if (data->linkage() == Linkage::EXTERNAL)
            {
                // Nothing to do, the other is equal/stronger
                continue;
            }

            // This shouldn't be reached
            assert(0);
            continue;
        }

        return tuid;
    }

    bool had_collision() { return _collisions.size() > 0; }
    const obj::Array<SymbolCollision> &get_collisions() { return _collisions; }


    // Get a symbol with respect to the TU's private symbols
    Symbol *get_symbol(obj::Str symbol, TUID tuid)
    {
        SymbolPtr *data;

        data = _units[tuid].symbols.get(symbol);
        if (data) return data->ptr();

        data = _symbols.get(symbol);
        if (data) return data->ptr();

        print::missing_symbol(symbol);
    }

};


}

