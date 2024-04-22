#pragma once
#include "sql_mm.h"

struct SQLiteConnectionInfo
{
    // Relative to the base game directory.
    char *database;
};

class ISQLiteClient
{
public:
    virtual ISQLConnection *CreateSQLiteConnection(SQLiteConnectionInfo info) = 0;
};
