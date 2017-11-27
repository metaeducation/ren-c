//
//  File: %c-error.c
//  Summary: "error handling"
//  Section: core
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


#include "sys-core.h"


//
//  Snap_State_Core: C
//
// Used by SNAP_STATE, PUSH_TRAP, and PUSH_UNHALTABLE_TRAP.
//
// **Note:** Modifying this routine likely means a necessary modification to
// both `Assert_State_Balanced_Debug()` and `Trapped_Helper_Halted()`.
//
void Snap_State_Core(struct Reb_State *s)
{
    // See remarks in Set_Stack_Limit() for why this is needed as part of
    // PUSH_UNHALTABLE_TRAP, in light of multithreading as in Ren Garden.
    // It's not ideal, but it works around a problem for the moment.
    //
    s->stack_limit = Stack_Limit;

    s->dsp = DSP;
    s->top_chunk = TG_Top_Chunk;

    // There should not be a Collect_Keys in progress.  (We use a non-zero
    // length of the collect buffer to tell if a later fail() happens in
    // the middle of a Collect_Keys.)
    //
    assert(ARR_LEN(BUF_COLLECT) == 0);

    s->guarded_len = SER_LEN(GC_Guarded);
    s->frame = FS_TOP;

    s->manuals_len = SER_LEN(GC_Manuals);
    s->mold_buf_len = SER_LEN(MOLD_BUF);
    s->mold_loop_tail = ARR_LEN(TG_Mold_Stack);

    // !!! Is this initialization necessary?
    s->error = NULL;
}


#if !defined(NDEBUG)

//
//  Assert_State_Balanced_Debug: C
//
// Check that all variables in `state` have returned to what they were at
// the time of snapshot.
//
void Assert_State_Balanced_Debug(
    struct Reb_State *s,
    const char *file,
    int line
){
    if (s->dsp != DSP) {
        printf(
            "DS_PUSH()x%d without DS_POP/DS_DROP\n",
            DSP - s->dsp
        );
        panic_at (NULL, file, line);
    }

    assert(s->top_chunk == TG_Top_Chunk);

    assert(s->frame == FS_TOP);

    assert(ARR_LEN(BUF_COLLECT) == 0);

    if (s->guarded_len != SER_LEN(GC_Guarded)) {
        printf(
            "PUSH_GUARD()x%d without DROP_GUARD()\n",
            cast(int, SER_LEN(GC_Guarded) - s->guarded_len)
        );
        REBNOD *guarded = *SER_AT(
            REBNOD*,
            GC_Guarded,
            SER_LEN(GC_Guarded) - 1
        );
        panic_at (guarded, file, line);
    }

    // !!! Note that this inherits a test that uses GC_Manuals->content.xxx
    // instead of SER_LEN().  The idea being that although some series
    // are able to fit in the series node, the GC_Manuals wouldn't ever
    // pay for that check because it would always be known not to.  Review
    // this in general for things that may not need "series" overhead,
    // e.g. a contiguous pointer stack.
    //
    if (s->manuals_len > SER_LEN(GC_Manuals)) {
        //
        // Note: Should this ever actually happen, panic() on the series won't
        // do any real good in helping debug it.  You'll probably need to
        // add additional checking in the Manage_Series and Free_Series
        // routines that checks against the caller's manuals_len.
        //
        panic_at ("manual series freed outside checkpoint", file, line);
    }
    else if (s->manuals_len < SER_LEN(GC_Manuals)) {
        printf(
            "Make_Series()x%d without Free_Series or MANAGE_SERIES\n",
            cast(int, SER_LEN(GC_Manuals) - s->manuals_len)
        );
        REBSER *manual = *(SER_AT(
            REBSER*,
            GC_Manuals,
            SER_LEN(GC_Manuals) - 1
        ));
        panic_at (manual, file, line);
    }

    assert(s->mold_buf_len == SER_LEN(MOLD_BUF));
    assert(s->mold_loop_tail == ARR_LEN(TG_Mold_Stack));

    assert(s->error == NULL); // !!! necessary?
}

#endif


//
//  Trapped_Helper_Halted: C
//
// This is used by both PUSH_TRAP and PUSH_UNHALTABLE_TRAP to do the work of
// responding to a longjmp.  (Hence it is run when setjmp returns TRUE.)  Its
// job is to safely recover from a sudden interruption, though the list of
// things which can be safely recovered from is finite.
//
// (Among the countless things that are not handled automatically would be a
// memory allocation via malloc().)
//
// Note: This is a crucial difference between C and C++, as C++ will walk up
// the stack at each level and make sure any constructors have their
// associated destructors run.  *Much* safer for large systems, though not
// without cost.  Rebol's greater concern is not so much the cost of setup for
// stack unwinding, but being written without requiring a C++ compiler.
//
// Returns whether the trapped error was a RE_HALT or not.
//
REBOOL Trapped_Helper_Halted(struct Reb_State *s)
{
    ASSERT_CONTEXT(s->error);
    assert(CTX_TYPE(s->error) == REB_ERROR);

    REBOOL halted = LOGICAL(ERR_NUM(s->error) == RE_HALT);

    // Restore Rebol data stack pointer at time of Push_Trap
    //
    DS_DROP_TO(s->dsp);

    // Drop to the chunk state at the time of Push_Trap
    //
    while (TG_Top_Chunk != s->top_chunk)
        Drop_Chunk_Of_Values(NULL);

    // If we were in the middle of a Collect_Keys and an error occurs, then
    // the binding lookup table has entries in it that need to be zeroed out.
    // We can tell if that's necessary by whether there is anything
    // accumulated in the collect buffer.
    //
    if (ARR_LEN(BUF_COLLECT) != 0)
        Collect_End(NULL); // !!! No binder, review implications

    // Free any manual series that were extant at the time of the error
    // (that were created since this PUSH_TRAP started).  This includes
    // any arglist series in call frames that have been wiped off the stack.
    // (Closure series will be managed.)
    //
    assert(SER_LEN(GC_Manuals) >= s->manuals_len);
    while (SER_LEN(GC_Manuals) != s->manuals_len) {
        // Freeing the series will update the tail...
        Free_Series(
            *SER_AT(REBSER*, GC_Manuals, SER_LEN(GC_Manuals) - 1)
        );
    }

    SET_SERIES_LEN(GC_Guarded, s->guarded_len);
    TG_Frame_Stack = s->frame;
    TERM_SEQUENCE_LEN(MOLD_BUF, s->mold_buf_len);

#if !defined(NDEBUG)
    //
    // Because reporting errors in the actual Push_Mold process leads to
    // recursion, this debug flag helps make it clearer what happens if
    // that does happen... and can land on the right comment.  But if there's
    // a fail of some kind, the flag for the warning needs to be cleared.
    //
    TG_Pushing_Mold = FALSE;
#endif

    SET_SERIES_LEN(TG_Mold_Stack, s->mold_loop_tail);

    Saved_State = s->last_state;
    Stack_Limit = s->stack_limit;

    return halted;
}


