//
//  file: %mod-odbc.c
//  summary: "Interface from REBOL3 to ODBC"
//  section: extension
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2010-2011 Christian Ensel
// Copyright 2017-2025 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This file provides the natives (OPEN-CONNECTION, INSERT-ODBC, etc.) which
// are used as the low-level support to implement the higher level services
// of the ODBC scheme (which are written in Rebol).
//
// The driver is made to handle queries which look like:
//
//     ["select * from tables where (name = ?) and (age = ?)" -[Brian]- 50]
//
// The ? notation for substitution points is what is known as a "parameterized
// query".  See the README.md regarding the dialect built on top of this.
//

#include "reb-config.h"

#if TO_WINDOWS
    #define WIN32_LEAN_AND_MEAN  // trim down the Win32 headers
    #include <windows.h>
#endif

#include <sql.h>  // depends on defines like VOID on Windows
#include <sqlext.h>

#if RUNTIME_CHECKS
    #include <stdio.h>
#endif

#include "assert-fix.h"
#include "needful/needful.h"
#include "c-extras.h"  // for EXTERN_C, nullptr, etc.


#include "rebol.h"
#include "tmp-mod-odbc.h"

typedef RebolValue Value;


// https://stackoverflow.com/q/58438456
#define USE_SQLITE_DESCRIBECOL_WORKAROUND


// The version of ODBC that this is written to use is 3.0, which was released
// around 1995.  At time of writing (2017) it is uncommon to encounter ODBC
// systems that don't implement at least that.  It's not clear if ODBCVER is
// actually standard or not, so define it to 3.0 if it isn't.
// https://stackoverflow.com/q/58443534
//
#ifndef ODBCVER
    #define ODBCVER 0x0300
#endif


// For variable sized data, SQLDescribeColW() does not necessarily give back
// a useful column_size (e.g. BLOBs can come back with the maximum of
// 2,147,483,647).  It's not until the SQLGetData() that you can actually
// get back a useful length...first passing in a too-small buffer to get
// the size, then making a right-sized buffer in a second SQLGetData() call.
//
// However, you cannot pass in a nullptr for the buffer.  So DUMMY_BUFFER is
// used instead.
//
#define DUMMY_BUFFER_SIZE  1
char g_dummy_buffer[DUMMY_BUFFER_SIZE];


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
        0 == SQL_NULL_HANDLE, // Note it is long, not pointer, on Mac ODBC
        "ODBC module code currently asssumes SQL_NULL_HANDLE is 0"
    );
#endif


// Only one SQLHENV is needed for all connections.  It is lazily initialized by
// the ODBC module when needed.
//
SQLHENV henv = SQL_NULL_HANDLE;


struct ConnectionStruct {  // indirect so SHUTDOWN* can find and kill open HDBC
    SQLHDBC hdbc;  // if SQL_NULL_HANDLE, cleanup already done

    struct ConnectionStruct* next;
};
typedef struct ConnectionStruct Connection;

struct ParameterStruct {  // For binding parameters
    SQLULEN column_size;
    SQLPOINTER buffer;
    SQLULEN buffer_size;
    SQLLEN length;
};
typedef struct ParameterStruct Parameter;

struct ColumnStruct {  // For describing a single column
    Value* title;  // a TEXT!
    SQLSMALLINT sql_type;
    SQLSMALLINT c_type;
    SQLULEN column_size;
    SQLPOINTER buffer;
    SQLULEN buffer_size;
    SQLSMALLINT precision;
    SQLSMALLINT nullable;
    bool is_unsigned;
};
typedef struct ColumnStruct Column;


struct ColumnListStruct {  // For describing a list of columns
    Column* columns;  // if nullptr, cleanup already done
    SQLLEN num_columns;

    struct ColumnListStruct* next;
};
typedef struct ColumnListStruct ColumnList;


// Because this C code is bridging to a garbage collected language, we have to
// be prepared for the case when shutdown occurs with connections, parameters,
// and columns left open.  We have a hook in the extension SHUTDOWN* call
// but we need some lists to go through.
//
// The only time anything is actually removed from this list is when the
// HANDLE! holding the reference is GC'd.
//
Connection* g_all_connections = nullptr;
ColumnList* g_all_columnlists = nullptr;


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

Value* Error_ODBC_Core(
    SQLSMALLINT handleType,
    SQLHANDLE handle,
    const char *file,  // nullptr in release builds
    int line
){
    SQLWCHAR state[6];
    SQLINTEGER native;

    const SQLSMALLINT buffer_size = 4086;
    SQLWCHAR message[4086];
    SQLSMALLINT message_len;

    SQLRETURN rc = SQLGetDiagRecW(  // WCHAR API in case internationalized?
        handleType,
        handle,
        1,
        state,
        &native,
        message,
        buffer_size,
        &message_len
    );

    switch (rc) {
      case SQL_SUCCESS_WITH_INFO:  // error buffer wasn't big enough
        message_len = buffer_size;  // !!! REVIEW: reallocate vs. truncate?
        goto success;

      case SQL_SUCCESS:
      success:
        return rebValue(
            "make warning!", rebR(rebLengthedTextWide(message, message_len))
        );
    }

    // The following errors should not happen, so it's good in the debug
    // build to have a bit more information about exactly which ODBC API
    // call is having the problem.
    //
  #if RUNTIME_CHECKS
    printf("!! Couldn't get ODBC Error Message: %s @ %d\n", file, line);
  #else
    UNUSED(file);
    UNUSED(line);
  #endif

    switch (rc) {
      case SQL_INVALID_HANDLE:
        return rebValue(
            "make warning! -[Internal ODBC extension error (invalid handle)]-"
        );

      case SQL_ERROR:
        return rebValue(
            "make warning! -[Internal ODBC extension error (bad diag record #)]-"
        );

      case SQL_NO_DATA:
        return rebValue(
            "make warning! -[No ODBC diagnostic information available]-"
        );

      default:
        break;  // should not happen if the ODBC interface/driver are working
    }

    assert(!"SQLGetDiagRecW returned undocumented SQLRESULT value");
    return rebValue("make warning! -[Undocumented SQLRESULT SQLGetDiagRecW()]-");
}

#if RUNTIME_CHECKS  // report file and line info with mystery errors
    #define Error_ODBC(handleType,handle) \
        Error_ODBC_Core((handleType), (handle), __FILE__, __LINE__)
#else
    #define Error_ODBC(handleType,handle) \
        Error_ODBC_Core((handleType), (handle), nullptr, 0)
#endif

#define Error_ODBC_Stmt(hstmt) \
    Error_ODBC(SQL_HANDLE_STMT, hstmt)

#define Error_ODBC_Env(henv) \
    Error_ODBC(SQL_HANDLE_ENV, henv)

#define Error_ODBC_Dbc(hdbc) \
    Error_ODBC(SQL_HANDLE_DBC, hdbc)


// These are the cleanup functions for the handles that will be called if the
// GC notices no one is using them anymore (as opposed to being explicitly
// called by a close operation).

static void Force_Connection_Cleanup(Connection* conn) {
    if (conn->hdbc == SQL_NULL_HANDLE)
        return;  // already cleared out by CLOSE-CONNECTION or SHUTDOWN*

    SQLDisconnect(conn->hdbc);
    SQLFreeHandle(SQL_HANDLE_DBC, conn->hdbc);
    conn->hdbc = SQL_NULL_HANDLE;
}

static void Connection_Handle_Cleaner(void* p, size_t length) {
    Connection* conn = cast(Connection*, p);
    UNUSED(length);

    Force_Connection_Cleanup(conn);

    if (conn == g_all_connections)
        g_all_connections = conn->next;
    else {
        Connection* temp = g_all_connections;
        while (temp->next != conn)
            temp = temp->next;
        temp->next = temp->next->next;
    }

    rebFree(conn);
}


