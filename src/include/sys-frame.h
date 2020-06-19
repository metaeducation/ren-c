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
// created with plain THROW instead of THROW/NAME then its name is null).
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

inline static bool THROWN(const RELVAL *v) {
    assert(v->header.bits & NODE_FLAG_CELL);

    if (v->header.bits & VALUE_FLAG_THROWN) {
        assert(NOT_END(v)); // can't throw END, but allow THROWN() to test it
        return true;
    }
    return false;
}

inline static void CONVERT_NAME_TO_THROWN(REBVAL *name, const REBVAL *arg) {
    assert(not THROWN(name));
    SET_VAL_FLAG(name, VALUE_FLAG_THROWN);

    ASSERT_UNREADABLE_IF_DEBUG(&TG_Thrown_Arg);

    Move_Value(&TG_Thrown_Arg, arg);
}

static inline void CATCH_THROWN(RELVAL *arg_out, REBVAL *thrown) {
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


inline static bool FRM_IS_VALIST(REBFRM *f) {
    return f->source->vaptr != nullptr;
}

inline static REBARR *FRM_ARRAY(REBFRM *f) {
    assert(IS_END(f->value) or not FRM_IS_VALIST(f));
    return f->source->array;
}

// !!! Though the evaluator saves its `index`, the index is not meaningful
// in a valist.  Also, if `opt_head` values are used to prefetch before an
// array, those will be lost too.  A true debugging mode would need to
// convert these cases to ordinary arrays before running them, in order
// to accurately present any errors.
//
inline static REBCNT FRM_INDEX(REBFRM *f) {
    if (IS_END(f->value))
        return ARR_LEN(f->source->array);

    assert(not FRM_IS_VALIST(f));
    return f->source->index - 1;
}

inline static REBCNT FRM_EXPR_INDEX(REBFRM *f) {
    assert(not FRM_IS_VALIST(f));
    return f->expr_index == END_FLAG
        ? ARR_LEN((f)->source->array)
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
    if (not f->source->array)
        return nullptr;

    if (NOT_SER_FLAG(f->source->array, ARRAY_FLAG_FILE_LINE))
        return nullptr;

    return LINK(f->source->array).file;
}

inline static const char* FRM_FILE_UTF8(REBFRM *f) {
    //
    // !!! Note: This is used too early in boot at the moment to use
    // Canon(__ANONYMOUS__).
    //
    REBSTR *str = FRM_FILE(f);
    return str ? STR_HEAD(str) : "(anonymous)"; 
}

inline static int FRM_LINE(REBFRM *f) {
    if (not f->source->array)
        return 0;

    if (NOT_SER_FLAG(f->source->array, ARRAY_FLAG_FILE_LINE))
        return 0;

    return MISC(SER(f->source->array)).line;
}

#define FRM_OUT(f) \
    (f)->out


// Note about FRM_NUM_ARGS: A native should generally not detect the arity it
// was invoked with, (and it doesn't make sense as most implementations get
// the full list of arguments and refinements).  However, ACTION! dispatch
// has several different argument counts piping through a switch, and often
// "cheats" by using the arity instead of being conditional on which action
// ID ran.  Consider when reviewing the future of ACTION!.
//
#define FRM_NUM_ARGS(f) \
    (cast(REBSER*, (f)->varlist)->content.dynamic.len - 1) // minus rootvar

#define FRM_CELL(f) \
    cast(REBVAL*, &(f)->cell)

#define FRM_SHOVE(f) \
    cast(REBVAL*, &(f)->shove)

inline static bool Is_Frame_Gotten_Shoved(REBFRM *f) {
    if (f->gotten != FRM_SHOVE(f))
        return false;
    assert(GET_VAL_FLAG(f->gotten, VALUE_FLAG_ENFIXED));
    return true; // see REBNATIVE(shove)
}

#define FRM_PRIOR(f) \
    ((f)->prior + 0) // prevent assignment via this macro

#define FRM_PHASE_OR_DUMMY(f) \
    f->rootvar->payload.any_context.phase

#if defined(NDEBUG) or !defined(__cplusplus)
    #define FRM_PHASE(f) \
        FRM_PHASE_OR_DUMMY(f)
#else
    // The C++ debug build adds a check that a frame is not uing a tricky
    // noop dispatcher, when access to the phase is gotten with FRM_PHASE().
    // This trick lets the sunk cost of calling a dispatcher be used instead
    // of a separate flag checked on every evaluator cycle.  This is so that
    // routines like `MAYBE PARSE "AAA" [SOME "A"]` can build the parse frame
    // without actually *running* PARSE yet...return from Eval_Core_Throws(),
    // extract the first argument, and then call back into Eval_Core_Throws()
    // to actually run the PARSE.
    //
    // Any manipulations aware of this hack need to access the field directly.
    //
    inline static REBACT* &FRM_PHASE(REBFRM *f) {
        REBACT* &phase = FRM_PHASE_OR_DUMMY(f);
        assert(phase != PG_Dummy_Action);
        return phase;
    }
#endif

#define FRM_BINDING(f) \
    f->rootvar->extra.binding

#define FRM_UNDERLYING(f) \
    ACT_UNDERLYING((f)->original)

#define FRM_DSP_ORIG(f) \
    ((f)->dsp_orig + 0) // prevent assignment via this macro


// ARGS is the parameters and refinements
// 1-based indexing into the arglist (0 slot is for FRAME! value)

#define FRM_ARGS_HEAD(f) \
    ((f)->rootvar + 1)

#ifdef NDEBUG
    #define FRM_ARG(f,n) \
        ((f)->rootvar + (n))
#else
    inline static REBVAL *FRM_ARG(REBFRM *f, REBCNT n) {
        assert(n != 0 and n <= FRM_NUM_ARGS(f));

        REBVAL *var = f->rootvar + n; // 1-indexed
        assert(not IS_RELATIVE(cast(RELVAL*, var)));
        return var;
    }
#endif


// Quick access functions from natives (or compatible functions that name a
// Reb_Frame pointer `frame_`) to get some of the common public fields.
//
#define D_FRAME     frame_
#define D_OUT       FRM_OUT(frame_)         // GC-safe slot for output value
#define D_ARGC      FRM_NUM_ARGS(frame_)    // count of args+refinements/args
#define D_ARG(n)    FRM_ARG(frame_, (n))    // pass 1 for first arg

#define RETURN(v) \
    return Move_Value(D_OUT, (v));

inline static bool Is_Action_Frame(REBFRM *f) {
    if (f->original != nullptr) {
        //
        // Do not count as a function frame unless its gotten to the point
        // of pushing arguments.
        //
        return true;
    }
    return false;
}

// While a function frame is fulfilling its arguments, the `f->param` will
// be pointing to a typeset.  The invariant that is maintained is that
// `f->param` will *not* be a typeset when the function is actually in the
// process of running.  (So no need to set/clear/test another "mode".)
//
inline static bool Is_Action_Frame_Fulfilling(REBFRM *f)
{
    assert(Is_Action_Frame(f));
    return NOT_END(f->param);
}


inline static void Get_Frame_Label_Or_Blank(REBVAL *out, REBFRM *f) {
    assert(Is_Action_Frame(f));
    if (f->opt_label != NULL)
        Init_Word(out, f->opt_label); // invoked via WORD! or PATH!
    else
        Init_Blank(out); // anonymous invocation
}

inline static const char* Frame_Label_Or_Anonymous_UTF8(REBFRM *f) {
    assert(Is_Action_Frame(f));
    if (f->opt_label != NULL)
        return STR_HEAD(f->opt_label);
    return "[anonymous]";
}

inline static void SET_FRAME_VALUE(REBFRM *f, const RELVAL* value) {
    assert(not f->gotten); // is fetched f->value, we'd be invalidating it!
    f->value = value;
}



//=////////////////////////////////////////////////////////////////////////=//
//
//  ARGUMENT AND PARAMETER ACCESS HELPERS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// These accessors are what is behind the INCLUDE_PARAMS_OF_XXX macros that
// are used in natives.  They capture the implicit Reb_Frame* passed to every
// REBNATIVE ('frame_') and read the information out cleanly, like this:
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
        ACT_PARAM(FRM_PHASE(frame_), (p_##name)) /* a TYPESET! */

    #define REF(name) \
        (not IS_BLANK(ARG(name))) /* should be faster than IS_FALSEY() */
#else
    struct Native_Param {
        int num;
        enum Reb_Kind kind_cache; // for inspecting in watchlist
        REBVAL *arg; // for inspecting in watchlist
    };

    struct Native_Refine {
        int num;
        bool used_cache; // for inspecting in watchlist
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
        ACT_PARAM(FRM_PHASE(frame_), (p_##name).num) /* a TYPESET! */

    #define REF(name) \
        ((p_##name).used_cache /* used_cache use stops REF() on PARAM()s */ \
            ? not IS_BLANK(ARG(name)) \
            : not IS_BLANK(ARG(name)))
#endif


// The native entry prelude makes sure that once native code starts running,
// then the frame's stub is flagged to indicate access via a FRAME! should
// not have write access to variables.  That could cause crashes, as raw C
// code is not insulated against having bit patterns for types in cells that
// aren't expected.
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
    SET_SER_INFO(f->varlist, SERIES_INFO_HOLD); // may or may not be managed
}


inline static void Begin_Action(
    REBFRM *f,
    REBSTR *opt_label,
    REBVAL *mode // LOOKBACK_ARG or ORDINARY_ARG or END
){
    assert(not f->original);
    f->original = FRM_PHASE_OR_DUMMY(f);

    assert(IS_POINTER_TRASH_DEBUG(f->opt_label)); // only valid w/REB_ACTION
    assert(not opt_label or GET_SER_FLAG(opt_label, SERIES_FLAG_UTF8_STRING));
    f->opt_label = opt_label;
  #if defined(DEBUG_FRAME_LABELS) // helpful for looking in the debugger
    f->label_utf8 = cast(const char*, Frame_Label_Or_Anonymous_UTF8(f));
  #endif

    assert(
        mode == LOOKBACK_ARG
        or mode == ORDINARY_ARG
        or mode == END_NODE
    );
    f->refine = mode;
}


// Allocate the series of REBVALs inspected by a function when executed (the
// values behind ARG(name), REF(name), D_ARG(3),  etc.)
//
// This only allocates space for the arguments, it does not initialize.
// Eval_Core initializes as it goes, and updates f->param so the GC knows how
// far it has gotten so as not to see garbage.  APPLY has different handling
// when it has to build the frame for the user to write to before running;
// so Eval_Core only checks the arguments, and does not fulfill them.
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
    REBACT *act,
    REBNOD *binding
){
    f->param = ACT_PARAMS_HEAD(act); // Specializations hide some params...
    REBCNT num_args = ACT_NUM_PARAMS(act); // ...so see REB_TS_HIDDEN

    // !!! Note: Should pick "smart" size when allocating varlist storage due
    // to potential reuse--but use exact size for *this* action, for now.
    //
    REBSER *s;
    if (not f->varlist) { // usually means first action call in the REBFRM
        s = Alloc_Series_Node(
            SERIES_MASK_CONTEXT
                | SERIES_FLAG_STACK
                | SERIES_FLAG_FIXED_SIZE // FRAME!s don't expand ATM
        );
        s->info = Endlike_Header(
            FLAG_WIDE_BYTE_OR_0(0) // signals array, also implicit terminator
                | FLAG_LEN_BYTE_OR_255(255) // signals dynamic
        );
        s->link_private.keysource = NOD(f); // maps varlist back to f
        s->misc_private.meta = nullptr; // GC will sees this
        f->varlist = ARR(s);
    }
    else {
        s = SER(f->varlist);
        if (s->content.dynamic.rest >= num_args + 1 + 1) // +roovar, +end
            goto sufficient_allocation;

        //assert(SER_BIAS(s) == 0);
        Free_Unbiased_Series_Data(
            s->content.dynamic.data,
            SER_TOTAL(s)
        );
    }

    if (not Did_Series_Data_Alloc(s, num_args + 1 + 1)) // +rootvar, +end
        fail ("Out of memory in Push_Action()");

    f->rootvar = cast(REBVAL*, s->content.dynamic.data);
    f->rootvar->header.bits =
        NODE_FLAG_NODE | NODE_FLAG_CELL | NODE_FLAG_STACK
        | CELL_FLAG_PROTECTED // cell payload/binding tweaked, not by user
        | FLAG_KIND_BYTE(REB_FRAME);
    TRACK_CELL_IF_DEBUG(f->rootvar, __FILE__, __LINE__);
    f->rootvar->payload.any_context.varlist = f->varlist;

  sufficient_allocation:

    f->rootvar->payload.any_context.phase = act; // FRM_PHASE() (can be dummy)
    f->rootvar->extra.binding = binding; // FRM_BINDING()

    s->content.dynamic.len = num_args + 1;
    RELVAL *tail = ARR_TAIL(f->varlist);
    tail->header.bits = NODE_FLAG_STACK | FLAG_KIND_BYTE(REB_0);
    TRACK_CELL_IF_DEBUG(tail, __FILE__, __LINE__);

    // Current invariant for all arrays (including fixed size), last cell in
    // the allocation is an end.
    RELVAL *ultimate = ARR_AT(f->varlist, s->content.dynamic.rest - 1);
    ultimate->header = Endlike_Header(0); // unreadable
    TRACK_CELL_IF_DEBUG(ultimate, __FILE__, __LINE__);

  #if !defined(NDEBUG)
    RELVAL *prep = ultimate - 1;
    for (; prep > tail; --prep) {
        prep->header.bits = FLAG_KIND_BYTE(REB_T_TRASH); // unreadable
        TRACK_CELL_IF_DEBUG(prep, __FILE__, __LINE__);
    }
  #endif

    f->arg = f->rootvar + 1;

    // Each layer of specialization of a function can only add specializations
    // of arguments which have not been specialized already.  For efficiency,
    // the act of specialization merges all the underlying layers of
    // specialization together.  This means only the outermost specialization
    // is needed to fill the specialized slots contributed by later phases.
    //
    // f->special here will either equal f->param (to indicate normal argument
    // fulfillment) or the head of the "exemplar".  To speed this up, the
    // absence of a cached exemplar just means that the "specialty" holds the
    // paramlist... this means no conditional code is needed here.
    //
    f->special = ACT_SPECIALTY_HEAD(act);

    f->u.defer.arg = nullptr;

    assert(NOT_SER_FLAG(f->varlist, NODE_FLAG_MANAGED));
    assert(NOT_SER_INFO(f->varlist, SERIES_INFO_INACCESSIBLE));
}


inline static void Drop_Action(REBFRM *f) {
    assert(NOT_SER_INFO(f->varlist, FRAME_INFO_FAILED));

    assert(
        not f->opt_label
        or GET_SER_FLAG(f->opt_label, SERIES_FLAG_UTF8_STRING)
    );

    if (not (f->flags.bits & DO_FLAG_FULFILLING_ARG))
        f->flags.bits &= ~DO_FLAG_BARRIER_HIT;

    assert(
        GET_SER_INFO(f->varlist, SERIES_INFO_INACCESSIBLE)
        or LINK(f->varlist).keysource == NOD(f)
    );

    if (GET_SER_INFO(f->varlist, SERIES_INFO_INACCESSIBLE)) {
        //
        // If something like Encloser_Dispatcher() runs, it might steal the
        // variables from a context to give them to the user, leaving behind
        // a non-dynamic node.  Pretty much all the bits in the node are
        // therefore useless.  It served a purpose by being non-null during
        // the call, however, up to this moment.
        //
        if (GET_SER_FLAG(f->varlist, NODE_FLAG_MANAGED))
            f->varlist = nullptr; // references exist, let a new one alloc
        else {
            // This node could be reused vs. calling Make_Node() on the next
            // action invocation...but easier for the moment to let it go.
            //
            Free_Node(SER_POOL, f->varlist);
            f->varlist = nullptr;
        }
    }
    else if (GET_SER_FLAG(f->varlist, NODE_FLAG_MANAGED)) {
        //
        // The varlist wound up getting referenced in a cell that will outlive
        // this Drop_Action().  The pointer needed to stay working up until
        // now, but the args memory won't be available.  But since we know
        // there were outstanding references to the varlist, we need to
        // convert it into a "stub" that's enough to avoid crashes.
        //
        // ...but we don't free the memory for the args, we just hide it from
        // the stub and get it ready for potential reuse by the next action
        // call.  That's done by making an adjusted copy of the stub, which
        // steals its dynamic memory (by setting the stub not HAS_DYNAMIC).
        //
        f->varlist = CTX_VARLIST(
            Steal_Context_Vars(
                CTX(f->varlist),
                NOD(f->original) // degrade keysource from f
            )
        );
        assert(NOT_SER_FLAG(f->varlist, NODE_FLAG_MANAGED));
        LINK(f->varlist).keysource = NOD(f);
    }
    else {
        // We can reuse the varlist and its data allocation, which may be
        // big enough for ensuing calls.  
        //
        // But no series bits we didn't set should be set...and right now,
        // only Enter_Native() sets HOLD.  Clear that.
        //
        CLEAR_SER_INFOS(f->varlist, SERIES_INFO_HOLD);
        assert(0 == (SER(f->varlist)->info.bits & ~( // <- note bitwise not
            SERIES_INFO_0_IS_TRUE // parallels NODE_FLAG_NODE
            | FLAG_WIDE_BYTE_OR_0(0) // don't mask out wide (0 for arrays))
            | FLAG_LEN_BYTE_OR_255(255) // mask out non-dynamic-len (dynamic)
        )));
    }

  #if !defined(NDEBUG)
    if (f->varlist) {
        assert(NOT_SER_INFO(f->varlist, SERIES_INFO_INACCESSIBLE));
        assert(NOT_SER_FLAG(f->varlist, NODE_FLAG_MANAGED));

        REBVAL *rootvar = cast(REBVAL*, ARR_HEAD(f->varlist));
        assert(IS_FRAME(rootvar));
        assert(rootvar->payload.any_context.varlist == f->varlist);
        TRASH_POINTER_IF_DEBUG(rootvar->payload.any_context.phase);
        TRASH_POINTER_IF_DEBUG(rootvar->extra.binding);
    }
  #endif

    f->original = nullptr; // signal an action is no longer running

    TRASH_POINTER_IF_DEBUG(f->opt_label);
  #if defined(DEBUG_FRAME_LABELS)
    TRASH_POINTER_IF_DEBUG(f->label_utf8);
  #endif
}


//
//  Context_For_Frame_May_Manage: C
//
inline static REBCTX *Context_For_Frame_May_Manage(REBFRM *f)
{
    assert(not Is_Action_Frame_Fulfilling(f));
    SET_SER_FLAG(f->varlist, NODE_FLAG_MANAGED);
    return CTX(f->varlist);
}


inline static REBACT *VAL_PHASE(REBVAL *frame) {
    assert(IS_FRAME(frame));
    return frame->payload.any_context.phase;
}