//
//  Fail_Core: C
//
// Cause a "trap" of an error by longjmp'ing to the enclosing PUSH_TRAP (or
// PUSH_UNHALTABLE_TRAP).  Note that these failures interrupt code mid-stream,
// so if a Rebol function is running it will not make it to the point of
// returning the result value.  This distinguishes the "fail" mechanic from
// the "throw" mechanic, which has to bubble up a THROWN() value through
// D_OUT (used to implement BREAK, CONTINUE, RETURN, LEAVE...)
//
// The function will auto-detect if the pointer it is given is an ERROR!'s
// REBCTX*, a REBVAL*, or a UTF-8 string.  If it's a string, an error will be
// created from it automatically.  If it's a value, then it is turned into
// the ubiquitous (and kind of lame) "Invalid Arg" error.
//
// Note: Over the long term, one does not want to hard-code error strings in
// the executable.  That makes them more difficult to hook with translations,
// or to identify systemically with some kind of "error code".  However,
// it's a realistic quick-and-dirty way of delivering a more meaningful
// error than just using a RE_MISC error code, and can be found just as easily
// to clean up later.
//
ATTRIBUTE_NO_RETURN void Fail_Core(const void *p)
{
    REBCTX *error;

    switch (Detect_Rebol_Pointer(p)) {
    case DETECTED_AS_UTF8: {
        error = Error_User(cast(const char*, p));
        break; }

    case DETECTED_AS_SERIES: {
        REBSER *s = m_cast(REBSER*, cast(const REBSER*, p)); // don't mutate
        if (NOT_SER_FLAG(s, ARRAY_FLAG_VARLIST))
            panic (s);
        error = CTX(s);
        break; }

    case DETECTED_AS_VALUE: {
        const REBVAL *v = cast(const REBVAL*, p);
        error = Error_Invalid_Arg_Raw(v);
        break; }

    default:
        panic (p); // suppress compiler error from non-smart compilers
    }

    ASSERT_CONTEXT(error);
    assert(CTX_TYPE(error) == REB_ERROR);

    // If we raise the error we'll lose the stack, and if it's an early
    // error we always want to see it (do not use ATTEMPT or TRY on
    // purpose in Startup_Core()...)
    //
    if (PG_Boot_Phase < BOOT_DONE)
        panic (error);

    // There should be a PUSH_TRAP of some kind in effect if a `fail` can
    // ever be run.
    //
    if (Saved_State == NULL)
        panic (error);

    // The information for the Rebol call frames generally is held in stack
    // variables, so the data will go bad in the longjmp.  We have to free
    // the data *before* the jump.  Be careful not to let this code get too
    // recursive or do other things that would be bad news if we're responding
    // to C_STACK_OVERFLOWING.  (See notes on the sketchiness in general of
    // the way R3-Alpha handles stack overflows, and alternative plans.)
    //
    REBFRM *f = FS_TOP;
    while (f != Saved_State->frame) {
        if (Is_Function_Frame(f)) {
            const REBOOL drop_chunks = FALSE;
            Drop_Function_Core(f, drop_chunks);
        }

        // See notes in Do_Va_Core() about how it is required by C standard
        // to call va_end() after va_start().  If we longjmp past the point
        // that called va_start(), we have to clean up the va_list else there
        // could be undefined behavior.
        //
        if (FRM_IS_VALIST(f))
            va_end(*f->source.vaptr);

        REBFRM *prior = f->prior;
        Abort_Frame_Core(f); // will call va_end() if variadic frame
        f = prior;
    }

    TG_Frame_Stack = f; // TG_Frame_Stack is writable FS_TOP

    Saved_State->error = error;

    // If a THROWN() was being processed up the stack when the error was
    // raised, then it had the thrown argument set.  Trash it in debug
    // builds.  (The value will not be kept alive, it is not seen by GC)
    //
    Init_Unreadable_Blank(&TG_Thrown_Arg);

    LONG_JUMP(Saved_State->cpu_state, 1);
}


//
//  Stack_Depth: C
//
REBCNT Stack_Depth(void)
{
    REBCNT depth = 0;

    REBFRM *f = FS_TOP;
    while (f) {
        if (Is_Function_Frame(f))
            if (NOT(Is_Function_Frame_Fulfilling(f))) {
                //
                // We only count invoked functions (not group or path
                // evaluations or "pending" functions that are building their
                // arguments but have not been formally invoked yet)
                //
                ++depth;
            }

        f = FRM_PRIOR(f);
    }

    return depth;
}


//
//  Find_Error_For_Code: C
//
// Find the id word, the error type (category) word, and the error
// message template block-or-string for a given error number.
//
// This scans the data which is loaded into the boot file by
// processing %errors.r
//
// If the message is not found, return NULL.  Will not write to
// `id_out` or `type_out` unless returning a non-NULL pointer.
//
const REBVAL *Find_Error_For_Code(REBVAL *id_out, REBVAL *type_out, REBCNT code)
{
    REBCNT n;

    // See %errors.r for the list of data which is loaded into the boot
    // file as objects for the "error catalog"
    //
    REBCTX *categories = VAL_CONTEXT(Get_System(SYS_CATALOG, CAT_ERRORS));
    assert(CTX_KEY_SYM(categories, 1) == SYM_SELF);

    // Find the correct catalog category
    n = code / RE_CATEGORY_SIZE; // 0 for Special, 1 for Internal...
    if (SELFISH(n + 1) > CTX_LEN(categories)) // 1-based, not 0 based
        return NULL;

    // Get context of object representing the elements of the category itself
    if (!IS_OBJECT(CTX_VAR(categories, SELFISH(n + 1)))) {
        assert(FALSE);
        return NULL;
    }

    REBCTX *category = VAL_CONTEXT(CTX_VAR(categories, SELFISH(n + 1)));
    assert(CTX_KEY_SYM(category, 1) == SYM_SELF);

    // Find the correct template in the catalog category (see %errors.r)
    n = code % RE_CATEGORY_SIZE; // 0-based order within category
    if (SELFISH(n + 2) > CTX_LEN(category)) // 1-based (CODE: TYPE:)
        return NULL;

    // Sanity check CODE: field of category object
    if (!IS_INTEGER(CTX_VAR(category, SELFISH(1)))) {
        assert(FALSE);
        return NULL;
    }
    assert(
        (code / RE_CATEGORY_SIZE) * RE_CATEGORY_SIZE
        == cast(REBCNT, VAL_INT32(CTX_VAR(category, SELFISH(1))))
    );

    // Sanity check TYPE: field of category object
    // !!! Same spelling as what we set in VAL_WORD_SYM(type_out))?
    if (!IS_STRING(CTX_VAR(category, SELFISH(2)))) {
        assert(FALSE);
        return NULL;
    }

    REBVAL *message = CTX_VAR(category, SELFISH(n + 3));

    // Error message template must be string or block
    assert(IS_BLOCK(message) || IS_STRING(message));

    // Success! Write category word from the category list context key sym,
    // and specific error ID word from the context key sym within category
    //
    Init_Word(
        type_out,
        CTX_KEY_SPELLING(categories, SELFISH((code / RE_CATEGORY_SIZE) + 1))
    );
    Init_Word(
        id_out,
        CTX_KEY_SPELLING(category, SELFISH((code % RE_CATEGORY_SIZE) + 3))
    );

    return message;
}


