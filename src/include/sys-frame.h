//
//  File: %sys-frame.h
//  Summary: {Accessors and Argument Pushers/Poppers for Function Call Frames}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
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


//=////////////////////////////////////////////////////////////////////////=//
//
//  THROWN status
//
//=////////////////////////////////////////////////////////////////////////=//
//
// All THROWN values have two parts: the REBVAL arg being thrown and
// a REBVAL indicating the /NAME of a labeled throw.  (If the throw was
// created with plain THROW instead of THROW/NAME then its name is NONE!).
// You cannot fit both values into a single value's bits of course, but
// since only one THROWN() value is supposed to exist on the stack at a
// time the arg part is stored off to the side when one is produced
// during an evaluation.  It must be processed before another evaluation
// is performed, and if the GC or DO are ever given a value with a
// THROWN() bit they will assert!
//
// A reason to favor the name as "the main part" is that having the name
// value ready-at-hand allows easy testing of it to see if it needs
// to be passed on.  That happens more often than using the arg, which
// will occur exactly once (when it is caught).
//

#define THROWN(v) \
    GET_VAL_FLAG((v), VALUE_FLAG_THROWN)

static inline void CONVERT_NAME_TO_THROWN(
    REBVAL *name, const REBVAL *arg
){
    assert(!THROWN(name));
    SET_VAL_FLAG(name, VALUE_FLAG_THROWN);

    ASSERT_UNREADABLE_IF_DEBUG(&TG_Thrown_Arg);

    Move_Value(&TG_Thrown_Arg, arg);
}