//
// !!! SQL introduced "NCHAR" for "Native Characters", which typically are
// 2-bytes-per-character instead of just one.  As time has gone on, that's no
// longer enough...and the UTF-8 encoding is the most pervasive way of storing
// strings.  But it uses a varying number of bytes per character, which runs
// counter to SQL's desire to use fixed-size-records.
//
// There is no clear endgame in the SQL world for what is going to be done
// about this.  So many text strings (that might have emoji/etc.) get stored
// as BLOB, which limits their searchability from within the SQL language
// itself.  NoSQL databases have been edging into this space as a result.
//
// Since Ren-C makes the long bet on UTF-8, it started out by storing and
// fetching UTF-8 from CHAR-based fields.  But some systems (e.g. Excel) seem
// to not be returning UTF-8 when you request a CHAR() field via SQL_C_CHAR:
//
// https://github.com/metaeducation/rebol-odbc/issues/8
//
// Latin1 was tried, but it wasn't that either.  As a workaround, we let
// you globally set the encoding/decoding method of CHAR fields.
//
typedef enum {
    CHAR_COL_UTF8,
    //
    // !!! Should we offer a CHAR_COL_UCS2, which errors if you use any
    // codepoints higher than 0xFFFF ?  (Right now that just uses UTF-16.)
    //
    CHAR_COL_UTF16,
    CHAR_COL_LATIN1
} CharColumnEncoding;

// For now, default to the most conservative choice...which is to let the
// driver/driver-manager do the translation from wide characters, but that is
// less efficient than doing UTF-8
//
CharColumnEncoding g_char_column_encoding = CHAR_COL_UTF16;


//
//  export odbc-set-char-encoding: native [
//
//  "Set the encoding for CHAR, CHAR(n), VARCHAR(n), LONGVARCHAR fields"
//
//      return: []
//      encoding "Currently you have to use UTF-16 for UCS-2"
//          [~(utf-8 latin-1 utf-16)~]
//  ]
//
DECLARE_NATIVE(ODBC_SET_CHAR_ENCODING)
//
// UTF-8 is preferred to UTF8: https://stackoverflow.com/q/809620/
{
    INCLUDE_PARAMS_OF_ODBC_SET_CHAR_ENCODING;

    g_char_column_encoding = cast(CharColumnEncoding, rebUnboxInteger(
        "switch encoding [",
            "'utf-8 [", rebI(CHAR_COL_UTF8), "]",
            "'utf-16 [", rebI(CHAR_COL_UTF16), "]",
            "'latin-1 [", rebI(CHAR_COL_LATIN1), "]",
        "] else [",
            "panic --[ENCODING must be UTF-8, UCS-2, UTF-16, or LATIN-1]--"
        "]"
    ));

    return "~";
}


//
//  export open-connection: native [
//
//      return: "Object with HDBC handle field initialized"
//          [object!]
//      spec "ODBC connection string, e.g. commonly 'Dsn=DatabaseName'"
//          [text!]
//  ]
//
DECLARE_NATIVE(OPEN_CONNECTION)
{
    INCLUDE_PARAMS_OF_OPEN_CONNECTION;

    // We treat ODBC's SQLWCHAR type (wide SQL char) as 2 bytes per wchar, even
    // on platforms where wchar_t is larger.  This gives unixODBC compatibility:
    //
    // https://stackoverflow.com/a/7552533/211160
    //
    // "unixODBC follows MS ODBC Driver manager and has SQLWCHARs as 2 bytes
    //  UCS-2 encoded. iODBC I believe uses wchar_t (this is based on
    //  attempting to support iODBC in DBD::ODBC)"
    //
    // Ren-C supports the full unicode range of codepoints, so if codepoints
    // bigger than 0xFFFF are used then they are encoded as surrogate pairs.
    // UCS-2 constraint can be added to error rather than tolerate this.
    //
    assert(sizeof(SQLWCHAR) == sizeof(REBWCHAR));

    SQLRETURN rc;

    // Lazily allocate the environment handle if not already allocated, and set
    // its version to ODBC3.  (We could track if we allocated it and free it
    // if the open fails, but for now just let SHUTDOWN* take care of it.)
    //
    if (henv == SQL_NULL_HANDLE) {
        rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
        if (not SQL_SUCCEEDED(rc))
            return rebDelegate("panic", Error_ODBC_Env(SQL_NULL_HENV));

        rc = SQLSetEnvAttr(
            henv,
            SQL_ATTR_ODBC_VERSION,
            p_cast(SQLPOINTER, u_cast(uintptr_t, SQL_OV_ODBC3)),
            0  // StringLength (ignored for this attribute)
        );
        if (not SQL_SUCCEEDED(rc)) {
            Value* error = Error_ODBC_Env(henv);
            SQLFreeHandle(SQL_HANDLE_ENV, henv);
            henv = SQL_NULL_HANDLE;
            return rebDelegate("panic", error);
        }
    }

    // Allocate the connection handle, with login timeout of 5 seconds (why?)
    //
    SQLHDBC hdbc;
    rc = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
    if (not SQL_SUCCEEDED(rc)) {
        Value* error = Error_ODBC_Env(henv);
        SQLFreeHandle(SQL_HANDLE_ENV, henv);
        return rebDelegate("panic", error);
    }

    rc = SQLSetConnectAttr(
        hdbc,
        SQL_LOGIN_TIMEOUT,
        p_cast(SQLPOINTER, u_cast(uintptr_t, 5)),
        0
    );
    if (not SQL_SUCCEEDED(rc)) {
        Value* error = Error_ODBC_Dbc(hdbc);
        SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
        return rebDelegate("panic", error);
    }

    // Connect to the Driver

    SQLWCHAR *connect_string = rebSpellWide("spec");

    SQLSMALLINT out_connect_len;
    rc = SQLDriverConnectW(
        hdbc,  // ConnectionHandle
        nullptr,  // WindowHandle
        connect_string,  // InConnectionString
        SQL_NTS,  // StringLength1 (null terminated string)
        nullptr,  // OutConnectionString (not interested in this)
        0,  // BufferLength (again, not interested)
        &out_connect_len,  // StringLength2Ptr (gets returned anyway)
        SQL_DRIVER_NOPROMPT  // DriverCompletion
    );
    rebFree(connect_string);

    if (not SQL_SUCCEEDED(rc)) {
        Value* error = Error_ODBC_Dbc(hdbc);
        SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
        return rebDelegate("panic", error);
    }

    // Extension SHUTDOWN* might happen with HDBC handles outstanding, so we
    // need a level of indirection to enumerate them (ODBC does not offer it).
    //
    Connection* conn = rebTryAlloc(Connection);
    if (conn == nullptr)
        return "panic -[Could not allocate Connection tracking object]-";
    rebUnmanageMemory(conn);

    conn->hdbc = hdbc;
    conn->next = g_all_connections;
    g_all_connections = conn;

    Value* hdbc_value = rebHandle(
        conn, sizeof(Connection*), &Connection_Handle_Cleaner
    );

    return rebValue(
        "make database-prototype [",
            "hdbc:", rebR(hdbc_value),
            // also has statements: [] as default
        "]"
    );
}


