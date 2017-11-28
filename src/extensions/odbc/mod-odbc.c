//
//  File: %mod-odbc.c
//  Summary: "Interface from REBOL3 to ODBC"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2010-2011 Christian Ensel
// Copyright 2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This file provides the natives (OPEN-CONNECTION, INSERT-ODBC, etc.) which
// are used as the low-level support to implement the higher level services
// of the ODBC scheme (which are written in Rebol).
//
// The driver is made to handle queries which look like:
//
//     ["select * from tables where (name = ?) and (age = ?)" {Brian} 42]
//
// The ? notation for substitution points is what is known as a "parameterized
// query".  The reason it is supported at the driver level (instead of making
// the usermode Rebol code merge into a single string) is to make it easier to
// defend against SQL injection attacks.  This way, the scheme code does not
// need to worry about doing SQL-syntax-aware string escaping.
//
// The version of ODBC that this is written to use is 3.0, which was released
// around 1995.  At time of writing (2017) it is uncommon to encounter ODBC
// systems that don't implement at least that.
//

#ifdef TO_WINDOWS
    #include <windows.h>
    #undef IS_ERROR
#endif

#include "sys-core.h"
#include "sys-ext.h"
#include "tmp-mod-odbc-first.h"


#include <sql.h>
#include <sqlext.h>


//
// https://docs.microsoft.com/en-us/sql/odbc/reference/appendixes/c-data-types
//
// The C mappings do not necessarily ensure things like SQLHANDLE (e.g. a
// SQLHDBC or SQLHENV) are pointers, or that SQL_NULL_HANDLE is NULL.  This
// code would have to be modified on a platform where these were structs.
//
#if defined(__cplusplus) && __cplusplus >= 201103L
    static_assert(
        std::is_pointer<SQLHANDLE>::value,
        "ODBC module code currently assumes SQLHANDLE is a pointer type"
    );
    static_assert(
        NULL == SQL_NULL_HANDLE,
        "ODBC module code currently asssumes SQL_NULL_HANDLE is NULL"
    );
#endif


enum GET_CATALOG {
    GET_CATALOG_TABLES,
    GET_CATALOG_COLUMNS,
    GET_CATALOG_TYPES
}; // Used with ODBC_GetCatalog

typedef struct {
    SQLULEN column_size;
    SQLPOINTER buffer;
    SQLULEN buffer_size;
    SQLLEN length;
} PARAMETER; // For binding parameters

typedef struct {
    REBVAL *title_word; // a WORD!
    SQLSMALLINT sql_type;
    SQLSMALLINT c_type;
    SQLULEN column_size;
    SQLPOINTER buffer;
    SQLULEN buffer_size;
    SQLLEN length;
    SQLSMALLINT precision;
    SQLSMALLINT nullable;
    REBOOL is_unsigned;
} COLUMN; // For describing columns


//=////////////////////////////////////////////////////////////////////////=//
//
// ODBC ERRORS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// It's possible for ODBC to provide extra information if you know the type
// and handle that experienced the last error.
//
// !!! Review giving these errors better object-like identities instead of
// just being strings.
//

REBCTX *Error_ODBC(SQLSMALLINT handleType, SQLHANDLE handle) {
    SQLWCHAR state[6];
    SQLINTEGER native;

    const SQLSMALLINT buffer_size = 4086;
    SQLWCHAR message[4086];
    SQLSMALLINT message_len = 0;

    SQLRETURN rc = SQLGetDiagRecW(
        handleType,
        handle,
        1,
        state,
        &native,
        message,
        buffer_size,
        &message_len
    );

    DECLARE_LOCAL (string);

    if (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO) {
        REBVAL *temp = rebSizedStringUCS2(message, message_len);
        Move_Value(string, temp);
        rebRelease(temp);
    }
    else {
        REBVAL *temp = rebString("unknown ODBC error");
        Move_Value(string, temp);
        rebRelease(temp);
    }

    return Error(RE_USER, string, END);
}

#define Error_ODBC_Stmt(hstmt) \
    Error_ODBC(SQL_HANDLE_STMT, hstmt)

#define Error_ODBC_Env(henv) \
    Error_ODBC(SQL_HANDLE_ENV, henv)

#define Error_ODBC_Dbc(hdbc) \
    Error_ODBC(SQL_HANDLE_DBC, hdbc)


// These are the cleanup functions for the handles that will be called if the
// GC notices no one is using them anymore (as opposed to being explicitly
// called by a close operation).
//
// !!! There may be an ordering issue, that closing the environment before
// closing a database connection (for example) causes errors...so the handles
// may actually need to account for that by linking to each other's managed
// array and cleaning up their dependent handles before freeing themselves.

static void cleanup_hdbc(const REBVAL *v) {
    SQLHDBC hdbc = cast(SQLHDBC, VAL_HANDLE_VOID_POINTER(v));
    if (hdbc == SQL_NULL_HANDLE)
        return; // already cleared out by CLOSE-ODBC

    SQLDisconnect(hdbc);
    SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
}

static void cleanup_henv(const REBVAL *v) {
    SQLHENV henv = cast(SQLHENV, VAL_HANDLE_VOID_POINTER(v));
    if (henv == SQL_NULL_HANDLE)
        return; // already cleared out by CLOSE-ODBC

    SQLFreeHandle(SQL_HANDLE_ENV, henv);
}


