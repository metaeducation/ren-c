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
            cast(int, DSP - s->dsp)
        );
        panic_at (nullptr, file, line);
    }

    assert(s->frame == FS_TOP);

    assert(ARR_LEN(BUF_COLLECT) == 0);

    if (s->guarded_len != SER_LEN(GC_Guarded)) {
        printf(
            "PUSH_GC_GUARD()x%d without DROP_GC_GUARD()\n",
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
        // add additional checks in Manage_Series and Free_Unmanaged_Series
        // that check against the caller's manuals_len.
        //
        panic_at ("manual series freed outside checkpoint", file, line);
    }
    else if (s->manuals_len < SER_LEN(GC_Manuals)) {
        printf(
            "Make_Ser()x%d w/o Free_Unmanaged_Series or MANAGE_SERIES\n",
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
    assert(SER_LEN(GC_Manuals) >= s->manuals_len);
    while (SER_LEN(GC_Manuals) != s->manuals_len) {
        // Freeing the series will update the tail...
        Free_Unmanaged_Series(
            *SER_AT(REBSER*, GC_Manuals, SER_LEN(GC_Manuals) - 1)
        );
    }

    SET_SERIES_LEN(GC_Guarded, s->guarded_len);
    TG_Top_Frame = s->frame;
    TERM_SEQUENCE_LEN(MOLD_BUF, s->mold_buf_len);

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

    Saved_State = s->last_state;
}


//
//  Fail_Core: C
//
// Cause a "trap" of an error by longjmp'ing to the enclosing PUSH_TRAP.  Note
// that these failures interrupt code mid-stream, so if a Rebol function is
// running it will not make it to the point of returning the result value.
// This distinguishes the "fail" mechanic from the "throw" mechanic, which has
// to bubble up a THROWN() value through D_OUT (used to implement BREAK,
// CONTINUE, RETURN, LEAVE, HALT...)
//
// The function will auto-detect if the pointer it is given is an ERROR!'s
// REBCTX* or a UTF-8 string.  If it's a string, an error will be created from
// it automatically.
//
// !!! Previously, detecting a value would use that value as the ubiquitous
// (but vague) "Invalid Arg" error.  However, since this is called by fail(),
// that is misleading as rebFail() takes ERROR! values, STRING!s, or BLOCK!s
// so those cases were changed to `fail (Invalid_Arg(v))` instead.
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

  #ifdef DEBUG_HAS_PROBE
    //
    // This is set via an environment variable (e.g. R3_PROBE_FAILURES=1)
    // Helpful for debugging boot, before command line parameters are parsed.
    //
    if (PG_Probe_Failures) {
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
        if (Is_Action_Frame(f)) {
            assert(f->varlist); // action must be running
            REBARR *stub = f->varlist; // will be stubbed, info bits reset
            Drop_Action(f);
            SET_SER_INFO(stub, FRAME_INFO_FAILED); // API leaks o.k.
        }

        REBFRM *prior = f->prior;
        Abort_Frame(f); // will call va_end() if variadic frame
        f = prior;
    }

    TG_Top_Frame = f; // TG_Top_Frame is writable FS_TOP

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

    REBCNT ncat = SELFISH(1);
    for (; ncat <= CTX_LEN(categories); ++ncat) {
        REBCTX *category = VAL_CONTEXT(CTX_VAR(categories, ncat));

        REBCNT n = SELFISH(1);
        for (; n <= CTX_LEN(category); ++n) {
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
    REBFRM *where // must be valid and executing on the stack
) {
    assert(where != NULL);

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

        DS_PUSH_TRASH;
        Get_Frame_Label_Or_Blank(DS_TOP, f);
    }
    Init_Block(&vars->where, Pop_Stack_Values(dsp_orig));

    // Nearby location of the error.  Reify any valist that is running,
    // so that the error has an array to present.
    //
    Init_Near_For_Frame(&vars->nearest, where);

    // Try to fill in the file and line information of the error from the
    // stack, looking for arrays with ARRAY_FLAG_FILE_LINE.
    //
    f = where;
    for (; f != FS_BOTTOM; f = f->prior) {
        if (not f->source->array) {
            //
            // !!! We currently skip any calls from C (e.g. rebRun()) and look
            // for calls from Rebol files for the file and line.  However,
            // rebRun() might someday supply its C code __FILE__ and __LINE__,
            // which might be interesting to put in the error instead.
            //
            continue;
        }
        if (NOT_SER_FLAG(f->source->array, ARRAY_FLAG_FILE_LINE))
            continue;
        break;
    }
    if (f != FS_BOTTOM) {
        REBSTR *file = LINK(f->source->array).file;
        REBLIN line = MISC(f->source->array).line;

        REBSYM file_sym = STR_SYMBOL(file);
        if (file_sym != SYM___ANONYMOUS__)
            Init_Word(&vars->file, file);
        if (line != 0)
            Init_Integer(&vars->line, line);
    }
}


//
//  Make_Error_Object_Throws: C
//
// Creates an error object from arg and puts it in value.
// The arg can be a string or an object body block.
//
// Returns true if a THROWN() value is made during evaluation.
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
bool Make_Error_Object_Throws(
    REBVAL *out, // output location **MUST BE GC SAFE**!
    const REBVAL *arg
) {
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
        Bind_Values_Deep(VAL_ARRAY_AT(arg), error);

        DECLARE_LOCAL (evaluated);
        if (Do_Any_Array_At_Throws(evaluated, arg)) {
            Move_Value(out, evaluated);
            return true;
        }

        vars = ERR_VARS(error);
    }
    else if (IS_TEXT(arg)) {
        //
        // String argument to MAKE ERROR! makes a custom error from user:
        //
        //     code: _ ;-- default is blank
        //     type: _
        //     id: _
        //     message: "whatever the string was"
        //
        // Minus the message, this is the default state of root_error.

        error = Copy_Context_Shallow_Managed(root_error);

        vars = ERR_VARS(error);
        assert(IS_BLANK(&vars->type));
        assert(IS_BLANK(&vars->id));

        Init_Text(&vars->message, Copy_Sequence_At_Position(arg));
    }
    else
        fail (Error_Invalid(arg));

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

    Set_Location_Of_Error(error, FS_TOP);

    Init_Error(out, error);
    return false;
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

    REBCNT expected_args = 0;
    if (IS_BLOCK(message)) { // GET-WORD!s in template should match va_list
        RELVAL *temp = VAL_ARRAY_HEAD(message);
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

        REBCNT root_len = CTX_LEN(root_error);

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
        const RELVAL *temp = VAL_ARRAY_HEAD(message);
    #else
        // Will get here even for a parameterless string due to throwing in
        // the extra "arguments" of the __FILE__ and __LINE__
        //
        const RELVAL *temp =
            IS_TEXT(message)
                ? cast(const RELVAL*, END_NODE) // gcc/g++ 2.95 needs (bug)
                : VAL_ARRAY_HEAD(message);
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

                Init_Typeset(
                    key,
                    TS_VALUE, // !!! Currently not in use
                    VAL_WORD_SPELLING(temp)
                );
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

    Move_Value(&vars->message, message);
    Move_Value(&vars->id, id);
    Move_Value(&vars->type, type);

    Set_Location_Of_Error(error, FS_TOP);
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
    assert(ANY_WORD(target) or ANY_PATH(target));

    DECLARE_LOCAL (specific);
    Derelativize(specific, target, specifier);
    return Error_Need_Non_Void_Raw(specific);
}


//
//  Error_Non_Logic_Refinement: C
//
// Ren-C allows functions to be specialized, such that a function's frame can
// be filled (or partially filled) by an example frame.  The variables
// corresponding to refinements must be canonized to either TRUE or FALSE
// by these specializations, because that's what the called function expects.
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

    REBARR *a = Make_Arr(2);
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
    assert(IS_TYPESET(param));

    DECLARE_LOCAL (param_word);
    Init_Word(param_word, VAL_PARAM_SPELLING(param));

    DECLARE_LOCAL (label);
    Get_Frame_Label_Or_Blank(label, f);

    return Error_No_Arg_Raw(label, param_word);
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
//  Error_No_Relative_Core: C
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
    assert(Is_Param_Variadic(param));
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
REBCTX *Error_Invalid(const REBVAL *value)
{
    return Error_Invalid_Arg_Raw(value);
}


//
//  Error_Invalid_Core: C
//
REBCTX *Error_Invalid_Core(const RELVAL *value, REBSPC *specifier)
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
REBCTX *Error_Bad_Refine_Revoke(const RELVAL *param, const REBVAL *arg)
{
    assert(IS_TYPESET(param));

    DECLARE_LOCAL (param_name);
    Init_Word(param_name, VAL_PARAM_SPELLING(param));

    while (VAL_PARAM_CLASS(param) != PARAM_CLASS_REFINEMENT)
        --param;

    DECLARE_LOCAL (refine_name);
    Init_Refinement(refine_name, VAL_PARAM_SPELLING(param));

    if (IS_NULLED(arg)) // was void and shouldn't have been
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
    CATCH_THROWN(arg, thrown); // clears bit, thrown is now the /NAME

    return Error_No_Catch_Raw(arg, thrown);
}


//
//  Error_Invalid_Type: C
//
// <type> type is not allowed here.
//
REBCTX *Error_Invalid_Type(enum Reb_Kind kind)
{
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
//  Error_Illegal_Action: C
//
REBCTX *Error_Illegal_Action(enum Reb_Kind type, REBVAL *verb)
{
    assert(IS_WORD(verb));
    return Error_Cannot_Use_Raw(verb, Datatype_From_Kind(type));
}


//
//  Error_Math_Args: C
//
REBCTX *Error_Math_Args(enum Reb_Kind type, REBVAL *verb)
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
) {
    assert(IS_TYPESET(param));

    DECLARE_LOCAL (param_word);
    Init_Word(param_word, VAL_PARAM_SPELLING(param));

    DECLARE_LOCAL (label);
    Get_Frame_Label_Or_Blank(label, f);

    if (actual != REB_MAX_NULLED)
        return Error_Expect_Arg_Raw(
            label,
            Datatype_From_Kind(actual),
            param_word
        );

    // Although REB_MAX_NULLED is not a type, the typeset bits are used
    // to check it.  Since Datatype_From_Kind() will fail, use another error.
    //
    return Error_Arg_Required_Raw(label, param_word);
}


//
//  Error_Bad_Return_Type: C
//
REBCTX *Error_Bad_Return_Type(REBFRM *f, enum Reb_Kind kind) {
    DECLARE_LOCAL (label);
    Get_Frame_Label_Or_Blank(label, f);

    if (kind == REB_MAX_NULLED)
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
        VAL_ARRAY_AT(boot_errors),
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
            VAL_ARRAY_HEAD(val),
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
    enum Reb_Symbol errcode = SYM_SECURITY_ERROR;

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
        else if (ANY_BINSTR(policy) and name != NULL) {
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
        errcode = SYM_SECURITY;
        policy = name ? name : 0;

    error:
        ; // need statement
        DECLARE_LOCAL (temp);
        if (!policy) {
            Init_Word(temp, spelling);
            policy = temp;
        }
        fail (Error(SYM_SECURITY, errcode, policy));
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
void MF_Error(REB_MOLD *mo, const RELVAL *v, bool form)
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
    if (IS_BLANK(&vars->type))
        Emit(mo, "** S", RM_ERROR_LABEL);
    else {
        assert(IS_WORD(&vars->type));
        Emit(mo, "** W S", &vars->type, RM_ERROR_LABEL);
    }

    // Append: error message ARG1, ARG2, etc.
    if (IS_BLOCK(&vars->message))
        Form_Array_At(mo, VAL_ARRAY(&vars->message), 0, error);
    else if (IS_TEXT(&vars->message))
        Form_Value(mo, &vars->message);
    else
        Append_Unencoded(mo->series, RM_BAD_ERROR_FORMAT);

    // Form: ** Where: function
    REBVAL *where = KNOWN(&vars->where);
    if (
        not IS_BLANK(where)
        and not (IS_BLOCK(where) and VAL_LEN_AT(where) == 0)
    ){
        Append_Utf8_Codepoint(mo->series, '\n');
        Append_Unencoded(mo->series, RM_ERROR_WHERE);
        Form_Value(mo, where);
    }

    // Form: ** Near: location
    REBVAL *nearest = KNOWN(&vars->nearest);
    if (not IS_BLANK(nearest)) {
        Append_Utf8_Codepoint(mo->series, '\n');
        Append_Unencoded(mo->series, RM_ERROR_NEAR);

        if (IS_TEXT(nearest)) {
            //
            // !!! The scanner puts strings into the near information in order
            // to say where the file and line of the scan problem was.  This
            // seems better expressed as an explicit argument to the scanner
            // error, because otherwise it obscures the LOAD call where the
            // scanner was invoked.  Review.
            //
            Append_Utf8_String(mo->series, nearest, VAL_LEN_HEAD(nearest));
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
    if (not IS_BLANK(file)) {
        Append_Utf8_Codepoint(mo->series, '\n');
        Append_Unencoded(mo->series, RM_ERROR_FILE);
        if (IS_WORD(file))
            Form_Value(mo, file);
        else
            Append_Unencoded(mo->series, RM_BAD_ERROR_FORMAT);
    }

    // Form: ** Line: line-number
    REBVAL *line = KNOWN(&vars->line);
    if (not IS_BLANK(line)) {
        Append_Utf8_Codepoint(mo->series, '\n');
        Append_Unencoded(mo->series, RM_ERROR_LINE);
        if (IS_INTEGER(line))
            Form_Value(mo, line);
        else
            Append_Unencoded(mo->series, RM_BAD_ERROR_FORMAT);
    }
}