//
//  Set_Location_Of_Error: C
//
// Since errors are generally raised to stack levels above their origin, the
// stack levels causing the error are no longer running by the time the
// error object is inspected.  A limited snapshot of context information is
// captured in the WHERE and NEAR fields, and some amount of file and line
// information may be captured as well.
//
// The information is derived from the current execution position and stack
// depth of a running frame.  Also, if running from a C fail() call, the
// file and line information can be captured in the debug build.
//
void Set_Location_Of_Error(
    REBCTX *error,
    REBFRM *where // must be valid and executing on the stack
) {
    assert(where != NULL);

    REBDSP dsp_orig = DSP;

    ERROR_VARS *vars = ERR_VARS(error);

    // WHERE is a backtrace in the form of a block of label words, that start
    // from the top of stack and go downward.
    //
    REBFRM *f = where;
    for (; f != NULL; f = f->prior) {
        //
        // Only invoked functions (not pending functions, groups, etc.)
        //
        if (NOT(Is_Function_Frame(f)))
            continue;
        if (Is_Function_Frame_Fulfilling(f))
            continue;

        DS_PUSH_TRASH;
        Get_Frame_Label_Or_Blank(DS_TOP, f);
    }
    Init_Block(&vars->where, Pop_Stack_Values(dsp_orig));

    // Nearby location of the error.  Reify any valist that is running,
    // so that the error has an array to present.
    //
    Init_Near_For_Frame(&vars->nearest, where);

    // Try to fill in the file and line information of the error from the
    // stack, looking for arrays with SERIES_FLAG_FILE_LINE.
    //
    f = where;
    for (; f != NULL; f = f->prior) {
        if (FRM_IS_VALIST(f))
            continue;
        if (NOT_SER_FLAG(f->source.array, SERIES_FLAG_FILE_LINE))
            continue;
        break;
    }
    if (f != NULL) {
        Init_Word(&vars->file, LINK(f->source.array).file);
        Init_Integer(&vars->line, MISC(f->source.array).line);
    }
}