//
//  export open-statement: native [
//
//      return: [logic?]
//      connection [object!]
//      statement [object!]
//  ]
//
DECLARE_NATIVE(OPEN_STATEMENT)
//
// !!! Similar to previous routines, this takes an empty statement object in
// to initialize.
{
    INCLUDE_PARAMS_OF_OPEN_STATEMENT;

    Connection* conn = rebUnboxHandle(Connection*,
        "ensure handle! connection.hdbc"
    );
    SQLHDBC hdbc = conn->hdbc;

    SQLRETURN rc;

    SQLHSTMT hstmt;
    rc = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
    if (not SQL_SUCCEEDED(rc))
        return rebDelegate("panic", Error_ODBC_Dbc(hdbc));

    Value* hstmt_value = rebHandle(hstmt, sizeof(hstmt), nullptr);

    rebElide("statement.hstmt:", rebR(hstmt_value));

    return rebLogic(true);
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
    Parameter* p,
    SQLUSMALLINT number,  // parameter number
    const Value* v
){
    assert(number != 0);

    p->length = 0;  // ignored for most types
    p->column_size = 0;  // also ignored for most types
    Corrupt_If_Needful(p->buffer);  // required to be set by switch()

    // We don't expose integer mappings for Rebol data types in libRebol to
    // use in a switch() statement, so no:
    //
    //    switch (opt Type_Of(v)) { case TYPE_INTEGER: {...} ...}
    //
    // But since the goal is to translate into ODBC types anyway, we can go
    // ahead and do that with Rebol code that embeds those types.
    //
    // This could be sped up by making it a function, so you aren't paying
    // for the scanning of the code each time:
    //
    //   https://forum.rebol.info/t/540/4
    //
    SQLSMALLINT c_type = rebUnboxInteger("switch:type @", v, "[",
        "word! [",
            "switch @", v, "[",
                "'null [", rebI(SQL_C_DEFAULT), "]",
                "'true [", rebI(SQL_C_BIT), "]",
                "'false [", rebI(SQL_C_BIT), "]",
            "] else [",
                "panic -[Legal WORD!-parameters: [null true false]]-",
            "]",
        "]",

        // When we ask to insert data, the ODBC layer is supposed to be able
        // to take a C variable in any known integral type format, and so
        // long as the actual number represented is not out of range for the
        // column it should still work.  So a multi-byte integer should go
        // into a byte column as long as it's only using the range 0-255.
        //
        // !!! Originally this went ahead and always requested to insert a
        // "BigInt" to correspond to R3-Alpha's 64-bit standard.  However,
        // SQL_C_SBIGINT doesn't work on various ODBC drivers...among them
        // Oracle (and MySQL won't translate bigints, at least on unixodbc):
        //
        // https://stackoverflow.com/a/41598379
        //
        // There is a suggestion from MySQL that using SQL_NUMERIC can work
        // around this, but it doesn't seem to help.  Instead, try using just
        // a SQLINTEGER so long as the number fits in that range...and then
        // escalate to BigNum only when necessary.  (The worst it could do is
        // fail, and you'd get an out of range error otherwise anyway.)
        //
        // The bounds are part of the ODBC standard, so appear literally here.
        //
        "integer! [",
            "case [",
                v, "> 4294967295 [", rebI(SQL_C_UBIGINT), "]",
                v, "> 2147483647 [", rebI(SQL_C_ULONG), "]",
                v, "< -2147483648 [", rebI(SQL_C_SBIGINT), "]",
            "] else [", rebI(SQL_C_LONG), "]",
        "]",

        "decimal! [", rebI(SQL_C_DOUBLE), "]",

        "time! [", rebI(SQL_C_TYPE_TIME), "]",

        "date! [",
            "either pick", v, "'time [",  // does it have a time component?
                rebI(SQL_C_TYPE_TIMESTAMP),  // can hold both date and time
            "][",
                rebI(SQL_C_TYPE_DATE),  // just holds the date component
            "]",
        "]",

        "text! [", rebI(SQL_C_WCHAR), "]",

        "blob! [", rebI(SQL_C_BINARY), "]",

        "panic -[Non-SQL-mappable type used in parameter binding]-",
    "]");

    SQLSMALLINT sql_type;

    switch (c_type) {
      case SQL_C_DEFAULT: {  // null
        sql_type = SQL_NULL_DATA;
        assert(rebUnboxLogic("'null =", v));
        p->buffer_size = 0;
        p->buffer = nullptr;
        break; }

      case SQL_C_BIT: {  // [true false]
        sql_type = SQL_BIT;
        p->buffer_size = sizeof(unsigned char);
        p->buffer = rebAllocN(char, p->buffer_size);
        *cast(unsigned char*, p->buffer) = rebUnboxBoolean(rebQ(v));
        break; }

      case SQL_C_ULONG: {  // unsigned INTEGER! in 32-bit positive range
        sql_type = SQL_INTEGER;
        p->buffer_size = sizeof(SQLUINTEGER);
        p->buffer = rebAllocN(char, p->buffer_size);
        *cast(SQLUINTEGER*, p->buffer) = rebUnboxInteger64(v);  // headroom
        break; }

      case SQL_C_LONG: {  // signed INTEGER! in 32-bit negative range
        sql_type = SQL_INTEGER;
        p->buffer_size = sizeof(SQLINTEGER);  // use signed insertion
        p->buffer = rebAllocN(char, p->buffer_size);
        *cast(SQLINTEGER*, p->buffer) = rebUnboxInteger(v);  // signed 32-bit
        break; }

      case SQL_C_UBIGINT: {  // unsigned INTEGER! above 32-bit positive range
        sql_type = SQL_INTEGER;
        p->buffer_size = sizeof(SQLUBIGINT);  // !!! See notes RE: ODBC BIGINT
        p->buffer = rebAllocN(char, p->buffer_size);
        *cast(SQLUBIGINT*, p->buffer) = rebUnboxInteger64(v);
        break; }

      case SQL_C_SBIGINT: {  // signed INTEGER! below 32-bit negative range
        sql_type = SQL_INTEGER;
        p->buffer_size = sizeof(SQLBIGINT);  // !!! See notes RE: ODBC BIGINT
        p->buffer = rebAllocN(char, p->buffer_size);
        *cast(SQLBIGINT*, p->buffer) = rebUnboxInteger64(v);
        break; }

      case SQL_C_DOUBLE: {  // DECIMAL!
        sql_type = SQL_DOUBLE;
        p->buffer_size = sizeof(SQLDOUBLE);
        p->buffer = rebAllocN(char, p->buffer_size);
        *cast(SQLDOUBLE*, p->buffer) = rebUnboxDecimal(v);
        break; }

      case SQL_C_TYPE_TIME: {  // TIME! (fractions not preserved)
        sql_type = SQL_TYPE_TIME;
        p->buffer_size = sizeof(TIME_STRUCT);
        p->buffer = rebAllocN(char, p->buffer_size);

        TIME_STRUCT *time = cast(TIME_STRUCT*, p->buffer);
        time->hour = rebUnboxInteger("pick", v, "'hour");
        time->minute = rebUnboxInteger("pick", v, "'minute");
        time->second = rebUnboxInteger("pick", v, "'second");
        break; }

      case SQL_C_TYPE_DATE: {  // DATE! with no time component
        sql_type = SQL_TYPE_DATE;
        p->buffer_size = sizeof(DATE_STRUCT);
        p->buffer = rebAllocN(char, p->buffer_size);

        DATE_STRUCT *date = cast(DATE_STRUCT*, p->buffer);
        date->year = rebUnboxInteger("pick", v, "'year");
        date->month = rebUnboxInteger("pick", v, "'month");
        date->day = rebUnboxInteger("pick", v, "'day");
        break; }

      case SQL_C_TYPE_TIMESTAMP: {  // DATE! with a time component
        sql_type = SQL_TYPE_TIMESTAMP;
        p->buffer_size = sizeof(TIMESTAMP_STRUCT);
        p->buffer = rebAllocN(char, p->buffer_size);

        Value* time = rebValue("pick", v, "'time");
        Value* second_and_fraction = rebValue("pick", time, "'second");

        // !!! Although we write a `fraction` out, this appears to often
        // be dropped by the ODBC binding:
        //
        // https://github.com/metaeducation/rebol-odbc/issues/1
        //
        TIMESTAMP_STRUCT *stamp = cast(TIMESTAMP_STRUCT*, p->buffer);
        stamp->year = rebUnboxInteger("pick", v, "'year");
        stamp->month = rebUnboxInteger("pick", v, "'month");
        stamp->day = rebUnboxInteger("pick", v, "'day");
        stamp->hour = rebUnboxInteger("pick", time, "'hour");
        stamp->minute = rebUnboxInteger("pick", time, "'minute");
        stamp->second = rebUnboxInteger(
            "to integer! round:down", second_and_fraction
        );
        stamp->fraction = rebUnboxInteger(  // see note above
            "to integer! round:down (",
                second_and_fraction, "mod 1",
            ") * 1000000000"
        );

        rebRelease(second_and_fraction);
        rebRelease(time);
        break; }

        // There's no guarantee that a database will interpret its CHARs
        // as UTF-8, so it might think it's something like a Latin1 string of
        // a longer length.  Hence using database features like "give me all
        // the people with names shorter than 5 characters" might not work
        // as expected.  But find functions should work within the ASCII
        // subset even on databases that don't know what they're dealing with.
        //
      case SQL_C_CHAR: {  // TEXT! when target column is VARCHAR
        size_t encoded_size_no_term;
        switch (g_char_column_encoding) {
          case CHAR_COL_UTF8: {
            unsigned char *utf8 = rebBytes(&encoded_size_no_term, v);
            p->buffer = utf8;
            break; }

          case CHAR_COL_UTF16:
            goto encode_as_utf16;  // if driver can't handle UTF-8

          case CHAR_COL_LATIN1: {
            Value* temp = rebValue(
                "append make blob! length of", v,
                    "map-each 'ch", v, "["
                        "if 255 < to integer! ch ["
                            "panic -[Codepoint too high for Latin1]-"
                         "]"
                         "to integer! ch"
                    "]"
            );
            unsigned char *latin1 = rebBytes(&encoded_size_no_term, temp);
            rebRelease(temp);
            p->buffer = latin1;
            break; }

          default:
            assert(!"Invalid CHAR_COL_XXX enumeration");
            rebJumps ("panic -[Invalid CHAR_COL_XXX enumeration]-");
        }

        sql_type = SQL_VARCHAR;
        p->length = p->column_size = cast(SQLSMALLINT, encoded_size_no_term);
        break; }

        // In the specific case where the target column is an NCHAR, we try
        // to go through the WCHAR based APIs.
        //
        // !!! We also jump here if we don't trust the driver's UTF-8 ability
        // with a SQL_C_CHAR field.  See notes.
        //
      encode_as_utf16:
      case SQL_C_WCHAR: {  // TEXT! when target column is NCHAR
        //
        // Call to get the length of how big a buffer to make, then a second
        // call to fill the buffer after its made.
        //
        // Note: Some ODBC drivers may not support UTF16 and only UCS2.  This
        // means it could give bad displays or length calculations if
        // codepoints > 0xFFFF are used.
        //
        unsigned int num_wchars_no_term = rebSpellIntoWide(nullptr, 0, v);
        SQLWCHAR *chars = rebAllocN(SQLWCHAR, num_wchars_no_term + 1);
        unsigned int check = rebSpellIntoWide(chars, num_wchars_no_term, v);
        assert(check == num_wchars_no_term);
        UNUSED(check);

        sql_type = SQL_WVARCHAR;
        p->buffer_size = sizeof(SQLWCHAR) * num_wchars_no_term;
        p->buffer = chars;
        p->length = p->column_size = cast(SQLSMALLINT, 2 * num_wchars_no_term);
        break; }

    // 1. For VARBINARY columns, column_size should indicate the maximum
    //    column capacity, not necessarily the data size.

      case SQL_C_BINARY: {  // BLOB!
        size_t size;
        unsigned char *bytes = rebBytes(&size, v);

        sql_type = SQL_VARBINARY;
        p->buffer = bytes;
        p->buffer_size = size * sizeof(char);
        p->length = size;

        p->column_size = size;  // see [1]
        break; }

      default:
        rebJumps ("panic -[Unhandled SQL type in switch() statement]-");
    }

    SQLRETURN rc = SQLBindParameter(
        hstmt,  // StatementHandle
        number,  // ParameterNumber
        SQL_PARAM_INPUT,  // InputOutputType
        c_type,  // ValueType
        sql_type,  // ParameterType
        p->column_size,  // ColumnSize
        0,  // DecimalDigits
        p->buffer,  // ParameterValuePtr
        p->buffer_size,  // BufferLength
        &p->length  // StrLen_Or_IndPtr
    );

    return rc;
}


