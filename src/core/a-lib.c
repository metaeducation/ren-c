//
//  File: %a-lib.c
//  Summary: "Lightweight Export API (REBVAL as opaque type)"
//  Section: environment
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
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
// This is the "external" API, and %reb-lib.h contains its exported
// definitions.  That file (and %make-reb-lib.r which generates it) contains
// comments and notes which will help understand it.
//
// What characterizes the external API is that it is not necessary to #include
// the extensive definitions of `struct REBSER` or the APIs for dealing with
// all the internal details (e.g. PUSH_GUARD_SERIES(), which are easy to get
// wrong).  Not only does this simplify the interface, but it also means that
// the C code using the library isn't competing as much for definitions in
// the global namespace.
//
// (That was true of the original RL_API in R3-Alpha, but this later iteration
// speaks in terms of actual REBVAL* cells--vs. creating a new type.  They are
// just opaque pointers to cells whose lifetime is governed by the core.)
//
// Each exported routine here has a name RL_rebXxxYyy.  This is a name by
// which it can be called internally from the codebase like any other function
// that is part of the core.  However, macros for calling it from the core
// are given as `#define rebXxxYyy RL_rebXxxYyy`.  This is a little bit nicer
// and consistent with the way it looks when an external client calls the
// functions.
//
// Then extension clients use macros which have you call the functions through
// a struct-based "interface" (similar to the way that interfaces work in
// something like COM).  Here the macros merely pick the API functions through
// a table, e.g. `#define rebXxxYyy interface_struct->rebXxxYyy`.  This means
// paying a slight performance penalty to dereference that API per call, but
// it keeps API clients from depending on the conventional C linker...so that
// DLLs can be "linked" against a Rebol EXE.
//
// (It is not generically possible to export symbols from an executable, and
// just in general there's no cross-platform assurances about how linking
// works, so this provides the most flexibility.)
//

#include "sys-core.h"


// "Linkage back to HOST functions. Needed when we compile as a DLL
// in order to use the OS_* macro functions."
//
#ifdef REB_API  // Included by C command line
    REBOL_HOST_LIB *Host_Lib;
#endif


static REBRXT Reb_To_RXT[REB_MAX];
static enum Reb_Kind RXT_To_Reb[RXT_MAX];


//
//  rebVersion: RL_API
//
// Obtain current REBOL interpreter version information.
//
// Returns:
//     A byte array containing version, revision, update, and more.
// Arguments:
//     vers - a byte array to hold the version info. First byte is length,
//         followed by version, revision, update, system, variation.
// Notes:
//     In the original RL_API, this function was to be called before any other
//     initialization to determine version compatiblity with the caller.
//     With the massive changes in Ren-C and the lack of RL_API clients, this
//     check is low priority.  This is how it was originally done:
//
//          REBYTE vers[8];
//          vers[0] = 5; // len
//          RL_Version(&vers[0]);
//
//          if (vers[1] != RL_VER || vers[2] != RL_REV)
//              OS_CRASH(cb_cast("Incompatible reb-lib DLL"));
//
void RL_rebVersion(REBYTE vers[])
{
    // [0] is length
    vers[1] = REBOL_VER;
    vers[2] = REBOL_REV;
    vers[3] = REBOL_UPD;
    vers[4] = REBOL_SYS;
    vers[5] = REBOL_VAR;
}


