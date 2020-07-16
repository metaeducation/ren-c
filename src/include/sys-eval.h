//
//  File: %sys-eval.h
//  Summary: {Low-Level Internal Evaluator API}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Rebol Open Source Contributors
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
// The primary routine that performs DO and EVALUATE is Eval_Core_Throws().
// It takes one parameter which holds the running state of the evaluator.
// This state may be allocated on the C variable stack...and fail() is
// written such that a longjmp up to a failure handler above it can run
// safely and clean up even though intermediate stacks have vanished.
//
// Ren-C can run the evaluator across a REBARR-style series of input based on
// index.  It can also enumerate through C's `va_list`, providing the ability
// to pass pointers as REBVAL* to comma-separated input at the source level.
//
// To provide even greater flexibility, it allows the very first element's
// pointer in an evaluation to come from an arbitrary source.  It doesn't
// have to be resident in the same sequence from which ensuing values are
// pulled, allowing a free head value (such as an ACTION! REBVAL in a local
// C variable) to be evaluated in combination from another source (like a
// va_list or series representing the arguments.)  This avoids the cost and
// complexity of allocating a series to combine the values together.
//
// These features alone would not cover the case when REBVAL pointers that
// are originating with C source were intended to be supplied to a function
// with no evaluation.  In R3-Alpha, the only way in an evaluative context
// to suppress such evaluations would be by adding elements (such as QUOTE).
// Besides the cost and labor of inserting these, the risk is that the
// intended functions to be called without evaluation, if they quoted
// arguments would then receive the QUOTE instead of the arguments.
//
// The problem was solved by adding a feature to the evaluator which was
// also opened up as a new privileged native called EVAL.  EVAL's refinements
// completely encompass evaluation possibilities in R3-Alpha, but it was also
// necessary to consider cases where a value was intended to be provided
// *without* evaluation.  This introduced EVAL/ONLY.
//


