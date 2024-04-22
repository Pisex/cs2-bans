#pragma once

#include <functional>
#include <string>

#define SQLMM_INTERFACE "SQLInterface"

class ISQLQuery;

typedef std::function<void(bool)> ConnectCallbackFunc;
typedef std::function<void(ISQLQuery *)> QueryCallbackFunc;
typedef std::function<void(std::vector<ISQLQuery *>)> TransactionSuccessCallbackFunc;
typedef std::function<void(std::string, int)> TransactionFailureCallbackFunc;

struct Transaction
{
    std::vector<std::string> queries;
};

class ISQLRow
{
public:
};

class ISQLResult
{
public:
    virtual int GetRowCount() = 0;
    virtual int GetFieldCount() = 0;
    virtual bool FieldNameToNum(const char *name, unsigned int *columnId) = 0;
    virtual const char *FieldNumToName(unsigned int colId) = 0;
    virtual bool MoreRows() = 0;
    virtual ISQLRow *FetchRow() = 0;
    virtual ISQLRow *CurrentRow() = 0;
    virtual bool Rewind() = 0;
    virtual int GetFieldType(unsigned int field) = 0;
    virtual const char *GetString(unsigned int columnId, size_t *length = nullptr) = 0;
    virtual size_t GetDataSize(unsigned int columnId) = 0;
    virtual float GetFloat(unsigned int columnId) = 0;
    virtual int GetInt(unsigned int columnId) = 0;
    virtual bool IsNull(unsigned int columnId) = 0;
};

class ISQLQuery
{
public:
    virtual ISQLResult *GetResultSet() = 0;
    virtual bool FetchMoreResults() = 0;
    virtual unsigned int GetInsertId() = 0;
    virtual unsigned int GetAffectedRows() = 0;
};

class ISQLConnection
{
public:
    virtual void Connect(ConnectCallbackFunc callback) = 0;
    virtual void Query(char *query, QueryCallbackFunc callback) = 0;
    virtual void Query(const char *query, QueryCallbackFunc callback, ...) = 0;
    virtual void ExecuteTransaction(Transaction txn, TransactionSuccessCallbackFunc success, TransactionFailureCallbackFunc failure) = 0;
    virtual void Destroy() = 0;
    virtual std::string Escape(char *string) = 0;
    virtual std::string Escape(const char *string) = 0;
};

class IMySQLClient;
class ISQLiteClient;

class ISQLInterface
{
public:
    virtual IMySQLClient *GetMySQLClient() = 0;
    virtual ISQLiteClient *GetSQLiteClient() = 0;
};
