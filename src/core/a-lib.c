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
    const REBOL_HOST_LIB *Host_Lib;
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
// to `rebRelease()` something before an error check than be forced to free it
// on branches that test for an error on the call before the free.  See the
// notes on Windows GetLastError() about how the APIs document whether they
// manipulate the error or not--not all of them clear it rotely.
//
// !!! A better way of doing this kind of thing would probably be to have
// the code generator for the RL_API notice attributes on the APIs in the
// comment blocks, e.g. `// rebBlock: RL_API [#clears-error]` or similar.
// Then the first line of the API would be `ENTER_API(rebBlock)` which
// would run theappropriate code for what was described in the API header.
//
// !!! `return_api_error` is a fairly lame way of setting the error, but
// shows the concept of what needs to be done to obey the convention.  How
// could it return an "errant" result of an unboxing, for instance, if the
// type cannot be unboxed as asked?
//
inline static void Enter_Api_Cant_Error(void) {
    if (PG_last_error == NULL)
        panic ("rebStartup() not called before API call");
}
inline static void Enter_Api_Clear_Last_Error(void) {
    Enter_Api_Cant_Error();
    SET_END(PG_last_error);
}
#define return_api_error(msg) \
    do { \
        assert(IS_END(PG_last_error)); \
        Init_Error(PG_last_error, Error_User(msg)); \
        return NULL; \
    } while (0)


inline static REBVAL *Alloc_Value(void)
{
    REBVAL *pairing = Alloc_Pairing();

    // The long term goal is to have diverse and sophisticated memory
    // management options for API handles...where there is some automatic
    // GC when the attached frame goes away, but also to permit manual
    // management.  The extra information associated with the API value
    // allows the GC to do this, but currently we just set it to a BLANK!
    // in order to indicate manual management is needed.
    //
    // Manual release will always be necessary in some places, such as the
    // console code: since it is top-level its "owning frame" never goes away.
    // It may be that all "serious" code to the API does explicit management,
    // some with the help of a C++ wrapper.
    //
    // In any case, it's not bad to have solid bookkeeping in the code for the
    // time being and panics on shutdown if there's a leak.  But clients of
    // the API will have simpler options also.
    //
    Init_Blank(PAIRING_KEY(pairing)); // the meta-value of the API handle
    return pairing;
}


//
//  Startup_Api: C
//
// RL_API routines may be used by extensions (which are invoked by a fully
// initialized Rebol core) or by normal linkage (such as from within the core
// itself).  A call to rebStartup() won't be needed in the former case.  So
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
    PG_last_error = Alloc_Value();
    SET_END(PG_last_error);

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
    Reb_To_RXT[REB_ERROR] = RXT_ERROR;
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
// Obtain the current Rebol version information.  Takes a byte array to
// hold the version info:
//
//      vers[0]: (input) length of the expected version information
//      vers[1]: version
//      vers[2]: revision
//      vers[3]: update
//      vers[4]: system
//      vers[5]: variation
//
// !!! In the original RL_API, this function was to be called before any other
// initialization to determine version compatiblity with the caller.  With the
// massive changes in Ren-C and the lack of RL_API clients, this check is low
// priority...but something like it will be needed.
//
// This is how it was originally done:
//
//      REBYTE vers[8];
//      vers[0] = 5; // len
//      RL_Version(&vers[0]);
//
//      if (vers[1] != RL_VER || vers[2] != RL_REV)
//          rebPanic ("Incompatible reb-lib DLL");
//
void RL_rebVersion(REBYTE vers[])
{
    if (vers[5] != 5)
        panic ("rebVersion() requires 1 + 5 byte structure");

    vers[1] = REBOL_VER;
    vers[2] = REBOL_REV;
    vers[3] = REBOL_UPD;
    vers[4] = REBOL_SYS;
    vers[5] = REBOL_VAR;
}


