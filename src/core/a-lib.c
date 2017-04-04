//
//  File: %a-lib.c
//  Summary: "exported REBOL library functions"
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

#include "sys-core.h"

// !!! Most of the Rebol source does not include %reb-ext.h.  As a result
// REBRXT and RXIARG and RXIFRM are not defined when %tmp-funcs.h is being
// compiled, so the MAKE PREP process doesn't auto-generate prototypes for
// these functions.
//
// Rather than try and define RX* for all of the core to include, assume that
// the burden of keeping these in sync manually is for the best.
//
#include "reb-ext.h"

// Linkage back to HOST functions. Needed when we compile as a DLL
// in order to use the OS_* macro functions.
#ifdef REB_API  // Included by C command line
REBOL_HOST_LIB *Host_Lib;
#endif


static REBRXT Reb_To_RXT[REB_MAX];
static enum Reb_Kind RXT_To_Reb[RXT_MAX];


#include "reb-lib.h" // forward definitions needed for "extern C" linkage


//
//  RL_Version: C
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
RL_API void RL_Version(REBYTE vers[])
{
    // [0] is length
    vers[1] = REBOL_VER;
    vers[2] = REBOL_REV;
    vers[3] = REBOL_UPD;
    vers[4] = REBOL_SYS;
    vers[5] = REBOL_VAR;
}