//
//  Make_Error_Object_Throws: C
//
// Creates an error object from arg and puts it in value.
// The arg can be a string or an object body block.
//
// Returns TRUE if a THROWN() value is made during evaluation.
//
// This function is called by MAKE ERROR!.  Note that most often
// system errors from %errors.r are thrown by C code using
// Make_Error(), but this routine accommodates verification of
// errors created through user code...which may be mezzanine
// Rebol itself.  A goal is to not allow any such errors to
// be formed differently than the C code would have made them,
// and to cross through the point of R3-Alpha error compatibility,
// which makes this a rather tortured routine.  However, it
// maps out the existing landscape so that if it is to be changed
// then it can be seen exactly what is changing.
//
REBOOL Make_Error_Object_Throws(
    REBVAL *out, // output location **MUST BE GC SAFE**!
    const REBVAL *arg
) {
    // Frame from the error object template defined in %sysobj.r
    //
    REBCTX *root_error = VAL_CONTEXT(Get_System(SYS_STANDARD, STD_ERROR));

    REBCTX *error;
    ERROR_VARS *vars; // C struct mirroring fixed portion of error fields

    if (IS_ERROR(arg) || IS_OBJECT(arg)) {
        // Create a new error object from another object, including any
        // non-standard fields.  WHERE: and NEAR: will be overridden if
        // used.  If ID:, TYPE:, or CODE: were used in a way that would
        // be inconsistent with a Rebol system error, an error will be
        // raised later in the routine.

        error = Merge_Contexts_Selfish(root_error, VAL_CONTEXT(arg));
        vars = ERR_VARS(error);
    }
    else if (IS_BLOCK(arg)) {
        // If a block, then effectively MAKE OBJECT! on it.  Afterward,
        // apply the same logic as if an OBJECT! had been passed in above.

        // Bind and do an evaluation step (as with MAKE OBJECT! with A_MAKE
        // code in REBTYPE(Context) and code in REBNATIVE(construct))

        error = Make_Selfish_Context_Detect(
            REB_ERROR, // type
            VAL_ARRAY_AT(arg), // values to scan for toplevel set-words
            root_error // parent
        );

        // Protect the error from GC by putting into out, which must be
        // passed in as a GC-protecting value slot.
        //
        Init_Error(out, error);

        Rebind_Context_Deep(root_error, error, NULL); // NULL=>no more binds
        Bind_Values_Deep(VAL_ARRAY_AT(arg), error);

        DECLARE_LOCAL (evaluated);
        if (Do_Any_Array_At_Throws(evaluated, arg)) {
            Move_Value(out, evaluated);
            return TRUE;
        }

        vars = ERR_VARS(error);
    }
    else if (IS_STRING(arg)) {
        //
        // String argument to MAKE ERROR! makes a custom error from user:
        //
        //     code: _ ;-- default is blank
        //     type: _
        //     id: _
        //     message: "whatever the string was"
        //
        // Minus the message, this is the default state of root_error.

        error = Copy_Context_Shallow(root_error);

        // !!! fix in Startup_Errors()?
        //
        VAL_RESET_HEADER(CTX_VALUE(error), REB_ERROR);

        vars = ERR_VARS(error);
        assert(IS_BLANK(&vars->code));
        assert(IS_BLANK(&vars->type));
        assert(IS_BLANK(&vars->id));

        Init_String(&vars->message, Copy_Sequence_At_Position(arg));
    }
    else {
        // No other argument types are handled by this routine at this time.

        fail (Error_Invalid_Error_Raw(arg));
    }

    // Validate the error contents, and reconcile message template and ID
    // information with any data in the object.  Do this for the IS_STRING
    // creation case just to make sure the rules are followed there too.

    // !!! Note that this code is very cautious because the goal isn't to do
    // this as efficiently as possible, rather to put up lots of alarms and
    // traffic cones to make it easy to pick and choose what parts to excise
    // or tighten in an error enhancement upgrade.

    if (IS_INTEGER(&vars->code)) {
        assert(VAL_INT32(&vars->code) != RE_USER); // not real code, use blank

        // Users can make up anything for error codes allocated to them,
        // but Rebol's historical default is to "own" error codes less
        // than RE_USER.  If a code is used in the sub-RE_USER range then
        // make sure any id or type provided do not conflict.

        if (!IS_BLANK(&vars->message)) // assume a MESSAGE: is wrong
            fail (Error_Invalid_Error_Raw(arg));

        DECLARE_LOCAL (id);
        DECLARE_LOCAL (type);
        const REBVAL *message = Find_Error_For_Code(
            id,
            type,
            cast(REBCNT, VAL_INT32(&vars->code))
        );

        if (message == NULL)
            fail (Error_Invalid_Error_Raw(arg));

        Move_Value(&vars->message, message);

        if (!IS_BLANK(&vars->id)) {
            if (
                !IS_WORD(&vars->id)
                || VAL_WORD_CANON(&vars->id) != VAL_WORD_CANON(id)
            ){
                fail (Error_Invalid_Error_Raw(arg));
            }
        }
        Move_Value(&vars->id, id); // binding and case normalized

        if (!IS_BLANK(&vars->type)) {
            if (
                !IS_WORD(&vars->id)
                || VAL_WORD_CANON(&vars->type) != VAL_WORD_CANON(type)
            ){
                fail (Error_Invalid_Error_Raw(arg));
            }
        }
        Move_Value(&vars->type, type); // binding and case normalized

        // !!! TBD: Check that all arguments were provided!
    }
    else if (IS_WORD(&vars->type) && IS_WORD(&vars->id)) {
        // If there was no CODE: supplied but there was a TYPE: and ID: then
        // this may overlap a combination used by Rebol where we wish to
        // fill in the code.  (No fast lookup for this, must search.)

        REBCTX *categories = VAL_CONTEXT(Get_System(SYS_CATALOG, CAT_ERRORS));

        assert(IS_BLANK(&vars->code));

        // Find correct category for TYPE: (if any)
        REBVAL *category
            = Select_Canon_In_Context(categories, VAL_WORD_CANON(&vars->type));

        if (category) {
            assert(IS_OBJECT(category));
            assert(VAL_CONTEXT_KEY_SYM(category, 1) == SYM_SELF);
            assert(VAL_CONTEXT_KEY_SYM(category, SELFISH(1)) == SYM_CODE);
            assert(IS_INTEGER(VAL_CONTEXT_VAR(category, SELFISH(1))));

            REBCNT code = cast(REBCNT,
                VAL_INT32(VAL_CONTEXT_VAR(category, SELFISH(1)))
            );

            assert(VAL_CONTEXT_KEY_SYM(category, SELFISH(2)) == SYM_TYPE);
            assert(IS_STRING(VAL_CONTEXT_VAR(category, SELFISH(2))));

            // Find correct message for ID: (if any)

            REBVAL *message = Select_Canon_In_Context(
                VAL_CONTEXT(category), VAL_WORD_CANON(&vars->id)
            );

            if (message) {
                assert(IS_STRING(message) || IS_BLOCK(message));

                if (!IS_BLANK(&vars->message))
                    fail (Error_Invalid_Error_Raw(arg));

                Move_Value(&vars->message, message);

                Init_Integer(&vars->code,
                    code
                    + Find_Canon_In_Context(
                        error, VAL_WORD_CANON(&vars->id), FALSE
                    )
                    - Find_Canon_In_Context(error, Canon(SYM_TYPE), FALSE)
                    - 1
                );
            }
            else {
                // At the moment, we don't let the user make a user-ID'd
                // error using a category from the internal list just
                // because there was no id from that category.  In effect
                // all the category words have been "reserved"

                // !!! Again, remember this is all here just to show compliance
                // with what the test suite tested for, it disallowed e.g.
                // it expected the following to be an illegal error because
                // the `script` category had no `set-self` error ID.
                //
                //     make error! [type: 'script id: 'set-self]

                fail (Error_Invalid_Error_Raw(arg));
            }
            assert(IS_INTEGER(&vars->code));
        }
        else {
            // The type and category picked did not overlap any existing one
            // so let it be a user error.
            //
            assert(IS_BLANK(&vars->code));
            Init_Blank(&vars->code);
        }
    }
    else {
        // It's either a user-created error or otherwise.  It may
        // have bad ID, TYPE, or message fields, or a completely
        // strange code #.  The question of how non-standard to
        // tolerate is an open one.

        // For now we just write blank into the error code field, if that was
        // not already there.

        if (NOT(IS_BLANK(&vars->code)))
            fail (Error_Invalid_Error_Raw(arg));

        // !!! Because we will experience crashes in the molding logic,
        // we put some level of requirement besides "code # not 0".
        // This is conservative logic and not good for general purposes.

        if (
            !(IS_WORD(&vars->id) || IS_BLANK(&vars->id))
            || !(IS_WORD(&vars->type) || IS_BLANK(&vars->type))
            || !(
                IS_BLOCK(&vars->message)
                || IS_STRING(&vars->message)
                || IS_BLANK(&vars->message)
            )
        ) {
            fail (Error_Invalid_Error_Raw(arg));
        }
    }

    // There might be no Rebol code running when the error is created (e.g.
    // the static creation of the stack overflow error before any code runs)
    //
    if (FS_TOP != NULL)
        Set_Location_Of_Error(error, FS_TOP);

    Init_Error(out, error);
    return FALSE;
}