//
//  rebInit: RL_API
//
// Initialize the REBOL interpreter.
//
// Returns:
//     Zero on success, otherwise an error indicating that the
//     host library is not compatible with this release.
// Arguments:
//     lib - the host lib (OS_ functions) to be used by REBOL.
//         See host-lib.c for details.
// Notes:
//     This function will allocate and initialize all memory
//     structures used by the REBOL interpreter. This is an
//     extensive process that takes time.
//
void RL_rebInit(void *lib)
{
    // These tables used to be built by overcomplicated Rebol scripts.  It's
    // less hassle to have them built on initialization.

    REBCNT n;
    for (n = 0; n < REB_MAX; ++n) {
        //
        // Though statics are initialized to 0, this makes it more explicit,
        // as well as deterministic if there's an Init/Shutdown/Init...
        //
        Reb_To_RXT[n] = 0; // default that some types have no exported RXT_
    }

    // REB_BAR unsupported?
    // REB_LIT_BAR unsupported?
    Reb_To_RXT[REB_WORD] = RXT_WORD;
    Reb_To_RXT[REB_SET_WORD] = RXT_SET_WORD;
    Reb_To_RXT[REB_GET_WORD] = RXT_GET_WORD;
    Reb_To_RXT[REB_LIT_WORD] = RXT_GET_WORD;
    Reb_To_RXT[REB_REFINEMENT] = RXT_REFINEMENT;
    Reb_To_RXT[REB_ISSUE] = RXT_ISSUE;
    Reb_To_RXT[REB_PATH] = RXT_PATH;
    Reb_To_RXT[REB_SET_PATH] = RXT_SET_PATH;
    Reb_To_RXT[REB_GET_PATH] = RXT_GET_PATH;
    Reb_To_RXT[REB_LIT_PATH] = RXT_LIT_PATH;
    Reb_To_RXT[REB_GROUP] = RXT_GROUP;
    Reb_To_RXT[REB_BLOCK] = RXT_BLOCK;
    Reb_To_RXT[REB_BINARY] = RXT_BINARY;
    Reb_To_RXT[REB_STRING] = RXT_STRING;
    Reb_To_RXT[REB_FILE] = RXT_FILE;
    Reb_To_RXT[REB_EMAIL] = RXT_EMAIL;
    Reb_To_RXT[REB_URL] = RXT_URL;
    Reb_To_RXT[REB_BITSET] = RXT_BITSET;
    Reb_To_RXT[REB_IMAGE] = RXT_IMAGE;
    Reb_To_RXT[REB_VECTOR] = RXT_VECTOR;
    Reb_To_RXT[REB_BLANK] = RXT_BLANK;
    Reb_To_RXT[REB_LOGIC] = RXT_LOGIC;
    Reb_To_RXT[REB_INTEGER] = RXT_INTEGER;
    Reb_To_RXT[REB_DECIMAL] = RXT_DECIMAL;
    Reb_To_RXT[REB_PERCENT] = RXT_PERCENT;
    // REB_MONEY unsupported?
    Reb_To_RXT[REB_CHAR] = RXT_CHAR;
    Reb_To_RXT[REB_PAIR] = RXT_PAIR;
    Reb_To_RXT[REB_TUPLE] = RXT_TUPLE;
    Reb_To_RXT[REB_TIME] = RXT_TIME;
    Reb_To_RXT[REB_DATE] = RXT_DATE;
    // REB_MAP unsupported?
    // REB_DATATYPE unsupported?
    // REB_TYPESET unsupported?
    // REB_VARARGS unsupported?
    Reb_To_RXT[REB_OBJECT] = RXT_OBJECT;
    // REB_FRAME unsupported?
    Reb_To_RXT[REB_MODULE] = RXT_MODULE;
    // REB_ERROR unsupported?
    // REB_PORT unsupported?
    Reb_To_RXT[REB_GOB] = RXT_GOB;
    // REB_EVENT unsupported?
    Reb_To_RXT[REB_HANDLE] = RXT_HANDLE;
    // REB_STRUCT unsupported?
    // REB_LIBRARY unsupported?

    for (n = 0; n < REB_MAX; ++n)
        RXT_To_Reb[Reb_To_RXT[n]] = cast(enum Reb_Kind, n); // reverse lookup

    // The RL_XXX API functions are stored like a C++ vtable, so they are
    // function pointers inside of a struct.  It's not completely obvious
    // what the applications of this are...theoretically it could be for
    // namespacing, or using multiple different versions of the API in a
    // single codebase, etc.  But all known clients use macros against a
    // global "RL" rebol library, so it's not clear what the advantage is
    // over just exporting C functions.

    Host_Lib = cast(REBOL_HOST_LIB*, lib);

    if (Host_Lib->size < HOST_LIB_SIZE)
        panic ("Host-lib wrong size");

    if (((HOST_LIB_VER << 16) + HOST_LIB_SUM) != Host_Lib->ver_sum)
        panic ("Host-lib wrong version/checksum");

    Startup_Core();
}