SQLRETURN Get_ODBC_Catalog(
    SQLHSTMT hstmt,
    Value* block
){
    int which = rebUnbox(
        "switch first ensure block! @", block, "[",
            "'tables [1]",
            "'columns [2]",
            "'types [3]",
        "] else [",
            "panic -[Catalog must be TABLES, COLUMNS, or TYPES]-",
        "]"
    );

    rebElide(
        "if 5 < length of", block, "[",
            "panic -[Catalog block should not have more than 4 patterns]-",
        "]"
    );

    SQLWCHAR *pattern[4];
    SQLRETURN rc;

  extract_patterns: {

    int index;
    for (index = 2; index != 6; ++index) {
        pattern[index - 2] = rebSpellWideOpt(  // returns nullptr if NULL
            "ensure [null? text!]",
                "pick ensure block!", block, rebI(index)
        );
    }

} query_catalog: {

    switch (which) {
      case 1:
        rc = SQLTablesW(
            hstmt,
            pattern[2], SQL_NTS,  // catalog
            pattern[1], SQL_NTS,  // schema
            pattern[0], SQL_NTS,  // table
            pattern[3], SQL_NTS  // type
        );
        break;

      case 2:
        rc = SQLColumnsW(
            hstmt,
            pattern[3], SQL_NTS,  // catalog
            pattern[2], SQL_NTS,  // schema
            pattern[0], SQL_NTS,  // table
            pattern[1], SQL_NTS  // column
        );
        break;

      case 3:
        rc = SQLGetTypeInfoW(hstmt, SQL_ALL_TYPES);
        break;

      default:
        assert(false);
        rebJumps ("panic -[Invalid GET_CATALOG_XXX value]-");
    }

} free_patterns: {

    int n;
    for (n = 0; n != 4; ++n)
        rebFree(pattern[n]);  // no-op if nullptr

} panic_on_failure_or_return: {

    if (not SQL_SUCCEEDED(rc))
        rebJumps("panic", Error_ODBC_Stmt(hstmt));

    return rc;
}}


#define COLUMN_TITLE_SIZE 255

static void Force_ColumnList_Cleanup(ColumnList* list) {
    if (list->columns == nullptr)
        return;  // already freed e.g. by SHUTDOWN*

    SQLSMALLINT col_num;
    for (col_num = 0; col_num < list->num_columns; ++col_num) {
        Column* col = &list->columns[col_num];
        rebFreeOpt(col->buffer);
        rebRelease(col->title);
    }
    rebFree(list->columns);
    list->columns = nullptr;
}

static void Column_List_Handle_Cleaner(void* p, size_t length) {
    ColumnList* list = cast(ColumnList*, p);
    UNUSED(length);

    Force_ColumnList_Cleanup(list);

    if (list == g_all_columnlists)
        g_all_columnlists = list->next;
    else {
        ColumnList* temp = g_all_columnlists;
        while (temp->next != list)
            temp = temp->next;
        temp->next = temp->next->next;
    }

    rebFree(list);
}


