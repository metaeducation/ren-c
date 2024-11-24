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
//  Derive_Error_From_Pointer_Core: C
//
// This is the polymorphic code behind fail(), FAIL(), and RAISE():
//
//    fail ("UTF-8 string");  // delivers error with that text
//    fail (api_value);       // ensure it's an ERROR!, release and use as-is
//    fail (error_context);   // use the Error* as-is
//    fail (PARAM(name));     // impliciate parameter as having a bad value
//    fail (other_cell);      // just report as a generic "bad value"
//
// 1. We would face an ambiguity in taking API handles, as to whether that
//    is an error, or if it is "some value" that is just a bad value.  Since
//    internal code that would use this function does not deal often in
//    API values, it's believed that assuming they are errors when passed
//    to `fail()` or `FAIL()` or `RAISE()` is the best policy.
//
// 2. We check to see if the Cell is in the paramlist of the current running
//    native.  (We could theoretically do this with ARG(), or have a nuance of
//    behavior with ARG()...or even for the Key*...but failing on the PARAM()
//    feels like the best way to "blame" that argument.)
//
Error* Derive_Error_From_Pointer_Core(const void* p) {
    if (p == nullptr)
        return Error_Unknown_Error_Raw();

    switch (Detect_Rebol_Pointer(p)) {
      case DETECTED_AS_UTF8:
        return Error_User(c_cast(char*, p));

      case DETECTED_AS_STUB: {
        Flex* f = m_cast(Flex*, c_cast(Flex* , p));  // don't mutate
        if (not Is_Stub_Varlist(f))
            panic (f);  // only kind of Flex allowed are contexts of ERROR!
        if (CTX_TYPE(cast(VarList*, f)) != REB_ERROR)
            panic (f);
        return cast(Error*, f); }

      case DETECTED_AS_CELL: {
        const Atom* atom = c_cast(Atom*, p);
        assert(Is_Stable(atom));  // !!! Should unstable args be allowed?
        UNUSED(atom);

        const Value* v = c_cast(Value*, p);

        if (Is_Node_Root_Bit_Set(v)) {  // API handles must be errors [1]
            Error* error;
            if (Is_Error(v)) {
                error = Cell_Error(v);
            }
            else {
                assert(!"fail() given API handle that is not an ERROR!");
                error = Error_Bad_Value(v);
            }
            rebRelease(m_cast(Value*, v));  // released even if we didn't
            return error;
        }

        if (not Is_Action_Level(TOP_LEVEL))
            return Error_Bad_Value(v);

        const Param* head = ACT_PARAMS_HEAD(Level_Phase(TOP_LEVEL));
        REBLEN num_params = ACT_NUM_PARAMS(Level_Phase(TOP_LEVEL));

        if (v >= head and v < head + num_params) {  // PARAM() error [2]
            const Param* param = cast_PAR(c_cast(Value*, v));
            return Error_Invalid_Arg(TOP_LEVEL, param);
        }
        return Error_Bad_Value(v); }

      default:
        break;
    }
    panic (p);
}


