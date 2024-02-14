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
//  Fail_Core: C
//
// Trigger failure of an error by longjmp'ing to enclosing RESCUE_SCOPE.  Note
// that these failures interrupt code mid-stream, so if a Rebol function is
// running it will not make it to the point of returning the result value.
// This distinguishes the "fail" mechanic from the "throw" mechanic, which has
// to bubble up a thrown value through OUT (used to implement BREAK,
// CONTINUE, RETURN, LEAVE, HALT...)
//
// The function will auto-detect if the pointer it is given is an ERROR!'s
// Context* or a UTF-8 char *.  If it's UTF-8, an error will be created from
// it automatically (but with no ID...the string becomes the "ID")
//
// If the pointer is to a function parameter of the current native (e.g. what
// you get for PARAM(name) in a native), then it will report both the
// parameter name and value as being implicated as a problem.  This only
// works for the current topmost stack level.
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
  #if REBOL_FAIL_JUST_ABORTS
    assert(!"Fail_Core() called and REBOL_FAIL_JUST_ABORTS, shouldn't happen");
  #endif

  #if DEBUG_PRINTF_FAIL_LOCATIONS && DEBUG_COUNT_TICKS
    //
    // File and line are printed by the calling macro to capture __FILE__ and
    // __LINE__ without adding parameter overhead to this function for non
    // debug builds.
    //
    printf("%ld\n", cast(long, TG_tick));  /* tick count prefix */
  #endif

    // You can't abruptly fail during the handling of abrupt failure.  At the
    // moment we're assuming that once a level has failed it can't recover if
    // it originated the failure...but this may be revisited.
    //
    assert(Not_Level_Flag(TOP_LEVEL, ABRUPT_FAILURE));

    // The topmost level must be the one issuing the error.  If a level was
    // pushed with LEVEL_FLAG_TRAMPOLINE_KEEPALIVE that finished executing
    // but remained pushed, it must be dropped before the level that pushes
    // it issues a failure.
    //
    assert(TOP_LEVEL->executor != nullptr);

    Context* error;
    if (p == nullptr) {
        error = Error_Unknown_Error_Raw();
    }
    else switch (Detect_Rebol_Pointer(p)) {
      case DETECTED_AS_UTF8:
        error = Error_User(c_cast(char*, p));
        break;

      case DETECTED_AS_SERIES: {
        Series* s = m_cast(Series*, c_cast(Series* , p));  // don't mutate
        if (not IS_VARLIST(s))
            panic (s);  // only kind of series allowed are contexts of ERROR!
        error = cast(Context*, s);
        break; }

      case DETECTED_AS_CELL: {
        const Atom* atom = c_cast(Atom*, p);
        assert(not Is_Antiform_Unstable(atom));  // should handle this case...
        UNUSED(atom);

        const Value* v = c_cast(Value*, p);

        // Check to see if the REBVAL* cell is in the paramlist of the current
        // running native.  (We could theoretically do this with ARG(), or
        // have a nuance of behavior with ARG()...or even for the Key* .)
        //
        if (Is_Node_Root_Bit_Set(v)) {
            //
            // If you call the internal fail() function on an API handle, that
            // should be the handle of an error.  If we allowed it to take
            // any value, then it would call into question the treatment of
            // the error as an error and not erroring on "some value"
            //
            if (Is_Error(v)) {
                error = VAL_CONTEXT(v);
            }
            else {
                assert(!"fail() given API handle that is not an ERROR!");
                error = Error_Bad_Value(v);
            }
            rebRelease(m_cast(Value*, v));  // released even if we didn't
        }
        else if (not Is_Action_Level(TOP_LEVEL))
            error = Error_Bad_Value(v);
        else {
            const Param* head = ACT_PARAMS_HEAD(Level_Phase(TOP_LEVEL));
            REBLEN num_params = ACT_NUM_PARAMS(Level_Phase(TOP_LEVEL));

            if (v >= head and v < head + num_params) {
                const Param* param = cast_PAR(c_cast(REBVAL*, v));
                error = Error_Invalid_Arg(TOP_LEVEL, param);
            }
            else
                error = Error_Bad_Value(v);
        }
        break; }

      default:
        panic (p);  // suppress compiler error from non-smart compilers
    }

    Assert_Context(error);
    assert(CTX_TYPE(error) == REB_ERROR);

  #if DEBUG_EXTANT_STACK_POINTERS
    //
    // We trust that the stack levels were checked on each evaluator step as
    // 0, so that when levels are unwound we should be back to 0 again.  The
    // longjmp will cross the C++ destructors, which is technically undefined
    // but for this debug setting we can hope it will just not run them.
    //
    // Set_Location_Of_Error() uses stack, so this has to be done first, else
    // the PUSH() will warn that there is stack outstanding.
    //
    g_ds.num_refs_extant = 0;
  #endif

    // If the error doesn't have a where/near set, set it from stack.  Do
    // this before the PROBE() of the error, so the information is useful.
    //
    // !!! Do not do this for out off memory errors, as it allocates memory.
    // If this were to be done there would have to be a preallocated array
    // to use for it.
    //
    if (error != Error_No_Memory(1020))  // static global, review
        Force_Location_Of_Error(error, TOP_LEVEL);

  #if DEBUG_HAS_PROBE
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

    // If we raise the error we'll lose the stack, and if it's an early
    // error we always want to see it (do not use ATTEMPT or TRY on
    // purpose in Startup_Core()...)
    //
    if (PG_Boot_Phase < BOOT_DONE)
        panic (error);

    // There should be a RESCUE_SCOPE of some kind in effect if a `fail` can
    // ever be run.
    //
    if (g_ts.jump_list == nullptr)
        panic (error);

    // If a throw was being processed up the stack when the error was raised,
    // then it had the thrown argument set.
    //
    Erase_Cell(&g_ts.thrown_arg);
    Erase_Cell(&g_ts.thrown_label);

  #if REBOL_FAIL_JUST_ABORTS
    panic (nullptr);  // all branches need to do something, this never happens
  #elif REBOL_FAIL_USES_TRY_CATCH
    throw error;
  #else
    STATIC_ASSERT(REBOL_FAIL_USES_LONGJMP);

    // "If the function that called setjmp has exited (whether by return or
    //  by a different longjmp higher up the stack), the behavior is undefined.
    //  In other words, only long jumps up the call stack are allowed."
    //
    //  http://en.cppreference.com/w/c/program/longjmp

    g_ts.jump_list->error = error;  // longjmp() argument too small for pointer
    LONG_JUMP(g_ts.jump_list->cpu_state, 1);  // 1 so setjmp() returns nonzero
  #endif
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
            if (not Is_Level_Fulfilling(L)) {
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
const REBVAL *Find_Error_For_Sym(SymId id)
{
    const Symbol* canon = Canon_Symbol(id);

    Context* categories = VAL_CONTEXT(Get_System(SYS_CATALOG, CAT_ERRORS));

    REBLEN ncat = 1;
    for (; ncat <= CTX_LEN(categories); ++ncat) {
        Context* category = VAL_CONTEXT(CTX_VAR(categories, ncat));

        REBLEN n = 1;
        for (; n != CTX_LEN(category) + 1; ++n) {
            if (Are_Synonyms(KEY_SYMBOL(CTX_KEY(category, n)), canon)) {
                REBVAL *message = CTX_VAR(category, n);
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
// depth of a running level.  Also, if running from a C fail() call, the
// file and line information can be captured in the debug build.
//
void Set_Location_Of_Error(
    Context* error,
    Level* where  // must be valid and executing on the stack
) {
    while (Get_Level_Flag(where, BLAME_PARENT))  // e.g. Apply_Only_Throws()
        where = where->prior;

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
        if (Is_Level_Fulfilling(L))
            continue;

        Get_Level_Label_Or_Nulled(PUSH(), L);

        // !!! We can't push a NULL to stack to pop in block.  BLANK! is one
        // option...it's not as informative as (anonymous) but also takes up
        // less space, so people might come to appreciate it.  Review.
        //
        if (Is_Nulled(TOP))
            Init_Blank(TOP);
    }
    Init_Block(&vars->where, Pop_Stack_Values(base));

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
    if (Is_Nulled(&vars->nearest))
        Init_Near_For_Level(&vars->nearest, where);

    // Try to fill in the file and line information of the error from the
    // stack, looking for arrays with ARRAY_HAS_FILE_LINE.
    //
    L = where;
    for (; L != BOTTOM_LEVEL; L = L->prior) {
        if (Level_Is_Variadic(L)) {
            //
            // !!! We currently skip any calls from C (e.g. rebValue()) and look
            // for calls from Rebol files for the file and line.  However,
            // rebValue() might someday supply its C code __FILE__ and __LINE__,
            // which might be interesting to put in the error instead.
            //
            continue;
        }
        if (Not_Array_Flag(Level_Array(L), HAS_FILE_LINE_UNMASKED))
            continue;
        break;
    }

    if (L != BOTTOM_LEVEL) {  // found a level with file and line information
        const String* file = LINK(Filename, Level_Array(L));
        LineNumber line = Level_Array(L)->misc.line;

        if (file)
            Init_File(&vars->file, file);
        if (line != 0)
            Init_Integer(&vars->line, line);
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
Bounce MAKE_Error(
    Level* level_,
    enum Reb_Kind kind,
    Option(const Value*) parent,
    const REBVAL *arg
){
    assert(kind == REB_ERROR);
    UNUSED(kind);

    if (parent)  // !!! Should probably be able to work!
        fail (Error_Bad_Make_Parent(kind, unwrap(parent)));

    // Frame from the error object template defined in %sysobj.r
    //
    Context* root_error = VAL_CONTEXT(Get_System(SYS_STANDARD, STD_ERROR));

    Context* e;
    ERROR_VARS *vars; // C struct mirroring fixed portion of error fields

    if (Is_Block(arg)) {
        // If a block, then effectively MAKE OBJECT! on it.  Afterward,
        // apply the same logic as if an OBJECT! had been passed in above.

        // Bind and do an evaluation step (as with MAKE OBJECT! with A_MAKE
        // code in REBTYPE(Context) and code in DECLARE_NATIVE(construct))

        const Element* tail;
        const Element* head = Cell_Array_At(&tail, arg);

        e = Make_Context_Detect_Managed(
            REB_ERROR, // type
            head, // values to scan for toplevel set-words
            tail,
            root_error // parent
        );

        // Protect the error from GC by putting into out, which must be
        // passed in as a GC-protecting value slot.
        //
        Init_Error(OUT, e);

        Rebind_Context_Deep(root_error, e, nullptr);  // NULL=>no more binds

        DECLARE_STABLE (virtual_arg);
        Copy_Cell(virtual_arg, arg);
        Virtual_Bind_Deep_To_Existing_Context(
            virtual_arg,
            e,
            nullptr,  // binder
            REB_WORD
        );

        DECLARE_LOCAL (evaluated);
        if (Do_Any_Array_At_Throws(evaluated, virtual_arg, SPECIFIED))
            return BOUNCE_THROWN;

        vars = ERR_VARS(e);
    }
    else if (Is_Text(arg)) {
        //
        // String argument to MAKE ERROR! makes a custom error from user:
        //
        //     code: null  ; default is null
        //     type: null
        //     id: null
        //     message: "whatever the string was"
        //
        // Minus the message, this is the default state of root_error.

        e = Copy_Context_Shallow_Managed(root_error);
        Init_Error(OUT, e);

        vars = ERR_VARS(e);
        assert(Is_Nulled(&vars->type));
        assert(Is_Nulled(&vars->id));

        Init_Text(&vars->message, Copy_String_At(arg));
    }
    else
        return RAISE(arg);

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

        Context* categories = VAL_CONTEXT(Get_System(SYS_CATALOG, CAT_ERRORS));

        // Find correct category for TYPE: (if any)
        Option(Value*) category = Select_Symbol_In_Context(
            CTX_ARCHETYPE(categories),
            Cell_Word_Symbol(&vars->type)
        );

        if (category) {
            assert(Is_Object(unwrap(category)));

            // Find correct message for ID: (if any)

            Option(Value*) message = Select_Symbol_In_Context(
                unwrap(category),
                Cell_Word_Symbol(&vars->id)
            );

            if (message) {
                assert(Is_Text(unwrap(message)) or Is_Block(unwrap(message)));

                if (not Is_Nulled(&vars->message))
                    return RAISE(Error_Invalid_Error_Raw(arg));

                Copy_Cell(&vars->message, unwrap(message));
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

                return RAISE(Error_Invalid_Error_Raw(CTX_ARCHETYPE(e)));
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
            fail (Error_Invalid_Error_Raw(CTX_ARCHETYPE(e)));
        }
    }

    assert(Is_Error(OUT));
    return OUT;
}


//
//  TO_Error: C
//
// !!! Historically this was identical to MAKE ERROR!, but MAKE and TO are
// being rethought.
//
Bounce TO_Error(Level* level_, enum Reb_Kind kind, const REBVAL *arg)
{
    return MAKE_Error(level_, kind, nullptr, arg);
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
Context* Make_Error_Managed_Core(
    SymId cat_id,
    SymId id,
    va_list *vaptr
){
    if (PG_Boot_Phase < BOOT_ERRORS) { // no STD_ERROR or template table yet
      #if !defined(NDEBUG)
        printf(
            "fail() before errors initialized, cat_id = %d, id = %d\n",
            cast(int, cat_id),
            cast(int, id)
        );
      #endif

        DECLARE_ELEMENT (id_value);
        Init_Integer(id_value, cast(int, id));
        panic (id_value);
    }

    Context* root_error = VAL_CONTEXT(Get_System(SYS_STANDARD, STD_ERROR));

    DECLARE_STABLE (id_value);
    DECLARE_STABLE (type);
    const REBVAL *message;  // Stack values ("movable") are allowed
    if (cat_id == SYM_0 and id == SYM_0) {
        Init_Nulled(id_value);
        Init_Nulled(type);
        message = va_arg(*vaptr, const REBVAL*);
    }
    else {
        assert(cat_id != SYM_0 and id != SYM_0);
        Init_Word(type, Canon_Symbol(cat_id));
        Init_Word(id_value, Canon_Symbol(id));

        // Assume that error IDs are unique across categories (this is checked
        // by %make-boot.r).  If they were not, then this linear search could
        // not be used.
        //
        message = Find_Error_For_Sym(id);
    }

    assert(message);

    REBLEN expected_args = 0;
    if (Is_Block(message)) { // GET-WORD!s in template should match va_list
        const Element* tail;
        const Element* temp = Cell_Array_At(&tail, message);
        for (; temp != tail; ++temp) {
            if (Is_Get_Word(temp))
                ++expected_args;
            else
                assert(Is_Text(temp));
        }
    }
    else // Just a string, no arguments expected.
        assert(Is_Text(message));

    // !!! Should things like NEAR and WHERE be in the META and not in the
    // object for the ERROR! itself, so the error could have arguments with
    // any name?  (e.g. NEAR and WHERE?)  In that case, we would be copying
    // the "standard format" error as a meta object instead.
    //
    bool deeply = false;
    Context* error = Copy_Context_Extra_Managed(
        root_error,
        expected_args,  // Note: won't make new keylist if expected_args is 0
        deeply
    );

    // Arrays from errors.r look like `["The value" :arg1 "is not" :arg2]`
    // They can also be a single TEXT! (which will just bypass this loop).
    //
    if (not Is_Text(message)) {
        const Element* msg_tail;
        const Element* msg_item = Cell_Array_At(&msg_tail, message);

        for (; msg_item != msg_tail; ++msg_item) {
            if (not Is_Get_Word(msg_item))
                continue;

            const Symbol* symbol = Cell_Word_Symbol(msg_item);
            REBVAL *var = Append_Context(error, symbol);

            const void *p = va_arg(*vaptr, const void*);

            if (p == nullptr) {
                //
                // !!! Should variadic error take `nullptr` instead of
                // "nulled cells"?
                //
                assert(!"nullptr passed to Make_Error_Managed_Core()");
                Init_Nulled(var);
            }
            else switch (Detect_Rebol_Pointer(p)) {
              case DETECTED_AS_END :
                assert(!"Not enough arguments in Make_Error_Managed()");
                Init_Anti_Word(var, Canon(END));
                break;

              case DETECTED_AS_CELL : {
                //
                // It's too much effort to force callers to pass SPECIFIC
                // values--so strip off the binding.  (We could preserve it
                // in those cases it was specified, but that could set up
                // the wrong expectations that the system is trying.)
                //
                Copy_Cell(var, c_cast(Value*, p));
                break; }

              default:
                assert(false);
                fail ("Bad pointer passed to Error()");
            }
        }
    }

    assert(CTX_LEN(error) == CTX_LEN(root_error) + expected_args);

    HEART_BYTE(CTX_ROOTVAR(error)) = REB_ERROR;

    // C struct mirroring fixed portion of error fields
    //
    ERROR_VARS *vars = ERR_VARS(error);

    Copy_Cell(&vars->message, message);
    Copy_Cell(&vars->id, id_value);
    Copy_Cell(&vars->type, type);

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
Context* Error(
    int cat_id,
    int id, // can't be SymId, see note below
    ... /* REBVAL *arg1, REBVAL *arg2, ... */
){
    va_list va;

    // Note: if id is SymId, triggers: "passing an object that undergoes
    // default argument promotion to 'va_start' has undefined behavior"
    //
    va_start(va, id);

    Context* error = Make_Error_Managed_Core(
        cast(SymId, cat_id),
        cast(SymId, id),
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
Context* Error_User(const char *utf8) {
    DECLARE_LOCAL (message);
    Init_Text(message, Make_String_UTF8(utf8));
    return Error(SYM_0, SYM_0, message, rebEND);
}


//
//  Error_Need_Non_End: C
//
Context* Error_Need_Non_End(const Element* target) {
    assert(
        Is_Set_Word(target) or Is_Set_Tuple(target) or Is_Set_Group(target)
        or Is_Set_Path(target)  // only needed in legacy Redbol
    );
    return Error_Need_Non_End_Raw(target);
}


//
//  Error_Bad_Word_Get: C
//
Context* Error_Bad_Word_Get(
    const Element* target,
    const Value* anti
){
    // SET calls this, and doesn't work on just SET-WORD! and SET-PATH!
    //
    assert(
        Any_Word(target)
        or Any_Sequence(target)
        or Any_Block(target)
        or Any_Group(target)
    );
    assert(Is_Antiform(anti));

    // Don't want the error message to have an antiform version as argument, as
    // they're already paying for an error regarding the state.
    //
    DECLARE_ELEMENT (reified);
    Copy_Meta_Cell(reified, anti);

    return Error_Bad_Word_Get_Raw(target, reified);
}


//
//  Error_Bad_Func_Def: C
//
Context* Error_Bad_Func_Def(const Element* spec, const Element* body)
{
    // !!! Improve this error; it's simply a direct emulation of arity-1
    // error that existed before refactoring code out of MAKE_Function().

    Array* a = Make_Array(2);
    Append_Value(a, spec);
    Append_Value(a, body);

    DECLARE_LOCAL (def);
    Init_Block(def, a);

    return Error_Bad_Func_Def_Raw(def);
}


//
//  Error_No_Arg: C
//
Context* Error_No_Arg(Option(const Symbol*) label, const Symbol* symbol)
{
    DECLARE_LOCAL (param_word);
    Init_Word(param_word, symbol);

    DECLARE_LOCAL (label_word);
    if (label)
        Init_Word(label_word, unwrap(label));
    else
        Init_Nulled(label_word);

    return Error_No_Arg_Raw(label_word, param_word);
}


//
//  Error_No_Memory: C
//
// !!! Historically, Rebol had a stack overflow error that didn't want to
// create new C function stack levels.  So the error was preallocated.  The
// same needs to apply to out of memory errors--they shouldn't be allocating
// a new error object.
//
Context* Error_No_Memory(REBLEN bytes)
{
    UNUSED(bytes);  // !!! Revisit how this information could be tunneled
    return VAL_CONTEXT(Root_No_Memory_Error);
}


//
//  Error_Not_Varargs: C
//
Context* Error_Not_Varargs(
    Level* L,
    const Key* key,
    const Param* param,
    const REBVAL *arg
){
    assert(Get_Parameter_Flag(param, VARIADIC));
    assert(not Is_Varargs(arg));

    // Since the "types accepted" are a lie (an [integer! <variadic>] takes
    // VARARGS! when fulfilled in a frame directly, not INTEGER!) then
    // an "honest" parameter has to be made to give the error.
    //
    DECLARE_LOCAL (honest_param);
    Init_Unconstrained_Parameter(
        honest_param,
        FLAG_PARAMCLASS_BYTE(PARAMCLASS_NORMAL)
            | PARAMETER_FLAG_VARIADIC
    );
    UNUSED(honest_param);  // !!! pass to Error_Arg_Type(?)

    return Error_Phase_Arg_Type(L, key, param, arg);
}


//
//  Error_Invalid_Arg: C
//
Context* Error_Invalid_Arg(Level* L, const Param* param)
{
    assert(Is_Unspecialized(c_cast(Value*, param)));

    const Param* headparam = ACT_PARAMS_HEAD(Level_Phase(L));
    assert(param >= headparam);
    assert(param <= headparam + Level_Num_Args(L));

    REBLEN index = 1 + (param - headparam);

    DECLARE_LOCAL (label);
    if (not L->label)
        Init_Nulled(label);
    else
        Init_Word(label, unwrap(L->label));

    DECLARE_LOCAL (param_name);
    Init_Word(param_name, KEY_SYMBOL(ACT_KEY(Level_Phase(L), index)));

    REBVAL *arg = Level_Arg(L, index);
    return Error_Invalid_Arg_Raw(label, param_name, arg);
}


//
//  Error_Bad_Value: C
//
// This is the very vague and generic error citing a value with no further
// commentary or context.  It becomes a catch all for "unexpected input" when
// a more specific error would often be more useful.
//
// The behavior of `fail (some_value)` generates this error, as it can be
// distinguished from `fail (some_context)` meaning that the context iss for
// an actual intended error.
//
Context* Error_Bad_Value(const Value* value)
{
    if (Is_Antiform(value))
        return Error_Bad_Antiform(value);

    return Error_Bad_Value_Raw(value);
}


//
//  Error_Bad_Null: C
//
Context* Error_Bad_Null(const Cell* target) {
    return Error_Bad_Null_Raw(target);
}


//
//  Error_No_Catch_For_Throw: C
//
Context* Error_No_Catch_For_Throw(Level* level_)
{
    DECLARE_LOCAL (label);
    Copy_Cell(label, VAL_THROWN_LABEL(level_));

    DECLARE_LOCAL (arg);
    CATCH_THROWN(arg, level_);

    if (Is_Error(label)) {  // what would have been fail()
        assert(Is_Nulled(arg));
        return VAL_CONTEXT(label);
    }

    if (Is_Antiform(label))
        Meta_Quotify(label);  // !!! Review... stops errors in molding
    if (Is_Antiform(arg))
        Meta_Quotify(arg);  // !!! Review... stops errors in molding

    return Error_No_Catch_Raw(arg, label);
}


//
//  Error_Invalid_Type: C
//
// <type> type is not allowed here.
//
Context* Error_Invalid_Type(enum Reb_Kind kind)
{
    return Error_Invalid_Type_Raw(Datatype_From_Kind(kind));
}


//
//  Error_Out_Of_Range: C
//
// Accessors like VAL_UINT8() are written to be able to extract the value
// from QUOTED! integers (used in applications like molding, where the quoted
// status is supposed to be ignored).  Dequoted_Derelativize() is defined
// after %cell-integer.h, so we handle the issue here.
//
Context* Error_Out_Of_Range(const Cell* arg)
{
    DECLARE_STABLE (unquoted);
    Dequoted_Derelativize(unquoted, arg, SPECIFIED);

    return Error_Out_Of_Range_Raw(unquoted);
}


//
//  Error_Protected_Key: C
//
Context* Error_Protected_Key(const Symbol* sym)
{
    DECLARE_LOCAL (key_name);
    Init_Word(key_name, sym);

    return Error_Protected_Word_Raw(key_name);
}


//
//  Error_Math_Args: C
//
Context* Error_Math_Args(enum Reb_Kind type, const Symbol* verb)
{
    DECLARE_LOCAL (verb_cell);
    Init_Word(verb_cell, verb);
    return Error_Not_Related_Raw(verb_cell, Datatype_From_Kind(type));
}

//
//  Error_Cannot_Use: C
//
Context* Error_Cannot_Use(const Symbol* verb, const Value* first_arg)
{
    DECLARE_LOCAL (verb_cell);
    Init_Word(verb_cell, verb);

    fail (Error_Cannot_Use_Raw(
        verb_cell,
        Datatype_From_Kind(VAL_TYPE(first_arg))
    ));
}


//
//  Error_Unexpected_Type: C
//
Context* Error_Unexpected_Type(enum Reb_Kind expected, enum Reb_Kind actual)
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
// Function in frame of `call` expected parameter `param` to be a type
// different than the arg given.
//
// !!! Right now, we do not include the arg itself in the error.  It would
// potentially lead to some big molding, and the error machinery isn't
// really equipped to handle it.
//
Context* Error_Arg_Type(
    Option(const Symbol*) name,
    const Key* key,
    const Param* param,
    const REBVAL *arg
){
    if (Cell_ParamClass(param) == PARAMCLASS_META and Is_Meta_Of_Raised(arg))
        return VAL_CONTEXT(arg);

    DECLARE_LOCAL (param_word);
    Init_Word(param_word, KEY_SYMBOL(key));

    DECLARE_LOCAL (label);
    if (name)
        Init_Word(label, unwrap(name));
    else
        Init_Nulled(label);

    DECLARE_LOCAL (spec);
    Option(const Array*) param_array = Cell_Parameter_Spec(param);
    if (param_array)
        Init_Block(spec, unwrap(param_array));
    else
        Init_Block(spec, EMPTY_ARRAY);

    return Error_Expect_Arg_Raw(
        label,
        spec,
        param_word
    );
}


//
//  Error_Phase_Arg_Type: C
//
// When RESKIN has been used, or if an ADAPT messes up a type and it isn't
// allowed by an inner phase, then it causes an error.  But it's confusing to
// say that the original function didn't take that type--it was on its
// interface.  A different message is helpful, so this does that by coercing
// the ordinary error into one making it clear it's an internal phase.
//
Context* Error_Phase_Arg_Type(
    Level* L,
    const Key* key,
    const Param* param,
    const REBVAL *arg
){
    if (Level_Phase(L) == L->u.action.original)  // not an internal phase
        return Error_Arg_Type(L->label, key, param, arg);

    if (Cell_ParamClass(param) == PARAMCLASS_META and Is_Meta_Of_Raised(arg))
        return VAL_CONTEXT(arg);

    Context* error = Error_Arg_Type(L->label, key, param, arg);
    ERROR_VARS* vars = ERR_VARS(error);
    assert(Is_Word(&vars->id));
    assert(Cell_Word_Id(&vars->id) == SYM_EXPECT_ARG);
    Init_Word(&vars->id, Canon(PHASE_EXPECT_ARG));
    return error;
}


//
//  Error_No_Logic_Typecheck: C
//
Context* Error_No_Logic_Typecheck(Option(const Symbol*) label)
{
    DECLARE_LOCAL (name);
    if (label)
        Init_Word(name, unwrap(label));
    else
        Init_Nulled(name);

    return Error_No_Logic_Typecheck_Raw(name);
}


//
//  Error_No_Arg_Typecheck: C
//
Context* Error_No_Arg_Typecheck(Option(const Symbol*) label)
{
    DECLARE_LOCAL (name);
    if (label)
        Init_Word(name, unwrap(label));
    else
        Init_Nulled(name);

    return Error_No_Arg_Typecheck_Raw(name);
}

//
//  Error_Bad_Argless_Refine: C
//
// Refinements that take no arguments can only be # or NULL as far as DO FRAME!
// is concerned.  (Some higher level mechanisms like APPLY will editorialize
// and translate true => # and false => NULL, but the core mechanics don't.)
//
Context* Error_Bad_Argless_Refine(const Key* key)
{
    DECLARE_LOCAL (word);
    Refinify(Init_Word(word, KEY_SYMBOL(key)));
    return Error_Bad_Argless_Refine_Raw(word);
}


//
//  Error_Bad_Return_Type: C
//
Context* Error_Bad_Return_Type(Level* L, Atom* atom) {
    DECLARE_STABLE (label);
    Get_Level_Label_Or_Nulled(label, L);

    if (Is_Void(atom))  // void's "kind" is null, no type (good idea?)
        return Error_Bad_Void_Return_Raw(label);

    if (Is_Pack(atom) and Is_Pack_Undecayable(atom))
        return Error_User("Bad return pack (undecayable elements)");

    enum Reb_Kind kind = VAL_TYPE(atom);
    return Error_Bad_Return_Type_Raw(label, Datatype_From_Kind(kind));
}


//
//  Error_Bad_Make: C
//
Context* Error_Bad_Make(enum Reb_Kind type, const Cell* spec)
{
    return Error_Bad_Make_Arg_Raw(Datatype_From_Kind(type), spec);
}


//
//  Error_Bad_Make_Parent: C
//
Context* Error_Bad_Make_Parent(enum Reb_Kind type, const Cell* parent)
{
    assert(parent != nullptr);
    return Error_Bad_Make_Parent_Raw(Datatype_From_Kind(type), parent);
}


//
//  Error_Cannot_Reflect: C
//
Context* Error_Cannot_Reflect(enum Reb_Kind type, const REBVAL *arg)
{
    return Error_Cannot_Use_Raw(arg, Datatype_From_Kind(type));
}


//
//  Error_On_Port: C
//
Context* Error_On_Port(SymId id, REBVAL *port, REBINT err_code)
{
    FAIL_IF_BAD_PORT(port);

    Context* ctx = VAL_CONTEXT(port);
    REBVAL *spec = CTX_VAR(ctx, STD_PORT_SPEC);

    REBVAL *val = CTX_VAR(VAL_CONTEXT(spec), STD_PORT_SPEC_HEAD_REF);
    if (Is_Blank(val))
        val = CTX_VAR(VAL_CONTEXT(spec), STD_PORT_SPEC_HEAD_TITLE);  // less

    DECLARE_LOCAL (err_code_value);
    Init_Integer(err_code_value, err_code);

    return Error(SYM_ACCESS, id, val, err_code_value, rebEND);
}


//
//  Error_Bad_Antiform: C
//
Context* Error_Bad_Antiform(const Atom* anti) {
    assert(Is_Antiform(anti));

    DECLARE_ELEMENT (reified);
    Copy_Meta_Cell(reified, anti);

    return Error_Bad_Antiform_Raw(reified);
}


//
//  Error_Bad_Void: C
//
Context* Error_Bad_Void(void) {
    return Error_Bad_Void_Raw();
}


//
//  Startup_Errors: C
//
// Create error objects and error type objects
//
Context* Startup_Errors(const Element* boot_errors)
{
  #if DEBUG_HAS_PROBE
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

    const Element* errors_tail;
    Element* errors_head
        = Cell_Array_At_Known_Mutable(&errors_tail, boot_errors);

    assert(VAL_INDEX(boot_errors) == 0);
    Context* catalog = Construct_Context_Managed(
        REB_OBJECT,
        errors_head,  // modifies bindings
        errors_tail,
        Cell_Specifier(boot_errors),
        nullptr
    );

    // Morph blocks into objects for all error categories.
    //
    const Element* category_tail = Array_Tail(CTX_VARLIST(catalog));
    REBVAL *category = CTX_VARS_HEAD(catalog);
    for (; category != category_tail; ++category) {
        const Element* tail;
        Element* head = Cell_Array_At_Known_Mutable(&tail, category);
        Context* error = Construct_Context_Managed(
            REB_OBJECT,
            head,  // modifies bindings
            tail,
            SPECIFIED, // source array not in a function body
            nullptr
        );
        Init_Object(category, error);
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

    // !!! The original "No memory" error let you supply the size of the
    // request that could not be fulfilled.  But if you are creating a new
    // out of memory error with that identity, you need to do an allocation...
    // and out of memory errors can't work this way.  It may be that the
    // error is generated after the stack is unwound and memory freed up.
    //
    DECLARE_LOCAL (temp);
    Init_Integer(temp, 1020);

    Root_No_Memory_Error = Init_Error(
        Alloc_Value(),
        Error_No_Memory_Raw(temp)
    );
}


//
//  Shutdown_Stackoverflow: C
//
void Shutdown_Stackoverflow(void)
{
    rebRelease(Root_Stackoverflow_Error);
    Root_Stackoverflow_Error = nullptr;

    rebRelease(Root_No_Memory_Error);
    Root_No_Memory_Error = nullptr;
}


// !!! Though molding has a general facility for a "limit" of the overall
// mold length, this only limits the length a particular value can contribute
// to the mold.  It was only used in error molding and was kept working
// without a general review of such a facility.  Review.
//
static void Mold_Value_Limit(REB_MOLD *mo, Element* v, REBLEN limit)
{
    String* str = mo->series;

    REBLEN start_len = String_Len(str);
    Size start_size = String_Size(str);

    Mold_Value(mo, v);  // Note: can't cache pointer into `str` across this

    REBLEN end_len = String_Len(str);

    if (end_len - start_len > limit) {
        Utf8(const*) at = cast(Utf8(const*),
            c_cast(Byte*, String_Head(str)) + start_size
        );
        REBLEN n = 0;
        for (; n < limit; ++n)
            at = Skip_Codepoint(at);

        Term_String_Len_Size(str, start_len + limit, at - String_Head(str));
        Free_Bookmarks_Maybe_Null(str);

        Append_Ascii(str, "...");
    }
}


//
//  MF_Error: C
//
void MF_Error(REB_MOLD *mo, const Cell* v, bool form)
{
    // Protect against recursion. !!!!
    //
    if (not form) {
        MF_Context(mo, v, false);
        return;
    }

    Context* error = VAL_CONTEXT(v);
    ERROR_VARS *vars = ERR_VARS(error);

    // Form: ** <type> Error:
    //
    Append_Ascii(mo->series, "** ");
    if (Is_Word(&vars->type)) {  // has a <type>
        Append_Spelling(mo->series, Cell_Word_Symbol(&vars->type));
        Append_Codepoint(mo->series, ' ');
    }
    else
        assert(Is_Nulled(&vars->type));  // no <type>
    Append_Ascii(mo->series, RM_ERROR_LABEL);  // "Error:"

    // Append: error message ARG1, ARG2, etc.
    if (Is_Block(&vars->message))
        Form_Array_At(mo, Cell_Array(&vars->message), 0, error);
    else if (Is_Text(&vars->message))
        Form_Value(mo, cast(Element*, &vars->message));
    else
        Append_Ascii(mo->series, RM_BAD_ERROR_FORMAT);

    // Form: ** Where: function
    Value* where = &vars->where;
    if (
        not Is_Nulled(where)
        and not (Is_Block(where) and Cell_Series_Len_At(where) == 0)
    ){
        if (Is_Block(where)) {
            Append_Codepoint(mo->series, '\n');
            Append_Ascii(mo->series, RM_ERROR_WHERE);
            Form_Value(mo, cast(Element*, where));
        }
        else
            Append_Ascii(mo->series, RM_BAD_ERROR_FORMAT);
    }

    // Form: ** Near: location
    Value* nearest = &vars->nearest;
    if (not Is_Nulled(nearest)) {
        Append_Codepoint(mo->series, '\n');
        Append_Ascii(mo->series, RM_ERROR_NEAR);

        if (Is_Text(nearest)) {
            //
            // !!! The scanner puts strings into the near information in order
            // to say where the file and line of the scan problem was.  This
            // seems better expressed as an explicit argument to the scanner
            // error, because otherwise it obscures the LOAD call where the
            // scanner was invoked.  Review.
            //
            Append_String(mo->series, nearest);
        }
        else if (Any_Array(nearest) or Any_Path(nearest))
            Mold_Value_Limit(mo, cast(Element*, nearest), 60);
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
    Value* file = &vars->file;
    if (not Is_Nulled(file)) {
        Append_Codepoint(mo->series, '\n');
        Append_Ascii(mo->series, RM_ERROR_FILE);
        if (Is_File(file))
            Form_Value(mo, cast(Element*, file));
        else
            Append_Ascii(mo->series, RM_BAD_ERROR_FORMAT);
    }

    // Form: ** Line: line-number
    Value* line = &vars->line;
    if (not Is_Nulled(line)) {
        Append_Codepoint(mo->series, '\n');
        Append_Ascii(mo->series, RM_ERROR_LINE);
        if (Is_Integer(line))
            Form_Value(mo, cast(Element*, line));
        else
            Append_Ascii(mo->series, RM_BAD_ERROR_FORMAT);
    }
}