//
// Sets up the COLUMNS description, retrieves column titles and descriptions
//
void Describe_ODBC_Results(
    SQLHSTMT hstmt,
    int num_columns,
    Column* columns
){
    SQLSMALLINT column_index;
    for (column_index = 1; column_index <= num_columns; ++column_index) {
        Column* col = &columns[column_index - 1];

        SQLWCHAR title[COLUMN_TITLE_SIZE];
        SQLSMALLINT title_length;

        SQLRETURN rc = SQLDescribeColW(
            hstmt,
            column_index,
            &title[0],
            COLUMN_TITLE_SIZE,
            &title_length,
            &col->sql_type,
            &col->column_size,
            &col->precision,
            &col->nullable
        );
        if (not SQL_SUCCEEDED(rc))
            rebJumps ("panic", Error_ODBC_Stmt(hstmt));

        col->title = rebLengthedTextWide(title, title_length);
        rebUnmanage(col->title);

        // Numeric types may be signed or unsigned, which informs how to
        // interpret the bits that come back when turned into a Rebol value.
        // A separate API call is needed to detect that.

        SQLLEN numeric_attribute; // Note: SQLINTEGER won't work

        rc = SQLColAttribute(
            hstmt,  // StatementHandle
            column_index,  // ColumnNumber
            SQL_DESC_UNSIGNED,  // FieldIdentifier, see the other SQL_DESC_XXX
            nullptr,  // CharacterAttributePtr
            0,  // BufferLength
            nullptr,  // StringLengthPtr
            &numeric_attribute  // only parameter needed for SQL_DESC_UNSIGNED
        );
        if (not SQL_SUCCEEDED(rc))
            rebJumps ("panic", Error_ODBC_Stmt(hstmt));

        if (numeric_attribute == SQL_TRUE)
            col->is_unsigned = true;
        else {
            assert(numeric_attribute == SQL_FALSE);
            col->is_unsigned = false;
        }

        // We *SHOULD* be able to rely on the `sql_type` that SQLDescribeCol()
        // gives us, but SQLite returns SQL_VARCHAR for other column types.
        // As a workaround that shouldn't do any harm on non-SQLite databases,
        // we double-check the string name of the column; and use the string
        // name to override if it isn't actually a VARCHAR:
        // https://stackoverflow.com/a/58438457/
        //
        // Additionally, it seems that even if you call `SQLColAttribute` and
        // not `SQLColAttributeW`, the Windows driver still gives back wide
        // characters for the type name.  So use the W version, despite that
        // type names are really just ASCII:
        // https://github.com/metaeducation/rebol-odbc/issues/7
        //
      #if defined(USE_SQLITE_DESCRIBECOL_WORKAROUND)
        if (col->sql_type == SQL_VARCHAR) {
            SQLWCHAR type_name[32];  // Note: `typename` is a C++ keyword
            SQLSMALLINT type_name_len;
            rc = SQLColAttributeW(  // See above for why the "W" version used
                hstmt,  // StatementHandle
                column_index,  // ColumnNumber
                SQL_DESC_TYPE_NAME,  // FieldIdentifier, see SQL_DESC_XXX list
                &type_name,  // CharacterAttributePtr
                32,  // BufferLength
                &type_name_len,  // StringLengthPtr
                nullptr  // NumericAttributePtr, not needed w/string attribute
            );

            // The type that comes back doesn't have any size attached.  But
            // it may be upper or lower case, and perhaps mixed--e.g. if it
            // preserves whatever case the user typed in their SQL.  (MySQL
            // seems to report lowercase--for what it's worth.)
            //
            // We use Rebol code to do the comparison since it's automatically
            // case insensitive.  It's not super fast, but this only happens
            // once per query--not per row.
            //
            Value* type_name_rebval = rebTextWide(type_name);
            col->sql_type = rebUnboxInteger(
                "switch", type_name_rebval, "[",
                    "-[VARCHAR]- [", rebI(SQL_VARCHAR), "]",  // make fastest

                    "-[BINARY]- [", rebI(SQL_BINARY), "]",
                    "-[VARBINARY]- [", rebI(SQL_VARBINARY), "]",
                    "-[CHAR]- [", rebI(SQL_CHAR), "]",
                    "-[NCHAR]- [", rebI(SQL_WCHAR), "]",
                    "-[NVARCHAR]- [", rebI(SQL_WVARCHAR), "]",
                    "-[DECIMAL]- [", rebI(SQL_DECIMAL), "]",
                "] else [",
                    "panic [",
                        "-[SQL_VARCHAR reported by ODBC for unknown type:]-",
                        type_name_rebval,
                    "]",
                "]"
            );
            rebRelease(type_name_rebval);
        }
      #endif

        // With the SQL_type hopefully accurate, pick an implementation type
        // to use when querying for columns of that type.
        //
        switch (col->sql_type) {
          case SQL_BIT:
            col->c_type = SQL_C_BIT;
            col->buffer_size = sizeof(unsigned char);
            break;

          // You should be able to request an UNSIGNED TINYINT as a SQL_C_ULONG
          // but this is broken in MySQL's ODBC driver (at least as of 9.5.0).
          // A stored `255` comes back as `4294967295` when the buffer is cast
          // to a `SQLUINTEGER`.  (The MariaDB driver behaves correctly.)  Use
          // the exact type instead as a workaround.
          //
          case SQL_TINYINT: {
            if (col->is_unsigned) {
                col->c_type = SQL_C_UTINYINT;
                col->buffer_size = sizeof(unsigned char);
            }
            else {
                col->c_type = SQL_C_SLONG;  // just use signed int
                col->buffer_size = sizeof(SQLINTEGER);
            }
            break; }

          case SQL_SMALLINT:
          case SQL_INTEGER:
            if (col->is_unsigned) {
                col->c_type = SQL_C_ULONG;
                col->buffer_size = sizeof(SQLUINTEGER);
            }
            else {
                col->c_type = SQL_C_SLONG;
                col->buffer_size = sizeof(SQLINTEGER);
            }
            break;

        // We could ask the driver to give all integer types back as BIGINT,
        // but driver support may be more sparse for this...so only use the
        // 64-bit datatypes if absolutely necessary.

          case SQL_BIGINT:
            if (col->is_unsigned) {
                col->c_type = SQL_C_UBIGINT;
                col->buffer_size = sizeof(SQLUBIGINT);
            }
            else {
                col->c_type = SQL_C_SBIGINT;
                col->buffer_size = sizeof(SQLBIGINT);
            }
            break;

          case SQL_DECIMAL:
          case SQL_NUMERIC:
          case SQL_REAL:
          case SQL_FLOAT:
          case SQL_DOUBLE:
            col->c_type = SQL_C_DOUBLE;
            col->buffer_size = sizeof(SQLDOUBLE);
            break;

          case SQL_TYPE_DATE:
            col->c_type = SQL_C_TYPE_DATE;
            col->buffer_size = sizeof(DATE_STRUCT);
            break;

          case SQL_TYPE_TIME:
            col->c_type = SQL_C_TYPE_TIME;
            col->buffer_size = sizeof(TIME_STRUCT);
            break;

          case SQL_TYPE_TIMESTAMP:
            col->c_type = SQL_C_TYPE_TIMESTAMP;
            col->buffer_size = sizeof(TIMESTAMP_STRUCT);
            break;

          case SQL_BINARY:
          case SQL_VARBINARY:
          case SQL_LONGVARBINARY:
            col->c_type = SQL_C_BINARY;
            col->buffer_size = 0;
            break;

          case SQL_CHAR:
          case SQL_VARCHAR:
            if (g_char_column_encoding == CHAR_COL_UTF16)
                goto decode_as_utf16;  // !!! see notes on CHAR_COL_UTF16

            col->c_type = SQL_C_CHAR;

            // "The driver counts the null-termination character when it
            // returns character data to *TargetValuePtr.  *TargetValuePtr
            // must therefore contain space for the null-termination character
            // or the driver will truncate the data"
            //
            col->buffer_size = col->column_size + 1;
            break;

          decode_as_utf16:
          case SQL_WCHAR:
          case SQL_WVARCHAR:
            col->c_type = SQL_C_WCHAR;

            // See note above in the non-(W)ide SQL_CHAR/SQL_VARCHAR cases.
            //
            col->buffer_size = sizeof(WCHAR) * (col->column_size + 1);
            break;

          case SQL_LONGVARCHAR:
            if (g_char_column_encoding == CHAR_COL_UTF16)
                goto decode_as_long_utf16;  // !!! see notes on CHAR_COL_UTF16

            col->c_type = SQL_C_CHAR;

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
            col->buffer_size = (32700 + 1);
            break;

          decode_as_long_utf16:
          case SQL_WLONGVARCHAR:
            col->c_type = SQL_C_WCHAR;

            // See note above in the non-(W)ide SQL_LONGVARCHAR case.
            //
            col->buffer_size = sizeof(WCHAR) * (32700 + 1);
            break;

          default:  // used to allocate character buffer based on column size
            rebJumps ("panic -[Unknown column SQL_XXX type]-");
        }

        if (col->buffer_size == 0)
            col->buffer = nullptr;
        else {
            col->buffer = rebTryAllocN(char, col->buffer_size);
            if (col->buffer == nullptr)
                rebJumps ("panic -[Couldn't allocate column buffer!]-");
            rebUnmanageMemory(col->buffer);
        }
    }
}