//
//  rebShutdown: RL_API
//
// Shut down a Rebol interpreter (that was initialized via RL_Init).
//
// Returns:
//     nothing
// Arguments:
//     clean - whether you want Rebol to release all of its memory
//     accrued since initialization.  If you pass false, then it will
//     only do the minimum needed for data integrity (assuming you
//     are planning to exit the process, and hence the OS will
//     automatically reclaim all memory/handles/etc.)
//
void RL_rebShutdown(REBOOL clean)
{
    // At time of writing, nothing Shutdown_Core() does pertains to
    // committing unfinished data to disk.  So really there is
    // nothing to do in the case of an "unclean" shutdown...yet.

    if (clean) {
    #ifdef NDEBUG
        // Only do the work above this line in an unclean shutdown
        return;
    #else
        // Run a clean shutdown anyway in debug builds--even if the
        // caller didn't need it--to see if it triggers any alerts.
        //
        Shutdown_Core();
    #endif
    }
    else {
        Shutdown_Core();
    }
}


//
//  rebEscape: RL_API
//
// Signal that code evaluation needs to be interrupted.
//
// Returns:
//     nothing
// Notes:
//     This function set's a signal that is checked during evaluation
//     and will cause the interpreter to begin processing an escape
//     trap. Note that control must be passed back to REBOL for the
//     signal to be recognized and handled.
//
void RL_rebEscape(void)
{
    // How should HALT vs. BREAKPOINT be decided?  When does a Ctrl-C want
    // to quit entirely vs. begin an interactive debugging session?
    //
    // !!! For now default to halting, but use SIG_INTERRUPT when a decision
    // is made about how to debug break.
    //
    SET_SIGNAL(SIG_HALT);
}


//
//  rebEvent: RL_API
//
// Appends an application event (e.g. GUI) to the event port.
//
// Returns:
//     Returns TRUE if queued, or FALSE if event queue is full.
// Arguments:
//     evt - A properly initialized event structure. The
//         contents of this structure are copied as part of
//         the function, allowing use of locals.
// Notes:
//     Sets a signal to get REBOL attention for WAIT and awake.
//     To avoid environment problems, this function only appends
//     to the event queue (no auto-expand). So if the queue is full
//
// !!! Note to whom it may concern: REBEVT would now be 100% compatible with
// a REB_EVENT REBVAL if there was a way of setting the header bits in the
// places that generate them.
//
int RL_rebEvent(REBEVT *evt)
{
    REBVAL *event = Append_Event();     // sets signal

    if (event) {                        // null if no room left in series
        VAL_RESET_HEADER(event, REB_EVENT); // has more space, if needed
        event->extra.eventee = evt->eventee;
        event->payload.event.type = evt->type;
        event->payload.event.flags = evt->flags;
        event->payload.event.win = evt->win;
        event->payload.event.model = evt->model;
        event->payload.event.data = evt->data;
        return 1;
    }

    return 0;
}


//
//  rebUpdateEvent: RL_API
//
// Updates an application event (e.g. GUI) to the event port.
//
// Returns:
//     Returns 1 if updated, or 0 if event appended, and -1 if full.
// Arguments:
//     evt - A properly initialized event structure. The
//          model and type of the event are used to address
//          the unhandled event in the queue, when it is found,
//          it will be replaced with this one
//
int RL_rebUpdateEvent(REBEVT *evt)
{
    REBVAL *event = Find_Last_Event(evt->model, evt->type);

    if (event) {
        event->extra.eventee = evt->eventee;
        event->payload.event.type = evt->type;
        event->payload.event.flags = evt->flags;
        event->payload.event.win = evt->win;
        event->payload.event.model = evt->model;
        event->payload.event.data = evt->data;
        return 1;
    }

    return rebEvent(evt) - 1;
}


//
//  rebFindEvent: RL_API
//
// Find an application event (e.g. GUI) to the event port.
//
// Returns:
//     A pointer to the find event
// Arguments:
//     model - event model
//     type - event type
//
REBEVT *RL_rebFindEvent(REBINT model, REBINT type)
{
    REBVAL * val = Find_Last_Event(model, type);
    if (val != NULL) {
        return cast(REBEVT*, val); // should be compatible!
    }
    return NULL;
}


//
//  rebGobHead: RL_API
//
REBGOB** RL_rebGobHead(REBGOB *gob)
{
    return SER_HEAD(REBGOB*, GOB_PANE(gob));
}


//
//  rebGobString: RL_API
//
REBYTE* RL_rebGobString(REBGOB *gob)
{
    return BIN_HEAD(GOB_CONTENT(gob));
}


