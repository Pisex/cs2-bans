/**
 * =============================================================================
 * CS2Fixes
 * Copyright (C) 2023 Source2ZE
 * Original code from SourceMod
 * Copyright (C) 2004-2014 AlliedModders LLC
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "sql_mm.h"

typedef enum EMySQLType
{
    MM_MYSQL_TYPE_DECIMAL,
    MM_MYSQL_TYPE_TINY,
    MM_MYSQL_TYPE_SHORT,
    MM_MYSQL_TYPE_LONG,
    MM_MYSQL_TYPE_FLOAT,
    MM_MYSQL_TYPE_DOUBLE,
    MM_MYSQL_TYPE_NULL,
    MM_MYSQL_TYPE_TIMESTAMP,
    MM_MYSQL_TYPE_LONGLONG,
    MM_MYSQL_TYPE_INT24,
    MM_MYSQL_TYPE_DATE,
    MM_MYSQL_TYPE_TIME,
    MM_MYSQL_TYPE_DATETIME,
    MM_MYSQL_TYPE_YEAR,
    MM_MYSQL_TYPE_NEWDATE,
    MM_MYSQL_TYPE_VARCHAR,
    MM_MYSQL_TYPE_BIT,
    MM_MYSQL_TYPE_TIMESTAMP2,
    MM_MYSQL_TYPE_DATETIME2,
    MM_MYSQL_TYPE_TIME2,
    MM_MYSQL_TYPE_UNKNOWN,
    MM_MYSQL_TYPE_JSON = 245,
    MM_MYSQL_TYPE_NEWDECIMAL = 246,
    MM_MYSQL_TYPE_ENUM = 247,
    MM_MYSQL_TYPE_SET = 248,
    MM_MYSQL_TYPE_TINY_BLOB = 249,
    MM_MYSQL_TYPE_MEDIUM_BLOB = 250,
    MM_MYSQL_TYPE_LONG_BLOB = 251,
    MM_MYSQL_TYPE_BLOB = 252,
    MM_MYSQL_TYPE_VAR_STRING = 253,
    MM_MYSQL_TYPE_STRING = 254,
    MM_MYSQL_TYPE_GEOMETRY = 255
} EMySQLType;

struct MySQLConnectionInfo
{
    const char *host;
    const char *user;
    const char *pass;
    const char *database;
    int port = 3306;
    int timeout = 60;
};

class IMySQLConnection : public ISQLConnection
{
};

class IMySQLClient
{
public:
    virtual IMySQLConnection *CreateMySQLConnection(MySQLConnectionInfo info) = 0;
};