//
//  export insert-odbc: native [
//
//  "Executes SQL statements (prepare on first pass, executes conservatively)"
//
//      return: "Row count for row change, column title BLOCK! for selects"
//          [integer! block!]
//      statement [object!]
//      sql "Dialect beginning with TABLES, COLUMNS, TYPES, or SQL STRING!"
//          [block!]
//  ]
//
DECLARE_NATIVE(INSERT_ODBC)
{
    INCLUDE_PARAMS_OF_INSERT_ODBC;

    SQLHSTMT hstmt = rebUnboxHandle(
        SQLHSTMT*, "ensure handle! statement.hstmt"
    );

    SQLRETURN rc;
    rc = SQLFreeStmt(hstmt, SQL_RESET_PARAMS);  // !!! check rc?
    rc = SQLCloseCursor(hstmt);  // !!! check rc?
    UNUSED(rc);

    //=//// MAKE SQL REQUEST FROM DIALECTED SQL BLOCK /////////////////////=//
    //
    // The block passed in is used to form a query.

    bool use_cache = false;

    bool get_catalog = rebUnboxBoolean(
        "switch:type first sql [",
            "lit-word?/ ['true]",  // like Rebol2: 'tables, 'columns, 'types
            "text! ['false]",
        "] else [",
            "panic [",
                "-[SQL dialect must start with WORD! or TEXT! value:]- @sql"
            "]",
        "]"
    );

    if (get_catalog) {
        Value* sql = rebValue("sql");
        rc = Get_ODBC_Catalog(hstmt, sql);
        rebRelease(sql);
    }
    else {
        // Prepare/Execute statement, when first element in the block is a
        // (statement) string

        // Compare with previously prepared statement, and if not the same,
        // then prepare a new statement.
        //
        use_cache = rebUnboxLogic(
            "equal? (first sql) ensure [null? text!] statement.string"
        );

        SQLLEN sql_index = 1;

        if (not use_cache) {
            SQLWCHAR *sql_string = rebSpellWide("first sql");

            rc = SQLPrepareW(
                hstmt,
                sql_string,
                SQL_NTS  // Null-Terminated String
            );
            if (not SQL_SUCCEEDED(rc))
                return rebDelegate("panic", Error_ODBC_Stmt(hstmt));

            rebFree(sql_string);

            // Remember statement string handle, but keep a copy since it
            // may be mutated by the user.
            //
            // !!! Could re-use value with existing series if read only
            //
            rebElide("statement.string: copy first sql");
        }

        // The SQL string may contain ? characters, which indicates that it is
        // a parameterized query.  The separation of the parameters into a
        // different quarantined part of the query is to protect against SQL
        // injection.

        SQLLEN num_params = rebUnbox("length of sql") - sql_index;

        ++sql_index;

        Parameter* params = nullptr;
        if (num_params != 0) {
            params = rebAllocN(Parameter, num_params);

            SQLLEN n;
            for (n = 0; n < num_params; ++n, ++sql_index) {
                Value* value = rebValue("pick sql", rebI(sql_index));
                rc = ODBC_BindParameter(
                    hstmt,
                    &params[n],
                    n + 1,
                    value
                );
                rebRelease(value);
                if (not SQL_SUCCEEDED(rc))
                    return rebDelegate("panic", Error_ODBC_Stmt(hstmt));
            }
        }

        // Execute statement, but don't check result code until after the
        // parameters and their data buffers have been freed.
        //
        rc = SQLExecute(hstmt);

        if (num_params != 0) {
            SQLLEN n;
            for (n = 0; n != num_params; ++n) {
                if (params[n].buffer != nullptr)
                    rebFree(params[n].buffer);
            }
            rebFree(params);
        }

        switch (rc) {
          case SQL_SUCCESS:
          case SQL_SUCCESS_WITH_INFO:
            break;

          case SQL_NO_DATA:  // UPDATE, INSERT, or DELETE affecting no rows
            break;

          case SQL_NEED_DATA:
            assert(!"SQL_NEED_DATA seen...only happens w/data @ execution");
            return rebDelegate("panic", Error_ODBC_Stmt(hstmt));

          case SQL_STILL_EXECUTING:
            assert(!"SQL_STILL_EXECUTING seen...only w/async calls");
            return rebDelegate("panic", Error_ODBC_Stmt(hstmt));

          case SQL_ERROR:
            return rebDelegate("panic", Error_ODBC_Stmt(hstmt));

          case SQL_INVALID_HANDLE:
            assert(!"SQL_INVALID_HANDLE seen...should never happen");
            return rebDelegate("panic", Error_ODBC_Stmt(hstmt));

        #if ODBCVER >= 0x0380
          case SQL_PARAM_DATA_AVAILABLE:
            assert(!"SQL_PARAM_DATA_AVAILABLE seen...only in ODBC 3.8");
            return rebDelegate("panic", Error_ODBC_Stmt(hstmt));
        #endif
        }
    }

    //=//// RETURN RECORD COUNT IF NO RESULT ROWS /////////////////////////=//
    //
    // Insert or Update or Delete statements do not return records, and this
    // is indicated by a 0 count for columns in the return result.

    SQLSMALLINT num_columns;
    rc = SQLNumResultCols(hstmt, &num_columns);
    if (not SQL_SUCCEEDED(rc))
        return rebDelegate("panic", Error_ODBC_Stmt(hstmt));

    if (num_columns == 0) {
        SQLLEN num_rows;
        rc = SQLRowCount(hstmt, &num_rows);
        if (not SQL_SUCCEEDED(rc))
            return rebDelegate("panic", Error_ODBC_Stmt(hstmt));

        return rebInteger(num_rows);
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

    if (use_cache)
        return rebValue("ensure block! statement.titles");

    Value* old_columns_value = rebValue(
        "ensure [null? handle!] statement.columns"
    );
    if (old_columns_value) {
        //
        // Because we have the HANDLE! here we could go ahead and free the
        // columnlist itself (not just the columns), but that would mean the
        // GC of the HANDLE! would need to detect nulls.  Just let the GC do
        // the free.
        //
        ColumnList* old_list = rebUnboxHandle(ColumnList*, old_columns_value);
        Force_ColumnList_Cleanup(old_list);
        rebRelease(old_columns_value);
    }

    ColumnList* list = rebAlloc(ColumnList);
    rebUnmanageMemory(list);

    list->columns = rebAllocN(Column, num_columns);
    rebUnmanageMemory(list->columns);

    list->num_columns = num_columns;
    list->next = g_all_columnlists;
    g_all_columnlists = list;

    Value* columns_value = rebHandle(list, 1, &Column_List_Handle_Cleaner);

    rebElide("statement.columns:", rebR(columns_value));

    Describe_ODBC_Results(hstmt, num_columns, list->columns);

    Value* titles = rebValue("make block!", rebI(num_columns));
    SQLSMALLINT column_index;
    for (column_index = 1; column_index <= num_columns; ++column_index)
        rebElide("append", titles, list->columns[column_index - 1].title);

    // remember column titles if next call matches, return them as the result
    //
    rebElide("statement.titles:", titles);
    return titles;
}


//
// A query will fill a column's buffer with data.  This data can be
// reinterpreted as a Rebol value.  Successive queries for records reuse the
// buffer for a column.
//
Value* ODBC_Column_To_Rebol_Value(
    Column* col,
    Option(SQLPOINTER) allocated,
    SQLLEN len
){
    if (len == SQL_NULL_DATA)
        return nullptr;

    switch (col->c_type) {
      case SQL_C_BIT:
        //
        // Note: MySQL ODBC returns -2 for sql_type when a field is BIT(n)
        // where n != 1, as opposed to SQL_BIT and column_size of n.  See
        // remarks on the panic() below.
        //
        if (len != 1)
            rebJumps("panic -[BIT(n) fields are only supported for n = 1]-");

        if (*cast(unsigned char*, col->buffer))
            return rebValue("'true");  // can't append antiform to block :-(
        return rebValue("'false");

       case SQL_C_UTINYINT:  // unsigned: 0..255
        return rebInteger(*cast(unsigned char*, col->buffer));

    // ODBC was asked at SQLGetData time to give back *most* integer
    // types as SQL_C_SLONG or SQL_C_ULONG, regardless of actual size
    // in the sql_type (not the c_type)

      case SQL_C_SLONG:  // signed: -32,768..32,767
        return rebInteger(*cast(SQLINTEGER*, col->buffer));

      case SQL_C_ULONG:  // signed: -2[31]..2[31] - 1
        return rebInteger64(*cast(SQLUINTEGER*, col->buffer));  // headroom

    // Special exception made for big integers, where seemingly MySQL
    // would not properly map smaller types into big integers if all
    // you ask for are big ones.
    //
    // !!! Review: bug may not exist if SQLGetData() is used.

      case SQL_C_SBIGINT:  // signed: -2[63]..2[63]-1
        return rebInteger64(*cast(SQLBIGINT*, col->buffer));

      case SQL_C_UBIGINT:  // unsigned: 0..2[64] - 1
        if (*cast(SQLUBIGINT*, col->buffer) > INT64_MAX)
            rebJumps ("panic -[INTEGER! can't hold all unsigned 64-bit ints]-");

        return rebInteger64(*cast(SQLUBIGINT*, col->buffer));

    // ODBC was asked at column binding time to give back all floating
    // point types as SQL_C_DOUBLE, regardless of actual size.

      case SQL_C_DOUBLE:
        return rebDecimal(*cast(SQLDOUBLE*, col->buffer));

      case SQL_C_TYPE_DATE: {
        DATE_STRUCT* date = cast(DATE_STRUCT*, col->buffer);
        return rebValue(
            "make date! [",
                rebI(date->year), rebI(date->month), rebI(date->day),
            "]"
        ); }

      case SQL_C_TYPE_TIME: {
        //
        // The TIME_STRUCT in ODBC does not contain a fraction/nanosecond
        // component.  Hence a TIME(7) might be able to store 17:32:19.123457
        // but when it is retrieved it will just be 17:32:19
        //
        TIME_STRUCT* time = cast(TIME_STRUCT*, col->buffer);
        return rebValue(
            "make time! [",
                rebI(time->hour), rebI(time->minute), rebI(time->second),
            "]"
        ); }

    // Note: It's not entirely clear how to work with timezones in ODBC, there
    // is a datatype called SQL_SS_TIMESTAMPOFFSET_STRUCT which extends
    // TIMESTAMP_STRUCT with timezone_hour and timezone_minute.  Someone can
    // try and figure this out in the future if they are so inclined.

      case SQL_C_TYPE_TIMESTAMP: {
        TIMESTAMP_STRUCT* stamp = cast(TIMESTAMP_STRUCT*, col->buffer);

        // !!! The fraction is generally 0, even if you wrote a nonzero value
        // in the timestamp:
        //
        // https://github.com/metaeducation/rebol-odbc/issues/1
        //
        SQLUINTEGER fraction = stamp->fraction;

        // !!! This isn't a very elegant way of combining a date and time
        // component, but the point is that however it is done...it should
        // be done with Rebol code vs. some special C date API.  See
        // GitHub issue #2313 regarding improving the Rebol side.
        //
        return rebValue("ensure date! (make-date-ymdsnz",
            rebI(stamp->year),
            rebI(stamp->month),
            rebI(stamp->day),
            rebI(
                stamp->hour * 3600 + stamp->minute * 60 + stamp->second
            ),  // seconds
            rebI(fraction),  // billionths of a second (nanoseconds)
            "^void",  // timezone (none)  !!! better API way to make voids?
        ")"); }

    // SQL_BINARY, SQL_VARBINARY, and SQL_LONGVARBINARY were all requested
    // as SQL_C_BINARY.

      case SQL_C_BINARY:
        if (allocated)
            return rebRepossess(unwrap allocated, len);
        return rebSizedBlob(col->buffer, len);

    // There's no guarantee that CHAR fields contain valid UTF-8, but we
    // currently only support that.
    //
    // !!! Should there be a Latin1 fallback if the UTF-8 interpretation
    // fails?

      case SQL_C_CHAR: {
        switch (g_char_column_encoding) {
          case CHAR_COL_UTF8:
            return rebSizedText(
                cast(char*, col->buffer),  // unixodbc SQLCHAR is unsigned
                len
            );

          case CHAR_COL_UTF16:
            assert(!"UTF-16/UCS-2 should have requested SQL_C_WCHAR");
            break;

          case CHAR_COL_LATIN1: {
            // Need to do a UTF-8 conversion for Rebol to use the string.
            //
            // !!! This is a slow way to do it; but optimize when needed.
            // (Should there be rebSizedTextLatin1() ?)
            //
            Value* binary = rebSizedBlob(
                cast(unsigned char*, col->buffer),
                len
            );
            return rebValue(
                "append make text!", rebI(len),
                    "map-each 'byte", rebR(binary), "[make-char byte]"
            ); }
        }
        break; }

      case SQL_C_WCHAR:
        assert(len % 2 == 0);
        return rebLengthedTextWide(
            cast(SQLWCHAR*, col->buffer),
            len / 2
        );

      default:
        break;
    }

    // Note: This happens with BIT(2) and the MySQL ODBC driver, which
    // reports a sql_type of -2 for some reason.
    //
    rebJumps("panic -[Unsupported SQL_XXX type returned from query]-");
}


//
//  export copy-odbc: native [
//
//      return: "Block of row blocks for selects and catalog functions"
//          [block!]
//      statement [object!]
//      :part [integer!]
//  ]
//
DECLARE_NATIVE(COPY_ODBC)
{
    INCLUDE_PARAMS_OF_COPY_ODBC;

    SQLHSTMT hstmt = rebUnboxHandle(SQLHSTMT,
        "ensure handle! statement.hstmt"
    );

    ColumnList* list = rebUnboxHandle(ColumnList*,
        "ensure handle! statement.columns"
    );
    Column* columns = list->columns;

    if (hstmt == SQL_NULL_HANDLE or not columns)
        return "panic -[Invalid statement object!]-";

    SQLRETURN rc;

    SQLSMALLINT num_columns;
    rc = SQLNumResultCols(hstmt, &num_columns);
    if (not SQL_SUCCEEDED(rc))
        return rebDelegate("panic", Error_ODBC_Stmt(hstmt));

    // compares-0 based row against num_rows, so -1 is chosen to never match
    // and hence mean "as many rows as available"
    //
    SQLLEN num_rows = rebUnbox("any [part, -1]");

    Value* results = rebValue(
        "make block!", rebI(num_rows == -1 ? 10 : num_rows)
    );

    SQLLEN row = 0;
    for (; row != num_rows; row = (num_rows == -1) ? 0 : row + 1) {

        // This SQLFetch operation "fetches" the next row.  If we were using
        // column binding, it would be writing data into the memory buffers
        // we had given it.  But if you use column binding, your buffers have
        // to be fixed size...and when they're not big enough, you lose the
        // data.  By avoiding column binding, we can grow our buffers through
        // multiple successive calls to SQLGetData().
        //
        rc = SQLFetch(hstmt);

        switch (rc) {
          case SQL_SUCCESS:
            break;  // Row retrieved, and data copied into column buffers

          case SQL_SUCCESS_WITH_INFO: {
            SQLWCHAR state[6];
            SQLINTEGER native;

            SQLSMALLINT message_len = 0;

            // !!! It seems you wouldn't need the SQLWCHAR version for this,
            // but Windows complains if you use SQLCHAR and try to call the
            // non-W version.  :-/  Review.
            //
            rc = SQLGetDiagRecW(
                SQL_HANDLE_STMT,  // HandleType
                hstmt,  // Handle
                1,  // RecNumber
                state,  // SQLState
                &native,  // NativeErrorPointer
                nullptr,  // MessageText
                0,  // BufferLength
                &message_len  // TextLengthPtr
            );

            // Right now we ignore the "info" if there was success, but
            // `state` is what you'd examine to know what the information is.
            //
            break; }

          case SQL_NO_DATA:
            goto no_more_data;

          case SQL_INVALID_HANDLE:
          case SQL_STILL_EXECUTING:
          case SQL_ERROR:
          default:  // No other return codes were listed
            return rebDelegate("panic", Error_ODBC_Stmt(hstmt));
        }

        Value* record = rebValue("make block!", rebI(num_columns));

        SQLSMALLINT column_index;
        for (column_index = 1; column_index <= num_columns; ++column_index) {
            Column* col = &columns[column_index - 1];

            if (col->buffer == nullptr)
                assert(col->buffer_size == 0);

            SQLLEN len;
            rc = SQLGetData(
                hstmt,
                column_index,
                col->c_type,
                col->buffer ? col->buffer : g_dummy_buffer,  // can't be null
                col->buffer_size,  // zero if null
                &len
            );

            if (col->buffer == nullptr and len == SQL_NO_TOTAL)
                return "panic -[ODBC gave SQL_NO_TOTAL for var-size field]-";

            Option(SQLPOINTER) allocated;

            switch (rc) {
              case SQL_SUCCESS:
                if (
                    len != SQL_NULL_DATA
                    and len > cast(SQLLEN, col->buffer_size)
                ){
                    assert(col->buffer == nullptr);  // Firebase does this (!)
                    goto success_with_info;
                }
                allocated = nullptr;
                break;

              success_with_info:
              case SQL_SUCCESS_WITH_INFO:  // potential truncation
                assert(len != SQL_NULL_DATA);
                assert(len != SQL_NO_TOTAL);
                assert(col->buffer == nullptr);
                allocated = rebAllocBytes(len + 1);  // can be rebRepossess()'d

                if (len != 0) {  // MariaDB ODBC won't let you call with 0 len
                    SQLLEN len_check;
                    rc = SQLGetData(
                        hstmt,
                        column_index,
                        col->c_type,
                        unwrap allocated,
                        len,  // amount of space in buffer
                        &len_check
                    );
                    if (rc != SQL_SUCCESS)
                        return rebDelegate("panic", Error_ODBC_Stmt(hstmt));

                    assert(len_check == len);
                }
                break;

              case SQL_NO_DATA:
                assert("!Got back SQL_NO_DATA from SQLGetData()");
                goto no_more_data;

              case SQL_ERROR:
              case SQL_STILL_EXECUTING:
              case SQL_INVALID_HANDLE:
              default:  // No other return codes were listed
                return rebDelegate("panic", Error_ODBC_Stmt(hstmt));
            }

            Value* temp = ODBC_Column_To_Rebol_Value(col, allocated, len);

            rebElide(
                "append", record, rebQ(temp)  // rebQ for ~null~ true false
            );
            rebRelease(temp);
        }

        rebElide("append", results, rebR(record));
    }

  no_more_data:

    return results;
}


//
//  export update-odbc: native [
//
//      return: []
//      connection [object!]
//      access [logic?]
//      commit [logic?]
//  ]
//
DECLARE_NATIVE(UPDATE_ODBC)
{
    INCLUDE_PARAMS_OF_UPDATE_ODBC;

    // Get connection handle
    //
    SQLHDBC hdbc = rebUnboxHandle(SQLHDBC, "ensure handle! connection.hdbc");

    SQLRETURN rc;

    bool access = rebUnboxLogic("access");
    rc = SQLSetConnectAttr(
        hdbc,
        SQL_ATTR_ACCESS_MODE,
        p_cast(
            SQLPOINTER*,
            u_cast(uintptr_t,
                access ? SQL_MODE_READ_WRITE : SQL_MODE_READ_ONLY
            )
        ),
        SQL_IS_UINTEGER
    );

    if (not SQL_SUCCEEDED(rc))
        return rebDelegate("panic", Error_ODBC_Dbc(hdbc));

    bool commit = rebUnboxLogic("commit");
    rc = SQLSetConnectAttr(
        hdbc,
        SQL_ATTR_AUTOCOMMIT,
        p_cast(
            SQLPOINTER*,
            u_cast(uintptr_t,
                commit ? SQL_AUTOCOMMIT_ON : SQL_AUTOCOMMIT_OFF
            )
        ),
        SQL_IS_UINTEGER
    );

    if (not SQL_SUCCEEDED(rc))
        return rebDelegate("panic", Error_ODBC_Dbc(hdbc));

    return "~";
}


//
//  export close-statement: native [
//
//      return: [logic?]
//      statement [object!]
//  ]
//
DECLARE_NATIVE(CLOSE_STATEMENT)
{
    INCLUDE_PARAMS_OF_CLOSE_STATEMENT;

    Value* columns_value = rebValue(
        "ensure [null? handle!] statement.columns"
    );
    if (columns_value) {
        ColumnList* list = rebUnboxHandle(ColumnList*, columns_value);
        Force_ColumnList_Cleanup(list);
        rebElide("statement.columns: null");

        rebRelease(columns_value);
    }

    Value* hstmt_value = rebValue("ensure [null? handle!] statement.hstmt");
    if (hstmt_value) {
        SQLHSTMT hstmt = rebUnboxHandle(SQLHSTMT, hstmt_value);
        assert(hstmt);

        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);

        rebModifyHandleCData(hstmt_value, SQL_NULL_HANDLE);
        rebModifyHandleCleaner(hstmt_value, nullptr);

        rebElide("statement.hstmt: null");

        rebRelease(hstmt_value);
    }

    return rebLogic(true);
}