//
//  open-connection: native/export [
//
//      return: [logic!]
//          {Always true if success}
//      connection [object!]
//          {Template object for HENV and HDBC handle fields to set}
//      spec [string!]
//          {ODBC connection string, e.g. commonly "Dsn=DatabaseName"}
//  ]
//  new-words: [henv hdbc]
//
REBNATIVE(open_connection)
//
// !!! The original R3 extension code used this method of having the client
// code pass in an object vs. just returning an object, presumably because
// making new objects from inside the native code and naming the fields was
// too hard and/or undocumented.  It shouldn't be difficult to change.
{
    INCLUDE_PARAMS_OF_OPEN_CONNECTION;

    // We treat ODBC's SQLWCHAR type (wide SQL char) as 2 bytes UCS2, even on
    // platforms where wchar_t is larger.  This gives unixODBC compatibility:
    //
    // https://stackoverflow.com/a/7552533/211160
    //
    // "unixODBC follows MS ODBC Driver manager and has SQLWCHARs as 2 bytes
    //  UCS2 encoded. iODBC I believe uses wchar_t (this is based on
    //  attempting to support iODBC in DBD::ODBC)"
    //
    // This requirement meant adding UCS2 support to the API.  There are
    // wchar_t-based APIs as well, so a motivated individual could make some
    // shims to allow picking which Rebol string API to use if they wanted
    // it to work with char_t-based ODBC where sizeof(wchar_t) != sizeof(u32)
    //
    assert(sizeof(SQLWCHAR) == sizeof(u32));

    SQLRETURN rc;

    // Allocate the environment handle, and set its version to ODBC3
    //
    SQLHENV henv;
    rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
        fail (Error_ODBC_Env(SQL_NULL_HENV));

    rc = SQLSetEnvAttr(
        henv,
        SQL_ATTR_ODBC_VERSION,
        cast(SQLPOINTER, SQL_OV_ODBC3),
        0 // StringLength (ignored for this attribute)
    );
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
        REBCTX *error = Error_ODBC_Env(henv);
        SQLFreeHandle(SQL_HANDLE_ENV, henv);
        fail (error);
    }

    // Allocate the connection handle, with login timeout of 5 seconds (why?)
    //
    SQLHDBC hdbc;
    rc = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
        REBCTX *error = Error_ODBC_Env(henv);
        SQLFreeHandle(SQL_HANDLE_ENV, henv);
        fail (error);
    }

    rc = SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, cast(SQLPOINTER, 5), 0);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
        REBCTX *error = Error_ODBC_Env(henv);
        SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
        SQLFreeHandle(SQL_HANDLE_ENV, henv);
        fail (error);
    }

    // Connect to the Driver, using the converted connection string
    //
    REBCNT connect_len;
    SQLWCHAR *connect = rebSpellingOfAllocUCS2(&connect_len, ARG(spec));

    SQLSMALLINT out_connect_len;
    rc = SQLDriverConnectW(
        hdbc, // ConnectionHandle
        NULL, // WindowHandle
        connect, // InConnectionString
        cast(SQLSMALLINT, connect_len), // StringLength1
        NULL, // OutConnectionString (not interested in this)
        0, // BufferLength (again, not interested)
        &out_connect_len, // StringLength2Ptr (gets returned anyway)
        SQL_DRIVER_NOPROMPT // DriverCompletion
    );
    OS_FREE(connect);

    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
        REBCTX *error = Error_ODBC_Env(henv);
        SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
        SQLFreeHandle(SQL_HANDLE_ENV, henv);
        fail (error);
    }

    REBCTX *connection = VAL_CONTEXT(ARG(connection));
    Init_Handle_Managed(
        Sink_Field(connection, ODBC_WORD_HENV),
        henv, // pointer
        0, // size
        &cleanup_henv
    );
    Init_Handle_Managed(
        Sink_Field(connection, ODBC_WORD_HDBC),
        hdbc, // pointer
        0, // size
        &cleanup_hdbc
    );

    return R_TRUE;
}


//
//  open-statement: native/export [
//
//      return: [logic!]
//      connection [object!]
//      statement [object!]
//  ]
//  new-words: [hstmt]
//
REBNATIVE(open_statement)
//
// !!! Similar to previous routines, this takes an empty statement object in
// to initialize.
{
    INCLUDE_PARAMS_OF_OPEN_STATEMENT;

    REBCTX *connection = VAL_CONTEXT(ARG(connection));
    SQLHDBC hdbc = cast(SQLHDBC, VAL_HANDLE_VOID_POINTER(
        Get_Typed_Field(connection, ODBC_WORD_HDBC, REB_HANDLE)
    ));

    SQLRETURN rc;

    SQLHSTMT hstmt;
    rc = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
        fail (Error_ODBC_Dbc(hdbc));

    REBCTX *statement = VAL_CONTEXT(ARG(statement));
    Init_Handle_Simple(
        Sink_Field(statement, ODBC_WORD_HSTMT),
        hstmt, // pointer
        0 // len
    ); 

    return R_TRUE;
}


