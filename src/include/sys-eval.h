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
// Ren-C can run the evaluator across a Array-style series of input based on
// index.  It can also enumerate through C's `va_list`, providing the ability
// to pass pointers as Value* to comma-separated input at the source level.
//
// To provide even greater flexibility, it allows the very first element's
// pointer in an evaluation to come from an arbitrary source.  It doesn't
// have to be resident in the same sequence from which ensuing values are
// pulled, allowing a free head value (such as an ACTION! cell in a local
// C variable) to be evaluated in combination from another source (like a
// va_list or series representing the arguments.)  This avoids the cost and
// complexity of allocating a series to combine the values together.
//
// These features alone would not cover the case when cell pointers that
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
INLINE bool IS_QUOTABLY_SOFT(const Cell* v) {
    return Is_Group(v) or Is_Get_Word(v) or Is_Get_Path(v);
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  DO's LOWEST-LEVEL EVALUATOR HOOKING
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This API is used internally in the implementation of Eval_Core.  It does
// not speak in terms of arrays or indices, it works entirely by setting
// up a call frame (L), and threading that frame's state through successive
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
// L->value with a valid "fetched" Value*.
//
// This privileged level of access can be used by natives that feel they can
// optimize performance by working with the evaluator directly.
//

INLINE void Push_Level_Core(Level* L)
{
    // All calls to a Eval_Core_Throws() are assumed to happen at the same C
    // stack level for a pushed frame (though this is not currently enforced).
    // Hence it's sufficient to check for C stack overflow only once, e.g.
    // not on each Eval_Step_Throws() for `reduce [a | b | ... | z]`.
    //
    if (C_STACK_OVERFLOWING(&L))
        Fail_Stack_Overflow();

    assert(SECOND_BYTE(&L->flags) == 0); // END signal
    assert(not (L->flags.bits & NODE_FLAG_CELL));

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
    Node* containing;
    if (
        did (containing = Try_Find_Containing_Node_Debug(L->out))
        and Is_Node_A_Stub(containing)
        and Not_Flex_Flag(cast_Flex(containing), DONT_RELOCATE)
    ){
        printf("Request for ->out location in movable series memory\n");
        panic (containing);
    }
  #else
    assert(not IN_DATA_STACK_DEBUG(L->out));
  #endif

  #if DEBUG_EXPIRED_LOOKBACK
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
  #if RUNTIME_CHECKS
    Level* L_temp = TOP_LEVEL;
    for (; L_temp != BOTTOM_LEVEL; L_temp = L_temp->prior) {
        if (not Is_Action_Level(L_temp))
            continue;
        if (Is_Action_Level_Fulfilling(L_temp))
            continue;
        if (Get_Flex_Info(L_temp->varlist, INACCESSIBLE))
            continue; // Encloser_Dispatcher() reuses args from up stack
        assert(
            L->out < Level_Args_Head(L_temp)
            or L->out >= Level_Args_Head(L_temp) + Level_Num_Args(L_temp)
        );
    }
  #endif

    // Some initialized bit pattern is needed to check to see if a
    // function call is actually in progress, or if eval_type is just
    // TYPE_ACTION but doesn't have valid args/state.  The original action is a
    // good choice because it is only affected by the function call case,
    // see Is_Action_Level_Fulfilling().
    //
    L->original = nullptr;

    Corrupt_Pointer_If_Debug(L->opt_label);
  #if DEBUG_FRAME_LABELS
    Corrupt_Pointer_If_Debug(L->label_utf8);
  #endif

  #if RUNTIME_CHECKS
    //
    // !!! TBD: the relevant file/line update when L->source->array changes
    //
    Option(String*) file = File_Of_Level(L);
    if (file)
        L->file_ucs2 = String_Head(unwrap file);  // sadly UCS-2 in old branch
    else
        L->file_ucs2 = nullptr;
    L->line = LVL_LINE(L);
  #endif

    L->prior = TG_Top_Level;
    TG_Top_Level = L;

    Corrupt_Pointer_If_Debug(L->varlist); // must Try_Reuse_Varlist() or fill in

    // If the source for the frame is a Array*, then we want to temporarily
    // lock that array against mutations.
    //
    if (LVL_IS_VALIST(L)) {
        //
        // There's nothing to put a hold on while it's a va_list-based frame.
        // But a GC might occur and "Reify" it, in which case the array
        // which is created will have a hold put on it to be released when
        // the frame is finished.
    }
    else {
        if (Get_Flex_Info(L->source->array, HOLD))
            NOOP; // already temp-locked
        else {
            Set_Flex_Info(L->source->array, HOLD);
            Set_Eval_Flag(L, TOOK_FRAME_HOLD);
        }
    }

  #if DEBUG_BALANCE_STATE
    SNAP_STATE(&L->state); // to make sure stack balances, etc.
    L->state.stack_base = L->stack_base;
  #endif
}

// Pretend the input source has ended; used with TYPE_E_PROCESS_ACTION.
//
INLINE void Push_Level_At_End(Level* L, Flags flags) {
    L->flags = Endlike_Header(flags);

    assert(L->source == &TG_Level_Source_End); // see DECLARE_END_LEVEL
    L->gotten = nullptr;
    SET_FRAME_VALUE(L, END_NODE);
    L->specifier = SPECIFIED;

    Push_Level_Core(L);
}

INLINE void UPDATE_EXPRESSION_START(Level* L) {
    L->expr_index = L->source->index; // this is garbage if EVAL_FLAG_VA_LIST
}

INLINE void Reuse_Varlist_If_Available(Level* L) {
    assert(Is_Pointer_Corrupt_Debug(L->varlist));
    if (not TG_Reuse)
        L->varlist = nullptr;
    else {
        L->varlist = TG_Reuse;
        TG_Reuse = LINK(TG_Reuse).reuse;
        L->rootvar = cast(Value*, L->varlist->content.dynamic.data);
        LINK(L->varlist).keysource = L;  // carries NODE_FLAG_CELL
    }
}

INLINE void Push_Level_At(
    Level* L,
    Array* array,
    REBLEN index,
    Specifier* specifier,
    Flags flags
){
    L->flags = Endlike_Header(flags);

    L->gotten = nullptr; // Eval_Core_Throws() must fetch for TYPE_WORD, etc.
    SET_FRAME_VALUE(L, Array_At(array, index));

    L->source->vaptr = nullptr;
    L->source->array = array;
    L->source->index = index + 1;
    L->source->pending = L->value + 1;
    L->source->deferring_infix = false;

    L->specifier = specifier;

    // Frames are pushed to reuse for several sequential operations like
    // ANY, ALL, CASE, REDUCE.  It is allowed to change the output cell for
    // each evaluation.  But the GC expects initialized bits in the output
    // slot at all times; use an unwritable END until the first eval call.
    //
    L->out = m_cast(Value*, END_NODE);

    Push_Level_Core(L);
    Reuse_Varlist_If_Available(L);
}

INLINE void Push_Level(Level* L, const Value* v)
{
    Push_Level_At(
        L, Cell_Array(v), VAL_INDEX(v), VAL_SPECIFIER(v), DO_MASK_NONE
    );
}


// Ordinary Rebol internals deal with Value* that are resident in arrays.
// But a va_list can contain UTF-8 string components or special instructions
// that are other Detect_Rebol_Pointer() types.  Anyone who wants to set or
// preload a frame's state for a va_list has to do this detection, so this
// code has to be factored out (because a C va_list cannot have its first
// parameter in the variadic).
//
INLINE void Set_Level_Detected_Fetch(
    const Cell* *opt_lookback,
    Level* L,
    const void *p
){
    // This is the last chance we'll have to see L->value.  So if we are
    // supposed to be freeing it or releasing it, then it must be proxied
    // into a place where the data will be safe long enough for lookback.

    if (Not_Node_Root_Bit_Set(L->value)) {
        if (opt_lookback)
            *opt_lookback = L->value; // non-API values must be stable/GC-safe
        goto detect;
    }

    Array* a; // ^--goto
    a = Singular_From_Cell(L->value);
    if (Not_Flex_Info(a, API_RELEASE)) {
        if (opt_lookback)
            *opt_lookback = L->value; // keep-alive API value or instruction
        goto detect;
    }

    if (opt_lookback) {
        //
        // Eval_Core_Throws() is wants the old L->value, but we're going to
        // free it.  It has to be kept alive -and- kept safe from GC.  e.g.
        //
        //     Value* word = rebValue("make word! {hello}");
        //     rebValue(rebR(word), "shove (recycle :the)");
        //
        // The `current` cell the evaluator is looking at is the WORD!, then
        // L->value receives the "shove" `shove`.  The shove runs the code in
        // the GROUP!.  But there are no other references to `hello` after
        // the Free_Value() done by rebR(), so it's a candidate for recycle,
        // which would mean shoving a bad `current` as the arg to `:the`
        //
        // The Level_Spare(L) is used as the GC-safe location proxied to.
        //
        Copy_Cell(Level_Spare(L), KNOWN(L->value));
        *opt_lookback = Level_Spare(L);
    }

    if (Get_Flex_Info(a, API_INSTRUCTION))
        Free_Instruction(Singular_From_Cell(L->value));
    else
        rebRelease(cast(const Value*, L->value));


  detect:;

    if (not p) { // libRebol's null/~null~ (Is_Nulled prohibited below)

        L->source->array = nullptr;
        L->value = NULLED_CELL;

    } else switch (Detect_Rebol_Pointer(p)) {

      case DETECTED_AS_UTF8: {
        StackIndex base = TOP_INDEX;

        TranscodeState transcode;
        String* filename = nullptr;
        const LineNumber start_line = 1;
        Init_Transcode_Vaptr(
            &transcode,
            filename,
            start_line,
            cast(const Byte*, p),
            L->source->vaptr
        );

        // !!! In the working definition, the "topmost level" of a variadic
        // call is considered to be already evaluated...unless you ask to
        // evaluate it further.  This is what allows `rebSpellInto(v)`
        // to work as well as `rebSpellInto("first", v)`, the idea of
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
        ScanState scan;
        Init_Scan_Level(&scan, SCAN_MASK_NONE, &transcode, '\0');

        // !!! Current hack is to just allow one binder to be passed in for
        // use binding any newly loaded portions (spliced ones are left with
        // their bindings, though there may be special "binding instructions"
        // or otherwise, that get added).
        //
        transcode.context = Get_Context_From_Stack();
        transcode.lib = (transcode.context != Lib_Context)
            ? Lib_Context
            : nullptr;

        struct Reb_Binder binder;
        Init_Interning_Binder(&binder, transcode.context);
        transcode.binder = &binder;

        Option(Error*) error = Scan_To_Stack(&scan);
        Shutdown_Interning_Binder(&binder, transcode.context);

        if (error)
            fail (unwrap error);

        // !!! for now, assume scan went to the end; ultimately it would need
        // to pass the "source".
        //
        L->source->vaptr = nullptr;

        if (TOP_INDEX == base) {
            //
            // This happens when somone says rebValue(..., "", ...) or similar,
            // and gets an empty array from a string scan.  It's not legal
            // to put an END in L->value, and it's unknown if the variadic
            // feed is actually over so as to put null... so get another
            // value out of the va_list and keep going.
            //
            p = va_arg(*L->source->vaptr, const void*);
            goto detect;
        }

        Array* reified = Pop_Stack_Values(base);

        // !!! We really should be able to free this array without managing it
        // when we're done with it, though that can get a bit complicated if
        // there's an error or need to reify into a value.  For now, do the
        // inefficient thing and manage it.
        //
        Manage_Flex(reified);

        L->value = Array_Head(reified);
        L->source->pending = L->value + 1; // may be END
        L->source->array = reified;
        L->source->index = 1;

        break; }

      case DETECTED_AS_STUB: { // "instructions" like rebQ()
        Array* instruction = cast_Array(m_cast(void*, p));

        // The instruction should be unmanaged, and will be freed on the next
        // entry to this routine (optionally copying out its contents into
        // the frame's cell for stable lookback--if necessary).
        //
        assert(Get_Flex_Info(instruction, API_INSTRUCTION));
        assert(Not_Node_Managed(instruction));
        L->value = ARR_SINGLE(instruction);
        break; }

      case DETECTED_AS_FREE:
        panic (p);

      case DETECTED_AS_CELL: {
        const Value* cell = cast(const Value*, p);
          if (Is_Nulled(cell) and Is_Api_Value(cell))
              fail ("NULL cell leaked to API");

        // If the cell is in an API holder with FLEX_INFO_API_RELEASE then
        // it will be released on the *next* call (see top of function)

        L->source->array = nullptr;
        L->value = cell; // note that END is detected separately
        assert(not IS_RELATIVE(L->value));
        break; }

      case DETECTED_AS_END: {
        //
        // We're at the end of the variadic input, so end of the line.
        //
        L->value = END_NODE;
        Corrupt_Pointer_If_Debug(L->source->pending);

        // The va_end() is taken care of here, or if there is a throw/fail it
        // is taken care of by Abort_Level_Core()
        //
        va_end(*L->source->vaptr);
        L->source->vaptr = nullptr;

        // !!! Error reporting expects there to be an array.  The whole story
        // of errors when there's a va_list is not told very well, and what
        // will have to likely happen is that in debug modes, all valists
        // are reified from the beginning, else there's not going to be
        // a way to present errors in context.  Fake an empty array for now.
        //
        L->source->array = EMPTY_ARRAY;
        L->source->index = 0;
        break; }

      default:
        assert(false);
    }
}


//
// Fetch_Next_In_Level() (see notes above)
//
// Once a va_list is "fetched", it cannot be "un-fetched".  Hence only one
// unit of fetch is done at a time, into L->value.  L->source->pending thus
// must hold a signal that data remains in the va_list and it should be
// consulted further.  That signal is an END marker.
//
// More generally, an END marker in L->source->pending for this routine is a
// signal that the vaptr (if any) should be consulted next.
//
INLINE void Fetch_Next_In_Level(
    const Cell* *opt_lookback,
    Level* L
){
    assert(NOT_END(L->value)); // caller should test this first

  #if DEBUG_EXPIRED_LOOKBACK
    if (f->stress) {
        Erase_Cell(f->stress);
        free(f->stress);
        f->stress = nullptr;
    }
  #endif

    // We are changing L->value, and thus by definition any L->gotten value
    // will be invalid.  It might be "wasteful" to always set this to END,
    // especially if it's going to be overwritten with the real fetch...but
    // at a source level, having every call to Fetch_Next_In_Level have to
    // explicitly set L->gotten to null is overkill.  Could be split into
    // a version that just trashes L->gotten in the debug build vs. END.
    //
    L->gotten = nullptr;

    if (NOT_END(L->source->pending)) {
        //
        // We assume the ->pending value lives in a source array, and can
        // just be incremented since the array has FLEX_INFO_HOLD while it
        // is being executed hence won't be relocated or modified.  This
        // means the release build doesn't need to call Array_At().
        //
        assert(
            L->source->array // incrementing plain array of cells
            or L->source->pending == Array_At(L->source->array, L->source->index)
        );

        if (opt_lookback)
            *opt_lookback = L->value; // must be non-movable, GC-safe

        L->value = L->source->pending;

        ++L->source->pending; // might be becoming an END marker, here
        ++L->source->index;
    }
    else if (not L->source->vaptr) {
        //
        // The frame was either never variadic, or it was but got spooled into
        // an array by Reify_Va_To_Array_In_Level().  The first END we hit
        // is the full stop end.
        //
        if (opt_lookback)
            *opt_lookback = L->value; // all values would have been spooled

        L->value = END_NODE;
        Corrupt_Pointer_If_Debug(L->source->pending);

        ++L->source->index; // for consistency in index termination state

        if (Get_Eval_Flag(L, TOOK_FRAME_HOLD)) {
            assert(Get_Flex_Info(L->source->array, HOLD));
            Clear_Flex_Info(L->source->array, HOLD);

            // !!! Future features may allow you to move on to another array.
            // If so, the "hold" bit would need to be reset like this.
            //
            Clear_Eval_Flag(L, TOOK_FRAME_HOLD);
        }
    }
    else {
        // A variadic can source arbitrary pointers, which can be detected
        // and handled in different ways.  Notably, a UTF-8 string can be
        // differentiated and loaded.
        //
        const void *p = va_arg(*L->source->vaptr, const void*);
        L->source->index = TRASHED_INDEX; // avoids warning in release build
        Set_Level_Detected_Fetch(opt_lookback, L, p);
    }

  #if DEBUG_EXPIRED_LOOKBACK
    if (opt_lookback) {
        f->stress = cast(Cell*, malloc(sizeof(Cell)));
        memcpy(f->stress, *opt_lookback, sizeof(Cell));
        *opt_lookback = f->stress;
    }
  #endif
}


INLINE void Quote_Next_In_Level(Value* dest, Level* L) {
    Derelativize(dest, L->value, L->specifier);
    Fetch_Next_In_Level(nullptr, L);
}


INLINE void Abort_Level(Level* L) {
    if (L->varlist and Not_Node_Managed(L->varlist))
        GC_Kill_Flex(L->varlist);  // not alloc'd with manuals tracking
    Corrupt_Pointer_If_Debug(L->varlist);

    // Abort_Level() handles any work that wouldn't be done done naturally by
    // feeding a frame to its natural end.
    //
    if (IS_END(L->value))
        goto pop;

    if (LVL_IS_VALIST(L)) {
        assert(Not_Eval_Flag(L, TOOK_FRAME_HOLD));

        // Aborting valist frames is done by just feeding all the values
        // through until the end.  This is assumed to do any work, such
        // as FLEX_INFO_API_RELEASE, which might be needed on an item.  It
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

        while (NOT_END(L->value))
            Fetch_Next_In_Level(nullptr, L);
    }
    else {
        if (Get_Eval_Flag(L, TOOK_FRAME_HOLD)) {
            //
            // The frame was either never variadic, or it was but got spooled
            // into an array by Reify_Va_To_Array_In_Level()
            //
            assert(Get_Flex_Info(L->source->array, HOLD));
            Clear_Flex_Info(L->source->array, HOLD);
        }
    }

pop:;

    assert(TG_Top_Level == L);
    TG_Top_Level = L->prior;
}


INLINE void Drop_Level_Core(Level* L) {
  #if DEBUG_EXPIRED_LOOKBACK
    free(f->stress);
  #endif

    if (L->varlist) {
        assert(Not_Node_Managed(L->varlist));
        LINK(L->varlist).reuse = TG_Reuse;
        TG_Reuse = L->varlist;
    }
    Corrupt_Pointer_If_Debug(L->varlist);

    assert(TG_Top_Level == L);
    TG_Top_Level = L->prior;
}

INLINE void Drop_Level_Unbalanced(Level* L) {
  #if DEBUG_BALANCE_STATE
    //
    // To avoid slowing down the debug build a lot, Eval_Core_Throws() doesn't
    // check this every cycle, just on drop.  But if it's hard to find which
    // exact cycle caused the problem, see BALANCE_CHECK_EVERY_EVALUATION_STEP
    //
    L->state.stack_base = TOP_INDEX; // e.g. Reduce_To_Stack_Throws()
    L->state.mold_buf_len = Flex_Len(MOLD_BUF); // REMOVE-EACH accumulates
    ASSERT_STATE_BALANCED(&L->state);
  #endif
    Drop_Level_Core(L);
}

INLINE void Drop_Level(Level* L)
{
    if (Get_Eval_Flag(L, TO_END))
        assert(IS_END(L->value) or THROWN(L->out));

    assert(TOP_INDEX == L->stack_base);  // Drop_Level_Core() does not check
    Drop_Level_Unbalanced(L);
}


// This is a very light wrapper over Eval_Core_Throws(), which is used with
// Push_Level_At() for operations like ANY or REDUCE that wish to perform
// several successive operations on an array, without creating a new frame
// each time.
//
INLINE bool Eval_Step_Throws(
    Value* out,
    Level* L
){
    assert(IS_END(out));

    assert(not (L->flags.bits & (EVAL_FLAG_TO_END | EVAL_FLAG_NO_LOOKAHEAD)));
    uintptr_t prior_flags = L->flags.bits;

    L->out = out;
    L->stack_base = TOP_INDEX;
    bool threw = Eval_Core_Throws(L);  // should already be pushed

    // The & on the following line is purposeful.  See Init_Endlike_Header.
    // EVAL_FLAG_NO_LOOKAHEAD may be set by an operation like ELIDE.
    //
    (&L->flags)->bits = prior_flags;

    return threw;
}


// Bit heavier wrapper of Eval_Core_Throws() than Eval_Step_In_Level_Throws().
// It also reuses the frame...but has to clear and restore the frame's
// flags.  It is currently used only by SET-WORD! and SET-PATH!.
//
// Note: Consider pathological case `x: eval the y: eval eval the z: ...`
// This can be done without making a new frame, but the eval cell which holds
// the SET-WORD! needs to be put back in place before returning, so that the
// set knows where to write.  The caller handles this with the data stack.
//
INLINE bool Eval_Step_Mid_Level_Throws(Level* L, Flags flags) {
    assert(L->stack_base == TOP_INDEX);

    Flags prior_flags = L->flags.bits;
    L->flags = Endlike_Header(flags);

    bool threw = Eval_Core_Throws(L); // should already be pushed

    L->flags.bits = prior_flags; // e.g. restore EVAL_FLAG_TO_END
    return threw;
}


// It should not be necessary to use a subframe unless there is meaningful
// state which would be overwritten in the parent frame.  For the moment,
// that only happens if a function call is in effect -or- if a SET-WORD! or
// SET-PATH! are running with an expiring `current` in effect.  Else it is
// more efficient to call Eval_Step_In_Level_Throws(), or the also lighter
// Eval_Step_In_Mid_Level_Throws().
//
// !!! This operation used to try and optimize some cases without using a
// subframe.  But checking for whether an optimization would be legal or not
// was complex, as even something inert like `1` cannot be evaluated into a
// slot as `1` unless you are sure there's no `+` or other infixed operation.
// Over time as the evaluator got more complicated, the redundant work and
// conditional code paths showed a slight *slowdown* over just having an
// inline function that built a frame and recursed Eval_Core_Throws().
//
// Future investigation could attack the problem again and see if there is
// any common case that actually offered an advantage to optimize for here.
//
INLINE bool Eval_Step_In_Subframe_Throws(
    Value* out,
    Level* higher,  // may not be direct parent (not child->prior upon push!)
    Flags flags,
    Level* child  // passed w/base preload, refinements can be on stack
){
    child->out = out;

    // !!! Should they share a source instead of updating?
    //
    assert(child->source == higher->source);
    child->value = higher->value;
    child->gotten = higher->gotten;
    child->specifier = higher->specifier;

    // L->gotten is never marked for GC, because it should never be kept
    // alive across arbitrary evaluations (L->value should keep it alive).
    // We'll write it back with an updated value from the child after the
    // call, and no one should be able to read it until then (e.g. the caller
    // can't be a variadic frame that is executing yet)
    //
  #if RUNTIME_CHECKS
    Corrupt_Pointer_If_Debug(higher->gotten);
    REBLEN old_index = higher->source->index;
  #endif

    child->flags = Endlike_Header(flags);

    // One case in which child->prior on this push may not be equal to the
    // higher frame passed in is variadics.  The frame making the call to
    // advance the variadic feed can be deeper down the stack, and it will
    // be the ->prior, so it's important not to corrupt it based on assuming
    // it is the variadic frame.
    //
    Push_Level_Core(child);
    Reuse_Varlist_If_Available(child);
    bool threw = Eval_Core_Throws(child);
    Drop_Level(child);

    assert(
        IS_END(child->value)
        or LVL_IS_VALIST(child)
        or old_index != child->source->index
        or (flags & EVAL_FLAG_REEVALUATE_CELL)
        or threw
    );

    // !!! Should they share a source instead of updating?
    //
    higher->value = child->value;
    higher->gotten = child->gotten;
    assert(higher->specifier == child->specifier); // !!! can't change?

    return threw;
}


// Most common case of evaluator invocation in Rebol: the data lives in an
// array series.
//
INLINE REBIXO Eval_At_Core(
    Value* out, // must be initialized, marked stale if empty / all invisible
    const Cell* opt_first, // non-array element to kick off execution with
    Array* array,
    REBLEN index,
    Specifier* specifier, // must match array, but also opt_first if relative
    Flags flags // EVAL_FLAG_TO_END, etc.
){
    DECLARE_LEVEL (L);
    L->flags = Endlike_Header(flags); // SET_FRAME_VALUE() *could* use

    L->source->vaptr = nullptr;
    L->source->array = array;
    L->source->deferring_infix = false;

    L->gotten = nullptr; // SET_FRAME_VALUE() asserts this is nullptr

    if (opt_first) {
        SET_FRAME_VALUE(L, opt_first);
        L->source->index = index;
        L->source->pending = Array_At(array, index);
        assert(NOT_END(L->value));
    }
    else {
        SET_FRAME_VALUE(L, Array_At(array, index));
        L->source->index = index + 1;
        L->source->pending = L->value + 1;
        if (IS_END(L->value))
            return END_FLAG;
    }

    L->out = out;
    L->specifier = specifier;

    Push_Level_Core(L);
    Reuse_Varlist_If_Available(L);
    bool threw = Eval_Core_Throws(L);
    Drop_Level(L);

    if (threw)
        return THROWN_FLAG;

    assert(
        not (flags & EVAL_FLAG_TO_END)
        or L->source->index == Array_Len(array) + 1
    );
    return L->source->index;
}


//
//  Reify_Va_To_Array_In_Level: C
//
// For performance and memory usage reasons, a variadic C function call that
// wants to invoke the evaluator with just a comma-delimited list of Value*
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
INLINE void Reify_Va_To_Array_In_Level(
    Level* L,
    bool truncated
) {
    StackIndex base = TOP_INDEX;

    assert(LVL_IS_VALIST(L));

    if (truncated)
        Init_Word(PUSH(), CANON(__OPTIMIZED_OUT__));

    if (NOT_END(L->value)) {
        assert(L->source->pending == END_NODE);

        do {
            assert(not Is_Antiform(L->value));
            Derelativize(PUSH(), L->value, L->specifier);

            Fetch_Next_In_Level(nullptr, L);
        } while (NOT_END(L->value));

        if (truncated)
            L->source->index = 2; // skip the --optimized-out--
        else
            L->source->index = 1; // position at start of the extracted values
    }
    else {
        assert(Is_Pointer_Corrupt_Debug(L->source->pending));

        // Leave at end of frame, but give back the array to serve as
        // notice of the truncation (if it was truncated)
        //
        L->source->index = 0;
    }

    assert(not L->source->vaptr); // feeding forward should have called va_end

    // special array...may contain voids and eval flip is kept
    L->source->array = Pop_Stack_Values(base);
    Manage_Flex(L->source->array); // held alive while frame running

    // The array just popped into existence, and it's tied to a running
    // frame...so safe to say we're holding it.  (This would be more complex
    // if we reused the empty array if base == TOP_INDEX, since someone else
    // might have a hold on it...not worth the complexity.)
    //
    Set_Flex_Info(L->source->array, HOLD);
    Set_Eval_Flag(L, TOOK_FRAME_HOLD);

    if (truncated)
        SET_FRAME_VALUE(L, Array_At(L->source->array, 1)); // skip `--optimized--`
    else
        SET_FRAME_VALUE(L, Array_Head(L->source->array));

    L->source->pending = L->value + 1;
}


// (va_list by pointer: http://stackoverflow.com/a/3369762/211160)
//
// Central routine for doing an evaluation of an array of values by calling
// a C function with those parameters (e.g. supplied as arguments, separated
// by commas).  Uses same method to do so as functions like printf() do.
//
// The evaluator has a common means of fetching values out of both arrays
// and C va_lists via Fetch_Next_In_Level(), so this code can behave the
// same as if the passed in values came from an array.
//
// !!! C's va_lists are very dangerous, there is no type checking!  The
// C++ build should be able to check this for the callers of this function
// *and* check that you ended properly.  It means this function will need
// two different signatures (and so will each caller of this routine).
//
// Returns THROWN_FLAG, END_FLAG, or VA_LIST_FLAG
//
INLINE REBIXO Eval_Va_Core(
    Value* out, // must be initialized, marked stale if empty / all invisible
    const void *opt_first,
    va_list *vaptr,
    Flags flags
){
    DECLARE_LEVEL (L);
    L->flags = Endlike_Header(flags); // read by Set_Level_Detected_Fetch

    L->source->index = TRASHED_INDEX; // avoids warning in release build
    L->source->array = nullptr;
    L->source->vaptr = vaptr;
    L->source->pending = END_NODE; // signal next fetch comes from va_list
    L->source->deferring_infix = false;

    // We reuse logic in Fetch_Next_In_Level() and Set_Level_Detected_Fetch()
    // but the previous L->value will be tested for NODE_FLAG_ROOT.
    //
    DECLARE_VALUE (junk);
    L->value = Init_Unreadable(junk); // shows where garbage came from

    if (opt_first)
        Set_Level_Detected_Fetch(nullptr, L, opt_first);
    else
        Fetch_Next_In_Level(nullptr, L);

    if (IS_END(L->value))
        return END_FLAG;

    L->out = out;
    L->specifier = SPECIFIED; // relative values not allowed in va_lists
    L->gotten = nullptr;

    Push_Level_Core(L);
    Reuse_Varlist_If_Available(L);
    bool threw = Eval_Core_Throws(L);
    Drop_Level(L); // will va_end() if not reified during evaluation

    if (threw)
        return THROWN_FLAG;

    if (
        (flags & EVAL_FLAG_TO_END) // not just an EVALUATE, but a full DO
        or Get_Cell_Flag(L->out, OUT_MARKED_STALE) // just ELIDEs and COMMENTs
    ){
        assert(IS_END(L->value));
        return END_FLAG;
    }

    if ((flags & EVAL_FLAG_NO_RESIDUE) and NOT_END(L->value))
        fail (Error_Apply_Too_Many_Raw());

    return VA_LIST_FLAG; // frame may be at end, next call might just END_FLAG
}


INLINE bool Eval_Value_Core_Throws(
    Value* out,
    const Cell* value, // e.g. a BLOCK! here would just evaluate to itself!
    Specifier* specifier
){
    REBIXO indexor = Eval_At_Core(
        SET_END(out), // start with END to detect no actual eval product
        value, // put the value as the opt_first element
        EMPTY_ARRAY,
        0, // start index (it's an empty array, there's no added processing)
        specifier,
        EVAL_FLAG_TO_END
    );

    if (IS_END(out))
        fail ("Eval_Value_Core_Throws() empty or just COMMENTs/ELIDEs");

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
INLINE void Handle_Api_Dispatcher_Result(Level* L, const Value* r) {
    assert(not THROWN(r)); // only L->out can return thrown cells

  #if RUNTIME_CHECKS
    if (Not_Node_Root_Bit_Set(r)) {
        printf("dispatcher returned non-API value not in OUT\n");
      #if DEBUG_FRAME_LABELS
        printf("during ACTION!: %s\n", L->label_utf8);
      #endif
        printf("`return OUT;` or use `RETURN (non_api_cell);`\n");
        panic(r);
    }
  #endif

    if (Is_Nulled(r))
        assert(!"Dispatcher returned nulled cell, not C nullptr for API use");

    Copy_Cell(L->out, r);
    if (Not_Node_Managed(r))
        rebRelease(r);
}
