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
// Copyright 2012-2017 Ren-C Open Source Contributors
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


#include "sys-core.h"


//
//  Snap_State_Core: C
//
// Used by SNAP_STATE and PUSH_TRAP.
//
// **Note:** Modifying this routine likely means a necessary modification to
// both `Assert_State_Balanced_Debug()` and `Trapped_Helper_Halted()`.
//
void Snap_State_Core(struct Reb_State *s)
{
    s->dsp = DSP;

    // There should not be a Collect_Keys in progress.  (We use a non-zero
    // length of the collect buffer to tell if a later fail() happens in
    // the middle of a Collect_Keys.)
    //
    assert(ARR_LEN(BUF_COLLECT) == 0);

    s->guarded_len = SER_USED(GC_Guarded);
    s->frame = FS_TOP;

    s->manuals_len = SER_USED(GC_Manuals);
    s->mold_buf_len = STR_LEN(STR(MOLD_BUF));
    s->mold_buf_size = STR_SIZE(STR(MOLD_BUF));
    s->mold_loop_tail = ARR_LEN(TG_Mold_Stack);

    s->saved_sigmask = Eval_Sigmask;

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
            "DS_PUSH()x%d without DS_DROP()\n",
            cast(int, DSP - s->dsp)
        );
        panic_at (nullptr, file, line);
    }

    assert(s->frame == FS_TOP);

    assert(ARR_LEN(BUF_COLLECT) == 0);

    if (s->guarded_len != SER_USED(GC_Guarded)) {
        printf(
            "PUSH_GC_GUARD()x%d without DROP_GC_GUARD()\n",
            cast(int, SER_USED(GC_Guarded) - s->guarded_len)
        );
        REBNOD *guarded = *SER_AT(
            REBNOD*,
            GC_Guarded,
            SER_USED(GC_Guarded) - 1
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
    if (s->manuals_len > SER_USED(GC_Manuals)) {
        //
        // Note: Should this ever actually happen, panic() on the series won't
        // do any real good in helping debug it.  You'll probably need
        // additional checks in Manage_Series() and Free_Unmanaged_Series()
        // that check against the caller's manuals_len.
        //
        panic_at ("manual series freed outside checkpoint", file, line);
    }
    else if (s->manuals_len < SER_USED(GC_Manuals)) {
        printf(
            "Make_Series()x%d w/o Free_Unmanaged_Series or Manage_Series\n",
            cast(int, SER_USED(GC_Manuals) - s->manuals_len)
        );
        REBSER *manual = *(SER_AT(
            REBSER*,
            GC_Manuals,
            SER_USED(GC_Manuals) - 1
        ));
        panic_at (manual, file, line);
    }

    assert(s->mold_buf_len == STR_LEN(STR(MOLD_BUF)));
    assert(s->mold_buf_size == STR_SIZE(STR(MOLD_BUF)));
    assert(s->mold_loop_tail == ARR_LEN(TG_Mold_Stack));

    assert(s->saved_sigmask == Eval_Sigmask);  // !!! is this always true?

    assert(s->error == NULL); // !!! necessary?
}

#endif


//
//  Trapped_Helper: C
//
// This do the work of responding to a longjmp.  (Hence it is run when setjmp
// returns true.)  Its job is to safely recover from a sudden interruption,
// though the list of things which can be safely recovered from is finite.
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
void Trapped_Helper(struct Reb_State *s)
{
    ASSERT_CONTEXT(s->error);
    assert(CTX_TYPE(s->error) == REB_ERROR);

    // Restore Rebol data stack pointer at time of Push_Trap
    //
    DS_DROP_TO(s->dsp);

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
    assert(SER_USED(GC_Manuals) >= s->manuals_len);
    while (SER_USED(GC_Manuals) != s->manuals_len) {
        // Freeing the series will update the tail...
        Free_Unmanaged_Series(
            *SER_AT(REBSER*, GC_Manuals, SER_USED(GC_Manuals) - 1)
        );
    }

    SET_SERIES_LEN(GC_Guarded, s->guarded_len);
    TG_Top_Frame = s->frame;
    TERM_STR_LEN_SIZE(STR(MOLD_BUF), s->mold_buf_len, s->mold_buf_size);

  #if !defined(NDEBUG)
    //
    // Because reporting errors in the actual Push_Mold process leads to
    // recursion, this debug flag helps make it clearer what happens if
    // that does happen... and can land on the right comment.  But if there's
    // a fail of some kind, the flag for the warning needs to be cleared.
    //
    TG_Pushing_Mold = false;
  #endif

    SET_SERIES_LEN(TG_Mold_Stack, s->mold_loop_tail);

    Eval_Sigmask = s->saved_sigmask;

    Saved_State = s->last_state;
}


//
//  Fail_Core: C
//
// Cause a "trap" of an error by longjmp'ing to the enclosing PUSH_TRAP.  Note
// that these failures interrupt code mid-stream, so if a Rebol function is
// running it will not make it to the point of returning the result value.
// This distinguishes the "fail" mechanic from the "throw" mechanic, which has
// to bubble up a thrown value through D_OUT (used to implement BREAK,
// CONTINUE, RETURN, LEAVE, HALT...)
//
// The function will auto-detect if the pointer it is given is an ERROR!'s
// REBCTX* or a UTF-8 char *.  If it's UTF-8, an error will be created from
// it automatically (but with no ID...the string becomes the "ID")
//
// If the pointer is to a function parameter (e.g. what you get for PAR(name)
// inside a native), then it will figure out what parameter that function is
// for, find the most recent call on the stack, and report both the parameter
// name and value as being implicated as a problem).
//
// Passing an arbitrary REBVAL* will give a generic "Invalid Arg" error.
//
// Note: Over the long term, one does not want to hard-code error strings in
// the executable.  That makes them more difficult to hook with translations,
// or to identify systemically with some kind of "error code".  However,
// it's a realistic quick-and-dirty way of delivering a more meaningful
// error than just using a RE_MISC error code, and can be found just as easily
// to clean up later with a textual search for `fail ("`
//
ATTRIBUTE_NO_RETURN void Fail_Core(const void *p)
{
  #if defined(DEBUG_PRINTF_FAIL_LOCATIONS) && defined(DEBUG_COUNT_TICKS)
    //
    // File and line are printed by the calling macro to capture __FILE__ and
    // __LINE__ without adding parameter overhead to this function for non
    // debug builds.
    //
    printf("%ld\n", cast(long, TG_Tick));  /* tick count prefix */
  #endif

  #ifdef DEBUG_HAS_PROBE
    if (PG_Probe_Failures) {  // see R3_PROBE_FAILURES environment variable
        static bool probing = false;

        if (p == cast(void*, VAL_CONTEXT(Root_Stackoverflow_Error))) {
            printf("PROBE(Stack Overflow): mold in PROBE would recurse\n");
            fflush(stdout);
        }
        else if (probing) {
            printf("PROBE(Recursing): recursing for unknown reason\n");
            panic (p);
        }
        else {
            probing = true;
            PROBE(p);
            probing = false;
        }
    }
  #endif

    REBCTX *error;
    if (p == nullptr) {
        error = Error_Unknown_Error_Raw();
    }
    else switch (Detect_Rebol_Pointer(p)) {
      case DETECTED_AS_UTF8:
        error = Error_User(cast(const char*, p));
        break;

      case DETECTED_AS_SERIES: {
        REBSER *s = m_cast(REBSER*, cast(const REBSER*, p));  // don't mutate
        if (not IS_SER_ARRAY(s) or NOT_ARRAY_FLAG(s, IS_VARLIST))
            panic (s);
        error = CTX(s);
        break; }

      case DETECTED_AS_CELL: {
        const REBVAL *v = cast(const REBVAL*, p);
        if (IS_PARAM(v)) {
            const REBVAL *v_seek = v;
            while (not IS_ACTION(v_seek))
                --v_seek;
            REBFRM *f_seek = FS_TOP;
            REBACT *act = VAL_ACTION(v_seek);
            while (f_seek->original != act) {
                --f_seek;
                if (f_seek == FS_BOTTOM)
                    panic ("fail (PAR(name)); issued for param not on stack");
            }
            error = Error_Invalid_Arg(f_seek, v);
        }
        else
            error = Error_Bad_Value(v);
        break; }

      default:
        panic (p);  // suppress compiler error from non-smart compilers
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

    // If the error doesn't have a where/near set, set it from stack
    //
    ERROR_VARS *vars = ERR_VARS(error);
    if (IS_NULLED_OR_BLANK(&vars->where))
        Set_Location_Of_Error(error, FS_TOP);

    // The information for the Rebol call frames generally is held in stack
    // variables, so the data will go bad in the longjmp.  We have to free
    // the data *before* the jump.  Be careful not to let this code get too
    // recursive or do other things that would be bad news if we're responding
    // to C_STACK_OVERFLOWING.  (See notes on the sketchiness in general of
    // the way R3-Alpha handles stack overflows, and alternative plans.)
    //
    REBFRM *f = FS_TOP;
    while (f != Saved_State->frame) {
        if (Is_Action_Frame(f)) {
            assert(f->varlist); // action must be running
            REBARR *stub = f->varlist; // will be stubbed, info bits reset
            Drop_Action(f);
            SET_SERIES_FLAG(stub, VARLIST_FRAME_FAILED); // API leaks o.k.
        }

        REBFRM *prior = f->prior;
        Abort_Frame(f); // will call va_end() if variadic frame
        f = prior;
    }

    TG_Top_Frame = f; // TG_Top_Frame is writable FS_TOP

    Saved_State->error = error;

    // If a throw was being processed up the stack when the error was raised,
    // then it had the thrown argument set.  Trash it in debug builds.  (The
    // value will not be kept alive, it is not seen by GC)
    //
  #if !defined(NDEBUG)
    SET_END(&TG_Thrown_Arg);
  #endif

    LONG_JUMP(Saved_State->cpu_state, 1);
}


//
//  Stack_Depth: C
//
REBLEN Stack_Depth(void)
{
    REBLEN depth = 0;

    REBFRM *f = FS_TOP;
    while (f) {
        if (Is_Action_Frame(f))
            if (not Is_Action_Frame_Fulfilling(f)) {
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
//  Find_Error_For_Sym: C
//
// This scans the data which is loaded into the boot file from %errors.r.
// It finds the error type (category) word, and the error message template
// block-or-string for a given error ID.
//
// This once used numeric error IDs.  Now that the IDs are symbol-based, a
// linear search has to be used...though a MAP! could/should be used.
//
// If the message is not found, return nullptr.
//
const REBVAL *Find_Error_For_Sym(enum Reb_Symbol id_sym)
{
    REBSTR *id_canon = Canon(id_sym);

    REBCTX *categories = VAL_CONTEXT(Get_System(SYS_CATALOG, CAT_ERRORS));
    assert(CTX_KEY_SYM(categories, 1) == SYM_SELF);

    REBLEN ncat = SELFISH(1);
    for (; ncat <= CTX_LEN(categories); ++ncat) {
        REBCTX *category = VAL_CONTEXT(CTX_VAR(categories, ncat));

        REBLEN n = SELFISH(1);
        for (; n != CTX_LEN(category) + 1; ++n) {
            if (SAME_STR(CTX_KEY_SPELLING(category, n), id_canon)) {
                REBVAL *message = CTX_VAR(category, n);
                assert(IS_BLOCK(message) or IS_TEXT(message));
                return message;
            }
        }
    }

    return nullptr;
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
    REBFRM *where  // must be valid and executing on the stack
) {
    while (GET_EVAL_FLAG(where, BLAME_PARENT))  // e.g. Apply_Only_Throws()
        where = where->prior;

    REBDSP dsp_orig = DSP;

    ERROR_VARS *vars = ERR_VARS(error);

    // WHERE is a backtrace in the form of a block of label words, that start
    // from the top of stack and go downward.
    //
    REBFRM *f = where;
    for (; f != FS_BOTTOM; f = f->prior) {
        //
        // Only invoked functions (not pending functions, groups, etc.)
        //
        if (not Is_Action_Frame(f))
            continue;
        if (Is_Action_Frame_Fulfilling(f))
            continue;
        if (f->original == PG_Dummy_Action)
            continue;

        Get_Frame_Label_Or_Blank(DS_PUSH(), f);
    }
    Init_Block(&vars->where, Pop_Stack_Values(dsp_orig));

    // Nearby location of the error.  Reify any valist that is running,
    // so that the error has an array to present.
    //
    // !!! Review: The "near" information is used in things like the scanner
    // missing a closing quote mark, and pointing to the source code (not
    // the implementation of LOAD).  We don't want to override that or we
    // would lose the message.  But we still want the stack of where the
    // LOAD was being called in the "where".  For the moment don't overwrite
    // any existing near, but a less-random design is needed here.
    //
    if (IS_NULLED_OR_BLANK(&vars->nearest))
        Init_Near_For_Frame(&vars->nearest, where);

    // Try to fill in the file and line information of the error from the
    // stack, looking for arrays with ARRAY_HAS_FILE_LINE.
    //
    f = where;
    for (; f != FS_BOTTOM; f = f->prior) {
        if (not f->feed->array) {
            //
            // !!! We currently skip any calls from C (e.g. rebValue()) and look
            // for calls from Rebol files for the file and line.  However,
            // rebValue() might someday supply its C code __FILE__ and __LINE__,
            // which might be interesting to put in the error instead.
            //
            continue;
        }
        if (NOT_ARRAY_FLAG(f->feed->array, HAS_FILE_LINE_UNMASKED))
            continue;
        break;
    }
    if (f != FS_BOTTOM) {
        REBSTR *file = LINK_FILE(f->feed->array);
        REBLIN line = MISC(f->feed->array).line;

        REBSYM file_sym = STR_SYMBOL(file);
        if (IS_NULLED_OR_BLANK(&vars->file)) {
            if (file_sym != SYM___ANONYMOUS__)
                Init_Word(&vars->file, file);
            if (line != 0)
                Init_Integer(&vars->line, line);
        }
    }
}


//
// MAKE_Error: C
//
// Hook for MAKE ERROR! (distinct from MAKE for ANY-CONTEXT!, due to %types.r)
//
// Note: Most often system errors from %errors.r are thrown by C code using
// Make_Error(), but this routine accommodates verification of errors created
// through user code...which may be mezzanine Rebol itself.  A goal is to not
// allow any such errors to be formed differently than the C code would have
// made them, and to cross through the point of R3-Alpha error compatibility,
// which makes this a rather tortured routine.  However, it maps out the
// existing landscape so that if it is to be changed then it can be seen
// exactly what is changing.
//
REB_R MAKE_Error(
    REBVAL *out,  // output location **MUST BE GC SAFE**!
    enum Reb_Kind kind,
    const REBVAL *opt_parent,
    const REBVAL *arg
){
    assert(kind == REB_ERROR);
    UNUSED(kind);

    if (opt_parent)  // !!! Should probably be able to work!
        fail (Error_Bad_Make_Parent(kind, opt_parent));

    // Frame from the error object template defined in %sysobj.r
    //
    REBCTX *root_error = VAL_CONTEXT(Get_System(SYS_STANDARD, STD_ERROR));

    REBCTX *error;
    ERROR_VARS *vars; // C struct mirroring fixed portion of error fields

    if (IS_ERROR(arg) or IS_OBJECT(arg)) {
        // Create a new error object from another object, including any
        // non-standard fields.  WHERE: and NEAR: will be overridden if
        // used.  If ID:, TYPE:, or CODE: were used in a way that would
        // be inconsistent with a Rebol system error, an error will be
        // raised later in the routine.

        error = Merge_Contexts_Selfish_Managed(root_error, VAL_CONTEXT(arg));
        vars = ERR_VARS(error);
    }
    else if (IS_BLOCK(arg)) {
        // If a block, then effectively MAKE OBJECT! on it.  Afterward,
        // apply the same logic as if an OBJECT! had been passed in above.

        // Bind and do an evaluation step (as with MAKE OBJECT! with A_MAKE
        // code in REBTYPE(Context) and code in REBNATIVE(construct))

        error = Make_Selfish_Context_Detect_Managed(
            REB_ERROR, // type
            VAL_ARRAY_AT(arg), // values to scan for toplevel set-words
            root_error // parent
        );

        // Protect the error from GC by putting into out, which must be
        // passed in as a GC-protecting value slot.
        //
        Init_Error(out, error);

        Rebind_Context_Deep(root_error, error, NULL); // NULL=>no more binds
        Bind_Values_Deep(VAL_ARRAY_AT_MUTABLE_HACK(arg), error);

        DECLARE_LOCAL (evaluated);
        if (Do_Any_Array_At_Throws(evaluated, arg, SPECIFIED)) {
            Move_Value(out, evaluated);
            return R_THROWN;
        }

        vars = ERR_VARS(error);
    }
    else if (IS_TEXT(arg)) {
        //
        // String argument to MAKE ERROR! makes a custom error from user:
        //
        //     code: _  ; default is blank
        //     type: _
        //     id: _
        //     message: "whatever the string was"
        //
        // Minus the message, this is the default state of root_error.

        error = Copy_Context_Shallow_Managed(root_error);

        vars = ERR_VARS(error);
        assert(IS_BLANK(&vars->type));
        assert(IS_BLANK(&vars->id));

        Init_Text(&vars->message, Copy_String_At(arg));
    }
    else
        fail (arg);

    // Validate the error contents, and reconcile message template and ID
    // information with any data in the object.  Do this for the IS_STRING
    // creation case just to make sure the rules are followed there too.

    // !!! Note that this code is very cautious because the goal isn't to do
    // this as efficiently as possible, rather to put up lots of alarms and
    // traffic cones to make it easy to pick and choose what parts to excise
    // or tighten in an error enhancement upgrade.

    if (IS_WORD(&vars->type) and IS_WORD(&vars->id)) {
        // If there was no CODE: supplied but there was a TYPE: and ID: then
        // this may overlap a combination used by Rebol where we wish to
        // fill in the code.  (No fast lookup for this, must search.)

        REBCTX *categories = VAL_CONTEXT(Get_System(SYS_CATALOG, CAT_ERRORS));

        // Find correct category for TYPE: (if any)
        REBVAL *category
            = Select_Canon_In_Context(categories, VAL_WORD_CANON(&vars->type));

        if (category) {
            assert(IS_OBJECT(category));
            assert(CTX_KEY_SYM(VAL_CONTEXT(category), 1) == SYM_SELF);

            // Find correct message for ID: (if any)

            REBVAL *message = Select_Canon_In_Context(
                VAL_CONTEXT(category), VAL_WORD_CANON(&vars->id)
            );

            if (message) {
                assert(IS_TEXT(message) or IS_BLOCK(message));

                if (not IS_BLANK(&vars->message))
                    fail (Error_Invalid_Error_Raw(arg));

                Move_Value(&vars->message, message);
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

                fail (Error_Invalid_Error_Raw(CTX_ARCHETYPE(error)));
            }
        }
        else {
            // The type and category picked did not overlap any existing one
            // so let it be a user error (?)
        }
    }
    else {
        // It's either a user-created error or otherwise.  It may have bad ID,
        // TYPE, or message fields.  The question of how non-standard to
        // tolerate is an open one.

        // !!! Because we will experience crashes in the molding logic, we put
        // some level of requirements.  This is conservative logic and not
        // good for general purposes.

        if (not (
            (IS_WORD(&vars->id) or IS_BLANK(&vars->id))
            and (IS_WORD(&vars->type) or IS_BLANK(&vars->type))
            and (
                IS_BLOCK(&vars->message)
                or IS_TEXT(&vars->message)
                or IS_BLANK(&vars->message)
            )
        )){
            fail (Error_Invalid_Error_Raw(CTX_ARCHETYPE(error)));
        }
    }

    return Init_Error(out, error);
}


//
//  TO_Error: C
//
// !!! Historically this was identical to MAKE ERROR!, but MAKE and TO are
// being rethought.
//
REB_R TO_Error(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    return MAKE_Error(out, kind, nullptr, arg);
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
// This routine should either succeed and return to the caller, or panic()
// and crash if there is a problem (such as running out of memory, or that
// %errors.r has not been loaded).  Hence the caller can assume it will
// regain control to properly call va_end with no longjmp to skip it.
//
REBCTX *Make_Error_Managed_Core(
    enum Reb_Symbol cat_sym,
    enum Reb_Symbol id_sym,
    va_list *vaptr
){
    if (PG_Boot_Phase < BOOT_ERRORS) { // no STD_ERROR or template table yet
      #if !defined(NDEBUG)
        printf(
            "fail() before errors initialized, cat_sym = %d, id_sym = %d\n",
            cast(int, cat_sym),
            cast(int, id_sym)
        );
      #endif

        DECLARE_LOCAL (id_value);
        Init_Integer(id_value, cast(int, id_sym));
        panic (id_value);
    }

    REBCTX *root_error = VAL_CONTEXT(Get_System(SYS_STANDARD, STD_ERROR));

    DECLARE_LOCAL (id);
    DECLARE_LOCAL (type);
    const REBVAL *message;
    if (cat_sym == SYM_0 and id_sym == SYM_0) {
        Init_Blank(id);
        Init_Blank(type);
        message = va_arg(*vaptr, const REBVAL*);
    }
    else {
        assert(cat_sym != SYM_0 and id_sym != SYM_0);
        Init_Word(type, Canon(cat_sym));
        Init_Word(id, Canon(id_sym));

        // Assume that error IDs are unique across categories (this is checked
        // by %make-boot.r).  If they were not, then this linear search could
        // not be used.
        //
        message = Find_Error_For_Sym(id_sym);
    }

    assert(message);

    REBLEN expected_args = 0;
    if (IS_BLOCK(message)) { // GET-WORD!s in template should match va_list
        const RELVAL *temp = ARR_HEAD(VAL_ARRAY(message));
        for (; NOT_END(temp); ++temp) {
            if (IS_GET_WORD(temp))
                ++expected_args;
            else
                assert(IS_TEXT(temp));
        }
    }
    else // Just a string, no arguments expected.
        assert(IS_TEXT(message));

    REBCTX *error;
    if (expected_args == 0) {

        // If there are no arguments, we don't need to make a new keylist...
        // just a new varlist to hold this instance's settings.

        error = Copy_Context_Shallow_Managed(root_error);
    }
    else {
        // !!! See remarks on how the modern way to handle this may be to
        // put error arguments in the error object, and then have the META-OF
        // hold the generic error parameters.  Investigate how this ties in
        // with user-defined types.

        REBLEN root_len = CTX_LEN(root_error);

        // Should the error be well-formed, we'll need room for the new
        // expected values *and* their new keys in the keylist.
        //
        error = Copy_Context_Shallow_Extra_Managed(root_error, expected_args);

        // Fix up the tail first so CTX_KEY and CTX_VAR don't complain
        // in the debug build that they're accessing beyond the error length
        //
        TERM_ARRAY_LEN(CTX_VARLIST(error), root_len + expected_args + 1);
        TERM_ARRAY_LEN(CTX_KEYLIST(error), root_len + expected_args + 1);

        REBVAL *key = CTX_KEY(error, root_len) + 1;
        REBVAL *value = CTX_VAR(error, root_len) + 1;

    #ifdef NDEBUG
        const RELVAL *temp = ARR_HEAD(VAL_ARRAY(message));
    #else
        // Will get here even for a parameterless string due to throwing in
        // the extra "arguments" of the __FILE__ and __LINE__
        //
        const RELVAL *temp =
            IS_TEXT(message)
                ? cast(const RELVAL*, END_NODE) // gcc/g++ 2.95 needs (bug)
                : ARR_HEAD(VAL_ARRAY(message));
    #endif

        for (; NOT_END(temp); ++temp) {
            if (IS_GET_WORD(temp)) {
                const void *p = va_arg(*vaptr, const void*);

                // !!! Variadic Error() predates rebNull...but should possibly
                // be adapted to take nullptr instead of "nulled cells".  For
                // the moment, though, it still takes nulled cells.
                //
                assert(p != nullptr);

                if (IS_END(p)) {
                  #ifdef NDEBUG
                    //
                    // If the C code passed too few args in a debug build,
                    // prevent a crash in the release build by filling it.
                    //
                    p = BLANK_VALUE; // ...or perhaps ISSUE! `#404` ?
                  #else
                    //
                    // Termination is currently optional, but catches mistakes
                    // (requiring it could check for too *many* arguments.)
                    //
                    panic ("too few args passed for error");
                  #endif
                }

              #if !defined(NDEBUG)
                if (IS_RELATIVE(cast(const RELVAL*, p))) {
                    //
                    // Make_Error doesn't have any way to pass in a specifier,
                    // so only specific values should be used.
                    //
                    printf("Relative value passed to Make_Error()\n");
                    panic (p);
                }
              #endif

                const REBVAL *arg = cast(const REBVAL*, p);

                Init_Context_Key(key, VAL_WORD_SPELLING(temp));
                Move_Value(value, arg);

                key++;
                value++;
            }
        }

        assert(IS_END(key)); // set above by TERM_ARRAY_LEN
        assert(IS_END(value)); // ...same
    }

    mutable_KIND_BYTE(CTX_ROOTVAR(error)) = REB_ERROR;
    mutable_MIRROR_BYTE(CTX_ROOTVAR(error)) = REB_ERROR;

    // C struct mirroring fixed portion of error fields
    //
    ERROR_VARS *vars = ERR_VARS(error);

    Move_Value(&vars->message, message);
    Move_Value(&vars->id, id);
    Move_Value(&vars->type, type);

    return error;
}


//
//  Error: C
//
// This variadic function takes a number of REBVAL* arguments appropriate for
// the error category and ID passed.  It is commonly used with fail():
//
//     fail (Error(SYM_CATEGORY, SYM_SOMETHING, arg1, arg2, ...));
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
REBCTX *Error(
    int cat_sym,
    int id_sym, // can't be enum Reb_Symbol, see note below
    ... /* REBVAL *arg1, REBVAL *arg2, ... */
){
    va_list va;

    // Note: if id_sym is enum, triggers: "passing an object that undergoes
    // default argument promotion to 'va_start' has undefined behavior"
    //
    va_start(va, id_sym);

    REBCTX *error = Make_Error_Managed_Core(
        cast(enum Reb_Symbol, cat_sym),
        cast(enum Reb_Symbol, id_sym),
        &va
    );

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
REBCTX *Error_User(const char *utf8) {
    DECLARE_LOCAL (message);
    Init_Text(message, Make_String_UTF8(utf8));
    return Error(SYM_0, SYM_0, message, rebEND);
}


//
//  Error_Need_Non_End_Core: C
//
REBCTX *Error_Need_Non_End_Core(const RELVAL *target, REBSPC *specifier) {
    assert(IS_SET_WORD(target) or IS_SET_PATH(target));

    DECLARE_LOCAL (specific);
    Derelativize(specific, target, specifier);
    return Error_Need_Non_End_Raw(specific);
}


//
//  Error_Need_Non_Void_Core: C
//
REBCTX *Error_Need_Non_Void_Core(const RELVAL *target, REBSPC *specifier) {
    //
    // SET calls this, and doesn't work on just SET-WORD! and SET-PATH!
    //
    assert(ANY_WORD(target) or ANY_PATH(target) or ANY_BLOCK(target));

    DECLARE_LOCAL (specific);
    Derelativize(specific, target, specifier);
    return Error_Need_Non_Void_Raw(specific);
}


//
//  Error_Need_Non_Null_Core: C
//
REBCTX *Error_Need_Non_Null_Core(const RELVAL *target, REBSPC *specifier) {
    //
    // SET calls this, and doesn't work on just SET-WORD! and SET-PATH!
    //
    assert(ANY_WORD(target) or ANY_PATH(target) or ANY_BLOCK(target));

    DECLARE_LOCAL (specific);
    Derelativize(specific, target, specifier);
    return Error_Need_Non_Null_Raw(specific);
}


//
//  Error_Non_Logic_Refinement: C
//
// !!! This error is a placeholder for addressing the issue of using a value
// to set a refinement that's not a good fit for the refinement type, e.g.
//
//     specialize 'append [only: 10]
//
// It seems that LOGIC! should be usable, and for purposes of chaining a
// refinement-style PATH! should be usable too (for using one refinement to
// trigger another--whether the name is the same or not).  BLANK! has to be
// legal as well.  But arbitrary values probably should not be.
//
REBCTX *Error_Non_Logic_Refinement(const RELVAL *param, const REBVAL *arg) {
    DECLARE_LOCAL (word);
    Init_Word(word, VAL_PARAM_SPELLING(param));
    return Error_Non_Logic_Refine_Raw(word, Type_Of(arg));
}


//
//  Error_Bad_Func_Def: C
//
REBCTX *Error_Bad_Func_Def(const REBVAL *spec, const REBVAL *body)
{
    // !!! Improve this error; it's simply a direct emulation of arity-1
    // error that existed before refactoring code out of MAKE_Function().

    REBARR *a = Make_Array(2);
    Append_Value(a, spec);
    Append_Value(a, body);

    DECLARE_LOCAL (def);
    Init_Block(def, a);

    return Error_Bad_Func_Def_Raw(def);
}


//
//  Error_No_Arg: C
//
REBCTX *Error_No_Arg(REBFRM *f, const RELVAL *param)
{
    DECLARE_LOCAL (param_word);
    Init_Word(param_word, VAL_PARAM_SPELLING(param));

    DECLARE_LOCAL (label);
    Get_Frame_Label_Or_Blank(label, f);

    return Error_No_Arg_Raw(label, param_word);
}


//
//  Error_No_Memory: C
//
REBCTX *Error_No_Memory(REBLEN bytes)
{
    DECLARE_LOCAL (bytes_value);

    Init_Integer(bytes_value, bytes);
    return Error_No_Memory_Raw(bytes_value);
}


//
//  Error_No_Relative_Core: C
//
REBCTX *Error_No_Relative_Core(REBCEL(const*) any_word)
{
    DECLARE_LOCAL (unbound);
    Init_Any_Word(
        unbound,
        CELL_KIND(any_word),
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
    assert(Is_Param_Variadic(param));
    assert(kind != REB_VARARGS);

    // Since the "types accepted" are a lie (an [integer! <variadic>] takes
    // VARARGS! when fulfilled in a frame directly, not INTEGER!) then
    // an "honest" parameter has to be made to give the error.
    //
    DECLARE_LOCAL (honest_param);
    Init_Param(
        honest_param,
        REB_P_NORMAL,
        VAL_PARAM_SPELLING(param),
        FLAGIT_KIND(REB_VARARGS) // actually expected
    );

    return Error_Arg_Type(f, honest_param, kind);
}


//
//  Error_Invalid: C
//
// This is the very vague and generic "invalid argument" error with no further
// commentary or context.  It becomes a catch all for "unexpected input" when
// a more specific error would often be more useful.
//
// It is given a short function name as it is--unfortunately--used very often.
//
// Note: Historically the behavior of `fail (some_value)` would generate this
// error, as it could be distinguished from `fail (some_context)` meaning that
// the context was for an actual intended error.  However, this created a bad
// incompatibility with rebFail(), where the non-exposure of raw context
// pointers meant passing REBVAL* was literally failing on an error value.
//
REBCTX *Error_Invalid_Arg(REBFRM *f, const RELVAL *param)
{
    assert(IS_PARAM(param));

    RELVAL *rootparam = ARR_HEAD(ACT_PARAMLIST(FRM_PHASE(f)));
    assert(IS_ACTION(rootparam));
    assert(param > rootparam);
    assert(param <= rootparam + 1 + FRM_NUM_ARGS(f));

    DECLARE_LOCAL (label);
    if (not f->opt_label)
        Init_Blank(label);
    else
        Init_Word(label, f->opt_label);

    DECLARE_LOCAL (param_name);
    Init_Word(param_name, VAL_PARAM_SPELLING(param));

    REBVAL *arg = FRM_ARG(f, param - rootparam);
    if (IS_NULLED(arg))
        return Error_Arg_Required_Raw(label, param_name);

    return Error_Invalid_Arg_Raw(label, param_name, arg);
}


//
//  Error_Bad_Value_Core: C
//
// Will turn into an unknown error if a nulled cell is passed in.
//
REBCTX *Error_Bad_Value_Core(const RELVAL *value, REBSPC *specifier)
{
    if (IS_NULLED(value))
        fail (Error_Unknown_Error_Raw());

    DECLARE_LOCAL (specific);
    Derelativize(specific, value, specifier);

    return Error_Bad_Value_Raw(specific);
}

//
//  Error_Bad_Value_Core: C
//
REBCTX *Error_Bad_Value(const REBVAL *value)
{
    return Error_Bad_Value_Core(value, SPECIFIED);
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
    DECLARE_LOCAL (label);
    Move_Value(label, VAL_THROWN_LABEL(thrown));

    DECLARE_LOCAL (arg);
    CATCH_THROWN(arg, thrown);

    return Error_No_Catch_Raw(arg, label);
}


//
//  Error_Invalid_Type: C
//
// <type> type is not allowed here.
//
REBCTX *Error_Invalid_Type(enum Reb_Kind kind)
{
    if (kind == REB_NULLED) {
        DECLARE_LOCAL (null_word);
        Init_Word(null_word, Canon(SYM_NULL));
        fail (Error_Invalid_Type_Raw(null_word));
    }
    return Error_Invalid_Type_Raw(Datatype_From_Kind(kind));
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
//  Error_Math_Args: C
//
REBCTX *Error_Math_Args(enum Reb_Kind type, const REBVAL *verb)
{
    assert(IS_WORD(verb));
    return Error_Not_Related_Raw(verb, Datatype_From_Kind(type));
}


//
//  Error_Unexpected_Type: C
//
REBCTX *Error_Unexpected_Type(enum Reb_Kind expected, enum Reb_Kind actual)
{
    assert(expected < REB_MAX);
    assert(actual < REB_MAX);

    return Error_Expect_Val_Raw(
        Datatype_From_Kind(expected),
        Datatype_From_Kind(actual)
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
    enum Reb_Kind actual
){
    DECLARE_LOCAL (param_word);
    Init_Word(param_word, VAL_PARAM_SPELLING(param));

    DECLARE_LOCAL (label);
    Get_Frame_Label_Or_Blank(label, f);

    if (FRM_PHASE(f) != f->original) {
        //
        // When RESKIN has been used, or if an ADAPT messes up a type and
        // it isn't allowed by an inner phase, then it causes an error.  But
        // it's confusing to say that the original function didn't take that
        // type--it was on its interface.  A different message is needed.
        //
        if (actual == REB_NULLED)
            return Error_Phase_No_Arg_Raw(label, param_word);

        return Error_Phase_Bad_Arg_Type_Raw(
            label,
            Datatype_From_Kind(actual),
            param_word
        );
    }

    if (actual == REB_NULLED)  // no Datatype_From_Kind()
        return Error_Arg_Required_Raw(label, param_word);

    return Error_Expect_Arg_Raw(
        label,
        Datatype_From_Kind(actual),
        param_word
    );
}


//
//  Error_Bad_Return_Type: C
//
REBCTX *Error_Bad_Return_Type(REBFRM *f, enum Reb_Kind kind) {
    DECLARE_LOCAL (label);
    Get_Frame_Label_Or_Blank(label, f);

    if (kind == REB_NULLED)
        return Error_Needs_Return_Opt_Raw(label);

    if (kind == REB_VOID)
        return Error_Needs_Return_Value_Raw(label);

    return Error_Bad_Return_Type_Raw(label, Datatype_From_Kind(kind));
}


//
//  Error_Bad_Make: C
//
REBCTX *Error_Bad_Make(enum Reb_Kind type, const REBVAL *spec)
{
    return Error_Bad_Make_Arg_Raw(Datatype_From_Kind(type), spec);
}


//
//  Error_Bad_Make_Parent: C
//
REBCTX *Error_Bad_Make_Parent(enum Reb_Kind type, const REBVAL *parent)
{
    assert(parent != nullptr);
    return Error_Bad_Make_Parent_Raw(Datatype_From_Kind(type), parent);
}


//
//  Error_Cannot_Reflect: C
//
REBCTX *Error_Cannot_Reflect(enum Reb_Kind type, const REBVAL *arg)
{
    return Error_Cannot_Use_Raw(arg, Datatype_From_Kind(type));
}


//
//  Error_On_Port: C
//
REBCTX *Error_On_Port(enum Reb_Symbol id_sym, REBVAL *port, REBINT err_code)
{
    FAIL_IF_BAD_PORT(port);

    REBCTX *ctx = VAL_CONTEXT(port);
    REBVAL *spec = CTX_VAR(ctx, STD_PORT_SPEC);

    REBVAL *val = VAL_CONTEXT_VAR(spec, STD_PORT_SPEC_HEAD_REF);
    if (IS_BLANK(val))
        val = VAL_CONTEXT_VAR(spec, STD_PORT_SPEC_HEAD_TITLE); // less info

    DECLARE_LOCAL (err_code_value);
    Init_Integer(err_code_value, err_code);

    return Error(SYM_ACCESS, id_sym, val, err_code_value, rebEND);
}


//
//  Startup_Errors: C
//
// Create error objects and error type objects
//
REBCTX *Startup_Errors(const REBVAL *boot_errors)
{
  #ifdef DEBUG_HAS_PROBE
    const char *env_probe_failures = getenv("R3_PROBE_FAILURES");
    if (env_probe_failures != NULL and atoi(env_probe_failures) != 0) {
        printf(
            "**\n"
            "** R3_PROBE_FAILURES is nonzero in environment variable!\n"
            "** Rather noisy, but helps for debugging the boot process...\n"
            "**\n"
        );
        fflush(stdout);
        PG_Probe_Failures = true;
    }
  #endif

    assert(VAL_INDEX(boot_errors) == 0);
    REBCTX *catalog = Construct_Context_Managed(
        REB_OBJECT,
        VAL_ARRAY_KNOWN_MUTABLE_AT(boot_errors),  // modifies bindings
        VAL_SPECIFIER(boot_errors),
        NULL
    );

    // Create objects for all error types (CAT_ERRORS is "selfish", currently
    // so self is in slot 1 and the actual errors start at context slot 2)
    //
    REBVAL *val;
    for (val = CTX_VAR(catalog, SELFISH(1)); NOT_END(val); val++) {
        REBCTX *error = Construct_Context_Managed(
            REB_OBJECT,
            ARR_HEAD(VAL_ARRAY_KNOWN_MUTABLE(val)),  // modifies bindings
            SPECIFIED, // source array not in a function body
            NULL
        );
        Init_Object(val, error);
    }

    return catalog;
}


//
//  Startup_Stackoverflow: C
//
void Startup_Stackoverflow(void)
{
    Root_Stackoverflow_Error = Init_Error(
        Alloc_Value(),
        Error_Stack_Overflow_Raw()
    );
}


//
//  Shutdown_Stackoverflow: C
//
void Shutdown_Stackoverflow(void)
{
    rebRelease(Root_Stackoverflow_Error);
    Root_Stackoverflow_Error = NULL;
}


// !!! Though molding has a general facility for a "limit" of the overall
// mold length, this only limits the length a particular value can contribute
// to the mold.  It was only used in error molding and was kept working
// without a general review of such a facility.  Review.
//
static void Mold_Value_Limit(REB_MOLD *mo, RELVAL *v, REBLEN limit)
{
    REBSTR *str = mo->series;

    REBLEN start_len = STR_LEN(str);
    REBSIZ start_size = STR_SIZE(str);

    Mold_Value(mo, v);  // Note: can't cache pointer into `str` across this

    REBLEN end_len = STR_LEN(str);

    if (end_len - start_len > limit) {
        REBCHR(*) at = STR_HEAD(str) + start_size;
        REBLEN n = 0;
        for (; n < limit; ++n)
            at = NEXT_STR(at);

        SET_STR_LEN_SIZE(str, start_len + limit, at - STR_HEAD(str));
        Free_Bookmarks_Maybe_Null(str);

        Append_Ascii(str, "...");
    }
}


//
//  MF_Error: C
//
void MF_Error(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    // Protect against recursion. !!!!
    //
    if (not form) {
        MF_Context(mo, v, false);
        return;
    }

    REBCTX *error = VAL_CONTEXT(v);
    ERROR_VARS *vars = ERR_VARS(error);

    // Form: ** <type> Error:
    //
    Append_Ascii(mo->series, "** ");
    if (IS_WORD(&vars->type)) {  // has a <type>
        Append_Spelling(mo->series, VAL_WORD_SPELLING(&vars->type));
        Append_Codepoint(mo->series, ' ');
    }
    else
        assert(IS_BLANK(&vars->type));  // no <type>
    Append_Ascii(mo->series, RM_ERROR_LABEL);  // "Error:"

    // Append: error message ARG1, ARG2, etc.
    if (IS_BLOCK(&vars->message))
        Form_Array_At(mo, VAL_ARRAY(&vars->message), 0, error);
    else if (IS_TEXT(&vars->message))
        Form_Value(mo, &vars->message);
    else
        Append_Ascii(mo->series, RM_BAD_ERROR_FORMAT);

    // Form: ** Where: function
    REBVAL *where = SPECIFIC(&vars->where);
    if (
        not IS_BLANK(where)
        and not (IS_BLOCK(where) and VAL_LEN_AT(where) == 0)
    ){
        Append_Codepoint(mo->series, '\n');
        Append_Ascii(mo->series, RM_ERROR_WHERE);
        Form_Value(mo, where);
    }

    // Form: ** Near: location
    REBVAL *nearest = SPECIFIC(&vars->nearest);
    if (not IS_BLANK(nearest)) {
        Append_Codepoint(mo->series, '\n');
        Append_Ascii(mo->series, RM_ERROR_NEAR);

        if (IS_TEXT(nearest)) {
            //
            // !!! The scanner puts strings into the near information in order
            // to say where the file and line of the scan problem was.  This
            // seems better expressed as an explicit argument to the scanner
            // error, because otherwise it obscures the LOAD call where the
            // scanner was invoked.  Review.
            //
            Append_String(mo->series, nearest, VAL_LEN_HEAD(nearest));
        }
        else if (ANY_ARRAY(nearest) or ANY_PATH(nearest))
            Mold_Value_Limit(mo, nearest, 60);
        else
            Append_Ascii(mo->series, RM_BAD_ERROR_FORMAT);
    }

    // Form: ** File: filename
    //
    // !!! In order to conserve space in the system, filenames are interned.
    // Although interned strings are GC'd when no longer referenced, they can
    // only be used in ANY-WORD! values at the moment, so the filename is
    // not a FILE!.
    //
    REBVAL *file = SPECIFIC(&vars->file);
    if (not IS_BLANK(file)) {
        Append_Codepoint(mo->series, '\n');
        Append_Ascii(mo->series, RM_ERROR_FILE);
        if (IS_WORD(file))
            Form_Value(mo, file);
        else
            Append_Ascii(mo->series, RM_BAD_ERROR_FORMAT);
    }

    // Form: ** Line: line-number
    REBVAL *line = SPECIFIC(&vars->line);
    if (not IS_BLANK(line)) {
        Append_Codepoint(mo->series, '\n');
        Append_Ascii(mo->series, RM_ERROR_LINE);
        if (IS_INTEGER(line))
            Form_Value(mo, line);
        else
            Append_Ascii(mo->series, RM_BAD_ERROR_FORMAT);
    }
}