// The buffer at *ParameterValuePtr SQLBindParameter binds to is deferred
// buffer, and so is the StrLen_or_IndPtr. They need to be vaild over until
// Execute or ExecDirect are called.
//
// Bound parameters are a Rebol value of incoming type.  These values inform
// the dynamic allocation of a buffer for the parameter, pre-filling it with
// the content of the value.
//
SQLRETURN ODBC_BindParameter(
    SQLHSTMT hstmt,
    PARAMETER *p,
    SQLUSMALLINT number, // parameter number
    const REBVAL *v
){
    assert(number != 0);

    SQLSMALLINT c_type;
    SQLSMALLINT sql_type;

    p->length = 0;
    p->column_size = 0;
    TRASH_POINTER_IF_DEBUG(p->buffer); // must be set

    switch (VAL_TYPE(v)) {
    case REB_BLANK: {
        p->buffer = NULL;
        p->buffer_size = 0;
        p->length = 0;

        c_type = SQL_C_DEFAULT;
        sql_type = SQL_NULL_DATA;
        break; }

    case REB_LOGIC: {
        p->buffer_size = sizeof(unsigned char);
        p->buffer = OS_ALLOC_N(char, p->buffer_size);

        *cast(unsigned char*, p->buffer) = VAL_LOGIC(v);

        c_type = SQL_C_BIT;
        sql_type = SQL_BIT;
        break; }

    case REB_INTEGER: {
        p->buffer_size = sizeof(REBI64);
        p->buffer = OS_ALLOC_N(char, p->buffer_size);

        *cast(REBI64*, p->buffer) = VAL_INT64(v);

        c_type = SQL_C_SBIGINT; // Rebol's INTEGER! type is signed
        sql_type = SQL_INTEGER;
        break; }

    case REB_DECIMAL: {
        p->buffer_size = sizeof(double);
        p->buffer = OS_ALLOC_N(char, p->buffer_size);

        *cast(double*, p->buffer) = VAL_DECIMAL(v);

        c_type = SQL_C_DOUBLE;
        sql_type = SQL_DOUBLE;
        break; }

    case REB_TIME: {
        p->buffer_size = sizeof(TIME_STRUCT);
        p->buffer = OS_ALLOC_N(char, p->buffer_size);

        TIME_STRUCT *time = cast(TIME_STRUCT*, p->buffer);

        REB_TIMEF tf;
        Split_Time(VAL_NANO(v), &tf); // loses sign

        time->hour = tf.h;
        time->minute = tf.m;
        time->second = tf.s; // cast(REBDEC, tf.s) + (tf.n * NANO) for precise

        p->length = p->column_size = sizeof(TIME_STRUCT);

        c_type = SQL_C_TYPE_TIME;
        sql_type = SQL_TYPE_TIME;
        break; }

    case REB_DATE: {
        if (NOT_VAL_FLAG(v, DATE_FLAG_HAS_TIME)) {
            p->buffer_size = sizeof(DATE_STRUCT);
            p->buffer = OS_ALLOC_N(char, p->buffer_size);

            DATE_STRUCT *date = cast(DATE_STRUCT*, p->buffer);

            date->year = VAL_YEAR(v);
            date->month = VAL_MONTH(v);
            date->day = VAL_DAY(v);

            p->length = p->column_size = sizeof(DATE_STRUCT);

            c_type = SQL_C_TYPE_DATE;
            sql_type = SQL_TYPE_DATE;
        }
        else {
            p->buffer_size = sizeof(TIMESTAMP_STRUCT);
            p->buffer = OS_ALLOC_N(char, p->buffer_size);

            TIMESTAMP_STRUCT *stamp = cast(TIMESTAMP_STRUCT*, p->buffer);

            stamp->year = VAL_YEAR(v);
            stamp->month = VAL_MONTH(v);
            stamp->day = VAL_DAY(v);
            stamp->hour = VAL_SECS(v) / 3600;
            stamp->minute = (VAL_SECS(v) % 3600) / 60;
            stamp->second = VAL_SECS(v) % 60;
            stamp->fraction = VAL_NANO(v) % SEC_SEC;

            c_type = SQL_C_TYPE_TIMESTAMP;
            sql_type = SQL_TYPE_TIMESTAMP;
        }
        break; }

    case REB_STRING: {
        REBCNT length;
        SQLWCHAR *chars = rebSpellingOfAllocUCS2(&length, v);

        p->buffer_size = sizeof(SQLWCHAR) * length;
        
        p->length = p->column_size = cast(SQLSMALLINT, 2 * length);

        c_type = SQL_C_WCHAR;
        sql_type = SQL_VARCHAR;
        p->buffer = chars;
        break; }

    case REB_BINARY: {
        p->buffer_size = VAL_LEN_AT(v); // sizeof(char) guaranteed to be 1
        p->buffer = ALLOC_N(char, p->buffer_size);

        if (p->buffer == NULL)
            fail ("Couldn't allocate parameter buffer!");

        memcpy(p->buffer, VAL_BIN_AT(v), p->buffer_size);

        p->length = p->column_size = p->buffer_size;

        c_type = SQL_C_BINARY;
        sql_type = SQL_VARBINARY;
        break; }

    default: // used to do same as REB_BLANK, should it?
        fail ("Non-SQL type used in parameter binding");
    }

    SQLRETURN rc = SQLBindParameter(
        hstmt, // StatementHandle
        number, // ParameterNumber
        SQL_PARAM_INPUT, // InputOutputType
        c_type, // ValueType
        sql_type, // ParameterType
        p->column_size, // ColumnSize
        0, // DecimalDigits
        p->buffer, // ParameterValuePtr
        p->buffer_size, // BufferLength
        &p->length // StrLen_Or_IndPtr
    );
   
    return rc;
}