//
//  Make_Error_Managed_Core: C
//
// (WARNING va_list by pointer: http://stackoverflow.com/a/3369762/211160)
//
// Create and init a new error object based on a C va_list and an error code.
// It knows how many arguments the error particular error ID requires based
// on the templates defined in %errors.r.
//
// If the error code RE_USER is used, then the error will have
//
// This routine should either succeed and return to the caller, or panic()
// and crash if there is a problem (such as running out of memory, or that
// %errors.r has not been loaded).  Hence the caller can assume it will
// regain control to properly call va_end with no longjmp to skip it.
//
REBCTX *Make_Error_Managed_Core(REBCNT code, va_list *vaptr)
{
    assert(code != 0);

    if (PG_Boot_Phase < BOOT_ERRORS) { // no STD_ERROR or template table yet
    #if !defined(NDEBUG)
        printf(
            "fail() before object table initialized, code = %d\n",
            cast(int, code)
        );
    #endif

        DECLARE_LOCAL (code_value);
        Init_Integer(code_value, code);

        panic (code_value);
    }

    REBCTX *root_error = VAL_CONTEXT(Get_System(SYS_STANDARD, STD_ERROR));

    DECLARE_LOCAL (id);
    DECLARE_LOCAL (type);
    const REBVAL *message;
    if (code == RE_USER) {
        Init_Blank(id);
        Init_Blank(type);
        message = va_arg(*vaptr, const REBVAL*);
    }
    else
        message = Find_Error_For_Code(id, type, code);

    assert(message != NULL);

    REBCNT expected_args = 0;
    if (IS_BLOCK(message)) { // GET-WORD!s in template should match va_list
        RELVAL *temp = VAL_ARRAY_HEAD(message);
        for (; NOT_END(temp); ++temp) {
            if (IS_GET_WORD(temp))
                ++expected_args;
            else
                assert(IS_STRING(temp));
        }
    }
    else // Just a string, no arguments expected.
        assert(IS_STRING(message));

    REBCTX *error;
    if (expected_args == 0) {
        // If there are no arguments, we don't need to make a new keylist...
        // just a new varlist to hold this instance's settings. (root
        // error keylist is already managed)

        error = Copy_Context_Shallow(root_error);

        // !!! Should tweak root error during boot so it actually is an ERROR!
        // (or use literal error construction syntax, if it worked?)
        //
        VAL_RESET_HEADER(CTX_VALUE(error), REB_ERROR);
    }
    else {
        // !!! See remarks on how the modern way to handle this may be to
        // put error arguments in the error object, and then have the META-OF
        // hold the generic error parameters.  Investigate how this ties in
        // with user-defined types.

        REBCNT root_len = CTX_LEN(root_error);

        // Should the error be well-formed, we'll need room for the new
        // expected values *and* their new keys in the keylist.
        //
        error = Copy_Context_Shallow_Extra(root_error, expected_args);

        // !!! Should tweak root error during boot so it actually is an ERROR!
        // (or use literal error construction syntax, if it worked?)
        //
        VAL_RESET_HEADER(CTX_VALUE(error), REB_ERROR);

        // Fix up the tail first so CTX_KEY and CTX_VAR don't complain
        // in the debug build that they're accessing beyond the error length
        //
        TERM_ARRAY_LEN(CTX_VARLIST(error), root_len + expected_args + 1);
        TERM_ARRAY_LEN(CTX_KEYLIST(error), root_len + expected_args + 1);

        REBVAL *key = CTX_KEY(error, root_len) + 1;
        REBVAL *value = CTX_VAR(error, root_len) + 1;

    #ifdef NDEBUG
        const RELVAL *temp = VAL_ARRAY_HEAD(message);
    #else
        // Will get here even for a parameterless string due to throwing in
        // the extra "arguments" of the __FILE__ and __LINE__
        //
        const RELVAL *temp =
            IS_STRING(message)
                ? cast(const RELVAL*, END) // needed by gcc/g++ 2.95 (bug)
                : VAL_ARRAY_HEAD(message);
    #endif

        for (; NOT_END(temp); ++temp) {
            if (IS_GET_WORD(temp)) {
                const REBVAL *arg = va_arg(*vaptr, const REBVAL*);

                // NULL is 0 in C, and so passing NULL to a va_arg list and
                // reading it as a pointer is not legal (because it will just
                // be an integer).  One would have to use `(REBVAL*)NULL`, so
                // END is used instead (consistent w/variadic Do_XXX)
                //
                assert(arg != NULL);

                if (IS_END(arg)) {
                    // Terminating with an end marker is optional but can help
                    // catch errors here of too few args passed when the
                    // template expected more substitutions.

                #ifdef NDEBUG
                    // If the C code passed too few args in a debug build,
                    // prevent a crash in the release build by filling it.
                    // No perfect answer if you're going to keep running...
                    // something like ISSUE! #404 could be an homage:
                    //
                    //     http://www.room404.com/page.php?pg=homepage
                    //
                    // But we'll just use NONE.  Debug build asserts here.

                    arg = BLANK_VALUE;
                #else
                    printf(
                        "too few args passed for error code %d\n",
                        cast(int, code)
                    );
                    assert(FALSE);

                    // !!! Note that we have no way of checking for too *many*
                    // args with C's va_list machinery
                #endif
                }

            #if !defined(NDEBUG)
                if (IS_RELATIVE(cast(const RELVAL*, arg))) {
                    //
                    // Make_Error doesn't have any way to pass in a specifier,
                    // so only specific values should be used.
                    //
                    printf("Relative value passed to Make_Error()\n");
                    panic (arg);
                }
            #endif

                ASSERT_VALUE_MANAGED(arg);

                Init_Typeset(key, ALL_64, VAL_WORD_SPELLING(temp));
                Move_Value(value, arg);

                key++;
                value++;
            }
        }

        assert(IS_END(key)); // set above by TERM_ARRAY_LEN
        assert(IS_END(value)); // ...same
    }

    // C struct mirroring fixed portion of error fields
    //
    ERROR_VARS *vars = ERR_VARS(error);

    if (code == RE_USER)
        assert(IS_BLANK(&vars->code)); // no error number
    else
        Init_Integer(&vars->code, code);

    Move_Value(&vars->message, message);
    Move_Value(&vars->id, id);
    Move_Value(&vars->type, type);

    // There might be no Rebol code running when the error is created (e.g.
    // the static creation of the stack overflow error before any code runs)
    //
    if (FS_TOP != NULL)
        Set_Location_Of_Error(error, FS_TOP);

    // !!! We create errors and then fail() on them without ever putting them
    // into a REBVAL.  This means that if left unmanaged, they would count as
    // manual memory that the fail() needed to clean up...but the fail()
    // plans on reporting this error (!).  In these cases the GC doesn't run
    // but the cleanup does, so for now manage the error in the hopes it
    // will be used up quickly.
    //
    MANAGE_ARRAY(CTX_VARLIST(error));
    return error;
}


//
//  Error: C
//
// This variadic function takes a number of REBVAL* arguments appropriate for
// the error number passed.  It is commonly used with fail():
//
//     fail (Error(RE_SOMETHING, arg1, arg2, ...));
//
// Note that in C, variadic functions don't know how many arguments they were
// passed.  Make_Error_Managed_Core() knows how many arguments are in an
// error's template in %errors.r for a given error id, so that is the number
// of arguments it will *attempt* to use--reading invalid memory if wrong.
//
// (All C variadics have this problem, e.g. `printf("%d %d", 12);`)
//
// But the risk of mistakes is reduced by creating wrapper functions, with a
// fixed number of arguments specific to each error...and the wrappers can
// also do additional argument processing:
//
//     fail (Error_Something(arg1, thing_processed_to_make_arg2));
//
// But to make variadic calls *slightly* safer, a caller can pass END
// after the last argument for a double-check that won't try reading invalid
// memory if too few arguments are given:
//
//     fail (Error(RE_SOMETHING, arg1, arg2, END));
//
REBCTX *Error(REBCNT num, ... /* REBVAL *arg1, REBVAL *arg2, ... */)
{
    va_list va;
    REBCTX *error;

    va_start(va, num);
    error = Make_Error_Managed_Core(num, &va);
    va_end(va);

    return error;
}


//
//  Error_User: C
//
// Simple error constructor from a string (historically this was called a
// "user error" since MAKE ERROR! of a STRING! would produce them in usermode
// without any error template in %errors.r)
//
REBCTX *Error_User(const char *str) {
    DECLARE_LOCAL (message);
    Init_String(message, Make_UTF8_May_Fail(cb_cast(str)));
    return Error(RE_USER, message, END);
}


//
//  Error_Lookback_Quote_Too_Late: C
//
REBCTX *Error_Lookback_Quote_Too_Late(const RELVAL *word, REBSPC *specifier) {
    assert(IS_WORD(word));

    DECLARE_LOCAL (specific);
    Derelativize(specific, word, specifier);

    return Error_Enfix_Quote_Late_Raw(specific);
}


