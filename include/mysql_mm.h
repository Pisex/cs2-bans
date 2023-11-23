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

#include <functional>
#include <string>

#define MYSQLMM_INTERFACE "IMySQLClient"

class IMySQLQuery;

typedef std::function<void(bool)> ConnectCallbackFunc;
typedef std::function<void(IMySQLQuery*)> QueryCallbackFunc;

typedef enum EMySQLType {
	MM_MYSQL_TYPE_DECIMAL, MM_MYSQL_TYPE_TINY,
	MM_MYSQL_TYPE_SHORT, MM_MYSQL_TYPE_LONG,
	MM_MYSQL_TYPE_FLOAT, MM_MYSQL_TYPE_DOUBLE,
	MM_MYSQL_TYPE_NULL, MM_MYSQL_TYPE_TIMESTAMP,
	MM_MYSQL_TYPE_LONGLONG, MM_MYSQL_TYPE_INT24,
	MM_MYSQL_TYPE_DATE, MM_MYSQL_TYPE_TIME,
	MM_MYSQL_TYPE_DATETIME, MM_MYSQL_TYPE_YEAR,
	MM_MYSQL_TYPE_NEWDATE, MM_MYSQL_TYPE_VARCHAR,
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

class IMySQLRow
{
public:
};

class IMySQLResult
{
public:
	virtual int GetRowCount() = 0;
	virtual int GetFieldCount() = 0;
	virtual bool FieldNameToNum(const char* name, unsigned int* columnId) = 0;
	virtual const char* FieldNumToName(unsigned int colId) = 0;
	virtual bool MoreRows() = 0;
	virtual IMySQLRow* FetchRow() = 0;
	virtual IMySQLRow* CurrentRow() = 0;
	virtual bool Rewind() = 0;
	virtual EMySQLType GetFieldType(unsigned int field) = 0;
	virtual char* GetString(unsigned int columnId, size_t* length = nullptr) = 0;
	virtual size_t GetDataSize(unsigned int columnId) = 0;
	virtual float GetFloat(unsigned int columnId) = 0;
	virtual int GetInt(unsigned int columnId) = 0;
	virtual bool IsNull(unsigned int columnId) = 0;
};

class IMySQLQuery
{
public:
	virtual IMySQLResult* GetResultSet() = 0;
	virtual bool FetchMoreResults() = 0;
	virtual unsigned int GetInsertId() = 0;
	virtual unsigned int GetAffectedRows() = 0;
};

struct MySQLConnectionInfo
{
	const char* host;
	const char* user;
	const char* pass;
	const char* database;
	int port = 3306;
	int timeout = 60;
};

class IMySQLConnection
{
public:
	virtual void Connect(ConnectCallbackFunc callback) = 0;
	virtual void Query(char* query, QueryCallbackFunc callback) = 0;
	virtual void Query(const char* query, QueryCallbackFunc callback, ...) = 0;
	virtual void Destroy() = 0;
	virtual std::string Escape(char* string) = 0;
	virtual std::string Escape(const char* string) = 0;
};

class IMySQLClient
{
public:
	virtual IMySQLConnection* CreateMySQLConnection(MySQLConnectionInfo info) = 0;
};