static inline void CATCH_THROWN(REBVAL *arg_out, REBVAL *thrown) {
    //
    // Note: arg_out and thrown may be the same pointer
    //
    assert(THROWN(thrown));
    CLEAR_VAL_FLAG(thrown, VALUE_FLAG_THROWN);

    ASSERT_READABLE_IF_DEBUG(&TG_Thrown_Arg);
    Move_Value(arg_out, &TG_Thrown_Arg);
    Init_Unreadable_Blank(&TG_Thrown_Arg);
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  LOW-LEVEL FRAME ACCESSORS
//
//=////////////////////////////////////////////////////////////////////////=//

#define FS_TOP (TG_Frame_Stack + 0) // avoid assignment to FS_TOP via + 0

#define FRM_IS_VALIST(f) \
    ((f)->source.vaptr != NULL)

#define FRM_AT_END(f) \
    ((f)->value == NULL)

#define FRM_HAS_MORE(f) \
    ((f)->value != NULL)

inline static REBARR *FRM_ARRAY(REBFRM *f) {
    assert(!FRM_IS_VALIST(f));
    return f->source.array;
}

// !!! Though the evaluator saves its `index`, the index is not meaningful
// in a valist.  Also, if `opt_head` values are used to prefetch before an
// array, those will be lost too.  A true debugging mode would need to
// convert these cases to ordinary arrays before running them, in order
// to accurately present any errors.
//
inline static REBCNT FRM_INDEX(REBFRM *f) {
    assert(!FRM_IS_VALIST(f));
    return FRM_AT_END(f)
        ? ARR_LEN(f->source.array)
        : f->source.index - 1;
}

inline static REBCNT FRM_EXPR_INDEX(REBFRM *f) {
    assert(!FRM_IS_VALIST(f));
    return f->expr_index == END_FLAG
        ? ARR_LEN((f)->source.array)
        : f->expr_index - 1;
}

inline static REBSTR* FRM_FILE(REBFRM *f) {
    //
    // !!! the rebRun function could be a variadic macro in C99 or higher, as
    // `rebRunFileLine(__FILE__, __LINE__, ...`.  This could let the file and
    // line information make it into the frame, and be used when loading new
    // source material -or- if no source material were loaded, it could just
    // be kept as a UTF-8 string inside the frame without needing interning
    // as a series.  But for now, just signal that it came from C code.
    //
    if (f->source.array == NULL)
        return Canon(SYM___ANONYMOUS__);

    if (NOT_SER_FLAG(f->source.array, ARRAY_FLAG_FILE_LINE))
        return Canon(SYM___ANONYMOUS__);

    return LINK(f->source.array).file;
}

inline static const char* FRM_FILE_UTF8(REBFRM *f) {
    return STR_HEAD(FRM_FILE(f));
}

inline static int FRM_LINE(REBFRM *f) {
    if (f->source.array == NULL)
        return 0;

    if (NOT_SER_FLAG(f->source.array, ARRAY_FLAG_FILE_LINE))
        return 0;

    return MISC(SER(f->source.array)).line;
}

#define FRM_OUT(f) \
    cast(REBVAL * const, (f)->out) // writable Lvalue


// Note about FRM_NUM_ARGS: A native should generally not detect the arity it
// was invoked with, (and it doesn't make sense as most implementations get
// the full list of arguments and refinements).  However, ACTION! dispatch
// has several different argument counts piping through a switch, and often
// "cheats" by using the arity instead of being conditional on which action
// ID ran.  Consider when reviewing the future of ACTION!.
//
#define FRM_NUM_ARGS(f) \
    ACT_FACADE_NUM_PARAMS((f)->phase)

inline static REBVAL *FRM_CELL(REBFRM *f) {
    //
    // An earlier optimization would use the frame's cell if a function
    // took exactly one argument for that argument.  This meant it was not
    // available to those functions to use as a GC-protected temporary.  The
    // optimization made it complex for the generalized code that does
    // stack level discovery from a value pointer, and was removed.
    //
    return KNOWN(&f->cell);
}

#define FRM_PRIOR(f) \
    ((f)->prior)

inline static REBACT *FRM_UNDERLYING(REBFRM *f) {
    assert(ACT_UNDERLYING(f->phase) == ACT_UNDERLYING(f->original));
    return ACT_UNDERLYING(f->phase);
}

#define FRM_DSP_ORIG(f) \
    ((f)->dsp_orig + 0) // Lvalue

// `arg` is in use to point at the arguments during evaluation, and `param`
// may hold a SET-WORD! or SET-PATH! available for a lookback to quote.
// But during evaluations, `refine` is free.
//
// Since the GC is aware of the pointers, it can protect whatever refine is
// pointing at.  This can be useful for routines that have a local
// memory cell.  This does not require a push or a pop of anything--it only
// protects as long as the native is running.  (This trick is available to
// the dispatchers as well.)
//
#define PROTECT_FRM_X(f,v) \
    ((f)->refine = (v))


// ARGS is the parameters and refinements
// 1-based indexing into the arglist (0 slot is for object/function value)
#ifdef NDEBUG
    #define FRM_ARG(f,n) \
        ((f)->args_head + (n) - 1)
#else
    inline static REBVAL *FRM_ARG(REBFRM *f, REBCNT n) {
        assert(n != 0 and n <= FRM_NUM_ARGS(f));

        REBVAL *var = &f->args_head[n - 1];

        assert(!THROWN(var));
        assert(not IS_RELATIVE(cast(RELVAL*, var)));
        return var;
    }
#endif


// Quick access functions from natives (or compatible functions that name a
// Reb_Frame pointer `frame_`) to get some of the common public fields.
//
#define D_FRAME     frame_
#define D_OUT       FRM_OUT(frame_)         // GC-safe slot for output value
#define D_CELL      FRM_CELL(frame_)        // GC-safe cell if > 1 argument
#define D_ARGC      FRM_NUM_ARGS(frame_)    // count of args+refinements/args
#define D_ARG(n)    FRM_ARG(frame_, (n))    // pass 1 for first arg

#define D_PROTECT_X(v)      PROTECT_FRM_X(frame_, (v))

inline static REBOOL Is_Action_Frame(REBFRM *f) {
    if (f->eval_type == REB_ACTION) {
        //
        // Do not count as a function frame unless its gotten to the point
        // of pushing arguments.
        //
        return f->phase != NULL;
    }
    return FALSE;
}

// While a function frame is fulfilling its arguments, the `f->param` will
// be pointing to a typeset.  The invariant that is maintained is that
// `f->param` will *not* be a typeset when the function is actually in the
// process of running.  (So no need to set/clear/test another "mode".)
//
inline static REBOOL Is_Action_Frame_Fulfilling(REBFRM *f)
{
    assert(Is_Action_Frame(f));
    return NOT_END(f->param);
}


inline static void Get_Frame_Label_Or_Blank(REBVAL *out, REBFRM *f) {
    assert(f->eval_type == REB_ACTION);
    if (f->opt_label != NULL)
        Init_Word(out, f->opt_label); // invoked via WORD! or PATH!
    else
        Init_Blank(out); // anonymous invocation
}

inline static const char* Frame_Label_Or_Anonymous_UTF8(REBFRM *f) {
    assert(f->eval_type == REB_ACTION);
    if (f->opt_label != NULL)
        return STR_HEAD(f->opt_label);
    return "[anonymous]";
}

inline static void SET_FRAME_VALUE(REBFRM *f, const RELVAL* value) {
    assert(f->gotten == END); // is fetched f->value, we'd be invalidating it!

    if (IS_END(value))
        f->value = NULL;
    else
        f->value = value;
}



//=////////////////////////////////////////////////////////////////////////=//
//
//  ARGUMENT AND PARAMETER ACCESS HELPERS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// These accessors are designed to make it convenient for natives written in
// C to access their arguments and refinements.  (They are what is behind the
// implementation of the INCLUDE_PARAMS_OF_XXX macros that are used in
// natives.)
//
// They capture the implicit Reb_Frame* passed to every REBNATIVE ('frame_')
// and read the information out cleanly, like this:
//
//     PARAM(1, foo);
//     REFINE(2, bar);
//
//     if (IS_INTEGER(ARG(foo)) and REF(bar)) { ... }
//
// Though REF can only be used with a REFINE() declaration, ARG can be used
// with either.  By contract, Rebol functions are allowed to mutate their
// arguments and refinements just as if they were locals...guaranteeing only
// their return result as externally visible.  Hence the ARG() cell for a
// refinement provides a GC-safe slot for natives to hold values once they
// have observed what they need from the refinement.
//
// Under the hood `PARAM(1, foo)` and `REFINE(2, bar)` are const values in
// the release build.  Under optimization they disappear completely, so that
// addressing is done directly into the call frame's cached `arg` pointer.
// It is also possible to get the typeset-with-symbol for a particular
// parameter or refinement, e.g. with `PAR(foo)` or `PAR(bar)`.
//
// The PARAM and REFINE macros use token pasting to name the variables they
// are declaring `p_name` instead of just `name`.  This prevents collisions
// with C/C++ identifiers, so PARAM(case) and REFINE(new) would make `p_case`
// and `p_new` instead of just `case` and `new` as the variable names.  (This
// is only visible in the debugger.)
//
// As a further aid, the debug build version of the structures contain the
// actual pointers to the arguments.  It also keeps a copy of a cache of the
// type for the arguments, because the numeric type encoding in the bits of
// the header requires a debug call (or by-hand-binary decoding) to interpret
// Whether a refinement was used or not at time of call is also cached.
//

#ifdef NDEBUG
    #define PARAM(n,name) \
        static const int p_##name = n

    #define REFINE(n,name) \
        static const int p_##name = n

    #define ARG(name) \
        FRM_ARG(frame_, (p_##name))

    #define PAR(name) \
        ACT_PARAM(frame_->phase, (p_##name)) /* a TYPESET! */

    #define REF(name) \
        IS_TRUTHY(ARG(name))
#else
    struct Native_Param {
        int num;
        enum Reb_Kind kind_cache; // for inspecting in watchlist
        REBVAL *arg; // for inspecting in watchlist
    };

    struct Native_Refine {
        int num;
        REBOOL used_cache; // for inspecting in watchlist
        REBVAL *arg; // for inspecting in watchlist
    };

    // Note: Assigning non-const initializers to structs, e.g. `= {var, f()};`
    // is a non-standard extension to C.  So we break out the assignments.

    #define PARAM(n,name) \
        struct Native_Param p_##name; \
        p_##name.num = (n); \
        p_##name.kind_cache = VAL_TYPE(FRM_ARG(frame_, (n))); \
        p_##name.arg = FRM_ARG(frame_, (n)); \

    #define REFINE(n,name) \
        struct Native_Refine p_##name; \
        p_##name.num = (n); \
        p_##name.used_cache = IS_TRUTHY(FRM_ARG(frame_, (n))); \
        p_##name.arg = FRM_ARG(frame_, (n)); \

    #define ARG(name) \
        FRM_ARG(frame_, (p_##name).num)

    #define PAR(name) \
        ACT_PARAM(frame_->phase, (p_##name).num) /* a TYPESET! */

    #define REF(name) \
        ((p_##name).used_cache /* used_cache use stops REF() on PARAM()s */ \
            ? IS_TRUTHY(ARG(name)) \
            : IS_TRUTHY(ARG(name)))
#endif


// The native entry prelude makes sure that once native code starts running,
// then a reified frame will be locked or a non-reified frame will be flagged
// in such a way as to indicate that it should be locked when reified.  This
// prevents a FRAME! generated for a native from being able to get write
// access to the variables, which could cause crashes, as raw C code is not
// insulated against having bit patterns for types in cells that aren't
// expected.
//
// !!! Debug injection of bad types into usermode code may cause havoc as
// well, and should be considered a security/permissions issue.  It just won't
// (or shouldn't) crash the evaluator itself.
//
// This is automatically injected by the INCLUDE_PARAMS_OF_XXX macros.  The
// reason this is done with code inlined into the native itself instead of
// based on an IS_NATIVE() test is to avoid the cost of the testing--which
// is itself a bit dodgy to tell a priori if a dispatcher is native or not.
// This way there is no test and only natives pay the cost of flag setting.
//
inline static void Enter_Native(REBFRM *f) {
    f->flags.bits |= DO_FLAG_NATIVE_HOLD;
    if (f->varlist != NULL)
        SET_SER_INFO(f->varlist, SERIES_INFO_HOLD);
}


// Allocate the series of REBVALs inspected by a function when executed (the
// values behind ARG(name), REF(name), D_ARG(3),  etc.)
//
// This only allocates space for the arguments, it does not initialize.
// Do_Core initializes as it goes, and updates f->param so the GC knows how
// far it has gotten so as not to see garbage.  APPLY has different handling
// when it has to build the frame for the user to write to before running;
// so Do_Core only checks the arguments, and does not fulfill them.
//
// If the function is a specialization, then the parameter list of that
// specialization will have *fewer* parameters than the full function would.
// For this reason we push the arguments for the "underlying" function.
// Yet if there are specialized values, they must be filled in from the
// exemplar frame.
//
// Rather than "dig" through layers of functions to find the underlying
// function or the specialization's exemplar frame, those properties are
// cached during the creation process.
//
inline static void Push_Action(
    REBFRM *f,
    REBSTR *opt_label,
    REBACT *a,
    REBSPC *binding
){
    f->eval_type = REB_ACTION;

    assert(
        opt_label == NULL
        or GET_SER_FLAG(opt_label, SERIES_FLAG_UTF8_STRING)
    );
    assert(IS_POINTER_TRASH_DEBUG(f->opt_label)); // only valid w/REB_ACTION
    f->opt_label = opt_label;

  #if defined(DEBUG_FRAME_LABELS)
    //
    // It's helpful when looking in the debugger to be able to look at a frame
    // and see a cached string for the function it's running.
    //
    f->label_utf8 = cast(const char*, Frame_Label_Or_Anonymous_UTF8(f));
  #endif

    f->original = f->phase = a;

    f->binding = binding; // e.g. how a RETURN knows where to return to

    // The underlying function is who the frame is *ultimately* being built
    // for.  This underlying function can have more arguments than the
    // original "interface" function being called.  Consider that even if you
    // make a specialization of APPEND that no longer has any parameters,
    // eventually the C code for REBNATIVE(append) will be executed to do
    // the work.  And it will expect the ARG() and REF() macros to find the
    // right arguments at the right indices.
    //
    // The "facade" is the interface this function uses, which must have the
    // same number of arguments and be compatible with the underlying
    // function.  But it may accept more limited data types than the layers
    // underneath, or change the parameter conventions (e.g. from normal to
    // quoted).  A facade might be a valid paramlist, but it might just
    // *look* like a paramlist, with the underlying function in slot 0 instead
    // of a canon value which points back to itself.
    //
    REBCNT num_args = ACT_FACADE_NUM_PARAMS(a);

    // Allocate the data for the args and locals on the chunk stack.  The
    // addresses of these values will be stable for the duration of the
    // function call, but the pointers will be invalid after that point.
    //
    f->arg = f->args_head = Push_Value_Chunk_Of_Length(num_args);
    assert(CHUNK_LEN_FROM_VALUES(f->args_head) == num_args);
    assert(IS_END(f->args_head + num_args)); // guaranteed by chunk stack

    f->param = ACT_FACADE_HEAD(f->phase);

    // Each layer of specialization of a function can only add specializaitons
    // of arguments which have not been specialized already.  For efficiency,
    // the act of specialization merges all the underlying layers of
    // specialization together.  This means only the outermost specialization
    // is needed to fill all the specialized slots contributed by later phases.
    //
    REBCTX *exemplar = ACT_EXEMPLAR(a);
    if (exemplar != NULL)
        f->special = CTX_VARS_HEAD(exemplar);
    else
        f->special = const_KNOWN(f->param);

    f->deferred = NULL;

    // A REBFRM* for a function call may-or-may-not need an associated REBCTX*
    // dynamically allocated.  Whether it does or not depends on if bindings
    // to the args or locals wind up "leaking" into slots that have a lifetime
    // longer than the stack level of that REBFRM* (which would include any
    // indefinite-extent object variables or slots in arrays).  It can also
    // be necessary to create that REBCTX if the user tries to create a
    // FRAME! value for the function call, with similar rules about lifetime.
    //
    // Move_Value() and Derelativize() contain the logic that generates this
    // varlist on demand, but start out assuming one is not needed.
    //
    f->varlist = NULL;

    // Make sure the person who pushed the function correctly sets the
    // f->refine to either ORDINARY_ARG or LOOKBACK_ARG after this call.
    //
    TRASH_POINTER_IF_DEBUG(f->refine);
}


// This routine needs to be shared with the error handling code.  It would be
// nice if it were inlined into Do_Core...but repeating the code just to save
// the function call overhead is second-guessing the optimizer and would be
// a cause of bugs.
//
// Note that in response to an error, we do not want to drop the chunks,
// because there are other clients of the chunk stack that may be running.
// Hence the chunks will be freed by the error trap helper.
//
inline static void Drop_Action_Core(
    REBFRM *f,
    REBOOL drop_chunks
){
    assert(
        f->opt_label == NULL
        or GET_SER_FLAG(f->opt_label, SERIES_FLAG_UTF8_STRING)
    );
    TRASH_POINTER_IF_DEBUG(f->opt_label);
  #if defined(DEBUG_FRAME_LABELS)
    TRASH_POINTER_IF_DEBUG(f->label_utf8);
  #endif

    f->phase = NULL; // should args_head == NULL be the indicator instead?

    // The frame may be reused for another function call, and that function
    // may not start with native code (or use native code at all).
    //
    // !!! Should the code be willing to drop the running flag off the varlist
    // as well if it is persistent, so that the values can be modified once
    // the native code is no longer running?
    //
    f->flags.bits &= ~DO_FLAG_NATIVE_HOLD;

    if (drop_chunks) {
        if (f->varlist == NULL) {
            Drop_Chunk_Of_Values(f->args_head);

            goto finished; // nothing else to do...
        }

        // A varlist may happen even with stackvars...if "singular" (e.g.
        // it's just a REBSER node for purposes of GC-referencing, but gets
        // its actual content from the stackvars.
        //
        if (ARR_LEN(f->varlist) == 1)
            Drop_Chunk_Of_Values(f->args_head);
    }
    else {
        if (f->varlist == NULL)
            goto finished;
    }

    assert(GET_SER_FLAG(f->varlist, SERIES_FLAG_ARRAY | ARRAY_FLAG_VARLIST));
    ASSERT_ARRAY_MANAGED(f->varlist);

    // The varlist is going to outlive this call, so the frame correspondence
    // in it needs to be cleared out, so callers will know the frame is dead.
    // We substitute the paramlist of the original function the frame is for
    // in the keysource slot.
    //
    assert(cast(REBFRM*, LINK(f->varlist).keysource) == f);
    LINK(f->varlist).keysource = NOD(ACT_PARAMLIST(f->original));

    if (NOT_SER_FLAG(f->varlist, CONTEXT_FLAG_STACK)) {
        //
        // If there's no stack memory being tracked by this context, it
        // has dynamic memory and is being managed by the garbage collector
        // so there's nothing to do.
        //
        assert(GET_SER_INFO(f->varlist, SERIES_INFO_HAS_DYNAMIC));
        goto finished;
    }

    // It's reified but has its data pointer into the chunk stack, which
    // means we have to free it and mark the array inaccessible.

    assert(NOT_SER_INFO(f->varlist, SERIES_INFO_HAS_DYNAMIC));

    if (drop_chunks) {
        assert(NOT_SER_INFO(f->varlist, SERIES_INFO_INACCESSIBLE));
        SET_SER_INFOS(f->varlist, SERIES_INFO_INACCESSIBLE);
    }
    else
        SET_SER_INFOS(
            f->varlist,
            SERIES_INFO_INACCESSIBLE | FRAME_INFO_FAILED
        );

finished:

    TRASH_POINTER_IF_DEBUG(f->args_head);
    TRASH_POINTER_IF_DEBUG(f->varlist);

    return; // needed for release build so `finished:` labels a statement
}
