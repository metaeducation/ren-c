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

    if (rc == SQL_SUCCESS or rc == SQL_SUCCESS_WITH_INFO) {
        REBVAL *temp = rebLengthedTextW(message, message_len);
        Move_Value(string, temp);
        rebRelease(temp);
    }
    else {
        REBVAL *temp = rebText("unknown ODBC error");
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
//      spec [text!]
//          {ODBC connection string, e.g. commonly "Dsn=DatabaseName"}
//  ]
//
REBNATIVE(open_connection)
//
// !!! The original R3 extension code used this method of having the client
// code pass in an object vs. just returning an object, presumably because
// making new objects from inside the native code and naming the fields was
// too hard and/or undocumented.  It shouldn't be difficult to change.
{
    ODBC_INCLUDE_PARAMS_OF_OPEN_CONNECTION;

    // We treat ODBC's SQLWCHAR type (wide SQL char) as 2 bytes UCS2, even on
    // platforms where wchar_t is larger.  This gives unixODBC compatibility:
    //
    // https://stackoverflow.com/a/7552533/211160
    //
    // "unixODBC follows MS ODBC Driver manager and has SQLWCHARs as 2 bytes
    //  UCS-2 encoded. iODBC I believe uses wchar_t (this is based on
    //  attempting to support iODBC in DBD::ODBC)"
    //
    assert(sizeof(SQLWCHAR) == sizeof(REBWCHAR));

    SQLRETURN rc;

    // Allocate the environment handle, and set its version to ODBC3
    //
    SQLHENV henv;
    rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
    if (rc != SQL_SUCCESS and rc != SQL_SUCCESS_WITH_INFO)
        fail (Error_ODBC_Env(SQL_NULL_HENV));

    rc = SQLSetEnvAttr(
        henv,
        SQL_ATTR_ODBC_VERSION,
        cast(SQLPOINTER, SQL_OV_ODBC3),
        0 // StringLength (ignored for this attribute)
    );
    if (rc != SQL_SUCCESS and rc != SQL_SUCCESS_WITH_INFO) {
        REBCTX *error = Error_ODBC_Env(henv);
        SQLFreeHandle(SQL_HANDLE_ENV, henv);
        fail (error);
    }

    // Allocate the connection handle, with login timeout of 5 seconds (why?)
    //
    SQLHDBC hdbc;
    rc = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
    if (rc != SQL_SUCCESS and rc != SQL_SUCCESS_WITH_INFO) {
        REBCTX *error = Error_ODBC_Env(henv);
        SQLFreeHandle(SQL_HANDLE_ENV, henv);
        fail (error);
    }

    rc = SQLSetConnectAttr(hdbc, SQL_LOGIN_TIMEOUT, cast(SQLPOINTER, 5), 0);
    if (rc != SQL_SUCCESS and rc != SQL_SUCCESS_WITH_INFO) {
        REBCTX *error = Error_ODBC_Env(henv);
        SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
        SQLFreeHandle(SQL_HANDLE_ENV, henv);
        fail (error);
    }

    // Connect to the Driver, using the converted connection string
    //
    REBCNT connect_len = rebUnbox("length of", ARG(spec), END);
    SQLWCHAR *connect = rebSpellAllocW(ARG(spec), END);

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
    rebFree(connect);

    if (rc != SQL_SUCCESS and rc != SQL_SUCCESS_WITH_INFO) {
        REBCTX *error = Error_ODBC_Env(henv);
        SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
        SQLFreeHandle(SQL_HANDLE_ENV, henv);
        fail (error);
    }

    REBVAL *henv_value = rebHandle(henv, 0, &cleanup_henv);
    REBVAL *hdbc_value = rebHandle(hdbc, 0, &cleanup_hdbc);

    rebElide("poke", ARG(connection), "'henv", rebR(henv_value), END);
    rebElide("poke", ARG(connection), "'hdbc", rebR(hdbc_value), END);

    return R_TRUE;
}


//
//  open-statement: native/export [
//
//      return: [logic!]
//      connection [object!]
//      statement [object!]
//  ]
//
REBNATIVE(open_statement)
//
// !!! Similar to previous routines, this takes an empty statement object in
// to initialize.
{
    ODBC_INCLUDE_PARAMS_OF_OPEN_STATEMENT;

    REBVAL *connection = ARG(connection);
    REBVAL *hdbc_value = rebRun(
        "ensure handle! pick", connection, "'hdbc", END
    );
    SQLHDBC hdbc = VAL_HANDLE_POINTER(SQLHDBC, hdbc_value);
    rebRelease(hdbc_value);

    SQLRETURN rc;

    SQLHSTMT hstmt;
    rc = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
    if (rc != SQL_SUCCESS and rc != SQL_SUCCESS_WITH_INFO)
        fail (Error_ODBC_Dbc(hdbc));

    REBVAL *hstmt_value = rebHandle(hstmt, 0, nullptr);

    rebElide("poke", ARG(statement), "'hstmt", rebR(hstmt_value), END);

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

    p->length = 0; // ignored for most types
    p->column_size = 0; // also ignored for most types
    TRASH_POINTER_IF_DEBUG(p->buffer); // required to be set by switch()

    switch (VAL_TYPE(v)) {
    case REB_BLANK: {
        p->buffer_size = 0;
        p->buffer = NULL;

        c_type = SQL_C_DEFAULT;
        sql_type = SQL_NULL_DATA;
        break; }

    case REB_LOGIC: {
        p->buffer_size = sizeof(unsigned char);
        p->buffer = rebAllocN(char, p->buffer_size);

        *cast(unsigned char*, p->buffer) = VAL_LOGIC(v);

        c_type = SQL_C_BIT;
        sql_type = SQL_BIT;
        break; }

    case REB_INTEGER: {
        p->buffer_size = sizeof(REBI64);
        p->buffer = rebAllocN(char, p->buffer_size);

        *cast(REBI64*, p->buffer) = VAL_INT64(v);

        c_type = SQL_C_SBIGINT; // Rebol's INTEGER! type is signed
        sql_type = SQL_INTEGER;
        break; }

    case REB_DECIMAL: {
        p->buffer_size = sizeof(double);
        p->buffer = rebAllocN(char, p->buffer_size);

        *cast(double*, p->buffer) = VAL_DECIMAL(v);

        c_type = SQL_C_DOUBLE;
        sql_type = SQL_DOUBLE;
        break; }

    case REB_TIME: {
        p->buffer_size = sizeof(TIME_STRUCT);
        p->buffer = rebAllocN(char, p->buffer_size);

        TIME_STRUCT *time = cast(TIME_STRUCT*, p->buffer);

        REB_TIMEF tf;
        Split_Time(VAL_NANO(v), &tf); // loses sign

        time->hour = tf.h;
        time->minute = tf.m;
        time->second = tf.s; // cast(REBDEC, tf.s) + (tf.n * NANO) for precise

        c_type = SQL_C_TYPE_TIME;
        sql_type = SQL_TYPE_TIME;
        break; }

    case REB_DATE: {
        if (NOT_VAL_FLAG(v, DATE_FLAG_HAS_TIME)) {
            p->buffer_size = sizeof(DATE_STRUCT);
            p->buffer = rebAllocN(char, p->buffer_size);

            DATE_STRUCT *date = cast(DATE_STRUCT*, p->buffer);

            date->year = VAL_YEAR(v);
            date->month = VAL_MONTH(v);
            date->day = VAL_DAY(v);

            c_type = SQL_C_TYPE_DATE;
            sql_type = SQL_TYPE_DATE;
        }
        else {
            p->buffer_size = sizeof(TIMESTAMP_STRUCT);
            p->buffer = rebAllocN(char, p->buffer_size);

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

    case REB_TEXT: {
        REBCNT len_no_term = rebSpellingOfW(NULL, 0, v); // first, get length
        SQLWCHAR *chars = rebAllocN(SQLWCHAR, len_no_term + 1);

        REBCNT len_check = rebSpellingOfW(chars, len_no_term, v); // now, get
        assert(len_check == len_no_term);
        UNUSED(len_check);

        p->buffer_size = sizeof(SQLWCHAR) * len_no_term;

        p->length = p->column_size = cast(SQLSMALLINT, 2 * len_no_term);

        c_type = SQL_C_WCHAR;
        sql_type = SQL_VARCHAR;
        p->buffer = chars;
        break; }

    case REB_BINARY: {
        p->buffer_size = VAL_LEN_AT(v); // sizeof(char) guaranteed to be 1
        p->buffer = rebAllocN(char, p->buffer_size);

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
    const REBVAL *which,
    REBVAL *block
){
    assert(IS_BLOCK(block)); // !!! Should it ensure exactly 4 items?

    SQLSMALLINT length[4];
    SQLWCHAR *pattern[4];

    int arg;
    for (arg = 0; arg < 4; arg++) {
        //
        // !!! What if not at head?  Original code seems incorrect, because
        // it passed the array at the catalog word, vs TEXT!.
        //
        REBVAL *value = rebRun(
            "ensure* text! pick", block, rebI(arg + 1), END
        );
        if (value) {
            REBCNT len = rebUnbox("length of", value, END);
            pattern[arg] = rebSpellAllocW(value, END);
            length[arg] = len;
            rebRelease(value);
        }
        else {
            length[arg] = 0;
            pattern[arg] = NULL;
        }
    }

    SQLRETURN rc;

    int w = rebUnbox(
        "switch ensure word!", which, "[",
            "'tables [1]",
            "'columns [2]",
            "'types [3]",
        "] else [",
            "fail {Catalog must be TABLES, COLUMNS, or TYPES}",
        "]", END
    );

    switch (w) {
    case 1:
        rc = SQLTablesW(
            hstmt,
            pattern[2], length[2], // catalog
            pattern[1], length[1], // schema
            pattern[0], length[0], // table
            pattern[3], length[3] // type
        );
        break;

    case 2:
        rc = SQLColumnsW(
            hstmt,
            pattern[3], length[3], // catalog
            pattern[2], length[2], // schema
            pattern[0], length[0], // table
            pattern[1], length[1]  // column
        );
        break;

    case 3:
        rc = SQLGetTypeInfoW(hstmt, SQL_ALL_TYPES);
        break;

    default:
        panic ("Invalid GET_CATALOG_XXX value");
    }

    for (arg = 0; arg != 4; arg++) {
        if (pattern[arg] != NULL)
            rebFree(pattern[arg]);
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
            (source[s] == *underscore or source[s] == *space)
                ? *hyphen
                : towlower(source[s]);

        if (
            (
                s < length - 2
                and iswupper(source[s])
                and iswupper(source[s + 1])
                and iswlower(source[s + 2])
            ) or (
                s < length - 1
                and iswlower(source[s])
                and iswupper(source[s + 1])
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
        if (rc != SQL_SUCCESS and rc != SQL_SUCCESS_WITH_INFO)
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
        if (rc != SQL_SUCCESS and rc != SQL_SUCCESS_WITH_INFO)
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

        column->title_word = rebSizedWordW(title, title_length);
        rebUnmanage(column->title_word);
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

        if (rc != SQL_SUCCESS and rc != SQL_SUCCESS_WITH_INFO)
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
//
REBNATIVE(insert_odbc)
{
    ODBC_INCLUDE_PARAMS_OF_INSERT_ODBC;

    REBVAL *statement = ARG(statement);
    REBVAL *hstmt_value = rebRun(
        "ensure handle! pick", statement, "'hstmt", END
    );
    SQLHSTMT hstmt = VAL_HANDLE_POINTER(SQLHSTMT, hstmt_value);
    rebRelease(hstmt_value);

    SQLRETURN rc;

    rc = SQLFreeStmt(hstmt, SQL_RESET_PARAMS); // !!! check rc?
    rc = SQLCloseCursor(hstmt); // !!! check rc?

    // !!! Some code here would set the number of rows, but was commented out
    // saying it was "in the wrong place" (?)
    //
    // SQLULEN max_rows = 0;
    // rc = SQLSetStmtAttr(hstmt, SQL_ATTR_MAX_ROWS, &max_rows, SQL_IS_POINTER);
    // if (rc != SQL_SUCCESS and rc != SQL_SUCCESS_WITH_INFO)
    //     fail (Error_ODBC_Stmt(hstmt));


    //=//// MAKE SQL REQUEST FROM DIALECTED SQL BLOCK /////////////////////=//
    //
    // The block passed in is used to form a query.

    REBCNT sql_index = 1;
    REBVAL *value = rebRun(
        "pick", ARG(sql), rebI(sql_index),
        "else [fail {Empty array passed for SQL dialect}]", END
    );

    REBOOL use_cache = FALSE;

    REBOOL get_catalog = rebDid(
        "word? <- try match [word! text!]", value, "or [",
            "fail {SQL dialect must start with WORD! or STRING! value}"
        "]", END
    );

    if (get_catalog) {
        rc = ODBC_GetCatalog(hstmt, value, ARG(sql));
    }
    else {
        // Prepare/Execute statement, when first element in the block is a
        // (statement) string

        // Compare with previously prepared statement, and if not the same,
        // then prepare a new statement.
        //
        use_cache = rebDid(
            value, "==",
            "ensure [text! blank!] pick", statement, "'string", END
        );

        if (not use_cache) {
            REBCNT length = rebUnbox("length of", value, END);
            SQLWCHAR *sql_string = rebSpellAllocW(value, END);

            rc = SQLPrepareW(hstmt, sql_string, cast(SQLSMALLINT, length));
            if (rc != SQL_SUCCESS and rc != SQL_SUCCESS_WITH_INFO)
                fail (Error_ODBC_Stmt(hstmt));

            rebFree(sql_string);

            // Remember statement string handle, but keep a copy since it
            // may be mutated by the user.
            //
            // !!! Could re-use value with existing series if read only
            //
            rebElide("poke", statement, "'string", "(copy", value, ")", END);
        }
        rebRelease(value);

        // The SQL string may contain ? characters, which indicates that it is
        // a parameterized query.  The separation of the parameters into a
        // different quarantined part of the query is to protect against SQL
        // injection.

        REBCNT num_params = rebLengthOf(ARG(sql)) - sql_index; // after SQL
        ++sql_index;

        PARAMETER *params = NULL;
        if (num_params != 0) {
            params = rebAllocN(PARAMETER, num_params);

            REBCNT n;
            for (n = 0; n < num_params; ++n, ++sql_index) {
                value = rebRun("pick", ARG(sql), rebI(sql_index), END);
                rc = ODBC_BindParameter(
                    hstmt,
                    &params[n],
                    n + 1,
                    value
                );
                rebRelease(value);
                if (rc != SQL_SUCCESS and rc != SQL_SUCCESS_WITH_INFO)
                    fail (Error_ODBC_Stmt(hstmt));
            }
        }

        // Execute statement, but don't check result code until after the
        // parameters and their data buffers have been freed.
        //
        rc = SQLExecute(hstmt);

        if (num_params != 0) {
            REBCNT n;
            for (n = 0; n != num_params; ++n) {
                if (params[n].buffer != NULL)
                    rebFree(params[n].buffer);
            }
            rebFree(params);
        }

        if (rc != SQL_SUCCESS and rc != SQL_SUCCESS_WITH_INFO)
            fail (Error_ODBC_Stmt(hstmt));
    }

    //=//// RETURN RECORD COUNT IF NO RESULT ROWS /////////////////////////=//
    //
    // Insert/Update/Delete statements do not return records, and this is
    // indicated by a 0 count for columns in the return result.

    SQLSMALLINT num_columns;
    rc = SQLNumResultCols(hstmt, &num_columns);
    if (rc != SQL_SUCCESS and rc != SQL_SUCCESS_WITH_INFO)
        fail (Error_ODBC_Stmt(hstmt));

    if (num_columns == 0) {
        SQLLEN num_rows;
        rc = SQLRowCount(hstmt, &num_rows);
        if (rc != SQL_SUCCESS and rc != SQL_SUCCESS_WITH_INFO)
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
        REBVAL *cache = rebRun(
            "ensure block! pick", statement, "'titles", END
        );
        Move_Value(D_OUT, cache);
        rebRelease(cache);
        return R_OUT;
    }

    REBVAL *old_columns_value = rebRun(
        "opt ensure [handle! blank!] pick", statement, "'columns", END
    );
    if (old_columns_value) {
        COLUMN *old_columns = VAL_HANDLE_POINTER(COLUMN, old_columns_value);
        free(old_columns);
        rebRelease(old_columns_value);
    }

    COLUMN *columns = cast(COLUMN*, malloc(sizeof(COLUMN) * num_columns));
    if (not columns)
        fail ("Couldn't allocate column buffers!");

    REBVAL *columns_value = rebHandle(columns, num_columns, NULL);

    rebElide("poke", statement, "'columns", rebR(columns_value), END);

    rc = ODBC_DescribeResults(hstmt, num_columns, columns);
    if (rc != SQL_SUCCESS and rc != SQL_SUCCESS_WITH_INFO)
        fail (Error_ODBC_Stmt(hstmt));

    rc = ODBC_BindColumns(hstmt, num_columns, columns);
    if (rc != SQL_SUCCESS and rc != SQL_SUCCESS_WITH_INFO)
        fail (Error_ODBC_Stmt(hstmt));

    REBVAL *titles = rebRun("make block!", rebI(num_columns), END);
    int col;
    for (col = 0; col != num_columns; ++col)
        rebElide("append", titles, columns[col].title_word, END);

    // remember column titles if next call matches, return them as the result
    //
    rebElide("poke", statement, "'titles", titles, END);

    Move_Value(D_OUT, titles);
    rebRelease(titles);
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
            if (*cast(REBU64*, col->buffer) > INT64_MAX)
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

        return rebLogic(did (*cast(unsigned char*, col->buffer)));

    case SQL_BINARY:
    case SQL_VARBINARY:
    case SQL_LONGVARBINARY:
        return rebBinary(col->buffer, col->length);

    case SQL_CHAR:
    case SQL_VARCHAR:
    case SQL_LONGVARCHAR:
        fail ("Non-WCHAR data not supported by ODBC (currently)");

    case SQL_WCHAR:
    case SQL_WVARCHAR:
    case SQL_WLONGVARCHAR:
        assert(col->length % 2 == 0);
        return rebLengthedTextW(cast(SQLWCHAR*, col->buffer), col->length / 2);

    case SQL_GUID:
        fail ("SQL_GUID not supported by ODBC (currently)");

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
    ODBC_INCLUDE_PARAMS_OF_COPY_ODBC;

    REBVAL *hstmt_value = rebRun(
        "ensure handle! pick", ARG(statement), "'hstmt", END
    );
    SQLHSTMT hstmt = cast(SQLHSTMT, VAL_HANDLE_VOID_POINTER(hstmt_value));
    rebRelease(hstmt_value);

    REBVAL *columns_value = rebRun(
        "ensure handle! pick", ARG(statement), "'columns", END
    );
    COLUMN *columns = VAL_HANDLE_POINTER(COLUMN, columns_value);
    rebRelease(columns_value);

    if (hstmt == SQL_NULL_HANDLE or columns == NULL)
        fail ("Invalid statement object!");

    SQLRETURN rc;

    SQLSMALLINT num_columns;
    rc = SQLNumResultCols(hstmt, &num_columns);
    if (rc != SQL_SUCCESS and rc != SQL_SUCCESS_WITH_INFO)
        fail (Error_ODBC_Stmt(hstmt));

    // compares-0 based row against num_rows, so -1 is chosen to never match
    // and hence mean "as many rows as available"
    //
    SQLULEN num_rows = rebUnbox(ARG(length), "or [-1]", END);

    REBDSP dsp_orig = DSP;

    // Fetch columns
    //
    SQLULEN row = 0;
    while (row != num_rows and SQLFetch(hstmt) != SQL_NO_DATA) {
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
    ODBC_INCLUDE_PARAMS_OF_UPDATE_ODBC;

    REBVAL *connection = ARG(connection);

    // Get connection handle
    //
    REBVAL *hdbc_value = rebRun(
        "ensure handle! pick", connection, "'hdbc", END
    );
    SQLHDBC hdbc = cast(SQLHDBC, VAL_HANDLE_VOID_POINTER(hdbc_value));
    rebRelease(hdbc_value);

    SQLRETURN rc;

    REBOOL access = rebDid(ARG(access), END);
    rc = SQLSetConnectAttr(
        hdbc,
        SQL_ATTR_ACCESS_MODE,
        cast(SQLPOINTER*,
            cast(uintptr_t, access ? SQL_MODE_READ_WRITE : SQL_MODE_READ_ONLY)
        ),
        SQL_IS_UINTEGER
    );

    if (rc != SQL_SUCCESS and rc != SQL_SUCCESS_WITH_INFO)
        fail (Error_ODBC_Dbc(hdbc));

    REBOOL commit = rebDid(ARG(commit), END);
    rc = SQLSetConnectAttr(
        hdbc,
        SQL_ATTR_AUTOCOMMIT,
        cast(SQLPOINTER*,
            cast(uintptr_t, commit ? SQL_AUTOCOMMIT_ON : SQL_AUTOCOMMIT_OFF)
        ),
        SQL_IS_UINTEGER
    );

    if (rc != SQL_SUCCESS and rc != SQL_SUCCESS_WITH_INFO)
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
    ODBC_INCLUDE_PARAMS_OF_CLOSE_STATEMENT;

    REBVAL *statement = ARG(statement);

    REBVAL *columns_value = rebRun(
        "opt ensure [handle! blank!] pick", statement, "'columns", END
    );
    if (columns_value) {
        COLUMN *columns = VAL_HANDLE_POINTER(COLUMN, columns_value);
        assert(columns != NULL);

        REBCNT num_columns = VAL_HANDLE_LEN(columns_value);
        REBCNT col;
        for (col = 0; col != num_columns; ++col)
            rebRelease(columns[col].title_word);

        free(columns);
        SET_HANDLE_POINTER(columns_value, NULL); // avoid GC cleanup
        rebElide("poke", statement, "'columns", "blank", END);

        rebRelease(columns_value);
    }

    REBVAL *hstmt_value = rebRun(
        "opt ensure [handle! blank!] pick", statement, "'hstmt", END
    );
    if (hstmt_value) {
        SQLHSTMT hstmt = cast(SQLHSTMT, VAL_HANDLE_VOID_POINTER(hstmt_value));
        assert(hstmt != NULL);

        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        SET_HANDLE_POINTER(hstmt_value, SQL_NULL_HANDLE); // avoid GC cleanup
        rebElide("poke", statement, "'hstmt blank", END);

        rebRelease(hstmt_value);
    }

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
    ODBC_INCLUDE_PARAMS_OF_CLOSE_CONNECTION;

    REBVAL *connection = ARG(connection);

    // Close the database connection before the environment, since the
    // connection was opened from the environment.

    REBVAL *hdbc_value = rebRun(
        "opt ensure [handle! blank!] pick", connection, "'hdbc", END
    );
    if (hdbc_value) {
        SQLHDBC hdbc = cast(SQLHDBC, VAL_HANDLE_VOID_POINTER(hdbc_value));
        assert(hdbc != NULL);

        SQLDisconnect(hdbc);
        SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
        SET_HANDLE_POINTER(hdbc_value, SQL_NULL_HANDLE); // avoid GC cleanup

        rebElide("poke", connection, "'hdbc", "blank", END);
        rebRelease(hdbc_value);
    }

    // Close the environment
    //
    REBVAL *henv_value = rebRun(
        "opt ensure [handle! blank!] pick", connection, "'henv", END
    );
    if (henv_value) {
        SQLHENV henv = cast(SQLHENV, VAL_HANDLE_VOID_POINTER(henv_value));
        assert(henv != NULL);

        SQLFreeHandle(SQL_HANDLE_ENV, henv);
        SET_HANDLE_POINTER(henv_value, SQL_NULL_HANDLE); // avoid GC cleanup

        rebElide("poke", connection, "'henv", "blank", END);
        rebRelease(henv_value);
    }

    return R_TRUE;
}


#include "tmp-mod-odbc-last.h"
