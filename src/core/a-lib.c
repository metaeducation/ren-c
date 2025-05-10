//
//  file: %a-lib.c
//  summary: "Lightweight Export API (RebolValue as opaque type)"
//  section: environment
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Rebol Open Source Contributors
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
// This is the "external" API, and %rebol.h contains its exported
// definitions.  That file (and %make-librebol.r which generates it) contains
// comments and notes which will help understand it.
//
// What characterizes the external API is that it is not necessary to #include
// the extensive definitions of StubStruct/ValueStruct or APIs for dealing with
// all the internal details (e.g. Push_GC_Guard(), which are easy to get
// wrong).  Not only does this simplify the interface, but it also means that
// the C code using the library isn't competing as much for definitions in
// the global namespace.
//
// (That was true of the original API in R3-Alpha, but this later iteration
// speaks in terms of actual Value* cells--vs. creating a new type.  They are
// just opaque pointers to cells whose lifetime is either indefinite, or
// tied to particular function FRAME!s.)
//
// Each exported routine here has a name API_rebXxxYyy.  This is a name by
// which it can be called internally from the codebase like any other function
// that is part of the core.  However, macros for calling it from the core
// are given as `#define rebXxxYyy API_rebXxxYyy`.  This is a little bit nicer
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


const REBOL_HOST_LIB *Host_Lib = nullptr;
EXTERN_C REBOL_HOST_LIB Host_Lib_Init;


//
// rebEnterApi_internal: API
//
// This stub is added automatically to the calling wrappers.
//
// !!! Review how much checking one wants to do when calling API routines,
// and what the balance should be of debug vs. release.  Right now, this helps
// in particular notice if the core tries to use an API function before the
// proper moment in the boot.
//
void API_rebEnterApi_internal(void) {
    if (not Host_Lib)
        crash ("rebStartup() not called before API call");
}


//=//// SERIES-BACKED ALLOCATORS //////////////////////////////////////////=//
//
// These are replacements for malloc(), realloc(), and free() which use a
// byte-sized Binary series as the backing store for the data.
//
// One benefit of using a series is that it offers more options for automatic
// memory management (such as being freed in case of a panic(), vs. leaked as
// a malloc() would, or perhaps being GC'd when a particular FRAME! ends).
//
// It also has the benefit of helping interface with client code that has
// been stylized to use malloc()-ish hooks to produce data, when the eventual
// target of that data is a Rebol series.  It does this without exposing
// Flex* internals to the external API, by allowing one to "rebRepossess()"
// the underlying series as a BINARY! Value*.
//


//
//  rebMalloc: API
//
// * Unlike plain malloc(), this will panic() instead of return null if an
//   allocation cannot be fulfilled.
//
// * Like plain malloc(), if size is zero, the implementation just has to
//   return something that free() will take.  A backing series is added in
//   this case vs. returning null, in order to avoid null handling in other
//   routines (e.g. rebRepossess() or handle lifetime control functions).
//
// * Because of the above points, null is *never* returned.
//
// * It tries to be like malloc() by giving back a pointer "suitably aligned
//   for the size of any fundamental type".  See notes on ALIGN_SIZE.
//
// !!! rebAlignedMalloc() could exist to take an alignment, which could save
// on wasted bytes when ALIGN_SIZE > sizeof(Flex*)...or work with "weird"
// large fundamental types that need more alignment than ALIGN_SIZE.
//
void *API_rebMalloc(size_t size)
{
    Binary* bin = cast(Binary*, Make_Flex_Core(
        ALIGN_SIZE // stores Flex* (must be at least big enough for void*)
            + size // for the actual data capacity (may be 0...see notes)
            + 1, // for termination (even BINARY! has this, review necessity)
        sizeof(Byte), // rebRepossess() only creates binary series ATM
        FLEX_FLAG_DONT_RELOCATE // direct data pointer is being handed back!
            | FLEX_FLAG_ALWAYS_DYNAMIC // rebRepossess() needs bias field
    ));

    Byte *ptr = Binary_Head(bin) + ALIGN_SIZE;

    Binary* *ps = (cast(Binary**, ptr) - 1);
    *ps = bin;  // save self in bytes *right before* data
    POISON_MEMORY(ps, sizeof(Binary*));  // let ASAN catch underruns

    // !!! The data is uninitialized, and if it is turned into a BINARY! via
    // rebRepossess() before all bytes are assigned initialized, it could be
    // worse than just random data...MOLDing such a binary and reading those
    // bytes could be bad (due to, for instance, "trap representations"):
    //
    // https://stackoverflow.com/a/37184840
    //
    // It may be that rebMalloc() and rebRealloc() should initialize with 0
    // to defend against that, but that isn't free.  For now we make no such
    // promise--and leave it uninitialized so that address sanitizer notices
    // when bytes are used that haven't been assigned.
    //
    Term_Binary_Len(bin, ALIGN_SIZE + size);

    return ptr;
}


//
//  rebRealloc: API
//
// * Like plain realloc(), null is legal for ptr (despite the fact that
//   rebMalloc() never returns null, this can still be useful)
//
// * Like plain realloc(), it preserves the lesser of the old data range or
//   the new data range, and memory usage drops if new_size is smaller:
//
// https://stackoverflow.com/a/9575348
//
// * Unlike plain realloc() (but like rebMalloc()), this fails instead of
//   returning null, hence it is safe to say `ptr = rebRealloc(ptr, new_size)`
//
// * A 0 size is considered illegal.  This is consistent with the C11 standard
//   for realloc(), but not with malloc() or rebMalloc()...which allow it.
//
void *API_rebRealloc(void *ptr, size_t new_size)
{
    assert(new_size > 0); // realloc() deprecated this as of C11 DR 400

    if (not ptr) // C realloc() accepts null
        return rebMalloc(new_size);

    Binary* *ps = cast(Binary**, ptr) - 1;
    UNPOISON_MEMORY(ps, sizeof(Binary*)); // need to underrun to fetch `s`

    Binary* bin = *ps;

    REBLEN old_size = Binary_Len(bin) - ALIGN_SIZE;

    // !!! It's less efficient to create a new series with another call to
    // rebMalloc(), but simpler for the time being.  Switch to do this with
    // the same series node.
    //
    void *reallocated = rebMalloc(new_size);
    memcpy(reallocated, ptr, old_size < new_size ? old_size : new_size);
    Free_Unmanaged_Flex(bin);

    return reallocated;
}