//
//  RL_Init: C
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
void RL_Init(void *lib)
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
//  RL_Shutdown: C
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
RL_API void RL_Shutdown(REBOOL clean)
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
//  RL_Escape: C
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
RL_API void RL_Escape(void)
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
//  RL_Event: C
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
RL_API int RL_Event(REBEVT *evt)
{
    REBVAL *event = Append_Event();     // sets signal

    if (event) {                        // null if no room left in series
        Reset_Val_Header(event, REB_EVENT); // has more space, if needed
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
//  RL_Update_Event: C
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
RL_API int RL_Update_Event(REBEVT *evt)
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

    return RL_Event(evt) - 1;
}


//
//  RL_Find_Event: C
//
// Find an application event (e.g. GUI) to the event port.
//
// Returns:
//     A pointer to the find event
// Arguments:
//     model - event model
//     type - event type
//
RL_API REBEVT *RL_Find_Event (REBINT model, REBINT type)
{
    REBVAL * val = Find_Last_Event(model, type);
    if (val != NULL) {
        return cast(REBEVT*, val); // should be compatible!
    }
    return NULL;
}


//
//  RL_Gob_Head: C
//
RL_API REBGOB** RL_Gob_Head(REBGOB *gob)
{
    return SER_HEAD(REBGOB*, GOB_PANE(gob));
}


//
//  RL_Gob_String: C
//
RL_API REBYTE* RL_Gob_String(REBGOB *gob)
{
    return BIN_HEAD(GOB_CONTENT(gob));
}


//
//  RL_Gob_Len: C
//
RL_API REBCNT RL_Gob_Len(REBGOB *gob)
{
    return SER_LEN(GOB_PANE(gob));
}


//
//  RL_Encode_UTF8: C
//
// Encode the unicode into UTF8 byte string.
//
// Returns:
// Number of dst bytes used.
//
// Arguments:
// dst - destination for encoded UTF8 bytes
// max - maximum size of the result in bytes
// src - source array of bytes or wide characters
// len - input is source length, updated to reflect src chars used
// unicode - true if src is in wide character format
// crlf_to_lf - convert carriage-return + linefeed into just linefeed
//
// Notes:
// Does not add a terminator.
//
// !!! Host code is not supposed to call any Rebol routines except
// for those in the RL_Api.  This exposes Rebol's internal UTF8
// length routine, as it was being used by the Linux host code by
// Atronix.  Should be reviewed along with the rest of the RL_Api.
//
RL_API REBCNT RL_Encode_UTF8(
    REBYTE *dst,
    REBINT max,
    const void *src,
    REBCNT *len,
    REBOOL unicode,
    REBOOL crlf_to_lf
) {
    return Encode_UTF8(
        dst,
        max,
        src,
        len,
        (unicode ? OPT_ENC_UNISRC : 0) | (crlf_to_lf ? OPT_ENC_CRLF : 0)
    );
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
// However, long term the external "C" user API will not speak about REBSERs.
// It will operate purely on the level of REBVAL*, where those values will
// either be individually managed (as "pairings" under GC control) or have
// their lifetime controlled other ways.  That layer of API is of secondary
// importance to refining the internal API (also used by "user natives")
// as well as the Ren-Cpp API...although it will use several of the same
// mechanisms that Ren-Cpp does to achieve its goals.
//

inline static REBFRM *Extract_Live_Rebfrm_May_Fail(const REBVAL *frame) {
    if (!IS_FRAME(frame))
        fail ("Not a FRAME!");

    REBCTX *frame_ctx = VAL_CONTEXT(frame);
    REBFRM *f = CTX_FRAME_IF_ON_STACK(frame_ctx);
    if (f == NULL)
        fail ("FRAME! is no longer on stack.");

    assert(Is_Any_Function_Frame(f));
    assert(NOT(Is_Function_Frame_Fulfilling(f)));
    return f;
}


//
//  RL_Frm_Num_Args: C
//
RL_API REBCNT RL_Frm_Num_Args(const REBVAL *frame) {
    REBFRM *f = Extract_Live_Rebfrm_May_Fail(frame);
    return FRM_NUM_ARGS(f);
}

//
//  RL_Frm_Arg: C
//
RL_API REBVAL *RL_Frm_Arg(const REBVAL *frame, REBCNT n) {
    REBFRM *f = Extract_Live_Rebfrm_May_Fail(frame);
    return FRM_ARG(f, n);
}

//
//  RL_Val_Logic: C
//
RL_API REBOOL RL_Val_Logic(const REBVAL *v) {
    return VAL_LOGIC(v);
}

//
//  RL_Val_Type: C
//
// !!! Among the few concepts from the original host kit API that may make
// sense, it could be a good idea to abstract numbers for datatypes from the
// REB_XXX numbering scheme.  So for the moment, REBRXT is being kept as is.
//
RL_API REBRXT RL_Val_Type(const REBVAL *v) {
    return IS_VOID(v)
        ? 0
        : Reb_To_RXT[VAL_TYPE(v)];
}


//
//  RL_Val_Update_Header: C
//
RL_API void RL_Val_Update_Header(REBVAL *v, REBRXT rxt) {
    if (rxt == 0)
        Init_Void(v);
    else
        Reset_Val_Header(v, RXT_To_Reb[rxt]);
}


//
//  RL_Val_Int64: C
//
RL_API REBI64 RL_Val_Int64(const REBVAL *v) {
    return VAL_INT64(v);
}

//
//  RL_Val_Int32: C
//
RL_API REBINT RL_Val_Int32(const REBVAL *v) {
    return VAL_INT32(v);
}

//
//  RL_Val_Decimal: C
//
RL_API REBDEC RL_Val_Decimal(const REBVAL *v) {
    return VAL_DECIMAL(v);
}

//
//  RL_Val_Char: C
//
RL_API REBUNI RL_Val_Char(const REBVAL *v) {
    return VAL_CHAR(v);
}

//
//  RL_Val_Time: C
//
RL_API REBI64 RL_Val_Time(const REBVAL *v) {
    return VAL_TIME(v);
}

//
//  RL_Val_Date: C
//
RL_API REBINT RL_Val_Date(const REBVAL *v) {
    return VAL_DATE(v).bits; // !!! Is this right?
}

//
//  RL_Val_Tuple_Data: C
//
RL_API REBYTE *RL_Val_Tuple_Data(const REBVAL *v) {
    return VAL_TUPLE_DATA(m_cast(REBVAL*, v));
}

//
//  RL_Val_Index: C
//
RL_API REBCNT RL_Val_Index(const REBVAL *v) {
    return VAL_INDEX(v);
}

//
//  RL_Init_Val_Index: C
//
RL_API void RL_Init_Val_Index(REBVAL *v, REBCNT i) {
    VAL_INDEX(v) = i;
}

//
//  RL_Val_Handle_Pointer: C
//
RL_API void *RL_Val_Handle_Pointer(const REBVAL *v) {
    return VAL_HANDLE_POINTER(void, v);
}

//
//  RL_Set_Handle_Pointer: C
//
RL_API void RL_Set_Handle_Pointer(REBVAL *v, void *p) {
    v->extra.singular = NULL; // !!! only support "dumb" handles for now
    SET_HANDLE_POINTER(v, p);
}

//
//  RL_Val_Image_Wide: C
//
RL_API REBCNT RL_Val_Image_Wide(const REBVAL *v) {
    return VAL_IMAGE_WIDE(v);
}

//
//  RL_Val_Image_High: C
//
RL_API REBCNT RL_Val_Image_High(const REBVAL *v) {
    return VAL_IMAGE_HIGH(v);
}

//
//  RL_Val_Pair_X_Float: C
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
RL_API float RL_Val_Pair_X_Float(const REBVAL *v) {
    return VAL_PAIR_X(v);
}

//
//  RL_Val_Pair_Y_Float: C
//
// !!! See notes on RL_Val_Pair_X_Float
//
RL_API float RL_Val_Pair_Y_Float(const REBVAL *v) {
    return VAL_PAIR_Y(v);
}

//
//  RL_Init_Date: C
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
RL_API void RL_Init_Date(
    REBVAL *out,
    int year,
    int month,
    int day,
    int time,
    int nano,
    int zone
) {
    Reset_Val_Header(out, REB_DATE);
    VAL_YEAR(out)  = year;
    VAL_MONTH(out) = month;
    VAL_DAY(out) = day;
    VAL_ZONE(out) = zone / ZONE_MINS;
    VAL_TIME(out) = TIME_SEC(time) + nano;
}


#include "reb-lib-lib.h"

//
//  Extension_Lib: C
//
void *Extension_Lib(void)
{
    return &Ext_Lib;
}