//
//  Fail_Abruptly_Helper: C
//
// Trigger failure of an error by longjmp'ing to enclosing RESCUE_SCOPE.  Note
// that these failures interrupt code mid-stream, so if a Rebol function is
// running it will not make it to the point of returning the result value.
// This distinguishes the "fail" mechanic from the "throw" mechanic, which has
// to bubble up a thrown value through OUT (used to implement BREAK,
// CONTINUE, RETURN, LEAVE, HALT...)
//
// The function will auto-detect if the pointer it is given is an ERROR!'s
// VarList* or a UTF-8 char *.  If it's UTF-8, an error will be created from
// it automatically (but with no ID...the string becomes the "ID")
//
// If the pointer is to a function parameter of the current native (e.g. what
// you get for PARAM(name) in a native), then it will report both the
// parameter name and value as being implicated as a problem.  This only
// works for the current topmost stack level.
//
// Passing an arbitrary Value* will give a generic "Invalid Arg" error.
//
// Note: Over the long term, one does not want to hard-code error strings in
// the executable.  That makes them more difficult to hook with translations,
// or to identify systemically with some kind of "error code".  However,
// it's a realistic quick-and-dirty way of delivering a more meaningful
// error than just using a RE_MISC error code, and can be found just as easily
// to clean up later with a textual search for `fail ("`
//
Error* Fail_Abruptly_Helper(Error* error)
{
    Assert_Varlist(error);
    assert(CTX_TYPE(error) == REB_ERROR);

    // You can't abruptly fail during the handling of abrupt failure.
    //
    assert(not (Is_Throwing(TOP_LEVEL) and Is_Throwing_Failure(TOP_LEVEL)));

    // The topmost level must be the one issuing the error.  If a level was
    // pushed with LEVEL_FLAG_TRAMPOLINE_KEEPALIVE that finished executing
    // but remained pushed, it must be dropped before the level that pushes
    // it issues a failure.
    //
    assert(TOP_LEVEL->executor != nullptr);

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
    if (
        error != Cell_Varlist(g_error_no_memory)
        and error != Cell_Varlist(g_error_stack_overflow)  // e.g. bad PUSH()
    ){
        Force_Location_Of_Error(error, TOP_LEVEL);  // needs PUSH(), etc.
    }

  #if DEBUG_HAS_PROBE
    if (PG_Probe_Failures) {  // see R3_PROBE_FAILURES environment variable
        static bool probing = false;

        if (error == cast(void*, Cell_Varlist(g_error_stack_overflow))) {
            printf("PROBE(Stack Overflow): mold in PROBE would recurse\n");
            fflush(stdout);
        }
        else if (probing) {
            printf("PROBE(Recursing): recursing for unknown reason\n");
            panic (error);
        }
        else {
            probing = true;
            PROBE(error);
            probing = false;
        }
    }
  #endif

    // If we raise the error we'll lose the stack, and if it's an early
    // error we always want to see it (do not use RESCUE on purpose in
    // Startup_Core()...)
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

    return error;
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
const Value* Find_Error_For_Sym(SymId id)
{
    const Symbol* canon = Canon_Symbol(id);

    VarList* categories = Cell_Varlist(Get_System(SYS_CATALOG, CAT_ERRORS));

    Index ncat = 1;
    for (; ncat <= Varlist_Len(categories); ++ncat) {
        VarList* category = Cell_Varlist(Varlist_Slot(categories, ncat));

        Index n = 1;
        for (; n <= Varlist_Len(category); ++n) {
            if (Are_Synonyms(Key_Symbol(Varlist_Key(category, n)), canon)) {
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
// depth of a running level.
//
// (We could have RUNTIME_CHECKS builds capture the C __FILE__ and __LINE__
// of origin as fields in the variable.  But rather than implement that idea,
// the cheaper DEBUG_PRINTF_FAIL_LOCATIONS was added, which works well enough
// if you're not running under a C debugger.)
//
// 1. Intrinsic natives are very limited in what they are allowed to do (when
//    they are executing as an intrinsic, e.g. they have only one argument
//    that lives in their parent Level's SPARE).  But FAIL is one of the
//    things they should be able to do, and we need to know what's failing...
//    so we can't implicate the parent.
//
// 2. The WHERE is a backtrace of a block of words, starting from the top of
//    the stack and going downwards.  If a label is not available for a level,
//    we could omit it (which would be deceptive) or we could put ~anonymous~
//    there.  The lighter tidle of trash (~) is a fairly slight choice, but
//    so far it has seemed to be less abrasive while still useful.
//
// 3. A Level that is in the process of gathering its arguments isn't running
//    its own code yet.  So it's kind of important to distinguish it in the
//    stack trace to make that fact clear.  (For a time it was not listed
//    at all, but that wasn't as informative.)  Putting it inside a FENCE!
//    is generalized, so that if Level labels could ever be TUPLE! it would
//    still be possible to do it.
//
// 4. !!! Review: The "near" information is used in things like the scanner
//    missing a closing quote mark, and pointing to the source code (not
//    the implementation of LOAD).  We don't want to override that or we
//    would lose the message.  But we still want the stack of where the
//    LOAD was being called in the "where".  For the moment don't overwrite
//    any existing near, but a less-random design is needed here.
//
// 5. For the file and line of the error, we look at SOURCE-flavored arrays,
//    which have SOURCE_FLAG_HAS_FILE_LINE...which either was put on at
//    the time of scanning, or derived when the code is running based on
//    whatever information was on a running array.
//
//    But we currently skip any calls from C (e.g. rebValue()).  Though
//    rebValue() might someday accept reb__FILE__() and reb__LINE__(),
//    instructions, which could let us implicate C source here.
//
void Set_Location_Of_Error(
    Error* error,
    Level* where  // must be valid and executing on the stack
) {
    StackIndex base = TOP_INDEX;

    ERROR_VARS *vars = ERR_VARS(error);

    Level* L = where;
    for (; L != BOTTOM_LEVEL; L = L->prior) {
        if (Get_Level_Flag(L, DISPATCHING_INTRINSIC)) {  // [1]
            Option(const Symbol*) label = VAL_FRAME_LABEL(Level_Scratch(L));
            if (label)
                Init_Word(PUSH(), unwrap label);
            else
                Init_Trash(PUSH());  // less space than ~ANYONYMOUS~ [2]
            continue;
        }

        if (not Is_Action_Level(L))
            continue;

        PUSH();
        if (not Try_Get_Action_Level_Label(TOP, L))
            Init_Trash(TOP);  // [2]

        if (Is_Level_Fulfilling(L)) { // differentiate fulfilling levels [3]
            Source* a = Alloc_Singular(FLAG_FLAVOR(SOURCE) | NODE_FLAG_MANAGED);
            Move_Cell(Stub_Cell(a), TOP);
            Init_Fence(TOP, a);
            continue;
        }
    }
    Init_Block(&vars->where, Pop_Source_From_Stack(base));

    if (Is_Nulled(&vars->nearest))  // don't override scanner data [4]
        Init_Near_For_Level(&vars->nearest, where);

    L = where;
    for (; L != BOTTOM_LEVEL; L = L->prior) {
        if (Level_Is_Variadic(L)) {  // could rebValue() have file/line? [5]
            continue;
        }
        if (Not_Source_Flag(Level_Array(L), HAS_FILE_LINE))
            continue;
        break;
    }

    if (L != BOTTOM_LEVEL) {  // found a level with file and line information
        Option(const String*) file = LINK(Filename, Level_Array(L));
        LineNumber line = Level_Array(L)->misc.line;

        if (file)
            Init_File(&vars->file, unwrap file);
        if (line != 0)
            Init_Integer(&vars->line, line);
    }
}


//
// Makehook_Error: C
//
// Hook for MAKE ERROR! (distinct from MAKE for ANY-CONTEXT?, due to %types.r)
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
Bounce Makehook_Error(Level* level_, Heart heart, Element* arg) {
    assert(heart == REB_ERROR);
    UNUSED(heart);

    // Frame from the error object template defined in %sysobj.r
    //
    VarList* root_error = Cell_Varlist(Get_System(SYS_STANDARD, STD_ERROR));

    VarList* error;
    ERROR_VARS *vars; // C struct mirroring fixed portion of error fields

    if (Is_Block(arg)) {
        // If a block, then effectively MAKE OBJECT! on it.  Afterward,
        // apply the same logic as if an OBJECT! had been passed in above.

        // Bind and do an evaluation step (as with MAKE OBJECT! with A_MAKE
        // code in DECLARE_GENERICS(Context) and code in DECLARE_NATIVE(construct))

        const Element* tail;
        const Element* head = Cell_List_At(&tail, arg);

        error = Make_Varlist_Detect_Managed(
            COLLECT_ONLY_SET_WORDS,
            REB_ERROR, // type
            head, // values to scan for toplevel set-words
            tail,
            root_error // parent
        );

        // Protect the error from GC by putting into out, which must be
        // passed in as a GC-protecting value slot.
        //
        Init_Error(OUT, error);

        BINDING(arg) = Make_Use_Core(
            Varlist_Archetype(error),
            Cell_List_Binding(arg),
            CELL_MASK_ERASED_0
        );

        DECLARE_ATOM (evaluated);
        if (Eval_Any_List_At_Throws(evaluated, arg, SPECIFIED))
            return BOUNCE_THROWN;

        vars = ERR_VARS(error);
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

        error = Copy_Varlist_Shallow_Managed(root_error);
        Init_Error(OUT, error);

        vars = ERR_VARS(error);
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

        VarList* categories = Cell_Varlist(Get_System(SYS_CATALOG, CAT_ERRORS));

        // Find correct category for TYPE: (if any)
        Option(Value*) category = Select_Symbol_In_Context(
            Varlist_Archetype(categories),
            Cell_Word_Symbol(&vars->type)
        );

        if (category) {
            assert(Is_Object(unwrap category));

            // Find correct message for ID: (if any)

            Option(Value*) message = Select_Symbol_In_Context(
                unwrap category,
                Cell_Word_Symbol(&vars->id)
            );

            if (message) {
                assert(Is_Text(unwrap message) or Is_Block(unwrap message));

                if (not Is_Nulled(&vars->message))
                    return RAISE(Error_Invalid_Error_Raw(arg));

                Copy_Cell(&vars->message, unwrap message);
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

                return RAISE(
                    Error_Invalid_Error_Raw(Varlist_Archetype(error))
                );
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
            return FAIL(Error_Invalid_Error_Raw(Varlist_Archetype(error)));
        }
    }

    assert(Is_Error(OUT));
    return OUT;
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
    SymId cat_id,
    SymId id,
    va_list* vaptr
){
    if (PG_Boot_Phase < BOOT_ERRORS) { // no STD_ERROR or template table yet
      #if RUNTIME_CHECKS
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

    VarList* root_varlist = Cell_Varlist(Get_System(SYS_STANDARD, STD_ERROR));

    DECLARE_VALUE (id_value);
    DECLARE_VALUE (type);
    const Value* message;  // Stack values ("movable") are allowed
    if (cat_id == SYM_0 and id == SYM_0) {
        Init_Nulled(id_value);
        Init_Nulled(type);
        message = va_arg(*vaptr, const Value*);
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
        const Element* temp = Cell_List_At(&tail, message);
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
    VarList* varlist = Copy_Varlist_Extra_Managed(
        root_varlist,
        expected_args,  // Note: won't make new keylist if expected_args is 0
        deeply
    );

    // Arrays from errors.r look like `["The value" :arg1 "is not" :arg2]`
    // They can also be a single TEXT! (which will just bypass this loop).
    //
    if (not Is_Text(message)) {
        const Element* msg_tail;
        const Element* msg_item = Cell_List_At(&msg_tail, message);

        for (; msg_item != msg_tail; ++msg_item) {
            if (not Is_Get_Word(msg_item))
                continue;

            const Symbol* symbol = Cell_Word_Symbol(msg_item);
            Value* var = Append_Context(varlist, symbol);

            const void *p = va_arg(*vaptr, const void*);

            if (p == nullptr) {
                //
                // !!! Should variadic error take `nullptr` instead of
                // "nulled cells"?
                //
                assert(!"nullptr passed to Make_Error_Managed_Vaptr()");
                Init_Nulled(var);
            }
            else switch (Detect_Rebol_Pointer(p)) {
              case DETECTED_AS_END :
                assert(!"Not enough arguments in Make_Error_Managed()");
                Init_Anti_Word(var, CANON(END));
                break;

              case DETECTED_AS_CELL : {
                Copy_Cell(var, c_cast(Value*, p));
                break; }

              default:
                assert(false);
                fail ("Bad pointer passed to Make_Error_Managed()");
            }
        }
    }

    assert(Varlist_Len(varlist) == Varlist_Len(root_varlist) + expected_args);

    HEART_BYTE(Rootvar_Of_Varlist(varlist)) = REB_ERROR;

    // C struct mirroring fixed portion of error fields
    //
    ERROR_VARS *vars = ERR_VARS(varlist);

    Copy_Cell(&vars->message, message);
    Copy_Cell(&vars->id, id_value);
    Copy_Cell(&vars->type, type);

    return cast(Error*, varlist);
}


//
//  Make_Error_Managed: C
//
// This variadic function takes a number of Value* arguments appropriate for
// the error category and ID passed.  It is commonly used with fail():
//
//     fail (Make_Error_Managed(SYM_CATEGORY, SYM_SOMETHING, arg1, arg2, ...));
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
    int cat_id,
    int id, // can't be SymId, see note below
    ... /* Value* arg1, Value* arg2, ... */
){
    va_list va;

    // Note: if id is SymId, triggers: "passing an object that undergoes
    // default argument promotion to 'va_start' has undefined behavior"
    //
    va_start(va, id);

    Error* error = Make_Error_Managed_Vaptr(
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
Error* Error_User(const char *utf8) {
    DECLARE_ATOM (message);
    Init_Text(message, Make_String_UTF8(utf8));
    return Make_Error_Managed(SYM_0, SYM_0, message, rebEND);
}


//
//  Error_Need_Non_End: C
//
// This error was originally just for SET-WORD!, but now it's used by sigils
// which are trying to operate on their right hand sides.
//
// So the message was changed to "error while evaluating VAR:" instead of
// "error while setting VAR:", so "error while evaluating @" etc. make sense.
//
Error* Error_Need_Non_End(const Element* target) {
    assert(
        Is_Sigil(target)  // ^ needs things on the right
        or Is_Meta_Of_Void(target)
        or Any_Word(target) or Any_Tuple(target)
    );
    return Error_Need_Non_End_Raw(target);
}


//
//  Error_Bad_Word_Get: C
//
// 1. Don't want the error message to have an antiform version as argument, as
//    they're already paying for an error regarding the state.
//
Error* Error_Bad_Word_Get(
    const Element* target,
    const Atom* vacancy
){
    assert(Any_Vacancy(vacancy));

    DECLARE_ELEMENT (reified);
    Copy_Meta_Cell(reified, vacancy);  // avoid failures in error message [1]

    return Error_Bad_Word_Get_Raw(target, reified);
}


//
//  Error_Bad_Func_Def: C
//
Error* Error_Bad_Func_Def(const Element* spec, const Element* body)
{
    // !!! Improve this error; it's simply a direct emulation of arity-1
    // error that existed before refactoring code out of Make_Function().

    Source* a = Make_Source_Managed(2);
    Append_Value(a, spec);
    Append_Value(a, body);

    DECLARE_ELEMENT (def);
    Init_Block(def, a);

    return Error_Bad_Func_Def_Raw(def);
}


//
//  Error_No_Arg: C
//
Error* Error_No_Arg(Option(const Symbol*) label, const Symbol* symbol)
{
    DECLARE_ELEMENT (param_word);
    Init_Word(param_word, symbol);

    DECLARE_ELEMENT (label_word);
    if (label)
        Init_Word(label_word, unwrap label);
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
Error* Error_No_Memory(REBLEN bytes)
{
    UNUSED(bytes);  // !!! Revisit how this information could be tunneled
    return Cell_Error(g_error_no_memory);
}


//
//  Error_Not_Varargs: C
//
Error* Error_Not_Varargs(
    Level* L,
    const Key* key,
    const Param* param,
    const Value* arg
){
    assert(Get_Parameter_Flag(param, VARIADIC));
    assert(not Is_Varargs(arg));

    // Since the "types accepted" are a lie (an [integer! <variadic>] takes
    // VARARGS! when fulfilled in a frame directly, not INTEGER!) then
    // an "honest" parameter has to be made to give the error.
    //
    DECLARE_ATOM (honest_param);
    Init_Unconstrained_Hole(
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
Error* Error_Invalid_Arg(Level* L, const Param* param)
{
    assert(Is_Hole(c_cast(Value*, param)));

    const Param* headparam = ACT_PARAMS_HEAD(Level_Phase(L));
    assert(param >= headparam);
    assert(param <= headparam + Level_Num_Args(L));

    REBLEN index = 1 + (param - headparam);

    DECLARE_ATOM (label);
    if (not Try_Get_Action_Level_Label(label, L))
        Init_Word(label, CANON(ANONYMOUS));

    DECLARE_ATOM (param_name);
    Init_Word(param_name, Key_Symbol(ACT_KEY(Level_Phase(L), index)));

    Value* arg = Level_Arg(L, index);
    return Error_Invalid_Arg_Raw(label, param_name, arg);
}


//
//  Error_Bad_Intrinsic_Arg_1: C
//
// 1. See DETAILS_FLAG_CAN_DISPATCH_AS_INTRINSIC for why a non-intrinsic
//    dispatch doesn't defer typechecking and reuse the "fast" work of
//    the intrinsic mode.
//
Error* Error_Bad_Intrinsic_Arg_1(Level* const L)
{
    assert(Get_Level_Flag(L, DISPATCHING_INTRINSIC));  // valid otherwise [1]

    USE_LEVEL_SHORTHANDS (L);

    Action* action;
    Value* arg;
    DECLARE_ATOM (label);

    if (Get_Level_Flag(L, DISPATCHING_INTRINSIC)) {
        action = VAL_ACTION(SCRATCH);
        arg = stable_SPARE;
        Option(const Symbol*) symbol = VAL_FRAME_LABEL(SCRATCH);
        if (symbol)
            Init_Word(label, unwrap symbol);
        else
            Init_Word(label, CANON(ANONYMOUS));
    }
    else {
        action = Level_Phase(L);
        arg = Level_Arg(L, 2);
        if (not Try_Get_Action_Level_Label(label, L))
            Init_Word(label, CANON(ANONYMOUS));
    }

    Param* param = ACT_PARAM(action, 2);
    assert(Is_Hole(c_cast(Value*, param)));
    UNUSED(param);

    DECLARE_ATOM (param_name);
    Init_Word(param_name, Key_Symbol(ACT_KEY(action, 2)));

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
Error* Error_Bad_Value(const Value* value)
{
    if (Is_Antiform(value))
        return Error_Bad_Antiform(value);

    return Error_Bad_Value_Raw(value);
}


//
//  Error_Bad_Null: C
//
Error* Error_Bad_Null(const Cell* target) {
    return Error_Bad_Null_Raw(target);
}


//
//  Error_No_Catch_For_Throw: C
//
Error* Error_No_Catch_For_Throw(Level* level_)
{
    DECLARE_ATOM (label);
    Copy_Cell(label, VAL_THROWN_LABEL(level_));

    DECLARE_ATOM (arg);
    CATCH_THROWN(arg, level_);

    if (Is_Error(label)) {  // what would have been fail()
        assert(Is_Nulled(arg));
        return Cell_Error(label);
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
Error* Error_Invalid_Type(Kind kind)
{
    return Error_Invalid_Type_Raw(Datatype_From_Kind(kind));
}


//
//  Error_Out_Of_Range: C
//
// Accessors like VAL_UINT8() are written to be able to extract the value
// from QUOTED? integers (used in applications like molding, where the quoted
// status is supposed to be ignored).  Copy_Dequoted_Cell() is defined
// after %cell-integer.h, so we handle the issue here.
//
Error* Error_Out_Of_Range(const Cell* arg)
{
    DECLARE_ELEMENT (unquoted);
    Copy_Dequoted_Cell(unquoted, arg);

    return Error_Out_Of_Range_Raw(unquoted);
}


//
//  Error_Protected_Key: C
//
Error* Error_Protected_Key(const Symbol* sym)
{
    DECLARE_ELEMENT (key_name);
    Init_Word(key_name, sym);

    return Error_Protected_Word_Raw(key_name);
}


//
//  Error_Math_Args: C
//
Error* Error_Math_Args(Kind type, const Symbol* verb)
{
    DECLARE_ATOM (verb_cell);
    Init_Word(verb_cell, verb);
    return Error_Not_Related_Raw(verb_cell, Datatype_From_Kind(type));
}

//
//  Error_Cannot_Use: C
//
Error* Error_Cannot_Use(const Symbol* verb, const Value* first_arg)
{
    DECLARE_ATOM (verb_cell);
    Init_Word(verb_cell, verb);

    fail (Error_Cannot_Use_Raw(
        verb_cell,
        Datatype_From_Kind(VAL_TYPE(first_arg))
    ));
}


//
//  Error_Unexpected_Type: C
//
Error* Error_Unexpected_Type(Kind expected, Kind actual)
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
Error* Error_Arg_Type(
    Option(const Symbol*) name,
    const Key* key,
    const Param* param,
    const Value* arg
){
    if (Cell_ParamClass(param) == PARAMCLASS_META and Is_Meta_Of_Raised(arg))
        return Cell_Error(arg);

    DECLARE_ELEMENT (param_word);
    Init_Word(param_word, Key_Symbol(key));

    DECLARE_VALUE (label);
    if (name)
        Init_Word(label, unwrap name);
    else
        Init_Nulled(label);

    DECLARE_ELEMENT (spec);
    Option(const Source*) param_array = Cell_Parameter_Spec(param);
    if (param_array)
        Init_Block(spec, unwrap param_array);
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
Error* Error_Phase_Arg_Type(
    Level* L,
    const Key* key,
    const Param* param,
    const Value* arg
){
    if (Level_Phase(L) == L->u.action.original)  // not an internal phase
        return Error_Arg_Type(Level_Label(L), key, param, arg);

    if (Cell_ParamClass(param) == PARAMCLASS_META and Is_Meta_Of_Raised(arg))
        return Cell_Error(arg);

    Error* error = Error_Arg_Type(Level_Label(L), key, param, arg);
    ERROR_VARS* vars = ERR_VARS(error);
    assert(Is_Word(&vars->id));
    assert(Cell_Word_Id(&vars->id) == SYM_EXPECT_ARG);
    Init_Word(&vars->id, CANON(PHASE_EXPECT_ARG));
    return error;
}


//
//  Error_No_Logic_Typecheck: C
//
Error* Error_No_Logic_Typecheck(Option(const Symbol*) label)
{
    DECLARE_ATOM (name);
    if (label)
        Init_Word(name, unwrap label);
    else
        Init_Nulled(name);

    return Error_No_Logic_Typecheck_Raw(name);
}


//
//  Error_No_Arg_Typecheck: C
//
Error* Error_No_Arg_Typecheck(Option(const Symbol*) label)
{
    DECLARE_ATOM (name);
    if (label)
        Init_Word(name, unwrap label);
    else
        Init_Nulled(name);

    return Error_No_Arg_Typecheck_Raw(name);
}

//
//  Error_Bad_Argless_Refine: C
//
// Refinements that take no arguments can only be # or NULL as EVAL FRAME!
// is concerned.  (Some higher level mechanisms like APPLY will editorialize
// and translate true => # and false => NULL, but the core mechanics don't.)
//
Error* Error_Bad_Argless_Refine(const Key* key)
{
    DECLARE_ELEMENT (word);
    Refinify(Init_Word(word, Key_Symbol(key)));
    return Error_Bad_Argless_Refine_Raw(word);
}


//
//  Error_Bad_Return_Type: C
//
Error* Error_Bad_Return_Type(Level* L, Atom* atom) {
    DECLARE_VALUE (label);
    if (not Try_Get_Action_Level_Label(label, L))
        Init_Nulled(label);

    if (Is_Void(atom))  // void's "kind" is null, no type (good idea?)
        return Error_Bad_Void_Return_Raw(label);

    if (Is_Pack(atom) and Is_Pack_Undecayable(atom))
        return Error_User("Bad return pack (undecayable elements)");

    Kind kind = VAL_TYPE(atom);
    return Error_Bad_Return_Type_Raw(label, Datatype_From_Kind(kind));
}


//
//  Error_Bad_Make: C
//
Error* Error_Bad_Make(Kind type, const Cell* spec)
{
    return Error_Bad_Make_Arg_Raw(Datatype_From_Kind(type), spec);
}


//
//  Error_Cannot_Reflect: C
//
Error* Error_Cannot_Reflect(Kind type, const Value* arg)
{
    return Error_Cannot_Use_Raw(arg, Datatype_From_Kind(type));
}


//
//  Error_On_Port: C
//
Error* Error_On_Port(SymId id, Value* port, REBINT err_code)
{
    FAIL_IF_BAD_PORT(port);

    VarList* ctx = Cell_Varlist(port);
    Value* spec = Varlist_Slot(ctx, STD_PORT_SPEC);

    Value* val = Varlist_Slot(Cell_Varlist(spec), STD_PORT_SPEC_HEAD_REF);
    if (Is_Blank(val))
        val = Varlist_Slot(Cell_Varlist(spec), STD_PORT_SPEC_HEAD_TITLE);  // less

    DECLARE_ATOM (err_code_value);
    Init_Integer(err_code_value, err_code);

    return Make_Error_Managed(SYM_ACCESS, id, val, err_code_value, rebEND);
}


//
//  Error_Bad_Antiform: C
//
Error* Error_Bad_Antiform(const Atom* anti) {
    assert(Is_Antiform(anti));

    DECLARE_ELEMENT (reified);
    Copy_Meta_Cell(reified, anti);

    return Error_Bad_Antiform_Raw(reified);
}


//
//  Error_Bad_Void: C
//
Error* Error_Bad_Void(void) {
    return Error_Bad_Void_Raw();
}


//
//  Error_Unhandled: C
//
Error* Error_Unhandled(Level* level_, const Symbol* verb) {
    switch (Symbol_Id(verb)) {
      case SYM_AS:  // distinct error..?
      case SYM_TO: {
        INCLUDE_PARAMS_OF_TO;
        return Error_Bad_Cast_Raw(ARG(element), ARG(type)); }

      default:
        break;
    }

    return Error_Cannot_Use(verb, ARG_N(1));
}

//
//  Startup_Errors: C
//
// Create error objects and error type objects
//
VarList* Startup_Errors(const Element* boot_errors)
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

    assert(VAL_INDEX(boot_errors) == 0);

    Value* catalog_val = rebValue(CANON(CONSTRUCT), CANON(INERT), boot_errors);
    VarList* catalog = Cell_Varlist(catalog_val);

    // Morph blocks into objects for all error categories.
    //
    const Value* category_tail;
    Value* category = Varlist_Slots(&category_tail, catalog);
    for (; category != category_tail; ++category) {
        assert(Is_Block(category));
        Value* error = rebValue(CANON(CONSTRUCT), CANON(INERT), category);
        Copy_Cell(category, error);  // actually an OBJECT! :-/
        rebRelease(error);
    }

    rebRelease(catalog_val);  // API handle kept it alive for GC
    return catalog;
}


//
//  Startup_Stackoverflow: C
//
void Startup_Stackoverflow(void)
{
    ensure(nullptr, g_error_stack_overflow) = Init_Error(
        Alloc_Value(),
        Error_Stack_Overflow_Raw()
    );

    // !!! The original "No memory" error let you supply the size of the
    // request that could not be fulfilled.  But if you are creating a new
    // out of memory error with that identity, you need to do an allocation...
    // and out of memory errors can't work this way.  It may be that the
    // error is generated after the stack is unwound and memory freed up.
    //
    DECLARE_ATOM (temp);
    Init_Integer(temp, 1020);

    ensure(nullptr, g_error_no_memory) = Init_Error(
        Alloc_Value(),
        Error_No_Memory_Raw(temp)
    );
}


//
//  Shutdown_Stackoverflow: C
//
void Shutdown_Stackoverflow(void)
{
    rebReleaseAndNull(&g_error_stack_overflow);
    rebReleaseAndNull(&g_error_no_memory);
}


//
//  Startup_Utf8_Errors: C
//
// Certain scenarios of using Trap_Back_Scan_Utf8_Char() would become slow and
// leak lots of error allocations if we didn't preallocate these errors (for
// instance, FIND of a TEXT! in a non-UTF-8 binary BLOB! could allocate
// thousands of errors in a single search).
//
// None of these errors are parameterized, so there's no need for them to be
// allocated on a per-instance basis.
//
void Startup_Utf8_Errors(void)
{
    ensure(nullptr, g_error_utf8_too_short) = Init_Error(
        Alloc_Value(),
        Error_Utf8_Too_Short_Raw()
    );
    ensure(nullptr, g_error_utf8_trail_bad_bit) = Init_Error(
        Alloc_Value(),
        Error_Utf8_Trail_Bad_Bit_Raw()
    );
    ensure(nullptr, g_error_overlong_utf8) = Init_Error(
        Alloc_Value(),
        Error_Overlong_Utf8_Raw()
    );
    ensure(nullptr, g_error_codepoint_too_high) = Init_Error(
        Alloc_Value(),
        Error_Codepoint_Too_High_Raw()
    );
    ensure(nullptr, g_error_no_utf8_surrogates) = Init_Error(
        Alloc_Value(),
        Error_No_Utf8_Surrogates_Raw()
    );
    ensure(nullptr, g_error_illegal_zero_byte) = Init_Error(
        Alloc_Value(),
        Error_Illegal_Zero_Byte_Raw()
    );
}


//
//  Shutdown_Utf8_Errors: C
//
void Shutdown_Utf8_Errors(void)
{
    rebReleaseAndNull(&g_error_utf8_too_short);
    rebReleaseAndNull(&g_error_utf8_trail_bad_bit);
    rebReleaseAndNull(&g_error_overlong_utf8);
    rebReleaseAndNull(&g_error_codepoint_too_high);
    rebReleaseAndNull(&g_error_no_utf8_surrogates);
    rebReleaseAndNull(&g_error_illegal_zero_byte);
}


// !!! Though molding has a general facility for a "limit" of the overall
// mold length, this only limits the length a particular value can contribute
// to the mold.  It was only used in error molding and was kept working
// without a general review of such a facility.  Review.
//
static void Mold_Element_Limit(Molder* mo, Element* v, REBLEN limit)
{
    String* str = mo->string;

    REBLEN start_len = String_Len(str);
    Size start_size = String_Size(str);

    Mold_Element(mo, v);  // Note: can't cache pointer into `str` across this

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
void MF_Error(Molder* mo, const Cell* v, bool form)
{
    // Protect against recursion. !!!!
    //
    if (not form) {
        MF_Context(mo, v, false);
        return;
    }

    Error* error = Cell_Error(v);
    ERROR_VARS *vars = ERR_VARS(error);

    // Form: ** <type> Error:
    //
    Append_Ascii(mo->string, "** ");
    if (Is_Word(&vars->type)) {  // has a <type>
        Append_Spelling(mo->string, Cell_Word_Symbol(&vars->type));
        Append_Codepoint(mo->string, ' ');
    }
    else
        assert(Is_Nulled(&vars->type));  // no <type>
    Append_Ascii(mo->string, RM_ERROR_LABEL);  // "Error:"

    // Append: error message ARG1, ARG2, etc.
    if (Is_Block(&vars->message)) {
        bool relax = true;  // don't want error rendering to cause errors
        Form_Array_At(mo, Cell_Array(&vars->message), 0, error, relax);
    }
    else if (Is_Text(&vars->message))
        Form_Element(mo, cast(Element*, &vars->message));
    else
        Append_Ascii(mo->string, RM_BAD_ERROR_FORMAT);

    // Form: ** Where: function
    Value* where = &vars->where;
    if (
        not Is_Nulled(where)
        and not (Is_Block(where) and Cell_Series_Len_At(where) == 0)
    ){
        if (Is_Block(where)) {
            Append_Codepoint(mo->string, '\n');
            Append_Ascii(mo->string, RM_ERROR_WHERE);
            Mold_Element(mo, cast(Element*, where));  // want {fence} shown
        }
        else
            Append_Ascii(mo->string, RM_BAD_ERROR_FORMAT);
    }

    // Form: ** Near: location
    Value* nearest = &vars->nearest;
    if (not Is_Nulled(nearest)) {
        Append_Codepoint(mo->string, '\n');
        Append_Ascii(mo->string, RM_ERROR_NEAR);

        if (Is_Text(nearest)) {
            //
            // !!! The scanner puts strings into the near information in order
            // to say where the file and line of the scan problem was.  This
            // seems better expressed as an explicit argument to the scanner
            // error, because otherwise it obscures the LOAD call where the
            // scanner was invoked.  Review.
            //
            Append_Any_Utf8(mo->string, nearest);
        }
        else if (Any_List(nearest) or Any_Path(nearest))
            Mold_Element_Limit(mo, cast(Element*, nearest), 60);
        else
            Append_Ascii(mo->string, RM_BAD_ERROR_FORMAT);
    }

    // Form: ** File: filename
    //
    // !!! In order to conserve space in the system, filenames are interned.
    // Although interned strings are GC'd when no longer referenced, they can
    // only be used in ANY-WORD? values at the moment, so the filename is
    // not a FILE!.
    //
    Value* file = &vars->file;
    if (not Is_Nulled(file)) {
        Append_Codepoint(mo->string, '\n');
        Append_Ascii(mo->string, RM_ERROR_FILE);
        if (Is_File(file))
            Form_Element(mo, cast(Element*, file));
        else
            Append_Ascii(mo->string, RM_BAD_ERROR_FORMAT);
    }

    // Form: ** Line: line-number
    Value* line = &vars->line;
    if (not Is_Nulled(line)) {
        Append_Codepoint(mo->string, '\n');
        Append_Ascii(mo->string, RM_ERROR_LINE);
        if (Is_Integer(line))
            Form_Element(mo, cast(Element*, line));
        else
            Append_Ascii(mo->string, RM_BAD_ERROR_FORMAT);
    }
}
