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
// Copyright 2017-2021 Ren-C Open Source Contributors
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
//     ["select * from tables where (name = ?) and (age = ?)" {Brian} 42]
//
// The ? notation for substitution points is what is known as a "parameterized
// query".  The reason it is supported at the driver level (instead of making
// the usermode Rebol code merge into a single string) is to make it easier to
// defend against SQL injection attacks.  This way, the scheme code does not
// need to worry about doing SQL-syntax-aware string escaping.

#ifdef TO_WINDOWS
    #define WIN32_LEAN_AND_MEAN  // trim down the Win32 headers
    #include <windows.h>
    #undef IS_ERROR
#endif

#include "sys-core.h"

#include "tmp-mod-odbc.h"

// https://stackoverflow.com/q/58438456
#define USE_SQLITE_DESCRIBECOL_WORKAROUND

#include <sql.h>
#include <sqlext.h>

// The version of ODBC that this is written to use is 3.0, which was released
// around 1995.  At time of writing (2017) it is uncommon to encounter ODBC
// systems that don't implement at least that.  It's not clear if ODBCVER is
// actually standard or not, so define it to 3.0 if it isn't.
// https://stackoverflow.com/q/58443534
//
#ifndef ODBCVER
    #define ODBCVER 0x0300
#endif


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


typedef struct {
    SQLULEN column_size;
    SQLPOINTER buffer;
    SQLULEN buffer_size;
    SQLLEN length;
} PARAMETER;  // For binding parameters

typedef struct {
    REBVAL *title;  // a TEXT!
    SQLSMALLINT sql_type;
    SQLSMALLINT c_type;
    SQLULEN column_size;
    SQLPOINTER buffer;
    SQLULEN buffer_size;
    SQLLEN length;
    SQLSMALLINT precision;
    SQLSMALLINT nullable;
    bool is_unsigned;
} COLUMN;  // For describing columns


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

REBVAL *Error_ODBC_Core(
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
            "make error!", rebR(rebLengthedTextWide(message, message_len))
        );
    }

    // The following errors should not happen, so it's good in the debug
    // build to have a bit more information about exactly which ODBC API
    // call is having the problem.
    //
  #ifdef DEBUG_STDIO_OK
    printf("!! Couldn't get ODBC Error Message: %s @ %d\n", file, line);
  #else
    UNUSED(file);
    UNUSED(line);
  #endif

    switch (rc) {
      case SQL_INVALID_HANDLE:
        return rebValue(
            "make error! {Internal ODBC extension error (invalid handle)}"
        );

      case SQL_ERROR:
        return rebValue(
            "make error! {Internal ODBC extension error (bad diag record #)"
        );

      case SQL_NO_DATA:
        return rebValue(
            "make error! {No ODBC diagnostic information available}"
        );

      default:
        break;  // should not happen if the ODBC interface/driver are working
    }

    assert(!"SQLGetDiagRecW returned undocumented SQLRESULT value");
    return rebValue("make error! {Undocumented SQLRESULT in SQLGetDiagRecW}");
}

#if defined(DEBUG_STDIO_OK)  // report file/line info with mystery errors
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
//
// !!! There may be an ordering issue, that closing the environment before
// closing a database connection (for example) causes errors...so the handles
// may actually need to account for that by linking to each other's managed
// array and cleaning up their dependent handles before freeing themselves.

static void cleanup_hdbc(const REBVAL *v) {
    SQLHDBC hdbc = cast(SQLHDBC, VAL_HANDLE_VOID_POINTER(v));
    if (hdbc == SQL_NULL_HANDLE)
        return;  // already cleared out by CLOSE-ODBC

    SQLDisconnect(hdbc);
    SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
}