//
//  rebStartup: RL_API
//
// This function will allocate and initialize all memory structures used by
// the REBOL interpreter. This is an extensive process that takes time.
//
// `lib` is the host lib table (OS_XXX functions) which Rebol core does not
// take for granted--and assumes a host must provide to operate.  An example
// of this would be that getting the current UTC date and time varies from OS
// to OS, so for the NOW native to be implemented it has to call something
// outside of standard C...e.g. OS_GET_TIME().  So even though NOW is in the
// core, it will be incomplete without having that function supplied.
//
// !!! Increased modularization of the core, and new approaches, are making
// this concept obsolete.  For instance, the NOW native might not even live
// in the core, but be supplied by a "Timer Extension" which is considered to
// be sandboxed and non-core enough that having platform-specific code in it
// is not a problem.  Also, hooks can be supplied in the form of natives that
// are later HIJACK'd by some hosts (see rebPanic() and rebFail()), as a
// way of injecting richer platform-or-scenario-specific code into a more
// limited default host operation.  It is expected that the OS_XXX functions
// will eventually disappear completely.
//
void RL_rebStartup(const void *lib)
{
    if (PG_last_error != NULL)
        panic ("rebStartup() called when it's already started");

    Host_Lib = cast(const REBOL_HOST_LIB*, lib);

    if (Host_Lib->size < HOST_LIB_SIZE)
        panic ("Host-lib wrong size");

    if (((HOST_LIB_VER << 16) + HOST_LIB_SUM) != Host_Lib->ver_sum)
        panic ("Host-lib wrong version/checksum");

    Startup_Core();
}


//
//  rebShutdown: RL_API
//
// Shut down a Rebol interpreter initialized with rebStartup().
//
// The `clean` parameter tells whether you want Rebol to release all of its
// memory accrued since initialization.  If you pass false, then it will
// only do the minimum needed for data integrity (it assumes you are planning
// to exit the process, and hence the OS will automatically reclaim all
// memory/handles/etc.)
//
// For rigor, the debug build *always* runs a "clean" shutdown.
//
void RL_rebShutdown(REBOOL clean)
{
    Enter_Api_Cant_Error();

    // At time of writing, nothing Shutdown_Core() does pertains to
    // committing unfinished data to disk.  So really there is
    // nothing to do in the case of an "unclean" shutdown...yet.

#if !defined(NDEBUG)
    if (clean)
        return; // Only do the work above this line in an unclean shutdown
#else
    UNUSED(clean);
#endif

    Shutdown_Core();
}


//
//  rebBlock: RL_API
//
// !!! The variadic rebBlock() constructor is coming soon, but this is just
// to create an API handle to use with rebRelease() for a quick workaround to
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

    return Init_Block(Alloc_Value(), array);
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
    // rebRelease() the result.
    //
    REBVAL *result = Alloc_Value();
    assert(IS_TRASH_DEBUG(result)); // ok: Do_Va_Core() will set to END

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
    return result; // client's responsibility to rebRelease(), for now
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
    // of the concept.
    //
    return rebDo(rebEval(NAT_VALUE(do)), v, END);
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

    // Giving back a direct pointer to the last error would mean subsequent
    // API calls might error and overwrite it during their inspection.
    // Allocate a new API handle cell.
    //
    return Move_Value(Alloc_Value(), PG_last_error);
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
    Move_Value(ARR_HEAD(result), v);
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
REBVAL *RL_rebVoid(void)
{
    Enter_Api_Clear_Last_Error();
    return Init_Void(Alloc_Value());
}


//
//  rebBlank: RL_API
//
REBVAL *RL_rebBlank(void)
{
    Enter_Api_Clear_Last_Error();
    return Init_Blank(Alloc_Value());
}


//
//  rebLogic: RL_API
//
// !!! Uses libRed convention that it takes a long where 0 is false and all
// other values are true, for the moment.  REBOOL is standardized to only hold
// 0 or 1 inside the core, so taking a foreign REBOOL is risky and would
// require normalization anyway.
//
REBVAL *RL_rebLogic(long logic)
{
    Enter_Api_Clear_Last_Error();
    return Init_Logic(Alloc_Value(), LOGICAL(logic));
}


//
//  rebInteger: RL_API
//
// !!! Should there be rebSigned() and rebUnsigned(), in order to catch cases
// of using out of range values?
//
REBVAL *RL_rebInteger(REBI64 i)
{
    Enter_Api_Clear_Last_Error();
    return Init_Integer(Alloc_Value(), i);
}


//
//  rebDecimal: RL_API
//
REBVAL *RL_rebDecimal(REBDEC dec)
{
    Enter_Api_Clear_Last_Error();
    return Init_Decimal(Alloc_Value(), dec);
}


//
//  rebTimeHMS: RL_API
//
REBVAL *RL_rebTimeHMS(
    unsigned int hour,
    unsigned int minute,
    unsigned int second
){
    Enter_Api_Clear_Last_Error();

    REBVAL *result = Alloc_Value();
    VAL_RESET_HEADER(result, REB_TIME);
    VAL_NANO(result) = SECS_TO_NANO(hour * 3600 + minute * 60 + second);
    return result;
}


//
//  rebTimeNano: RL_API
//
REBVAL *RL_rebTimeNano(long nanoseconds) {
    Enter_Api_Clear_Last_Error();

    REBVAL *result = Alloc_Value();
    VAL_RESET_HEADER(result, REB_TIME);
    VAL_NANO(result) = nanoseconds;
    return result;
}


