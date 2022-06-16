//
//  File: %mod-javascript.c
//  Summary: "Support for calling Javascript from Rebol in Emscripten build"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2018-2020 Ren-C Open Source Contributors
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
// * This extension expands the RL_rebXXX() API with new entry points.  It
//   was tried to avoid this--doing everything with helper natives.  This
//   would use things like `reb.UnboxInteger("rebpromise-helper", ...)` and
//   build a pure-JS reb.Promise() on top of that.  Initially this was
//   rejected due reb.UnboxInteger() allocating stack for the va_list calling
//   convention...disrupting the "sneaky exit and reentry" done by the
//   Emterpreter.  Now that Emterpreter is replaced with Asyncify, that's
//   not an issue--but it's still faster to have raw WASM entry points like
//   RL_rebPromise_internal().
//
// * If the code block in the EM_ASM() family of functions contains a comma,
//   then wrap the whole code block inside parentheses ().  See the examples
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


#include "sys-core.h"

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
// Ren-C has a very aggressive debug build.  Turning on all the debugging
// means a prohibitive experience in emscripten--not just in size and speed of
// the build products, but the compilation can wind up taking a long time--or
// not succeeding at all).
//
// So most of the system is built with NDEBUG, and no debugging is built
// in for the emscripten build.  The hope is that the core is tested elsewhere
// (or if a bug is encountered in the interpreter under emscripten, it will
// be reproduced and can be debugged in a non-JavaScript build).
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

    EXTERN_C intptr_t RL_rebGetSilentTrace_internal(void) {
      { return cast(intptr_t, cast(void*, PG_Silent_Trace_Buf)); }
#endif

#if DEBUG_JAVASCRIPT_EXTENSION
    #undef assert  // if it was defined (most emscripten builds are NDEBUG)
    #define assert(expr) \
        do { if (!(expr)) { \
            printf("%s:%d - assert(%s)\n", __FILE__, __LINE__, #expr); \
            exit(0); \
        } } while (0)

    static bool PG_JS_Trace = false;  // Turned on/off with JS-TRACE native

    inline static void Javascript_Trace_Helper_Debug(const char *buf) {
        if (PG_JS_Trace) {
            printf("@%ld: %s\n", cast(long, TG_Tick), buf);  // prefix ticks
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

    // TRASH_POINTER_IF_DEBUG() is defined in release builds as a no-op, but
    // it's kind of complicated.  For the purposes in this file these END
    // macros work just as well and don't collide.

    #define ENDIFY_POINTER_IF_DEBUG(p) \
        p = m_cast(REBVAL*, END)

    #define IS_POINTER_END_DEBUG(p) \
        (p == m_cast(REBVAL*, END))

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
    #define ENDIFY_POINTER_IF_DEBUG(p)      NOOP
    #define IS_POINTER_END_DEBUG(p)         NOOP
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

inline static heapaddr_t Heapaddr_From_Pointer(void *p) {
    intptr_t i = cast(intptr_t, cast(void*, p));
    assert(i < UINT_MAX);
    return i;
}

inline static void* Pointer_From_Heapaddr(heapaddr_t addr)
  { return cast(void*, cast(intptr_t, addr)); }

static void cleanup_js_object(const REBVAL *v) {
    heapaddr_t id = Heapaddr_From_Pointer(VAL_HANDLE_VOID_POINTER(v));

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


//=//// FRAME ID AND THROWING /////////////////////////////////////////////=//
//
// We go ahead and use the REBCTX* instead of the raw REBFRM* to act as the
// unique pointer to identify a frame.  That's because if the JavaScript code
// throws and that throw needs to make it to a promise higher up the stack, it
// uses that pointer as an ID in a mapping table to associate the call with
// the JavaScript object it threw.
//
// !!! This aspect is overkill for something that can only happen once on
// the stack at a time.  Review.
//
// !!! Future designs may translate that object into Rebol so it could
// be caught by Rebol, but for now we assume a throw originating from
// JavaScript code may only be caught by JavaScript code.
//

inline static heapaddr_t Frame_Id_For_Frame_May_Outlive_Call(REBFRM* f) {
    REBCTX *frame_ctx = Context_For_Frame_May_Manage(f);
    return Heapaddr_From_Pointer(frame_ctx);
}


//=//// JS-NATIVE PER-ACTION! DETAILS /////////////////////////////////////=//
//
// All Rebol ACTION!s that claim to be natives have to provide a BODY field
// for source, and an ANY-CONTEXT! that indicates where any API calls will
// be bound while that native is on the stack.  For now, if you're writing
// any JavaScript native it will presume binding in the user context.
//
// (A refinement could be added to control this, e.g. JS-NATIVE/CONTEXT.
// But generally the caller of the API can override with their own binding.)
//
// For the JS-native-specific information, it uses a HANDLE!...but only to
// get the GC hook a handle provides.  When a JavaScript native is GC'd, it
// calls into JavaScript to remove the mapping from integer to function that
// was put in that table at the time of creation (the native_id).
//

inline static heapaddr_t Native_Id_For_Action(REBACT *act)
  { return Heapaddr_From_Pointer(ACT_KEYLIST(act)); }

enum {
    IDX_JS_NATIVE_OBJECT = IDX_NATIVE_MAX,
            // ^-- handle gives hookpoint for GC of table entry
    IDX_JS_NATIVE_IS_AWAITER,  // LOGIC! of if this is an awaiter or not
    IDX_JS_NATIVE_MAX
};

REB_R JavaScript_Dispatcher(REBFRM *f);


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

    struct Reb_Promise_Info *next;
};

static struct Reb_Promise_Info *PG_Promises;  // Singly-linked list


enum Reb_Native_State {
    NATIVE_STATE_NONE,
    NATIVE_STATE_RUNNING,
    NATIVE_STATE_RESOLVED,
    NATIVE_STATE_REJECTED
};

// Information cannot be exchanged between the worker thread and the main
// thread via JavaScript values, so they are proxied between threads as
// heap pointers via these globals.
//
static enum Reb_Native_State PG_Native_State;


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
// Note: See %make-reb-lib.r for code that produces the `rebPromise(...)` API,
// which ties the returned integer into the resolve and reject branches of an
// actual JavaScript ES6 Promise.
//
EXTERN_C intptr_t RL_rebPromise(void *p, va_list *vaptr)
{
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
    // then asyncify is incapable of doing it...it's stuck inside the
    // caller's JS stack it can't sleep_with_yield() from).
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

    DECLARE_VA_FEED (feed, p, vaptr, FEED_MASK_DEFAULT);

    REBDSP dsp_orig = DSP;
    while (NOT_END(feed->value)) {
        Derelativize(DS_PUSH(), feed->value, FEED_SPECIFIER(feed));
        SET_CELL_FLAG(DS_TOP, UNEVALUATED);
        Fetch_Next_In_Feed(feed);
    }
    // Note: exhausting feed should take care of the va_end()

    REBARR *code = Pop_Stack_Values(dsp_orig);
    assert(NOT_SERIES_FLAG(code, MANAGED));  // using array as ID, don't GC it

    // We singly link the promises such that they will be executed backwards.
    // What's good about that is that it will help people realize that over
    // the long run, there's no ordering guarantee of promises (e.g. if they
    // were running on individual threads).

    struct Reb_Promise_Info *info = TRY_ALLOC(struct Reb_Promise_Info);
    info->state = PROMISE_STATE_QUEUEING;
    info->promise_id = cast(intptr_t, code);
    info->next = PG_Promises;
    PG_Promises = info;

    EM_ASM(
        { setTimeout(function() { reb.m._RL_rebIdle_internal(); }, 0); }
    );  // note `_RL` (leading underscore means no cwrap)

    return info->promise_id;
}

struct ArrayAndBool {
    REBARR *code;
    bool failed;
};

// Function passed to rebRescue() so code can be run but trap errors safely.
//
REBVAL *Run_Array_Dangerous(void *opaque) {
    struct ArrayAndBool *x = cast(struct ArrayAndBool*, opaque);

    x->failed = true;  // assume it failed if the end was not reached

    REBVAL *v = Alloc_Value();

    REBVAL *first = nullptr;  // optional element to inject before array

    if (Do_At_Mutable_Maybe_Stale_Throws(v, first, x->code, 0, SPECIFIED)) {
        TRACE("Run_Array_Dangerous() is converting a throw to a failure");
        fail (Error_No_Catch_For_Throw(FS_TOP));
    }

    Clear_Stale_Flag(v);
    Reify_Eval_Out_Plain(v);

    x->failed = false;  // Since end was reached, it did not fail...

    if (IS_NULLED(v))  // don't leak API cell with nulled in it
        return nullptr;

    return v;
}


void RunPromise(void)
{
    struct Reb_Promise_Info *info = PG_Promises;
    assert(info->state == PROMISE_STATE_QUEUEING);
    info->state = PROMISE_STATE_RUNNING;

    REBARR *code = ARR(Pointer_From_Heapaddr(info->promise_id));
    assert(NOT_SERIES_FLAG(code, MANAGED));  // took off so it didn't GC
    SET_SERIES_FLAG(code, MANAGED);  // but need it back on to execute it

    // We run the code using rebRescue() so that if there are errors, we
    // will be able to trap them.  the difference between `throw()`
    // and `reject()` in JS is subtle.
    //
    // https://stackoverflow.com/q/33445415/

    struct ArrayAndBool x;  // bool needed to know if it failed
    x.code = code;
    REBVAL *result = rebRescue(&Run_Array_Dangerous, &x);
    TRACE("RunPromise() finished Run_Array_Dangerous()");
    assert(not result or not IS_NULLED(result));  // NULL is nullptr in API

    if (info->state == PROMISE_STATE_REJECTED) {
        assert(IS_FRAME(result));
        TRACE("RunPromise() => promise is rejecting due to...something (?)");

        // Note: Expired, can't use VAL_CONTEXT
        //
        assert(IS_FRAME(result));
        REBNOD *frame_ctx = VAL_NODE1(result);
        heapaddr_t throw_id = Heapaddr_From_Pointer(frame_ctx);

        EM_ASM(
            { reb.RejectPromise_internal($0, $1); },
            info->promise_id,  // => $0 (table entry will be freed)
            throw_id  // => $1 (table entry will be freed)
        );
    }
    else {
        assert(info->state == PROMISE_STATE_RUNNING);

        if (x.failed) {
            //
            // Note this could be an uncaught throw error, raised by the
            // Run_Array_Dangerous() itself...or a failure rebRescue()
            // caught...
            //
            assert(IS_ERROR(result));
            info->state = PROMISE_STATE_REJECTED;
            TRACE("RunPromise() => promise is rejecting due to error");
        }
        else {
            info->state = PROMISE_STATE_RESOLVED;
            TRACE("RunPromise() => promise is resolving");

            EM_ASM(
                { reb.ResolvePromise_internal($0, $1); },
                info->promise_id,  // => $0 (table entry will be freed)
                result  // => $1 (recipient takes over handle)
            );
        }
    }

    rebRelease(result);

    assert(PG_Promises == info);
    PG_Promises = info->next;
    FREE(struct Reb_Promise_Info, info);
}


//
// Until the stackless build is implemented, rebPromise() must defer its
// execution until there is no JavaScript above it or after it on the stack.
//
// Inside this call, emscripten_sleep() can sneakily make us fall through
// to the main loop.  We don't notice it here--it's invisible to the C
// code being yielded.  -BUT- the JS callsite for rebIdle() would
// notice, as it would seem rebIdle() had finished...when really what's
// happening is that the instrumented WASM is putting itself into
// suspended animation--which it will come out of via a setTimeout.
//
// (This is why there shouldn't be any meaningful JS on the stack above
// this besides the rebIdle() call itself.)
//
EXTERN_C void RL_rebIdle_internal(void)  // NO user JS code on stack!
{
    TRACE("rebIdle() => begin running promise code");

    // In stackless, we'd have some protocol by which RunPromise() could get
    // started in rebPromise(), then maybe be continued here.  For now, it
    // is always continued here.
    //
    RunPromise();

    TRACE("rebIdle() => finished running promise code");
}


// This is rebSignalResolveNative() and not rebResolveNative() which passes in
// a value to resolve with, because the emterpreter build can't really pass a
// REBVAL*.   All the APIs it would need to make REBVAL* are unavailable.  So
// it instead pokes a JavaScript function where it can be found when no longer
// in emscripten_sleep().
//
EXTERN_C void RL_rebSignalResolveNative_internal(intptr_t frame_id) {
    TRACE("reb.SignalResolveNative_internal()");

    assert(PG_Native_State == NATIVE_STATE_RUNNING);
    PG_Native_State = NATIVE_STATE_RESOLVED;
}


// See notes on rebSignalResolveNative()
//
EXTERN_C void RL_rebSignalRejectNative_internal(intptr_t frame_id) {
    TRACE("reb.SignalRejectNative_internal()");

    assert(PG_Native_State == NATIVE_STATE_RUNNING);
    PG_Native_State = NATIVE_STATE_REJECTED;
}


//
//  JavaScript_Dispatcher: C
//
// Called when the ACTION! produced by JS-NATIVE is run.  The tricky bit is
// that it doesn't actually return to the caller when the body of the JS code
// is done running...it has to wait for either the `resolve` or `reject`
// parameter functions to get called.
//
// An AWAITER can only be called inside a rebPromise().
//
REB_R JavaScript_Dispatcher(REBFRM *frame_)
{
    REBFRM *f = frame_;

    heapaddr_t native_id = Native_Id_For_Action(FRM_PHASE(f));
    heapaddr_t frame_id = Frame_Id_For_Frame_May_Outlive_Call(f);

    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    bool is_awaiter = VAL_LOGIC(ARR_AT(details, IDX_JS_NATIVE_IS_AWAITER));

    TRACE("JavaScript_Dispatcher(%s)", Frame_Label_Or_Anonymous_UTF8(f));

    struct Reb_Promise_Info *info = PG_Promises;
    if (is_awaiter) {
        if (info == nullptr)
            fail ("JavaScript /AWAITER can only be called from rebPromise()");
        if (info->state != PROMISE_STATE_RUNNING)
            fail ("Cannot call JavaScript /AWAITER during another await");
    }
    else
        assert(not info or info->state == PROMISE_STATE_RUNNING);

    if (PG_Native_State != NATIVE_STATE_NONE)
        assert(!"Cannot call JS-NATIVE during JS-NATIVE at this time");

    PG_Native_State = NATIVE_STATE_RUNNING;

    // Whether it's an awaiter or not (e.g. whether it has an `async` JS
    // function as the body), the same interface is used to call the function.
    // It will communicate whether an error happened or not through the
    // `rebSignalResolveNative()` or `rebSignalRejectNative()` either way,
    // and the results are fetched with the same mechanic.

    EM_ASM(
        { reb.RunNative_internal($0, $1) },
        native_id,  // => $0
        frame_id  // => $1
    );

    // We don't know exactly what JS event is going to trigger and cause a
    // resolve() to happen.  It could be a timer, it could be a fetch(),
    // it could be anything.  The Asyncify build doesn't really have a choice
    // other than to poll...there's no pthread wait conditions available.
    //
    // (Note: While this may make pthreads sound appealing, that route was
    // tried and fraught with overall complexity.  The cost was likely greater
    // overall than the cost of polling--especially since it often used
    // setTimeout() to accomplish the threading illusions in the first place!)
    //
    // We wait at least 50msec (probably more, as we don't control how long
    // the JS will be running whatever it does).
    //
    TRACE("JavaScript_Dispatcher() => begin emscripten_sleep() loop");
    while (PG_Native_State == NATIVE_STATE_RUNNING) {  // !!! volatile?
        //
        // Note that reb.Halt() can force promise rejection, by way of the
        // triggering of a cancellation signal.  See implementation notes for
        // `reb.CancelAllCancelables_internal()`.
        //
        emscripten_sleep(50);
    }
    TRACE("JavaScript_Dispatcher() => end emscripten_sleep() loop");

    // The protocol for JavaScript returning Ren-C API values to Ren-C is to
    // do so with functions that either "resolve" (succeed) or "reject"
    // (e.g. fail).  Even non-async functions use the callbacks, so that they
    // can signal a failure bubbling up out of them as distinct from success.

    if (PG_Native_State == NATIVE_STATE_REJECTED) {
        //
        // !!! Ultimately we'd like to make it so JavaScript code catches the
        // unmodified error that was throw()'n out of the JavaScript, or if
        // Rebol code calls javascript that calls Rebol that errors...it would
        // "tunnel" the error through and preserve the identity as best it
        // could.  But for starters, the transformations are lossy.

        PG_Native_State = NATIVE_STATE_NONE;

        // !!! The GetNativeError_internal() code calls libRebol to build the
        // error, via `reb.Value("make error!", ...)`.  But this means that
        // if the evaluator has had a halt signaled, that would be the code
        // that would convert it to a throw.  For now, the halt signal is
        // communicated uniquely back to us as 0.
        //
        heapaddr_t error_addr = EM_ASM_INT(
            { return reb.GetNativeError_internal($0) },
            frame_id  // => $0
        );

        if (error_addr == 0) { // !!! signals a halt...not a normal error
            TRACE("JavaScript_Dispatcher() => throwing a halt");

            // We clear the signal now that we've reacted to it.  (If we did
            // not, then when the console tried to continue running to handle
            // the throw it would have problems.)
            //
            // !!! Is there a good time to do this where we might be able to
            // call GetNativeError_internal()?  Or is this a good moment to
            // know it's "handled"?
            //
            CLR_SIGNAL(SIG_HALT);

            return Init_Thrown_With_Label(FRAME, Lib(NULL), Lib(HALT));
        }

        REBVAL *error = VAL(Pointer_From_Heapaddr(error_addr));
        REBCTX *ctx = VAL_CONTEXT(error);
        rebRelease(error);  // !!! failing, so not actually needed (?)

        TRACE("Calling fail() with error context");
        fail (ctx);
    }

    assert(PG_Native_State == NATIVE_STATE_RESOLVED);

    heapaddr_t result_addr = EM_ASM_INT(
        { return reb.GetNativeResult_internal($0) },
        frame_id  // => $0
    );

    REBVAL *native_result = VAL(Pointer_From_Heapaddr(result_addr));

    if (native_result == nullptr)
        Init_Nulled(OUT);
    else {
        assert(not IS_NULLED(native_result));  // API uses nullptr only
        Copy_Cell(OUT, native_result);
        rebRelease(native_result);
    }

    PG_Native_State = NATIVE_STATE_NONE;

    FAIL_IF_BAD_RETURN_TYPE(f);
    return OUT;
}


//
//  export js-native: native [
//
//  {Create ACTION! from textual JavaScript code}
//
//      return: [action!]
//      spec "Function specification (similar to the one used by FUNCTION)"
//          [block!]
//      source "JavaScript code as a text string" [text!]
//      /awaiter "Uses async JS function, invocation will implicitly `await`"
//  ]
//
REBNATIVE(js_native)
//
// Note: specialized as JS-AWAITER in %ext-javascript-init.reb
{
    JAVASCRIPT_INCLUDE_PARAMS_OF_JS_NATIVE;

    REBVAL *spec = ARG(spec);
    REBVAL *source = ARG(source);

    REBCTX *meta;
    REBFLGS flags = MKF_RETURN | MKF_KEYWORDS;
    REBARR *paramlist = Make_Paramlist_Managed_May_Fail(
        &meta,
        spec,
        &flags
    );

    REBACT *native = Make_Action(
        paramlist,
        nullptr,  // no partials
        &JavaScript_Dispatcher,
        IDX_JS_NATIVE_MAX  // details len [source module handle]
    );
    SET_ACTION_FLAG(native, IS_NATIVE);

    assert(ACT_META(native) == nullptr);  // should default to nullptr
    mutable_ACT_META(native) = meta;

    heapaddr_t native_id = Native_Id_For_Action(native);

    REBARR *details = ACT_DETAILS(native);

    if (Is_Series_Frozen(VAL_SERIES(source)))
        Copy_Cell(ARR_AT(details, IDX_NATIVE_BODY), source);  // no copy
    else {
        Init_Text(
            ARR_AT(details, IDX_NATIVE_BODY),
            Copy_String_At(source)  // might change
        );
    }

    // !!! A bit wasteful to use a whole cell for this--could just be whether
    // the ID is positive or negative.  Keep things clear, optimize later.
    //
    Init_Logic(ARR_AT(details, IDX_JS_NATIVE_IS_AWAITER), REF(awaiter));

    // The generation of the function called by JavaScript.  It takes no
    // arguments, as giving it arguments would make calling it more complex
    // as well as introduce several issues regarding mapping legal Rebol
    // names to names for JavaScript parameters.  libRebol APIs must be used
    // to access the arguments out of the frame.

    DECLARE_MOLD (mo);
    Push_Mold(mo);

    Append_Ascii(mo->series, "let f = ");  // variable we store function in

    // A JS-AWAITER can only be triggered from Rebol on the worker thread as
    // part of a rebPromise().  Making it an async function means it will
    // return an ES6 Promise, and allows use of the AWAIT JavaScript feature
    // inside the body:
    //
    // https://javascript.info/async-await
    //
    // Using plain return inside an async function returns a fulfilled promise
    // while using AWAIT causes the execution to pause and return a pending
    // promise.  When that promise is fulfilled it will jump back in and
    // pick up code on the line after that AWAIT.
    //
    if (REF(awaiter))
        Append_Ascii(mo->series, "async ");

    // We do not try to auto-translate the Rebol arguments into JS args.  It
    // would make calling it more complex, and introduce several issues of
    // mapping Rebol names to legal JavaScript identifiers.  reb.Arg() or
    // reb.ArgR() must be used to access the arguments out of the frame.
    //
    Append_Ascii(mo->series, "function () {");
    Append_String(mo->series, source);
    Append_Ascii(mo->series, "};\n");  // end `function() {`

    if (REF(awaiter))
        Append_Ascii(mo->series, "f.is_awaiter = true;\n");
    else
        Append_Ascii(mo->series, "f.is_awaiter = false;\n");

    REBYTE id_buf[60];  // !!! Why 60?  Copied from MF_Integer()
    REBINT len = Emit_Integer(id_buf, native_id);

    // Rebol cannot hold onto JavaScript objects directly, so there has to be
    // a table mapping some numeric ID (that we *can* hold onto) to the
    // corresponding JS function entity.
    //
    Append_Ascii(mo->series, "reb.RegisterId_internal(");
    Append_Ascii_Len(mo->series, s_cast(id_buf), len);
    Append_Ascii(mo->series, ", f);\n");

    // The javascript code for registering the function body is now the last
    // thing in the mold buffer.  Get a pointer to it.
    //
    TERM_BIN(mo->series);  // !!! is this necessary?
    const char *js = cs_cast(BIN_AT(mo->series, mo->offset));

    TRACE("Registering native_id %ld", cast(long, native_id));

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
                return reb.Value("make error!", reb.T(e.toString()));
            }
        },
        js  /* JS code registering the function body (the `$0` parameter) */
    );
    REBVAL *error = cast(REBVAL*, Pointer_From_Heapaddr(error_addr));
    if (error) {
        REBCTX *ctx = VAL_CONTEXT(error);
        rebRelease(error);  // !!! failing, so not actually needed (?)

        TRACE("JS-NATIVE had malformed JS, calling fail() w/error context");
        fail (ctx);
    }

    Drop_Mold(mo);

    // !!! Natives on the stack can specify where APIs like reb.Run() should
    // look for bindings.  For the moment, set user natives to use the user
    // context...it could be a parameter of some kind (?)
    //
    Copy_Cell(ARR_AT(details, IDX_NATIVE_CONTEXT), User_Context_Value);

    Init_Handle_Cdata_Managed(
        ARR_AT(details, IDX_JS_NATIVE_OBJECT),
        ACT_KEYLIST(native),
        0,
        &cleanup_js_object
    );

    return Init_Action(OUT, native, ANONYMOUS, UNBOUND);
}


//
//  export js-eval*: native [
//
//  {Evaluate textual JavaScript code}
//
//      return: "Note: Only supports types that reb.Box() supports"
//          [<opt> integer! text! bad-word!]
//      source "JavaScript code as a text string" [text!]
//      /local "Evaluate in local scope (as opposed to global)"
//      /value "Return a Rebol value"
//  ]
//
REBNATIVE(js_eval_p)
//
// Note: JS-EVAL is a higher-level routine built on this JS-EVAL* native, that
// can accept a BLOCK! with escaped-in Rebol values, via JS-DO-DIALECT-HELPER.
// In order to make that code easier to change without having to recompile and
// re-ship the JS extension, it lives in a separate script.
//
// !!! If the JS-DO-DIALECT stabilizes it may be worth implementing natively.
{
    JAVASCRIPT_INCLUDE_PARAMS_OF_JS_EVAL_P;

    const char *utf8 = s_cast(VAL_UTF8_AT(ARG(source)));

    // Methods for global evaluation:
    // http://perfectionkills.com/global-eval-what-are-the-options/
    //
    // !!! Note that if `eval()` is redefined, then all invocations will be
    // "indirect" and there will hence be no local evaluations.
    //
    if (not REF(value)) {
        if (REF(local))
            EM_ASM(
                { eval(UTF8ToString($0)) },
                utf8
            );  // !!! ...should be an else clause here...
        // !!! However, there's an emscripten bug, so use two `if`s instead
        // https://github.com/emscripten-core/emscripten/issues/11539
        //
        if (not REF(local))
            EM_ASM(
                { (1,eval)(UTF8ToString($0)) },
                utf8
            );

        return NONE;
    }

    // Currently, reb.Box() only translates to INTEGER!, TEXT!, BAD-WORD!, NULL
    //
    // !!! All other types come back as BAD-WORD!.  Should they error?
    //
    heapaddr_t addr;
    if (REF(local)) {
        addr = EM_ASM_INT(
            { return reb.Box(eval(UTF8ToString($0))) },  // direct (local)
            utf8
        );
    }
    else {
        heapaddr_t addr = EM_ASM_INT(
            { return reb.Box((1,eval)(UTF8ToString($0))) },  // indirect
            utf8
        );
    }
    return cast(REBVAL*, addr);  // evaluator takes ownership of handle
}


//
//  startup*: native [
//
//  {Initialize the JavaScript Extension}
//
//      return: <none>
//  ]
//
REBNATIVE(startup_p)
{
    JAVASCRIPT_INCLUDE_PARAMS_OF_STARTUP_P;

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

    PG_Native_State = NATIVE_STATE_NONE;

    return NONE;
}


//
//  export js-trace: native [
//
//  {Internal debug tool for seeing what's going on in JavaScript dispatch}
//
//      return: <none>
//      enable [logic!]
//  ]
//
REBNATIVE(js_trace)
{
    JAVASCRIPT_INCLUDE_PARAMS_OF_JS_TRACE;

  #if DEBUG_JAVASCRIPT_EXTENSION
    PG_Probe_Failures = PG_JS_Trace = VAL_LOGIC(ARG(enable));
  #else
    fail ("JS-TRACE only if DEBUG_JAVASCRIPT_EXTENSION set in %emscripten.r");
  #endif

    return NONE;
}


//
//  export js-stacklimit: native [
//
//  {Internal tracing tool reporting the stack level and how long to limit}
//
//      return: [block!]
//  ]
//
REBNATIVE(js_stacklimit)
{
    JAVASCRIPT_INCLUDE_PARAMS_OF_JS_STACKLIMIT;

    REBDSP dsp_orig = DSP;

    Init_Integer(DS_PUSH(), cast(uintptr_t, &dsp_orig));  // local pointer
    Init_Integer(DS_PUSH(), TG_Stack_Limit);
    return Init_Block(OUT, Pop_Stack_Values(dsp_orig));
}


// !!! Need shutdown, but there's currently no module shutdown
//
// https://forum.rebol.info/t/960