//
//  rebGobLen: RL_API
//
REBCNT RL_rebGobLen(REBGOB *gob)
{
    return SER_LEN(GOB_PANE(gob));
}


//
// !!! These routines are exports of the macros and inline functions which
// rely upon internal definitions that RL_XXX clients are not expected to have
// available.  While this implementation file can see inside the definitions
// of `struct Reb_Value`, the caller has an opaque definition.
//
// These are transitional as part of trying to get rid of RXIARG, RXIFRM, and
// COMMAND! in general.  Though it is not a good "API design" to just take
// any internal function you find yourself needing in a client and export it
// here with "RL_" in front of the name, it's at least understandable--and
// not really introducing any routines that don't already have to exist and
// be tested.
//

inline static REBFRM *Extract_Live_Rebfrm_May_Fail(const REBVAL *frame) {
    if (!IS_FRAME(frame))
        fail ("Not a FRAME!");

    REBCTX *frame_ctx = VAL_CONTEXT(frame);
    REBFRM *f = CTX_FRAME_IF_ON_STACK(frame_ctx);
    if (f == NULL)
        fail ("FRAME! is no longer on stack.");

    assert(Is_Function_Frame(f));
    assert(NOT(Is_Function_Frame_Fulfilling(f)));
    return f;
}


//
//  rebFrmNumArgs: RL_API
//
REBCNT RL_rebFrmNumArgs(const REBVAL *frame) {
    REBFRM *f = Extract_Live_Rebfrm_May_Fail(frame);
    return FRM_NUM_ARGS(f);
}

//
//  rebFrmArg: RL_API
//
REBVAL *RL_rebFrmArg(const REBVAL *frame, REBCNT n) {
    REBFRM *f = Extract_Live_Rebfrm_May_Fail(frame);
    return FRM_ARG(f, n);
}

//
//  rebValLogic: RL_API
//
REBOOL RL_rebValLogic(const REBVAL *v) {
    return VAL_LOGIC(v);
}

//
//  rebValType: RL_API
//
// !!! Among the few concepts from the original host kit API that may make
// sense, it could be a good idea to abstract numbers for datatypes from the
// REB_XXX numbering scheme.  So for the moment, REBRXT is being kept as is.
//
REBRXT RL_rebValType(const REBVAL *v) {
    return IS_VOID(v)
        ? 0
        : Reb_To_RXT[VAL_TYPE(v)];
}


//
//  rebValInt64: RL_API
//
REBI64 RL_rebValInt64(const REBVAL *v) {
    return VAL_INT64(v);
}

//
//  rebValInt32: RL_API
//
REBINT RL_rebValInt32(const REBVAL *v) {
    return VAL_INT32(v);
}

//
//  rebValDecimal: RL_API
//
REBDEC RL_rebValDecimal(const REBVAL *v) {
    return VAL_DECIMAL(v);
}

//
//  rebValChar: RL_API
//
REBUNI RL_rebValChar(const REBVAL *v) {
    return VAL_CHAR(v);
}

//
//  rebValTime: RL_API
//
REBI64 RL_rebValTime(const REBVAL *v) {
    return VAL_NANO(v);
}


//
//  rebValTupleData: RL_API
//
REBYTE *RL_rebValTupleData(const REBVAL *v) {
    return VAL_TUPLE_DATA(m_cast(REBVAL*, v));
}

//
//  rebValIndex: RL_API
//
REBCNT RL_rebValIndex(const REBVAL *v) {
    return VAL_INDEX(v);
}

//
//  rebInitValIndex: RL_API
//
void RL_rebInitValIndex(REBVAL *v, REBCNT i) {
    VAL_INDEX(v) = i;
}

//
//  rebValHandlePointer: RL_API
//
void *RL_rebValHandlePointer(const REBVAL *v) {
    return VAL_HANDLE_POINTER(void, v);
}

//
//  rebSetHandlePointer: RL_API
//
void RL_rebSetHandlePointer(REBVAL *v, void *p) {
    v->extra.singular = NULL; // !!! only support "dumb" handles for now
    SET_HANDLE_POINTER(v, p);
}

//
//  rebValImageWide: RL_API
//
REBCNT RL_rebValImageWide(const REBVAL *v) {
    return VAL_IMAGE_WIDE(v);
}

//
//  rebValImageHigh: RL_API
//
REBCNT RL_rebValImageHigh(const REBVAL *v) {
    return VAL_IMAGE_HIGH(v);
}