//
//  Error_Non_Logic_Refinement: C
//
// Ren-C allows functions to be specialized, such that a function's frame can
// be filled (or partially filled) by an example frame.  The variables
// corresponding to refinements must be canonized to either TRUE or FALSE
// by these specializations, because that's what the called function expects.
//
REBCTX *Error_Non_Logic_Refinement(REBFRM *f) {
    DECLARE_LOCAL (word);
    Init_Word(word, VAL_PARAM_SPELLING(f->param));
    return Error_Non_Logic_Refine_Raw(word, Type_Of(f->arg));
}


//
//  Error_Bad_Func_Def: C
//
REBCTX *Error_Bad_Func_Def(const REBVAL *spec, const REBVAL *body)
{
    // !!! Improve this error; it's simply a direct emulation of arity-1
    // error that existed before refactoring code out of MAKE_Function().

    REBARR *array = Make_Array(2);
    Append_Value(array, spec);
    Append_Value(array, body);

    DECLARE_LOCAL (def);

    Init_Block(def, array);
    return Error_Bad_Func_Def_Raw(def);
}


//
//  Error_No_Arg: C
//
REBCTX *Error_No_Arg(REBFRM *f, const RELVAL *param)
{
    assert(IS_TYPESET(param));

    DECLARE_LOCAL (param_word);
    Init_Word(param_word, VAL_PARAM_SPELLING(param));

    DECLARE_LOCAL (label);
    Get_Frame_Label_Or_Blank(label, f);

    return Error_No_Arg_Raw(label, param_word);
}


//
//  Error_Invalid_Datatype: C
//
REBCTX *Error_Invalid_Datatype(REBCNT id)
{
    DECLARE_LOCAL (id_value);

    Init_Integer(id_value, id);
    return Error_Invalid_Datatype_Raw(id_value);
}


//
//  Error_No_Memory: C
//
REBCTX *Error_No_Memory(REBCNT bytes)
{
    DECLARE_LOCAL (bytes_value);

    Init_Integer(bytes_value, bytes);
    return Error_No_Memory_Raw(bytes_value);
}


//
//  Error_No_Relative: C
//
REBCTX *Error_No_Relative_Core(const RELVAL *any_word)
{
    DECLARE_LOCAL (unbound);
    Init_Any_Word(
        unbound,
        VAL_TYPE(any_word),
        VAL_WORD_SPELLING(any_word)
    );

    return Error_No_Relative_Raw(unbound);
}


//
//  Error_Not_Varargs: C
//
REBCTX *Error_Not_Varargs(
    REBFRM *f,
    const RELVAL *param,
    enum Reb_Kind kind
){
    assert(GET_VAL_FLAG(param, TYPESET_FLAG_VARIADIC));
    assert(kind != REB_VARARGS);

    // Since the "types accepted" are a lie (an [integer! <...>] takes
    // VARARGS! when fulfilled in a frame directly, not INTEGER!) then
    // an "honest" parameter has to be made to give the error.
    //
    DECLARE_LOCAL (honest_param);
    Init_Typeset(
        honest_param,
        FLAGIT_KIND(REB_VARARGS), // actually expected
        VAL_PARAM_SPELLING(param)
    );

    return Error_Arg_Type(f, honest_param, kind);
}


//
//  Error_Invalid_Arg_Core: C
//
// This error is pretty vague...it's just "invalid argument"
// and the value with no further commentary or context.  It
// becomes a catch all for "unexpected input" when a more
// specific error would be more useful.
//
// Note that just `fail (value)` on REBVAL* will generate this error, this
// variant is used on RELVAL*.
//
REBCTX *Error_Invalid_Arg_Core(const RELVAL *value, REBSPC *specifier)
{
    DECLARE_LOCAL (specific);
    Derelativize(specific, value, specifier);

    return Error_Invalid_Arg_Raw(specific);
}


//
//  Error_Bad_Func_Def_Core: C
//
REBCTX *Error_Bad_Func_Def_Core(const RELVAL *item, REBSPC *specifier)
{
    DECLARE_LOCAL (specific);
    Derelativize(specific, item, specifier);
    return Error_Bad_Func_Def_Raw(specific);
}


//
//  Error_Bad_Refine_Revoke: C
//
// We may have to search for the refinement, so we always do (speed of error
// creation not considered that relevant to the evaluator, being overshadowed
// by the error handling).  See the remarks about the state of f->refine in
// the Reb_Frame definition.
//
REBCTX *Error_Bad_Refine_Revoke(REBFRM *f)
{
    assert(IS_TYPESET(f->param));

    DECLARE_LOCAL (param_name);
    Init_Word(param_name, VAL_PARAM_SPELLING(f->param));

    while (VAL_PARAM_CLASS(f->param) != PARAM_CLASS_REFINEMENT)
        --f->param;

    DECLARE_LOCAL (refine_name);
    Init_Refinement(refine_name, VAL_PARAM_SPELLING(f->param));

    if (IS_VOID(f->arg)) // was void and shouldn't have been
        return Error_Bad_Refine_Revoke_Raw(refine_name, param_name);

    // wasn't void and should have been
    //
    return Error_Argument_Revoked_Raw(refine_name, param_name);
}


//
//  Error_No_Value_Core: C
//
REBCTX *Error_No_Value_Core(const RELVAL *target, REBSPC *specifier) {
    DECLARE_LOCAL (specified);
    Derelativize(specified, target, specifier);

    return Error_No_Value_Raw(specified);
}


//
//  Error_No_Value: C
//
REBCTX *Error_No_Value(const REBVAL *target) {
    return Error_No_Value_Core(target, SPECIFIED);
}


//
//  Error_No_Catch_For_Throw: C
//
REBCTX *Error_No_Catch_For_Throw(REBVAL *thrown)
{
    DECLARE_LOCAL (arg);

    assert(THROWN(thrown));
    CATCH_THROWN(arg, thrown); // clears bit

    if (IS_BLANK(thrown))
        return Error_No_Catch_Raw(arg);

    return Error_No_Catch_Named_Raw(arg, thrown);
}


//
//  Error_Invalid_Type: C
//
// <type> type is not allowed here.
//
REBCTX *Error_Invalid_Type(enum Reb_Kind kind)
{
    return Error_Invalid_Type_Raw(Get_Type(kind));
}


//
//  Error_Out_Of_Range: C
//
// value out of range: <value>
//
REBCTX *Error_Out_Of_Range(const REBVAL *arg)
{
    return Error_Out_Of_Range_Raw(arg);
}


//
//  Error_Protected_Key: C
//
REBCTX *Error_Protected_Key(REBVAL *key)
{
    assert(IS_TYPESET(key));

    DECLARE_LOCAL (key_name);
    Init_Word(key_name, VAL_KEY_SPELLING(key));

    return Error_Protected_Word_Raw(key_name);
}


