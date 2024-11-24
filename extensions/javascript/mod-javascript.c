//
//  File: %mod-javascript.c
//  Summary: "Support for calling Javascript from Rebol in Emscripten build"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2018-2022 Ren-C Open Source Contributors
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
// See %extensions/javascript/README.md
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * This extension expands librebol with new API_rebXXX() entry points.  It
//   was tried to avoid this--doing everything with helper natives.  This
//   would use things like `reb.UnboxInteger("rebpromise-helper", ...)` and
//   build a pure-JS reb.Promise() on top of that.  Initially this was
//   rejected due reb.UnboxInteger() allocating stack for the va_list calling
//   convention...disrupting the "sneaky exit and reentry" done by the
//   Emterpreter.  Now that Emterpreter is replaced with Asyncify, that's
//   not an issue--but it's still faster to have raw WASM entry points like
//   API_rebPromise_internal().
//
// * If the code block in the EM_ASM() family of functions contains a comma,
//   then wrap the whole code block with parentheses ().  See the examples
//   which are cited in %em_asm.h
//
// * Stack overflows were historically checked via a limit calculated at boot
//   time (see notes on Set_Stack_Limit()).  That can't be used in the
//   emscripten build, hence stack overflows currently crash.  This is being
//   tackled by means of the stackless branch (unfinished at time of writing):
//
//   https://forum.rebol.info/t/switching-to-stackless-why-this-why-now/1247
//
// * Note that how many JS function recursions there are is affected by
//   optimization levels like -Os or -Oz.  These avoid inlining, which
//   means more JavaScript/WASM stack calls to do the same amount of work...
//   leading to invisible limit being hit sooner.  We should always compile
//   %c-eval.c with -O2 to try and avoid too many recursions, so see
//   #prefer-O2-optimization in %file-base.r.  Stackless will make this less
//   of an issue.
//


#include "tmp-mod-javascript.h"

#include <limits.h>  // for UINT_MAX

// Quick source links for emscripten.h and em_asm.h (which it includes):
//
// https://github.com/emscripten-core/emscripten/blob/master/system/include/emscripten/emscripten.h
// https://github.com/emscripten-core/emscripten/blob/master/system/include/emscripten/em_asm.h
//
#include <emscripten.h>


//=//// DEBUG_JAVASCRIPT_EXTENSION TOOLS //////////////////////////////////=//
//
// Ren-C has very aggressive RUNTIME_CHECKS, and turning them all on can
// result in a prohibitive emscripten build: not just in size and speed of
// the build products, but the compilation can wind up taking a long time--or
// not succeeding at all.  This has been getting better, and it's possible
// to do source-level debugging of the whole system in Chrome, albeit slowly.
//
// So usually the system is built with NO_RUNTIME_CHECKS.  The hope is that
// the core is tested elsewhere (or if a bug is encountered in the interpreter
// under emscripten, it will be reproduced and can be debugged in a
// non-JavaScript build).
//
// However, getting some amount of feedback in the console is essential to
// debugging the JavaScript extension itself.  These are some interim hacks
// for doing that until better ideas come along.

#if !defined(DEBUG_JAVASCRIPT_EXTENSION)
    #define DEBUG_JAVASCRIPT_EXTENSION 0
#endif

#if !defined(DEBUG_JAVASCRIPT_SILENT_TRACE)
    #define DEBUG_JAVASCRIPT_SILENT_TRACE 0
#endif