SQLRETURN ODBC_GetCatalog(
    SQLHSTMT hstmt,
    enum GET_CATALOG which,
    REBVAL *block
){
    assert(IS_BLOCK(block)); // !!! Should it ensure exactly 4 items?

    SQLSMALLINT length[4];
    SQLWCHAR *pattern[4];

    int arg;
    for (arg = 0; arg < 4; arg++) {
        //
        // !!! What if not at head?  Original code seems incorrect, because
        // it passed the array at the catalog word, which is not a string.
        //
        REBVAL *value = rebPickIndexed(block, arg + 1);
        if (IS_STRING(value)) {
            REBCNT len;
            pattern[arg] = rebSpellingOfAllocUCS2(&len, KNOWN(value));
            length[arg] = len;
        }
        else {
            length[arg] = 0;
            pattern[arg] = NULL;
        }
        rebRelease(value);
    }

    SQLRETURN rc;

    switch (which) {
    case GET_CATALOG_TABLES:
        rc = SQLTablesW(
            hstmt,
            pattern[2], length[2], // catalog
            pattern[1], length[1], // schema
            pattern[0], length[0], // table
            pattern[3], length[3] // type
        );
        break;

    case GET_CATALOG_COLUMNS:
        rc = SQLColumnsW(
            hstmt,
            pattern[3], length[3], // catalog
            pattern[2], length[2], // schema
            pattern[0], length[0], // table
            pattern[1], length[1]  // column
        );
        break;

    case GET_CATALOG_TYPES:
        rc = SQLGetTypeInfoW(hstmt, SQL_ALL_TYPES);
        break;

    default:
        panic ("Invalid GET_CATALOG_XXX value");
    }

    for (arg = 0; arg < 4; arg++) {
        if (pattern[arg] != NULL)
            OS_FREE(pattern[arg]);
    }

    return rc;
}


/*
int ODBC_UnCamelCase(SQLWCHAR *source, SQLWCHAR *target) {
    int length = lstrlenW(source);
    int t = 0;
    WCHAR *hyphen = L"-";
    WCHAR *underscore = L"_";
    WCHAR *space = L" ";

    int s;
    for (s = 0; s < length; s++) {
        target[t++] =
            (source[s] == *underscore || source[s] == *space)
                ? *hyphen
                : towlower(source[s]);

        if (
            (
                s < length - 2
                && iswupper(source[s])
                && iswupper(source[s + 1])
                && iswlower(source[s + 2])
            ) || (
                s < length - 1
                && iswlower(source[s])
                && iswupper(source[s + 1])
            )
        ){
            target[t++] = *hyphen;
        }
    }

    target[t++] = 0;
    return t;
}
*/


#define COLUMN_TITLE_SIZE 255

//
// Sets up the COLUMNS description, retrieves column titles and descriptions
//
SQLRETURN ODBC_DescribeResults(
    SQLHSTMT hstmt,
    int num_columns,
    COLUMN *columns
){
    SQLSMALLINT col;
    for (col = 0; col < num_columns; ++col) {
        COLUMN *column = &columns[col];

        SQLWCHAR title[COLUMN_TITLE_SIZE];
        SQLSMALLINT title_length;

        SQLRETURN rc = SQLDescribeColW(
            hstmt,
            cast(SQLSMALLINT, col + 1),
            &title[0],
            COLUMN_TITLE_SIZE,
            &title_length,
            &column->sql_type,
            &column->column_size,
            &column->precision,
            &column->nullable
        );
        if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
            fail (Error_ODBC_Stmt(hstmt));

        // Numeric types may be signed or unsigned, which informs how to
        // interpret the bits that come back when turned into a Rebol value.
        // A separate API call is needed to detect that.

        SQLLEN numeric_attribute; // Note: SQLINTEGER won't work

        rc = SQLColAttributeW(
            hstmt, // StatementHandle
            cast(SQLSMALLINT, col + 1), // ColumnNumber
            SQL_DESC_UNSIGNED, // FieldIdentifier, see the other SQL_DESC_XXX
            NULL, // CharacterAttributePtr
            0, // BufferLength
            NULL, // StringLengthPtr
            &numeric_attribute // only parameter needed for SQL_DESC_UNSIGNED
        );
        if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
            fail (Error_ODBC_Stmt(hstmt));

        if (numeric_attribute == SQL_TRUE)
            column->is_unsigned = TRUE;
        else {
            assert(numeric_attribute == SQL_FALSE);
            column->is_unsigned = FALSE;
        }

        // Note: There was an "UnCamelCasing" distortion of the column names
        // given back by the database, which is presumably only desirable
        // when getting system descriptions (e.g. the properties when you
        // query metadata of a table) and was probably a Rebol2 compatibility
        // decision.
        //
        // int length = ODBC_UnCamelCase(column->title, title);

        column->title_word = rebSizedWordUCS2(title, title_length);
    }

    return SQL_SUCCESS;
}