static void cleanup_henv(const REBVAL *v) {
    SQLHENV henv = cast(SQLHENV, VAL_HANDLE_VOID_POINTER(v));
    if (henv == SQL_NULL_HANDLE)
        return;  // already cleared out by CLOSE-ODBC

    SQLFreeHandle(SQL_HANDLE_ENV, henv);
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
enum CharColumnEncoding {
    CHAR_COL_UTF8,
    //
    // !!! Should we offer a CHAR_COL_UCS2, which errors if you use any
    // codepoints higher than 0xFFFF ?  (Right now that just uses UTF-16.)
    //
    CHAR_COL_UTF16,
    CHAR_COL_LATIN1
};

// For now, default to the most conservative choice...which is to let the
// driver/driver-manager do the translation from wide characters, but that is
// less efficient than doing UTF-8
//
enum CharColumnEncoding char_column_encoding = CHAR_COL_UTF16;

//
//  export odbc-set-char-encoding: native [
//
//  {Set the encoding for CHAR, CHAR(n), VARCHAR(n), LONGVARCHAR fields}
//
//      return: <none>
//      encoding "Either UTF-8, Latin-1, or UCS-2"
//          [word!]
//  ]
//
REBNATIVE(odbc_set_char_encoding)
//
// UTF-8 is preferred to UTF8: https://stackoverflow.com/q/809620/
{
    ODBC_INCLUDE_PARAMS_OF_ODBC_SET_CHAR_ENCODING;

    char_column_encoding = cast(enum CharColumnEncoding, rebUnboxInteger(
        "switch @", ARG(encoding), "[",
            "'utf-8 [", rebI(CHAR_COL_UTF8), "]",
            "'ucs-2 [", rebI(CHAR_COL_UTF16), "]",  // TBD: limited codepoints
            "'utf-16 [", rebI(CHAR_COL_UTF16), "]",
            "'latin-1 [", rebI(CHAR_COL_LATIN1), "]",
        "] else [",
            "fail {ENCODING must be UTF-8, UCS-2, UTF-16, or LATIN-1}"
        "]"
    ));

    return rebNone();
}


//
//  export open-connection: native [
//
//      return: "Always true if success"
//          [logic!]
//      connection "Template object for HENV and HDBC handle fields to set"
//          [object!]
//      spec {ODBC connection string, e.g. commonly "Dsn=DatabaseName"}
//          [text!]
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

    // We treat ODBC's SQLWCHAR type (wide SQL char) as 2 bytes per wchar, even
    //on  platforms where wchar_t is larger.  This gives unixODBC compatibility:
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

    // Allocate the environment handle, and set its version to ODBC3
    //
    SQLHENV henv;
    rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
    if (not SQL_SUCCEEDED(rc))
        rebJumps ("fail", Error_ODBC_Env(SQL_NULL_HENV));

    rc = SQLSetEnvAttr(
        henv,
        SQL_ATTR_ODBC_VERSION,
        cast(SQLPOINTER, cast(uintptr_t, SQL_OV_ODBC3)),
        0  // StringLength (ignored for this attribute)
    );
    if (not SQL_SUCCEEDED(rc)) {
        REBVAL *error = Error_ODBC_Env(henv);
        SQLFreeHandle(SQL_HANDLE_ENV, henv);
        rebJumps ("fail", error);
    }

    // Allocate the connection handle, with login timeout of 5 seconds (why?)
    //
    SQLHDBC hdbc;
    rc = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
    if (not SQL_SUCCEEDED(rc)) {
        REBVAL *error = Error_ODBC_Env(henv);
        SQLFreeHandle(SQL_HANDLE_ENV, henv);
        rebJumps ("fail", error);
    }

    rc = SQLSetConnectAttr(
        hdbc,
        SQL_LOGIN_TIMEOUT,
        cast(SQLPOINTER, cast(uintptr_t, 5)),
        0
    );
    if (not SQL_SUCCEEDED(rc)) {
        REBVAL *error = Error_ODBC_Dbc(hdbc);
        SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
        SQLFreeHandle(SQL_HANDLE_ENV, henv);
        rebJumps ("fail", error);
    }

    // Connect to the Driver

    SQLWCHAR *connect = rebSpellWide(ARG(spec));

    SQLSMALLINT out_connect_len;
    rc = SQLDriverConnectW(
        hdbc,  // ConnectionHandle
        nullptr,  // WindowHandle
        connect,  // InConnectionString
        SQL_NTS,  // StringLength1 (null terminated string)
        nullptr,  // OutConnectionString (not interested in this)
        0,  // BufferLength (again, not interested)
        &out_connect_len,  // StringLength2Ptr (gets returned anyway)
        SQL_DRIVER_NOPROMPT  // DriverCompletion
    );
    rebFree(connect);

    if (not SQL_SUCCEEDED(rc)) {
        REBVAL *error = Error_ODBC_Dbc(hdbc);
        SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
        SQLFreeHandle(SQL_HANDLE_ENV, henv);
        rebJumps ("fail", error);
    }

    REBVAL *henv_value = rebHandle(henv, sizeof(henv), &cleanup_henv);
    REBVAL *hdbc_value = rebHandle(hdbc, sizeof(hdbc), &cleanup_hdbc);

    rebElide("poke", ARG(connection), "'henv", rebR(henv_value));
    rebElide("poke", ARG(connection), "'hdbc", rebR(hdbc_value));

    return rebLogic(true);
}


