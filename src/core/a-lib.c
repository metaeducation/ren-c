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
// just opaque pointers to cells whose lifetime is either indefinite, or
// tied to particular function FRAME!s.)
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
#include "mem-series.h" // needed for SER_SET_BIAS in rebRepossess()


// "Linkage back to HOST functions. Needed when we compile as a DLL
// in order to use the OS_* macro functions."
//
#ifdef REB_API  // Included by C command line
    const REBOL_HOST_LIB *Host_Lib = NULL;
    EXTERN_C REBOL_HOST_LIB Host_Lib_Init;
#endif


// !!! Review how much checking one wants to do when calling API routines,
// and what the balance should be of debug vs. release.  Right now, this helps
// in particular notice if the core tries to use an API function before the
// proper moment in the boot.
//
inline static void Enter_Api(void) {
    if (Host_Lib == NULL)
        panic ("rebStartup() not called before API call");
}



//=//// SERIES-BACKED ALLOCATORS //////////////////////////////////////////=//
//
// These are replacements for malloc(), realloc(), and free() which use a
// byte-sized REBSER as the backing store for the data.
//
// One benefit of using a series is that it offers more options for automatic
// memory management (such as being freed in case of a fail(), vs. leaked as
// a malloc() would, or perhaps being GC'd when a particular FRAME! ends).
//
// It also has the benefit of helping interface with client code that has
// been stylized to use malloc()-ish hooks to produce data, when the eventual
// target of that data is a Rebol series.  It does this without exposing
// REBSER* internals to the external API, by allowing one to "rebRepossess()"
// the underlying series as a BINARY! REBVAL*.
//


//
//  rebMalloc: RL_API
//
// * Unlike plain malloc(), this will fail() instead of return NULL if an
//   allocation cannot be fulfilled.
//
// * Like plain malloc(), if size is zero, the implementation just has to
//   return something that free() will take.  A backing series is added in
//   this case vs. returning NULL, in order to avoid NULL handling in other
//   routines (e.g. rebRepossess() or handle lifetime control functions).
//
// * Because of the above points, NULL is *never* returned.
//
// * It tries to be like malloc() by giving back a pointer "suitably aligned
//   for the size of any fundamental type".  See notes on ALIGN_SIZE.
//
// !!! rebAlignedMalloc() could exist to take an alignment, which could save
// on wasted bytes when ALIGN_SIZE > sizeof(REBSER*)...or work with "weird"
// large fundamental types that need more alignment than ALIGN_SIZE.
//
void *RL_rebMalloc(size_t size)
{
    Enter_Api();

    REBSER *s = Make_Series_Core(
        ALIGN_SIZE // stores REBSER* (must be at least big enough for void*)
            + size // for the actual data capacity (may be 0...see notes)
            + 1, // for termination (even BINARY! has this, review necessity)
        sizeof(REBYTE), // rebRepossess() only creates binary series ATM
        SERIES_FLAG_DONT_RELOCATE // direct data pointer is being handed back!
    );

    REBYTE *ptr = BIN_HEAD(s) + ALIGN_SIZE;

    REBSER **ps = (cast(REBSER**, ptr) - 1);
    *ps = s; // save self in bytes *right before* data
    POISON_MEMORY(ps, sizeof(REBSER*)); // let ASAN catch underruns

    // !!! The data is uninitialized, and if it is turned into a BINARY! via
    // rebRepossess() before all bytes are assigned initialized, it could be
    // worse than just random data...MOLDing such a binary and reading those
    // bytes could be bad (due to, for instance, "trap representations"):
    //
    // https://stackoverflow.com/a/37184840
    //
    // It may be that rebMalloc() and rebRealloc() should initialize with 0
    // in the release build to defend against that, but doing so in the debug
    // build would keep address sanitizer from noticing when memory was not
    // initialized.
    //
    TERM_BIN_LEN(s, ALIGN_SIZE + size);

    return ptr;
}


//
//  rebRealloc: RL_API
//
// * Like plain realloc(), NULL is legal for ptr (despite the fact that
//   rebMalloc() never returns NULL, this can still be useful)
//
// * Like plain realloc(), it preserves the lesser of the old data range or
//   the new data range, and memory usage drops if new_size is smaller:
//
// https://stackoverflow.com/a/9575348
//
// * Unlike plain realloc() (but like rebMalloc()), this fails instead of
//   returning NULL, hence it is safe to say `ptr = rebRealloc(ptr, new_size)`
//
// * A 0 size is considered illegal.  This is consistent with the C11 standard
//   for realloc(), but not with malloc() or rebMalloc()...which allow it.
//
void *RL_rebRealloc(void *ptr, size_t new_size)
{
    Enter_Api();

    assert(new_size > 0); // realloc() deprecated this as of C11 DR 400

    if (ptr == NULL) // C realloc() accepts NULL
        return rebMalloc(new_size);

    REBSER **ps = cast(REBSER**, ptr) - 1;
    UNPOISON_MEMORY(ps, sizeof(REBSER*)); // need to underrun to fetch `s`

    REBSER *s = *ps;

    REBCNT old_size = BIN_LEN(s) - ALIGN_SIZE;

    // !!! It's less efficient to create a new series with another call to
    // rebMalloc(), but simpler for the time being.  Switch to do this with
    // the same series node.
    //
    void *reallocated = rebMalloc(new_size);
    memcpy(reallocated, ptr, old_size < new_size ? old_size : new_size);
    Free_Series(s); // asserts that `s` is unmanaged

    return reallocated;
}


//
//  rebFree: RL_API
//
// * As with free(), NULL is accepted as a no-op.
//
void RL_rebFree(void *ptr)
{
    Enter_Api();

    if (ptr == NULL)
        return;

    REBSER **ps = cast(REBSER**, ptr) - 1;
    UNPOISON_MEMORY(ps, sizeof(REBSER*)); // need to underrun to fetch `s`

    REBSER *s = *ps;
    assert(BYTE_SIZE(s));

    Free_Series(s); // asserts that `s` is unmanaged
}


