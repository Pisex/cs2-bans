
#include "schemasystem/schemasystem.h"
#include "schemasystem/schematypes.h"
#include "schemasystem.h"
#include "utils.hpp"
#include <cstring>

#ifdef _WIN32
#define MODULE_PREFIX ""
#define MODULE_EXT ".dll"
#else
#define MODULE_PREFIX "lib"
#define MODULE_EXT ".so"
#endif

namespace schema
{
    int32_t GetServerOffset(const char* pszClassName, const char* pszPropName);
}

int32_t schema::GetServerOffset(const char* pszClassName, const char* pszPropName)
{
    SchemaClassInfoData_t* pClassInfo = g_pSchemaSystem->FindTypeScopeForModule(MODULE_PREFIX "server" MODULE_EXT)->FindDeclaredClass(pszClassName).Get();
    if (pClassInfo)
    {
        for (int i = 0; i < pClassInfo->m_nFieldCount; i++)
        {
            auto& pFieldData = pClassInfo->m_pFields[i];

            if (std::strcmp(pFieldData.m_pszName, pszPropName) == 0)
            {
                return pFieldData.m_nSingleInheritanceOffset;
            }
        }
    }

    return -1;
}