//
//  Error_Illegal_Action: C
//
REBCTX *Error_Illegal_Action(enum Reb_Kind type, REBSYM action)
{
    DECLARE_LOCAL (action_word);
    Init_Word(action_word, Canon(action));

    return Error_Cannot_Use_Raw(action_word, Get_Type(type));
}


//
//  Error_Math_Args: C
//
REBCTX *Error_Math_Args(enum Reb_Kind type, REBSYM action)
{
    DECLARE_LOCAL (action_word);
    Init_Word(action_word, Canon(action));

    return Error_Not_Related_Raw(action_word, Get_Type(type));
}


//
//  Error_Unexpected_Type: C
//
REBCTX *Error_Unexpected_Type(enum Reb_Kind expected, enum Reb_Kind actual)
{
    assert(expected < REB_MAX);
    assert(actual < REB_MAX);

    return Error_Expect_Val_Raw(
        Get_Type(expected),
        Get_Type(actual)
    );
}


//
//  Error_Arg_Type: C
//
// Function in frame of `call` expected parameter `param` to be
// a type different than the arg given (which had `arg_type`)
//
REBCTX *Error_Arg_Type(
    REBFRM *f,
    const RELVAL *param,
    enum Reb_Kind kind
) {
    assert(IS_TYPESET(param));

    DECLARE_LOCAL (param_word);
    Init_Word(param_word, VAL_PARAM_SPELLING(param));

    DECLARE_LOCAL (label);
    Get_Frame_Label_Or_Blank(label, f);

    if (kind != REB_MAX_VOID) {
        assert(kind != REB_0);
        REBVAL *datatype = Get_Type(kind);
        assert(IS_DATATYPE(datatype));

        return Error_Expect_Arg_Raw(
            label,
            datatype,
            param_word
        );
    }

    // Although REB_MAX_VOID is not a type, the typeset bits are used
    // to check it.  Since Get_Type() will fail, use another error.
    //
    return Error_Arg_Required_Raw(
        label,
        param_word
    );
}


//
//  Error_Bad_Return_Type: C
//
REBCTX *Error_Bad_Return_Type(REBFRM *f, enum Reb_Kind kind) {
    DECLARE_LOCAL (label);
    Get_Frame_Label_Or_Blank(label, f);

    if (kind == REB_MAX_VOID)
        return Error_Needs_Return_Value_Raw(label);

    REBVAL *datatype = Get_Type(kind);
    assert(IS_DATATYPE(datatype));
    return Error_Bad_Return_Type_Raw(label, datatype);
}


//
//  Error_Bad_Make: C
//
REBCTX *Error_Bad_Make(enum Reb_Kind type, const REBVAL *spec)
{
    return Error_Bad_Make_Arg_Raw(Get_Type(type), spec);
}


//
//  Error_Cannot_Reflect: C
//
REBCTX *Error_Cannot_Reflect(enum Reb_Kind type, const REBVAL *arg)
{
    return Error_Cannot_Use_Raw(arg, Get_Type(type));
}


//
//  Error_On_Port: C
//
REBCTX *Error_On_Port(REBCNT errnum, REBCTX *port, REBINT err_code)
{
    FAIL_IF_BAD_PORT(port);

    REBVAL *spec = CTX_VAR(port, STD_PORT_SPEC);

    REBVAL *val = VAL_CONTEXT_VAR(spec, STD_PORT_SPEC_HEAD_REF); // informative
    if (IS_BLANK(val))
        val = VAL_CONTEXT_VAR(spec, STD_PORT_SPEC_HEAD_TITLE); // less info

    DECLARE_LOCAL (err_code_value);
    Init_Integer(err_code_value, err_code);

    return Error(errnum, val, err_code_value, END);
}


//
//  Exit_Status_From_Value: C
//
// This routine's job is to turn an arbitrary value into an
// operating system exit status:
//
//     https://en.wikipedia.org/wiki/Exit_status
//
int Exit_Status_From_Value(REBVAL *value)
{
    assert(!THROWN(value));

    if (IS_INTEGER(value)) {
        //
        // Fairly obviously, an integer should return an integer result.
        // But Rebol integers are 64 bit and signed, while exit statuses don't
        // go that large.
        //
        return VAL_INT32(value);
    }
    else if (IS_VOID(value) || IS_BLANK(value)) {
        //
        // An void would happen with just QUIT or EXIT and no /WITH, so
        // treating that as a 0 for success makes sense.  A blank could also
        // make sense as success.
        //
        //     exit/with any [bad-code-a | bad-code-b]
        //
        return 0;
    }
    else if (IS_ERROR(value)) {
        //
        // Rebol errors do have an error number in them, and if your
        // program tries to return a Rebol error it seems it wouldn't
        // hurt to try using that.  They may be out of range for
        // platforms using byte-sized error codes, however...
        //
        return VAL_ERR_NUM(value);
    }

    // Just 1 otherwise.
    //
    return 1;
}


//
//  Startup_Errors: C
//
// Create error objects and error type objects
//
REBCTX *Startup_Errors(REBARR *boot_errors)
{
    REBCTX *catalog = Construct_Context(
        REB_OBJECT,
        ARR_HEAD(boot_errors),
        SPECIFIED, // we're confident source array isn't in a function body
        NULL
    );

    // Create objects for all error types (CAT_ERRORS is "selfish", currently
    // so self is in slot 1 and the actual errors start at context slot 2)
    //
    REBVAL *val;
    for (val = CTX_VAR(catalog, SELFISH(1)); NOT_END(val); val++) {
        REBCTX *error = Construct_Context(
            REB_OBJECT,
            VAL_ARRAY_HEAD(val),
            SPECIFIED, // source array not in a function body
            NULL
        );
        Init_Object(val, error);
    }

    return catalog;
}


