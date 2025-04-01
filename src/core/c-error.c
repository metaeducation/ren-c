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
    s->stack_base = TOP_INDEX;

    // There should not be a Collect_Keys in progress.  (We use a non-zero
    // length of the collect buffer to tell if a later fail() happens in
    // the middle of a Collect_Keys.)
    //
    assert(Array_Len(BUF_COLLECT) == 0);

    s->guarded_len = Flex_Len(GC_Guarded);
    s->level = TOP_LEVEL;

    s->manuals_len = Flex_Len(GC_Manuals);
    s->mold_buf_len = Flex_Len(MOLD_BUF);
    s->mold_loop_tail = Array_Len(TG_Mold_Stack);

    // !!! Is this initialization necessary?
    s->error = nullptr;
}


#if RUNTIME_CHECKS

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
    if (s->stack_base != TOP_INDEX) {
        printf(
            "PUSH()x%d without DROP()\n",
            cast(int, TOP_INDEX - s->stack_base)
        );
        panic_at (nullptr, file, line);
    }

    assert(s->level == TOP_LEVEL);

    assert(Array_Len(BUF_COLLECT) == 0);

    if (s->guarded_len != Flex_Len(GC_Guarded)) {
        printf(
            "Push_GC_Guard()x%d without Drop_GC_Guard()\n",
            cast(int, Flex_Len(GC_Guarded) - s->guarded_len)
        );
        Node* guarded = *Flex_At(
            Node*,
            GC_Guarded,
            Flex_Len(GC_Guarded) - 1
        );
        panic_at (guarded, file, line);
    }

    // !!! Note that this inherits a test that uses GC_Manuals->content.xxx
    // instead of Flex_Len().  The idea being that although some series
    // are able to fit in the series node, the GC_Manuals wouldn't ever
    // pay for that check because it would always be known not to.  Review
    // this in general for things that may not need "series" overhead,
    // e.g. a contiguous pointer stack.
    //
    if (s->manuals_len > Flex_Len(GC_Manuals)) {
        //
        // Note: Should this ever actually happen, panic() on the series won't
        // do any real good in helping debug it.  You'll probably need to
        // add additional checks in Manage_Flex and Free_Unmanaged_Flex
        // that check against the caller's manuals_len.
        //
        panic_at ("manual series freed outside checkpoint", file, line);
    }
    else if (s->manuals_len < Flex_Len(GC_Manuals)) {
        printf(
            "Make_Flex()x%d w/o Free_Unmanaged_Flex()/Manage_Flex()\n",
            cast(int, Flex_Len(GC_Manuals) - s->manuals_len)
        );
        Flex* manual = *(Flex_At(
            Flex*,
            GC_Manuals,
            Flex_Len(GC_Manuals) - 1
        ));
        panic_at (manual, file, line);
    }

    assert(s->mold_buf_len == Flex_Len(MOLD_BUF));
    assert(s->mold_loop_tail == Array_Len(TG_Mold_Stack));

    assert(s->error == nullptr);  // !!! necessary?
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
    assert(CTX_TYPE(s->error) == TYPE_ERROR);

    // Restore Rebol data stack pointer at time of Push_Trap
    //
    Drop_Data_Stack_To(s->stack_base);

    // If we were in the middle of a Collect_Keys and an error occurs, then
    // the binding lookup table has entries in it that need to be zeroed out.
    // We can tell if that's necessary by whether there is anything
    // accumulated in the collect buffer.
    //
    if (Array_Len(BUF_COLLECT) != 0)
        Collect_End(nullptr);  // !!! No binder, review implications

    // Free any manual series that were extant at the time of the error
    // (that were created since this PUSH_TRAP started).  This includes
    // any arglist series in call frames that have been wiped off the stack.
    // (Closure series will be managed.)
    //
    assert(Flex_Len(GC_Manuals) >= s->manuals_len);
    while (Flex_Len(GC_Manuals) != s->manuals_len) {
        // Freeing the series will update the tail...
        Free_Unmanaged_Flex(
            *Flex_At(Flex*, GC_Manuals, Flex_Len(GC_Manuals) - 1)
        );
    }

    Set_Flex_Len(GC_Guarded, s->guarded_len);
    TG_Top_Level = s->level;
    Term_Non_Array_Flex_Len(MOLD_BUF, s->mold_buf_len);

  #if RUNTIME_CHECKS
    //
    // Because reporting errors in the actual Push_Mold process leads to
    // recursion, this debug flag helps make it clearer what happens if
    // that does happen... and can land on the right comment.  But if there's
    // a fail of some kind, the flag for the warning needs to be cleared.
    //
    TG_Pushing_Mold = false;
  #endif

    Set_Flex_Len(TG_Mold_Stack, s->mold_loop_tail);

    Saved_State = s->last_state;
}