//
//  rebRepossess: RL_API
//
// Alternative to rebFree() is to take over the underlying series as a
// BINARY!.  The old void* should not be used after the transition, as this
// operation makes the series underlying the memory subject to relocation.
//
// If the passed in size is less than the size with which the series was
// allocated, the overage will be treated as unused series capacity.
//
// !!! All bytes in the allocation are expected to be initialized by this
// point, as failure to do so will mean reads crash the interpreter.  See
// remarks in rebMalloc() about the issue, and possibly doing zero fills.
//
// !!! It might seem tempting to use (BIN_LEN(s) - ALIGN_SIZE).  However,
// some routines make allocations bigger than they ultimately need and do not
// realloc() before converting the memory to a series...rebInflate() and
// rebDeflate() do this.  So a version passing the size will be necessary,
// and since C does not have the size exposed in malloc() and you track it
// yourself, it seems fair to *always* ask the caller to pass in a size.
//
REBVAL *RL_rebRepossess(void *ptr, REBCNT size)
{
    Enter_Api();

    REBSER **ps = cast(REBSER**, ptr) - 1;
    UNPOISON_MEMORY(ps, sizeof(REBSER*)); // need to underrun to fetch `s`

    REBSER *s = *ps;
    assert(not IS_SERIES_MANAGED(s));

    if (size > BIN_LEN(s) - ALIGN_SIZE)
        fail ("Attempt to rebRepossess() more than rebMalloc() capacity");

    assert(GET_SER_FLAG(s, SERIES_FLAG_DONT_RELOCATE));
    CLEAR_SER_FLAG(s, SERIES_FLAG_DONT_RELOCATE);

    if (GET_SER_INFO(s, SERIES_INFO_HAS_DYNAMIC)) {
        //
        // Dynamic series have the concept of a "bias", which is unused
        // allocated capacity at the head of a series.  Bump the "bias" to
        // treat the embedded REBSER* (aligned to REBI64) as unused capacity.
        //
        SER_SET_BIAS(s, ALIGN_SIZE);
        s->content.dynamic.data += ALIGN_SIZE;
        s->content.dynamic.rest -= ALIGN_SIZE;
    }
    else {
        // Data is in REBSER node itself, no bias.  Just slide the bytes down.
        //
        memmove( // src overlaps destination, can't use memcpy()
            BIN_HEAD(s),
            BIN_HEAD(s) + ALIGN_SIZE,
            size
        );
    }

    TERM_BIN_LEN(s, size);
    return Init_Binary(Alloc_Value(), s);
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
}


//
//  Shutdown_Api: C
//
// See remarks on Startup_Api() for the difference between this idea and
// rebShutdown.
//
void Shutdown_Api(void)
{
    assert(Host_Lib != NULL);
    Host_Lib = NULL;
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
//      if (vers[1] != RL_VER or vers[2] != RL_REV)
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
    if (Host_Lib != NULL)
        panic ("rebStartup() called when it's already started");

    Host_Lib = cast(const REBOL_HOST_LIB*, lib);

    if (Host_Lib->size < HOST_LIB_SIZE)
        panic ("Host-lib wrong size");

    if (((HOST_LIB_VER << 16) + HOST_LIB_SUM) != Host_Lib->ver_sum)
        panic ("Host-lib wrong version/checksum");

    Startup_Core();
}


//
//  rebInit: RL_API
//
// Initialize the REBOL interpreter with Host_Lib_Init
//
void RL_rebInit(void)
{
    rebStartup(&Host_Lib_Init);
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
    Enter_Api();

    // At time of writing, nothing Shutdown_Core() does pertains to
    // committing unfinished data to disk.  So really there is
    // nothing to do in the case of an "unclean" shutdown...yet.

  #if !defined(NDEBUG)
    if (not clean)
        return; // Only do the work above this line in an unclean shutdown
  #else
    UNUSED(clean);
  #endif

    // Run a clean shutdown anyway in debug builds--even if the
    // caller didn't need it--to see if it triggers any alerts.
    //
    Shutdown_Core();
}


// !!! This is a helper routine for producing arrays from a va_list.  It has
// a test of putting "UNEVAL" instructions before each spliced item, in order
// to prevent automatic evaluation.  This can be used by routines like print
// so that this would not try to run LABEL:
//
//     REBVAL *label = rebWord("label");
//     rebPrint("{The label is}", label, END);
//
// Inserting extra words is not how this would be done long term.  But the
// concept being reviewed is that top-level entities to some functions passed
// to va_list be "inert" by default.  It's difficult to implement in a
// consistent fashion because the moment one crosses into a nested BLOCK!,
// there is nowhere to store the "unevaluated" bit--since it is not a generic
// value flag that should be leaked.  For now, it's a test of the question of
// if some routines...like rebRun() and rebPrint()...would not handle splices
// as evaluative:
//
// https://forum.rebol.info/t/371
//
static REBARR* Array_From_Vaptr_Maybe_Null(
    const void *p,
    va_list* vaptr,
    REBOOL uneval_hack
){
    REBDSP dsp_orig = DSP;

    enum Reb_Pointer_Detect detect;

    while ((detect = Detect_Rebol_Pointer(p)) != DETECTED_AS_END) {
        if (p == NULL)
            fail ("use END to terminate rebPrint(), not NULL");

        switch (detect) {
        case DETECTED_AS_UTF8: {
            const REBYTE *utf8 = cast(const REBYTE*, p);
            const REBLIN start_line = 1;
            REBCNT size = LEN_BYTES(utf8);

            SCAN_STATE ss;
            Init_Scan_State(&ss, Intern("rebPrint()"), start_line, utf8, size);
            Scan_To_Stack(&ss);
            break; }

        case DETECTED_AS_SERIES:
            fail ("no complex instructions in rebPrint() yet");

        case DETECTED_AS_FREED_SERIES:
            panic (p);

        case DETECTED_AS_VALUE: {
            if (uneval_hack) {
                //
                // !!! By convention, these are supposed to be "spliced", and
                // not evaluated.  Unfortunately, we aren't really using the
                // variadic machinery here yet, and it's illegal to put
                // VALUE_FLAG_EVAL_FLIP in blocks.  Cheat by inserting UNEVAL.
                //
                DS_PUSH_TRASH;
                Init_Word(DS_TOP, Intern("uneval"));
            }

            DS_PUSH(cast(const REBVAL*, p));

            break; }

        case DETECTED_AS_END:
            assert(FALSE); // checked by while loop
            break;

        case DETECTED_AS_TRASH_CELL:
            panic (p);
        }

        p = va_arg(*vaptr, const void*);
    }

    REBARR *a = Pop_Stack_Values_Core(dsp_orig, NODE_FLAG_MANAGED);
    return a;
}