// The way that ODBC returns row data is to set up the pointers where each
// column will write to once, then that memory is reused for each successive
// row fetch.  It's also possible to request some amount of data translation,
// e.g. that even if a column is storing a byte you can ask it to be read into
// a C 64-bit integer (for instance).  The process is called "column binding".
//
SQLRETURN ODBC_BindColumns(
    SQLHSTMT hstmt,
    int num_columns,
    COLUMN *columns
){    
    SQLSMALLINT col_num;
    for (col_num = 0; col_num < num_columns; ++col_num) {
        COLUMN *c = &columns[col_num];

        switch (c->sql_type) {
        case SQL_BIT:
            c->c_type = SQL_C_BIT;
            c->buffer_size = sizeof(unsigned char);
            break;

        case SQL_SMALLINT:
        case SQL_TINYINT:
        case SQL_INTEGER:
            if (c->is_unsigned) {
                c->c_type = SQL_C_ULONG;
                c->buffer_size = sizeof(unsigned long int);
            }
            else {
                c->c_type = SQL_C_SLONG;
                c->buffer_size = sizeof(signed long int);
            }
            break;

        // We could ask the driver to give all integer types back as BIGINT,
        // but driver support may be more sparse for this...so only use the
        // 64-bit datatypes if absolutely necessary.
        //
        case SQL_BIGINT:
            if (c->is_unsigned) {
                c->c_type = SQL_C_UBIGINT;
                c->buffer_size = sizeof(REBU64);
            }
            else {
                c->c_type = SQL_C_SBIGINT;
                c->buffer_size = sizeof(REBI64);
            }
            break;

        case SQL_DECIMAL:
        case SQL_NUMERIC:
        case SQL_REAL:
        case SQL_FLOAT:
        case SQL_DOUBLE:
            c->c_type = SQL_C_DOUBLE;
            c->buffer_size = sizeof(double);
            break;

        case SQL_TYPE_DATE:
            c->c_type = SQL_C_TYPE_DATE;
            c->buffer_size = sizeof(DATE_STRUCT);
            break;

        case SQL_TYPE_TIME:
            c->c_type = SQL_C_TYPE_TIME;
            c->buffer_size = sizeof(TIME_STRUCT);
            break;

        case SQL_TYPE_TIMESTAMP:
            c->c_type = SQL_C_TYPE_TIMESTAMP;
            c->buffer_size = sizeof(TIMESTAMP_STRUCT);
            break;

        case SQL_BINARY:
        case SQL_VARBINARY:
        case SQL_LONGVARBINARY:
            c->c_type = SQL_C_BINARY;
            c->buffer_size = sizeof(char) * c->column_size;
            break;

        case SQL_CHAR:
        case SQL_VARCHAR:
        case SQL_WCHAR:
        case SQL_WVARCHAR:
            //
            // !!! Should the non-wide char types use less space by asking
            // for regular SQL_C_CHAR?  Would it be UTF-8?  Latin1?
            //
            c->c_type = SQL_C_WCHAR;

            // "The driver counts the null-termination character when it
            // returns character data to *TargetValuePtr.  *TargetValuePtr
            // must therefore contain space for the null-termination character
            // or the driver will truncate the data"
            //
            c->buffer_size = sizeof(WCHAR) * (c->column_size + 1);
            break;

        case SQL_LONGVARCHAR:
        case SQL_WLONGVARCHAR:
            //
            // !!! Should the non-wide char type use less space by asking
            // for regular SQL_C_CHAR?  Would it be UTF-8?  Latin1?
            //
            c->c_type = SQL_C_WCHAR;

            // The LONG variants of VARCHAR have no length limit specified in
            // the schema:
            //
            // https://stackoverflow.com/a/9547441
            //
            // !!! The MS SQL driver reports column_size as 1073741824 (1GB)
            // which means allocating fields of this type would cause memory
            // problems.  For the moment, cap it at 32k...though if it can
            // be larger a truncation should be noted, and possibly refetched
            // with a larger buffer size.
            //
            // As above, the + 1 is for the terminator.
            //
            c->buffer_size = sizeof(WCHAR) * (32700 + 1);
            break;

        default: // used to allocate a character buffer based on column size
            fail ("Unknown column SQL_XXX type");
        }

        c->buffer = ALLOC_N(char, c->buffer_size);
        if (c->buffer == NULL)
            fail ("Couldn't allocate column buffer!");

        SQLRETURN rc = SQLBindCol(
            hstmt, // StatementHandle
            col_num + 1, // ColumnNumber
            c->c_type, // TargetType
            c->buffer, // TargetValuePtr
            c->buffer_size, // BufferLength (ignored for fixed-size items)
            &c->length // StrLen_Or_Ind (SQLFetch will write here)
        );

        if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
            fail (Error_ODBC_Stmt(hstmt));
    }

    return SQL_SUCCESS;
}


