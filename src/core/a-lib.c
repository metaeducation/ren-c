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


static REBVAL *PG_last_error = NULL;
static REBRXT Reb_To_RXT[REB_MAX];
static enum Reb_Kind RXT_To_Reb[RXT_MAX];


// !!! Review how much checking one wants to do when calling API routines,
// and what the balance should be of debug vs. release.  Right now, this helps
// in particular notice if the core tries to use an API function before the
// proper moment in the boot.
//
// !!! Review the balance of which APIs can set or clear errors.  It's not
// useful if they all do as a rule...for instance, it may be more convenient
// to `rebFree()` something before an error check than be forced to free it
// on branches that test for an error on the call before the free.  See the
// notes on Windows GetLastError() about how the APIs document whether they
// manipulate the error or not--not all of them clear it rotely.
//
inline static void Enter_Api_Cant_Error(void) {
    if (PG_last_error == NULL)
        panic ("rebStartup() not called before API call");
}
inline static void Enter_Api_Clear_Last_Error(void) {
    Enter_Api_Cant_Error();
    SET_END(PG_last_error);
}


//
//  Startup_Api: C
//
// RL_API routines may be used by extensions (which are invoked by a fully
// initialized Rebol core) or by normal linkage (such as from within the core
// itself).  A call to rebInit() won't be needed in the former case.  So
// setup code that is needed to interact with the API needs to be done by the
// core independently.
//
void Startup_Api(void)
{
    // The last_error is used to signal whether the API has been initialized
    // as well as to store a copy of the last error.  If it's END then that
    // means no error.
    //
    assert(PG_last_error == NULL);
    PG_last_error = Alloc_Pairing(NULL);
    SET_END(PG_last_error);
    Init_Blank(PAIRING_KEY(PG_last_error));

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
}