//
//  export close-connection: native [
//
//      return: [logic?]
//      connection [object!]
//  ]
//
DECLARE_NATIVE(CLOSE_CONNECTION)
{
    INCLUDE_PARAMS_OF_CLOSE_CONNECTION;

    Value* hdbc_value = rebValue("ensure [null? handle!] connection.hdbc");
    if (not hdbc_value)  // connection was already closed (be tolerant?)
        return rebLogic(false);

    Connection* conn = rebUnboxHandle(Connection*, hdbc_value);
    rebRelease(hdbc_value);

    // We clean up the connection but do not free it; that can only be done
    // if all HANDLE! instances pointing to it are known to be gone.  (We are
    // eliminating one instance but someone might have copied the connection
    // object, for example.)
    //
    Force_Connection_Cleanup(conn);

    rebElide("connection.hdbc: null");

    // We could reference count how many connections were open and close the
    // global `henv` here if that seemed important (vs waiting for SHUTDOWN*).
    // But that could also slow down opening another connection, so favor
    // less complexity for now.

    return rebLogic(true);
}


//
//  startup*: native [
//
//  "Start up the ODBC Extension"
//
//      return: []
//  ]
//
DECLARE_NATIVE(STARTUP_P)
//
// To use ODBC you must initialize a SQL_HANDLE_ENV.  We do this lazily in
// OPEN-CONNECTION vs. at startup, so you don't pay for it unless you actually
// use ODBC features in the session.
{
    INCLUDE_PARAMS_OF_STARTUP_P;

    assert(henv == SQL_NULL_HANDLE);

    assert(g_all_connections == nullptr);
    assert(g_all_columnlists == nullptr);

    return "~";
}