//
//  insert-odbc: native/export [
//
//  {Executes SQL statements (prepare on first pass, executes conservatively)}
//
//      return: [integer! block!]
//          {Row count for row-changes, BLOCK! of column titles for selects}
//      statement [object!]
//      sql [block!]
//          {Dialect beginning with TABLES, COLUMNS, TYPES, or a SQL STRING!}
//  ]
//  new-words: [tables columns types titles string]
//
REBNATIVE(insert_odbc)
{
    INCLUDE_PARAMS_OF_INSERT_ODBC;

    REBCTX *statement = VAL_CONTEXT(ARG(statement));
    SQLHSTMT hstmt = VAL_HANDLE_POINTER(
        SQLHSTMT,
        Get_Typed_Field(statement, ODBC_WORD_HSTMT, REB_HANDLE)
    );

    SQLRETURN rc;

    rc = SQLFreeStmt(hstmt, SQL_RESET_PARAMS); // !!! check rc?
    rc = SQLCloseCursor(hstmt); // !!! check rc?

    // !!! Some code here would set the number of rows, but was commented out
    // saying it was "in the wrong place" (?)
    //
    // SQLULEN max_rows = 0;
    // rc = SQLSetStmtAttr(hstmt, SQL_ATTR_MAX_ROWS, &max_rows, SQL_IS_POINTER);
    // if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
    //     fail (Error_ODBC_Stmt(hstmt));


    //=//// MAKE SQL REQUEST FROM DIALECTED SQL BLOCK /////////////////////=//
    //
    // The block passed in is used to form a query.

    REBCNT sql_index = 1;
    REBVAL *value = rebPickIndexed(ARG(sql), sql_index);

    if (IS_VOID(value)) {
        rebRelease(value);
        fail ("Empty array passed for SQL dialect");
    }

    REBOOL use_cache = FALSE;

    switch (VAL_TYPE(value)) {
    case REB_WORD: {
        // Execute catalog function, when first element in the argument block
        // is a (catalog) word

        if (SAME_STR(VAL_WORD_SPELLING(value), ODBC_WORD_TABLES))
            rc = ODBC_GetCatalog(hstmt, GET_CATALOG_TABLES, ARG(sql));
        else if (SAME_STR(VAL_WORD_SPELLING(value), ODBC_WORD_COLUMNS))
            rc = ODBC_GetCatalog(hstmt, GET_CATALOG_COLUMNS, ARG(sql));
        else if (SAME_STR(VAL_WORD_SPELLING(value), ODBC_WORD_TYPES))
            rc = ODBC_GetCatalog(hstmt, GET_CATALOG_TYPES, ARG(sql));
        else
            fail ("Catalog must be TABLES, COLUMNS, or TYPES");
        break; }

    case REB_STRING: {
        // Prepare/Execute statement, when first element in the block is a
        // (statement) string

        // Compare with previously prepared statement, and if not the same,
        // then prepare a new statement.
        //
        REBVAL *previous = Get_Field(statement, ODBC_WORD_STRING);

        if (IS_STRING(previous)) {
            if (0 == Compare_String_Vals(value, previous, TRUE))
                use_cache = TRUE;
        }
        else
            assert(IS_BLANK(previous));

        if (NOT(use_cache)) {
            REBCNT length;
            SQLWCHAR *sql_string = rebSpellingOfAllocUCS2(&length, value);

            rc = SQLPrepareW(hstmt, sql_string, cast(SQLSMALLINT, length));
            if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
                fail (Error_ODBC_Stmt(hstmt));

            OS_FREE(sql_string);

            // Remember statement string handle, but keep a copy since it
            // may be mutated by the user.
            //
            // !!! Could re-use value with existing series if read only
            //
            Move_Value(
                Sink_Field(statement, ODBC_WORD_STRING),
                rebCopyExtra(value, 0)
            );
        }

        // The SQL string may contain ? characters, which indicates that it is
        // a parameterized query.  The separation of the parameters into a
        // different quarantined part of the query is to protect against SQL
        // injection.

        REBCNT num_params = rebLengthOf(ARG(sql)) - 1; // don't count the sql
        rebRelease(value);
        value = rebPickIndexed(ARG(sql), ++sql_index);

        PARAMETER *params = NULL;
        if (num_params != 0) {
            params = OS_ALLOC_N(PARAMETER, num_params);
            if (params == NULL)
                fail ("Couldn't allocate parameter buffer!");

            REBCNT n = 0;
            while (n < num_params) {
                rc = ODBC_BindParameter(
                    hstmt,
                    &params[n],
                    n + 1,
                    value
                );
                rebRelease(value);
                if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
                    fail (Error_ODBC_Stmt(hstmt));
                
                ++n;
                value = rebPickIndexed(ARG(sql), ++sql_index);
            }

            assert(IS_END(value));
        }

        // Execute statement, but don't check result code until after the
        // parameters and their data buffers have been freed.
        //
        rc = SQLExecute(hstmt);

        if (num_params != 0) {
            REBCNT n;
            for (n = 0; n < num_params; ++n) {
                if (params[n].buffer != NULL)
                    OS_FREE(params[n].buffer);
            }
            OS_FREE(params);
        }

        if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
            fail (Error_ODBC_Stmt(hstmt));

        break; }

    default:
        fail ("SQL dialect currently must start with WORD! or STRING! value");
    }

    //=//// RETURN RECORD COUNT IF NO RESULT ROWS /////////////////////////=//
    //
    // Insert/Update/Delete statements do not return records, and this is
    // indicated by a 0 count for columns in the return result.

    SQLSMALLINT num_columns;
    rc = SQLNumResultCols(hstmt, &num_columns);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
        fail (Error_ODBC_Stmt(hstmt));

    if (num_columns == 0) {
        SQLLEN num_rows;
        rc = SQLRowCount(hstmt, &num_rows);
        if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
            fail (Error_ODBC_Stmt(hstmt));

        Init_Integer(D_OUT, num_rows);
        return R_OUT;
    }

    //=//// RETURN CACHED TITLES BLOCK OR REBUILD IF NEEDED ///////////////=//
    //
    // A SELECT statement or a request for a catalog listing of tables or
    // other database features will generate rows.  However, this routine only
    // returns the titles of the columns.  COPY-ODBC is used to actually get
    // the values.
    //
    // !!! The reason it is factored this way might have dealt with the idea
    // that you could want to have different ways of sub-querying the results
    // vs. having all the records spewed to you.  The results might also be
    // very large so you don't want them all in memory at once.  The COPY-ODBC
    // routine does this.

    if (use_cache) {
        Move_Value(
            D_OUT,
            Get_Typed_Field(statement, ODBC_WORD_TITLES, REB_BLOCK)
        );
        return R_OUT;
    }

    COLUMN *columns;
    REBVAL *field = Get_Field(statement, ODBC_WORD_COLUMNS);
    if (IS_HANDLE(field)) {
        columns = VAL_HANDLE_POINTER(COLUMN, field);
        OS_FREE(columns);
    }
    else
        assert(IS_BLANK(field));

    columns = OS_ALLOC_N(COLUMN, num_columns);
    if (columns == NULL)
        fail ("Couldn't allocate column buffers!");

    Init_Handle_Simple(
        Sink_Field(statement, ODBC_WORD_COLUMNS),
        columns,
        num_columns
    );

    rc = ODBC_DescribeResults(hstmt, num_columns, columns);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
        fail (Error_ODBC_Stmt(hstmt));

    rc = ODBC_BindColumns(hstmt, num_columns, columns);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
        fail (Error_ODBC_Stmt(hstmt));

    REBARR *titles = Make_Array(num_columns);
    int col;
    for (col = 0; col < num_columns; ++col)
        Move_Value(ARR_AT(titles, col), columns[col].title_word);
    TERM_ARRAY_LEN(titles, num_columns);

    // remember column titles if next call matches, return them as the result
    //
    Init_Block(Sink_Field(statement, ODBC_WORD_TITLES), titles);
    Init_Block(D_OUT, titles);
    return R_OUT;
}