// !!! Find a better place for this!
//
inline static bool IS_QUOTABLY_SOFT(const RELVAL *v) {
    return IS_GROUP(v) or IS_GET_WORD(v) or IS_GET_PATH(v);
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  DO's LOWEST-LEVEL EVALUATOR HOOKING
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This API is used internally in the implementation of Eval_Core.  It does
// not speak in terms of arrays or indices, it works entirely by setting
// up a call frame (f), and threading that frame's state through successive
// operations, vs. setting it up and disposing it on each EVALUATE step.
//
// Like higher level APIs that move through the input series, this low-level
// API can move at full EVALUATE intervals.  Unlike the higher APIs, the
// possibility exists to move by single elements at a time--regardless of
// if the default evaluation rules would consume larger expressions.  Also
// making it different is the ability to resume after an EVALUATE on value
// sources that aren't random access (such as C's va_arg list).
//
// One invariant of access is that the input may only advance.  Before any
// operations are called, any low-level client must have already seeded
// f->value with a valid "fetched" REBVAL*.
//
// This privileged level of access can be used by natives that feel they can
// optimize performance by working with the evaluator directly.
//

inline static void Push_Frame_Core(REBFRM *f)
{
    // All calls to a Eval_Core_Throws() are assumed to happen at the same C
    // stack level for a pushed frame (though this is not currently enforced).
    // Hence it's sufficient to check for C stack overflow only once, e.g.
    // not on each Eval_Step_Throws() for `reduce [a | b | ... | z]`.
    //
    if (C_STACK_OVERFLOWING(&f))
        Fail_Stack_Overflow();

    assert(SECOND_BYTE(f->flags) == 0); // END signal
    assert(not (f->flags.bits & NODE_FLAG_CELL));

    // Though we can protect the value written into the target pointer 'out'
    // from GC during the course of evaluation, we can't protect the
    // underlying value from relocation.  Technically this would be a problem
    // for any series which might be modified while this call is running, but
    // most notably it applies to the data stack--where output used to always
    // be returned.
    //
    // !!! A non-contiguous data stack which is not a series is a possibility.
    //
  #ifdef STRESS_CHECK_DO_OUT_POINTER
    REBNOD *containing;
    if (
        did (containing = Try_Find_Containing_Node_Debug(f->out))
        and not (containing->header.bits & NODE_FLAG_CELL)
        and NOT_SER_FLAG(containing, SERIES_FLAG_DONT_RELOCATE)
    ){
        printf("Request for ->out location in movable series memory\n");
        panic (containing);
    }
  #else
    assert(not IN_DATA_STACK_DEBUG(f->out));
  #endif

  #ifdef DEBUG_EXPIRED_LOOKBACK
    f->stress = nullptr;
  #endif

    // The arguments to functions in their frame are exposed via FRAME!s
    // and through WORD!s.  This means that if you try to do an evaluation
    // directly into one of those argument slots, and run arbitrary code
    // which also *reads* those argument slots...there could be trouble with
    // reading and writing overlapping locations.  So unless a function is
    // in the argument fulfillment stage (before the variables or frame are
    // accessible by user code), it's not legal to write directly into an
    // argument slot.  :-/
    //
  #if !defined(NDEBUG)
    REBFRM *ftemp = FS_TOP;
    for (; ftemp != FS_BOTTOM; ftemp = ftemp->prior) {
        if (not Is_Action_Frame(ftemp))
            continue;
        if (Is_Action_Frame_Fulfilling(ftemp))
            continue;
        if (GET_SER_INFO(ftemp->varlist, SERIES_INFO_INACCESSIBLE))
            continue; // Encloser_Dispatcher() reuses args from up stack
        assert(
            f->out < FRM_ARGS_HEAD(ftemp)
            or f->out >= FRM_ARGS_HEAD(ftemp) + FRM_NUM_ARGS(ftemp)
        );
    }
  #endif

    // Some initialized bit pattern is needed to check to see if a
    // function call is actually in progress, or if eval_type is just
    // REB_ACTION but doesn't have valid args/state.  The original action is a
    // good choice because it is only affected by the function call case,
    // see Is_Action_Frame_Fulfilling().
    //
    f->original = nullptr;

    if (not (f->flags.bits & DO_FLAG_REEVALUATE_CELL))
        TRASH_POINTER_IF_DEBUG(f->u.defer.arg);
    TRASH_POINTER_IF_DEBUG(f->u.defer.param);
    TRASH_POINTER_IF_DEBUG(f->u.defer.refine);

    TRASH_POINTER_IF_DEBUG(f->opt_label);
  #if defined(DEBUG_FRAME_LABELS)
    TRASH_POINTER_IF_DEBUG(f->label_utf8);
  #endif

  #if !defined(NDEBUG)
    //
    // !!! TBD: the relevant file/line update when f->source->array changes
    //
    f->file = FRM_FILE_UTF8(f);
    f->line = FRM_LINE(f);
  #endif

    f->prior = TG_Top_Frame;
    TG_Top_Frame = f;

    TRASH_POINTER_IF_DEBUG(f->varlist); // must Try_Reuse_Varlist() or fill in

    // If the source for the frame is a REBARR*, then we want to temporarily
    // lock that array against mutations.  
    //
    if (FRM_IS_VALIST(f)) {
        //
        // There's nothing to put a hold on while it's a va_list-based frame.
        // But a GC might occur and "Reify" it, in which case the array
        // which is created will have a hold put on it to be released when
        // the frame is finished.
    }
    else {
        if (GET_SER_INFO(f->source->array, SERIES_INFO_HOLD))
            NOOP; // already temp-locked
        else {
            SET_SER_INFO(f->source->array, SERIES_INFO_HOLD);
            f->flags.bits |= DO_FLAG_TOOK_FRAME_HOLD;
        }
    }

  #if defined(DEBUG_BALANCE_STATE)
    SNAP_STATE(&f->state); // to make sure stack balances, etc.
    f->state.dsp = f->dsp_orig;
  #endif
}

// Pretend the input source has ended; used with REB_E_PROCESS_ACTION.
//
inline static void Push_Frame_At_End(REBFRM *f, REBFLGS flags) {
    f->flags = Endlike_Header(flags);

    assert(f->source == &TG_Frame_Source_End); // see DECLARE_END_FRAME
    f->gotten = nullptr;
    SET_FRAME_VALUE(f, END_NODE);
    f->specifier = SPECIFIED;

    Push_Frame_Core(f);
}

inline static void UPDATE_EXPRESSION_START(REBFRM *f) {
    f->expr_index = f->source->index; // this is garbage if DO_FLAG_VA_LIST
}

inline static void Reuse_Varlist_If_Available(REBFRM *f) {
    assert(IS_POINTER_TRASH_DEBUG(f->varlist));
    if (not TG_Reuse)
        f->varlist = nullptr;
    else {
        f->varlist = TG_Reuse;
        TG_Reuse = LINK(TG_Reuse).reuse;
        f->rootvar = cast(REBVAL*, SER(f->varlist)->content.dynamic.data);
        LINK(f->varlist).keysource = NOD(f);
    }
}

inline static void Push_Frame_At(
    REBFRM *f,
    REBARR *array,
    REBCNT index,
    REBSPC *specifier,
    REBFLGS flags
){
    f->flags = Endlike_Header(flags);

    f->gotten = nullptr; // Eval_Core_Throws() must fetch for REB_WORD, etc.
    SET_FRAME_VALUE(f, ARR_AT(array, index));

    f->source->vaptr = nullptr;
    f->source->array = array;
    f->source->index = index + 1;
    f->source->pending = f->value + 1;

    f->specifier = specifier;

    // Frames are pushed to reuse for several sequential operations like
    // ANY, ALL, CASE, REDUCE.  It is allowed to change the output cell for
    // each evaluation.  But the GC expects initialized bits in the output
    // slot at all times; use an unwritable END until the first eval call.
    //
    f->out = m_cast(REBVAL*, END_NODE);

    Push_Frame_Core(f);
    Reuse_Varlist_If_Available(f);
}

inline static void Push_Frame(REBFRM *f, const REBVAL *v)
{
    Push_Frame_At(
        f, VAL_ARRAY(v), VAL_INDEX(v), VAL_SPECIFIER(v), DO_MASK_NONE
    );
}


// Ordinary Rebol internals deal with REBVAL* that are resident in arrays.
// But a va_list can contain UTF-8 string components or special instructions
// that are other Detect_Rebol_Pointer() types.  Anyone who wants to set or
// preload a frame's state for a va_list has to do this detection, so this
// code has to be factored out (because a C va_list cannot have its first
// parameter in the variadic).
//
inline static void Set_Frame_Detected_Fetch(
    const RELVAL **opt_lookback,
    REBFRM *f,
    const void *p
){
    // This is the last chance we'll have to see f->value.  So if we are
    // supposed to be freeing it or releasing it, then it must be proxied
    // into a place where the data will be safe long enough for lookback.

    if (NOT_VAL_FLAG(f->value, NODE_FLAG_ROOT)) {
        if (opt_lookback)
            *opt_lookback = f->value; // non-API values must be stable/GC-safe
        goto detect;
    }

    REBARR *a; // ^--goto
    a = Singular_From_Cell(f->value);
    if (NOT_SER_INFO(a, SERIES_INFO_API_RELEASE)) {
        if (opt_lookback)
            *opt_lookback = f->value; // keep-alive API value or instruction
        goto detect;
    }

    if (opt_lookback) {
        //
        // Eval_Core_Throws() is wants the old f->value, but we're going to
        // free it.  It has to be kept alive -and- kept safe from GC.  e.g.
        //
        //     REBVAL *word = rebValue("make word! {hello}");
        //     rebValue(rebR(word), "-> (recycle :quote)");
        //
        // The `current` cell the evaluator is looking at is the WORD!, then
        // f->value receives the "shove" `->`.  The shove runs the code in
        // the GROUP!.  But there are no other references to `hello` after
        // the Free_Value() done by rebR(), so it's a candidate for recycle,
        // which would mean shoving a bad `current` as the arg to `:quote`
        //
        // The FRM_CELL(f) is used as the GC-safe location proxied to.
        //
        Move_Value(FRM_CELL(f), KNOWN(f->value));
        if (GET_VAL_FLAG(f->value, VALUE_FLAG_EVAL_FLIP))
            SET_VAL_FLAG(FRM_CELL(f), VALUE_FLAG_EVAL_FLIP);
        *opt_lookback = FRM_CELL(f);
    }

    if (GET_SER_INFO(a, SERIES_INFO_API_INSTRUCTION))
        Free_Instruction(Singular_From_Cell(f->value));
    else
        rebRelease(cast(const REBVAL*, f->value));


  detect:;

    if (not p) { // libRebol's null/<opt> (IS_NULLED prohibited below)

        f->source->array = nullptr;
        f->value = NULLED_CELL;

    } else switch (Detect_Rebol_Pointer(p)) {

      case DETECTED_AS_UTF8: {
        REBDSP dsp_orig = DSP;

        SCAN_STATE ss;
        const REBLIN start_line = 1;
        Init_Va_Scan_State_Core(
            &ss,
            Intern("sys-do.h"),
            start_line,
            cast(const REBYTE*, p),
            f->source->vaptr
        );

        // !!! In the working definition, the "topmost level" of a variadic
        // call is considered to be already evaluated...unless you ask to
        // evaluate it further.  This is what allows `rebSpellInto(v, rebEND)`
        // to work as well as `rebSpellInto("first", v, rebEND)`, the idea of
        // "fetch" is the reading of the C variable V, and it would be a
        // "double eval" if that v were a WORD! that then executed.
        //
        // Hence, nulls are legal, because it's as if you said `first :v`
        // with v being the C variable name.  However, this is not meaningful
        // if the value winds up spliced into a block--so any null in those
        // cases are treated as errors.
        //
        // For the moment, this also cues automatic interning on the string
        // runs...because if we did the binding here, all the strings would
        // have become arrays, and be indistinguishable from the components
        // that they were spliced in with.  So it would be too late to tell
        // which elements came from strings and which were existing blocks
        // from elsewhere.  This is not ideal, but it's just to start.
        //
        ss.opts |= SCAN_FLAG_NULLEDS_LEGAL;

        // !!! Current hack is to just allow one binder to be passed in for
        // use binding any newly loaded portions (spliced ones are left with
        // their bindings, though there may be special "binding instructions"
        // or otherwise, that get added).
        //
        ss.context = Get_Context_From_Stack();
        ss.lib = (ss.context != Lib_Context) ? Lib_Context : nullptr;

        struct Reb_Binder binder;
        Init_Interning_Binder(&binder, ss.context);
        ss.binder = &binder;

        REBVAL *error = rebRescue(cast(REBDNG*, &Scan_To_Stack), &ss);
        Shutdown_Interning_Binder(&binder, ss.context);

        if (error) {
            REBCTX *error_ctx = VAL_CONTEXT(error);
            rebRelease(error);
            fail (error_ctx);
        }

        // !!! for now, assume scan went to the end; ultimately it would need
        // to pass the "source".
        //
        f->source->vaptr = NULL;

        if (DSP == dsp_orig) {
            //
            // This happens when somone says rebValue(..., "", ...) or similar,
            // and gets an empty array from a string scan.  It's not legal
            // to put an END in f->value, and it's unknown if the variadic
            // feed is actually over so as to put null... so get another
            // value out of the va_list and keep going.
            //
            p = va_arg(*f->source->vaptr, const void*);
            goto detect;
        }

        REBARR *reified = Pop_Stack_Values_Keep_Eval_Flip(dsp_orig);

        // !!! We really should be able to free this array without managing it
        // when we're done with it, though that can get a bit complicated if
        // there's an error or need to reify into a value.  For now, do the
        // inefficient thing and manage it.
        //
        MANAGE_ARRAY(reified);

        f->value = ARR_HEAD(reified);
        f->source->pending = f->value + 1; // may be END
        f->source->array = reified;
        f->source->index = 1;

        assert(GET_SER_FLAG(f->source->array, ARRAY_FLAG_NULLEDS_LEGAL));
        break; }

      case DETECTED_AS_SERIES: { // "instructions" like rebEval(), rebUneval()
        REBARR *instruction = ARR(m_cast(void*, p));

        // The instruction should be unmanaged, and will be freed on the next
        // entry to this routine (optionally copying out its contents into
        // the frame's cell for stable lookback--if necessary).
        //
        assert(GET_SER_INFO(instruction, SERIES_INFO_API_INSTRUCTION));
        assert(NOT_SER_FLAG(instruction, NODE_FLAG_MANAGED));
        f->value = ARR_SINGLE(instruction);
        break; }

      case DETECTED_AS_FREED_SERIES:
        panic (p);

      case DETECTED_AS_CELL: {
        const REBVAL *cell = cast(const REBVAL*, p);
        if (IS_NULLED(cell))
            fail ("NULLED cell leaked to API, see NULLIZE() in C sources");

        // If the cell is in an API holder with SERIES_INFO_API_RELEASE then
        // it will be released on the *next* call (see top of function)

        f->source->array = nullptr;
        f->value = cell; // note that END is detected separately
        assert(
            not IS_RELATIVE(f->value) or (
                IS_NULLED(f->value)
                and (f->flags.bits & DO_FLAG_EXPLICIT_EVALUATE)
            )
        );
        break; }

      case DETECTED_AS_END: {
        //
        // We're at the end of the variadic input, so end of the line.
        //
        f->value = END_NODE;
        TRASH_POINTER_IF_DEBUG(f->source->pending);

        // The va_end() is taken care of here, or if there is a throw/fail it
        // is taken care of by Abort_Frame_Core()
        //
        va_end(*f->source->vaptr);
        f->source->vaptr = nullptr;

        // !!! Error reporting expects there to be an array.  The whole story
        // of errors when there's a va_list is not told very well, and what
        // will have to likely happen is that in debug modes, all valists
        // are reified from the beginning, else there's not going to be
        // a way to present errors in context.  Fake an empty array for now.
        //
        f->source->array = EMPTY_ARRAY;
        f->source->index = 0;
        break; }

      case DETECTED_AS_FREED_CELL:
        panic (p);

      default:
        assert(false);
    }
}


//
// Fetch_Next_In_Frame() (see notes above)
//
// Once a va_list is "fetched", it cannot be "un-fetched".  Hence only one
// unit of fetch is done at a time, into f->value.  f->source->pending thus
// must hold a signal that data remains in the va_list and it should be
// consulted further.  That signal is an END marker.
//
// More generally, an END marker in f->source->pending for this routine is a
// signal that the vaptr (if any) should be consulted next.
//
inline static void Fetch_Next_In_Frame(
    const RELVAL **opt_lookback,
    REBFRM *f
){
    assert(NOT_END(f->value)); // caller should test this first

  #ifdef DEBUG_EXPIRED_LOOKBACK
    if (f->stress) {
        TRASH_CELL_IF_DEBUG(f->stress);
        free(f->stress);
        f->stress = nullptr;
    }
  #endif

    // We are changing f->value, and thus by definition any f->gotten value
    // will be invalid.  It might be "wasteful" to always set this to END,
    // especially if it's going to be overwritten with the real fetch...but
    // at a source level, having every call to Fetch_Next_In_Frame have to
    // explicitly set f->gotten to null is overkill.  Could be split into
    // a version that just trashes f->gotten in the debug build vs. END.
    //
    f->gotten = nullptr;

    if (NOT_END(f->source->pending)) {
        //
        // We assume the ->pending value lives in a source array, and can
        // just be incremented since the array has SERIES_INFO_HOLD while it
        // is being executed hence won't be relocated or modified.  This
        // means the release build doesn't need to call ARR_AT().
        //
        assert(
            f->source->array // incrementing plain array of REBVAL[]
            or f->source->pending == ARR_AT(f->source->array, f->source->index)
        );

        if (opt_lookback)
            *opt_lookback = f->value; // must be non-movable, GC-safe

        f->value = f->source->pending;

        ++f->source->pending; // might be becoming an END marker, here
        ++f->source->index;
    }
    else if (not f->source->vaptr) {
        //
        // The frame was either never variadic, or it was but got spooled into
        // an array by Reify_Va_To_Array_In_Frame().  The first END we hit
        // is the full stop end.
        //
        if (opt_lookback)
            *opt_lookback = f->value; // all values would have been spooled

        f->value = END_NODE;
        TRASH_POINTER_IF_DEBUG(f->source->pending);

        ++f->source->index; // for consistency in index termination state

        if (f->flags.bits & DO_FLAG_TOOK_FRAME_HOLD) {
            assert(GET_SER_INFO(f->source->array, SERIES_INFO_HOLD));
            CLEAR_SER_INFO(f->source->array, SERIES_INFO_HOLD);

            // !!! Future features may allow you to move on to another array.
            // If so, the "hold" bit would need to be reset like this.
            //
            f->flags.bits &= ~DO_FLAG_TOOK_FRAME_HOLD;
        }
    }
    else {
        // A variadic can source arbitrary pointers, which can be detected
        // and handled in different ways.  Notably, a UTF-8 string can be
        // differentiated and loaded.
        //
        const void *p = va_arg(*f->source->vaptr, const void*);
        f->source->index = TRASHED_INDEX; // avoids warning in release build
        Set_Frame_Detected_Fetch(opt_lookback, f, p);
    }

  #ifdef DEBUG_EXPIRED_LOOKBACK
    if (opt_lookback) {
        f->stress = cast(RELVAL*, malloc(sizeof(RELVAL)));
        memcpy(f->stress, *opt_lookback, sizeof(RELVAL));
        *opt_lookback = f->stress;
    }
  #endif
}


inline static void Quote_Next_In_Frame(REBVAL *dest, REBFRM *f) {
    Derelativize(dest, f->value, f->specifier);
    SET_VAL_FLAG(dest, VALUE_FLAG_UNEVALUATED);
    Fetch_Next_In_Frame(nullptr, f);
}


inline static void Abort_Frame(REBFRM *f) {
    if (f->varlist and NOT_SER_FLAG(f->varlist, NODE_FLAG_MANAGED))
        GC_Kill_Series(SER(f->varlist)); // not alloc'd with manuals tracking
    TRASH_POINTER_IF_DEBUG(f->varlist);

    // Abort_Frame() handles any work that wouldn't be done done naturally by
    // feeding a frame to its natural end.
    // 
    if (IS_END(f->value))
        goto pop;

    if (FRM_IS_VALIST(f)) {
        assert(not (f->flags.bits & DO_FLAG_TOOK_FRAME_HOLD));

        // Aborting valist frames is done by just feeding all the values
        // through until the end.  This is assumed to do any work, such
        // as SERIES_INFO_API_RELEASE, which might be needed on an item.  It
        // also ensures that va_end() is called, which happens when the frame
        // manages to feed to the end.
        //
        // Note: While on many platforms va_end() is a no-op, the C standard
        // is clear it must be called...it's undefined behavior to skip it:
        //
        // http://stackoverflow.com/a/32259710/211160

        // !!! Since we're not actually fetching things to run them, this is
        // overkill.  A lighter sweep of the va_list pointers that did just
        // enough work to handle rebR() releases, and va_end()ing the list
        // would be enough.  But for the moment, it's more important to keep
        // all the logic in one place than to make variadic interrupts
        // any faster...they're usually reified into an array anyway, so
        // the frame processing the array will take the other branch.

        while (NOT_END(f->value))
            Fetch_Next_In_Frame(nullptr, f);
    }
    else {
        if (f->flags.bits & DO_FLAG_TOOK_FRAME_HOLD) {
            //
            // The frame was either never variadic, or it was but got spooled
            // into an array by Reify_Va_To_Array_In_Frame()
            //
            assert(GET_SER_INFO(f->source->array, SERIES_INFO_HOLD));
            CLEAR_SER_INFO(f->source->array, SERIES_INFO_HOLD);
        }
    }

pop:;

    assert(TG_Top_Frame == f);
    TG_Top_Frame = f->prior;
}


inline static void Drop_Frame_Core(REBFRM *f) {
  #if defined(DEBUG_EXPIRED_LOOKBACK)
    free(f->stress);
  #endif

    if (f->varlist) {
        assert(NOT_SER_FLAG(f->varlist, NODE_FLAG_MANAGED));
        LINK(f->varlist).reuse = TG_Reuse;
        TG_Reuse = f->varlist;
    }
    TRASH_POINTER_IF_DEBUG(f->varlist);

    assert(TG_Top_Frame == f);
    TG_Top_Frame = f->prior;
}

inline static void Drop_Frame_Unbalanced(REBFRM *f) {
  #if defined(DEBUG_BALANCE_STATE)
    //
    // To avoid slowing down the debug build a lot, Eval_Core_Throws() doesn't
    // check this every cycle, just on drop.  But if it's hard to find which
    // exact cycle caused the problem, see BALANCE_CHECK_EVERY_EVALUATION_STEP
    //
    f->state.dsp = DSP; // e.g. Reduce_To_Stack_Throws() doesn't want check
    f->state.mold_buf_len = SER_LEN(MOLD_BUF); // REMOVE-EACH accumulates
    ASSERT_STATE_BALANCED(&f->state);
  #endif
    Drop_Frame_Core(f);
}

inline static void Drop_Frame(REBFRM *f)
{
    if (f->flags.bits & DO_FLAG_TO_END)
        assert(IS_END(f->value) or THROWN(f->out));

    assert(DSP == f->dsp_orig); // Drop_Frame_Core() does not check
    Drop_Frame_Unbalanced(f);
}


// This is a very light wrapper over Eval_Core_Throws(), which is used with
// Push_Frame_At() for operations like ANY or REDUCE that wish to perform
// several successive operations on an array, without creating a new frame
// each time.
//
inline static bool Eval_Step_Throws(
    REBVAL *out,
    REBFRM *f
){
    assert(IS_END(out));

    assert(not (f->flags.bits & (DO_FLAG_TO_END | DO_FLAG_NO_LOOKAHEAD)));
    uintptr_t prior_flags = f->flags.bits;

    f->out = out;
    f->dsp_orig = DSP;
    bool threw = (*PG_Eval_Throws)(f); // should already be pushed

    // The & on the following line is purposeful.  See Init_Endlike_Header.
    // DO_FLAG_NO_LOOKAHEAD may be set by an operation like ELIDE.
    //
    // Since this routine is used by BLOCK!-style varargs, it must retain
    // knowledge of if BAR! was hit.
    //
    (&f->flags)->bits = prior_flags | (f->flags.bits & DO_FLAG_BARRIER_HIT);

    return threw;
}


// Unlike Eval_Step_Throws() which relies on tests of IS_END() on out to
// see if the end was reached, this expects the caller to preload the output
// with some value, and then test OUT_MARKED_STALE to see if the only thing
// run in the frame were invisibles (empty groups, comments) or nothing.
//
inline static bool Eval_Step_Maybe_Stale_Throws(
    REBVAL *out,
    REBFRM *f
){
    assert(NOT_END(out));

    assert(not (f->flags.bits & (DO_FLAG_TO_END | DO_FLAG_NO_LOOKAHEAD)));
    uintptr_t prior_flags = f->flags.bits;
    f->flags.bits |= DO_FLAG_PRESERVE_STALE;

    f->out = out;
    f->dsp_orig = DSP;
    bool threw = (*PG_Eval_Throws)(f); // should already be pushed

    // The & on the following line is purposeful.  See Init_Endlike_Header.
    // DO_FLAG_NO_LOOKAHEAD may be set by an operation like ELIDE.
    //
    // Since this routine is used by BLOCK!-style varargs, it must retain
    // knowledge of if BAR! was hit.
    //
    (&f->flags)->bits = prior_flags | (f->flags.bits & DO_FLAG_BARRIER_HIT);

    return threw;
}


// Bit heavier wrapper of Eval_Core_Throws() than Eval_Step_In_Frame_Throws().
// It also reuses the frame...but has to clear and restore the frame's
// flags.  It is currently used only by SET-WORD! and SET-PATH!.
//
// Note: Consider pathological case `x: eval quote y: eval eval quote z: ...`
// This can be done without making a new frame, but the eval cell which holds
// the SET-WORD! needs to be put back in place before returning, so that the
// set knows where to write.  The caller handles this with the data stack.
//
inline static bool Eval_Step_Mid_Frame_Throws(REBFRM *f, REBFLGS flags) {
    assert(f->dsp_orig == DSP);

    REBFLGS prior_flags = f->flags.bits;
    f->flags = Endlike_Header(flags);

    bool threw = (*PG_Eval_Throws)(f); // should already be pushed

    f->flags.bits = prior_flags; // e.g. restore DO_FLAG_TO_END    
    return threw;
}


// It should not be necessary to use a subframe unless there is meaningful
// state which would be overwritten in the parent frame.  For the moment,
// that only happens if a function call is in effect -or- if a SET-WORD! or
// SET-PATH! are running with an expiring `current` in effect.  Else it is
// more efficient to call Eval_Step_In_Frame_Throws(), or the also lighter
// Eval_Step_In_Mid_Frame_Throws().
//
// !!! This operation used to try and optimize some cases without using a
// subframe.  But checking for whether an optimization would be legal or not
// was complex, as even something inert like `1` cannot be evaluated into a
// slot as `1` unless you are sure there's no `+` or other enfixed operation.
// Over time as the evaluator got more complicated, the redundant work and
// conditional code paths showed a slight *slowdown* over just having an
// inline function that built a frame and recursed Eval_Core_Throws().
//
// Future investigation could attack the problem again and see if there is
// any common case that actually offered an advantage to optimize for here.
//
inline static bool Eval_Step_In_Subframe_Throws(
    REBVAL *out,
    REBFRM *higher, // may not be direct parent (not child->prior upon push!)
    REBFLGS flags,
    REBFRM *child // passed w/dsp_orig preload, refinements can be on stack
){
    child->out = out;

    // !!! Should they share a source instead of updating?
    //
    assert(child->source == higher->source);
    child->value = higher->value;
    child->gotten = higher->gotten;
    child->specifier = higher->specifier;

    // f->gotten is never marked for GC, because it should never be kept
    // alive across arbitrary evaluations (f->value should keep it alive).
    // We'll write it back with an updated value from the child after the
    // call, and no one should be able to read it until then (e.g. the caller
    // can't be a variadic frame that is executing yet)
    //
  #if !defined(NDEBUG)
    TRASH_POINTER_IF_DEBUG(higher->gotten);
    REBCNT old_index = higher->source->index;
  #endif

    child->flags = Endlike_Header(flags);

    // One case in which child->prior on this push may not be equal to the
    // higher frame passed in is variadics.  The frame making the call to
    // advance the variadic feed can be deeper down the stack, and it will
    // be the ->prior, so it's important not to corrupt it based on assuming
    // it is the variadic frame.
    //
    Push_Frame_Core(child);
    Reuse_Varlist_If_Available(child);
    (*PG_Eval_Throws)(child);
    Drop_Frame(child);

    assert(
        IS_END(child->value)
        or FRM_IS_VALIST(child)
        or old_index != child->source->index
        or (flags & DO_FLAG_REEVALUATE_CELL)
        or THROWN(out)
    );

    // !!! Should they share a source instead of updating?
    //
    higher->value = child->value;
    higher->gotten = child->gotten;
    assert(higher->specifier == child->specifier); // !!! can't change?

    if (child->flags.bits & DO_FLAG_BARRIER_HIT)
        higher->flags.bits |= DO_FLAG_BARRIER_HIT;

    return THROWN(out);
}


// Most common case of evaluator invocation in Rebol: the data lives in an
// array series.
//
inline static REBIXO Eval_Array_At_Core(
    REBVAL *out, // must be initialized, marked stale if empty / all invisible
    const RELVAL *opt_first, // non-array element to kick off execution with
    REBARR *array,
    REBCNT index,
    REBSPC *specifier, // must match array, but also opt_first if relative
    REBFLGS flags // DO_FLAG_TO_END, DO_FLAG_EXPLICIT_EVALUATE, etc.
){
    DECLARE_FRAME (f);
    f->flags = Endlike_Header(flags); // SET_FRAME_VALUE() *could* use

    f->source->vaptr = nullptr;
    f->source->array = array;
    f->gotten = nullptr; // SET_FRAME_VALUE() asserts this is nullptr
    if (opt_first) {
        SET_FRAME_VALUE(f, opt_first);
        f->source->index = index;
        f->source->pending = ARR_AT(array, index);
        assert(NOT_END(f->value));
    }
    else {
        SET_FRAME_VALUE(f, ARR_AT(array, index));
        f->source->index = index + 1;
        f->source->pending = f->value + 1;
        if (IS_END(f->value))
            return END_FLAG;
    }

    f->out = out;
    f->specifier = specifier;

    Push_Frame_Core(f);
    Reuse_Varlist_If_Available(f);
    bool threw = (*PG_Eval_Throws)(f);
    Drop_Frame(f);

    if (threw)
        return THROWN_FLAG;

    assert(
        not (flags & DO_FLAG_TO_END)
        or f->source->index == ARR_LEN(array) + 1
    );
    return f->source->index;
}


//
//  Reify_Va_To_Array_In_Frame: C
//
// For performance and memory usage reasons, a variadic C function call that
// wants to invoke the evaluator with just a comma-delimited list of REBVAL*
// does not need to make a series to hold them.  Eval_Core is written to use
// the va_list traversal as an alternate to DO-ing an ARRAY.
//
// However, va_lists cannot be backtracked once advanced.  So in a debug mode
// it can be helpful to turn all the va_lists into arrays before running
// them, so stack frames can be inspected more meaningfully--both for upcoming
// evaluations and those already past.
//
// A non-debug reason to reify a va_list into an array is if the garbage
// collector needs to see the upcoming values to protect them from GC.  In
// this case it only needs to protect those values that have not yet been
// consumed.
//
// Because items may well have already been consumed from the va_list() that
// can't be gotten back, we put in a marker to help hint at the truncation
// (unless told that it's not truncated, e.g. a debug mode that calls it
// before any items are consumed).
//
inline static void Reify_Va_To_Array_In_Frame(
    REBFRM *f,
    bool truncated
) {
    REBDSP dsp_orig = DSP;

    assert(FRM_IS_VALIST(f));

    if (truncated) {
        DS_PUSH_TRASH;
        Init_Word(DS_TOP, Canon(SYM___OPTIMIZED_OUT__));
    }

    if (NOT_END(f->value)) {
        assert(f->source->pending == END_NODE);

        do {
            // may be a NULLED cell.  Preserve VALUE_FLAG_EVAL_FLIP flag.
            DS_PUSH_RELVAL_KEEP_EVAL_FLIP(f->value, f->specifier);
            Fetch_Next_In_Frame(nullptr, f);
        } while (NOT_END(f->value));

        if (truncated)
            f->source->index = 2; // skip the --optimized-out--
        else
            f->source->index = 1; // position at start of the extracted values
    }
    else {
        assert(IS_POINTER_TRASH_DEBUG(f->source->pending));

        // Leave at end of frame, but give back the array to serve as
        // notice of the truncation (if it was truncated)
        //
        f->source->index = 0;
    }

    assert(not f->source->vaptr); // feeding forward should have called va_end

    // special array...may contain voids and eval flip is kept
    f->source->array = Pop_Stack_Values_Keep_Eval_Flip(dsp_orig);
    MANAGE_ARRAY(f->source->array); // held alive while frame running
    SET_SER_FLAG(f->source->array, ARRAY_FLAG_NULLEDS_LEGAL);

    // The array just popped into existence, and it's tied to a running
    // frame...so safe to say we're holding it.  (This would be more complex
    // if we reused the empty array if dsp_orig == DSP, since someone else
    // might have a hold on it...not worth the complexity.) 
    //
    SET_SER_INFO(f->source->array, SERIES_INFO_HOLD);
    f->flags.bits |= DO_FLAG_TOOK_FRAME_HOLD;

    if (truncated)
        SET_FRAME_VALUE(f, ARR_AT(f->source->array, 1)); // skip `--optimized--`
    else
        SET_FRAME_VALUE(f, ARR_HEAD(f->source->array));

    f->source->pending = f->value + 1;
}


// (va_list by pointer: http://stackoverflow.com/a/3369762/211160)
//
// Central routine for doing an evaluation of an array of values by calling
// a C function with those parameters (e.g. supplied as arguments, separated
// by commas).  Uses same method to do so as functions like printf() do.
//
// The evaluator has a common means of fetching values out of both arrays
// and C va_lists via Fetch_Next_In_Frame(), so this code can behave the
// same as if the passed in values came from an array.  However, when values
// originate from C they often have been effectively evaluated already, so
// it's desired that WORD!s or PATH!s not execute as they typically would
// in a block.  So this is often used with DO_FLAG_EXPLICIT_EVALUATE.
//
// !!! C's va_lists are very dangerous, there is no type checking!  The
// C++ build should be able to check this for the callers of this function
// *and* check that you ended properly.  It means this function will need
// two different signatures (and so will each caller of this routine).
//
// Returns THROWN_FLAG, END_FLAG, or VA_LIST_FLAG
//
inline static REBIXO Eval_Va_Core(
    REBVAL *out, // must be initialized, marked stale if empty / all invisible
    const void *opt_first,
    va_list *vaptr,
    REBFLGS flags
){
    DECLARE_FRAME (f);
    f->flags = Endlike_Header(flags); // read by Set_Frame_Detected_Fetch

    f->source->index = TRASHED_INDEX; // avoids warning in release build
    f->source->array = nullptr;
    f->source->vaptr = vaptr;
    f->source->pending = END_NODE; // signal next fetch comes from va_list

  #if defined(DEBUG_UNREADABLE_BLANKS)
    //
    // We reuse logic in Fetch_Next_In_Frame() and Set_Frame_Detected_Fetch()
    // but the previous f->value will be tested for NODE_FLAG_ROOT.
    //
    DECLARE_LOCAL (junk);
    f->value = Init_Unreadable_Blank(junk); // shows where garbage came from
  #else
    f->value = BLANK_VALUE; // less informative but faster to initialize
  #endif

    if (opt_first)
        Set_Frame_Detected_Fetch(nullptr, f, opt_first);
    else
        Fetch_Next_In_Frame(nullptr, f);

    if (IS_END(f->value))
        return END_FLAG;

    f->out = out;
    f->specifier = SPECIFIED; // relative values not allowed in va_lists
    f->gotten = nullptr;

    Push_Frame_Core(f);
    Reuse_Varlist_If_Available(f);
    bool threw = (*PG_Eval_Throws)(f);
    Drop_Frame(f); // will va_end() if not reified during evaluation

    if (threw)
        return THROWN_FLAG;

    if (
        (flags & DO_FLAG_TO_END) // not just an EVALUATE, but a full DO
        or (f->out->header.bits & OUT_MARKED_STALE) // just ELIDEs and COMMENTs
    ){
        assert(IS_END(f->value));
        return END_FLAG;
    }

    if ((flags & DO_FLAG_NO_RESIDUE) and NOT_END(f->value))
        fail (Error_Apply_Too_Many_Raw());

    return VA_LIST_FLAG; // frame may be at end, next call might just END_FLAG
}


inline static bool Eval_Value_Core_Throws(
    REBVAL *out,
    const RELVAL *value, // e.g. a BLOCK! here would just evaluate to itself!
    REBSPC *specifier
){
    REBIXO indexor = Eval_Array_At_Core(
        SET_END(out), // start with END to detect no actual eval product
        value, // put the value as the opt_first element
        EMPTY_ARRAY,
        0, // start index (it's an empty array, there's no added processing)
        specifier,
        DO_FLAG_TO_END
    );

    if (IS_END(out))
        fail ("Eval_Value_Core_Throws() empty or just COMMENTs/ELIDEs/BAR!s");

    return indexor == THROWN_FLAG;
}

#define Eval_Value_Throws(out,value) \
    Eval_Value_Core_Throws((out), (value), SPECIFIED)


// The evaluator accepts API handles back from action dispatchers, and the
// path evaluator accepts them from path dispatch.  This code does common
// checking used by both, which includes automatic release of the handle
// so the dispatcher can write things like `return rebValue(...);` and not
// encounter a leak.
//
inline static void Handle_Api_Dispatcher_Result(REBFRM *f, const REBVAL* r) {
    assert(not THROWN(r)); // only f->out can return thrown cells

  #if !defined(NDEBUG)
    if (NOT_VAL_FLAG(r, NODE_FLAG_ROOT)) {
        printf("dispatcher returned non-API value not in D_OUT\n");
        printf("during ACTION!: %s\n", f->label_utf8);
        printf("`return D_OUT;` or use `RETURN (non_api_cell);`\n");
        panic(r);
    }
  #endif

    if (IS_NULLED(r))
        assert(!"Dispatcher returned nulled cell, not C nullptr for API use");

    Move_Value(f->out, r);
    if (NOT_VAL_FLAG(r, NODE_FLAG_MANAGED))
        rebRelease(r);
}