//
//  rebFree: API
//
// * As with free(), null is accepted as a no-op.
//
void API_rebFree(void *ptr)
{
    if (not ptr)
        return;

    Binary* *ps = cast(Binary**, ptr) - 1;
    UNPOISON_MEMORY(ps, sizeof(Binary*)); // need to underrun to fetch `s`

    Binary* s = *ps;
    if (Is_Node_A_Cell(s)) {
        rebJumps(
            "crash spaced [",
                "{rebFree() mismatched with allocator!}"
                "{Did you mean to use free() instead of rebFree()?}",
            "]"
        );
    }

    assert(Flex_Wide(s) == 1);

    Free_Unmanaged_Flex(s);
}


//
//  rebRepossess: API
//
// Alternative to rebFree() is to take over the underlying series as a
// BINARY!.  The old void* should not be used after the transition, as this
// operation makes the series underlying the memory subject to relocation.
//
// If the passed in size is less than the size with which the series was
// allocated, the overage will be treated as unused series capacity.
//
// Note that all rebRepossess()'d data will be terminated by an 0x00 byte
// after the end of its capacity.
//
// !!! All bytes in the allocation are expected to be initialized by this
// point, as failure to do so will mean reads crash the interpreter.  See
// remarks in rebMalloc() about the issue, and possibly doing zero fills.
//
// !!! It might seem tempting to use (Binary_Len(s) - ALIGN_SIZE).  However,
// some routines make allocations bigger than they ultimately need and do not
// realloc() before converting the memory to a series...rebInflate() and
// rebDeflate() do this.  So a version passing the size will be necessary,
// and since C does not have the size exposed in malloc() and you track it
// yourself, it seems fair to *always* ask the caller to pass in a size.
//
RebolValue* API_rebRepossess(void *ptr, size_t size)
{
    Binary* *ps = cast(Binary**, ptr) - 1;
    UNPOISON_MEMORY(ps, sizeof(Binary*));  // need to underrun to fetch `s`

    Binary* bin = *ps;
    assert(not Is_Flex_Managed(bin));

    if (size > Binary_Len(bin) - ALIGN_SIZE)
        panic ("Attempt to rebRepossess() more than rebMalloc() capacity");

    assert(Get_Flex_Flag(bin, DONT_RELOCATE));
    Clear_Flex_Flag(bin, DONT_RELOCATE);

    if (Is_Flex_Dynamic(bin)) {
        //
        // Dynamic series have the concept of a "bias", which is unused
        // allocated capacity at the head of a series.  Bump the "bias" to
        // treat the embedded Flex* (aligned to REBI64) as unused capacity.
        //
        Set_Flex_Bias(bin, ALIGN_SIZE);
        bin->content.dynamic.data += ALIGN_SIZE;
        bin->content.dynamic.rest -= ALIGN_SIZE;
    }
    else {
        // Data is in Stub node itself, no bias.  Just slide the bytes down.
        //
        memmove( // src overlaps destination, can't use memcpy()
            Binary_Head(bin),
            Binary_Head(bin) + ALIGN_SIZE,
            size
        );
    }

    Term_Binary_Len(bin, size);
    return Init_Blob(Alloc_Value(), bin);
}



//
//  Startup_Api: C
//
// API routines may be used by extensions (which are invoked by a fully
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
    assert(Host_Lib);
    Host_Lib = nullptr;
}


//
//  rebStartup: API
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
// are later HIJACK'd by some hosts (see CRASH and PANIC), as a way of
// injecting richer platform-or-scenario-specific code into a more limited
// default host operation.  It is expected that the OS_XXX functions will
// eventually disappear completely.
//
void API_rebStartup(void)
{
    if (Host_Lib)
        crash ("rebStartup() called when it's already started");

    Host_Lib = &Host_Lib_Init;

    if (Host_Lib->size < HOST_LIB_SIZE)
        crash ("Host-lib wrong size");

    if (((HOST_LIB_VER << 16) + HOST_LIB_SUM) != Host_Lib->ver_sum)
        crash ("Host-lib wrong version/checksum");

    Startup_Core();
}