//
//  export open-statement: native [
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
    REBVAL *hdbc_value = rebValue(
        "ensure handle! pick @", connection, "'hdbc"
    );
    SQLHDBC hdbc = VAL_HANDLE_POINTER(SQLHDBC, hdbc_value);
    rebRelease(hdbc_value);

    SQLRETURN rc;

    SQLHSTMT hstmt;
    rc = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
    if (not SQL_SUCCEEDED(rc))
        rebJumps ("fail", Error_ODBC_Dbc(hdbc));

    REBVAL *hstmt_value = rebHandle(hstmt, sizeof(hstmt), nullptr);

    rebElide("poke", ARG(statement), "'hstmt", rebR(hstmt_value));

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
    PARAMETER *p,
    SQLUSMALLINT number,  // parameter number
    const REBVAL *v
){
    assert(number != 0);

    p->length = 0;  // ignored for most types
    p->column_size = 0;  // also ignored for most types
    TRASH_POINTER_IF_DEBUG(p->buffer);  // required to be set by switch()

    // We don't expose integer mappings for Rebol data types in libRebol to
    // use in a switch() statement, so no:
    //
    //    switch (VAL_TYPE(v)) { case REB_INTEGER: {...} ...}
    //
    // But since the goal is to translate into ODBC types anyway, we can go
    // ahead and do that with Rebol code that embeds those types.  See
    // the `rebPrepare()` proposal for how this pattern could be sped up:
    //
    // https://forum.rebol.info/t/689/2
    //
    SQLSMALLINT c_type = rebUnboxInteger("switch type of @", v, "[",
        "blank! [", rebI(SQL_C_DEFAULT), "]",
        "logic! [", rebI(SQL_C_BIT), "]",

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
        "binary! [", rebI(SQL_C_BINARY), "]",

        "fail {Non-SQL-mappable type used in parameter binding}",
    "]");

    SQLSMALLINT sql_type;

    switch (c_type) {
      case SQL_C_DEFAULT: {  // BLANK!
        sql_type = SQL_NULL_DATA;
        p->buffer_size = 0;
        p->buffer = nullptr;
        break; }

      case SQL_C_BIT: {  // LOGIC!
        sql_type = SQL_BIT;
        p->buffer_size = sizeof(unsigned char);
        p->buffer = rebAllocN(char, p->buffer_size);
        *cast(unsigned char*, p->buffer) = rebDid(v);
        break; }

      case SQL_C_ULONG: {  // unsigned INTEGER! in 32-bit positive range
        sql_type = SQL_INTEGER;
        p->buffer_size = sizeof(SQLUINTEGER);
        p->buffer = rebAllocN(char, p->buffer_size);
        *cast(SQLUINTEGER*, p->buffer) = rebUnboxInteger(v);
        break; }

      case SQL_C_LONG: {  // signed INTEGER! in 32-bit negative range
        sql_type = SQL_INTEGER;
        p->buffer_size = sizeof(SQLINTEGER);  // use signed insertion
        p->buffer = rebAllocN(char, p->buffer_size);
        *cast(SQLINTEGER*, p->buffer) = rebUnboxInteger(v);
        break; }

      case SQL_C_UBIGINT: {  // unsigned INTEGER! above 32-bit positive range
        sql_type = SQL_INTEGER;
        p->buffer_size = sizeof(SQLUBIGINT);  // !!! See notes RE: ODBC BIGINT
        p->buffer = rebAllocN(char, p->buffer_size);
        *cast(SQLUBIGINT*, p->buffer) = rebUnboxInteger(v);
        break; }

      case SQL_C_SBIGINT: {  // signed INTEGER! below 32-bit negative range
        sql_type = SQL_INTEGER;
        p->buffer_size = sizeof(SQLBIGINT);  // !!! See notes RE: ODBC BIGINT
        p->buffer = rebAllocN(char, p->buffer_size);
        *cast(SQLBIGINT*, p->buffer) = rebUnboxInteger(v);
        break; }

      case SQL_C_DOUBLE: {  // DECIMAL!
        sql_type = SQL_DOUBLE;
        p->buffer_size = sizeof(SQLDOUBLE);
        p->buffer = rebAllocN(char, p->buffer_size);
        *cast(SQLDOUBLE*, p->buffer) = rebUnboxDecimal(v);
        break; }

      case SQL_C_TYPE_TIME: {  // // TIME! (fractions not preserved)
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

        REBVAL *time = rebValue("pick", v, "'time");
        REBVAL *second_and_fraction = rebValue("pick", time, "'second");

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
            "to integer! round/down", second_and_fraction
        );
        stamp->fraction = rebUnboxInteger(  // see note above
            "to integer! round/down (",
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
        REBSIZ encoded_size_no_term;
        switch (char_column_encoding) {
          case CHAR_COL_UTF8: {
            unsigned char *utf8 = rebBytes(&encoded_size_no_term, v);
            p->buffer = utf8;
            break; }

          case CHAR_COL_UTF16:
            goto encode_as_utf16;  // if driver can't handle UTF-8

          case CHAR_COL_LATIN1: {
            REBVAL *temp = rebValue(
                "append make binary! length of", v,
                    "map-each ch", v, "["
                        "if 255 < to integer! ch ["
                            "fail {Codepoint too high for Latin1}"
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
            fail ("Invalid CHAR_COL_XXX enumeration");
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

      case SQL_C_BINARY: {  // BINARY!
        size_t size;
        unsigned char *bytes = rebBytes(&size, v);

        sql_type = SQL_VARBINARY;
        p->buffer = bytes;
        p->buffer_size = size;  // sizeof(char) guaranteed to be 1
        p->length = p->column_size = p->buffer_size;
        break; }

      default:
        rebJumps ("panic {Unhandled SQL type in switch() statement}");
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


SQLRETURN ODBC_GetCatalog(
    SQLHSTMT hstmt,
    REBVAL *block
){
    int which = rebUnbox(
        "switch first ensure block! @", block, "[",
            "'tables [1]",
            "'columns [2]",
            "'types [3]",
        "] else [",
            "fail {Catalog must be TABLES, COLUMNS, or TYPES}",
        "]"
    );

    rebElide(
        "if 5 < length of", block, "[",
            "fail {Catalog block should not have more than 4 patterns}",
        "]"
    );

    SQLWCHAR *pattern[4];

  blockscope {
    int index;
    for (index = 2; index != 6; ++index) {
        pattern[index - 2] = rebSpellWide(  // gives nullptr if BLANK!
            "try ensure [<opt> text!]",
                "pick ensure block!", block, rebI(index)
        );
    }
  }

    SQLRETURN rc;

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
        panic ("Invalid GET_CATALOG_XXX value");
    }

  blockscope {
    int n;
    for (n = 0; n != 4; ++n)
        rebFree(pattern[n]);  // no-op if nullptr
  }

    if (not SQL_SUCCEEDED(rc))
        rebJumps("fail", Error_ODBC_Stmt(hstmt));

    return rc;
}


#define COLUMN_TITLE_SIZE 255

static void cleanup_columns(const REBVAL *v) {
    COLUMN *columns = cast(COLUMN*, VAL_HANDLE_VOID_POINTER(v));
    if (columns == nullptr)
        return;  // cleanup_columns() may be called explicitly

    SQLSMALLINT num_columns = VAL_HANDLE_LEN(v);
    SQLSMALLINT col_num;
    for (col_num = 0; col_num < num_columns; ++col_num) {
        COLUMN *col = &columns[col_num];
        FREE_N(char, col->buffer_size, cast(char*, col->buffer));
        rebRelease(col->title);
    }
    free(columns);
}


//
// Sets up the COLUMNS description, retrieves column titles and descriptions
//
void ODBC_DescribeResults(
    SQLHSTMT hstmt,
    int num_columns,
    COLUMN *columns
){
    SQLSMALLINT column_index;
    for (column_index = 1; column_index <= num_columns; ++column_index) {
        COLUMN *col = &columns[column_index - 1];

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
            rebJumps ("fail", Error_ODBC_Stmt(hstmt));

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
            rebJumps ("fail", Error_ODBC_Stmt(hstmt));

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
            REBVAL *type_name_rebval = rebTextWide(type_name);
            col->sql_type = rebUnboxInteger(
                "switch", type_name_rebval, "[",
                    "{VARCHAR} [", rebI(SQL_VARCHAR), "]",  // make fastest

                    "{BINARY} [", rebI(SQL_BINARY), "]",
                    "{VARBINARY} [", rebI(SQL_VARBINARY), "]",
                    "{CHAR} [", rebI(SQL_CHAR), "]",
                    "{NCHAR} [", rebI(SQL_WCHAR), "]",
                    "{NVARCHAR} [", rebI(SQL_WVARCHAR), "]",
                    "{DECIMAL} [", rebI(SQL_DECIMAL), "]",
                "] else [",
                    "fail [",
                        "{SQL_VARCHAR reported by ODBC for unknown type:}",
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

          case SQL_SMALLINT:
          case SQL_TINYINT:
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
            col->buffer_size = sizeof(char) * col->column_size;
            break;

          case SQL_CHAR:
          case SQL_VARCHAR:
            if (char_column_encoding == CHAR_COL_UTF16)
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
            if (char_column_encoding == CHAR_COL_UTF16)
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
            fail ("Unknown column SQL_XXX type");
        }

        col->buffer = TRY_ALLOC_N(char, col->buffer_size);
        if (col->buffer == nullptr)
            fail ("Couldn't allocate column buffer!");
    }
}


//
//  export insert-odbc: native [
//
//  {Executes SQL statements (prepare on first pass, executes conservatively)}
//
//      return: "Row count for row change, column title BLOCK! for selects"
//          [integer! block!]
//      statement [object!]
//      sql "Dialect beginning with TABLES, COLUMNS, TYPES, or SQL STRING!"
//          [block!]
//  ]
//
REBNATIVE(insert_odbc)
{
    ODBC_INCLUDE_PARAMS_OF_INSERT_ODBC;

    REBVAL *statement = ARG(statement);
    REBVAL *hstmt_value = rebValue(
        "ensure handle! pick", statement, "'hstmt"
    );
    SQLHSTMT hstmt = VAL_HANDLE_POINTER(SQLHSTMT, hstmt_value);
    rebRelease(hstmt_value);

    SQLRETURN rc;

    rc = SQLFreeStmt(hstmt, SQL_RESET_PARAMS);  // !!! check rc?
    rc = SQLCloseCursor(hstmt);  // !!! check rc?

    //=//// MAKE SQL REQUEST FROM DIALECTED SQL BLOCK /////////////////////=//
    //
    // The block passed in is used to form a query.

    bool use_cache = false;

    bool get_catalog = rebDid(
        "switch type of first", rebQ(ARG(sql)), "[",
            "lit-word! [true]",  // like Rebol2: 'tables, 'columns, 'types
            "text! [false]",
        "] else [fail {SQL dialect must start with WORD! or TEXT! value}]"
    );

    if (get_catalog) {
        rc = ODBC_GetCatalog(hstmt, ARG(sql));
    }
    else {
        // Prepare/Execute statement, when first element in the block is a
        // (statement) string

        // Compare with previously prepared statement, and if not the same,
        // then prepare a new statement.
        //
        use_cache = rebDid(
            "strict-equal? first", ARG(sql),
                "ensure [text! blank!] pick", statement, "'string"
        );

        REBLEN sql_index = 1;

        if (not use_cache) {
            SQLWCHAR *sql_string = rebSpellWide("first", ARG(sql));

            rc = SQLPrepareW(
                hstmt,
                sql_string,
                SQL_NTS  // Null-Terminated String
            );
            if (not SQL_SUCCEEDED(rc))
                fail (Error_ODBC_Stmt(hstmt));

            rebFree(sql_string);

            // Remember statement string handle, but keep a copy since it
            // may be mutated by the user.
            //
            // !!! Could re-use value with existing series if read only
            //
            rebElide(
                "poke", statement, "'string", "(copy first", ARG(sql), ")"
            );
        }

        // The SQL string may contain ? characters, which indicates that it is
        // a parameterized query.  The separation of the parameters into a
        // different quarantined part of the query is to protect against SQL
        // injection.

        REBLEN num_params
            = rebUnbox("length of", ARG(sql)) - sql_index;  // after SQL

        ++sql_index;

        PARAMETER *params = nullptr;
        if (num_params != 0) {
            params = rebAllocN(PARAMETER, num_params);

            REBLEN n;
            for (n = 0; n < num_params; ++n, ++sql_index) {
                REBVAL *value = rebValue("pick", ARG(sql), rebI(sql_index));
                rc = ODBC_BindParameter(
                    hstmt,
                    &params[n],
                    n + 1,
                    value
                );
                rebRelease(value);
                if (not SQL_SUCCEEDED(rc))
                    fail (Error_ODBC_Stmt(hstmt));
            }
        }

        // Execute statement, but don't check result code until after the
        // parameters and their data buffers have been freed.
        //
        rc = SQLExecute(hstmt);

        if (num_params != 0) {
            REBLEN n;
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
            fail (Error_ODBC_Stmt(hstmt));

          case SQL_STILL_EXECUTING:
            assert(!"SQL_STILL_EXECUTING seen...only w/async calls");
            fail (Error_ODBC_Stmt(hstmt));

          case SQL_ERROR:
            fail (Error_ODBC_Stmt(hstmt));

          case SQL_INVALID_HANDLE:
            assert(!"SQL_INVALID_HANDLE seen...should never happen");
            fail (Error_ODBC_Stmt(hstmt));

        #if ODBCVER >= 0x0380
          case SQL_PARAM_DATA_AVAILABLE:
            assert(!"SQL_PARAM_DATA_AVAILABLE seen...only in ODBC 3.8");
            fail (Error_ODBC_Stmt(hstmt));
        #endif
        }
    }

    //=//// RETURN RECORD COUNT IF NO RESULT ROWS /////////////////////////=//
    //
    // Insert/Update/Delete statements do not return records, and this is
    // indicated by a 0 count for columns in the return result.

    SQLSMALLINT num_columns;
    rc = SQLNumResultCols(hstmt, &num_columns);
    if (not SQL_SUCCEEDED(rc))
        fail (Error_ODBC_Stmt(hstmt));

    if (num_columns == 0) {
        SQLLEN num_rows;
        rc = SQLRowCount(hstmt, &num_rows);
        if (not SQL_SUCCEEDED(rc))
            fail (Error_ODBC_Stmt(hstmt));

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

    if (use_cache) {
        REBVAL *cache = rebValue(
            "ensure block! pick", statement, "'titles"
        );
        return cache;
    }

    REBVAL *old_columns_value = rebValue(
        "ensure [<opt> handle!] pick", statement, "'columns"
    );
    if (old_columns_value) {
        cleanup_columns(old_columns_value);
        SET_HANDLE_CDATA(old_columns_value, nullptr);
        rebRelease(old_columns_value);
    }

    COLUMN *columns = cast(COLUMN*, malloc(sizeof(COLUMN) * num_columns));
    if (not columns)
        fail ("Couldn't allocate column buffers!");

    REBVAL *columns_value = rebHandle(columns, num_columns, &cleanup_columns);

    rebElide("poke", statement, "'columns", rebR(columns_value));

    ODBC_DescribeResults(hstmt, num_columns, columns);

    REBVAL *titles = rebValue("make block!", rebI(num_columns));
    SQLSMALLINT column_index;
    for (column_index = 1; column_index <= num_columns; ++column_index)
        rebElide("append", titles, columns[column_index - 1].title);

    // remember column titles if next call matches, return them as the result
    //
    rebElide("poke", statement, "'titles", titles);

    return titles;
}


//
// A query will fill a column's buffer with data.  This data can be
// reinterpreted as a Rebol value.  Successive queries for records reuse the
// buffer for a column.
//
REBVAL *ODBC_Column_To_Rebol_Value(COLUMN *col)
{
    if (col->length == SQL_NULL_DATA)
        return rebBlank();

    switch (col->c_type) {
      case SQL_C_BIT:
        //
        // Note: MySQL ODBC returns -2 for sql_type when a field is BIT(n)
        // where n != 1, as opposed to SQL_BIT and column_size of n.  See
        // remarks on the fail() below.
        //
        if (col->column_size != 1)
            fail ("BIT(n) fields are only supported for n = 1");

        return rebLogic(*cast(unsigned char*, col->buffer) != 0);

    // ODBC was asked at SQLGetData time to give back *most* integer
    // types as SQL_C_SLONG or SQL_C_ULONG, regardless of actual size
    // in the sql_type (not the c_type)

      case SQL_C_SLONG:  // signed: -32,768..32,767
        return rebInteger(*cast(SQLINTEGER*, col->buffer));

      case SQL_C_ULONG:  // signed: -2[31]..2[31] - 1
        return rebInteger(*cast(SQLUINTEGER*, col->buffer));

    // Special exception made for big integers, where seemingly MySQL
    // would not properly map smaller types into big integers if all
    // you ask for are big ones.
    //
    // !!! Review: bug may not exist if SQLGetData() is used.

      case SQL_C_SBIGINT:  // signed: -2[63]..2[63]-1
        return rebInteger(*cast(SQLBIGINT*, col->buffer));

      case SQL_C_UBIGINT:  // unsigned: 0..2[64] - 1
        if (*cast(REBU64*, col->buffer) > INT64_MAX)
            fail ("INTEGER! can't hold some unsigned 64-bit values");

        return rebInteger(*cast(SQLUBIGINT*, col->buffer));

    // ODBC was asked at column binding time to give back all floating
    // point types as SQL_C_DOUBLE, regardless of actual size.

      case SQL_C_DOUBLE:
        return rebDecimal(*cast(SQLDOUBLE*, col->buffer));

      case SQL_C_TYPE_DATE: {
        DATE_STRUCT *date = cast(DATE_STRUCT*, col->buffer);
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
        TIME_STRUCT *time = cast(TIME_STRUCT*, col->buffer);
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
        TIMESTAMP_STRUCT *stamp = cast(TIMESTAMP_STRUCT*, col->buffer);

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
            "_"  // timezone (leave blank)
        ")"); }

    // SQL_BINARY, SQL_VARBINARY, and SQL_LONGVARBINARY were all requested
    // as SQL_C_BINARY.

      case SQL_C_BINARY:
        return rebSizedBinary(col->buffer, col->length);

    // There's no guarantee that CHAR fields contain valid UTF-8, but we
    // currently only support that.
    //
    // !!! Should there be a Latin1 fallback if the UTF-8 interpretation
    // fails?

      case SQL_C_CHAR: {
        switch (char_column_encoding) {
          case CHAR_COL_UTF8:
            return rebSizedText(
                cast(char*, col->buffer),  // unixodbc SQLCHAR is unsigned
                col->length
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
            REBVAL *binary = rebSizedBinary(
                cast(unsigned char*, col->buffer),
                col->length
            );
            return rebValue(
                "append make text!", rebI(col->length),
                    "map-each byte", rebR(binary), "[to char! byte]"
            ); }
        }
        break; }

      case SQL_C_WCHAR:
        assert(col->length % 2 == 0);
        return rebLengthedTextWide(
            cast(SQLWCHAR*, col->buffer),
            col->length / 2
        );

      default:
        break;
    }

    // Note: This happens with BIT(2) and the MySQL ODBC driver, which
    // reports a sql_type of -2 for some reason.
    //
    fail ("Unsupported SQL_XXX type returned from query");
}


//
//  export copy-odbc: native [
//
//      return: "Block of row blocks for selects and catalog functions"
//          [block!]
//      statement [object!]
//      /part [integer!]
//  ]
//
REBNATIVE(copy_odbc)
{
    ODBC_INCLUDE_PARAMS_OF_COPY_ODBC;

    REBVAL *hstmt_value = rebValue(
        "ensure handle! pick", ARG(statement), "'hstmt"
    );
    SQLHSTMT hstmt = cast(SQLHSTMT, VAL_HANDLE_VOID_POINTER(hstmt_value));
    rebRelease(hstmt_value);

    REBVAL *columns_value = rebValue(
        "ensure handle! pick", ARG(statement), "'columns"
    );
    COLUMN *columns = VAL_HANDLE_POINTER(COLUMN, columns_value);
    rebRelease(columns_value);

    if (hstmt == SQL_NULL_HANDLE or not columns)
        fail ("Invalid statement object!");

    SQLRETURN rc;

    SQLSMALLINT num_columns;
    rc = SQLNumResultCols(hstmt, &num_columns);
    if (not SQL_SUCCEEDED(rc))
        fail (Error_ODBC_Stmt(hstmt));

    // compares-0 based row against num_rows, so -1 is chosen to never match
    // and hence mean "as many rows as available"
    //
    SQLLEN num_rows = rebUnbox("any [", REF(part), "-1]");

    REBVAL *results = rebValue(
        "make block!", rebI(num_rows == -1 ? 10 : num_rows)
    );

    SQLLEN row = 0;
    while (row != num_rows) {

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
            rebJumps ("fail", Error_ODBC_Stmt(hstmt));
        }

        REBVAL *record = rebValue("make block!", rebI(num_columns));

        SQLSMALLINT column_index;
        for (column_index = 1; column_index <= num_columns; ++column_index) {
            COLUMN *col = &columns[column_index - 1];

            rc = SQLGetData(
                hstmt,
                column_index,
                col->c_type,
                col->buffer,
                col->buffer_size,
                &col->length
            );

            switch (rc) {
              case SQL_SUCCESS:
                break;

              case SQL_SUCCESS_WITH_INFO:  // potential truncation
                //
                // !!! This code is untested, but something like this would
                // be needed here.  Review.
                //
                if (
                    col->c_type == SQL_C_CHAR
                    and col->length > cast(SQLLEN, col->buffer_size)
                ){
                    col->buffer = rebRealloc(col->buffer, col->length + 1);

                    SQLLEN len_partial = col->buffer_size - 1;
                    SQLLEN len_remaining = col->length - len_partial;
                    SQLLEN len_check;
                    rc = SQLGetData(
                        hstmt,
                        column_index,
                        col->c_type,
                        cast(char*, col->buffer) + len_partial,
                        len_remaining,  // amount of space in buffer
                        &len_check
                    );
                    if (rc != SQL_SUCCESS)
                        rebJumps ("fail", Error_ODBC_Stmt(hstmt));

                    assert(len_check == len_remaining);
                }
                break;

              case SQL_NO_DATA:
                assert("!Got back SQL_NO_DATA from SQLGetData()");
                goto no_more_data;

              case SQL_ERROR:
              case SQL_STILL_EXECUTING:
              case SQL_INVALID_HANDLE:
              default:  // No other return codes were listed
                rebJumps ("fail", Error_ODBC_Stmt(hstmt));
            }

            REBVAL *temp = ODBC_Column_To_Rebol_Value(col);
            rebElide("append", record, "quote", rebR(temp));
        }

        rebElide("append", results, "quote", rebR(record));
        ++row;
    }

  no_more_data:

    return results;
}


//
//  export update-odbc: native [
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
    REBVAL *hdbc_value = rebValue(
        "ensure handle! pick", connection, "'hdbc"
    );
    SQLHDBC hdbc = cast(SQLHDBC, VAL_HANDLE_VOID_POINTER(hdbc_value));
    rebRelease(hdbc_value);

    SQLRETURN rc;

    bool access = rebDid(ARG(access));
    rc = SQLSetConnectAttr(
        hdbc,
        SQL_ATTR_ACCESS_MODE,
        cast(SQLPOINTER*,
            cast(uintptr_t, access ? SQL_MODE_READ_WRITE : SQL_MODE_READ_ONLY)
        ),
        SQL_IS_UINTEGER
    );

    if (not SQL_SUCCEEDED(rc))
        fail (Error_ODBC_Dbc(hdbc));

    bool commit = rebDid(ARG(commit));
    rc = SQLSetConnectAttr(
        hdbc,
        SQL_ATTR_AUTOCOMMIT,
        cast(SQLPOINTER*,
            cast(uintptr_t, commit ? SQL_AUTOCOMMIT_ON : SQL_AUTOCOMMIT_OFF)
        ),
        SQL_IS_UINTEGER
    );

    if (not SQL_SUCCEEDED(rc))
        fail (Error_ODBC_Dbc(hdbc));

    return rebLogic(true);
}


//
//  export close-statement: native [
//
//      return: [logic!]
//      statement [object!]
//  ]
//
REBNATIVE(close_statement)
{
    ODBC_INCLUDE_PARAMS_OF_CLOSE_STATEMENT;

    REBVAL *statement = ARG(statement);

    REBVAL *columns_value = rebValue(
        "ensure [<opt> handle!] pick", statement, "'columns"
    );
    if (columns_value) {
        cleanup_columns(columns_value);
        SET_HANDLE_CDATA(columns_value, nullptr);  // avoid GC cleanup
        rebElide("poke", statement, "'columns", "null");

        rebRelease(columns_value);
    }

    REBVAL *hstmt_value = rebValue(
        "ensure [<opt> handle!] pick", statement, "'hstmt"
    );
    if (hstmt_value) {
        SQLHSTMT hstmt = cast(SQLHSTMT, VAL_HANDLE_VOID_POINTER(hstmt_value));
        assert(hstmt);

        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        SET_HANDLE_CDATA(hstmt_value, SQL_NULL_HANDLE);  // avoid GC cleanup
        rebElide("poke", statement, "'hstmt", "null");

        rebRelease(hstmt_value);
    }

    return rebLogic(true);
}


//
//  export close-connection: native [
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

    REBVAL *hdbc_value = rebValue(
        "ensure [<opt> handle!] pick", connection, "'hdbc"
    );
    if (hdbc_value) {
        SQLHDBC hdbc = cast(SQLHDBC, VAL_HANDLE_VOID_POINTER(hdbc_value));
        assert(hdbc);

        SQLDisconnect(hdbc);
        SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
        SET_HANDLE_CDATA(hdbc_value, SQL_NULL_HANDLE);  // avoid GC cleanup

        rebElide("poke", connection, "'hdbc", "blank");
        rebRelease(hdbc_value);
    }

    // Close the environment
    //
    REBVAL *henv_value = rebValue(
        "ensure [<opt> handle!] pick", connection, "'henv"
    );
    if (henv_value) {
        SQLHENV henv = cast(SQLHENV, VAL_HANDLE_VOID_POINTER(henv_value));
        assert(henv);

        SQLFreeHandle(SQL_HANDLE_ENV, henv);
        SET_HANDLE_CDATA(henv_value, SQL_NULL_HANDLE);  // avoid GC cleanup

        rebElide("poke", connection, "'henv", "null");
        rebRelease(henv_value);
    }

    return rebLogic(true);
}