//
//  Shutdown_Api: C
//
// See remarks on Startup_Api() for the difference between this idea and
// rebShutdown.
//
void Shutdown_Api(void)
{
    assert(PG_last_error != NULL);
    Free_Pairing(PG_last_error);
}


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
//              rebPanic ("Incompatible reb-lib DLL");
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
//  rebStartup: RL_API
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
void RL_rebStartup(void *lib)
{
    if (PG_last_error != NULL)
        panic ("rebStartup() called when it's already started");

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
    Enter_Api_Cant_Error();

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
//  rebBlock: RL_API
//
// !!! The variadic rebBlock() constructor is coming soon, but this is just
// to create an API handle to use with rebFree() for a quick workaround to
// get the one-entry-point idea in the console moving along.
//
REBVAL *RL_rebBlock(
    const void *p1,
    const void *p2,
    const void *p3,
    const void *p4
){
    Enter_Api_Clear_Last_Error();

    assert(Detect_Rebol_Pointer(p1) == DETECTED_AS_VALUE);
    assert(Detect_Rebol_Pointer(p2) == DETECTED_AS_VALUE);
    assert(Detect_Rebol_Pointer(p3) == DETECTED_AS_VALUE);
    assert(Detect_Rebol_Pointer(p4) == DETECTED_AS_END);

    REBARR *array = Make_Array(3);
    Append_Value(array, cast(const REBVAL*, p1));
    Append_Value(array, cast(const REBVAL*, p2));
    Append_Value(array, cast(const REBVAL*, p3));
    UNUSED(p4);
    TERM_ARRAY_LEN(array, 3);

    // For now, do a test of manual memory management in a pairing, and let's
    // just say a BLANK! means that for now.  Assume caller has to explicitly
    // rebFree() the result.
    //
    REBVAL *result = Alloc_Pairing(NULL);
    Init_Block(result, array);
    Init_Blank(PAIRING_KEY(result)); // the meta-value of the API handle

    return result;
}


// Broken out as a function to avoid longjmp "clobbering" from PUSH_TRAP()
// Actual pointers themselves have to be `const` (as opposed to pointing to
// const data) to avoid the compiler warning in some older GCCs.
//
inline static REBOOL Reb_Do_Api_Core_Fails(
    REBVAL * const out,
    const void * const p,
    va_list * const vaptr
){
    struct Reb_State state;
    REBCTX *error;

    PUSH_UNHALTABLE_TRAP(&error, &state); // must catch HALTs

// The first time through the following code 'error' will be NULL, but...
// `fail` can longjmp here, so 'error' won't be NULL *if* that happens!

    if (error != NULL) {
        if (ERR_NUM(error) == RE_HALT) {
            Init_Bar(PG_last_error); // denotes halting (for now)
            return TRUE;
        }

        Init_Error(PG_last_error, error);
        return TRUE;
    }

    // Note: It's not possible to make C variadics that can take 0 arguments;
    // there always has to be one real argument to find the varargs.  Luckily
    // the design of REBFRM* allows us to pre-load one argument outside of the
    // REBARR* or va_list is being passed in.  Pass `p` as opt_first argument.
    //
    // !!! Loading of UTF-8 strings is not supported yet, cast to REBVAL
    //
    REBIXO indexor = Do_Va_Core(
        out,
        cast(const REBVAL*, p), // opt_first (see note above)
        vaptr,
        DO_FLAG_EXPLICIT_EVALUATE | DO_FLAG_TO_END
    );

    DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

    if (indexor == THROWN_FLAG) {
        if (IS_FUNCTION(out) && VAL_FUNC_DISPATCHER(out) == &N_quit) {
            //
            // Command issued a purposeful QUIT or EXIT.  Convert the
            // QUIT/WITH value (if any) into an integer for last error to
            // signal this (for now).
            //
            CATCH_THROWN(out, out);
            Init_Integer(PG_last_error, Exit_Status_From_Value(out));
            return TRUE;
        }

        // For now, convert all other THROWN() values into uncaught throw
        // errors.  Since that error captures the thrown value as well as the
        // throw name, the information is there to be extracted.
        //
        Init_Error(PG_last_error, Error_No_Catch_For_Throw(out));
        return TRUE;
    }

    assert(indexor == END_FLAG); // we asked to do to end
    return FALSE;
}


//
//  rebDo: RL_API
//
// C variadic function which calls the evaluator on multiple pointers.
// Each pointer may either be a REBVAL* or a UTF-8 string which will be
// scanned to reflect one or more values in the sequence.  All REBVAL* are
// spliced in inert by default, as if they were an evaluative product already.
//
REBVAL *RL_rebDo(const void *p, ...)
{
    Enter_Api_Clear_Last_Error();

    // For now, do a test of manual memory management in a pairing, and let's
    // just say a BLANK! means that for now.  Assume caller has to explicitly
    // rebFree() the result.
    //
    REBVAL *result = Alloc_Pairing(NULL);
    assert(IS_TRASH_DEBUG(result)); // ok: Do_Va_Core() will set to END
    Init_Blank(PAIRING_KEY(result)); // the meta-value of the API handle

    va_list va;
    va_start(va, p);

    // Due to the way longjmp works, it can possibly "clobber" result if it
    // is in a register.  The easiest way to get around this is to wrap the
    // code in a separate function.  Even if that function is inlined, it
    // should obey the conventions.
    //
    if (Reb_Do_Api_Core_Fails(result, p, &va)) {
        Free_Pairing(result);
        va_end(va);
        return NULL;
    }

    va_end(va);
    return result; // client's responsibility to rebFree(), for now
}


//
//  rebDoValue: RL_API
//
// Non-variadic function which takes a single argument which must be a single
// value.  It invokes the basic behavior of the DO native on a value.
//
REBVAL *RL_rebDoValue(const REBVAL *v)
{
    // don't need an Enter_Api_Clear_Last_Error(); call while implementation
    // is based on the RL_API rebDo(), because it will do it.

    // !!! One design goal of Ren-C's RL_API is to limit the number of
    // fundamental exposed C functions.  So this formulation of rebDoValue()
    // is a good example of something that might be in "userspace", vs. part
    // of the "main" RL_API...possibly using generic memoizations for speed
    // so "do" and "quote" could be provided textually by users yet not
    // loaded and bound each time.
    //
    // As with much of the API it could be more optimal, but this is a test
    // of the concept.  BLANK_VALUE has to be used for the moment as the
    // first instruction since rebEval() instructions are not allowed in
    // the preload slot of a variadic yet.
    //
    return rebDo(BLANK_VALUE, rebEval(NAT_VALUE(do)), v, END);
}


//
//  rebLastError: RL_API
//
// Get the last error that occurred, or NULL if none.
//
REBVAL *RL_rebLastError(void)
{
    Enter_Api_Cant_Error(); // just checking error doesn't clear it

    if (IS_END(PG_last_error))
        return NULL; // error clear since last API called which might have one

    assert(
        IS_BAR(PG_last_error) // currently denotes HALT
        || IS_INTEGER(PG_last_error) // currently denotes QUIT/WITH status
        || IS_ERROR(PG_last_error) // other errors including uncaught THROW
    );

    // Returning a copy is important, because the error can be an object and
    // someone might want to pick apart the error properties using API
    // functions.  Giving back a direct pointer to the last error would mean
    // those APIs would overwrite it during inspection.
    //
    REBVAL *result = Alloc_Pairing(NULL);
    Move_Value(result, PG_last_error);
    Init_Blank(PAIRING_KEY(result)); // manual lifetime for now

    return result; // for now, caller has to rebFree() the result
}


//
//  rebEval: RL_API
//
// When rebDo() receives a REBVAL*, the default is to assume it should be
// spliced into the input stream as if it had already been evaluated.  It's
// only segments of code supplied via UTF-8 strings, that are live and can
// execute functions.
//
// This instruction is used with rebDo() in order to mark a value as being
// evaluated.
//
void *RL_rebEval(const REBVAL *v)
{
    Enter_Api_Clear_Last_Error();

    // !!! The presence of the VALUE_FLAG_EVAL_FLIP is a pretty good
    // indication that it's an eval instruction.  So it's not necessary to
    // fill in the ->link or ->misc fields.  But if there were more
    // instructions like this, there'd probably need to be a misc->opcode or
    // something to distinguish them.
    //
    REBARR *result = Alloc_Singular_Array();
    Move_Value(KNOWN(ARR_HEAD(result)), v);
    SET_VAL_FLAG(ARR_HEAD(result), VALUE_FLAG_EVAL_FLIP);

    // !!! The intent for the long term is that these rebEval() instructions
    // not tax the garbage collector and be freed as they are encountered
    // while traversing the va_list.  Right now an assert would trip if we
    // tried that.  It's a good assert in general, so rather than subvert it
    // the instructions are just GC managed for now.
    //
    MANAGE_ARRAY(result);
    return result;
}


//
//  rebVoid: RL_API
//
// One theory of the RL_API might be that void values not be allowed, as they
// are illegal in blocks.  A question might be whether an API REBVAL is more
// similar to a variable in a context, than it is to a cell in an array...and
// if a REBVAL* could point to a cell which could be mutable from void to
// non-void.
//
// For now, this rebVoid is one which returns a mutable REBVAL* and whose
// lifetime requires that it be freed.
//
REBVAL *RL_rebVoid(void)
{
    Enter_Api_Clear_Last_Error();

    REBVAL *result = Alloc_Pairing(NULL);
    Init_Void(result);
    Init_Blank(PAIRING_KEY(result));

    return result;
}


//
//  rebBlank: RL_API
//
// As with rebVoid(), this is conceptually something that doesn't need to
// be freed...but for uniformity might.  It could be reference counted just
// to make sure the right number of free calls were made, even if it were
// only allocated once.
//
REBVAL *RL_rebBlank(void)
{
    Enter_Api_Clear_Last_Error();

    REBVAL *result = Alloc_Pairing(NULL);
    Init_Blank(result);
    Init_Blank(PAIRING_KEY(result));

    return result;
}

//
//  rebHalt: RL_API
//
// Signal that code evaluation needs to be interrupted.
//
// Returns:
//     nothing
// Notes:
//     This function sets a signal that is checked during evaluation
//     and will cause the interpreter to begin processing an escape
//     trap. Note that control must be passed back to REBOL for the
//     signal to be recognized and handled.
//
void RL_rebHalt(void)
{
    Enter_Api_Clear_Last_Error();

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
    Enter_Api_Clear_Last_Error();

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
    Enter_Api_Clear_Last_Error();

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
    Enter_Api_Clear_Last_Error();

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
    Enter_Api_Clear_Last_Error();

    return SER_HEAD(REBGOB*, GOB_PANE(gob));
}


//
//  rebGobString: RL_API
//
REBYTE* RL_rebGobString(REBGOB *gob)
{
    Enter_Api_Clear_Last_Error();

    return BIN_HEAD(GOB_CONTENT(gob));
}


//
//  rebGobLen: RL_API
//
REBCNT RL_rebGobLen(REBGOB *gob)
{
    Enter_Api_Clear_Last_Error();

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
    Enter_Api_Clear_Last_Error();

    REBFRM *f = Extract_Live_Rebfrm_May_Fail(frame);
    return FRM_NUM_ARGS(f);
}

//
//  rebFrmArg: RL_API
//
REBVAL *RL_rebFrmArg(const REBVAL *frame, REBCNT n) {
    Enter_Api_Clear_Last_Error();

    REBFRM *f = Extract_Live_Rebfrm_May_Fail(frame);
    return FRM_ARG(f, n);
}

//
//  rebValLogic: RL_API
//
REBOOL RL_rebValLogic(const REBVAL *v) {
    Enter_Api_Clear_Last_Error();

    return VAL_LOGIC(v);
}

//
//  rebTypeOf: RL_API
//
// !!! Among the few concepts from the original host kit API that may make
// sense, it could be a good idea to abstract numbers for datatypes from the
// REB_XXX numbering scheme.  So for the moment, REBRXT is being kept as is.
//
REBRXT RL_rebTypeOf(const REBVAL *v) {
    Enter_Api_Clear_Last_Error();

    return IS_VOID(v)
        ? 0
        : Reb_To_RXT[VAL_TYPE(v)];
}


//
//  rebValInt64: RL_API
//
REBI64 RL_rebValInt64(const REBVAL *v) {
    Enter_Api_Clear_Last_Error();

    return VAL_INT64(v);
}

//
//  rebValInt32: RL_API
//
REBINT RL_rebValInt32(const REBVAL *v) {
    Enter_Api_Clear_Last_Error();

    return VAL_INT32(v);
}

//
//  rebValDecimal: RL_API
//
REBDEC RL_rebValDecimal(const REBVAL *v) {
    Enter_Api_Clear_Last_Error();

    return VAL_DECIMAL(v);
}

//
//  rebValChar: RL_API
//
REBUNI RL_rebValChar(const REBVAL *v) {
    Enter_Api_Clear_Last_Error();

    return VAL_CHAR(v);
}

//
//  rebValTime: RL_API
//
REBI64 RL_rebValTime(const REBVAL *v) {
    Enter_Api_Clear_Last_Error();

    return VAL_NANO(v);
}


//
//  rebValTupleData: RL_API
//
REBYTE *RL_rebValTupleData(const REBVAL *v) {
    Enter_Api_Clear_Last_Error();

    return VAL_TUPLE_DATA(m_cast(REBVAL*, v));
}

//
//  rebValIndex: RL_API
//
REBCNT RL_rebValIndex(const REBVAL *v) {
    Enter_Api_Clear_Last_Error();

    return VAL_INDEX(v);
}

//
//  rebInitValIndex: RL_API
//
void RL_rebInitValIndex(REBVAL *v, REBCNT i) {
    Enter_Api_Clear_Last_Error();

    VAL_INDEX(v) = i;
}

//
//  rebValHandlePointer: RL_API
//
void *RL_rebValHandlePointer(const REBVAL *v) {
    Enter_Api_Clear_Last_Error();

    return VAL_HANDLE_POINTER(void, v);
}

//
//  rebSetHandlePointer: RL_API
//
void RL_rebSetHandlePointer(REBVAL *v, void *p) {
    Enter_Api_Clear_Last_Error();

    v->extra.singular = NULL; // !!! only support "dumb" handles for now
    SET_HANDLE_POINTER(v, p);
}

//
//  rebValImageWide: RL_API
//
REBCNT RL_rebValImageWide(const REBVAL *v) {
    Enter_Api_Clear_Last_Error();

    return VAL_IMAGE_WIDE(v);
}

//
//  rebValImageHigh: RL_API
//
REBCNT RL_rebValImageHigh(const REBVAL *v) {
    Enter_Api_Clear_Last_Error();

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
    Enter_Api_Clear_Last_Error();

    return VAL_PAIR_X(v);
}

//
//  rebValPairYFloat: RL_API
//
// !!! See notes on RL_rebValPairXFloat
//
float RL_rebValPairYFloat(const REBVAL *v) {
    Enter_Api_Clear_Last_Error();

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
){
    Enter_Api_Clear_Last_Error();

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
//  rebSpellingOf: RL_API
//
// Extract UTF-8 data from an ANY-STRING! or ANY-WORD!.
//
REBCNT RL_rebSpellingOf(
    char *buf,
    REBCNT buf_chars,
    const REBVAL *v
){
    Enter_Api_Clear_Last_Error();

    REBCNT index;
    REBCNT len;
    const REBYTE *utf8;
    if (ANY_STRING(v)) {
        index = VAL_INDEX(v);
        len = VAL_LEN_AT(v);
        REBSER *temp = Temp_UTF8_At_Managed(v, &index, &len);
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
    memcpy(buf, cs_cast(utf8), limit);
    buf[limit] = '\0';
    return len;
}


//
//  rebSpellingOfAlloc: RL_API
//
char *RL_rebSpellingOfAlloc(REBCNT *len_out, const REBVAL *v)
{
    Enter_Api_Clear_Last_Error();

    REBCNT len = rebSpellingOf(NULL, 0, v);
    char *result = OS_ALLOC_N(char, len + 1);
    rebSpellingOf(result, len, v);
    if (len_out != NULL)
        *len_out = len;
    return result;
}


//
//  rebSpellingOfW: RL_API
//
// Extract wchar_t data from an ANY-STRING! or ANY-WORD!.  Note that while
// the size of a wchar_t varies on Linux, it is part of the windows platform
// standard to be two bytes.
//
REBCNT RL_rebSpellingOfW(
    wchar_t *buf,
    REBCNT buf_chars, // characters buffer can hold (not including terminator)
    const REBVAL *v
){
    Enter_Api_Clear_Last_Error();

    REBSER *s;
    REBCNT index;
    REBCNT len;
    if (ANY_STRING(v)) {
        s = VAL_SERIES(v);
        index = VAL_INDEX(v);
        len = VAL_LEN_AT(v);
    }
    else {
        assert(ANY_WORD(v));
        s = Make_UTF8_May_Fail(VAL_WORD_HEAD(v)); // makes REBUNI series
        index = 0;
        len = SER_LEN(s);
    }

    if (buf == NULL) { // querying for size
        assert(buf_chars == 0);
        return len; // caller must now allocate buffer of len + 1
    }

    REBCNT limit = MIN(buf_chars, len);
    REBCNT n = 0;
    for (; index < limit; ++n, ++index)
        buf[n] = GET_ANY_CHAR(s, index);

    buf[limit] = 0;
    return len;
}


//
//  rebSpellingOfAllocW: RL_API
//
wchar_t *RL_rebSpellingOfAllocW(REBCNT *len_out, const REBVAL *v)
{
    Enter_Api_Clear_Last_Error();

    REBCNT len = rebSpellingOfW(NULL, 0, v);
    wchar_t *result = OS_ALLOC_N(wchar_t, len + 1);
    rebSpellingOfW(result, len, v);
    if (len_out != NULL)
        *len_out = len;
    return result;
}


//
//  rebValBin: RL_API
//
// Extract binary data from a BINARY!
//
REBCNT RL_rebValBin(
    REBYTE *buf,
    REBCNT buf_chars,
    const REBVAL *binary
){
    Enter_Api_Clear_Last_Error();

    if (NOT(IS_BINARY(binary)))
        fail ("rebValBin() only works on BINARY!");

    REBCNT len = VAL_LEN_AT(binary);

    if (buf == NULL) {
        assert(buf_chars == 0);
        return len; // caller must allocate a buffer of size len + 1
    }

    REBCNT limit = MIN(buf_chars, len);
    memcpy(s_cast(buf), cs_cast(VAL_BIN_AT(binary)), limit);
    buf[limit] = '\0';
    return len;
}


//
//  rebValBinAlloc: RL_API
//
REBYTE *RL_rebValBinAlloc(REBCNT *len_out, const REBVAL *binary)
{
    Enter_Api_Clear_Last_Error();

    REBCNT len = rebValBin(NULL, 0, binary);
    REBYTE *result = OS_ALLOC_N(REBYTE, len + 1);
    rebValBin(result, len, binary);
    if (len_out != NULL)
        *len_out = len;
    return result;
}


//
//  rebString: RL_API
//
REBVAL *RL_rebString(const char *utf8)
{
    Enter_Api_Clear_Last_Error();

    // Default the returned handle's lifetime to the currently running FRAME!.
    // The user can unmanage it if they want it to live longer.
    //
    REBVAL *pairing = Alloc_Pairing(NULL);
    Init_String(pairing, Make_UTF8_May_Fail(cb_cast(utf8)));
    Init_Blank(PAIRING_KEY(pairing));
    return pairing;
}


//
//  rebFile: RL_API
//
REBVAL *RL_rebFile(const char *utf8)
{
    REBVAL *result = rebString(utf8);
    VAL_RESET_HEADER(result, REB_FILE);
    return result;
}


//
//  rebStringW: RL_API
//
REBVAL *RL_rebStringW(const wchar_t *wstr)
{
    Enter_Api_Clear_Last_Error();

    REBCNT num_chars;
#ifdef TO_WINDOWS
    num_chars = wcslen(wstr);
#else
    fail("wide character counting on wchar_t not implemented yet on linux");
#endif

    REBSER *ser = Make_Unicode(num_chars);
    memcpy(UNI_HEAD(ser), wstr, sizeof(wchar_t) * num_chars);
    TERM_UNI_LEN(ser, num_chars);

    REBVAL *pairing = Alloc_Pairing(NULL);
    Init_String(pairing, ser);
    Init_Blank(PAIRING_KEY(pairing));

    return pairing;
}


//
//  rebFileW: RL_API
//
REBVAL *RL_rebFileW(const wchar_t *wstr)
{
    REBVAL *result = rebStringW(wstr);
    VAL_RESET_HEADER(result, REB_FILE);
    return result;
}


//
//  rebUnmanage: RL_API
//
REBVAL *RL_rebUnmanage(REBVAL *v)
{
    Enter_Api_Clear_Last_Error();

    REBVAL *key = PAIRING_KEY(v);
    assert(key->header.bits & NODE_FLAG_MANAGED);
    UNUSED(key);

    Unmanage_Pairing(v);
    return v;
}


//
//  rebCopyExtra: RL_API
//
REBVAL *RL_rebCopyExtra(const REBVAL *v, REBCNT extra)
{
    // !!! It's actually a little bit harder than one might think to hook
    // into the COPY code without actually calling the function via the
    // evaluator, because it is an "action".  Review a good efficient method
    // for doing it, but for the moment it's just needed for FILE! so do that.
    //
    if (NOT(ANY_STRING(v)))
        fail ("rebCopy() only supports ANY-STRING! for now");

    REBVAL *result = Alloc_Pairing(NULL);
    Init_Any_Series(
        result,
        VAL_TYPE(v),
        Copy_Sequence_At_Len_Extra(
            VAL_SERIES(v),
            VAL_INDEX(v),
            VAL_LEN_AT(v),
            extra
        )
    );
    Init_Blank(PAIRING_KEY(result));

    return result;
}


//
//  rebFree: RL_API
//
void RL_rebFree(REBVAL *v)
{
    Enter_Api_Cant_Error();

    // !!! This check isn't really ideal.  There should be a foolproof way
    // to tell whether a REBVAL* is an API handle or not, so that series can't
    // be corrupted.  It's possible, just needs to be done...this covers 99%
    // of the cases for right now.
    //
    REBVAL *key = PAIRING_KEY(v);
    assert(NOT(key->header.bits & NODE_FLAG_MANAGED));
    if (NOT(IS_BLANK(key)))
        fail ("Attempt to rebFree() a non-API handle");

    Free_Pairing(v);
}


//
//  rebError: RL_API
//
REBVAL *RL_rebError(const char *msg)
{
    Enter_Api_Cant_Error();

    REBVAL *result = Alloc_Pairing(NULL);
    Init_Error(result, Error_User(msg));
    Init_Blank(PAIRING_KEY(result));

    return result; // user currently responsible for rebFree()-ing
}


//
//  rebFail: RL_API
//
void RL_rebFail(const void *p)
{
    Enter_Api_Cant_Error();

    // !!! Should allow caller to pass these in via macro.
    //
#if !defined(NDEBUG)
    TG_Erroring_C_File = __FILE__;
    TG_Erroring_C_Line = __LINE__;
#endif

    Fail_Core(p);
}


//
//  rebPanic: RL_API
//
// panic() and panic_at() are used internally to the interpreter for
// situations which are so corrupt that the interpreter cannot safely run
// any more functions (a "blue screen of death").  However, panics at the
// API level should not be that severe.
//
// So this routine is willing to do delegation.  If it receives a UTF-8
// string, it will convert it to a STRING! and call the PANIC native on that
// string.  If it receives a REBVAL*, it will call the PANIC-VALUE native on
// that string.
//
// By dispatching to FUNCTION!s to do the dirty work of crashing out and
// exiting Rebol, this allows a console or user to HIJACK those functions
// with custom behavior (such as more graceful exits, writing to logs).
// That hijacking would also affect any other "safe" PANIC calls in userspace.
//
void RL_rebPanic(const void *p)
{
    Enter_Api_Cant_Error();

#ifdef NDEBUG
    REBCNT tick = 0;
#else
    REBCNT tick = TG_Tick;
#endif

    // Like Panic_Core, the underlying API for rebPanic might want to take an
    // optional file and line.
    //
    char *file = NULL;
    int line = 0;

    // !!! Should there be a special bit or dispatcher used on the PANIC and
    // PANIC-VALUE functions that ensures they exit?  If it were a dispatcher
    // then HIJACK would have to be aware of it and preserve it.

    const void *p2 = p; // keep original p for examining in the debugger

    switch (Detect_Rebol_Pointer(p)) {
    case DETECTED_AS_UTF8:
        rebDo(
            BLANK_VALUE, // temp workaround, can't rebEval() first slot yet
            rebEval(NAT_VALUE(panic)),
            rebString(cast(const char*, p)),
            END
        );
        p2 = "HIJACK'd PANIC function did not exit Rebol";
        break;

    case DETECTED_AS_SERIES:
    case DETECTED_AS_FREED_SERIES:
        //
        // !!! The libRebol API might use REBSER nodes as an exposed type for
        // special operations (it's already the return result of rebEval()).
        // So it could be reasonable that API-based panics on them are "known"
        // API types that should give more information than a low-level crash.
        //
        break;

    case DETECTED_AS_VALUE:
        rebDo(
            BLANK_VALUE, // temp workaround, can't rebEval() first slot yet
            rebEval(NAT_VALUE(panic_value)),
            cast(const REBVAL*, p),
            END
        );
        p2 = "HIJACK'd PANIC-VALUE function did not exit Rebol";
        break;

    case DETECTED_AS_END:
    case DETECTED_AS_TRASH_CELL:
        break;
    };

    Panic_Core(p2, tick, file, line);
}


//
//  rebFileToLocalAlloc: RL_API
//
// This is the API exposure of TO-LOCAL-FILE.  It takes in a FILE! and
// returns an allocated UTF-8 buffer.
//
// !!! Should MAX_FILE_NAME be taken into account for the OS?
//
char *RL_rebFileToLocalAlloc(REBCNT *len_out, const REBVAL *file, REBOOL full)
{
    Enter_Api_Clear_Last_Error();

    if (NOT(IS_FILE(file)))
        fail ("rebFileToLocalAlloc() only works on FILE!");

    DECLARE_LOCAL (local);
    Init_String(
        local,
        To_Local_Path(VAL_UNI_AT(file), VAL_LEN_AT(file), full)
    );

    return rebSpellingOfAlloc(len_out, local);
}


//
//  rebFileToLocalAllocW: RL_API
//
// This is the API exposure of TO-LOCAL-FILE.  It takes in a FILE! and
// returns an allocated wchar_t buffer.
//
// !!! Should MAX_FILE_NAME be taken into account for the OS?
//
wchar_t *RL_rebFileToLocalAllocW(
    REBCNT *len_out,
    const REBVAL *file,
    REBOOL full
){
    Enter_Api_Clear_Last_Error();

    if (NOT(IS_FILE(file)))
        fail ("rebFileToLocalAllocW() only works on FILE!");

    DECLARE_LOCAL (local);
    Init_String(
        local,
        To_Local_Path(VAL_UNI_AT(file), VAL_LEN_AT(file), full)
    );

    return rebSpellingOfAllocW(len_out, local);
}


//
//  rebLocalToFile: RL_API
//
// This is the API exposure of TO-REBOL-FILE.  It takes in a UTF-8 buffer and
// returns a FILE!.
//
// !!! Should MAX_FILE_NAME be taken into account for the OS?
//
REBVAL *RL_rebLocalToFile(const char *local, REBOOL is_dir)
{
    Enter_Api_Clear_Last_Error();

    // !!! Current inefficiency is that the platform-specific code isn't
    // taking responsibility for doing this...Rebol core is going to be
    // agnostic on how files are translated within the hosts.  So the version
    // of the code on non-wide-char systems will be written just for it, and
    // no intermediate string will need be made.
    //
    REBVAL *string = rebString(local);

    REBVAL *result = Alloc_Pairing(NULL);
    Init_File(
        result,
        To_REBOL_Path(
            VAL_UNI_AT(string),
            VAL_LEN_AT(string),
            is_dir ? PATH_OPT_SRC_IS_DIR : 0
        )
    );
    Init_Blank(PAIRING_KEY(result));

    rebFree(string);
    return result;
}


//
//  rebLocalToFileW: RL_API
//
// This is the API exposure of TO-REBOL-FILE.  It takes in a wchar_t buffer and
// returns a FILE!.
//
// !!! Should MAX_FILE_NAME be taken into account for the OS?
//
REBVAL *RL_rebLocalToFileW(const wchar_t *local, REBOOL is_dir)
{
    Enter_Api_Clear_Last_Error();

#ifdef OS_WIDE_CHAR
    assert(sizeof(REBUNI) == sizeof(wchar_t));
    REBVAL *result = Alloc_Pairing(NULL);
    Init_File(
        result,
        To_REBOL_Path(
            cast(const REBUNI*, local), // C++ demands cast
            Strlen_Uni(cast(const REBUNI*, local)), // C++ demands cast
            is_dir ? PATH_OPT_SRC_IS_DIR : 0
        )
    );
    Init_Blank(PAIRING_KEY(result));
    return result;
#else
    UNUSED(local);
    UNUSED(is_dir);
    fail ("wchar_t support not available on non-Windows (yet)");
#endif
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