//
//  rebBlock: RL_API
//
// This constructs a block variadically from its arguments, which can be runs
// of UTF-8 data or REBVAL*.
//
// !!! Currently this does no binding of the data; hence any UTF-8 parts will
// be completely unbound, and any spliced values will keep their bindings.
//
REBVAL *RL_rebBlock(const void *p, ...) {
    va_list va;
    va_start(va, p);

    const REBOOL uneval_hack = FALSE;
    REBARR *a = Array_From_Vaptr_Maybe_Null(p, &va, uneval_hack);

    va_end(va);

    if (a == NULL)
        return NULL;

    return Init_Block(Alloc_Value(), a);
}


//
//  rebRun: RL_API
//
// C variadic function which calls the evaluator on multiple pointers.
// Each pointer may either be a REBVAL* or a UTF-8 string which will be
// scanned to reflect one or more values in the sequence.
//
// All REBVAL* are spliced in inert by default, as if they were an evaluative
// product already.  Use rebEval() to "retrigger" them (which wraps them in
// a singular REBARR*, another type of detectable pointer.)
//
REBVAL *RL_rebRun(const void *p, ...)
{
    Enter_Api();

    va_list va;
    va_start(va, p);

    DECLARE_LOCAL (temp); // so a fail() won't leak a handle...
    REBIXO indexor = Do_Va_Core(
        temp,
        p, // opt_first (preloads value)
        &va,
        DO_FLAG_EXPLICIT_EVALUATE | DO_FLAG_TO_END
    );
    va_end(va);

    if (indexor == THROWN_FLAG)
        fail (Error_No_Catch_For_Throw(temp));

    return Move_Value(Alloc_Value(), temp);
}


//
//  rebTrap: RL_API
//
// Behaves like rebRun() except traps errors.  Any throws/halts/quits will
// also be converted to an ERROR! and returned as a value.  As with the TRAP
// native when used without a /WITH clause, any non-raised errors that are
// evaluated to will return void...and voids turned into blanks.
//
REBVAL *RL_rebTrap(const void * const p, ...) {

    Enter_Api();

    struct Reb_State state;
    REBCTX *error_ctx;

    PUSH_TRAP(&error_ctx, &state);

    // The first time through the following code 'error' will be NULL, but...
    // `fail` can longjmp here, so 'error' won't be NULL *if* that happens!
    //
    if (error_ctx != NULL)
        return Init_Error(Alloc_Value(), error_ctx);

    va_list va;
    va_start(va, p);

    REBVAL *result = Alloc_Value();
    REBIXO indexor = Do_Va_Core(
        result,
        p, // opt_first (preloads value)
        &va,
        DO_FLAG_EXPLICIT_EVALUATE | DO_FLAG_TO_END
    );
    va_end(va);

    if (indexor == THROWN_FLAG) {
        REBCTX *error = Error_No_Catch_For_Throw(result);
        Free_Value(result);

        fail (error); // throws to above
    }

    DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

    // Analogous to how TRAP works, if you don't have a handler for the
    // error case then you can't return an ERROR!, since all errors indicate
    // a failure.
    //
    // !!! Is returning rebVoid() too "quiet" a response?  Should it fail?
    // Returning NULL seems like it would be prone to creating surprise
    // crashes if the caller didn't expect NULLs, or used them to signal
    // some other purpose.
    //
    if (IS_ERROR(result)) {
        rebRelease(result);
        return rebVoid();
    }

    if (IS_VOID(result)) {
        rebRelease(result);
        return rebBlank();
    }

    return result;
}


//
//  rebElide: RL_API
//
// Variant of rebRun() which assumes you don't need the result.  This saves on
// allocating an API handle, or the caller needing to manage its lifetime.
//
void RL_rebElide(const void *p, ...)
{
    Enter_Api();

    va_list va;
    va_start(va, p);

    DECLARE_LOCAL (elided);
    REBIXO indexor = Do_Va_Core(
        elided,
        p, // opt_first (preloads value)
        &va,
        DO_FLAG_EXPLICIT_EVALUATE | DO_FLAG_TO_END
    );
    va_end(va);

    if (indexor == THROWN_FLAG)
        fail (Error_No_Catch_For_Throw(elided));
}


//
//  rebRunInline: RL_API
//
// Non-variadic function which takes a single argument which must be a single
// value that is a BLOCK! or GROUP!.  The goal is that it not add an extra
// stack level the way calling DO would.  This is important for instance in
// the console, so that BACKTRACE does not look up and see a Rebol function
// like DO on the stack.
//
// !!! This may be replaceable with `rebRun(rebInline(v), END);` or something
// similar.
//
REBVAL *RL_rebRunInline(const REBVAL *array)
{
    Enter_Api();

    if (not IS_BLOCK(array) and not IS_GROUP(array))
        fail ("rebRunInline() only supports BLOCK! and GROUP!");

    DECLARE_LOCAL (group);
    Move_Value(group, array);
    VAL_SET_TYPE_BITS(group, REB_GROUP);

    return rebRun(rebEval(NAT_VALUE(eval)), group, END);
}