//
// A query will fill a column's buffer with data.  This data can be 
// reinterpreted as a Rebol value.  Successive queries for records reuse the
// buffer for a column.
//
REBVAL *ODBC_Column_To_Rebol_Value(COLUMN *col) {
    if (col->length == SQL_NULL_DATA)
        return rebBlank();

    switch (col->sql_type) {
    case SQL_TINYINT: // signed: –128..127, unsigned: 0..255
    case SQL_SMALLINT: // signed: –32,768..32,767, unsigned: 0..65,535
    case SQL_INTEGER: //  signed: –2[31]..2[31] – 1, unsigned: 0..2[32] – 1
        //
        // ODBC was asked at column binding time to give back *most* integer
        // types as SQL_C_SLONG or SQL_C_ULONG, regardless of actual size.
        //
        if (col->is_unsigned)
            return rebInteger(*cast(unsigned long*, col->buffer));

        return rebInteger(*cast(signed long*, col->buffer));

    case SQL_BIGINT: // signed: –2[63]..2[63] – 1, unsigned: 0..2[64] – 1
        //
        // Special exception made for big integers.
        //
        if (col->is_unsigned) {
            if (*cast(REBU64*, col->buffer) > MAX_I64)
                fail ("INTEGER! can't hold some unsigned 64-bit values");

            return rebInteger(*cast(REBU64*, col->buffer));
        }

        return rebInteger(*cast(REBI64*, col->buffer));

    case SQL_REAL: // precision 24
    case SQL_DOUBLE: // precision 53
    case SQL_FLOAT: // FLOAT(p) has at least precision p
    case SQL_NUMERIC: // NUMERIC(p,s) has exact? precision p and scale s
    case SQL_DECIMAL: // DECIMAL(p,s) has at least precision p and scale s
        //
        // ODBC was asked at column binding time to give back all floating
        // point types as SQL_C_DOUBLE, regardless of actual size.
        //
        return rebDecimal(*cast(double*, col->buffer));

    case SQL_TYPE_DATE: {
        DATE_STRUCT *date = cast(DATE_STRUCT*, col->buffer);
        return rebDateYMD(date->year, date->month, date->day); }

    case SQL_TYPE_TIME: {
        //
        // The TIME_STRUCT in ODBC does not contain a fraction/nanosecond
        // component.  Hence a TIME(7) might be able to store 17:32:19.123457
        // but when it is retrieved it will just be 17:32:19
        //
        TIME_STRUCT *time = cast(TIME_STRUCT*, col->buffer);
        return rebTimeHMS(time->hour, time->minute, time->second); }

    // Note: It's not entirely clear how to work with timezones in ODBC, there
    // is a datatype called SQL_SS_TIMESTAMPOFFSET_STRUCT which extends
    // TIMESTAMP_STRUCT with timezone_hour and timezone_minute.  Someone can
    // try and figure this out in the future if they are so inclined.

    case SQL_TYPE_TIMESTAMP: {
        TIMESTAMP_STRUCT *stamp = cast(TIMESTAMP_STRUCT*, col->buffer);

        REBVAL *date = rebDateYMD(stamp->year, stamp->month, stamp->day);

        // stamp->fraction is billionths of a second, e.g. nanoseconds
        //
        REBVAL *time = rebTimeNano(
            stamp->fraction + SECS_TO_NANO(
                stamp->hour * 3600
                + stamp->minute * 60
                + stamp->second
            )
        );
        
        REBVAL *datetime = rebDateTime(date, time);
        rebRelease(date);
        rebRelease(time);
        return datetime; }

    case SQL_BIT:
        //
        // Note: MySQL ODBC returns -2 for sql_type when a field is BIT(n)
        // where n != 1, as opposed to SQL_BIT and column_size of n.  See
        // remarks on the fail() below.
        //
        if (col->column_size != 1)
            fail ("BIT(n) fields are only supported for n = 1");

        return rebLogic(LOGICAL(*cast(unsigned char*, col->buffer)));

    case SQL_BINARY:
    case SQL_VARBINARY:
    case SQL_LONGVARBINARY:
        return rebBinary(col->buffer, col->length);

    case SQL_CHAR:
    case SQL_VARCHAR:
    case SQL_LONGVARCHAR:
    case SQL_WCHAR:
    case SQL_WVARCHAR:
    case SQL_WLONGVARCHAR:
    case SQL_GUID:
        return rebStringUCS2(cast(SQLWCHAR*, col->buffer)); // !!! pass size?

    default:
        break;
    }

    // Note: This happens with BIT(2) and the MySQL ODBC driver, which
    // reports a sql_type of -2 for some reason.
    //
    fail ("Unsupported SQL_XXX type returned from query");
}