//
//  rebValPairXFloat: RL_API
//
// !!! Pairs in R3-Alpha were not actually pairs of arbitrary values; but
// they were pairs of floats.  This meant their precision did not match either
// 64-bit integers or 64-bit decimals, because you can't fit two of those in
// one REBVAL and still have room for a header.  Ren-C changed the mechanics
// so that two actual values were efficiently stored in a PAIR! via a special
// kind of GC-able series node (with no further allocation).  Hence you can
// tell the difference between 1x2, 1.0x2.0, 1x2.0, 1.0x2, etc.
//
// Yet the R3-Alpha external interface did not make this distinction, so this
// API is for compatibility with those extracting floats.
//
float RL_rebValPairXFloat(const REBVAL *v) {
    return VAL_PAIR_X(v);
}

//
//  rebValPairYFloat: RL_API
//
// !!! See notes on RL_rebValPairXFloat
//
float RL_rebValPairYFloat(const REBVAL *v) {
    return VAL_PAIR_Y(v);
}

//
//  rebInitDate: RL_API
//
// There was a data structure called a REBOL_DAT in R3-Alpha which was defined
// in %reb-defs.h, and it appeared in the host callbacks to be used in
// `os_get_time()` and `os_file_time()`.  This allowed the host to pass back
// date information without actually knowing how to construct a date REBVAL.
//
// Today "host code" (which may all become "port code") is expected to either
// be able to speak in terms of Rebol values through linkage to the internal
// API or the more minimal RL_Api.  Either way, it should be able to make
// REBVALs corresponding to dates...even if that means making a string of
// the date to load and then RL_Do_String() to produce the value.
//
// This routine is a quick replacement for the format of the struct, as a
// temporary measure while it is considered whether things like os_get_time()
// will have access to the full internal API or not.
//
// !!! Note this doesn't allow you to say whether the date has a time
// or zone component at all.  Those could be extra flags, or if Rebol values
// were used they could be blanks vs. integers.  Further still, this kind
// of API is probably best kept as calls into Rebol code, e.g.
// RL_Do("make time!", ...); which might not offer the best performance, but
// the internal API is available for clients who need that performance,
// who can call date initialization themselves.
//
void RL_rebInitDate(
    REBVAL *out,
    int year,
    int month,
    int day,
    int seconds,
    int nano,
    int zone
) {
    VAL_RESET_HEADER(out, REB_DATE);
    VAL_YEAR(out)  = year;
    VAL_MONTH(out) = month;
    VAL_DAY(out) = day;

    SET_VAL_FLAG(out, DATE_FLAG_HAS_ZONE);
    INIT_VAL_ZONE(out, zone / ZONE_MINS);

    SET_VAL_FLAG(out, DATE_FLAG_HAS_TIME);
    VAL_NANO(out) = SECS_TO_NANO(seconds) + nano;
}


//
//  rebValUTF8: RL_API
//
// Extract UTF-8 data from an ANY-STRING! or ANY-WORD!.
//
REBCNT RL_rebValUTF8(
    REBYTE *buf,
    REBCNT buf_chars,
    const REBVAL *v
){
    REBCNT index;
    REBCNT len;
    const REBYTE *utf8;
    if (ANY_STRING(v)) {
        index = VAL_INDEX(v);
        len = VAL_LEN_AT(v);
        REBSER *temp = Temp_Bin_Str_Managed(v, &index, &len);
        utf8 = BIN_AT(temp, index);
    }
    else {
        assert(ANY_WORD(v));
        index = 0;
        utf8 = VAL_WORD_HEAD(v);
        len = LEN_BYTES(utf8);
    }

    if (buf == NULL) {
        assert(buf_chars == 0);
        return len; // caller must allocate a buffer of size len + 1
    }

    REBCNT limit = MIN(buf_chars, len);
    memcpy(s_cast(buf), cs_cast(utf8), limit);
    buf[limit] = '\0';
    return len;
}


//
//  rebValUTF8Alloc: RL_API
//
REBYTE *RL_rebValUTF8Alloc(REBCNT *out_len, const REBVAL *v)
{
    REBCNT len = rebValUTF8(NULL, 0, v);
    REBYTE *result = OS_ALLOC_N(REBYTE, len + 1);
    rebValUTF8(result, len, v);
    if (out_len != NULL)
        *out_len = len;
    return result;
}