//
//  Security_Policy: C
//
// Given a security symbol (like FILE) and a value (like the file
// path) returns the security policy (RWX) allowed for it.
//
// Args:
//
//     sym:  word that represents the type ['file 'net]
//     name: file or path value
//
// Returns BTYE array of flags for the policy class:
//
//     flags: [rrrr wwww xxxx ----]
//
//     Where each byte is:
//         0: SEC_ALLOW
//         1: SEC_ASK
//         2: SEC_THROW
//         3: SEC_QUIT
//
// The secuity is defined by the system/state/policies object, that
// is of the form:
//
//     [
//         file:  [%file1 tuple-flags %file2 ... default tuple-flags]
//         net:   [...]
//         call:  tuple-flags
//         stack: tuple-flags
//         eval:  integer (limit)
//     ]
//
const REBYTE *Security_Policy(REBSTR *spelling, const REBVAL *name)
{
    const REBVAL *policy = Get_System(SYS_STATE, STATE_POLICIES);
    const REBYTE *flags;
    REBCNT len;
    REBCNT errcode = RE_SECURITY_ERROR;

    if (!IS_OBJECT(policy)) goto error;

    // Find the security class in the block: (file net call...)
    policy = Select_Canon_In_Context(VAL_CONTEXT(policy), STR_CANON(spelling));
    if (!policy) goto error;

    // Obtain the policies for it:
    // Check for a master tuple: [file rrrr.wwww.xxxx]
    if (IS_TUPLE(policy)) return VAL_TUPLE(policy); // non-aligned
    // removed A90: if (IS_INTEGER(policy)) return (REBYTE*)VAL_INT64(policy); // probably not used

    // Only other form is detailed block:
    if (!IS_BLOCK(policy)) goto error;

    // Scan block of policies for the class: [file [allow read quit write]]
    len = 0;    // file or url length
    flags = 0;  // policy flags

    policy = KNOWN(VAL_ARRAY_HEAD(policy)); // no relatives in STATE_POLICIES

    for (; NOT_END(policy); policy += 2) {

        // Must be a policy tuple:
        if (!IS_TUPLE(policy+1)) goto error;

        // Is it a policy word:
        if (IS_WORD(policy)) { // any word works here
            // If no strings found, use the default:
            if (len == 0) flags = VAL_TUPLE(policy+1); // non-aligned
        }

        // Is it a string (file or URL):
        else if (ANY_BINSTR(policy) && name) {
            if (Match_Sub_Path(VAL_SERIES(policy), VAL_SERIES(name))) {
                // Is the match adequate?
                if (VAL_LEN_HEAD(name) >= len) {
                    len = VAL_LEN_HEAD(name);
                    flags = VAL_TUPLE(policy+1); // non-aligned
                }
            }
        }
        else goto error;
    }

    if (!flags) {
        errcode = RE_SECURITY;
        policy = name ? name : 0;

    error:
        ; // need statement
        DECLARE_LOCAL (temp);
        if (!policy) {
            Init_Word(temp, spelling);
            policy = temp;
        }
        fail (Error(errcode, policy));
    }

    return flags;
}


//
//  Trap_Security: C
//
// Take action on the policy flags provided. The sym and value
// are provided for error message purposes only.
//
void Trap_Security(REBCNT flag, REBSTR *sym, const REBVAL *value)
{
    if (flag == SEC_THROW) {
        if (!value) {
            Init_Word(DS_TOP, sym);
            value = DS_TOP;
        }
        fail (Error_Security_Raw(value));
    }
    else if (flag == SEC_QUIT)
        exit(101);
}


//
//  Check_Security: C
//
// A helper function that fetches the security flags for
// a given symbol (FILE) and value (path), and then tests
// that they are allowed.
//
void Check_Security(REBSTR *sym, REBCNT policy, REBVAL *value)
{
    const REBYTE *flags = Security_Policy(sym, value);
    Trap_Security(flags[policy], sym, value);
}


//
//  Find_Next_Error_Base_Code: C
//
// Find in system/catalog/errors the next error base (used by extensions)
//
REBINT Find_Next_Error_Base_Code(void)
{
    REBCTX * categories = VAL_CONTEXT(Get_System(SYS_CATALOG, CAT_ERRORS));
    if (CTX_LEN(categories) > RE_USER / RE_CATEGORY_SIZE)
        fail (Error_Out_Of_Error_Numbers_Raw());
    return (CTX_LEN(categories) - 1) * RE_CATEGORY_SIZE;
}


// Limited molder (used, e.g., for errors)
//
static void Mold_Value_Limit(REB_MOLD *mo, RELVAL *v, REBCNT len)
{
    REBCNT start = SER_LEN(mo->series);
    Mold_Value(mo, v);

    if (SER_LEN(mo->series) - start > len) {
        SET_SERIES_LEN(mo->series, start + len);
        Append_Unencoded(mo->series, "...");
    }
}


//
//  MF_Error: C
//
void MF_Error(REB_MOLD *mo, const RELVAL *v, REBOOL form)
{
    // Protect against recursion. !!!!
    //
    if (NOT(form)) {
        MF_Context(mo, v, FALSE);
        return;
    }

    REBCTX *error = VAL_CONTEXT(v);
    ERROR_VARS *vars = ERR_VARS(error);

    // Form: ** <type> Error:
    if (IS_BLANK(&vars->type))
        Emit(mo, "** S", RM_ERROR_LABEL);
    else {
        assert(IS_WORD(&vars->type));
        Emit(mo, "** W S", &vars->type, RM_ERROR_LABEL);
    }

    // Append: error message ARG1, ARG2, etc.
    if (IS_BLOCK(&vars->message))
        Form_Array_At(mo, VAL_ARRAY(&vars->message), 0, error);
    else if (IS_STRING(&vars->message))
        Form_Value(mo, &vars->message);
    else
        Append_Unencoded(mo->series, RM_BAD_ERROR_FORMAT);

    // Form: ** Where: function
    REBVAL *where = KNOWN(&vars->where);
    if (NOT(IS_BLANK(where))) {
        Append_Utf8_Codepoint(mo->series, '\n');
        Append_Unencoded(mo->series, RM_ERROR_WHERE);
        Form_Value(mo, where);
    }

    // Form: ** Near: location
    REBVAL *nearest = KNOWN(&vars->nearest);
    if (NOT(IS_BLANK(nearest))) {
        Append_Utf8_Codepoint(mo->series, '\n');
        Append_Unencoded(mo->series, RM_ERROR_NEAR);

        if (IS_STRING(nearest)) {
            //
            // !!! The scanner puts strings into the near information in order
            // to say where the file and line of the scan problem was.  This
            // seems better expressed as an explicit argument to the scanner
            // error, because otherwise it obscures the LOAD call where the
            // scanner was invoked.  Review.
            //
            Append_String(
                mo->series, VAL_SERIES(nearest), 0, VAL_LEN_HEAD(nearest)
            );
        }
        else if (ANY_ARRAY(nearest))
            Mold_Value_Limit(mo, nearest, 60);
        else
            Append_Unencoded(mo->series, RM_BAD_ERROR_FORMAT);
    }

    // Form: ** File: filename
    //
    // !!! In order to conserve space in the system, filenames are interned.
    // Although interned strings are GC'd when no longer referenced, they can
    // only be used in ANY-WORD! values at the moment, so the filename is
    // not a FILE!.
    //
    REBVAL *file = KNOWN(&vars->file);
    if (NOT(IS_BLANK(file))) {
        Append_Utf8_Codepoint(mo->series, '\n');
        Append_Unencoded(mo->series, RM_ERROR_FILE);
        if (IS_WORD(file))
            Form_Value(mo, file);
        else
            Append_Unencoded(mo->series, RM_BAD_ERROR_FORMAT);
    }

    // Form: ** Line: line-number
    REBVAL *line = KNOWN(&vars->line);
    if (NOT(IS_BLANK(line))) {
        Append_Utf8_Codepoint(mo->series, '\n');
        Append_Unencoded(mo->series, RM_ERROR_LINE);
        if (IS_INTEGER(line))
            Form_Value(mo, line);
        else
            Append_Unencoded(mo->series, RM_BAD_ERROR_FORMAT);
    }
}
