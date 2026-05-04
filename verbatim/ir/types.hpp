#pragma once

#include "shared/primitive.hpp"
#include "shared/config.hpp"
#include "shared/defs.hpp"

#include "obj/array.hpp"
#include "obj/str.hpp"

#include <cassert>
#include <utility>


// Definition of the ValueType
using ValueType = obj::Str;

namespace ir {


struct StructField {
    Primitive::Enum type;
    Size offset;

    // Whether it's a raw memory field (for arrays, buffers) rather than just the type
    bool raw_memory;
};


/* Types
 * Stores the size data of types, and manages the handout of ValueTypes for new values.
 * Initializes itself with the primitive types.
 */
struct Types {
private:

    struct TypeData {
        SizeData sizedata;
        Primitive::Enum primitive; // Set to PTR for struct types

        // Struct fields, for non-primitives
        obj::Array<StructField> fields;
    };

    obj::StrMap<TypeData> _types;

    // Map of primitives -> their ValueType
    ValueType _primitives[Primitive::MAX_VALUE + 1];

public:

    Types(cfg::Config *cfg)
    {
        // Add the primitives
        for (Size i = 0; i < Primitive::MAX_VALUE; i++)
        {
            Size size = Primitive::sizes[i];
            ValueType name = ValueType(Primitive::names[i]);

            _primitives[i] = name;
            
            TypeData typedata;
            typedata.sizedata = {size, size};
            typedata.primitive = (Primitive::Enum)i;
            _types[name] = typedata;
        }

        Size ptr_size = target::ptr_sizes[cfg->target.arch];
        _types[_primitives[Primitive::ptr]].sizedata = {ptr_size, ptr_size};
    }


    // Get the ValueType of a primitive
    ValueType primitive_type(Primitive::Enum primitive) {
        return _primitives[primitive];
    }


    // Add a type. Returns the type's assigned ValueType.
    // Struct fields must be in offset order.
    ValueType add_type(obj::Array<StructField> &&fields, SizeData sizedata)
    {
        obj::RawStr typestr = Primitive::struct_typestr_prefix;

        for (ObjSize i = 0; i < fields.size(); i++)
            typestr.append((const char *)&fields[i], sizeof(StructField));

        ValueType valuetype = ValueType(typestr);

        _types[valuetype] = TypeData{
            .sizedata = sizedata,
            .fields = std::move(fields),
            .primitive = Primitive::ptr,
        };

        return valuetype;
    }


    SizeData sizedata(ValueType type)
    {
        assert(_types.get(type));
        SizeData sizedata = _types[type].sizedata;
        assert(sizedata.size != Primitive::VALUETYPE_SIZE_INVALID);
        return sizedata;
    }

    const obj::Array<StructField> &fields(ValueType type) 
    {
        assert(_types.get(type) && _types[type].primitive == Primitive::ptr);
        return _types[type].fields;
    }

    Primitive::Enum as_primitive(ValueType type)
    {
        assert(_types.get(type));
        return _types[type].primitive;
    }


    // Merge types of another Types class into this one
    void merge(const Types &other)
    {
        ValueType *key;
        TypeData *data;
        ObjSize iter = 0;

        while (other._types.next(iter, key, data))
            _types[*key] = *data; // Data should be equal on existing entries
    }

};


}