//
//  shutdown*: native [
//
//  "Shut down the ODBC Extension"
//
//      return: []
//  ]
//
DECLARE_NATIVE(SHUTDOWN_P)
//
// We have to "neutralize" all the HANDLE! objects that we have allocated when
// the extension unloads.  Because if we don't, the final garbage collect pass
// will try to call the cleanup functions during core shutdown, which is too
// late--the API itself is shutdown (so functions like rebRelease would crash)
//
// There's really not a way in a garbage collected system such as this to shut
// down in "phases", e.g. where all the "user" objects are GC'd so we can trust
// we reach the ODBC extension shutdown with 0 extant connections.  Even if
// that were a coherent idea, you'd still have problems if one extension were
// holding on to handles from another--what order would they shut down in?
{
    INCLUDE_PARAMS_OF_SHUTDOWN_P;

    // There are extant pointers in HANDLE! values to the parameters, columns,
    // and connections or else they wouldn't be in the list!  So we can't
    // free the memory for them, we can only do the cleanup and mark them
    // no longer in use so that when the handles are later processed they
    // know to only free the associated memory.

    ColumnList* list = g_all_columnlists;
    for (; list != nullptr; list = list->next)
        Force_ColumnList_Cleanup(list);

    Connection* conn = g_all_connections;
    for (; conn != nullptr; conn = conn->next)
        Force_Connection_Cleanup(conn);

    if (henv != SQL_NULL_HANDLE) {
        SQLFreeHandle(SQL_HANDLE_ENV, henv);
        henv = SQL_NULL_HANDLE;
    }

    return "~";
}