//
//  rebShutdown: API
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
void API_rebShutdown(bool clean)
{
    // At time of writing, nothing Shutdown_Core() does pertains to
    // committing unfinished data to disk.  So really there is
    // nothing to do in the case of an "unclean" shutdown...yet.

  #if NO_RUNTIME_CHECKS
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


//
//  rebTick: API
//
// If the executable is built with tick counting, this will return the tick
// without requiring any Rebol code to run (which would disrupt the tick).
//
long API_rebTick(void)
{
    return cast(long, TICK);
}


//
//  rebArg: API
//
// !!! When code is being used to look up arguments of a function, exactly
// how that will work is being considered:
//
// https://forum.rebol.info/t/817
// https://forum.rebol.info/t/820
//
// For the moment, this routine specifically accesses arguments of the most
// recent ACTION! on the stack.
//
RebolValue* API_rebArg(const void *p, va_list *vaptr)
{
    Level* L = TOP_LEVEL;
    REBACT *act = Level_Phase(L);

    // !!! Currently the JavaScript wrappers do not do the right thing for
    // taking just a `const char*`, so this falsely is a variadic to get the
    // JavaScript string proxying.
    //
    const char *name = cast(const char*, p);
    const void *p2 = va_arg(*vaptr, const void*);
    if (Detect_Rebol_Pointer(p2) != DETECTED_AS_END)
        panic ("rebArg() isn't actually variadic, it's arity-1");

    Symbol* symbol = Intern_UTF8_Managed(
        cb_cast(name),
        LEN_BYTES(cb_cast(name))
    );

    Value* param = ACT_PARAMS_HEAD(act);
    Value* arg = Level_Args_Head(L);
    for (; NOT_END(param); ++param, ++arg) {
        if (Are_Synonyms(Cell_Parameter_Symbol(param), symbol))
            return Copy_Cell(Alloc_Value(), arg);
    }

    panic ("Unknown rebArg(...) name.");
}


//
//  rebValue: API
//
// C variadic function which calls the evaluator on multiple pointers.
// Each pointer may either be a Value* or a UTF-8 string which will be
// scanned to reflect one or more values in the sequence.
//
RebolValue* API_rebValue(const void *p, va_list *vaptr)
{
    Value* result = Alloc_Value();
    if (Do_Va_Throws(result, p, vaptr)) // calls va_end()
        panic (Error_No_Catch_For_Throw(result)); // no need to release result

    if (not Is_Nulled(result))
        return result;

    rebRelease(result);
    return nullptr;  // API uses nullptr for NULL
}


//
//  rebElide: API
//
// Variant of rebValue() which assumes you don't need the result.  This saves on
// allocating an API handle, or the caller needing to manage its lifetime.
//
void API_rebElide(const void *p, va_list *vaptr)
{
    DECLARE_VALUE (elided);
    if (Do_Va_Throws(elided, p, vaptr)) // calls va_end()
        panic (Error_No_Catch_For_Throw(elided));
}


//
//  rebJumps: API [
//      #noreturn
//  ]
//
// rebJumps() is like rebElide, but has the noreturn attribute.  This helps
// inform the compiler that the routine is not expected to return.  Use it
// with things like `rebJumps("panic", ...)` or `rebJumps("throw", ...)`.  If
// by some chance the code passed to it does not jump and finishes normally,
// then an error will be raised.
//
// !!! The name is not ideal, but other possibilites aren't great:
//
//    rebDeadEnd(...) -- doesn't sound like it should take arguments
//    rebNoReturn(...) -- whose return?
//    rebStop(...) -- STOP is rather final sounding, the code keeps going
//
void API_rebJumps(const void *p, va_list *vaptr)
{
    DECLARE_VALUE (elided);
    if (Do_Va_Throws(elided, p, vaptr)) { // calls va_end()
        //
        // !!! Being able to THROW across C stacks is necessary in the general
        // case (consider implementing QUIT or HALT).  Probably need to be
        // converted to a kind of error, and then re-converted into a THROW
        // to bubble up through Rebol stacks?  Development on this is ongoing.
        //
        panic (Error_No_Catch_For_Throw(elided));
    }

    panic ("rebJumps() was used to run code, but it didn't PANIC/QUIT/THROW!");
}


//
//  rebValueInline: API
//
// Non-variadic function which takes a single argument which must be a single
// value that is a BLOCK! or GROUP!.  The goal is that it not add an extra
// stack level the way calling DO would.  This is important for instance in
// the console, so that BACKTRACE does not look up and see a Rebol function
// like DO on the stack.
//
RebolValue* API_rebValueInline(const RebolValue* array)
{
    if (not Is_Block(array) and not Is_Group(array))
        panic ("rebValueInline() only supports BLOCK! and GROUP!");

    DECLARE_VALUE (group);
    Copy_Cell(group, array);
    CHANGE_VAL_TYPE_BITS(group, TYPE_GROUP);

    return rebValue(group);
}


//
//  rebQ: API
//
// antiforms are not legal to splice into blocks, and the bootstrap executable
// does not have generic QUOTE_BYTE() or antiform mechanics.
//
// rebQ() works around it by splicing in a GROUP!, or words for certain
// forms.
//
//    null => `~null~`
//    void => `~void~`
//    trash => `~`
//    any-element! => `(the ...)`
//
// There's a parallel Rebol action! that does this called UNEVAL, which is
// for use with REDUCE and COMPOSE/ONLY.  However, rather than return Value*
// directly, this acts as an "instruction" that can be passed to the rebValue()
// variadic stream.  This leaves the implementation method more open, and
// has the benefit of not requiring a rebRelease().
//
const void *API_rebQ(const RebolValue* v)
{
    Array* instruction = Alloc_Instruction();

    Cell* single = ARR_SINGLE(instruction);
    if (not v)
        Init_Nulled(single);
    else if (Is_Nulled(v)) {
        assert(not Is_Api_Value(v));
        Init_Nulled(single);
    }
    else
        Copy_Cell(single, v);

    Meta_Quotify(cast(Value*, single));

    return instruction;  // add opcodes?
}


//
//  rebR: API
//
// Convenience tool for making "auto-release" form of values.  They will only
// exist for one API call.  They will be automatically rebRelease()'d when
// they are seen (or even if they are not seen, if there is a failure on that
// call it will still process the va_list in order to release these handles)
//
const void *API_rebR(RebolValue* v)
{
    if (not Is_Api_Value(v))
        panic ("Cannot apply rebR() to non-API value");

    Array* a = Singular_From_Cell(v);
    if (Get_Flex_Info(a, API_RELEASE))
        panic ("Cannot apply rebR() more than once to the same API value");

    Set_Flex_Info(a, API_RELEASE);
    return v; // returned as const void* to discourage use outside variadics
}


//
//  rebTrash: API
//
RebolValue* API_rebTrash(void)
{
    return Init_Trash(Alloc_Value());
}


//
//  rebBlank: API
//
RebolValue* API_rebBlank(void)
{
    return Init_Blank(Alloc_Value());
}


//
//  rebLogic: API
//
// !!! Use of bool in this file assumes compatibility between C99 stdbool and
// C++ stdbool.  Are they compatible?
//
// "There doesn't seem to be any guarantee that C int is compatible with C++
//  int. 'Linkage from C++ to objects defined in other languages and to
//  objects defined in C++ from other languages is implementation-defined
//  (...) I'd expect C and C++ compilers intended to be used together (such as
//  gcc and g++) to make their bool and int types, among others, compatible."
// https://stackoverflow.com/q/3529831
//
// Take this for granted, then assume that a shim `bool` that is -not- part of
// stdbool would be defined to be the same size as C++'s bool (if such a
// pre-C99 system could even be used to make a C++ build at all!)
//
RebolValue* API_rebLogic(bool logic)
{
    if (logic)
        return Init_Okay(Alloc_Value());
    return nullptr;
}


//
//  rebChar: API
//
RebolValue* API_rebChar(uint32_t codepoint)
{
    if (codepoint > MAX_UNI)
        panic ("Codepoint out of range, see: https://forum.rebol.info/t/374");

    return Init_Char(Alloc_Value(), codepoint);
}


//
//  rebInteger: API
//
// !!! Should there be rebSigned() and rebUnsigned(), in order to catch cases
// of using out of range values?
//
RebolValue* API_rebInteger(int64_t i)
{
    return Init_Integer(Alloc_Value(), i);
}


//
//  rebI: API
//
// Convenience form of `rebR(rebInteger(i))`.
//
const void *API_rebI(int64_t i)
{
    return rebR(rebInteger(i));
}


//
//  rebDecimal: API
//
RebolValue* API_rebDecimal(double dec)
{
    return Init_Decimal(Alloc_Value(), dec);
}


//
//  rebHalt: API
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
void API_rebHalt(void)
{
    SET_SIGNAL(SIG_HALT);
}


//
//  rebRescue: API
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
RebolValue* API_rebRescue(
    REBDNG *dangerous, // !!! pure C function only if not using throw/catch!
    void *opaque
){
    struct Reb_State state;
    Error* error_ctx;

    PUSH_TRAP(&error_ctx, &state);

    // We want allocations that occur in the body of the C function for the
    // rebRescue() to be automatically cleaned up in the case of an error.
    //
    // !!! This is currently done by knowing what frame an error occurred in
    // and marking any allocations while that frame was in effect as being
    // okay to "leak" (in the sense of leaking to be GC'd).  So we have to
    // make a dummy frame here, and unfortunately the frame must be reified
    // so it has to be an "action frame".  Improve mechanic later, but for
    // now pretend to be applying a dummy native.
    //
    DECLARE_END_LEVEL (L);
    L->out = m_cast(Value*, END_NODE); // should not be written

    Symbol* opt_label = nullptr;
    Push_Level_At_End(L, DO_MASK_NONE);

    Reuse_Varlist_If_Available(L); // needed to attach API handles to
    Push_Action(L, PG_Dummy_Action, UNBOUND);
    Begin_Action(L, opt_label, m_cast(Value*, END_NODE));
    assert(IS_END(L->arg));
    L->param = END_NODE; // signal all arguments gathered
    assert(L->refine == END_NODE); // passed to Begin_Action();
    L->arg = m_cast(Value*, END_NODE);
    L->special = END_NODE;

    // The first time through the following code 'error' will be null, but...
    // `panic` can longjmp here, so 'error' won't be null *if* that happens!
    //
    if (error_ctx) {
        assert(L->varlist); // action must be running
        Array* stub = L->varlist; // will be stubbed, with info bits reset
        Drop_Action(L);
        Set_Flex_Info(stub, FRAME_PANICKED); // signal API leaks ok
        Abort_Level(L);
        return Init_Error(Alloc_Value(), error_ctx);
    }

    Value* result = (*dangerous)(opaque);

    Drop_Action(L);

    // !!! To abstract how the system deals with exception handling, the
    // rebRescue() routine started being used in lieu of PUSH_TRAP/DROP_TRAP
    // internally to the system.  Some of these system routines accumulate
    // stack state, so Drop_Level_Unbalanced() must be used.
    //
    Drop_Level_Unbalanced(L);

    DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

    if (not result)
        return nullptr; // null is considered a legal result

    // Analogous to how TRAP works, if you don't have a handler for the
    // error case then you can't return an ERROR!, since all errors indicate
    // a failure.  Use VAL_TYPE_RAW() as BOUNCE_THROWN or other special things can
    // be used internally.
    //
    if (VAL_TYPE_RAW(result) == TYPE_ERROR) {
        if (Is_Api_Value(result))
            rebRelease(result);
        return rebTrash();
    }

    if (not Is_Api_Value(result))
        return result; // no proxying needed

    assert(not Is_Nulled(result)); // leaked API nulled cell (not nullptr)

    // !!! We automatically proxy the ownership of any managed handles to the
    // caller.  Any other handles that leak out (e.g. via state) will not be
    // covered by this, and would have to be unmanaged.  Do another allocation
    // just for the sake of it.

    Value* proxy = Copy_Cell(Alloc_Value(), result); // parent is not f
    rebRelease(result);
    return proxy;
}


//
//  rebRescueWith: API
//
// Variant of rebRescue() with a handler hook (parallels TRAP/WITH, except
// for C code as the protected code and the handler).  More similar to
// Ruby's rescue2 operation.
//
RebolValue* API_rebRescueWith(
    REBDNG *dangerous, // !!! pure C function only if not using throw/catch!
    REBRSC *rescuer, // errors in the rescuer function will *not* be caught
    void *opaque
){
    struct Reb_State state;
    Error* error_ctx;

    PUSH_TRAP(&error_ctx, &state);

    // The first time through the following code 'error' will be null, but...
    // `panic` can longjmp here, so 'error' won't be null *if* that happens!
    //
    if (error_ctx) {
        Value* error = Init_Error(Alloc_Value(), error_ctx);

        Value* result = (*rescuer)(error, opaque); // *not* guarded by trap!

        rebRelease(error);
        return result; // no special handling, may be null
    }

    Value* result = (*dangerous)(opaque); // guarded by trap
    assert(not Is_Nulled(result)); // nulled cells not exposed by API

    DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

    return result;  // no special handling, may be nullptr
}


//
//  rebDid: API
//
bool API_rebDid(const void *p, va_list *vaptr)
{
    DECLARE_VALUE (condition);
    if (Do_Va_Throws(condition, p, vaptr)) // calls va_end()
        panic (Error_No_Catch_For_Throw(condition));

    return IS_TRUTHY(condition); // will panic() on voids
}


//
//  rebNot: API
//
// !!! If this were going to be a macro like (not (rebDid(...))) it would have
// to be a variadic macro.  Just make a separate entry point for now.
//
bool API_rebNot(const void *p, va_list *vaptr)
{
    DECLARE_VALUE (condition);
    if (Do_Va_Throws(condition, p, vaptr)) // calls va_end()
        panic (Error_No_Catch_For_Throw(condition));

    return IS_FALSEY(condition); // will panic() on voids
}


//
//  rebUnbox: API
//
// C++, JavaScript, and other languages can do some amount of intelligence
// with a generic `rebUnbox()` operation...either picking the type to return
// based on the target in static typing, or returning a dynamically typed
// value.  For convenience in C, make the generic unbox operation return
// an integer for INTEGER!, LOGIC!, CHAR!...assume it's most common so the
// short name is worth it.
//
long API_rebUnbox(const void *p, va_list *vaptr)
{
    DECLARE_VALUE (result);
    if (Do_Va_Throws(result, p, vaptr))
        panic (Error_No_Catch_For_Throw(result));

    switch (Type_Of(result)) {
      case TYPE_INTEGER:
        return VAL_INT64(result);

      case TYPE_CHAR:
        return VAL_CHAR(result);

      case TYPE_NULLED:
        return 0;

      case TYPE_OKAY:
        return 1;

      default:
        panic ("C-based rebUnbox() only supports INTEGER!, CHAR!, and LOGIC!");
    }
}


//
//  rebUnboxInteger: API
//
long API_rebUnboxInteger(const void *p, va_list *vaptr)
{
    DECLARE_VALUE (result);
    if (Do_Va_Throws(result, p, vaptr))
        panic (Error_No_Catch_For_Throw(result));

    if (Type_Of(result) != TYPE_INTEGER)
        panic ("rebUnboxInteger() called on non-INTEGER!");

    return VAL_INT64(result);
}


//
//  rebUnboxDecimal: API
//
double API_rebUnboxDecimal(const void *p, va_list *vaptr)
{
    DECLARE_VALUE (result);
    if (Do_Va_Throws(result, p, vaptr))
        panic (Error_No_Catch_For_Throw(result));

    if (Type_Of(result) == TYPE_DECIMAL)
        return VAL_DECIMAL(result);

    if (Type_Of(result) == TYPE_INTEGER)
        return cast(double, VAL_INT64(result));

    panic ("rebUnboxDecimal() called on non-DECIMAL! or non-INTEGER!");
}


//
//  rebUnboxChar: API
//
uint32_t API_rebUnboxChar(const void *p, va_list *vaptr)
{
    DECLARE_VALUE (result);
    if (Do_Va_Throws(result, p, vaptr))
        panic (Error_No_Catch_For_Throw(result));

    if (Type_Of(result) != TYPE_CHAR)
        panic ("rebUnboxChar() called on non-CHAR!");

    return VAL_CHAR(result);
}


//
//  rebHandle: API
//
// !!! The HANDLE! type has some complexity to it, because function pointers
// in C and C++ are not actually guaranteed to be the same size as data
// pointers.  Also, there is an optional size stored in the handle, and a
// cleanup function the GC may call when references to the handle are gone.
//
RebolValue* API_rebHandle(void *data, size_t length, CLEANUP_CFUNC *cleaner)
{
    return Init_Handle_Managed(Alloc_Value(), data, length, cleaner);
}


//
//  rebSpellInto: API
//
// Extract UTF-8 data from an ANY-STRING! or ANY-WORD!.
//
// API does not return the number of UTF-8 characters for a value, because
// the answer to that is always cached for any value position as LENGTH OF.
// The more immediate quantity of concern to return is the number of bytes.
//
size_t API_rebSpellInto(
    char *buf,
    size_t buf_size, // number of bytes
    const RebolValue* v
){
    const char *utf8;
    Size utf8_size;
    if (Any_String(v)) {
        Size offset;
        Binary* temp = Temp_UTF8_At_Managed(
            &offset, &utf8_size, v, Cell_Series_Len_At(v)
        );
        utf8 = cs_cast(Binary_At(temp, offset));
    }
    else {
        assert(Any_Word(v));

        Symbol* symbol = Cell_Word_Symbol(v);
        utf8 = Symbol_Head(symbol);
        utf8_size = Symbol_Size(symbol);
    }

    if (not buf) {
        assert(buf_size == 0);
        return utf8_size; // caller must allocate a buffer of size + 1
    }

    Size limit = MIN(buf_size, utf8_size);
    memcpy(buf, utf8, limit);
    buf[limit] = '\0';
    return utf8_size;
}


//
//  rebSpell: API
//
// This gives the spelling as UTF-8 bytes.  Length in codepoints should be
// extracted with LENGTH OF.  If size in bytes of the encoded UTF-8 is needed,
// use the binary extraction API (works on ANY-STRING! to get UTF-8)
//
char *API_rebSpell(const void *p, va_list *vaptr)
{
    DECLARE_VALUE (string);
    if (Do_Va_Throws(string, p, vaptr)) // calls va_end()
        panic (Error_No_Catch_For_Throw(string));

    if (Is_Nulled(string))
        return nullptr;  // NULL is passed through, for opting out

    size_t size = rebSpellInto(nullptr, 0, string);
    char *result = cast(char*, rebMalloc(size + 1)); // add space for term
    rebSpellInto(result, size, string);
    return result;
}


//
//  rebSpellIntoW: API
//
// Extract UCS-2 data from an ANY-STRING! or ANY-WORD!.  Note this is *not*
// UTF-16, so codepoints that require more than two bytes to represent will
// cause errors.
//
// !!! Although the rebSpellInto API deals in bytes, this deals in count of
// characters.  (The use of REBLEN instead of Size indicates this.)  It may
// be more useful for the wide string APIs to do this so leaving it that way
// for now.
//
unsigned int API_rebSpellIntoW(
    REBWCHAR *buf,
    unsigned int buf_chars, // chars buf can hold (not including terminator)
    const RebolValue* v
){
    Flex* s;
    REBLEN index;
    REBLEN len;
    if (Any_String(v)) {
        s = Cell_Flex(v);
        index = VAL_INDEX(v);
        len = Cell_Series_Len_At(v);
    }
    else {
        assert(Any_Word(v));

        Symbol* symbol = Cell_Word_Symbol(v);
        s = Make_Sized_String_UTF8(Symbol_Head(symbol), Symbol_Size(symbol));
        index = 0;
        len = Flex_Len(s);
    }

    if (not buf) { // querying for size
        assert(buf_chars == 0);
        if (Any_Word(v))
            Free_Unmanaged_Flex(s);
        return len; // caller must now allocate buffer of len + 1
    }

    REBLEN limit = MIN(buf_chars, len);
    REBLEN n = 0;
    for (; index < limit; ++n, ++index)
        buf[n] = GET_ANY_CHAR(s, index);

    buf[limit] = 0;

    if (Any_Word(v))
        Free_Unmanaged_Flex(s);
    return len;
}


//
//  rebSpellW: API
//
// Gives the spelling as WCHARs.  If length in codepoints is needed, use
// a separate LENGTH OF call.
//
// !!! Unlike with rebSpell(), there is not an alternative for getting
// the size in UTF-16-encoded characters, just the LENGTH OF result.  While
// that works for UCS-2 (where all codepoints are two bytes), it would not
// work if Rebol supported UTF-16.  Which it may never do in the core or
// API (possible solutions could include usermode UTF-16 conversion to binary,
// and extraction of that with rebBytes(), then dividing the size by 2).
//
REBWCHAR *API_rebSpellW(const void *p, va_list *vaptr)
{
    DECLARE_VALUE (string);
    if (Do_Va_Throws(string, p, vaptr)) // calls va_end()
        panic (Error_No_Catch_For_Throw(string));

    if (Is_Nulled(string))
        return nullptr;  // NULL is passed through, for opting out

    REBLEN len = rebSpellIntoW(nullptr, 0, string);
    REBWCHAR *result = cast(
        REBWCHAR*, rebMalloc(sizeof(REBWCHAR) * (len + 1))
    );
    rebSpellIntoW(result, len, string);
    return result;
}


//
//  rebBytesInto: API
//
// Extract binary data from a BINARY!
//
// !!! Caller must allocate a buffer of the returned size + 1.  It's not clear
// if this is a good idea; but this is based on a longstanding convention of
// zero termination of Rebol series, including binaries.  Review.
//
size_t API_rebBytesInto(
    unsigned char *buf,
    size_t buf_size,
    const RebolValue* blob
){
    if (not Is_Binary(blob))
        panic ("rebBytesInto() only works on BINARY!");

    REBLEN size = Cell_Series_Len_At(blob);

    if (not buf) {
        assert(buf_size == 0);
        return size; // currently, caller must allocate a buffer of size + 1
    }

    REBLEN limit = MIN(buf_size, size);
    memcpy(s_cast(buf), cs_cast(Cell_Blob_At(blob)), limit);
    buf[limit] = '\0';
    return size;
}


//
//  rebBytes: API
//
// Can be used to get the bytes of a BINARY! and its size, or the UTF-8
// encoding of an ANY-STRING! or ANY-WORD! and that size in bytes.  (Hence,
// for strings it is like rebSpell() except telling you how many bytes.)
//
// !!! This may wind up being a generic TO BINARY! converter, so you might
// be able to get the byte conversion for any type.
//
unsigned char *API_rebBytes(
    size_t *size_out, // !!! Enforce non-null, to ensure type safety?
    const void *p, va_list *vaptr
){
    DECLARE_VALUE (series);
    if (Do_Va_Throws(series, p, vaptr)) // calls va_end()
        panic (Error_No_Catch_For_Throw(series));

    if (Is_Nulled(series)) {
        *size_out = 0;
        return nullptr;  // NULL is passed through, for opting out
    }

    if (Any_Word(series) or Any_String(series)) {
        *size_out = rebSpellInto(nullptr, 0, series);
        Byte *result = rebAllocN(Byte, (*size_out + 1));
        rebSpell(result, *size_out, series);
        return result;
    }

    if (Is_Binary(series)) {
        *size_out = rebBytesInto(nullptr, 0, series);
        Byte *result = rebAllocN(Byte, (*size_out + 1));
        rebBytesInto(result, *size_out, series);
        return result;
    }

    panic ("rebBytes() only works with ANY-STRING!/ANY-WORD!/BINARY!");
}


//
//  rebBinary: API
//
RebolValue* API_rebBinary(const void *bytes, size_t size)
{
    Binary* bin = Make_Binary(size);
    memcpy(Binary_Head(bin), bytes, size);
    Term_Binary_Len(bin, size);

    return Init_Blob(Alloc_Value(), bin);
}


//
//  rebSizedText: API
//
// If utf8 does not contain valid UTF-8 data, this may panic().
//
RebolValue* API_rebSizedText(const char *utf8, size_t size)
{
    return Init_Text(Alloc_Value(), Make_Sized_String_UTF8(utf8, size));
}


//
//  rebText: API
//
RebolValue* API_rebText(const char *utf8)
{
    return rebSizedText(utf8, strsize(utf8));
}


//
//  rebT: API
//
// Shorthand for `rebR(rebText(...))` to more easily create text parameters.
//
// !!! Since the data is UTF-8, it may be possible to make this a "delayed"
// text argument...that saves the pointer it is given and uses it directly,
// then only proxies it into a series at Copy_Cell() time.
//
const void *API_rebT(const char *utf8)
{
    return rebR(rebText(utf8));
}


//
//  rebLengthedTextWide: API
//
RebolValue* API_rebLengthedTextWide(
    const REBWCHAR *wstr,
    unsigned int num_chars
){
    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    for (; num_chars != 0; --num_chars, ++wstr)
        Append_Codepoint(mo->utf8flex, *wstr);

    return Init_Text(Alloc_Value(), Pop_Molded_String(mo));
}


//
//  rebTextWide: API
//
RebolValue* API_rebTextWide(const REBWCHAR *wstr)
{
    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    for (; *wstr != 0; ++wstr)
        Append_Codepoint(mo->utf8flex, *wstr);

    return Init_Text(Alloc_Value(), Pop_Molded_String(mo));
}


//
//  rebManage: API
//
// The "friendliest" default for the API is to assume you want handles to be
// tied to the lifetime of the frame they're in.  Long-running top-level
// processes like the C code running the console would eventually exhaust
// memory if that were the case...so there should be some options for metrics
// as a form of "leak detection" even so.
//
RebolValue* API_rebManage(RebolValue* v)
{
    assert(Is_Api_Value(v));

    Array* a = Singular_From_Cell(v);
    assert(Is_Node_Root_Bit_Set(a));

    if (Is_Flex_Managed(a))
        panic ("Attempt to rebManage() a handle that's already managed.");

    Set_Node_Managed_Bit(a);
    assert(not LINK(a).owner);
    LINK(a).owner = Varlist_For_Level_May_Manage(TOP_LEVEL);

    return v;
}


//
//  rebUnmanage: API
//
// This converts an API handle value to indefinite lifetime.
//
void API_rebUnmanage(void *p)
{
    Node* nod = p;
    if (Is_Node_A_Stub(nod))
        panic ("rebUnmanage() not yet implemented for rebMalloc() data");

    Value* v = cast(Value*, nod);
    assert(Is_Api_Value(v));

    Array* a = Singular_From_Cell(v);
    assert(Is_Node_Root_Bit_Set(a));

    if (not Is_Flex_Managed(a))
        panic ("Attempt to rebUnmanage() a handle with indefinite lifetime.");

    // It's not safe to convert the average Flex that might be referred to
    // from managed to unmanaged, because you don't know how many references
    // might be in cells.  But the singular Array holding API handles has
    // pointers to its cell being held by client C code only.  It's at their
    // own risk to do this, and not use those pointers after a free.
    //
    Clear_Node_Managed_Bit(a);
    assert(Get_Array_Flag(LINK(a).owner, IS_VARLIST));
    LINK(a).owner = nullptr;
}


//
//  rebRelease: API
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
void API_rebRelease(const RebolValue* v)
{
    if (not v)  // note that rebLogic() returns nullptr for false... :-/
        return;  // less rigorous, but makes life easier for C programmers

    if (not Is_Api_Value(v))
        crash ("Attempt to rebRelease() a non-API handle");

    Free_Value(m_cast(Value*, v));
}


//
//  rebPromise: API
//
// The concept of promises in the API is that the code may not be able to run
// to completion, due to a synchronous dependency on something that must be
// fulfilled asynchronously (like trying to implement INPUT in JavaScript).
// This means the interpreter state must be able to suspend, ask for the
// information, and wait for an answer.  This can only be done in JavaScript
// using the PTHREAD emulation of SharedArrayBuffer plus a web worker...so
// that the worker can do an Atomics.wait() on a queued work request, or by
// means of compiling to emterpreter bytecode.
//
// What the promise does is it returns an integer of a unique memory address
// it allocated to use in a mapping for the [resolve, reject] functions.
// It will trigger those mappings when the promise is fulfilled.  In order to
// come back and do that fulfillment, it either puts the code processing into
// a timer callback (emterpreter) or queues it to a thread (pthreads).
//
// The resolve will be called if it reaches the end of the input and the
// reject if there is a failure.
//
intptr_t API_rebPromise(const void *p, va_list *vaptr)
{
  #if !defined(TO_JAVASCRIPT)
    UNUSED(p);
    UNUSED(vaptr);
    panic ("rebPromise() is only available in JavaScript builds");
  #else
    // If we're using a thread model to implement the pausing, then we would
    // have to start executing on that thread here.  The return value model
    // right now is simple and doesn't have a notion for returning either a
    // promise or not, so we always have to return a value that translates to
    // a promise...hence we can't (for instance) do the calculation and notice
    // no asynchronous information was needed.  That is an optimization which
    // could be pursued later.
    //
    // But since that's not what this is doing right now, go ahead and spool
    // the va_list into an array to be executed after a timeout.
    //
    // Currently such spooling is not done except with a frame, and there are
    // a lot of details to get right.  Plus there may be some binding context
    // information coming from the callsite (?).  So here we do a reuse of
    // the code the GC uses to reify va_lists in frames, which we presume does
    // all the ps and qs.  It's messy, but refactor if it turns out to work.

    const Flags flags = EVAL_FLAG_TO_END;

    // !!! The following code is derived from Eval_Va_Core()

    DECLARE_LEVEL (L);
    L->flags = Endlike_Header(flags); // read by Set_Level_Detected_Fetch

    L->source->index = TRASHED_INDEX; // avoids warning in release build
    L->source->array = nullptr;
    L->source->vaptr = vaptr;
    L->source->pending = END_NODE; // signal next fetch comes from va_list
    L->source->deferring_infix = false;

    //
    // We reuse logic in Fetch_Next_In_Level() and Set_Level_Detected_Fetch()
    // but the previous L->value will be tested for NODE_FLAG_ROOT.
    //
    DECLARE_VALUE (junk);
    L->value = Init_Unreadable(junk); // shows where garbage came from

    Set_Level_Detected_Fetch(nullptr, L, p);

    L->out = m_cast(Value*, END_NODE);
    L->specifier = SPECIFIED; // relative values not allowed in va_lists
    L->gotten = nullptr;

    const bool truncated = false;
    Reify_Va_To_Array_In_Level(L, truncated);

    // The array is managed, but let's unmanage it so it doesn't get GC'd and
    // use it as the ID of the table entry for the promise.
    //
    assert(Is_Node_Managed(L->source->array));
    Clear_Node_Managed_Bit(L->source->array);

    EM_ASM_({
        setTimeout(function() { // evaluate the code w/no other code on GUI
            _API_rebPromise_callback($0); // for emscripten_sleep_with_yield()
        }, 0);
    }, L->source->array);

    return cast(intptr_t, L->source->array);
  #endif
}


//
//  rebPromise_callback: API
//
// In the emterpreter build, this is the code that rebPromise() defers to run
// until there is no JavaScript above it or after it on the GUI thread stack.
// This makes it safe to use emscripten_sleep_with_yield() inside of it.
//
// *However* it must be called via _API_rebPromise_callback, not a wrapper.
// emscripten_sleep_with_yield() sets the EmterpreterAsync.state to 1 while it
// is unwinding, and the cwrap() implementation checks the state *after* the
// call that it is 0...since usually, continuing to run would mean running
// more JavaScript.  Calling directly avoids this check as we're *sure* this
// is in an otherwise empty top-level handler.
//
void API_rebPromise_callback(intptr_t promise_id)
{
  #if !defined(TO_JAVASCRIPT)
    UNUSED(promise_id);
    panic ("rebPromise() is only available in JavaScript builds");
  #else
    Array* arr = cast(Array*, cast(void*, promise_id));

    // !!! Should probably do a Push_Trap in order to make sure the REJECT can
    // be called.

    // We took off the managed flag in order to avoid GC.  Let's put it back
    // on... the evaluator will lock it.
    //
    // !!! We probably can't unmanage and free it after because it (may?) be
    // legal for references to that array to make it out to the debugger?
    //
    assert(Not_Is_Node_Managed(arr));
    Set_Node_Managed_Bit(arr);

    Value* result = Alloc_Value();
    if (THROWN_FLAG == Eval_At_Core(
        Init_Void(result),
        nullptr, // opt_first (null indicates nothing, not nulled cell)
        arr,
        0, // index
        SPECIFIED,
        EVAL_FLAG_TO_END // was reified w/explicit
    )){
        panic (Error_No_Catch_For_Throw(result)); // no need to release result
    }

    if (Is_Nulled(result)) {
        rebRelease(result); // recipient must release if not nullptr
        result = nullptr;
    }

    EM_ASM_({
        API_Resolve($0, $1); // assumes it can now free the table entry
    }, promise_id, result);
  #endif
}


//
//  rebDeflateAlloc: API
//
// Exposure of the deflate() of the built-in zlib.  Assumes no envelope.
//
// Uses zlib's recommended default for compression level.
//
// See rebRepossess() for the ability to mutate the result into a BINARY!
//
void *API_rebDeflateAlloc(
    size_t *out_len,
    const void *input,
    size_t in_len
){
    Symbol* envelope = CANON(NONE);
    return Compress_Alloc_Core(out_len, input, in_len, envelope);
}


//
//  rebZdeflateAlloc: API
//
// Variant of rebDeflateAlloc() which adds a zlib envelope...which is a 2-byte
// header and 32-bit ADLER32 CRC at the tail.
//
void *API_rebZdeflateAlloc(
    size_t *out_len,
    const void *input,
    size_t in_len
){
    Symbol* envelope = CANON(ZLIB);
    return Compress_Alloc_Core(out_len, input, in_len, envelope);
}


//
//  rebGzipAlloc: API
//
// Slight variant of deflate() which stores the uncompressed data's size
// implicitly in the returned data, and a CRC32 checksum.
//
void *API_rebGzipAlloc(
    size_t *out_len,
    const void *input,
    size_t in_len
){
    Symbol* envelope = nullptr; // see notes in Gunzip on why GZIP is default
    return Compress_Alloc_Core(out_len, input, in_len, envelope);
}


//
//  rebInflateAlloc: API
//
// Exposure of the inflate() of the built-in zlib.  Assumes no envelope.
//
// Use max = -1 to guess decompressed size, or for best memory efficiency,
// specify `max` as the precise size of the original data.
//
// See rebRepossess() for the ability to mutate the result into a BINARY!
//
void *API_rebInflateAlloc(
    size_t *len_out,
    const void *input,
    size_t len_in,
    int max
){
    Symbol* envelope = CANON(NONE);
    return Decompress_Alloc_Core(len_out, input, len_in, max, envelope);
}


//
//  rebZinflateAlloc: API
//
// Variant of rebInflateAlloc() which assumes a zlib envelope...checking for
// the 2-byte header and verifying the 32-bit ADLER32 CRC at the tail.
//
void *API_rebZinflateAlloc(
    size_t *len_out,
    const void *input,
    size_t len_in,
    int max
){
    Symbol* envelope = CANON(ZLIB);
    return Decompress_Alloc_Core(len_out, input, len_in, max, envelope);
}


//
//  rebGunzipAlloc: API
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
void *API_rebGunzipAlloc(
    size_t *len_out,
    const void *input,
    size_t len_in,
    int max
){
    // Note: Because GZIP is what Rebol uses for booting, `nullptr` means
    // use GZIP.  That's because symbols in %words.r haven't been loaded yet,
    // so a call to CANON(XXX) would fail.
    //
    Symbol* envelope = nullptr; // GZIP is the default
    return Decompress_Alloc_Core(len_out, input, len_in, max, envelope);
}


//
//  rebDeflateDetectAlloc: API
//
// Does DEFLATE with detection, and also ignores the size information in a
// gzip file, due to the reasoning here:
//
// http://stackoverflow.com/a/9213826
//
void *API_rebDeflateDetectAlloc(
    size_t *len_out,
    const void *input,
    size_t len_in,
    int max
){
    Symbol* envelope = CANON(DETECT);
    return Decompress_Alloc_Core(len_out, input, len_in, max, envelope);
}


// !!! Although it is very much the goal to get all OS-specific code out of
// the core (including the API), this particular hook is extremely useful to
// have available to all clients.  It might be done another way (e.g. by
// having hosts HIJACK the PANIC native with an adaptation that processes
// integer arguments).  But for now, stick it in the API just to get the
// wide availability.
//
#ifdef TO_WINDOWS
    #include <windows.h>
#else
    #include <errno.h>
    #define MAX_POSIX_ERROR_LEN 1024
#endif

//
//  rebError_OS: API
//
// Produce an error from an OS error code, by asking the OS for textual
// information it knows internally from its database of error strings.
//
// Note that error codes coming from WSAGetLastError are the same as codes
// coming from GetLastError in 32-bit and above Windows:
//
// https://stackoverflow.com/q/15586224/
//
// !!! Should not be in core, but extensions need a way to trigger the
// common functionality one way or another.
//
RebolValue* API_rebError_OS(int errnum)
{
    Error* error;

  #ifdef TO_WINDOWS
    if (errnum == 0)
        errnum = GetLastError();

    WCHAR *lpMsgBuf; // FormatMessage writes allocated buffer address here

    // Specific errors have %1 %2 slots, and if you know the error ID and
    // that it's one of those then this lets you pass arguments to fill
    // those in.  But since this is a generic error, we have no more
    // parameterization (hence FORMAT_MESSAGE_IGNORE_INSERTS)
    //
    va_list *Arguments = nullptr;

    // Apparently FormatMessage can find its error strings in a variety of
    // DLLs, but we don't have any context here so just use the default.
    //
    LPCVOID lpSource = nullptr;

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
        Value* message = rebTextWide(lpMsgBuf);
        LocalFree(lpMsgBuf);

        error = Make_Error_Managed(SYM_0, SYM_0, message, END_NODE);
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
        if (maybe_str != buffer) {
            strncpy(buffer, maybe_str, MAX_POSIX_ERROR_LEN - 1);
            buffer[MAX_POSIX_ERROR_LEN - 1] = '\0';
        }
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

    return Init_Error(Alloc_Value(), error);
}