#if DEBUG_JAVASCRIPT_SILENT_TRACE

    // Trace output can influence the behavior of the system so that race
    // conditions or other things don't manifest.  This is tricky.  If this
    // happens we can add to the silent trace buffer.
    //
    static char PG_Silent_Trace_Buf[64000] = "";

    EXTERN_C intptr_t API_rebGetSilentTrace_internal(void) {
      { return i_cast(intptr_t, PG_Silent_Trace_Buf); }
#endif

#if DEBUG_JAVASCRIPT_EXTENSION
    #undef assert  // if it was defined (most emscripten builds are NDEBUG)
    #define assert(expr) \
        do { if (!(expr)) { \
            printf("%s:%d - assert(%s)\n", __FILE__, __LINE__, #expr); \
            exit(0); \
        } } while (0)

    static bool PG_JS_Trace = false;  // Turned on/off with JS-TRACE native

    INLINE void Javascript_Trace_Helper_Debug(const char *buf) {
        if (PG_JS_Trace) {
            printf("TICK %" PRIu64 ": %s\n", TICK, buf);  // prefix ticks
            fflush(stdout);  // just to be safe
        }
    }

    // Caution: trace buffer not thread safe (interpreter is not thread safe
    // anyway, but JS extension at one time did use threads in a way that
    // didn't run the interpreter in parallel but ran some C code in parallel
    // that might have called trace.  That feature was removed, though.)
    //
    // Note: This is done as a statement and not a `do { } while (0)` macro
    // on purpose!  It's so that EM_ASM_INT() can use it in an expression
    // returning a value.
    //
    #define JS_TRACE_BUF_SIZE 2048
    static char js_trace_buf_debug[JS_TRACE_BUF_SIZE];
    #define TRACE(...)  /* variadic, but emscripten is at least C99! :-) */ \
        Javascript_Trace_Helper_Debug( \
            (snprintf(js_trace_buf_debug, JS_TRACE_BUF_SIZE, __VA_ARGS__), \
                js_trace_buf_debug))

    // One of the best pieces of information to follow for a TRACE() is what
    // the EM_ASM() calls.  So printing the JavaScript sent to execute is
    // very helpful.  But it's not possible to "hook" EM_ASM() in terms of
    // its previous definition:
    //
    // https://stackoverflow.com/q/3085071/
    //
    // Fortunately the definitions for EM_ASM() are pretty simple, so writing
    // them again is fine...just needs to change if emscripten.h does.
    //
    #undef EM_ASM
    #define EM_ASM(code, ...) \
        ( \
            TRACE("EM_ASM(%s)", #code), \
            (void)emscripten_asm_const_int( \
                CODE_EXPR(#code) _EM_ASM_PREP_ARGS(__VA_ARGS__) \
            ) \
        )

    #undef EM_ASM_INT
    #define EM_ASM_INT(code, ...) \
        ( \
            TRACE("EM_ASM_INT(%s)", #code), \
            emscripten_asm_const_int( \
                CODE_EXPR(#code) _EM_ASM_PREP_ARGS(__VA_ARGS__) \
            ) \
        )
#else
    // assert() is defined as a noop in release builds already

    #define TRACE(...)                      NOOP
#endif


//=//// HEAP ADDRESS ABSTRACTION //////////////////////////////////////////=//
//
// Generally speaking, C exchanges integers with JavaScript.  These integers
// (e.g. the ones that come back from EM_ASM_INT) are typed as `unsigned int`.
// That's unfortunately not a `uintptr_t`...which would be a type that by
// definition can hold any pointer.  But there are cases in the emscripten
// code where this is presumed to be good enough to hold any heap address.
//
// Track the places that make this assumption with `heapaddr_t`, and sanity
// check that we aren't truncating any C pointers in the conversions.
//
// Note heap addresses can be used as ID numbers in JavaScript for mapping
// C entities to JavaScript objects that cannot be referred to directly.
// Tables referring to them must be updated when the related pointer is
// freed, as the pointer may get reused.

typedef unsigned int heapaddr_t;

INLINE heapaddr_t Heapaddr_From_Pointer(const void *p) {
    uintptr_t u = i_cast(uintptr_t, p);
    assert(u < UINT_MAX);
    return u;
}

INLINE void* Pointer_From_Heapaddr(heapaddr_t addr)
  { return p_cast(void*, cast(uintptr_t, addr)); }

static void cleanup_js_object(const Value* v) {
    heapaddr_t id = Heapaddr_From_Pointer(Cell_Handle_Void_Pointer(v));

    // If a lot of JS items are GC'd, would it be better to queue this in
    // a batch, as `reb.UnregisterId_internal([304, 1020, ...])`?  (That was
    // more of an issue when the GC could run on a separate thread and have
    // to use postMessage each time it wanted to run code.)
    //
    EM_ASM(
        { reb.UnregisterId_internal($0); },  // don't leak map[int->JS funcs]
        id  // => $0
    );
}


//=//// LEVEL ID AND THROWING /////////////////////////////////////////////=//
//
// !!! Outdated comment, review what happened here:
//
// "We go ahead and use the VarList* instead of the raw Level* to act as
//  the unique pointer to identify a level.  That's because if the JavaScript
//  code throws and that throw needs to make it to a promise higher up the
//  stack, it uses that pointer as an ID in a mapping table to associate the
//  call with the JavaScript object it threw.
//
//  This aspect is overkill for something that can only happen once on the
//  stack at a time.  Future designs may translate that object into Rebol so
//  it could be caught by Rebol, but for now we assume a throw originating
//  from JavaScript code may only be caught by JavaScript code."
//

INLINE heapaddr_t Level_Id_For_Level(Level* L) {
    return Heapaddr_From_Pointer(L);
}

INLINE Level* Level_From_Level_Id(heapaddr_t id) {
    return cast(Level*, Pointer_From_Heapaddr(id));
}

INLINE Value* Value_From_Value_Id(heapaddr_t id) {
    if (id == 0)
        return nullptr;

    Value* v = cast(Value*, Pointer_From_Heapaddr(id));
    assert(not Is_Nulled(v));  // API speaks in nullptr only
    return v;
}


//=//// JS-NATIVE PER-ACTION! DETAILS /////////////////////////////////////=//

enum {
    // The API uses some clever variable shadowing tricks to make it so that
    // the `reb` that is seen in each function for making calls like
    // `reb.Value("some-native-arg")` has visibility of the frame variables
    // of the native being called, for the duration of its specific body.
    // But that frame has to inherit from some context to get definitions
    // to get things out of lib or the module that's running.  This context
    // is set for natives at construction time to know what to make the
    // frame inherit from.
    //
    // !!! This is a limiting idea, and it may be better to allow (require?)
    // the body of a JavaScript native to be a block with a string in it...
    // so that the block can capture an environment.  This way you could make
    // a JavaScript native inside a function and inherit the visibility of
    // variables inside that function, etc.
    //
    IDX_JS_NATIVE_CONTEXT = IDX_NATIVE_CONTEXT,

    // Each native has a corresponding JavaScript object that holds the
    // actual implementation function of the body.  Since pointers to JS
    // objects can't be held directly by WebAssembly (yet), instead they
    // are stored in a map that is indexed by a numeric key.
    //
    // A HANDLE! is used to store the map key, so that it can also save a
    // cleanup callback that will be run during GC, so that map entries
    // in the JavaScript do not leak.
    //
    IDX_JS_NATIVE_OBJECT,

    // The JavaScript source code for the function.  We don't technically
    // need to hang onto this...and could presumably ask JavaScript to give
    // it back to us for the SOURCE command.
    //
    IDX_JS_NATIVE_SOURCE,

    // A LOGIC! is stored of whether this native is an awaiter or not.
    // (There should probably be some kind of ACTION_FLAG_XXX that is given
    // by the system to natives to use for simple flags like this)
    //
    IDX_JS_NATIVE_IS_AWAITER,

    IDX_JS_NATIVE_MAX
};


INLINE heapaddr_t Native_Id_For_Action(Action* act)
  { return Heapaddr_From_Pointer(ACT_IDENTITY(act)); }

Bounce JavaScript_Dispatcher(Level* L);


//=//// GLOBAL PROMISE STATE //////////////////////////////////////////////=//
//
// Several promises can be requested sequentially, and so they queue up in
// a linked list.  However, until stackless is implemented they can only
// run one at a time...so they have to become unblocked in the same order
// they are submitted.
//
// !!! Having the interpreter serve multiple promises in flight at once is a
// complex issue, which in the stackless build would end up being tied in
// with any other green-thread scheduling.  It's not currently tested, and is
// here as a placeholder for future work.
//

enum Reb_Promise_State {
    PROMISE_STATE_QUEUEING,
    PROMISE_STATE_RUNNING,
    PROMISE_STATE_AWAITING,
    PROMISE_STATE_RESOLVED,
    PROMISE_STATE_REJECTED
};

struct Reb_Promise_Info {
    enum Reb_Promise_State state;
    heapaddr_t promise_id;
    Context* binding;  // where code is to be run

    struct Reb_Promise_Info *next;
};

static struct Reb_Promise_Info *PG_Promises;  // Singly-linked list


enum Reb_Native_State {
    ST_JS_NATIVE_INITIAL_ENTRY = STATE_0,
    ST_JS_NATIVE_RUNNING,
    ST_JS_NATIVE_SUSPENDED,
    ST_JS_NATIVE_RESOLVED,
    ST_JS_NATIVE_REJECTED
};


// <review>  ;-- Review in light of asyncify
// This returns an integer of a unique memory address it allocated to use in
// a mapping for the [resolve, reject] functions.  We will trigger those
// mappings when the promise is fulfilled.  In order to come back and do that
// fulfillment, it either puts the code processing into a timer callback
// (emterpreter) or queues it to a thread (pthreads).
// </review>
//
// The resolve will be called if it reaches the end of the input and the
// reject if there is a failure.
//
// Note: See %make-librebol.r for code that produces the `rebPromise(...)` API,
// which ties the returned integer into the resolve and reject branches of an
// actual JavaScript ES6 Promise.
//
EXTERN_C intptr_t API_rebPromise(
    RebolContext** binding_ref,
    void* p, void* vaptr
){
    TRACE("rebPromise() called");

    // If we're asked to run `rebPromise("input")`, that requires interacting
    // with the DOM, and there is no way of fulfilling it synchronously.  But
    // something like `rebPromise("1 + 2")` could be run in a synchronous
    // way...if there wasn't some HIJACK or debug in effect that needed to
    // `print` as part of tracing that code.
    //
    // So speculatively running and then yielding only on asynchronous
    // requests would be *technically* possible.  But it would require the
    // stackless build features--unfinished at time of writing.  Without that
    // then asyncify is incapable of doing it...it's stuck in the caller's JS
    // stack it can't sleep_with_yield() from).
    //
    // But there's also an issue that if we allow a thread to run now, then we
    // would have to block the MAIN thread from running.  And while the MAIN
    // was blocked we might actually fulfill the promise in question.  But
    // then this would need a protocol for returning already fulfilled
    // promises--which becomes a complex management exercise of when the
    // table entry is freed for the promise.
    //
    // To keep the contract simple (and not having a wildly different version
    // for the emterpreter vs. not), we don't execute anything now.  Instead
    // we spool the request into an array.  Then we use `setTimeout()` to ask
    // to execute that array in a callback at the top level.  This permits
    // an emterpreter sleep_with_yield(), or running a thread that can take
    // for granted the resolve() function created on return from this helper
    // already exists.

    DECLARE_VALUE (block);
    UNUSED(binding_ref);  // shouldn't use one if we're transcoding?
    API_rebTranscodeInto(binding_ref, block, p, vaptr);

    Array* code = Cell_Array_Ensure_Mutable(block);
    assert(Is_Node_Managed(code));
    Clear_Node_Managed_Bit(code);  // using array as ID, don't GC it

    // We singly link the promises such that they will be executed backwards.
    // What's good about that is that it will help people realize that over
    // the long run, there's no ordering guarantee of promises (e.g. if they
    // were running on individual threads).

    struct Reb_Promise_Info *info = Try_Alloc_Memory(struct Reb_Promise_Info);
    info->state = PROMISE_STATE_QUEUEING;
    info->promise_id = Heapaddr_From_Pointer(code);
    info->binding = Get_Context_From_Stack();
    info->next = PG_Promises;
    PG_Promises = info;

    EM_ASM(
        { setTimeout(function() { reb.m._API_rebIdle_internal(); }, 0); }
    );  // note `_RL` (leading underscore means no cwrap)

    return info->promise_id;
}


// 1. Cooperative suspension is when there are no "stackful" invocations of
//    the trampoline.  This is the preferred method.  Pre-emptive suspension
//    is when the stack is not able to be unwound, and tricky code in
//    emscripten has to be used.
//
void RunPromise(void)
{
    struct Reb_Promise_Info *info = PG_Promises;

    switch (info->state) {
      case PROMISE_STATE_QUEUEING :
        goto queue_promise;

      case PROMISE_STATE_RUNNING :
        goto run_promise;

      default : assert(false);
    }

  queue_promise: {  //////////////////////////////////////////////////////////

    info->state = PROMISE_STATE_RUNNING;

    Source* a = cast(Source*, Pointer_From_Heapaddr(info->promise_id));
    assert(Not_Node_Managed(a));  // took off so it didn't GC
    Set_Node_Managed_Bit(a);  // but need it back on to execute it

    DECLARE_ATOM (code);
    Init_Block(code, a);
    BINDING(code) = info->binding;

    Level* L = Make_Level_At(&Stepper_Executor, code, LEVEL_FLAG_ROOT_LEVEL);

    Push_Level_Dont_Inherit_Interruptibility(  // you can HALT inside a promise
        cast(Atom*, Alloc_Value_Core(CELL_MASK_ERASED_0)),  // don't set root
        L
    );
    goto run_promise;

} run_promise: {  ////////////////////////////////////////////////////////////

    Bounce r = Trampoline_From_Top_Maybe_Root();

    if (r == BOUNCE_SUSPEND) {  // cooperative suspension [1]
        return;  // the setTimeout() on resolve/reject will queue us back
    }

    Value* metaresult;
    if (r == BOUNCE_THROWN) {
        assert(Is_Throwing(TOP_LEVEL));
        Error* error = Error_No_Catch_For_Throw(TOP_LEVEL);
        metaresult = Init_Error(TOP_LEVEL->out, error);
    }
    else
        metaresult = Meta_Quotify(TOP_LEVEL->out);

    Drop_Level(TOP_LEVEL);

    // Note: The difference between `throw()` and `reject()` in JS is subtle.
    //
    // https://stackoverflow.com/q/33445415/

    TRACE("RunPromise() finished Running Array");

    if (info->state == PROMISE_STATE_RUNNING) {
        if (rebUnboxLogic("error? @", metaresult)) {
            //
            // Note this could be an uncaught throw error, or a specific
            // fail() error.
            //
            info->state = PROMISE_STATE_REJECTED;
            TRACE("RunPromise() => promise is rejecting due to error");
          #if DEBUG_HAS_PROBE
            if (PG_Probe_Failures)
                PROBE(metaresult);
          #endif
            Free_Value(metaresult);  // !!! report the error?
        }
        else {
            info->state = PROMISE_STATE_RESOLVED;
            TRACE("RunPromise() => promise is resolving");

            // !!! The Promise expects to receive this result and process it.
            // But what if it doesn't pay attention to it and release it?
            // It could cause leaks.
            //
            Value* result = rebValue("unmeta", rebQ(metaresult));
            Free_Value(metaresult);
            rebUnmanage(result);

            EM_ASM(
                { reb.ResolvePromise_internal($0, $1); },
                info->promise_id,  // => $0 (table entry will be freed)
                result  // => $1 (recipient takes over handle)
            );
        }
    }
    else {
        // !!! It's not clear what this code was supposed to handle; it seems
        // to have been leftover from the pthreads build.  It was using the
        // result of the block evaluation and asserting it was a FRAME!.
        // Keeping it here to see if it triggers the trace; but it's not
        // likely to because there's only one reb.Promise() wrapping all of
        // the ReplPad at this time.

        TRACE("RunPromise() => promise is rejecting due to...something (?)");

        assert(info->state == PROMISE_STATE_REJECTED);

        Value* result = nullptr;  // !!! What was this supposed to be?

        // Note: Expired, can't use VAL_CONTEXT
        //
        assert(Is_Frame(result));
        const Node* frame_ctx = Cell_Node1(result);
        heapaddr_t throw_id = Heapaddr_From_Pointer(frame_ctx);

        EM_ASM(
            { reb.RejectPromise_internal($0, $1); },
            info->promise_id,  // => $0 (table entry will be freed)
            throw_id  // => $1 (table entry will be freed)
        );
    }

    assert(PG_Promises == info);
    PG_Promises = info->next;
    Free_Memory(struct Reb_Promise_Info, info);
}}


//
// Until the stackless build is implemented, rebPromise() must defer its
// execution until there is no JavaScript above it or after it on the stack.
//
// During this call, emscripten_sleep() can sneakily make us fall through
// to the main loop.  We don't notice it here--it's invisible to the C
// code being yielded.  -BUT- the JS callsite for rebIdle() would
// notice, as it would seem rebIdle() had finished...when really what's
// happening is that the instrumented WASM is putting itself into
// suspended animation--which it will come out of via a setTimeout.
//
// (This is why there shouldn't be any meaningful JS on the stack above
// this besides the rebIdle() call itself.)
//
EXTERN_C void API_rebIdle_internal(void)  // NO user JS code on stack!
{
    TRACE("rebIdle() => begin running promise code");

    // In stackless, we'd have some protocol by which RunPromise() could get
    // started in rebPromise(), then maybe be continued here.  For now, it
    // is always continued here.
    //
    RunPromise();

    TRACE("rebIdle() => finished running promise code");
}


// Initially this was rebSignalResolveNative() and not rebResolveNative()
// The reason was that the empterpreter build had the Ren-C interpreter
// suspended, and there was no way to build a Value* to pass through to it.
// So the result was stored as a function in a table to generate the value.
// Now it pokes the result directly into the frame's output slot.
//
EXTERN_C void API_rebResolveNative_internal(
    intptr_t level_id,
    intptr_t result_id
){
    Level* const L = Level_From_Level_Id(level_id);
    USE_LEVEL_SHORTHANDS (L);

    TRACE("reb.ResolveNative_internal(%s)", Level_Label_Or_Anonymous_UTF8(L));

    Value* result = Value_From_Value_Id(result_id);

    if (result == nullptr)
        Init_Nulled(OUT);
    else {
        Copy_Cell(OUT, result);
        rebRelease(result);
    }

    if (STATE == ST_JS_NATIVE_RUNNING) {
        //
        // is in EM_ASM() code executing right now, will see the update
    }
    else {
        assert(STATE == ST_JS_NATIVE_SUSPENDED);  // needs wakeup
        EM_ASM(
            { setTimeout(function() { reb.m._API_rebIdle_internal(); }, 0); }
        );  // note `_RL` (leading underscore means no cwrap)
    }

    STATE = ST_JS_NATIVE_RESOLVED;
}


// See notes on rebResolveNative()
//
EXTERN_C void API_rebRejectNative_internal(
    intptr_t level_id,
    intptr_t error_id
){
    Level* const L = Level_From_Level_Id(level_id);
    USE_LEVEL_SHORTHANDS (L);

    TRACE("reb.RejectNative_internal(%s)", Level_Label_Or_Anonymous_UTF8(L));

    Value* error = Value_From_Value_Id(error_id);

    if (error == nullptr) {  // Signals halt...not normal error [3]
        TRACE("JavaScript_Dispatcher() => throwing a halt");

        Init_Nulled(OUT);
    }
    else {
        assert(Is_Error(error));
        Copy_Cell(OUT, error);
        rebRelease(error);
    }

    if (STATE == ST_JS_NATIVE_RUNNING) {
        //
        // is in EM_ASM() code executing right now, will see the update
    }
    else {
        assert(STATE == ST_JS_NATIVE_SUSPENDED);  // needs wakeup
        EM_ASM(
            { setTimeout(function() { reb.m._API_rebIdle_internal(); }, 0); }
        );  // note `_RL` (leading underscore means no cwrap)
    }

    STATE = ST_JS_NATIVE_REJECTED;
}


//
//  JavaScript_Dispatcher: C
//
// Called when the ACTION! produced by JS-NATIVE is run.  The tricky bit is
// that it doesn't actually return to the caller when the body of the JS code
// is done running...it has to wait for either the `resolve` or `reject`
// parameter functions to get called.
//
// An AWAITER can only be called during a rebPromise().
//
//////////////////////////////////////////////////////////////////////////////
//
// 1. Whether it's an awaiter or not (e.g. whether it has an `async` JS
//    function as the body), the same interface is used to call the function.
//    It will communicate whether an error happened or not through the
//    `rebResolveNative()` or `rebRejectNative()` either way.  But by the time
//    the JavaScript is finished running for a non-awaiter, a resolve or
//    reject must have happened...awaiters *probably* need more time.
//
// 2. We don't know exactly what JS event is going to trigger and cause a
//    resolve() to happen.  It could be a timer, it could be a fetch(), it
//    could be anything.  Whether you're using a cooperative stackless yield
//    from Ren-C or emscripten's (fattening, slower) Asyncify capability, you
//    pretty much have to use polling.
//
//    (Note: This may make pthreads sound appealing to get pthread_wait(), but
//     that route was tried and fraught with overall complexity.  The cost was
//     likely greater overall than the cost of polling--especially since it
//     often used setTimeout() to accomplish the threading illusions in the
//     first place!)
//
// 3. The GetNativeError_internal() code calls libRebol to build the error,
//    via `reb.Value("make error!", ...)`.  But this means that if the
//    evaluator has had a halt signaled, that would be the code that would
//    convert it to a throw.  For now, the halt signal is communicated
//    uniquely back to us as 0.
//
Bounce JavaScript_Dispatcher(Level* const L)
{
    USE_LEVEL_SHORTHANDS (L);

    heapaddr_t level_id = Level_Id_For_Level(L);

    TRACE(
        "JavaScript_Dispatcher(%s, %d)",
        Level_Label_Or_Anonymous_UTF8(L), STATE
    );

    switch (STATE) {
      case ST_JS_NATIVE_INITIAL_ENTRY :
        goto initial_entry;

      case ST_JS_NATIVE_RUNNING :
        return FAIL(
            "JavaScript_Dispatcher reentry while running, shouldn't happen"
        );

      case ST_JS_NATIVE_SUSPENDED :
        return FAIL(
            "JavaScript_Dispatcher when suspended, needed resolve/reject"
        );

      case ST_JS_NATIVE_RESOLVED :
        goto handle_resolved;

      case ST_JS_NATIVE_REJECTED :
        goto handle_rejected;

      default : assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    Details* details = Phase_Details(PHASE);
    bool is_awaiter = Cell_Logic(Details_At(details, IDX_JS_NATIVE_IS_AWAITER));

    struct Reb_Promise_Info *info = PG_Promises;
    if (is_awaiter) {
        if (info == nullptr)
            return FAIL(
                "JavaScript :AWAITER can only be called from rebPromise()"
            );
        if (info->state != PROMISE_STATE_RUNNING)
            return FAIL(
                "Cannot call JavaScript :AWAITER during another await"
            );
    }
    else
        assert(not info or info->state == PROMISE_STATE_RUNNING);

    heapaddr_t native_id = Native_Id_For_Action(Level_Phase(L));

    STATE = ST_JS_NATIVE_RUNNING;  // resolve/reject change this STATE byte

    EM_ASM(
        { reb.RunNative_internal($0, $1) },
        native_id,  // => $0, how it finds the javascript code to run
        level_id  // => $1, how it knows to find this frame to update STATE
    );

    if (not is_awaiter)  // same tactic for non-awaiter [1]
        assert(STATE != ST_JS_NATIVE_RUNNING);
    else {
        if (STATE == ST_JS_NATIVE_RUNNING) {
            TRACE(
                "JavaScript_Dispatcher(%s) => suspending incomplete awaiter",
                Level_Label_Or_Anonymous_UTF8(L)
            );

            // Note that reb.Halt() can force promise rejection, by way of the
            // triggering of a cancellation signal.  See implementation notes
            // for `reb.CancelAllCancelables_internal()`.
            //
            /* emscripten_sleep(50); */

            STATE = ST_JS_NATIVE_SUSPENDED;
            return BOUNCE_SUSPEND;  // signals trampoline to leave stack
        }
    }

    if (STATE == ST_JS_NATIVE_RESOLVED)
        goto handle_resolved;

    if (STATE == ST_JS_NATIVE_REJECTED)
        goto handle_rejected;

    return FAIL("Unknown frame STATE value after reb.RunNative_internal()");

} handle_resolved: {  ////////////////////////////////////////////////////////

    // 1. This code previously always called the typecheck for the return.
    //    It doesn't seem like it should have.  :-/  Added a check to see
    //    if the phase had a return or not.

    Phase* phase = Level_Phase(L);
    if (ACT_HAS_RETURN(phase)) {  // !!! does it always have RETURN? [1]
        assert(KEY_SYM(ACT_KEYS_HEAD(phase)) == SYM_RETURN);
        const Param* param = ACT_PARAMS_HEAD(phase);

        if (not Typecheck_Coerce_Return_Uses_Spare_And_Scratch(L, param, OUT))
            return FAIL(Error_Bad_Return_Type(L, OUT));
    }

    return OUT;

} handle_rejected: {  ////////////////////////////////////////////////////////

    // !!! Ultimately we'd like to make it so JavaScript code catches the
    // unmodified error that was throw()'n out of the JavaScript, or if
    // Rebol code calls javascript that calls Rebol that errors...it would
    // "tunnel" the error through and preserve the identity as best it
    // could.  But for starters, the transformations are lossy.

    if (Is_Nulled(OUT)) {  // special HALT signal
        //
        // We clear the signal now that we've reacted to it.  (If we did
        // not, then when the console tried to continue running to handle
        // the throw it would have problems.)
        //
        // !!! Is there a good time to do this where we might be able to
        // call GetNativeError_internal()?  Or is this a good moment to
        // know it's "handled"?
        //
        Clear_Trampoline_Flag(HALT);

        return Init_Thrown_With_Label(LEVEL, LIB(NULL), LIB(HALT));
    }

    TRACE("Calling fail() with error context");

    Error* e = Cell_Error(OUT);
    return FAIL(e);
}}


//
//  export /js-native: native [
//
//  "Create ACTION! from textual JavaScript code"
//
//      return: [action?]
//      spec "Function specification (similar to the one used by FUNCTION)"
//          [block!]
//      source "JavaScript code as a text string" [text!]
//      :awaiter "Uses async JS function, invocation will implicitly `await`"
//  ]
//
DECLARE_NATIVE(js_native)
//
// Note: specialized as JS-AWAITER in %ext-javascript-init.reb
{
    INCLUDE_PARAMS_OF_JS_NATIVE;

    Element* spec = cast(Element*, ARG(spec));
    Element* source = cast(Element*, ARG(source));

    VarList* meta;
    Flags flags = MKF_RETURN;
    Array* paramlist = Make_Paramlist_Managed_May_Fail(
        &meta,
        spec,
        &flags
    );

    Phase* native = Make_Action(
        paramlist,
        nullptr,  // no partials
        &JavaScript_Dispatcher,
        IDX_JS_NATIVE_MAX  // details len [source module handle]
    );
    Set_Action_Flag(native, IS_NATIVE);

    assert(ACT_ADJUNCT(native) == nullptr);  // should default to nullptr
    mutable_ACT_ADJUNCT(native) = meta;

    heapaddr_t native_id = Native_Id_For_Action(native);

    Details* details = Phase_Details(native);

    if (Is_Flex_Frozen(Cell_String(source)))  // don't have to copy if frozen
        Copy_Cell(Details_At(details, IDX_JS_NATIVE_SOURCE), source);
    else {
        Init_Text(
            Details_At(details, IDX_JS_NATIVE_SOURCE),
            Copy_String_At(source)  // might change
        );
    }

    // !!! A bit wasteful to use a whole cell for this--could just be whether
    // the ID is positive or negative.  Keep things clear, optimize later.
    //
    Init_Logic(Details_At(details, IDX_JS_NATIVE_IS_AWAITER), REF(awaiter));

  //=//// MAKE ASCII SOURCE FOR JAVASCRIPT FUNCTION ///////////////////////=//

    // 1. A JS-AWAITER can only be triggered from Rebol on the worker thread
    //    as part of a rebPromise().  Making it an async function means it
    //    will return an ES6 Promise, and allows use of the AWAIT JavaScript
    //    feature in the body:
    //
    //      https://javascript.info/async-await
    //
    //    Using plain return within an async function returns a fulfilled
    //    promise while using AWAIT causes the execution to pause and return
    //    a pending promise.  When that promise is fulfilled it will jump back
    //    in and pick up code on the line after that AWAIT.
    //
    // 2. We do not try to auto-translate the Rebol arguments into JS args.
    //    That would make calling it more complex, and introduce several
    //    issues of mapping Rebol names to legal JavaScript identifiers.
    //
    //    Instead, the function receives an updated `reb` API interface, that
    //    is intended to "shadow" the global `reb` interface and override it
    //    during the body of the function.  This local `reb` has a binding
    //    for the JS-NATIVE's frame, such that when reb.Value("argname")
    //    is called, this reb passes that binding through to API_rebValue(),
    //    and the argument can be resolved this way.
    //
    //     !!! There should be some customization here where if the interface
    //     was imported via another name than `reb`, it would be used here.
    //
    // 3. WebAssembly cannot hold onto JavaScript objects directly.  So we
    //    need to store the created function somewhere we can find it later
    //    when it's time to invoke it.  This is done by making a table that
    //    maps a numeric ID (that we *can* hold onto) to the corresponding
    //    JavaScript function entity.

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    Append_Ascii(mo->string, "let f = ");  // variable we store function in

    if (REF(awaiter))
        Append_Ascii(mo->string, "async ");  // run inside rebPromise() [1]

    Append_Ascii(mo->string, "function (reb) {");  // just one arg [2]
    Append_Any_Utf8(mo->string, source);
    Append_Ascii(mo->string, "};\n");  // end `function() {`

    if (REF(awaiter))
        Append_Ascii(mo->string, "f.is_awaiter = true;\n");
    else
        Append_Ascii(mo->string, "f.is_awaiter = false;\n");

    Byte id_buf[60];  // !!! Why 60?  Copied from MF_Integer()
    REBINT len = Emit_Integer(id_buf, native_id);

    Append_Ascii(mo->string, "reb.RegisterId_internal(");  // put in table [3]
    Append_Ascii_Len(mo->string, s_cast(id_buf), len);
    Append_Ascii(mo->string, ", f);\n");

    Term_Binary(mo->string);  // !!! is this necessary?
    const char *js = cs_cast(Binary_At(mo->string, mo->base.size));

    TRACE("Registering native_id %ld", cast(long, native_id));

  //=//// RUN FUNCTION GENERATION (ALSO ADDS TO TABLE) ////////////////////=//

    // The table mapping IDs to JavaScript objects only exists on the main
    // thread.  So in the pthread build, if we're on the worker we have to
    // synchronously wait on the registration.  (Continuing without blocking
    // would be bad--what if they ran the function right after declaring it?)
    //
    // Badly formed JavaScript can cause an error which we want to give back
    // to Rebol.  Since we're going to give it back to Rebol anyway, we go
    // ahead and have the code we run on the main thread translate the JS
    // error object into a Rebol error, so that the handle can be passed
    // back (proxying the JS error object and receiving it in this C call
    // would be more complex).
    //
    // Note: There is no main_thread_emscripten_run_script(), but all that
    // emscripten_run_script() does is call eval() anyway.  :-/
    //
    heapaddr_t error_addr = EM_ASM_INT(
        {
            try {
                eval(UTF8ToString($0));
                return null;
            }
            catch (e) {
                return reb.JavaScriptError(e, $1);
            }
        },
        js,  /* JS code registering the function body (the `$0` parameter) */
        source
    );
    Value* errval = cast(Value*, Pointer_From_Heapaddr(error_addr));
    if (errval) {
        Error* e = Cell_Error(errval);
        rebRelease(errval);  // !!! failing, so not actually needed (?)

        TRACE("JS-NATIVE had malformed JS, calling fail() w/error context");
        return FAIL(e);
    }

    Drop_Mold(mo);

    // !!! Natives on the stack can specify where APIs like reb.Run() should
    // look for bindings.  For the moment, set user natives to use the user
    // context...it could be a parameter of some kind (?)
    //
    Copy_Cell(Details_At(details, IDX_NATIVE_CONTEXT), User_Context_Value);

    // We want this native and its JS Object to GC in the same step--because
    // if the native GC'd without removing its identity from the table, then
    // a new native could come into existence recycling that pointer before
    // the handle could clean up the old ID.  For now, we trust that this
    // native and a HANDLE! resident in its details will GC in the same step.
    //
    Init_Handle_Cdata_Managed(
        Details_At(details, IDX_JS_NATIVE_OBJECT),
        ACT_IDENTITY(native),
        1,  // 0 size interpreted to mean it's a C function
        &cleanup_js_object
    );

    return Init_Action(OUT, native, ANONYMOUS, UNBOUND);
}


//
//  export /js-eval*: native [
//
//  "Evaluate textual JavaScript code"
//
//      return: "Note: Only supports types that reb.Box() supports"
//          [~ ~null~ logic? integer! text!]
//      source "JavaScript code as a text string" [text!]
//      :local "Evaluate in local scope (as opposed to global)"
//      :value "Return a Rebol value"
//  ]
//
DECLARE_NATIVE(js_eval_p)
//
// Note: JS-EVAL is a higher-level routine built on this JS-EVAL* native, that
// can accept a BLOCK! with escaped-in Rebol values, via JS-DO-DIALECT-HELPER.
// In order to make that code easier to change without having to recompile and
// re-ship the JS extension, it lives in a separate script.
//
// !!! If the JS-DO-DIALECT stabilizes it may be worth implementing natively.
{
    INCLUDE_PARAMS_OF_JS_EVAL_P;

    Value* source = ARG(source);

    const char *utf8 = c_cast(char*, Cell_Utf8_At(source));
    heapaddr_t addr;

    // Methods for global evaluation:
    // http://perfectionkills.com/global-eval-what-are-the-options/
    //
    // !!! Note that if `eval()` is redefined, then all invocations will be
    // "indirect" and there will hence be no local evaluations.
    //
    if (REF(value))
        goto want_result;

    if (REF(local))
        addr = EM_ASM_INT(
            { try { eval(UTF8ToString($0)); return 0 }
                catch(e) { return reb.JavaScriptError(e, $1) }
            },
            utf8,
            Heapaddr_From_Pointer(source)
        );
    else
        addr = EM_ASM_INT(
            { try { (1,eval)(UTF8ToString($0)); return 0 }
                catch(e) { return reb.JavaScriptError(e, $1) }
            },
            utf8,
            Heapaddr_From_Pointer(source)
        );

    if (addr == 0)
        return NOTHING;

    goto handle_error;

  want_result: {  ////////////////////////////////////////////////////////////

    // Currently, reb.Box() only translates to INTEGER!, TEXT!, NOTHING?, NULL
    //
    // !!! All other types come back as nothing (~ antiform).  Error instead?
    //
    if (REF(local)) {
        addr = EM_ASM_INT(
            { try { return reb.Box(eval(UTF8ToString($0))) }  // direct (local)
              catch(e) { return reb.JavaScriptError(e, $1) }
            },
            utf8,
            Heapaddr_From_Pointer(source)
        );
    }
    else {
        heapaddr_t addr = EM_ASM_INT(
            { try { return reb.Box((1,eval)(UTF8ToString($0))) }  // indirect
              catch(e) { return reb.JavaScriptError(e, $1) }
            },
            utf8,
            Heapaddr_From_Pointer(source)
        );
    }
    Value* value = Value_From_Value_Id(addr);
    if (not value or not Is_Error(value))
        return value;  // evaluator takes ownership of handle

    goto handle_error;

} handle_error: {  ///////////////////////////////////////////////////////////

    Value* errval = Value_From_Value_Id(addr);
    Error* e = Cell_Error(errval);
    rebRelease(errval);
    return FAIL(e);
}}


//
//  /startup*: native [
//
//  "Initialize the JavaScript Extension"
//
//      return: [~]
//  ]
//
DECLARE_NATIVE(startup_p)
{
    INCLUDE_PARAMS_OF_STARTUP_P;

  #if DEBUG_JAVASCRIPT_EXTENSION
    //
    // See remarks in %load-r3.js about why environment variables are used to
    // control such settings (at least for now) in the early boot process.
    // Once boot is complete, JS-TRACE can be called (if built with JS debug).
    // Emscripten provides ENV to mimic environment variables.
    //
    const char *env_js_trace = getenv("R3_TRACE_JAVASCRIPT");
    if (env_js_trace and atoi(env_js_trace) != 0) {
        PG_JS_Trace = true;
        printf("ENV['R3_TRACE_JAVASCRIPT'] is nonzero...PG_JS_Trace is on\n");
    }
  #endif

    TRACE("INIT-JAVASCRIPT-EXTENSION called");

    return NOTHING;
}


//
//  export /js-trace: native [
//
//  "Internal debug tool for seeing what's going on in JavaScript dispatch"
//
//      return: [~]
//      enable [logic?]
//  ]
//
DECLARE_NATIVE(js_trace)
{
    INCLUDE_PARAMS_OF_JS_TRACE;

  #if DEBUG_JAVASCRIPT_EXTENSION
    PG_Probe_Failures = PG_JS_Trace = Cell_Logic(ARG(enable));
    return NOTHING;
  #else
    return FAIL(
        "JS-TRACE only if DEBUG_JAVASCRIPT_EXTENSION set in %emscripten.r"
    );
  #endif
}


// !!! Need shutdown, but there's currently no module shutdown
//
// https://forum.rebol.info/t/960