//
//  rebPrint: RL_API
//
// Call through to the Rebol PRINT logic.
//
REBOOL RL_rebPrint(const void *p, ...)
{
    Enter_Api();

    REBVAL *print = CTX_VAR(
        Lib_Context,
        Find_Canon_In_Context(Lib_Context, STR_CANON(Intern("print")), TRUE)
    );

    va_list va;
    va_start(va, p);

    const REBOOL uneval_hack = TRUE; // !!! see notes in Array_From_Vaptr
    REBARR *a = Array_From_Vaptr_Maybe_Null(p, &va, uneval_hack);
    va_end(va);

    if (a == NULL)
        return FALSE;

    Deep_Freeze_Array(a);

    // !!! See notes in rebRun() on this particular choice of binding.  For
    // internal usage of PRINT (e.g. calls from PARSE) it really should not
    // be binding into user!
    //
    REBCTX *user_context = VAL_CONTEXT(
        Get_System(SYS_CONTEXTS, CTX_USER)
    );
    Bind_Values_Set_Midstream_Shallow(ARR_HEAD(a), user_context);
    Bind_Values_Deep(ARR_HEAD(a), Lib_Context);

    DECLARE_LOCAL (block);
    Init_Block(block, a);

    REBVAL *result = rebRun(rebEval(print), block, END);
    if (result == NULL)
        return FALSE;

    rebRelease(result);
    return TRUE;
}


//
//  rebEval: RL_API
//
// When rebRun() receives a REBVAL*, the default is to assume it should be
// spliced into the input stream as if it had already been evaluated.  It's
// only segments of code supplied via UTF-8 strings, that are live and can
// execute functions.
//
// This instruction is used with rebRun() in order to mark a value as being
// evaluated.  So `rebRun(rebEval(some_word), ...)` will execute that word
// if it's bound to an ACTION! and dereference if it's a variable.
//
void *RL_rebEval(const REBVAL *v)
{
    Enter_Api();

    if (IS_VOID(v))
        fail ("Cannot pass voids to rebEval()");

    // !!! The presence of the VALUE_FLAG_EVAL_FLIP is a pretty good
    // indication that it's an eval instruction.  So it's not necessary to
    // fill in the ->link or ->misc fields.  But if there were more
    // instructions like this, there'd probably need to be a misc->opcode or
    // something to distinguish them.
    //
    REBARR *result = Alloc_Singular_Array();
    Move_Value(KNOWN(ARR_SINGLE(result)), v);
    SET_VAL_FLAG(ARR_SINGLE(result), VALUE_FLAG_EVAL_FLIP);

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
    Enter_Api();
    return Init_Void(Alloc_Value());
}


//
//  rebBlank: RL_API
//
REBVAL *RL_rebBlank(void)
{
    Enter_Api();
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
    Enter_Api();
    return Init_Logic(Alloc_Value(), did logic);
}


//
//  rebChar: RL_API
//
REBVAL *RL_rebChar(REBUNI codepoint)
{
    Enter_Api();
    return Init_Char(Alloc_Value(), codepoint);
}


//
//  rebInteger: RL_API
//
// !!! Should there be rebSigned() and rebUnsigned(), in order to catch cases
// of using out of range values?
//
REBVAL *RL_rebInteger(REBI64 i)
{
    Enter_Api();
    return Init_Integer(Alloc_Value(), i);
}


//
//  rebDecimal: RL_API
//
REBVAL *RL_rebDecimal(REBDEC dec)
{
    Enter_Api();
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
    Enter_Api();

    REBVAL *result = Alloc_Value();
    RESET_VAL_HEADER(result, REB_TIME);
    VAL_NANO(result) = SECS_TO_NANO(hour * 3600 + minute * 60 + second);
    return result;
}