//
//  copy-odbc: native/export [
//
//      return: [block!]
//          {Result-set block of row blocks for selects and catalog functions}
//      statement [object!]
//      length [integer! blank!]
//  ]
//
REBNATIVE(copy_odbc)
{
    INCLUDE_PARAMS_OF_COPY_ODBC;

    REBCTX *statement = VAL_CONTEXT(ARG(statement));

    SQLHSTMT hstmt = cast(SQLHSTMT,
        VAL_HANDLE_VOID_POINTER(
            Get_Typed_Field(statement, ODBC_WORD_HSTMT, REB_HANDLE)
        )
    );

    COLUMN *columns = VAL_HANDLE_POINTER(
        COLUMN,
        Get_Typed_Field(statement, ODBC_WORD_COLUMNS, REB_HANDLE)
    );

    if (hstmt == SQL_NULL_HANDLE || columns == NULL)
        fail ("Invalid statement object!");

    SQLRETURN rc;

    SQLSMALLINT num_columns;
    rc = SQLNumResultCols(hstmt, &num_columns);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
        fail (Error_ODBC_Stmt(hstmt));

    SQLULEN num_rows;
    if (IS_BLANK(ARG(length)))
        num_rows = -1; // compares 0 based row against, so this never matches
    else {
        assert(IS_INTEGER(ARG(length)));
        num_rows = VAL_INT32(ARG(length));
    }

    REBDSP dsp_orig = DSP;

    // Fetch columns
    //
    SQLULEN row = 0;
    while ((row != num_rows) && (SQLFetch(hstmt) != SQL_NO_DATA)) {
        REBARR *record = Make_Array(num_columns);

        SQLSMALLINT col;
        for (col = 0; col < num_columns; ++col) {
            REBVAL *temp = ODBC_Column_To_Rebol_Value(&columns[col]);
            Move_Value(ARR_AT(record, col), temp);
            rebRelease(temp);
        }
        TERM_ARRAY_LEN(record, num_columns);

        DS_PUSH_TRASH;
        Init_Block(DS_TOP, record);
        ++row;
    }
    
    Init_Block(D_OUT, Pop_Stack_Values(dsp_orig));
    return R_OUT;
}


//
//  update-odbc: native/export [
//
//      connection [object!]
//      access [logic!]
//      commit [logic!]
//  ]
//
REBNATIVE(update_odbc)
{
    INCLUDE_PARAMS_OF_UPDATE_ODBC;

    REBCTX *connection = VAL_CONTEXT(ARG(connection));

    // Get connection handle
    //
    SQLHDBC hdbc = cast(SQLHDBC, VAL_HANDLE_VOID_POINTER(
        Get_Typed_Field(connection, ODBC_WORD_HDBC, REB_HANDLE)
    ));
    SQLRETURN rc;

    rc = SQLSetConnectAttr(
        hdbc,
        SQL_ATTR_ACCESS_MODE,
        cast(
            SQLPOINTER*,
            cast(REBUPT, IS_TRUTHY(ARG(access))
                ? SQL_MODE_READ_WRITE
                : SQL_MODE_READ_ONLY)
        ),
        SQL_IS_UINTEGER
    );
    
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
        fail (Error_ODBC_Dbc(hdbc));

    rc = SQLSetConnectAttr(
        hdbc,
        SQL_ATTR_AUTOCOMMIT,
        cast(
            SQLPOINTER*,
            cast(REBUPT, IS_TRUTHY(ARG(commit))
                ? SQL_AUTOCOMMIT_ON
                : SQL_AUTOCOMMIT_OFF)
        ),
        SQL_IS_UINTEGER
    );

    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
        fail (Error_ODBC_Dbc(hdbc));

    return R_TRUE;
}


//
//  close-statement: native/export [
//
//      return: [logic!]
//      statement [object!]
//  ]
//
REBNATIVE(close_statement)
{
    INCLUDE_PARAMS_OF_CLOSE_STATEMENT;

    REBCTX *statement = VAL_CONTEXT(ARG(statement));

    REBVAL *field;

    field = Get_Field(statement, ODBC_WORD_HSTMT);
    if (IS_HANDLE(field)) {
        SQLHSTMT hstmt = cast(SQLHSTMT, VAL_HANDLE_VOID_POINTER(field));
        assert(hstmt != NULL);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        SET_HANDLE_POINTER(field, SQL_NULL_HANDLE); // avoid GC cleanup free 
        Init_Blank(field);
    }
    else
        assert(IS_BLANK(field));

    field = Get_Field(statement, ODBC_WORD_COLUMNS);
    if (IS_HANDLE(field)) {
        COLUMN *columns = VAL_HANDLE_POINTER(COLUMN, field);
        assert(columns != NULL);
        REBCNT num_columns = VAL_HANDLE_LEN(field);

        REBCNT col;
        for (col = 0; col < num_columns; ++col)
            rebRelease(columns[col].title_word);

        OS_FREE(columns);
        SET_HANDLE_POINTER(field, NULL);
        Init_Blank(field);
    }
    else
        assert(IS_BLANK(field));

    return R_TRUE;
}


//
//  close-connection: native/export [
//
//      return: [logic!]
//      connection [object!]
//  ]
//
REBNATIVE(close_connection)
{
    INCLUDE_PARAMS_OF_CLOSE_CONNECTION;

    REBCTX *connection = VAL_CONTEXT(ARG(connection));

    REBVAL *field;

    // Close the database connection before the environment, since the
    // connection was opened from the environment.
    //
    field = Get_Field(connection, ODBC_WORD_HDBC);
    if (IS_HANDLE(field)) {
        SQLHDBC hdbc = cast(SQLHDBC, VAL_HANDLE_VOID_POINTER(field));
        assert(hdbc != NULL);
        SQLDisconnect(hdbc);
        SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
        SET_HANDLE_POINTER(field, SQL_NULL_HANDLE); // avoid GC cleanup free
        Init_Blank(field);
    }
    else
        assert(IS_BLANK(field));

    // Close the environment
    //
    field = Get_Field(connection, ODBC_WORD_HENV);
    if (IS_HANDLE(field)) {
        SQLHENV henv = cast(SQLHENV, VAL_HANDLE_VOID_POINTER(field));
        assert(henv != NULL);
        SQLFreeHandle(SQL_HANDLE_ENV, henv);
        SET_HANDLE_POINTER(field, SQL_NULL_HANDLE); // avoid GC cleanup free
        Init_Blank(field);
    }
    else
        assert(IS_BLANK(field));

    return R_TRUE;
}


#include "tmp-mod-odbc-last.h"