//
//  Fail_Core: C
//
// Cause a "trap" of an error by longjmp'ing to the enclosing PUSH_TRAP.  Note
// that these failures interrupt code mid-stream, so if a Rebol function is
// running it will not make it to the point of returning the result value.
// This distinguishes the "fail" mechanic from the "throw" mechanic, which has
// to bubble up a THROWN() value through OUT (used to implement BREAK,
// CONTINUE, RETURN, LEAVE, HALT...)
//
// The function will auto-detect if the pointer it is given is an ERROR!'s
// VarList* or a UTF-8 string.  If it's a string, an error will be created from
// it automatically.
//
// !!! Previously, detecting a value would use that value as the ubiquitous
// (but vague) "Invalid Arg" error.  However, since this is called by fail(),
// that is misleading as rebFail() takes ERROR! values, STRING!s, or BLOCK!s
// so those cases were changed to `fail (Invalid_Arg(V))` instead.
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
    Error* error;

  #if DEBUG_HAS_PROBE
    //
    // This is set via an environment variable (e.g. R3_PROBE_FAILURES=1)
    // Helpful for debugging boot, before command line parameters are parsed.
    //
    if (PG_Probe_Failures) {
        static bool probing = false;

        if (p == cast(void*, Cell_Varlist(Root_Stackoverflow_Error))) {
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

    case DETECTED_AS_STUB: {
        Flex* s = m_cast(Flex*, cast(const Flex*, p)); // don't mutate
        if (Not_Array_Flag(s, IS_VARLIST))
            panic (s);
        error = cast(Error*, s);
        break; }

    default:
        panic (p); // suppress compiler error from non-smart compilers
    }

    ASSERT_CONTEXT(error);
    assert(CTX_TYPE(error) == TYPE_ERROR);

    // If we raise the error we'll lose the stack, and if it's an early
    // error we always want to see it (do not use ATTEMPT or TRAP on
    // purpose in Startup_Core()...)
    //
    if (PG_Boot_Phase < BOOT_DONE)
        panic (error);

    // There should be a PUSH_TRAP of some kind in effect if a `fail` can
    // ever be run.
    //
    if (Saved_State == nullptr)
        panic (error);

    // The information for the Rebol call frames generally is held in stack
    // variables, so the data will go bad in the longjmp.  We have to free
    // the data *before* the jump.  Be careful not to let this code get too
    // recursive or do other things that would be bad news if we're responding
    // to C_STACK_OVERFLOWING.  (See notes on the sketchiness in general of
    // the way R3-Alpha handles stack overflows, and alternative plans.)
    //
    Level* L = TOP_LEVEL;
    while (L != Saved_State->level) {
        if (Is_Action_Level(L)) {
            assert(L->varlist); // action must be running
            Array* stub = L->varlist; // will be stubbed, info bits reset
            Drop_Action(L);
            Set_Flex_Info(stub, FRAME_FAILED); // API leaks o.k.
        }

        Level* prior = L->prior;
        Abort_Level(L); // will call va_end() if variadic frame
        L = prior;
    }

    TG_Top_Level = L;  // TG_Top_Level is writable TOP_LEVEL

    Saved_State->error = error;

    // If a THROWN() was being processed up the stack when the error was
    // raised, then it had the thrown argument set.  Mark unreadable in debug
    // builds.  (The value will not be kept alive, it is not seen by GC)
    //
    Init_Unreadable(&TG_Thrown_Arg);

    LONG_JUMP(Saved_State->cpu_state, 1);
}