//
//  rebValWstring: RL_API
//
// Extract wchar_t data from an ANY-STRING! or ANY-WORD!.  Note that while
// the size of a wchar_t varies on Linux, it is part of the windows platform
// standard to be two bytes.
//
REBCNT RL_rebValWstring(
    wchar_t *buf,
    REBCNT buf_chars, // characters buffer can hold (not including terminator)
    const REBVAL *v
){
/*
    if (VAL_BYTE_SIZE(val)) {
        // On windows, we need to convert byte to wide:
        REBINT n = VAL_LEN_AT(val);
        REBSER *up = Make_Unicode(n);

        // !!!"Leaks" in the sense that the GC has to take care of this
        MANAGE_SERIES(up);

        n = Decode_UTF8_Negative_If_Latin1(
            UNI_HEAD(up),
            VAL_BIN_AT(val),
            n,
            FALSE
        );
        TERM_UNI_LEN(up, abs(n));

        if (out) *out = up;

        return cast(REBCHR*, UNI_HEAD(up));
    }
    else {
        // Already wide, we can use it as-is:
        // !Assumes the OS uses same wide format!

        if (out) *out = VAL_SERIES(val);

        return cast(REBCHR*, VAL_UNI_AT(val));
    }
*/

    REBCNT index;
    REBCNT len;
    if (ANY_STRING(v)) {
        index = VAL_INDEX(v);
        len = VAL_LEN_AT(v);
    }
    else {
        assert(ANY_WORD(v));
        panic ("extracting wide characters from WORD! not yet supported");
    }

    if (buf == NULL) { // querying for size
        assert(buf_chars == 0);
        return len; // caller must now allocate buffer of len + 1
    }

    REBSER *s = VAL_SERIES(v);

    REBCNT limit = MIN(buf_chars, len);
    REBCNT n = 0;
    for (; index < limit; ++n, ++index)
        buf[n] = GET_ANY_CHAR(s, index);

    buf[limit] = 0;
    return len;
}


//
//  rebValWstringAlloc: RL_API
//
wchar_t *RL_rebValWstringAlloc(REBCNT *out_len, const REBVAL *v)
{
    REBCNT len = rebValWstring(NULL, 0, v);
    wchar_t *result = OS_ALLOC_N(wchar_t, len + 1);
    rebValWstring(result, len, v);
    if (out_len != NULL)
        *out_len = len;
    return result;
}


//
//  rebString: RL_API
//
REBVAL *RL_rebString(const char *utf8)
{
    // Default the returned handle's lifetime to the currently running FRAME!.
    // The user can unmanage it if they want it to live longer.
    //
    assert(FS_TOP != NULL);
    REBVAL *pairing = Alloc_Pairing(FS_TOP);
    Init_String(pairing, Make_UTF8_May_Fail(utf8));
    Manage_Pairing(pairing);
    return pairing;
}


//
//  rebUnmanage: RL_API
//
REBVAL *RL_rebUnmanage(REBVAL *v)
{
    REBVAL *key = PAIRING_KEY(v);
    assert(key->header.bits & NODE_FLAG_MANAGED);
    UNUSED(key);

    Unmanage_Pairing(v);
    return v;
}


//
//  rebFree: RL_API
//
void RL_rebFree(REBVAL *v)
{
    REBVAL *key = PAIRING_KEY(v);
    assert(NOT(key->header.bits & NODE_FLAG_MANAGED));
    UNUSED(key);

    Free_Pairing(v);
}


//
//  rebPanic: RL_API
//
void RL_rebPanic(const void *p)
{
    Panic_Core(p, __FILE__, __LINE__);
}


// We wish to define a table of the above functions to pass to clients.  To
// save on typing, the declaration of the table is autogenerated as a file we
// can include here.
//
// It doesn't make a lot of sense to expose this table to clients via an API
// that returns it, because that's a chicken-and-the-egg problem.  The reason
// a table is being used in the first place is because extensions can't link
// to an EXE (in a generic way).  So the table is passed to them, in that
// extension's DLL initialization function.
//
// !!! Note: if Rebol is built as a DLL or LIB, the story is different.
//
extern RL_LIB Ext_Lib;
#include "tmp-reb-lib-table.inc" // declares Ext_Lib