//
//  rebTimeNano: RL_API
//
REBVAL *RL_rebTimeNano(long nanoseconds) {
    Enter_Api();

    REBVAL *result = Alloc_Value();
    RESET_VAL_HEADER(result, REB_TIME);
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
    Enter_Api();

    REBVAL *result = Alloc_Value();
    RESET_VAL_HEADER(result, REB_DATE); // no time or time zone flags
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
    Enter_Api();

    if (not IS_DATE(date))
        fail ("rebDateTime() date parameter must be DATE!");

    if (not IS_TIME(time))
        fail ("rebDateTime() time parameter must be TIME!");

    // if we had a timezone, we'd need to set DATE_FLAG_HAS_ZONE and
    // then INIT_VAL_ZONE().  But since DATE_FLAG_HAS_ZONE is not set,
    // the timezone bitfield in the date is ignored.

    REBVAL *result = Alloc_Value();
    RESET_VAL_HEADER(result, REB_DATE);
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
    Enter_Api();

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
    Enter_Api();

    REBVAL *event = Append_Event();     // sets signal

    if (event) {                        // null if no room left in series
        RESET_VAL_HEADER(event, REB_EVENT); // has more space, if needed
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
//  rebRescue: RL_API
//
// This API abstracts the mechanics by which exception-handling is done.
// While code that knows specifically which form is used can take advantage of
// that knowledge and use the appropriate mechanism without this API, any
// code (such as core code) that wants to be agnostic to mechanism should
// use rebRescue() instead.
//
// There are three current mechanisms which can be built with.  One is to
// use setjmp()/longjmp(), which is extremely dodgy.  But it's what R3-Alpha
// used, and it's the only choice if one is sticking to ANSI C89-99:
//
// https://en.wikipedia.org/wiki/Setjmp.h#Exception_handling
//
// If one is willing to compile as C++ -and- link in the necessary support
// for exception handling, there are benefits to doing exception handling
// with throw/catch.  One advantage is performance: most compilers can avoid
// paying for catch blocks unless a throw occurs ("zero-cost exceptions"):
//
// https://stackoverflow.com/q/15464891/ (description of the phenomenon)
// https://stackoverflow.com/q/38878999/ (note that it needs linker support)
//
// It also means that C++ API clients can use try/catch blocks without needing
// the rebRescue() abstraction, as well as have destructors run safely.
// (longjmp pulls the rug out from under execution, and doesn't stack unwind).
//
// The other abstraction is for JavaScript, where an emscripten build would
// have to painstakingly emulate setjmp/longjmp.  Using inline JavaScript to
// catch and throw is more efficient, and also provides the benefit of API
// clients being able to use normal try/catch of a RebolError instead of
// having to go through rebRescue().
//
// But using rebRescue() internally allows the core to be compiled and run
// compatibly across all these scenarios.  It is named after Ruby's operation,
// which deals with the identical problem:
//
// http://silverhammermba.github.io/emberb/c/#rescue
//
// !!! As a first step, this only implements the setjmp/longjmp logic.
//
REBVAL *RL_rebRescue(
    REBDNG *dangerous, // !!! pure C function only if not using throw/catch!
    void *opaque
){
    Enter_Api();

    struct Reb_State state;
    REBCTX *error_ctx;

    PUSH_TRAP(&error_ctx, &state);

    // The first time through the following code 'error' will be NULL, but...
    // `fail` can longjmp here, so 'error' won't be NULL *if* that happens!
    //
    if (error_ctx != NULL)
        return Init_Error(Alloc_Value(), error_ctx);

    REBVAL *result = (*dangerous)(opaque);

    DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

    if (result == NULL)
        return NULL; // NULL is considered a legal result

    // Analogous to how TRAP works, if you don't have a handler for the
    // error case then you can't return an ERROR!, since all errors indicate
    // a failure.
    //
    // !!! Is returning rebVoid() too "quiet" a response?  Should it fail?
    // Returning NULL seems like it would be prone to creating surprise
    // crashes if the caller didn't expect NULLs, or used them to signal
    // some other purpose.
    //
    if (IS_ERROR(result)) {
        rebRelease(result);
        return rebVoid();
    }

    if (IS_VOID(result)) {
        rebRelease(result);
        return rebBlank();
    }

    return result;
}


//
//  rebRescueWith: RL_API
//
// Variant of rebRescue() with a handler hook (parallels TRAP/WITH, except
// for C code as the protected code and the handler).  More similar to
// Ruby's rescue2 operation.
//
REBVAL *RL_rebRescueWith(
    REBDNG *dangerous, // !!! pure C function only if not using throw/catch!
    REBRSC *rescuer, // errors in the rescuer function will *not* be caught
    void *opaque
){
    Enter_Api();

    struct Reb_State state;
    REBCTX *error_ctx;

    PUSH_TRAP(&error_ctx, &state);

    // The first time through the following code 'error' will be NULL, but...
    // `fail` can longjmp here, so 'error' won't be NULL *if* that happens!
    //
    if (error_ctx != NULL) {
        REBVAL *error = Init_Error(Alloc_Value(), error_ctx);

        REBVAL *result = (*rescuer)(error, opaque); // *not* guarded by trap!

        rebRelease(error);
        return result; // no special handling, may be NULL
    }

    REBVAL *result = (*dangerous)(opaque); // guarded by trap

    DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

    return result; // no special handling, may be NULL
}


inline static REBFRM *Extract_Live_Rebfrm_May_Fail(const REBVAL *frame) {
    if (not IS_FRAME(frame))
        fail ("Not a FRAME!");

    REBFRM *f = CTX_FRAME_MAY_FAIL(VAL_CONTEXT(frame));

    assert(Is_Action_Frame(f) and not Is_Action_Frame_Fulfilling(f));
    return f;
}


//
//  rebFrmNumArgs: RL_API
//
REBCNT RL_rebFrmNumArgs(const REBVAL *frame) {
    Enter_Api();

    REBFRM *f = Extract_Live_Rebfrm_May_Fail(frame);
    return FRM_NUM_ARGS(f);
}

//
//  rebFrmArg: RL_API
//
REBVAL *RL_rebFrmArg(const REBVAL *frame, REBCNT n) {
    Enter_Api();

    REBFRM *f = Extract_Live_Rebfrm_May_Fail(frame);
    return FRM_ARG(f, n);
}


//
//  rebDid: RL_API
//
REBOOL RL_rebDid(const void *p, ...) {
    Enter_Api();

    va_list va;
    va_start(va, p);

    DECLARE_LOCAL (condition);
    REBIXO indexor = Do_Va_Core(
        condition,
        p, // opt_first (preloads value)
        &va,
        DO_FLAG_EXPLICIT_EVALUATE | DO_FLAG_TO_END
    );
    if (indexor == THROWN_FLAG) {
        va_end(va);
        fail (Error_No_Catch_For_Throw(condition));
    }

    va_end(va);

    return not IS_VOID_OR_FALSEY(condition); // DID treats voids as "falsey"
}


//
//  rebNot: RL_API
//
// !!! If this were going to be a macro like (not (rebDid(...))) it would have
// to be a variadic macro.  Just make a separate entry point for now.
//
REBOOL RL_rebNot(const void *p, ...) {
    Enter_Api();

    va_list va;
    va_start(va, p);

    DECLARE_LOCAL (condition);
    REBIXO indexor = Do_Va_Core(
        condition,
        p, // opt_first (preloads value)
        &va,
        DO_FLAG_EXPLICIT_EVALUATE | DO_FLAG_TO_END
    );
    if (indexor == THROWN_FLAG) {
        va_end(va);
        fail (Error_No_Catch_For_Throw(condition));
    }

    va_end(va);

    return IS_VOID_OR_FALSEY(condition); // NOT treats voids as "falsey"
}


//
//  rebUnboxInteger: RL_API
//
long RL_rebUnboxInteger(const REBVAL *v) {
    Enter_Api();
    return VAL_INT64(v);
}

//
//  rebUnboxDecimal: RL_API
//
REBDEC RL_rebUnboxDecimal(const REBVAL *v) {
    Enter_Api();
    return VAL_DECIMAL(v);
}

//
//  rebUnboxChar: RL_API
//
REBUNI RL_rebUnboxChar(const REBVAL *v) {
    Enter_Api();
    return VAL_CHAR(v);
}

//
//  rebNanoOfTime: RL_API
//
long RL_rebNanoOfTime(const REBVAL *v) {
    Enter_Api();
    return VAL_NANO(v);
}


//
//  rebValTupleData: RL_API
//
REBYTE *RL_rebValTupleData(const REBVAL *v) {
    Enter_Api();
    return VAL_TUPLE_DATA(m_cast(REBVAL*, v));
}


//
//  rebIndexOf: RL_API
//
long RL_rebIndexOf(const REBVAL *v) {
    Enter_Api();
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
    Enter_Api();

    REBVAL *result = Alloc_Value();
    RESET_VAL_HEADER(result, REB_DATE);
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
//  rebMoldAlloc: RL_API
//
// Mold any value and produce a UTF-8 string from it.
//
// !!! Ideally the UTF-8 string returned could use an allocation strategy that
// would make it attach GC to the current FRAME!, while also allowing it to be
// rebRelease()'d.  It might also return a `const char*` to the internal UTF8
// data with a hold on it.
//
char *RL_rebMoldAlloc(REBSIZ *size_out, const REBVAL *v)
{
    Enter_Api();

    DECLARE_MOLD (mo);
    Push_Mold(mo);
    Mold_Value(mo, v);

    REBSIZ size = BIN_LEN(mo->series) - mo->start;

    char *result = cast(char*, rebMalloc(size + 1));
    memcpy(result, BIN_AT(mo->series, mo->start), size + 1); // \0 terminated

    if (size_out != NULL)
        *size_out = size;

    Drop_Mold(mo);
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
    Enter_Api();

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
    Enter_Api();

    size_t size = rebSpellingOf(NULL, 0, v);
    char *result = cast(char*, rebMalloc(size + 1)); // add space for term
    rebSpellingOf(result, size, v);
    if (size_out != NULL)
        *size_out = size;
    return result;
}


//
//  rebSpellingOfW: RL_API
//
// Extract UCS2 data from an ANY-STRING! or ANY-WORD!.
//
// !!! Although the rebSpellingOf API deals in bytes, this deals in count of
// characters.  (The use of REBCNT instead of size_t indicates this.)  It may
// be more useful for the wide string APIs to do this so leaving it that way
// for now.
//
REBCNT RL_rebSpellingOfW(
    REBWCHAR *buf,
    REBCNT buf_chars, // characters buffer can hold (not including terminator)
    const REBVAL *v
){
    Enter_Api();

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
        if (ANY_WORD(v))
            Free_Series(s);
        return len; // caller must now allocate buffer of len + 1
    }

    REBCNT limit = MIN(buf_chars, len);
    REBCNT n = 0;
    for (; index < limit; ++n, ++index)
        buf[n] = GET_ANY_CHAR(s, index);

    buf[limit] = 0;

    if (ANY_WORD(v))
        Free_Series(s);
    return len;
}


//
//  rebSpellingOfAllocW: RL_API
//
REBWCHAR *RL_rebSpellingOfAllocW(REBCNT *len_out, const REBVAL *v)
{
    Enter_Api();

    REBCNT len = rebSpellingOfW(NULL, 0, v);
    REBWCHAR *result = cast(
        REBWCHAR*, rebMalloc(sizeof(REBWCHAR) * (len + 1))
    );
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
    Enter_Api();

    if (not IS_BINARY(binary))
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
    Enter_Api();

    REBCNT len = rebBytesOfBinary(NULL, 0, binary);
    REBYTE *result = cast(REBYTE*, rebMalloc(len + 1));
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
    Enter_Api();

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
    Enter_Api();
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
    REBVAL *result = rebString(utf8); // Enter_Api() called
    RESET_VAL_HEADER(result, REB_FILE);
    return result;
}


//
//  rebTag: RL_API
//
REBVAL *RL_rebTag(const char *utf8)
{
    REBVAL *result = rebString(utf8);
    RESET_VAL_HEADER(result, REB_TAG);
    return result;
}


//
//  rebLock: RL_API
//
REBVAL *RL_rebLock(REBVAL *p1, const REBVAL *p2)
{
    assert(IS_END(p2)); // Not yet variadic...
    UNUSED(p2);

    REBSER *locker = NULL;
    Ensure_Value_Immutable(p1, locker);
    return p1;
}


//
//  rebSizedStringW: RL_API
//
REBVAL *RL_rebSizedStringW(const REBWCHAR *wstr, REBCNT len)
{
    Enter_Api();

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
    Enter_Api();
    return rebSizedStringW(wstr, UNKNOWN);
}


//
//  rebSizedWordW: RL_API
//
// !!! Currently needed by ODBC module to make column titles.
//
REBVAL *RL_rebSizedWordW(const REBWCHAR *ucs2, REBCNT len)
{
    Enter_Api();

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
//  rebFileW: RL_API
//
REBVAL *RL_rebFileW(const REBWCHAR *wstr)
{
    REBVAL *result = rebStringW(wstr); // Enter_Api() called
    RESET_VAL_HEADER(result, REB_FILE);
    return result;
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
REBVAL *RL_rebManage(REBVAL *v)
{
    Enter_Api();

    assert(Is_Api_Value(v));

    REBARR *a = Singular_From_Cell(v);
    assert(GET_SER_FLAG(a, NODE_FLAG_ROOT));

    if (IS_ARRAY_MANAGED(a))
        fail ("Attempt to rebManage() a handle that's already managed.");

    SET_SER_FLAG(a, NODE_FLAG_MANAGED);
    assert(LINK(a).owner == EMPTY_ARRAY);
    if (FS_TOP == NULL)
        LINK(a).owner = EMPTY_ARRAY;
    else
        LINK(a).owner = CTX_VARLIST(
            Context_For_Frame_May_Reify_Managed(FS_TOP)
        );

    return v;
}


//
//  rebUnmanage: RL_API
//
// This converts an API handle value to indefinite lifetime.
//
REBVAL *RL_rebUnmanage(REBVAL *v)
{
    Enter_Api();

    assert(Is_Api_Value(v));

    REBARR *a = Singular_From_Cell(v);
    assert(GET_SER_FLAG(a, NODE_FLAG_ROOT));

    if (not IS_ARRAY_MANAGED(a))
        fail ("Attempt to rebUnmanage() a handle with indefinite lifetime.");

    // It's not safe to convert the average series that might be referred to
    // from managed to unmanaged, because you don't know how many references
    // might be in cells.  But the singular array holding API handles has
    // pointers to its cell being held by client C code only.  It's at their
    // own risk to do this, and not use those pointers after a free.
    //
    CLEAR_SER_FLAG(a, NODE_FLAG_MANAGED);
    assert(
        LINK(a).owner == EMPTY_ARRAY // freed when program exits
        or GET_SER_FLAG(LINK(a).owner, ARRAY_FLAG_VARLIST)
    );
    LINK(a).owner = EMPTY_ARRAY;

    return v;
}


//
//  rebCopyExtra: RL_API
//
REBVAL *RL_rebCopyExtra(const REBVAL *v, REBCNT extra)
{
    Enter_Api();

    // !!! It's actually a little bit harder than one might think to hook
    // into the COPY code without actually calling the function via the
    // evaluator, because it is an "action".  Review a good efficient method
    // for doing it, but for the moment it's just needed for FILE! so do that.
    //
    if (not ANY_STRING(v))
        fail ("rebCopy() only supports ANY-STRING! for now");

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
//  rebLengthOf: RL_API
//
long RL_rebLengthOf(const REBVAL *series)
{
    Enter_Api();

    if (not ANY_SERIES(series))
        fail ("rebLengthOf() can only be used on ANY-SERIES!");

    return VAL_LEN_AT(series);
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
    Enter_Api();

    if (not Is_Api_Value(v))
        panic ("Attempt to rebRelease() a non-API handle");

    Free_Value(v);
}



//
//  rebError: RL_API
//
REBVAL *RL_rebError(const char *msg)
{
    Enter_Api();
    return Init_Error(Alloc_Value(), Error_User(msg));
}


//
//  rebFail: RL_API [
//      #noreturn
//  ]
//
// rebFail() is a distinct entry point (vs. just using rebElide("fail"))
// because it needs to have the noreturn attribute, so that compiler warnings
// can be enabled and checked.
//
// !!! Would calling it rebFAIL (...) make it stand out more?
//
// Note: Over the long term, one does not want to hard-code error strings in
// the executable.  That makes them more difficult to hook with translations,
// or to identify systemically with some kind of "error code".  However,
// it's a realistic quick-and-dirty way of delivering a more meaningful
// error than just using a RE_MISC error code, and can be found just as easily
// to clean up later.
//
// !!! Should there be a way for the caller to slip their C file and line
// information through as the source of the FAIL?
//
void RL_rebFail(const void *p, const void *p2)
{
    Enter_Api();

    assert(Detect_Rebol_Pointer(p2) == DETECTED_AS_END);

    rebElide("fail", p, p2); // should not return...should DO an ERROR!

    // !!! Should there be a special bit or dispatcher used on the FAIL to
    // ensure it does not continue running?  `return: []` is already taken
    // for the "invisible" meaning, but it could be an optimized dispatcher
    // used in wrapping, e.g.:
    //
    //     fail: noreturn func [...] [...]
    //
    // Though HIJACK would have to be aware of it and preserve the rule.
    //
    panic ("FAIL was called, but continued running!");
}


//
//  rebPanic: RL_API [
//      #noreturn
//  ]
//
// Calls PANIC via rebElide(), but is a separate entry point in order to have
// an attribute saying it doesn't return.
//
void RL_rebPanic(const void *p, const void *end)
{
    Enter_Api();
    assert(Detect_Rebol_Pointer(end) == DETECTED_AS_END); // !!! TBD: variadic
    UNUSED(end);

    rebElide(rebEval(NAT_VALUE(panic)), p, END);

    // !!! Should there be a special bit or dispatcher used on the PANIC and
    // PANIC-VALUE functions that ensures they exit?  If it were a dispatcher
    // then HIJACK would have to be aware of it and preserve it.
    //
    panic ("HIJACK'd PANIC function did not exit Rebol");
}


//
//  rebPanicValue: RL_API [
//      #noreturn
//  ]
//
// Calls PANIC-VALUE via rebElide(), but is a separate entry point in order to
// have an attribute saying it doesn't return.
//
void RL_rebPanicValue(const void *p, const void *end)
{
    Enter_Api();
    assert(Detect_Rebol_Pointer(end) == DETECTED_AS_END); // !!! TBD: variadic
    UNUSED(end);

    rebElide(rebEval(NAT_VALUE(panic_value)), p, END);

    // !!! Should there be a special bit or dispatcher used on the PANIC and
    // PANIC-VALUE functions that ensures they exit?  If it were a dispatcher
    // then HIJACK would have to be aware of it and preserve it.
    //
    panic ("HIJACK'd PANIC-VALUE function did not exit Rebol");
}


//
//  rebFileToLocalAlloc: RL_API
//
// This is the API exposure of TO-LOCAL-FILE.  It takes in a FILE! and
// returns an allocated UTF-8 buffer.
//
// !!! Should MAX_FILE_NAME be taken into account for the OS?
//
char *RL_rebFileToLocalAlloc(
    size_t *size_out,
    const REBVAL *file,
    REBFLGS flags // REB_FILETOLOCAL_XXX (FULL, WILD, NO_SLASH)
){
    Enter_Api();

    if (not IS_FILE(file))
        fail ("rebFileToLocalAlloc() only works on FILE!");

    DECLARE_LOCAL (local);
    return rebSpellingOfAlloc(
        size_out,
        Init_String(local, To_Local_Path(file, flags))
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
    REBFLGS flags // REB_FILETOLOCAL_XXX (FULL, WILD, NO_SLASH)
){
    Enter_Api();

    if (not IS_FILE(file))
        fail ("rebFileToLocalAllocW() only works on FILE!");

    DECLARE_LOCAL (local);
    return rebSpellingOfAllocW(
        len_out,
        Init_String(local, To_Local_Path(file, flags))
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
    Enter_Api();

    // !!! Current inefficiency is that the platform-specific code isn't
    // taking responsibility for doing this...Rebol core is going to be
    // agnostic on how files are translated within the hosts.  So the version
    // of the code on non-wide-char systems will be written just for it, and
    // no intermediate string will need be made.
    //
    REBVAL *string = rebString(local);

    REBVAL *file = Init_File(
        Alloc_Value(),
        To_REBOL_Path(string, is_dir ? PATH_OPT_SRC_IS_DIR : 0)
    );

    rebRelease(string);
    return file;
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
    Enter_Api();

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
//  rebEnd: RL_API
//
const REBVAL *RL_rebEnd(void) {return END;}


//
//  rebDeflateAlloc: RL_API
//
// Exposure of the deflate() of the built-in zlib.  Assumes no envelope.
//
// Uses zlib's recommended default for compression level.
//
// See rebRepossess() for the ability to mutate the result into a BINARY!
//
REBYTE *RL_rebDeflateAlloc(
    REBCNT *out_len,
    const unsigned char* input,
    REBCNT in_len
){
    return Compress_Alloc_Core(out_len, input, in_len, SYM_0);
}


//
//  rebZdeflateAlloc: RL_API
//
// Variant of rebDeflateAlloc() which adds a zlib envelope...which is a 2-byte
// header and 32-bit ADLER32 CRC at the tail.
//
REBYTE *RL_rebZdeflateAlloc(
    REBCNT *out_len,
    const unsigned char* input,
    REBCNT in_len
){
    return Compress_Alloc_Core(out_len, input, in_len, SYM_ZLIB);
}


//
//  rebGzipAlloc: RL_API
//
// Slight variant of deflate() which stores the uncompressed data's size
// implicitly in the returned data, and a CRC32 checksum.
//
REBYTE *RL_rebGzipAlloc(
    REBCNT *out_len,
    const unsigned char* input,
    REBCNT in_len
){
    return Compress_Alloc_Core(out_len, input, in_len, SYM_GZIP);
}


//
//  rebInflateAlloc: RL_API
//
// Exposure of the inflate() of the built-in zlib.  Assumes no envelope.
//
// Use max = -1 to guess decompressed size, or for best memory efficiency,
// specify `max` as the precise size of the original data.
//
// See rebRepossess() for the ability to mutate the result into a BINARY!
//
REBYTE *RL_rebInflateAlloc(
    REBCNT *len_out,
    const REBYTE *input,
    REBCNT len_in,
    REBINT max
){
    return Decompress_Alloc_Core(len_out, input, len_in, max, SYM_0);
}


//
//  rebZinflateAlloc: RL_API
//
// Variant of rebInflateAlloc() which assumes a zlib envelope...checking for
// the 2-byte header and verifying the 32-bit ADLER32 CRC at the tail.
//
REBYTE *RL_rebZinflateAlloc(
    REBCNT *len_out,
    const REBYTE *input,
    REBCNT len_in,
    REBINT max
){
    return Decompress_Alloc_Core(len_out, input, len_in, max, SYM_ZLIB);
}


//
//  rebGunzipAlloc: RL_API
//
// Slight variant of inflate() which is compatible with gzip, and checks its
// CRC32.  For data whose original size was < 2^32 bytes, the gzip envelope
// stored that size...so memory efficiency is achieved even if max = -1.
//
// Note: That size guarantee exists for data compressed with rebGzipAlloc() or
// adhering to the gzip standard.  However, archives created with the GNU
// gzip tool make streams with possible trailing zeros or concatenations:
//
// http://stackoverflow.com/a/9213826
//
REBYTE *RL_rebGunzipAlloc(
    REBCNT *len_out,
    const REBYTE *input,
    REBCNT len_in,
    REBINT max
){
    return Decompress_Alloc_Core(len_out, input, len_in, max, SYM_GZIP);
}


//
//  rebDeflateDetectAlloc: RL_API
//
// Does DEFLATE with detection, and also ignores the size information in a
// gzip file, due to the reasoning here:
//
// http://stackoverflow.com/a/9213826
//
REBYTE *RL_rebDeflateDetectAlloc(
    REBCNT *len_out,
    const REBYTE *input,
    REBCNT len_in,
    REBINT max
){
    return Decompress_Alloc_Core(len_out, input, len_in, max, SYM_DETECT);
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

    DECLARE_LOCAL (temp);
    Init_Error(temp, error);
    rebFail(temp, END);
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
