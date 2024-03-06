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
// All THROWN values have two parts: the cell arg being thrown and
// a cell indicating the /NAME of a labeled throw.  (If the throw was
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

INLINE bool THROWN(const Cell* v) {
    assert(v->header.bits & NODE_FLAG_CELL);

    if (v->header.bits & VALUE_FLAG_THROWN) {
        assert(NOT_END(v)); // can't throw END, but allow THROWN() to test it
        return true;
    }
    return false;
}

INLINE void CONVERT_NAME_TO_THROWN(Value* name, const Value* arg) {
    assert(not THROWN(name));
    SET_VAL_FLAG(name, VALUE_FLAG_THROWN);

    Assert_Unreadable_If_Debug(&TG_Thrown_Arg);

    Copy_Cell(&TG_Thrown_Arg, arg);
}

INLINE void CATCH_THROWN(Cell* arg_out, Value* thrown) {
    //
    // Note: arg_out and thrown may be the same pointer
    //
    assert(THROWN(thrown));
    CLEAR_VAL_FLAG(thrown, VALUE_FLAG_THROWN);

    Assert_Readable_If_Debug(&TG_Thrown_Arg);
    Copy_Cell(arg_out, &TG_Thrown_Arg);
    Init_Unreadable(&TG_Thrown_Arg);
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  LOW-LEVEL FRAME ACCESSORS
//
//=////////////////////////////////////////////////////////////////////////=//


INLINE bool LVL_IS_VALIST(Level* L) {
    return L->source->vaptr != nullptr;
}

INLINE Array* LVL_ARRAY(Level* L) {
    assert(IS_END(L->value) or not LVL_IS_VALIST(L));
    return L->source->array;
}

// !!! Though the evaluator saves its `index`, the index is not meaningful
// in a valist.  Also, if `opt_head` values are used to prefetch before an
// array, those will be lost too.  A true debugging mode would need to
// convert these cases to ordinary arrays before running them, in order
// to accurately present any errors.
//
INLINE REBLEN LVL_INDEX(Level* L) {
    if (IS_END(L->value))
        return Array_Len(L->source->array);

    assert(not LVL_IS_VALIST(L));
    return L->source->index - 1;
}

INLINE REBLEN LVL_EXPR_INDEX(Level* L) {
    assert(not LVL_IS_VALIST(L));
    return L->expr_index == END_FLAG
        ? Array_Len((L)->source->array)
        : L->expr_index - 1;
}

INLINE Option(String*) File_Of_Level(Level* L) {
    //
    // !!! the rebValue function could be a variadic macro in C99 or higher, as
    // `rebValueFileLine(__FILE__, __LINE__, ...`.  This could let the file and
    // line information make it into the frame, and be used when loading new
    // source material -or- if no source material were loaded, it could just
    // be kept as a UTF-8 string inside the frame without needing interning
    // as a series.  But for now, just signal that it came from C code.
    //
    if (not L->source->array)
        return nullptr;

    if (NOT_SER_FLAG(L->source->array, ARRAY_FLAG_FILE_LINE))
        return nullptr;

    Option(String*) file = LINK(L->source->array).file;
    if (file)
        assert(Is_Series_Ucs2(unwrap(file)));

    return try_unwrap(file);
}

INLINE int LVL_LINE(Level* L) {
    if (not L->source->array)
        return 0;

    if (NOT_SER_FLAG(L->source->array, ARRAY_FLAG_FILE_LINE))
        return 0;

    return MISC(L->source->array).line;
}


// Note about Level_Num_Args: A native should generally not detect the arity it
// was invoked with, (and it doesn't make sense as most implementations get
// the full list of arguments and refinements).  However, ACTION! dispatch
// has several different argument counts piping through a switch, and often
// "cheats" by using the arity instead of being conditional on which action
// ID ran.  Consider when reviewing the future of ACTION!.
//
#define Level_Num_Args(L) \
    (cast(Series*, (L)->varlist)->content.dynamic.len - 1) // minus rootvar

#define Level_Spare(L) \
    cast(Value*, &(L)->spare)

#define Level_Shove(L) \
    cast(Value*, &(L)->shove)

INLINE bool Is_Level_Gotten_Shoved(Level* L) {
    if (L->gotten != Level_Shove(L))
        return false;
    assert(GET_VAL_FLAG(L->gotten, VALUE_FLAG_ENFIXED));
    return true; // see DECLARE_NATIVE(shove)
}

#define LVL_PHASE_OR_DUMMY(L) \
    L->rootvar->payload.any_context.phase

#if defined(NDEBUG) or !defined(__cplusplus)
    #define Level_Phase(L) \
        LVL_PHASE_OR_DUMMY(L)
#else
    // The C++ debug build adds a check that a frame is not uing a tricky
    // noop dispatcher, when access to the phase is gotten with Level_Phase().
    // This trick lets the sunk cost of calling a dispatcher be used instead
    // of a separate flag checked on every evaluator cycle.  This is so that
    // routines like `MAYBE PARSE "AAA" [SOME "A"]` can build the parse frame
    // without actually *running* PARSE yet...return from Eval_Core_Throws(),
    // extract the first argument, and then call back into Eval_Core_Throws()
    // to actually run the PARSE.
    //
    // Any manipulations aware of this hack need to access the field directly.
    //
    INLINE REBACT* &Level_Phase(Level* L) {
        REBACT* &phase = LVL_PHASE_OR_DUMMY(L);
        assert(phase != PG_Dummy_Action);
        return phase;
    }
#endif

#define LVL_BINDING(L) \
    L->rootvar->extra.binding

#define LVL_UNDERLYING(L) \
    ACT_UNDERLYING((L)->original)


// ARGS is the parameters and refinements
// 1-based indexing into the arglist (0 slot is for FRAME! value)

#define Level_Args_Head(L) \
    ((L)->rootvar + 1)

#ifdef NDEBUG
    #define Level_Arg(L,n) \
        ((L)->rootvar + (n))
#else
    INLINE Value* Level_Arg(Level* L, REBLEN n) {
        assert(n != 0 and n <= Level_Num_Args(L));

        Value* var = L->rootvar + n; // 1-indexed
        assert(not IS_RELATIVE(cast(Cell*, var)));
        return var;
    }
#endif


// Quick access functions from natives (or compatible functions that name a
// LevelStruct pointer `level_`) to get some of the common public fields.
//
#if REBOL_LEVEL_SHORTHAND_MACROS
    #define D_FRAME     level_
    #define OUT         level_->out       // GC-safe slot for output value
    #define D_ARGC      Level_Num_Args(level_)  // count of args+refinements/args
    #define D_ARG(n)    Level_Arg(level_, (n))  // pass 1 for first arg

    #define RETURN(v) \
        return Copy_Cell(OUT, (v))
#endif

INLINE bool Is_Action_Level(Level* L) {
    if (L->original != nullptr) {
        //
        // Do not count as a function frame unless its gotten to the point
        // of pushing arguments.
        //
        return true;
    }
    return false;
}

// While a function frame is fulfilling its arguments, the `L->param` will
// be pointing to a typeset.  The invariant that is maintained is that
// `L->param` will *not* be a typeset when the function is actually in the
// process of running.  (So no need to set/clear/test another "mode".)
//
INLINE bool Is_Action_Level_Fulfilling(Level* L)
{
    assert(Is_Action_Level(L));
    return NOT_END(L->param);
}


INLINE void Get_Level_Label_Or_Blank(Value* out, Level* L) {
    assert(Is_Action_Level(L));
    if (L->opt_label != nullptr)
        Init_Word(out, L->opt_label); // invoked via WORD! or PATH!
    else
        Init_Blank(out); // anonymous invocation
}

INLINE const char* Frame_Label_Or_Anonymous_UTF8(Level* L) {
    assert(Is_Action_Level(L));
    if (L->opt_label != nullptr)
        return Symbol_Head(L->opt_label);
    return "[anonymous]";
}

INLINE void SET_FRAME_VALUE(Level* L, const Cell* value) {
    assert(not L->gotten); // is fetched L->value, we'd be invalidating it!
    L->value = value;
}



//=////////////////////////////////////////////////////////////////////////=//
//
//  ARGUMENT AND PARAMETER ACCESS HELPERS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// These accessors are what is behind the INCLUDE_PARAMS_OF_XXX macros that
// are used in natives.  They capture the implicit Level* passed to every
// DECLARE_NATIVE ('level_') and read the information out cleanly, like this:
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
        Level_Arg(level_, (p_##name))

    #define PAR(name) \
        ACT_PARAM(Level_Phase(level_), (p_##name)) /* a TYPESET! */

    #define REF(name) \
        (not IS_BLANK(ARG(name))) /* should be faster than IS_FALSEY() */
#else
    struct Native_Param {
        int num;
        enum Reb_Kind kind_cache; // for inspecting in watchlist
        Value* arg; // for inspecting in watchlist
    };

    struct Native_Refine {
        int num;
        bool used_cache; // for inspecting in watchlist
        Value* arg; // for inspecting in watchlist
    };

    // Note: Assigning non-const initializers to structs, e.g. `= {var, f()};`
    // is a non-standard extension to C.  So we break out the assignments.

    #define PARAM(n,name) \
        struct Native_Param p_##name; \
        p_##name.num = (n); \
        p_##name.kind_cache = VAL_TYPE(Level_Arg(level_, (n))); \
        p_##name.arg = Level_Arg(level_, (n)); \

    #define REFINE(n,name) \
        struct Native_Refine p_##name; \
        p_##name.num = (n); \
        p_##name.used_cache = IS_TRUTHY(Level_Arg(level_, (n))); \
        p_##name.arg = Level_Arg(level_, (n)); \

    #define ARG(name) \
        Level_Arg(level_, (p_##name).num)

    #define PAR(name) \
        ACT_PARAM(Level_Phase(level_), (p_##name).num) /* a TYPESET! */

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
INLINE void Enter_Native(Level* L) {
    SET_SER_INFO(L->varlist, SERIES_INFO_HOLD); // may or may not be managed
}


INLINE void Begin_Action(
    Level* L,
    Symbol* opt_label,
    Value* mode // LOOKBACK_ARG or ORDINARY_ARG or END
){
    assert(not L->original);
    L->original = LVL_PHASE_OR_DUMMY(L);

    assert(Is_Pointer_Corrupt_Debug(L->opt_label)); // only valid w/REB_ACTION
    assert(not opt_label or GET_SER_FLAG(opt_label, SERIES_FLAG_UTF8));
    L->opt_label = opt_label;
  #if defined(DEBUG_FRAME_LABELS) // helpful for looking in the debugger
    L->label_utf8 = cast(const char*, Frame_Label_Or_Anonymous_UTF8(L));
  #endif

    assert(
        mode == LOOKBACK_ARG
        or mode == ORDINARY_ARG
        or mode == END_NODE
    );
    L->refine = mode;
}


// Allocate the series of REBVALs inspected by a function when executed (the
// values behind ARG(name), REF(name), D_ARG(3),  etc.)
//
// This only allocates space for the arguments, it does not initialize.
// Eval_Core initializes as it goes, and updates L->param so the GC knows how
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
INLINE void Push_Action(
    Level* L,
    REBACT *act,
    Stub* binding
){
    L->param = ACT_PARAMS_HEAD(act); // Specializations hide some params...
    REBLEN num_args = ACT_NUM_PARAMS(act); // ...so see REB_TS_HIDDEN

    // !!! Note: Should pick "smart" size when allocating varlist storage due
    // to potential reuse--but use exact size for *this* action, for now.
    //
    Series* s;
    if (not L->varlist) { // usually means first action call in the Level
        s = Alloc_Series_Node(
            SERIES_MASK_CONTEXT
                | SERIES_FLAG_FIXED_SIZE // FRAME!s don't expand ATM
        );
        s->info = Endlike_Header(
            FLAG_WIDE_BYTE_OR_0(0) // signals array, also implicit terminator
                | FLAG_LEN_BYTE_OR_255(255) // signals dynamic
        );
        s->link_private.keysource = NOD(L); // maps varlist back to f
        s->misc_private.meta = nullptr; // GC will sees this
        L->varlist = ARR(s);
    }
    else {
        s = L->varlist;
        if (s->content.dynamic.rest >= num_args + 1 + 1) // +roovar, +end
            goto sufficient_allocation;

        //assert(Series_Bias(s) == 0);
        Free_Unbiased_Series_Data(
            s->content.dynamic.data,
            SER_TOTAL(s)
        );
    }

    if (not Did_Series_Data_Alloc(s, num_args + 1 + 1)) // +rootvar, +end
        fail ("Out of memory in Push_Action()");

    L->rootvar = cast(Value*, s->content.dynamic.data);
    L->rootvar->header.bits =
        NODE_FLAG_NODE | NODE_FLAG_CELL | NODE_FLAG_STACK
        | CELL_FLAG_PROTECTED // cell payload/binding tweaked, not by user
        | FLAG_KIND_BYTE(REB_FRAME);
    TRACK_CELL_IF_DEBUG(L->rootvar, __FILE__, __LINE__);
    L->rootvar->payload.any_context.varlist = L->varlist;

  sufficient_allocation:

    L->rootvar->payload.any_context.phase = act; // Level_Phase() (can be dummy)
    L->rootvar->extra.binding = binding; // LVL_BINDING()

    s->content.dynamic.len = num_args + 1;
    Cell* tail = ARR_TAIL(L->varlist);
    tail->header.bits = NODE_FLAG_STACK | FLAG_KIND_BYTE(REB_0);
    TRACK_CELL_IF_DEBUG(tail, __FILE__, __LINE__);

    // Current invariant for all arrays (including fixed size), last cell in
    // the allocation is an end.
    Cell* ultimate = Array_At(L->varlist, s->content.dynamic.rest - 1);
    ultimate->header = Endlike_Header(0); // unreadable
    TRACK_CELL_IF_DEBUG(ultimate, __FILE__, __LINE__);

  #if !defined(NDEBUG)
    Cell* prep = ultimate - 1;
    for (; prep > tail; --prep)
        Poison_Cell(prep);
  #endif

    L->arg = L->rootvar + 1;

    // Each layer of specialization of a function can only add specializations
    // of arguments which have not been specialized already.  For efficiency,
    // the act of specialization merges all the underlying layers of
    // specialization together.  This means only the outermost specialization
    // is needed to fill the specialized slots contributed by later phases.
    //
    // L->special here will either equal L->param (to indicate normal argument
    // fulfillment) or the head of the "exemplar".  To speed this up, the
    // absence of a cached exemplar just means that the "specialty" holds the
    // paramlist... this means no conditional code is needed here.
    //
    L->special = ACT_SPECIALTY_HEAD(act);

    L->u.defer.arg = nullptr;

    assert(NOT_SER_FLAG(L->varlist, NODE_FLAG_MANAGED));
    assert(NOT_SER_INFO(L->varlist, SERIES_INFO_INACCESSIBLE));
}


INLINE void Drop_Action(Level* L) {
    assert(NOT_SER_INFO(L->varlist, FRAME_INFO_FAILED));

    assert(
        not L->opt_label
        or GET_SER_FLAG(L->opt_label, SERIES_FLAG_UTF8)
    );

    if (not (L->flags.bits & DO_FLAG_FULFILLING_ARG))
        L->flags.bits &= ~DO_FLAG_BARRIER_HIT;

    assert(
        GET_SER_INFO(L->varlist, SERIES_INFO_INACCESSIBLE)
        or LINK(L->varlist).keysource == L
    );

    if (GET_SER_INFO(L->varlist, SERIES_INFO_INACCESSIBLE)) {
        //
        // If something like Encloser_Dispatcher() runs, it might steal the
        // variables from a context to give them to the user, leaving behind
        // a non-dynamic node.  Pretty much all the bits in the node are
        // therefore useless.  It served a purpose by being non-null during
        // the call, however, up to this moment.
        //
        if (GET_SER_FLAG(L->varlist, NODE_FLAG_MANAGED))
            L->varlist = nullptr; // references exist, let a new one alloc
        else {
            // This node could be reused vs. calling Alloc_Pooled() on the next
            // action invocation...but easier for the moment to let it go.
            //
            Free_Pooled(SER_POOL, L->varlist);
            L->varlist = nullptr;
        }
    }
    else if (GET_SER_FLAG(L->varlist, NODE_FLAG_MANAGED)) {
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
        L->varlist = CTX_VARLIST(
            Steal_Context_Vars(
                CTX(L->varlist),
                L->original  // degrade keysource from L
            )
        );
        assert(NOT_SER_FLAG(L->varlist, NODE_FLAG_MANAGED));
        LINK(L->varlist).keysource = L;  // carries NODE_FLAG_CELL
    }
    else {
        // We can reuse the varlist and its data allocation, which may be
        // big enough for ensuing calls.
        //
        // But no series bits we didn't set should be set...and right now,
        // only Enter_Native() sets HOLD.  Clear that.
        //
        CLEAR_SER_INFO(L->varlist, SERIES_INFO_HOLD);
        assert(0 == (L->varlist->info.bits & ~( // <- note bitwise not
            SERIES_INFO_0_IS_TRUE // parallels NODE_FLAG_NODE
            | FLAG_WIDE_BYTE_OR_0(0) // don't mask out wide (0 for arrays))
            | FLAG_LEN_BYTE_OR_255(255) // mask out non-dynamic-len (dynamic)
        )));
    }

  #if !defined(NDEBUG)
    if (L->varlist) {
        assert(NOT_SER_INFO(L->varlist, SERIES_INFO_INACCESSIBLE));
        assert(NOT_SER_FLAG(L->varlist, NODE_FLAG_MANAGED));

        Value* rootvar = cast(Value*, ARR_HEAD(L->varlist));
        assert(IS_FRAME(rootvar));
        assert(rootvar->payload.any_context.varlist == L->varlist);
        Corrupt_Pointer_If_Debug(rootvar->payload.any_context.phase);
        Corrupt_Pointer_If_Debug(rootvar->extra.binding);
    }
  #endif

    L->original = nullptr; // signal an action is no longer running

    Corrupt_Pointer_If_Debug(L->opt_label);
  #if defined(DEBUG_FRAME_LABELS)
    Corrupt_Pointer_If_Debug(L->label_utf8);
  #endif
}


//
//  Context_For_Level_May_Manage: C
//
INLINE REBCTX *Context_For_Level_May_Manage(Level* L)
{
    assert(not Is_Action_Level_Fulfilling(L));
    SET_SER_FLAG(L->varlist, NODE_FLAG_MANAGED);
    return CTX(L->varlist);
}


INLINE REBACT *VAL_PHASE(Value* frame) {
    assert(IS_FRAME(frame));
    return frame->payload.any_context.phase;
}