//
//  rebDateYMD: RL_API
//
REBVAL *RL_rebDateYMD(
    unsigned int year,
    unsigned int month,
    unsigned int day
){
    Enter_Api_Clear_Last_Error();

    REBVAL *result = Alloc_Value();
    VAL_RESET_HEADER(result, REB_DATE); // no time or time zone flags
    VAL_YEAR(result) = year;
    VAL_MONTH(result) = month;
    VAL_DAY(result) = day;
    return result;
}


//
//  rebDateTime: RL_API
//
REBVAL *RL_rebDateTime(const REBVAL *date, const REBVAL *time)
{
    Enter_Api_Clear_Last_Error();

    if (NOT(IS_DATE(date)))
        return_api_error("rebDateTime() date parameter must be DATE!");
    if (NOT(IS_TIME(time)))
        return_api_error("rebDateTime() time parameter must be TIME!");

    // if we had a timezone, we'd need to set DATE_FLAG_HAS_ZONE and
    // then INIT_VAL_ZONE().  But since DATE_FLAG_HAS_ZONE is not set,
    // the timezone bitfield in the date is ignored.

    REBVAL *result = Alloc_Value();
    VAL_RESET_HEADER(result, REB_DATE);
    SET_VAL_FLAG(result, DATE_FLAG_HAS_TIME);
    VAL_YEAR(result) = VAL_YEAR(date);
    VAL_MONTH(result) = VAL_MONTH(date);
    VAL_DAY(result) = VAL_DAY(date);
    VAL_NANO(result) = VAL_NANO(time);
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
//  rebUnboxLogic: RL_API
//
REBOOL RL_rebUnboxLogic(const REBVAL *v) {
    Enter_Api_Clear_Last_Error();

    return VAL_LOGIC(v);
}


//
//  rebUnboxInteger: RL_API
//
long RL_rebUnboxInteger(const REBVAL *v) {
    Enter_Api_Clear_Last_Error();

    return VAL_INT64(v);
}

//
//  rebUnboxDecimal: RL_API
//
REBDEC RL_rebUnboxDecimal(const REBVAL *v) {
    Enter_Api_Clear_Last_Error();

    return VAL_DECIMAL(v);
}

//
//  rebUnboxChar: RL_API
//
REBUNI RL_rebUnboxChar(const REBVAL *v) {
    Enter_Api_Clear_Last_Error();

    return VAL_CHAR(v);
}

//
//  rebNanoOfTime: RL_API
//
long RL_rebNanoOfTime(const REBVAL *v) {
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
//  rebIndexOf: RL_API
//
long RL_rebIndexOf(const REBVAL *v) {
    Enter_Api_Clear_Last_Error();

    return VAL_INDEX(v);
}


//
//  rebInitDate: RL_API
//
// !!! Note this doesn't allow you to say whether the date has a time
// or zone component at all.  Those could be extra flags, or if Rebol values
// were used they could be blanks vs. integers.  Further still, this kind
// of API is probably best kept as calls into Rebol code, e.g.
// RL_Do("make time!", ...); which might not offer the best performance, but
// the internal API is available for clients who need that performance,
// who can call date initialization themselves.
//
REBVAL *RL_rebInitDate(
    int year,
    int month,
    int day,
    int seconds,
    int nano,
    int zone
){
    Enter_Api_Clear_Last_Error();

    REBVAL *result = Alloc_Value();
    VAL_RESET_HEADER(result, REB_DATE);
    VAL_YEAR(result) = year;
    VAL_MONTH(result) = month;
    VAL_DAY(result) = day;

    SET_VAL_FLAG(result, DATE_FLAG_HAS_ZONE);
    INIT_VAL_ZONE(result, zone / ZONE_MINS);

    SET_VAL_FLAG(result, DATE_FLAG_HAS_TIME);
    VAL_NANO(result) = SECS_TO_NANO(seconds) + nano;
    return result;
}


//
//  rebSpellingOf: RL_API
//
// Extract UTF-8 data from an ANY-STRING! or ANY-WORD!.
//
// API does not return the number of UTF-8 characters for a value, because
// the answer to that is always cached for any value position as LENGTH OF.
// The more immediate quantity of concern to return is the number of bytes.
//
size_t RL_rebSpellingOf(
    char *buf,
    size_t buf_size, // number of bytes
    const REBVAL *v
){
    Enter_Api_Clear_Last_Error();

    const char *utf8;
    REBSIZ utf8_size;
    if (ANY_STRING(v)) {
        REBSIZ offset;
        REBSER *temp = Temp_UTF8_At_Managed(
            &offset, &utf8_size, v, VAL_LEN_AT(v)
        );
        utf8 = cs_cast(BIN_AT(temp, offset));
    }
    else {
        assert(ANY_WORD(v));

        REBSTR *spelling = VAL_WORD_SPELLING(v);
        utf8 = STR_HEAD(spelling);
        utf8_size = STR_SIZE(spelling);
    }

    if (buf == NULL) {
        assert(buf_size == 0);
        return utf8_size; // caller must allocate a buffer of size + 1
    }

    REBSIZ limit = MIN(buf_size, utf8_size);
    memcpy(buf, utf8, limit);
    buf[limit] = '\0';
    return utf8_size;
}


//
//  rebSpellingOfAlloc: RL_API
//
char *RL_rebSpellingOfAlloc(size_t *size_out, const REBVAL *v)
{
    Enter_Api_Clear_Last_Error();

    size_t buf_size = rebSpellingOf(NULL, 0, v);
    char *buf = OS_ALLOC_N(char, buf_size + 1); // must add space for term
    rebSpellingOf(buf, buf_size, v);
    if (size_out != NULL)
        *size_out = buf_size;
    return buf;
}


//
//  rebSpellingOfW: RL_API
//
// Extract UCS2 data from an ANY-STRING! or ANY-WORD!
//
// !!! Although the rebSpellingOf API deals in bytes, this deals in count of
// characters.  (The use of REBCNT instead of size_t indicates this.)  It may
// be more useful for the wide string APIs to do this so leaving it that way
// for now.
//
REBCNT RL_rebSpellingOfW(
    REBWCHAR *buf, // see notes in %reb-c.h on REBWCHAR
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

        REBSTR *spelling = VAL_WORD_SPELLING(v);
        s = Make_Sized_String_UTF8(STR_HEAD(spelling), STR_SIZE(spelling));
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
REBWCHAR *RL_rebSpellingOfAllocW(REBCNT *len_out, const REBVAL *v)
{
    Enter_Api_Clear_Last_Error();

    REBCNT len = rebSpellingOfW(NULL, 0, v);
    REBWCHAR *result = OS_ALLOC_N(REBWCHAR, len + 1);
    rebSpellingOfW(result, len, v);
    if (len_out != NULL)
        *len_out = len;
    return result;
}


//
//  rebBytesOfBinary: RL_API
//
// Extract binary data from a BINARY!
//
REBCNT RL_rebBytesOfBinary(
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
//  rebBytesOfBinaryAlloc: RL_API
//
REBYTE *RL_rebBytesOfBinaryAlloc(REBCNT *len_out, const REBVAL *binary)
{
    Enter_Api_Clear_Last_Error();

    REBCNT len = rebBytesOfBinary(NULL, 0, binary);
    REBYTE *result = OS_ALLOC_N(REBYTE, len + 1);
    rebBytesOfBinary(result, len, binary);
    if (len_out != NULL)
        *len_out = len;
    return result;
}


//
//  rebBinary: RL_API
//
REBVAL *RL_rebBinary(const void *bytes, size_t size)
{
    Enter_Api_Clear_Last_Error();

    REBSER *bin = Make_Binary(size);
    memcpy(BIN_HEAD(bin), bytes, size);
    TERM_BIN_LEN(bin, size);

    return Init_Binary(Alloc_Value(), bin);
}


//
//  rebSizedString: RL_API
//
// If utf8 does not contain valid UTF-8 data, this may fail().
//
REBVAL *RL_rebSizedString(const char *utf8, size_t size)
{
    Enter_Api_Clear_Last_Error();

    return Init_String(Alloc_Value(), Make_Sized_String_UTF8(utf8, size));
}


//
//  rebString: RL_API
//
REBVAL *RL_rebString(const char *utf8)
{
    // Handles Enter_Api
    return rebSizedString(utf8, strsize(utf8));
}


//
//  rebFile: RL_API
//
REBVAL *RL_rebFile(const char *utf8)
{
    Enter_Api_Clear_Last_Error();

    REBVAL *result = rebString(utf8);
    VAL_RESET_HEADER(result, REB_FILE);
    return result;
}


//
//  rebSizedStringW: RL_API
//
REBVAL *RL_rebSizedStringW(const REBWCHAR *wstr, REBCNT len)
{
    Enter_Api_Clear_Last_Error();

    DECLARE_MOLD (mo);
    Push_Mold(mo);

    if (len == UNKNOWN) {
        for (; *wstr != 0; ++wstr)
            Append_Utf8_Codepoint(mo->series, *wstr);
    }
    else {
        for (; len != 0; --len, ++wstr)
            Append_Utf8_Codepoint(mo->series, *wstr);
    }

    return Init_String(Alloc_Value(), Pop_Molded_String(mo));
}


//
//  rebStringW: RL_API
//
REBVAL *RL_rebStringW(const REBWCHAR *wstr)
{
    return rebSizedStringW(wstr, UNKNOWN);
}


//
//  rebFileW: RL_API
//
REBVAL *RL_rebFileW(const REBWCHAR *wstr)
{
    REBVAL *result = rebStringW(wstr);
    VAL_RESET_HEADER(result, REB_FILE);
    return result;
}


//
//  rebSizedWordW: RL_API
//
// !!! Currently needed by ODBC module to make column titles.
//
REBVAL *RL_rebSizedWordW(const REBWCHAR *ucs2, REBCNT len)
{
    Enter_Api_Clear_Last_Error();

    DECLARE_MOLD (mo);
    Push_Mold(mo);

    if (len == UNKNOWN) {
        for (; *ucs2 != 0; ++ucs2)
            Append_Utf8_Codepoint(mo->series, *ucs2);
    }
    else {
        for (; len != 0; --len, ++ucs2)
            Append_Utf8_Codepoint(mo->series, *ucs2);
    }

    REBSER *bin = Pop_Molded_UTF8(mo);
    REBSTR *spelling = Intern_UTF8_Managed(BIN_HEAD(bin), BIN_LEN(bin));

    return Init_Word(Alloc_Value(), spelling);
}


//
//  rebError: RL_API
//
REBVAL *RL_rebError(const char *msg)
{
    Enter_Api_Clear_Last_Error();
    return Init_Error(Alloc_Value(), Error_User(msg));;
}


//
//  rebCopyExtra: RL_API
//
REBVAL *RL_rebCopyExtra(const REBVAL *v, REBCNT extra)
{
    Enter_Api_Clear_Last_Error();

    // !!! It's actually a little bit harder than one might think to hook
    // into the COPY code without actually calling the function via the
    // evaluator, because it is an "action".  Review a good efficient method
    // for doing it, but for the moment it's just needed for FILE! so do that.
    //
    if (NOT(ANY_STRING(v)))
        return_api_error ("rebCopy() only supports ANY-STRING! for now");

    return Init_Any_Series(
        Alloc_Value(),
        VAL_TYPE(v),
        Copy_Sequence_At_Len_Extra(
            VAL_SERIES(v),
            VAL_INDEX(v),
            VAL_LEN_AT(v),
            extra
        )
    );
}


//
//  rebAppend: RL_API
//
void RL_rebAppend(REBVAL *series, const REBVAL *value)
{
    Enter_Api_Clear_Last_Error();

    // !!! There is no ACT_VALUE() like there is NAT_VALUE() for accessing
    // the actual natives.  So we'd need to add that, bind via a potentially
    // flaky word or string, or find some other way to do it.  This only
    // exists for a very narrow purpose at time of writing...the appending
    // of strings to each other, so that client code isn't calling UNI_HEAD()
    // and looking at string series internals.  So temporarily hack this
    // in as a call to the internal routine for string appending.
    //
    if (NOT(ANY_STRING(series)) || NOT(ANY_STRING(value)))
        fail ("rebAppend() very temporarily only supports strings");

    REBCNT index = Modify_String(
        series,
        SYM_APPEND,
        value,
        0, // no flags
        0, // no partial append
        0 // no dups
    );

    // !!! The invariant for APPEND is to return the value at its head, but
    // this would mean producing a new API value that would have to be worried
    // about...most API-wise APPENDs probably don't want to use the result.
    //
    UNUSED(index);
}


//
//  rebLengthOf: RL_API
//
long RL_rebLengthOf(const REBVAL *series)
{
    Enter_Api_Clear_Last_Error();

    if (NOT(ANY_SERIES(series)))
        panic ("rebLengthOf() can only be used on ANY-SERIES!");

    return VAL_LEN_AT(series);
}


//
//  rebPickIndexed: RL_API
//
// Will return void if past the series length (GET-PATH! semantics)
//
REBVAL *RL_rebPickIndexed(const REBVAL *series, long index)
{
    Enter_Api_Clear_Last_Error();

    if (NOT(ANY_SERIES(series)))
        return_api_error (
            "rebPickIndexed() requires series argument to be ANY-SERIES!"
        );

    DECLARE_LOCAL (picker);
    REBARR *array = Make_Array(2);
    Append_Value(array, series);
    Append_Value(array, Init_Integer(picker, index));

    DECLARE_LOCAL (get_path);
    return rebDo(
        rebEval(Init_Any_Array(get_path, REB_GET_PATH, array)),
        END
    );
}


//
//  rebManage: RL_API
//
// The "friendliest" default for the API is to assume you want handles to be
// tied to the lifetime of the frame they're in.  Long-running top-level
// processes like the C code running the console would eventually exhaust
// memory if that were the case...so there should be some options for metrics
// as a form of "leak detection" even so.
//
// While the API is being developed and used in core code, the default is to
// be "unfriendly"...and you have to explicitly ask for management.
//
REBVAL *RL_rebManage(REBVAL *v)
{
    Enter_Api_Clear_Last_Error();

    REBVAL *key = PAIRING_KEY(v);
    assert(NOT(key->header.bits & NODE_FLAG_ROOT));
    UNUSED(key);

    if (FS_TOP == NULL)
        fail ("rebManage() temporarily disallowed in top level C for safety");

    Init_Pairing_Key_Owner(v, FS_TOP);
    return v;

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
//  rebRelease: RL_API
//
// An API handle is only 4 platform pointers in size (plus some bookkeeping),
// but it still takes up some storage.  The intended default for API handles
// is that they live as long as the function frame they belong to, but there
// will be several lifetime management tricks to ease releasing them.
//
// !!! For the time being, we lean heavily on explicit release.  Near term
// leak avoidance will need to at least allow for GC of handles across errors
// for their associated frames.
//
void RL_rebRelease(REBVAL *v)
{
    Enter_Api_Cant_Error();

    // !!! This check isn't really ideal.  There should be a foolproof way
    // to tell whether a REBVAL* is an API handle or not, so that series can't
    // be corrupted.  It's possible, just needs to be done...this covers 99%
    // of the cases for right now.
    //
    REBVAL *key = PAIRING_KEY(v);
    assert(NOT(key->header.bits & NODE_FLAG_MANAGED));
    if (NOT(IS_BLANK(key)) && NOT(IS_FRAME(key)))
        panic ("Attempt to rebRelease() a non-API handle");

    Free_Pairing(v);
}


//
//  rebFail: RL_API [
//      #noreturn
//  ]
//
// The rebFail() API actually chains through to the FAIL native itself, which
// means it can be passed errors or blocks.  It understands the case of
// being passed a UTF-8 string from code as well.
//
// !!! This probably needs a /WHERE variant, as well as some way for the
// caller to slip their C file and line information through.
//
void RL_rebFail(const void *p)
{
    Enter_Api_Cant_Error();

    // !!! It's not great to encourage an unnamed error, but then again it's
    // better to have all unnamed errors be easy to find.  By convention that
    // is done with `rebFail (NULL)`.

    if (p == NULL)
        p = "Generic Failure -- rebFail () called with NULL as parameter";

    // !!! Should there be a special bit or dispatcher used on the FAIL to
    // ensure it does not continue running?  If it were a dispatcher then
    // HIJACK would have to be aware of it and preserve it.  This is something
    // like ATTRIBUTE_NO_RETURN in C, which could be enforced.

    const void *p2 = p; // keep original p for examining in the debugger

    DECLARE_LOCAL (reason);

    switch (Detect_Rebol_Pointer(p)) {
    case DETECTED_AS_UTF8: {
        REBVAL *temp = rebString(cast(const char*, p2));
        Move_Value(reason, temp);
        rebRelease(temp); // would leak an API handle if we passed directly
        p2 = reason;
        goto fail_on_value; }

    case DETECTED_AS_SERIES:
    case DETECTED_AS_FREED_SERIES:
        //
        // !!! Is there any valid reason for this?
        //
        break;

    case DETECTED_AS_VALUE: {
        //
        // Passed in pointer is const because it may be a char*, but if it's
        // an API handle we currently need to free it...mutability cast :-/
        //
        {
        REBVAL *arg = m_cast(REBVAL*, cast(const REBVAL *, p));
        Move_Value(reason, arg);
        rebRelease(arg);
        p2 = reason;
        }

    fail_on_value:;
        REBVAL *result = rebDo(
            rebEval(NAT_VALUE(fail)),
            cast(const REBVAL*, p2),
            END
        );
        if (result == NULL) { // we *expect* it to fail (we called FAIL!)
            REBVAL *temp = rebLastError();
            DECLARE_LOCAL (e);
            Move_Value(e, temp);
            rebRelease(temp);
            fail (e); // bounce the failure
        }
        p2 = "HIJACK'd FAIL function did not interrupt execution"; // uh oh
        rebRelease(result);
        break; }

    case DETECTED_AS_END:
    case DETECTED_AS_TRASH_CELL:
        break;
    };

    rebPanic (p2); // just PANIC, as the fallthrough situations are all bad
}


//
//  rebPanic: RL_API [
//      #noreturn
//  ]
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
            rebEval(NAT_VALUE(panic)),
            rebString(cast(const char*, p)), // leaks (but we're crashing)
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
char *RL_rebFileToLocalAlloc(size_t *size_out, const REBVAL *file, REBOOL full)
{
    Enter_Api_Clear_Last_Error();

    if (NOT(IS_FILE(file)))
        return_api_error ("rebFileToLocalAlloc() only works on FILE!");

    DECLARE_LOCAL (local);
    return rebSpellingOfAlloc(
        size_out,
        Init_String(local, To_Local_Path(file, full))
    );
}


//
//  rebFileToLocalAllocW: RL_API
//
// This is the API exposure of TO-LOCAL-FILE.  It takes in a FILE! and
// returns an allocated UCS2 buffer.
//
// !!! Should MAX_FILE_NAME be taken into account for the OS?
//
REBWCHAR *RL_rebFileToLocalAllocW(
    REBCNT *len_out,
    const REBVAL *file,
    REBOOL full
){
    Enter_Api_Clear_Last_Error();

    if (NOT(IS_FILE(file)))
        return_api_error ("rebFileToLocalAllocW() only works on FILE!");

    DECLARE_LOCAL (local);
    return rebSpellingOfAllocW(
        len_out,
        Init_String(local, To_Local_Path(file, full))
    );
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

    REBVAL *result = Init_File(
        Alloc_Value(),
        To_REBOL_Path(string, is_dir ? PATH_OPT_SRC_IS_DIR : 0)
    );

    rebRelease(string);
    return result;
}


//
//  rebLocalToFileW: RL_API
//
// This is the API exposure of TO-REBOL-FILE.  It takes in a UCS2 buffer and
// returns a FILE!.
//
// !!! Should MAX_FILE_NAME be taken into account for the OS?
//
REBVAL *RL_rebLocalToFileW(const REBWCHAR *local, REBOOL is_dir)
{
    Enter_Api_Clear_Last_Error();

    assert(sizeof(REBUNI) == sizeof(REBWCHAR));

    REBVAL *string = rebStringW(local);

    REBVAL *result = Init_File(
        Alloc_Value(),
        To_REBOL_Path(
            string,
            is_dir ? PATH_OPT_SRC_IS_DIR : 0
        )
    );

    rebRelease(string);
    return result;
}


//
//  rebTrapHackOS: RL_API
//
// !!! The OS_Do_Device function is dependent on %reb-host.h, not %sys-core.h
// which means it can't call PUSH_UNHALTABLE_TRAP.  However, it wishes to
// dispatch to arbitrary code which may trigger a failure, and it wants to
// do some cleanup if it does fail.  Switching to include %sys-core.h causes
// linker headaches that are not immediately obvious how to sort out, so the
// interim trick to give it the trapping mechanism is to add a hook to the
// RL_API it already has and uses.  It's not terribly elegant but works for
// now, as the mechanism is rethought.
//
REBVAL *RL_rebTrapHackOS(DEVICE_CMD_FUNC cmd, REBREQ *req)
{
    struct Reb_State state;
    REBCTX *error;

    PUSH_UNHALTABLE_TRAP(&error, &state); // must catch HALTs

// The first time through the following code 'error' will be NULL, but...
// `fail` can longjmp here, so 'error' won't be NULL *if* that happens!

    if (error != NULL)
        return Init_Error(Alloc_Value(), error);

    int result = cmd(req);

    DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

    return rebInteger(result);
}


// !!! Although it is very much the goal to get all OS-specific code out of
// the core (including the API), this particular hook is extremely useful to
// have available to all clients.  It might be done another way (e.g. by
// having hosts HIJACK the FAIL native with an adaptation that processes
// integer arguments).  But for now, stick it in the API just to get the
// wide availability.
//
#ifdef TO_WINDOWS
    #undef IS_ERROR // windows has its own meaning for this.
    #include <windows.h>
#else
    #include <errno.h>
    #define MAX_POSIX_ERROR_LEN 1024
#endif

//
//  rebFail_OS: RL_API [
//      #noreturn
//  ]
//
// Produce an error from an OS error code, by asking the OS for textual
// information it knows internally from its database of error strings.
//
// This function is called via a macro which adds DEAD_END; after it.
//
// Note that error codes coming from WSAGetLastError are the same as codes
// coming from GetLastError in 32-bit and above Windows:
//
// https://stackoverflow.com/q/15586224/
//
// !!! Should not be in core, but extensions need a way to trigger the
// common functionality one way or another.
//
void RL_rebFail_OS(int errnum)
{
    REBCTX *error;

#ifdef TO_WINDOWS
    if (errnum == 0)
        errnum = GetLastError();

    WCHAR *lpMsgBuf; // FormatMessage writes allocated buffer address here

    // Specific errors have %1 %2 slots, and if you know the error ID and
    // that it's one of those then this lets you pass arguments to fill
    // those in.  But since this is a generic error, we have no more
    // parameterization (hence FORMAT_MESSAGE_IGNORE_INSERTS)
    //
    va_list *Arguments = NULL;

    // Apparently FormatMessage can find its error strings in a variety of
    // DLLs, but we don't have any context here so just use the default.
    //
    LPCVOID lpSource = NULL;

    DWORD ok = FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER // see lpMsgBuf
            | FORMAT_MESSAGE_FROM_SYSTEM // e.g. ignore lpSource
            | FORMAT_MESSAGE_IGNORE_INSERTS, // see Arguments
        lpSource,
        errnum, // message identifier
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // default language
        cast(WCHAR*, &lpMsgBuf), // allocated buffer address written here
        0, // buffer size (not used since FORMAT_MESSAGE_ALLOCATE_BUFFER)
        Arguments
    );

    if (ok == 0) {
        //
        // Might want to show the value of GetLastError() in this message,
        // but trying to FormatMessage() on *that* would be excessive.
        //
        error = Error_User("FormatMessage() gave no error description");
    }
    else {
        REBVAL *temp = rebStringW(lpMsgBuf);
        LocalFree(lpMsgBuf);

        DECLARE_LOCAL (message);
        Move_Value(message, temp);
        rebRelease(temp);

        error = Error(RE_USER, message, END);
    }
#else
    // strerror() is not thread-safe, but strerror_r is. Unfortunately, at
    // least in glibc, there are two different protocols for strerror_r(),
    // depending on whether you are using the POSIX-compliant implementation
    // or the GNU implementation.
    //
    // The convoluted test below is the inversion of the actual test glibc
    // suggests to discern the version of strerror_r() provided. As other,
    // non-glibc implementations (such as OS X's libSystem) also provide the
    // POSIX-compliant version, we invert the test: explicitly use the
    // older GNU implementation when we are sure about it, and use the
    // more modern POSIX-compliant version otherwise. Finally, we only
    // attempt this feature detection when using glibc (__GNU_LIBRARY__),
    // as this particular combination of the (more widely standardised)
    // _POSIX_C_SOURCE and _XOPEN_SOURCE defines might mean something
    // completely different on non-glibc implementations.
    //
    // (Note that undefined pre-processor names arithmetically compare as 0,
    // which is used in the original glibc test; we are more explicit.)

    #ifdef USE_STRERROR_NOT_STRERROR_R
        char *shared = strerror(errnum);
        error = Error_User(shared);
    #elif defined(__GNU_LIBRARY__) \
            && (defined(_GNU_SOURCE) \
                || ((!defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200112L) \
                    && (!defined(_XOPEN_SOURCE) || _XOPEN_SOURCE < 600)))

        // May return an immutable string instead of filling the buffer

        char buffer[MAX_POSIX_ERROR_LEN];
        char *maybe_str = strerror_r(errnum, buffer, MAX_POSIX_ERROR_LEN);
        if (maybe_str != buffer)
            strncpy(buffer, maybe_str, MAX_POSIX_ERROR_LEN);
        error = Error_User(buffer);
    #else
        // Quoting glibc's strerror_r manpage: "The XSI-compliant strerror_r()
        // function returns 0 on success. On error, a (positive) error number
        // is returned (since glibc 2.13), or -1 is returned and errno is set
        // to indicate the error (glibc versions before 2.13)."

        char buffer[MAX_POSIX_ERROR_LEN];
        int result = strerror_r(errnum, buffer, MAX_POSIX_ERROR_LEN);

        // Alert us to any problems in a debug build.
        assert(result == 0);

        if (result == 0)
            error = Error_User(buffer);
        else if (result == EINVAL)
            error = Error_User("EINVAL: bad errno passed to strerror_r()");
        else if (result == ERANGE)
            error = Error_User("ERANGE: insufficient buffer size for error");
        else
            error = Error_User("Unknown problem with strerror_r() message");
    #endif
#endif

    rebFail (Init_Error(Alloc_Value(), error));
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