//
//  Stack_Depth: C
//
REBLEN Stack_Depth(void)
{
    REBLEN depth = 0;

    Level* L = TOP_LEVEL;
    while (L) {
        if (Is_Action_Level(L))
            if (not Is_Action_Level_Fulfilling(L)) {
                //
                // We only count invoked functions (not group or path
                // evaluations or "pending" functions that are building their
                // arguments but have not been formally invoked yet)
                //
                ++depth;
            }

        L = L->prior;
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
const Value* Find_Error_For_Sym(SymId id_sym)
{
    Symbol* id_canon = Canon(id_sym);

    VarList* categories = Cell_Varlist(Get_System(SYS_CATALOG, CAT_ERRORS));
    assert(CTX_KEY_SYM(categories, 1) == SYM_SELF);

    REBLEN ncat = SELFISH(1);
    for (; ncat <= Varlist_Len(categories); ++ncat) {
        VarList* category = Cell_Varlist(Varlist_Slot(categories, ncat));

        REBLEN n = SELFISH(1);
        for (; n <= Varlist_Len(category); ++n) {
            if (Are_Synonyms(CTX_KEY_SPELLING(category, n), id_canon)) {
                Value* message = Varlist_Slot(category, n);
                assert(Is_Block(message) or Is_Text(message));
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
    Error* error,
    Level* where // must be valid and executing on the stack
) {
    assert(where != nullptr);

    StackIndex base = TOP_INDEX;

    ERROR_VARS *vars = ERR_VARS(error);

    // WHERE is a backtrace in the form of a block of label words, that start
    // from the top of stack and go downward.
    //
    Level* L = where;
    for (; L != BOTTOM_LEVEL; L = L->prior) {
        //
        // Only invoked functions (not pending functions, groups, etc.)
        //
        if (not Is_Action_Level(L))
            continue;
        if (Is_Action_Level_Fulfilling(L))
            continue;
        if (L->original == PG_Dummy_Action)
            continue;

        Get_Level_Label_Or_Blank(PUSH(), L);
    }
    Init_Block(&vars->where, Pop_Stack_Values(base));

    // Nearby location of the error.  Reify any valist that is running,
    // so that the error has an array to present.
    //
    Init_Near_For_Frame(&vars->nearest, where);

    // Try to fill in the file and line information of the error from the
    // stack, looking for arrays with ARRAY_FLAG_HAS_FILE_LINE.
    //
    L = where;
    for (; L != BOTTOM_LEVEL; L = L->prior) {
        if (not L->source->array) {
            //
            // !!! We currently skip any calls from C (e.g. rebValue()) and look
            // for calls from Rebol files for the file and line.  However,
            // rebValue() might someday supply its C code __FILE__ and __LINE__,
            // which might be interesting to put in the error instead.
            //
            continue;
        }
        if (Not_Array_Flag(L->source->array, HAS_FILE_LINE))
            continue;
        break;
    }
    if (L != BOTTOM_LEVEL) {
        Option(String*) file = LINK(L->source->array).file;
        LineNumber line = MISC(L->source->array).line;

        if (file)
            Init_File(&vars->file, unwrap file);
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
    Value* out, // output location **MUST BE GC SAFE**!
    const Value* arg
) {
    // Frame from the error object template defined in %sysobj.r
    //
    VarList* root_error = Cell_Varlist(Get_System(SYS_STANDARD, STD_ERROR));

    VarList* varlist;
    ERROR_VARS *vars; // C struct mirroring fixed portion of error fields

    if (Is_Error(arg) or Is_Object(arg)) {
        // Create a new error object from another object, including any
        // non-standard fields.  WHERE: and NEAR: will be overridden if
        // used.  If ID:, TYPE:, or CODE: were used in a way that would
        // be inconsistent with a Rebol system error, an error will be
        // raised later in the routine.

        varlist = Merge_Contexts_Selfish_Managed(root_error, Cell_Varlist(arg));
        vars = ERR_VARS(varlist);
    }
    else if (Is_Block(arg)) {
        // If a block, then effectively MAKE OBJECT! on it.  Afterward,
        // apply the same logic as if an OBJECT! had been passed in above.

        // Bind and do an evaluation step (as with MAKE OBJECT! with A_MAKE
        // code in REBTYPE(Context) and code in DECLARE_NATIVE(CONSTRUCT))

        varlist = Make_Selfish_Context_Detect_Managed(
            TYPE_ERROR, // type
            Cell_List_At(arg), // values to scan for toplevel set-words
            root_error // parent
        );

        // Protect the error from GC by putting into out, which must be
        // passed in as a GC-protecting value slot.
        //
        Init_Error(out, cast(Error*, varlist));

        Rebind_Context_Deep(root_error, varlist, nullptr);  // no more binds
        Bind_Values_Deep(Cell_List_At(arg), varlist);

        DECLARE_VALUE (evaluated);
        if (Eval_List_At_Throws(evaluated, arg)) {
            Copy_Cell(out, evaluated);
            return true;
        }

        vars = ERR_VARS(varlist);
    }
    else if (Is_Text(arg)) {
        //
        // String argument to MAKE ERROR! makes a custom error from user:
        //
        //     code: null ;-- default is null
        //     type: null
        //     id: null
        //     message: "whatever the string was"
        //
        // Minus the message, this is the default state of root_error.

        varlist = Copy_Context_Shallow_Managed(root_error);

        vars = ERR_VARS(varlist);
        assert(Is_Nulled(&vars->type));
        assert(Is_Nulled(&vars->id));

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

    if (Is_Word(&vars->type) and Is_Word(&vars->id)) {
        // If there was no CODE: supplied but there was a TYPE: and ID: then
        // this may overlap a combination used by Rebol where we wish to
        // fill in the code.  (No fast lookup for this, must search.)

        VarList* categories = Cell_Varlist(Get_System(SYS_CATALOG, CAT_ERRORS));

        // Find correct category for TYPE: (if any)
        Value* category
            = Select_Canon_In_Context(categories, VAL_WORD_CANON(&vars->type));

        if (category) {
            assert(Is_Object(category));
            assert(CTX_KEY_SYM(Cell_Varlist(category), 1) == SYM_SELF);

            // Find correct message for ID: (if any)

            Value* message = Select_Canon_In_Context(
                Cell_Varlist(category), VAL_WORD_CANON(&vars->id)
            );

            if (message) {
                assert(Is_Text(message) or Is_Block(message));

                if (not Is_Nulled(&vars->message))
                    fail (Error_Invalid_Error_Raw(arg));

                Copy_Cell(&vars->message, message);
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

                fail (Error_Invalid_Error_Raw(Varlist_Archetype(varlist)));
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
            (Is_Word(&vars->id) or Is_Nulled(&vars->id))
            and (Is_Word(&vars->type) or Is_Nulled(&vars->type))
            and (
                Is_Block(&vars->message)
                or Is_Text(&vars->message)
                or Is_Nulled(&vars->message)
            )
        )){
            fail (Error_Invalid_Error_Raw(Varlist_Archetype(varlist)));
        }
    }

    Error* error = cast(Error*, varlist);
    Set_Location_Of_Error(error, TOP_LEVEL);

    Init_Error(out, error);
    return false;
}


//
//  Make_Error_Managed_Vaptr: C
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
Error* Make_Error_Managed_Vaptr(
    SymId cat_sym,
    SymId id_sym,
    va_list *vaptr
){
    if (PG_Boot_Phase < BOOT_ERRORS) { // no STD_ERROR or template table yet
      #if RUNTIME_CHECKS
        printf(
            "fail() before errors initialized, cat_sym = %d, id_sym = %d\n",
            cast(int, cat_sym),
            cast(int, id_sym)
        );
      #endif

        DECLARE_VALUE (id_value);
        Init_Integer(id_value, cast(int, id_sym));
        panic (id_value);
    }

    VarList* root_error = Cell_Varlist(Get_System(SYS_STANDARD, STD_ERROR));

    DECLARE_VALUE (id);
    DECLARE_VALUE (type);
    const Value* message;
    if (cat_sym == SYM_0 and id_sym == SYM_0) {
        Init_Nulled(id);
        Init_Nulled(type);
        message = va_arg(*vaptr, const Value*);
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
    if (Is_Block(message)) { // GET-WORD!s in template should match va_list
        Cell* temp = VAL_ARRAY_HEAD(message);
        for (; NOT_END(temp); ++temp) {
            if (Is_Get_Word(temp))
                ++expected_args;
            else
                assert(Is_Text(temp));
        }
    }
    else // Just a string, no arguments expected.
        assert(Is_Text(message));

    VarList* varlist;
    if (expected_args == 0) {

        // If there are no arguments, we don't need to make a new keylist...
        // just a new varlist to hold this instance's settings.

        varlist = Copy_Context_Shallow_Managed(root_error);
    }
    else {
        // !!! See remarks on how the modern way to handle this may be to
        // put error arguments in the error object, and then have the META-OF
        // hold the generic error parameters.  Investigate how this ties in
        // with user-defined types.

        REBLEN root_len = Varlist_Len(root_error);

        // Should the error be well-formed, we'll need room for the new
        // expected values *and* their new keys in the keylist.
        //
        varlist = Copy_Context_Shallow_Extra_Managed(root_error, expected_args);

        // Fix up the tail first so Varlist_Key and Varlist_Slot don't complain
        // in the debug build that they're accessing beyond the error length
        //
        Term_Array_Len(Varlist_Array(varlist), root_len + expected_args + 1);
        Term_Array_Len(Keylist_Of_Varlist(varlist), root_len + expected_args + 1);

        Value* key = Varlist_Key(varlist, root_len) + 1;
        Value* value = Varlist_Slot(varlist, root_len) + 1;

    #if NO_RUNTIME_CHECKS
        const Cell* temp = VAL_ARRAY_HEAD(message);
    #else
        // Will get here even for a parameterless string due to throwing in
        // the extra "arguments" of the __FILE__ and __LINE__
        //
        const Cell* temp =
            Is_Text(message)
                ? cast(const Cell*, END_NODE) // gcc/g++ 2.95 needs (bug)
                : VAL_ARRAY_HEAD(message);
    #endif

        for (; NOT_END(temp); ++temp) {
            if (Is_Get_Word(temp)) {
                const void *p = va_arg(*vaptr, const void*);

                // !!! Variadic Error() predates rebNull...but should possibly
                // be adapted to take nullptr instead of "nulled cells".  For
                // the moment, though, it still takes nulled cells.
                //
                assert(p != nullptr);

                if (IS_END(p)) {
                    assert(!"too few args passed for error");
                    p = BLANK_VALUE;  // tolerate in release build?
                }
                else if (IS_RELATIVE(cast(const Cell*, p))) {
                    //
                    // Make_Error doesn't have any way to pass in a specifier,
                    // so only specific values should be used.
                    //
                    assert(!"Relative value passed to Make_Error()\n");
                    p = BLANK_VALUE;  // release tolerance?
                }

                const Value* arg = cast(const Value*, p);
                if (Is_Nulled(arg))
                    Init_Word(value, Canon(SYM__TNULL_T));
                else if (Is_Void(arg))
                    Init_Word(value, Canon(SYM__TVOID_T));
                else
                    Copy_Cell(value, arg);
                value++;

                Init_Typeset(
                    key,
                    TS_VALUE, // !!! Currently not in use
                    Cell_Word_Symbol(temp)
                );
                key++;
            }
        }

        assert(IS_END(key)); // set above by Term_Array_Len
        assert(IS_END(value)); // ...same
    }

    // C struct mirroring fixed portion of error fields
    //
    ERROR_VARS *vars = ERR_VARS(varlist);

    Copy_Cell(&vars->message, message);
    Copy_Cell(&vars->id, id);
    Copy_Cell(&vars->type, type);

    Error* error = cast(Error*, varlist);
    Set_Location_Of_Error(error, TOP_LEVEL);
    return error;
}


//
//  Make_Error_Managed: C
//
// This variadic function takes a number of Value* arguments appropriate for
// the error category and ID passed.  It is commonly used with fail():
//
//     fail (Error(SYM_CATEGORY, SYM_SOMETHING, arg1, arg2, ...));
//
// Note that in C, variadic functions don't know how many arguments they were
// passed.  Make_Error_Managed_Vaptr() knows how many arguments are in an
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
Error* Make_Error_Managed(
    int cat_sym,
    int id_sym, // can't be SymId, see note below
    ... /* Value* arg1, Value* arg2, ... */
){
    va_list va;

    // Note: if id_sym is enum, triggers: "passing an object that undergoes
    // default argument promotion to 'va_start' has undefined behavior"
    //
    va_start(va, id_sym);

    Error* error = Make_Error_Managed_Vaptr(
        cast(SymId, cat_sym),
        cast(SymId, id_sym),
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
Error* Error_User(const char *utf8) {
    DECLARE_VALUE (message);
    Init_Text(message, Make_String_UTF8(utf8));
    return Make_Error_Managed(SYM_0, SYM_0, message, rebEND);
}


//
//  Error_Need_Non_End_Core: C
//
Error* Error_Need_Non_End_Core(const Cell* target, Specifier* specifier) {
    assert(Is_Set_Word(target) or Is_Set_Path(target));

    DECLARE_VALUE (specific);
    Derelativize(specific, target, specifier);
    return Error_Need_Non_End_Raw(specific);
}


//
//  Error_Non_Logic_Refinement: C
//
// Ren-C allows functions to be specialized, such that a function's frame can
// be filled (or partially filled) by an example frame.  The variables
// corresponding to refinements must be canonized to either TRUE or FALSE
// by these specializations, because that's what the called function expects.
//
Error* Error_Non_Logic_Refinement(const Cell* param, const Value* arg) {
    DECLARE_VALUE (word);
    Init_Word(word, Cell_Parameter_Symbol(param));
    return Error_Non_Logic_Refine_Raw(word, Datatype_Of(arg));
}


//
//  Error_Bad_Func_Def: C
//
Error* Error_Bad_Func_Def(const Value* spec, const Value* body)
{
    // !!! Improve this error; it's simply a direct emulation of arity-1
    // error that existed before refactoring code out of MAKE_Function().

    Array* a = Make_Array(2);
    Append_Value(a, spec);
    Append_Value(a, body);

    DECLARE_VALUE (def);
    Init_Block(def, a);

    return Error_Bad_Func_Def_Raw(def);
}


//
//  Error_No_Arg: C
//
Error* Error_No_Arg(Level* L, const Cell* param)
{
    assert(Is_Typeset(param));

    DECLARE_VALUE (param_word);
    Init_Word(param_word, Cell_Parameter_Symbol(param));

    DECLARE_VALUE (label);
    Get_Level_Label_Or_Blank(label, L);

    return Error_No_Arg_Raw(label, param_word);
}


//
//  Error_No_Memory: C
//
Error* Error_No_Memory(REBLEN bytes)
{
    DECLARE_VALUE (bytes_value);

    Init_Integer(bytes_value, bytes);
    return Error_No_Memory_Raw(bytes_value);
}


//
//  Error_No_Relative_Core: C
//
Error* Error_No_Relative_Core(const Cell* any_word)
{
    DECLARE_VALUE (unbound);
    Init_Any_Word(
        unbound,
        Type_Of(any_word),
        Cell_Word_Symbol(any_word)
    );

    return Error_No_Relative_Raw(unbound);
}


//
//  Error_Not_Varargs: C
//
Error* Error_Not_Varargs(
    Level* L,
    const Cell* param,
    enum Reb_Kind kind
){
    assert(Is_Param_Variadic(param));
    assert(kind != TYPE_VARARGS);

    // Since the "types accepted" are a lie (an [integer! <...>] takes
    // VARARGS! when fulfilled in a frame directly, not INTEGER!) then
    // an "honest" parameter has to be made to give the error.
    //
    DECLARE_VALUE (honest_param);
    Init_Typeset(
        honest_param,
        FLAGIT_KIND(TYPE_VARARGS), // actually expected
        Cell_Parameter_Symbol(param)
    );

    return Error_Arg_Type(L, honest_param, kind);
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
// pointers meant passing Value* was literally failing on an error value.
//
Error* Error_Invalid(const Value* value)
{
    return Error_Invalid_Arg_Raw(value);
}


//
//  Error_Invalid_Core: C
//
Error* Error_Invalid_Core(const Cell* value, Specifier* specifier)
{
    DECLARE_VALUE (specific);
    Derelativize(specific, value, specifier);

    return Error_Invalid_Arg_Raw(specific);
}


//
//  Error_Bad_Func_Def_Core: C
//
Error* Error_Bad_Func_Def_Core(const Cell* item, Specifier* specifier)
{
    DECLARE_VALUE (specific);
    Derelativize(specific, item, specifier);
    return Error_Bad_Func_Def_Raw(specific);
}


//
//  Error_Bad_Refine_Revoke: C
//
// We may have to search for the refinement, so we always do (speed of error
// creation not considered that relevant to the evaluator, being overshadowed
// by the error handling).  See the remarks about the state of L->refine in
// the LevelStruct definition.
//
Error* Error_Bad_Refine_Revoke(const Cell* param, const Value* arg)
{
    assert(Is_Typeset(param));

    DECLARE_VALUE (param_name);
    Init_Word(param_name, Cell_Parameter_Symbol(param));

    while (Cell_Parameter_Class(param) != PARAMCLASS_REFINEMENT)
        --param;

    DECLARE_VALUE (refine_name);
    Init_Refinement(refine_name, Cell_Parameter_Symbol(param));

    if (Is_Nulled(arg)) // was void and shouldn't have been
        return Error_Bad_Refine_Revoke_Raw(refine_name, param_name);

    // wasn't void and should have been
    //
    return Error_Argument_Revoked_Raw(refine_name, param_name);
}


//
//  Error_No_Value_Core: C
//
Error* Error_No_Value_Core(const Cell* target, Specifier* specifier) {
    DECLARE_VALUE (specified);
    Derelativize(specified, target, specifier);

    return Error_No_Value_Raw(specified);
}


//
//  Error_Fetched_Tripwire_Core: C
//
Error* Error_Fetched_Tripwire_Core(
    const Cell* target,
    Specifier* specifier,
    const Value* tripwire
){
    DECLARE_VALUE (specified);
    Derelativize(specified, target, specifier);

    return Error_Fetched_Tripwire_Raw(specified, tripwire);
}


//
//  Error_No_Value: C
//
Error* Error_No_Value(const Value* target) {
    return Error_No_Value_Core(target, SPECIFIED);
}


//
//  Error_No_Catch_For_Throw: C
//
Error* Error_No_Catch_For_Throw(Value* thrown)
{
    DECLARE_VALUE (arg);

    assert(THROWN(thrown));
    CATCH_THROWN(arg, thrown); // clears bit, thrown is now the /NAME

    return Error_No_Catch_Raw(arg, thrown);
}


//
//  Error_Invalid_Type: C
//
// <type> type is not allowed here.
//
Error* Error_Invalid_Type(enum Reb_Kind kind)
{
    return Error_Invalid_Type_Raw(Datatype_From_Kind(kind));
}


//
//  Error_Out_Of_Range: C
//
// value out of range: <value>
//
Error* Error_Out_Of_Range(const Value* arg)
{
    return Error_Out_Of_Range_Raw(arg);
}


//
//  Error_Protected_Key: C
//
Error* Error_Protected_Key(Value* key)
{
    assert(Is_Typeset(key));

    DECLARE_VALUE (key_name);
    Init_Word(key_name, Key_Symbol(key));

    return Error_Protected_Word_Raw(key_name);
}


//
//  Error_Illegal_Action: C
//
Error* Error_Illegal_Action(enum Reb_Kind type, Value* verb)
{
    assert(Is_Word(verb));
    return Error_Cannot_Use_Raw(verb, Datatype_From_Kind(type));
}


//
//  Error_Math_Args: C
//
Error* Error_Math_Args(enum Reb_Kind type, Value* verb)
{
    assert(Is_Word(verb));
    return Error_Not_Related_Raw(verb, Datatype_From_Kind(type));
}


//
//  Error_Unexpected_Type: C
//
Error* Error_Unexpected_Type(enum Reb_Kind expected, enum Reb_Kind actual)
{
    assert(expected < TYPE_MAX);
    assert(actual < TYPE_MAX);

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
Error* Error_Arg_Type(
    Level* L,
    const Cell* param,
    enum Reb_Kind actual
) {
    assert(Is_Typeset(param));

    DECLARE_VALUE (param_word);
    Init_Word(param_word, Cell_Parameter_Symbol(param));

    DECLARE_VALUE (label);
    Get_Level_Label_Or_Blank(label, L);

    if (actual != TYPE_MAX_NULLED)
        return Error_Expect_Arg_Raw(
            label,
            Datatype_From_Kind(actual),
            param_word
        );

    // Although TYPE_MAX_NULLED is not a type, the typeset bits are used
    // to check it.  Since Datatype_From_Kind() will fail, use another error.
    //
    return Error_Arg_Required_Raw(label, param_word);
}


//
//  Error_Bad_Return_Type: C
//
Error* Error_Bad_Return_Type(Level* L, enum Reb_Kind kind) {
    DECLARE_VALUE (label);
    Get_Level_Label_Or_Blank(label, L);

    if (kind == TYPE_MAX_NULLED)
        return Error_Needs_Return_Opt_Raw(label);

    if (kind == TYPE_NOTHING)
        return Error_Needs_Return_Value_Raw(label);

    return Error_Bad_Return_Type_Raw(label, Datatype_From_Kind(kind));
}


//
//  Error_Bad_Make: C
//
Error* Error_Bad_Make(enum Reb_Kind type, const Value* spec)
{
    return Error_Bad_Make_Arg_Raw(Datatype_From_Kind(type), spec);
}


//
//  Error_Cannot_Reflect: C
//
Error* Error_Cannot_Reflect(enum Reb_Kind type, const Value* arg)
{
    return Error_Cannot_Use_Raw(arg, Datatype_From_Kind(type));
}


//
//  Error_On_Port: C
//
Error* Error_On_Port(SymId id_sym, Value* port, REBINT err_code)
{
    FAIL_IF_BAD_PORT(port);

    VarList* ctx = Cell_Varlist(port);
    Value* spec = Varlist_Slot(ctx, STD_PORT_SPEC);

    Value* val = Cell_Varlist_VAR(spec, STD_PORT_SPEC_HEAD_REF);
    if (Is_Nulled(val))
        val = Cell_Varlist_VAR(spec, STD_PORT_SPEC_HEAD_TITLE); // less info

    DECLARE_VALUE (err_code_value);
    Init_Integer(err_code_value, err_code);

    return Make_Error_Managed(SYM_ACCESS, id_sym, val, err_code_value, rebEND);
}


//
//  Startup_Errors: C
//
// Create error objects and error type objects
//
VarList* Startup_Errors(const Value* boot_errors)
{
  #if DEBUG_HAS_PROBE
    const char *env_probe_failures = getenv("R3_PROBE_FAILURES");
    if (env_probe_failures != nullptr and atoi(env_probe_failures) != 0) {
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
    VarList* catalog = Construct_Context_Managed(
        TYPE_OBJECT,
        Cell_List_At(boot_errors),
        VAL_SPECIFIER(boot_errors),
        nullptr
    );

    // Create objects for all error types (CAT_ERRORS is "selfish", currently
    // so self is in slot 1 and the actual errors start at context slot 2)
    //
    Value* val;
    for (val = Varlist_Slot(catalog, SELFISH(1)); NOT_END(val); val++) {
        VarList* error = Construct_Context_Managed(
            TYPE_OBJECT,
            VAL_ARRAY_HEAD(val),
            SPECIFIED, // source array not in a function body
            nullptr
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
    Root_Stackoverflow_Error = nullptr;
}


// Limited molder (used, e.g., for errors)
//
static void Mold_Value_Limit(Molder* mo, Cell* v, REBLEN len)
{
    REBLEN start = Flex_Len(mo->utf8flex);
    Mold_Value(mo, v);

    if (Flex_Len(mo->utf8flex) - start > len) {
        Set_Flex_Len(mo->utf8flex, start + len);
        Append_Unencoded(mo->utf8flex, "...");
    }
}


//
//  MF_Error: C
//
void MF_Error(Molder* mo, const Cell* v, bool form)
{
    // Protect against recursion. !!!!
    //
    if (not form) {
        MF_Context(mo, v, false);
        return;
    }

    VarList* error = Cell_Varlist(v);
    ERROR_VARS *vars = ERR_VARS(error);

    // Form: ** <type> Error:
    if (Is_Nulled(&vars->type))
        Emit(mo, "** S", RM_ERROR_LABEL);
    else {
        assert(Is_Word(&vars->type));
        Emit(mo, "** W S", &vars->type, RM_ERROR_LABEL);
    }

    // Append: error message ARG1, ARG2, etc.
    if (Is_Block(&vars->message))
        Form_Array_At(mo, Cell_Array(&vars->message), 0, error);
    else if (Is_Text(&vars->message))
        Form_Value(mo, &vars->message);
    else
        Append_Unencoded(mo->utf8flex, RM_BAD_ERROR_FORMAT);

    // Form: ** Where: function
    Value* where = KNOWN(&vars->where);
    if (
        not Is_Nulled(where)
        and not (Is_Block(where) and Cell_Series_Len_At(where) == 0)
    ){
        Append_Codepoint(mo->utf8flex, '\n');
        Append_Unencoded(mo->utf8flex, RM_ERROR_WHERE);
        Form_Value(mo, where);
    }

    // Form: ** Near: location
    Value* nearest = KNOWN(&vars->nearest);
    if (not Is_Nulled(nearest)) {
        Append_Codepoint(mo->utf8flex, '\n');
        Append_Unencoded(mo->utf8flex, RM_ERROR_NEAR);

        if (Is_Text(nearest)) {
            //
            // !!! The scanner puts strings into the near information in order
            // to say where the file and line of the scan problem was.  This
            // seems better expressed as an explicit argument to the scanner
            // error, because otherwise it obscures the LOAD call where the
            // scanner was invoked.  Review.
            //
            Append_Utf8_String(mo->utf8flex, nearest, VAL_LEN_HEAD(nearest));
        }
        else if (Any_List(nearest))
            Mold_Value_Limit(mo, nearest, 60);
        else
            Append_Unencoded(mo->utf8flex, RM_BAD_ERROR_FORMAT);
    }

    // Form: ** File: filename
    //
    // !!! In order to conserve space in the system, filenames are interned.
    // Although interned strings are GC'd when no longer referenced, they can
    // only be used in ANY-WORD! values at the moment, so the filename is
    // not a FILE!.
    //
    Value* file = KNOWN(&vars->file);
    if (not Is_Nulled(file)) {
        Append_Codepoint(mo->utf8flex, '\n');
        Append_Unencoded(mo->utf8flex, RM_ERROR_FILE);
        if (Is_File(file))
            Form_Value(mo, file);
        else
            Append_Unencoded(mo->utf8flex, RM_BAD_ERROR_FORMAT);
    }

    // Form: ** Line: line-number
    Value* line = KNOWN(&vars->line);
    if (not Is_Nulled(line)) {
        Append_Codepoint(mo->utf8flex, '\n');
        Append_Unencoded(mo->utf8flex, RM_ERROR_LINE);
        if (Is_Integer(line))
            Form_Value(mo, line);
        else
            Append_Unencoded(mo->utf8flex, RM_BAD_ERROR_FORMAT);
    }
}


//
//  try: native [
//
//  "Approximation of modern Ren-C definitional error try (nullify ERROR!)"
//
//      value [~null~ any-value!]
//  ]
//
DECLARE_NATIVE(TRY)
{
    INCLUDE_PARAMS_OF_TRY;

    Value *v = ARG(VALUE);

    if (Is_Error(v))
        return nullptr;

    return Copy_Cell(OUT, v);
}


//
//  trap: native [
//
//  "Approximation of modern Ren-C definitional error trap (ERROR! => ERROR!)"
//
//      return: [~null~ any-value!]
//      code [block!]
//  ]
//
DECLARE_NATIVE(TRAP)
{
    INCLUDE_PARAMS_OF_TRAP;

    Value *code = ARG(CODE);

    DECLARE_LEVEL (L);
    Push_Level(L, code);

    while (NOT_END(L->value)) {
        if (Eval_Step_Throws(SET_END(OUT), L)) {
            Abort_Level(L);
            return BOUNCE_THROWN;
        }

        if (IS_END(OUT)) { // e.g. `reduce [comment "hi"]`
            assert(IS_END(L->value));
            break;
        }

        if (Is_Error(OUT))
            break;
    }

    Drop_Level_Unbalanced(L); // Drop_Level() asserts on accumulation

    if (Is_Error(OUT))
        return OUT;

    return nullptr;
}


//
//  except: infix native [
//
//  "If left hand side is an ERROR!, run the branch"
//
//      return: "Input value if not error, or branch result"
//          [~null~ any-value!]
//      left "Run branch if this is an error"
//          [~null~ ~void~ any-value!]
//      branch [block! action!]
//  ]
//
DECLARE_NATIVE(EXCEPT)
{
    INCLUDE_PARAMS_OF_EXCEPT;

    Value* left = ARG(LEFT);
    if (not Is_Error(left))
        RETURN (left);

    if (Do_Branch_With_Throws(OUT, ARG(BRANCH), left))
        return BOUNCE_THROWN;

    return OUT;
}
