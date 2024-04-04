//====== Copyright ©, Valve Corporation, All rights reserved. =======
//
// Purpose: Additional shared object cache functionality for the GC
//
//=============================================================================

#ifndef SCHEMASYSTEM_H
#define SCHEMASYSTEM_H
#ifdef _WIN32
#pragma once
#endif

// #include "schemasystem/schemasystem.h"
// #include "schemasystem/schematypes.h"
#include <cstdint>
#include <type_traits>

#undef schema

namespace schema
{
    int32_t GetServerOffset(const char* pszClassName, const char* pszPropName);
}

#define SCHEMA_FIELD(type, className, propName)                                                        \
    std::add_lvalue_reference_t<type> propName()                                                       \
    {                                                                                                  \
        static const int32_t offset = schema::GetServerOffset(#className, #propName);        \
        if(offset == -1)                                                                               \
            std::runtime_error("Failed to find " #propName " in " #className);                         \
        return *reinterpret_cast<std::add_pointer_t<type>>(reinterpret_cast<intptr_t>(this) + offset); \
    }


#endif //SCHEMASYSTEM_H