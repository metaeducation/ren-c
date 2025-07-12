//
//  file: %c-error.c
//  summary: "error handling"
//  section: core
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// This is the polymorphic code behind panic(), FAIL(), and FAIL():
//
//    panic ("UTF-8 string");  // delivers error with that text
//    panic (api_value);       // ensure it's an ERROR!, release and use as-is
//    panic (error_context);   // use the Error* as-is
//    panic (PARAM(NAME));     // impliciate parameter as having a bad value
//    panic (other_cell);      // just report as a generic "bad value"
//
// 1. We would face an ambiguity in taking API handles, as to whether that
//    is an error, or if it is "some value" that is just a bad value.  Since
//    internal code that would use this function does not deal often in
//    API values, it's believed that assuming they are errors when passed
//    to `panic()` or `FAIL()` or `FAIL()` is the best policy.
//
// 2. We check to see if the Cell is in the paramlist of the current running
//    native.  (We could theoretically do this with ARG(), or have a nuance of
//    behavior with ARG()...or even for the Key*...but panicking on the PARAM()
//    feels like the best way to "blame" that argument.)
//
Error* Derive_Error_From_Pointer_Core(const void* p) {
    if (p == nullptr)
        return Error_Unknown_Error_Raw();

    switch (Detect_Rebol_Pointer(p)) {
      case DETECTED_AS_UTF8:
        return Error_User(cast(char*, p));

      case DETECTED_AS_STUB: {
        Flex* f = m_cast(Flex*, cast(Flex*, p));  // don't mutate
        if (not Is_Stub_Varlist(f))
            crash (f);  // only kind of Flex allowed are contexts of ERROR!
        if (CTX_TYPE(cast(VarList*, f)) != TYPE_WARNING)
            crash (f);
        return cast(Error*, f); }

      case DETECTED_AS_CELL: {
        const Atom* atom = cast(Atom*, p);
        assert(Is_Cell_Stable(atom));  // !!! Should unstable args be allowed?
        UNUSED(atom);

        const Value* v = cast(Value*, p);

        if (Is_Base_Root_Bit_Set(v)) {  // API handles must be errors [1]
            Error* error;
            if (Is_Warning(v)) {
                error = Cell_Error(v);
            }
            else {
                assert(!"panic() given API handle that is not an ERROR!");
                error = Error_Bad_Value(v);
            }
            rebRelease(m_cast(Value*, v));  // released even if we didn't
            return error;
        }

        if (not Is_Action_Level(TOP_LEVEL))
            return Error_Bad_Value(v);

        const Param* head = Phase_Params_Head(Level_Phase(TOP_LEVEL));
        REBLEN num_params = Phase_Num_Params(Level_Phase(TOP_LEVEL));

        if (v >= head and v < head + num_params) {  // PARAM() error [2]
            const Param* param = cast(const Param*, v);
            return Error_Invalid_Arg(TOP_LEVEL, param);
        }
        return Error_Bad_Value(v); }

      default:
        break;
    }
    crash (p);
}


//
//  Panic_Abruptly_Helper: C
//
// Trigger panic of an error by crossing stacks to enclosing RECOVER_SCOPE.
// Note these panics interrupt code mid-stream, so if a Rebol function is
// running it will not make it to the point of returning the result value.
// This distinguishes the "panic" mechanic from the "throw" mechanic, which has
// to bubble up a thrown value through OUT (used to implement BREAK,
// CONTINUE, RETURN, LEAVE, HALT...)
//
// The function will auto-detect if the pointer it is given is an ERROR!'s
// VarList* or a UTF-8 char *.  If it's UTF-8, an error will be created from
// it automatically (but with no ID...the string becomes the "ID")
//
// If the pointer is to a function parameter of the current native (e.g. what
// you get for PARAM(NAME) in a native), then it will report both the
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
// to clean up later with a textual search for `panic ("`
//
Error* Panic_Abruptly_Helper(Error* error)
{
    Assert_Varlist(error);
    assert(CTX_TYPE(error) == TYPE_WARNING);

    // You can't abruptly panic during the handling of an abrupt panic.
    //
    assert(not (Is_Throwing(TOP_LEVEL) and Is_Throwing_Panic(TOP_LEVEL)));

    // The topmost level must be the one issuing the error.  If a level was
    // pushed with LEVEL_FLAG_TRAMPOLINE_KEEPALIVE that finished executing
    // but remained pushed, it must be dropped before the level that pushes
    // it issues a panic.
    //
    // !!! This turned out to be too restrictive, it's too often useful to
    // panic() while a Level is still pushed (e.g. one being reused across
    // multiple pick steps).
    //
    dont(assert(TOP_LEVEL->executor != nullptr));
    while (TOP_LEVEL->executor == nullptr)
        Drop_Level(TOP_LEVEL);

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
    if (g_probe_panics) {  // see R3_PROBE_PANICS environment variable
        static bool probing = false;

        if (error == cast(void*, Cell_Varlist(g_error_stack_overflow))) {
            printf("PROBE(Stack Overflow): mold in PROBE would recurse\n");
            fflush(stdout);
        }
        else if (probing) {
            printf("PROBE(Recursing): recursing for unknown reason\n");
            crash (error);
        }
        else {
            probing = true;
            PROBE(error);
            probing = false;
        }
    }
  #endif

    // If we panic we'll lose the stack, and if it's an early panic we always
    // want to see it (do not use RESCUE on purpose in Startup_Core()...)
    //
    if (PG_Boot_Phase < BOOT_DONE)
        crash (error);

    // There should be a RECOVER_SCOPE of some kind in effect if a `panic` can
    // ever be run.
    //
    if (g_ts.jump_list == nullptr)
        crash (error);

    // If a throw was being processed up the stack when the panic happened,
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
            if (Is_Level_Dispatching(L)) {
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
                Value* message = Slot_Hack(Varlist_Slot(category, n));
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
// the cheaper DEBUG_PRINTF_PANIC_LOCATIONS was added, which works well enough
// if you're not running under a C debugger.)
//
// 1. Intrinsic natives are very limited in what they are allowed to do (when
//    they are executing as an intrinsic, e.g. they have only one argument
//    that lives in their parent Level's SPARE).  But PANIC is one of the
//    things they should be able to do, and we need to know what's panicking,
//    so we can't implicate the parent.
//
// 2. The WHERE is a backtrace of a block of words, starting from the top of
//    the stack and going downwards.  If a label is not available for a level,
//    we could omit it (which would be deceptive) or we could put ~anonymous~
//    there.  The lighter tilde of quasar (~) is a fairly slight choice, but
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
//    which have Link_Filename()...which either was put on at the time of
//    scanning, or derived when the code is running based on whatever
//    information was on a running array.
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
            Value* frame = Known_Stable(Level_Scratch(L));
            possibly(Is_Action(frame));
            Option(const Symbol*) label = Cell_Frame_Label_Deep(frame);
            if (label)
                Init_Word(PUSH(), unwrap label);
            else
                Init_Quasar(PUSH());  // less space than ~ANYONYMOUS~ [2]
            continue;
        }

        if (not Is_Action_Level(L))
            continue;

        Option(const Symbol*) label = Try_Get_Action_Level_Label(L);
        if (label)
            Init_Word(PUSH(), unwrap label);
        else
            Init_Quasar(PUSH());  // [2]

        if (Is_Level_Fulfilling_Or_Typechecking(L)) { // differentiate [3]
            Source* a = Alloc_Singular(STUB_MASK_MANAGED_SOURCE);
            Move_Cell(Stub_Cell(a), TOP);
            Init_Fence(TOP, a);
            continue;
        }
    }
    Init_Block(Slot_Init_Hack(&vars->where), Pop_Source_From_Stack(base));

    DECLARE_VALUE (nearest);
    require (Read_Slot(nearest, &vars->nearest));

    if (Is_Nulled(nearest))  // don't override scanner data [4]
        Init_Near_For_Level(Slot_Init_Hack(&vars->nearest), where);

    L = where;
    for (; L != BOTTOM_LEVEL; L = L->prior) {
        if (Level_Is_Variadic(L)) {  // could rebValue() have file/line? [5]
            continue;
        }
        if (not Link_Filename(Level_Array(L)))
            continue;
        break;
    }

    if (L != BOTTOM_LEVEL) {  // found a level with file and line information
        Option(const Strand*) file = Link_Filename(Level_Array(L));
        LineNumber line = MISC_SOURCE_LINE(Level_Array(L));

        if (file)
            Init_File(Slot_Init_Hack(&vars->file), unwrap file);
        if (line != 0)
            Init_Integer(Slot_Init_Hack(&vars->line), line);
    }
}


// Most often system errors from %errors.r are thrown by C code using
// Make_Error(), but this routine accommodates verification of errors created
// through user code...which may be mezzanine Rebol itself.  A goal is to not
// allow any such errors to be formed differently than the C code would have
// made them, and to cross through the point of R3-Alpha error compatibility,
// which makes this a rather tortured routine.  However, it maps out the
// existing landscape so that if it is to be changed then it can be seen
// exactly what is changing.
//
IMPLEMENT_GENERIC(MAKE, Is_Warning)
{
    INCLUDE_PARAMS_OF_MAKE;

    assert(Cell_Datatype_Type(ARG(TYPE)) == TYPE_WARNING);
    UNUSED(ARG(TYPE));

    Element* arg = Element_ARG(DEF);

    // Frame from the error object template defined in %sysobj.r
    //
    VarList* root_error = Cell_Varlist(Get_System(SYS_STANDARD, STD_ERROR));

    VarList* varlist;
    ERROR_VARS *vars; // C struct mirroring fixed portion of error fields

    if (Is_Block(arg)) {  // reuse MAKE OBJECT! logic for block
        const Element* tail;
        const Element* head = List_At(&tail, arg);

        varlist = Make_Varlist_Detect_Managed(
            COLLECT_ONLY_SET_WORDS,
            TYPE_WARNING, // type
            head, // values to scan for toplevel set-words
            tail,
            root_error // parent
        );

        require (
          Use* use = Alloc_Use_Inherits(List_Binding(arg))
        );
        Init_Warning(Stub_Cell(use), varlist);

        Tweak_Cell_Binding(arg, use);  // arg is GC protected, so Use is too
        Remember_Cell_Is_Lifeguard(Stub_Cell(use));  // protects Error in eval

        if (Eval_Any_List_At_Throws(SPARE, arg, SPECIFIED))
            return BOUNCE_THROWN;

        Erase_Cell(SPARE);  // ignore result of evaluation

        vars = ERR_VARS(varlist);
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

        varlist = Copy_Varlist_Shallow_Managed(root_error);

        vars = ERR_VARS(varlist);
        /*assert(Is_Nulled(&vars->type));*/  // TEMP! IGNORE
        /*assert(Is_Nulled(&vars->id));*/

        require (
          Strand* copy = Copy_String_At(arg)
        );
        Init_Text(Slot_Init_Hack(&vars->message), copy);
    }
    else
        return fail (arg);

    DECLARE_VALUE (id);
    require (Read_Slot(id, &vars->id));

    DECLARE_VALUE (type);
    require (Read_Slot(type, &vars->type));

    DECLARE_VALUE (message);
    require (Read_Slot(message, &vars->message));

    // Validate the error contents, and reconcile message template and ID
    // information with any data in the object.  Do this for the IS_STRING
    // creation case just to make sure the rules are followed there too.

    // !!! Note that this code is very cautious because the goal isn't to do
    // this as efficiently as possible, rather to put up lots of alarms and
    // traffic cones to make it easy to pick and choose what parts to excise
    // or tighten in an error enhancement upgrade.

    if (Is_Word(type) and Is_Word(id)) {
        // If there was no CODE: supplied but there was a TYPE: and ID: then
        // this may overlap a combination used by Rebol where we wish to
        // fill in the code.  (No fast lookup for this, must search.)

        VarList* categories = Cell_Varlist(Get_System(SYS_CATALOG, CAT_ERRORS));

        // Find correct category for TYPE: (if any)
        Slot* category = maybe Select_Symbol_In_Context(
            Varlist_Archetype(categories),
            Word_Symbol(type)
        );
        if (category) {
            assert(Is_Object(Slot_Hack(category)));

            // Find correct message for ID: (if any)

            Slot* correct_message = maybe Select_Symbol_In_Context(
                Known_Element(Slot_Hack(category)),
                Word_Symbol(&vars->id)
            );
            if (correct_message) {
                assert(
                    Is_Text(Slot_Hack(correct_message))
                    or Is_Block(Slot_Hack(correct_message))
                );
                if (not Is_Nulled(message))
                    return fail (Error_Invalid_Error_Raw(arg));

                Copy_Cell(
                    Slot_Init_Hack(&vars->message), Slot_Hack(correct_message)
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
                //     make warning! [type: 'script id: 'set-self]

                return fail (
                    Error_Invalid_Error_Raw(Varlist_Archetype(varlist))
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
            (Is_Word(id) or Is_Nulled(id))
            and (Is_Word(type) or Is_Nulled(type))
            and (
                Is_Block(message)
                or Is_Text(message)
                or Is_Nulled(message)
            )
        )){
            panic (Error_Invalid_Error_Raw(Varlist_Archetype(varlist)));
        }
    }

    return Init_Warning(OUT, varlist);
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
// This routine should either succeed and return to the caller, or crash()
// and crash if there is a problem (such as running out of memory, or that
// %errors.r has not been loaded).  Hence the caller can assume it will
// regain control to properly call va_end with no longjmp to skip it.
//
Error* Make_Error_Managed_Vaptr(
    Option(SymId) cat_id,
    Option(SymId) id,
    va_list* vaptr
){
    if (PG_Boot_Phase < BOOT_ERRORS) { // no STD_ERROR or template table yet
      #if RUNTIME_CHECKS
        printf(
            "panic() before errors initialized, cat_id = %d, id = %d\n",
            cast(int, cat_id),
            cast(int, id)
        );
      #endif

        DECLARE_ELEMENT (id_value);
        Init_Integer(id_value, cast(int, id));
        crash (id_value);
    }

    VarList* root_varlist = Cell_Varlist(Get_System(SYS_STANDARD, STD_ERROR));

    DECLARE_VALUE (id_value);
    DECLARE_VALUE (type);
    const Value* message;  // Stack values ("movable") are allowed
    if (not id) {
        Init_Nulled(id_value);
        Init_Nulled(type);
        message = va_arg(*vaptr, const Value*);
    }
    else {
        assert(cat_id != SYM_0);
        Init_Word(type, Canon_Symbol(unwrap cat_id));
        Init_Word(id_value, Canon_Symbol(unwrap id));

        // Assume that error IDs are unique across categories (this is checked
        // by %make-boot.r).  If they were not, then this linear search could
        // not be used.
        //
        message = Find_Error_For_Sym(unwrap id);
    }

    assert(message);

    REBLEN expected_args = 0;
    if (Is_Block(message)) { // GET-WORD!s in template should match va_list
        const Element* tail;
        const Element* temp = List_At(&tail, message);
        for (; temp != tail; ++temp) {
            if (Is_Get_Word(temp))
                ++expected_args;
            else
                assert(Is_Text(temp));
        }
    }
    else // Just a string, no arguments expected.
        assert(Is_Text(message));

    // !!! Should things like NEAR and WHERE be in the ADJUNCT and not in the
    // object for the ERROR! itself, so the error could have arguments with
    // any name?  (e.g. NEAR and WHERE?)  In that case, we would be copying
    // the "standard format" error as an adjunct object instead.
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
        const Element* msg_item = List_At(&msg_tail, message);

        for (; msg_item != msg_tail; ++msg_item) {
            if (not Is_Get_Word(msg_item))
                continue;

            const Symbol* symbol = Word_Symbol(msg_item);
            Init(Slot) slot = Append_Context(varlist, symbol);

            const void *p = va_arg(*vaptr, const void*);

            if (p == nullptr) {
                Init_Nulled(slot);  // we permit both nulled cells and nullptr
            }
            else switch (Detect_Rebol_Pointer(p)) {
              case DETECTED_AS_END :
                assert(!"Not enough arguments in Make_Error_Managed()");
                Init_Unset_Due_To_End(u_cast(Atom*, u_cast(Cell*, slot)));
                break;

              case DETECTED_AS_CELL: {
                Copy_Cell(slot, cast(Value*, p));
                break; }

              case DETECTED_AS_STUB: {  // let symbols act as words
                assert(Is_Stub_Symbol(cast(Stub*, p)));
                Init_Word(slot, cast(Symbol*, p));
                break; }

              default:
                assert(false);
                panic ("Bad pointer passed to Make_Error_Managed()");
            }
        }
    }

    assert(Varlist_Len(varlist) == Varlist_Len(root_varlist) + expected_args);

    KIND_BYTE(Rootvar_Of_Varlist(varlist)) = TYPE_WARNING;

    // C struct mirroring fixed portion of error fields
    //
    ERROR_VARS *vars = ERR_VARS(varlist);

    Copy_Cell(Slot_Init_Hack(&vars->message), message);
    Copy_Cell(Slot_Init_Hack(&vars->id), id_value);
    Copy_Cell(Slot_Init_Hack(&vars->type), type);

    return cast(Error*, varlist);
}


//
//  Make_Error_Managed_Raw: C
//
// This variadic function takes a number of Value* arguments appropriate for
// the error category and ID passed.  It is commonly used with panic():
//
//     panic (Make_Error_Managed(SYM_CATEGORY, SYM_SOMETHING, arg1, arg2, ...));
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
//     panic (Error_Something(arg1, thing_processed_to_make_arg2));
//
Error* Make_Error_Managed_Raw(
    int opt_cat_id,  // va_list is weird about enums...
    int opt_id,  // ...note that va_arg(va, enum_type) is illegal!
    ... /* Value* arg1, Value* arg2, ... */
){
    va_list va;

    va_start(va, opt_id);  // last fixed argument is opt_id, pass that

    Error* error = Make_Error_Managed_Vaptr(
        u_cast(Option(SymId), cast(SymId, opt_cat_id)),
        u_cast(Option(SymId), cast(SymId, opt_id)),
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
    Init_Text(message, Make_Strand_UTF8(utf8));
    return Make_Error_Managed(SYM_0, SYM_0, message, rebEND);
}


//
//  Error_Need_Non_End: C
//
// This error was originally just for SET-WORD!, but now it's used by sigils
// which are trying to operate on their right hand sides.
//
// So the message was changed to "error while evaluating VAR:" instead of
// "error while setting VAR:", so "error while evaluating $" etc. make sense.
//
Error* Error_Need_Non_End(const Element* target) {
    assert(Any_Sigiled_Space(target) or Any_Word(target) or Is_Tuple(target));
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
    const Value* trash
){
    assert(Is_Trash(trash));

    return Error_Bad_Word_Get_Raw(target, trash);
}


//
//  Error_Bad_Func_Def: C
//
Error* Error_Bad_Func_Def(const Element* spec, const Element* body)
{
    // !!! Improve this error; it's simply a direct emulation of arity-1
    // error that existed before refactoring code out of Make_Function().

    Source* a = Make_Source_Managed(2);
    require (
      Sink(Element) cell = Alloc_Tail_Array(a)
    );
    Copy_Cell(cell, spec);
    require (
      cell = Alloc_Tail_Array(a)
    );
    Copy_Cell(cell, body);

    DECLARE_ELEMENT (def);
    Init_Block(def, a);

    return Error_Bad_Func_Def_Raw(def);
}


//
//  Error_No_Arg: C
//
Error* Error_No_Arg(Option(const Symbol*) label, const Symbol* symbol)
{
    return Error_No_Arg_Raw(label, symbol);
}


//
//  Error_Unspecified_Arg: C
//
Error* Error_Unspecified_Arg(Level* L) {
    assert(Is_Endlike_Unset(L->u.action.arg));

    const Symbol* param_symbol = Key_Symbol(L->u.action.key);

    Option(const Symbol*) label = Try_Get_Action_Level_Label(L);
    return Error_Unspecified_Arg_Raw(label, param_symbol);
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
Error* Error_Invalid_Arg(Level* L, const Param* param)
{
    assert(Is_Parameter(param));

    const Param* headparam = Phase_Params_Head(Level_Phase(L));
    assert(param >= headparam);
    assert(param <= headparam + Level_Num_Args(L));

    REBLEN index = 1 + (param - headparam);

    Option(const Symbol*) label = Try_Get_Action_Level_Label(L);

    const Symbol* param_symbol = Key_Symbol(Phase_Key(Level_Phase(L), index));

    Atom* atom_arg = Level_Arg(L, index);
    Value *arg = Decay_If_Unstable(atom_arg) except (Error* e) {
        return e;
    }
    return Error_Invalid_Arg_Raw(label, param_symbol, arg);
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
    Atom* atom_arg = Level_Dispatching_Intrinsic_Atom_Arg(L);

    Details* details = Level_Intrinsic_Details(L);
    Option(const Symbol*) label = Level_Intrinsic_Label(L);

    Param* param = Phase_Param(details, 1);
    assert(Is_Parameter(param));
    UNUSED(param);

    const Symbol* param_symbol = Key_Symbol(Phase_Key(details, 1));

    Value* arg = Decay_If_Unstable(atom_arg) except (Error* e) {
        return e;
    }
    return Error_Invalid_Arg_Raw(label, param_symbol, arg);
}


//
//  Error_Bad_Value: C
//
// This is the very vague and generic error citing a value with no further
// commentary or context.  It becomes a catch all for "unexpected input" when
// a more specific error would often be more useful.
//
// The behavior of `panic (some_value)` generates this error, as it can be
// distinguished from `panic (some_context)` meaning that the context iss for
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
Error* Error_Bad_Null(const Element* target) {
    return Error_Bad_Null_Raw(target);
}


//
//  Error_No_Catch_For_Throw: C
//
Error* Error_No_Catch_For_Throw(Level* level_)
{
    DECLARE_VALUE (label);
    Copy_Cell(label, VAL_THROWN_LABEL(level_));

    DECLARE_ATOM (arg);
    CATCH_THROWN(arg, level_);

    if (Is_Warning(label)) {  // what would have been panic()
        assert(Is_Light_Null(arg));
        return Cell_Error(label);
    }

    Value* stable_arg = Decay_If_Unstable(arg) except (Error* e) {
        return e;
    }
    return Error_No_Catch_Raw(stable_arg, label);
}


//
//  Error_Invalid_Type: C
//
// <type> type is not allowed here.
//
Error* Error_Invalid_Type(Type type)
{
    return Error_Invalid_Type_Raw(Datatype_From_Type(type));
}


//
//  Error_Out_Of_Range: C
//
// Accessors like VAL_UINT8() are written to be able to extract the value
// from QUOTED? integers (used in applications like molding, where the quoted
// status is supposed to be ignored).  Copy_Dequoted_Cell() is defined
// after %cell-integer.h, so we handle the issue here.
//
Error* Error_Out_Of_Range(const Value* arg)
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
    return Error_Protected_Word_Raw(sym);
}


//
//  Error_Math_Args: C
//
Error* Error_Math_Args(Type type, const Symbol* verb)
{
    return Error_Not_Related_Raw(verb, Datatype_From_Type(type));
}

//
//  Error_Cannot_Use: C
//
Error* Error_Cannot_Use(const Symbol* verb, const Value* first_arg)
{
    return Error_Cannot_Use_Raw(verb, Datatype_Of(first_arg));
}


//
//  Error_Unexpected_Type: C
//
Error* Error_Unexpected_Type(Type expected, const Value* actual)
{
    assert(expected <= MAX_TYPE);
    assert(Is_Datatype(actual));

    return Error_Expect_Val_Raw(Datatype_From_Type(expected), actual);
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
    Option(const Symbol*) label,  // function's name
    const Key* key,
    const Param* param,
    const Value* arg
){
    if (Parameter_Class(param) == PARAMCLASS_META and Is_Lifted_Error(arg))
        return Cell_Error(arg);

    const Symbol* param_symbol = Key_Symbol(key);

    DECLARE_ELEMENT (spec);
    Option(const Source*) param_array = Parameter_Spec(param);
    if (param_array)
        Init_Block(spec, unwrap param_array);
    else
        Init_Block(spec, g_empty_array);

    return Error_Expect_Arg_Raw(
        label,
        spec,
        param_symbol
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

    if (Parameter_Class(param) == PARAMCLASS_META and Is_Lifted_Error(arg))
        return Cell_Error(arg);

    Error* error = Error_Arg_Type(Level_Label(L), key, param, arg);

    ERROR_VARS* vars = ERR_VARS(error);

    DECLARE_VALUE (id);
    require (Read_Slot(id, &vars->id));

    assert(Is_Word(id));
    assert(Word_Id(id) == SYM_EXPECT_ARG);

    Init_Word(Slot_Init_Hack(&vars->id), CANON(PHASE_EXPECT_ARG));
    return error;
}


//
//  Error_No_Logic_Typecheck: C
//
Error* Error_No_Logic_Typecheck(Option(const Symbol*) label)
{
    return Error_No_Logic_Typecheck_Raw(label);
}


//
//  Error_No_Arg_Typecheck: C
//
Error* Error_No_Arg_Typecheck(Option(const Symbol*) label)
{
    return Error_No_Arg_Typecheck_Raw(label);
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
    DECLARE_ELEMENT (refinement);
    assume (Refinify(Init_Word(refinement, Key_Symbol(key))));
    return Error_Bad_Argless_Refine_Raw(refinement);
}


//
//  Error_Bad_Return_Type: C
//
Error* Error_Bad_Return_Type(Level* L, Atom* atom, const Element* param) {
    Option(const Symbol*) label = Try_Get_Action_Level_Label(L);

    Option(const Source*) array = Parameter_Spec(param);
    assert(array);  // if you return all types, no type should be bad!
    DECLARE_ELEMENT (spec);
    Init_Block(spec, unwrap array);
    return Error_Bad_Return_Type_Raw(label, Datatype_Of(atom), spec);
}


//
//  Error_Bad_Make: C
//
Error* Error_Bad_Make(Type type, const Element* spec)
{
    return Error_Bad_Make_Arg_Raw(Datatype_From_Type(type), spec);
}


//
//  Error_On_Port: C
//
Error* Error_On_Port(SymId id, Value* port, REBINT err_code)
{
    VarList* ctx = Cell_Varlist(port);
    Slot* spec = Varlist_Slot(ctx, STD_PORT_SPEC);

    Slot* val = Varlist_Slot(Cell_Varlist(spec), STD_PORT_SPEC_HEAD_REF);
    if (Is_Space(Slot_Hack(val)))
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
    Copy_Lifted_Cell(reified, anti);

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
Error* Error_Unhandled(Level* level_) {
    UNUSED(level_);
    return Error_User("Unhandled generic dispatch");  // !!! better error idea
}


//
//  Startup_Errors: C
//
// Create error objects and error type objects
//
VarList* Startup_Errors(const Element* boot_errors)
{
  #if DEBUG_HAS_PROBE
    const char *env_probe_panics = getenv("R3_PROBE_PANICS");
    if (env_probe_panics != NULL and atoi(env_probe_panics) != 0) {
        printf(
            "**\n"
            "** R3_PROBE_PANICS is nonzero in environment variable!\n"
            "** Rather noisy, but helps for debugging the boot process...\n"
            "**\n"
        );
        fflush(stdout);
        g_probe_panics = true;
    }
  #endif

    assert(Series_Index(boot_errors) == 0);

    Value* catalog_val = rebValue(CANON(CONSTRUCT), CANON(PIN), boot_errors);
    VarList* catalog = Cell_Varlist(catalog_val);

    // Morph blocks into objects for all error categories.
    //
    const Slot* category_tail;
    Slot* category = Varlist_Slots(&category_tail, catalog);
    for (; category != category_tail; ++category) {
        assert(Is_Block(Slot_Hack(category)));
        Value* error = rebValue(
            CANON(CONSTRUCT), CANON(PIN), Slot_Hack(category)
        );
        Copy_Cell(Slot_Init_Hack(category), error);  // actually an OBJECT! :-/
        rebRelease(error);
    }

    rebRelease(catalog_val);  // API handle kept it alive for GC
    return catalog;
}


//
//  Startup_Stackoverflow: C
//
// 1. The original "No memory" error let you supply the size of the request
//    that could not be fulfilled.  But if you're creating a new out of memory
//    error with that identity, you need an allocation... and out of memory
//    errors can't work this way.  It may be that the error is generated after
//    the stack is unwound and memory freed up.
//
void Startup_Stackoverflow(void)
{
    ensure_nullptr(g_error_stack_overflow) = Init_Warning(
        Alloc_Value(),
        Error_Stack_Overflow_Raw()
    );

    DECLARE_ELEMENT (temp);
    Init_Integer(temp, 1020);  // !!! arbitrary [1]

    ensure_nullptr(g_error_no_memory) = Init_Warning(
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
// Certain scenarios of using Back_Scan_Utf8_Char() would become slow and
// leak lots of error allocations if we didn't preallocate these errors (for
// instance, FIND of a TEXT! in a non-UTF-8 binary BLOB! could allocate
// thousands of errors in a single search).
//
// None of these errors are parameterized, so there's no need for them to be
// allocated on a per-instance basis.
//
void Startup_Utf8_Errors(void)
{
    ensure_nullptr(g_error_utf8_too_short) = Init_Warning(
        Alloc_Value(),
        Error_Utf8_Too_Short_Raw()
    );
    ensure_nullptr(g_error_utf8_trail_bad_bit) = Init_Warning(
        Alloc_Value(),
        Error_Utf8_Trail_Bad_Bit_Raw()
    );
    ensure_nullptr(g_error_overlong_utf8) = Init_Warning(
        Alloc_Value(),
        Error_Overlong_Utf8_Raw()
    );
    ensure_nullptr(g_error_codepoint_too_high) = Init_Warning(
        Alloc_Value(),
        Error_Codepoint_Too_High_Raw()
    );
    ensure_nullptr(g_error_no_utf8_surrogates) = Init_Warning(
        Alloc_Value(),
        Error_No_Utf8_Surrogates_Raw()
    );
    ensure_nullptr(g_error_illegal_zero_byte) = Init_Warning(
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
    Strand* str = mo->strand;

    REBLEN start_len = Strand_Len(str);
    Size start_size = Strand_Size(str);

    Mold_Element(mo, v);  // Note: can't cache pointer into `str` across this

    REBLEN end_len = Strand_Len(str);

    if (end_len - start_len > limit) {
        Utf8(const*) at = cast(Utf8(const*),
            cast(Byte*, Strand_Head(str)) + start_size
        );
        REBLEN n = 0;
        for (; n < limit; ++n)
            at = Skip_Codepoint(at);

        Term_Strand_Len_Size(str, start_len + limit, at - Strand_Head(str));
        Free_Bookmarks_Maybe_Null(str);

        require (Append_Ascii(str, "..."));
    }
}


IMPLEMENT_GENERIC(MOLDIFY, Is_Warning)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(VALUE);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = Bool_ARG(FORM);

    // Protect against recursion. !!!!
    //
    if (not form) {
        Init_Nulled(ARG(FORM));  // form = false;
        return GENERIC_CFUNC(MOLDIFY, Any_Context)(LEVEL);
    }

    Error* error = Cell_Error(v);
    ERROR_VARS *vars = ERR_VARS(error);

    DECLARE_VALUE (type);
    require (Read_Slot(type, &vars->type));

    DECLARE_VALUE (message);
    require (Read_Slot(message, &vars->message));

    DECLARE_VALUE (where);
    require (Read_Slot(where, &vars->where));

    DECLARE_VALUE (nearest);
    require (Read_Slot(nearest, &vars->nearest));

    DECLARE_VALUE (file);
    require (Read_Slot(file, &vars->file));

    DECLARE_VALUE (line);
    require (Read_Slot(line, &vars->line));

    // Form: ** <type> Error:
    //
    require (Append_Ascii(mo->strand, "** "));
    if (Is_Word(type)) {  // has a <type>
        Append_Spelling(mo->strand, Word_Symbol(type));
        Append_Codepoint(mo->strand, ' ');
    }
    else
        assert(Is_Nulled(type));  // no <type>
    require (Append_Ascii(mo->strand, RM_ERROR_LABEL));  // "Error:"

    // Append: error message ARG1, ARG2, etc.
    if (Is_Block(message)) {
        bool relax = true;  // don't want error rendering to cause errors
        Form_Array_At(mo, Cell_Array(message), 0, error, relax);
    }
    else if (Is_Text(message))
        Form_Element(mo, cast(Element*, message));
    else {
        require (Append_Ascii(mo->strand, RM_BAD_ERROR_FORMAT));
    }

    // Form: ** Where: function
    if (
        not Is_Nulled(where)
        and not (Is_Block(where) and Series_Len_At(where) == 0)
    ){
        if (Is_Block(where)) {
            Append_Codepoint(mo->strand, '\n');
            require (Append_Ascii(mo->strand, RM_ERROR_WHERE));
            Mold_Element(mo, cast(Element*, where));  // want {fence} shown
        }
        else {
            require (Append_Ascii(mo->strand, RM_BAD_ERROR_FORMAT));
        }
    }

    // Form: ** Near: location
    if (not Is_Nulled(nearest)) {
        Append_Codepoint(mo->strand, '\n');
        require (Append_Ascii(mo->strand, RM_ERROR_NEAR));

        if (Is_Text(nearest)) {
            //
            // !!! The scanner puts strings into the near information in order
            // to say where the file and line of the scan problem was.  This
            // seems better expressed as an explicit argument to the scanner
            // error, because otherwise it obscures the LOAD call where the
            // scanner was invoked.  Review.
            //
            Append_Any_Utf8(mo->strand, nearest);
        }
        else if (Any_List(nearest) or Is_Path(nearest))
            Mold_Element_Limit(mo, cast(Element*, nearest), 60);
        else {
            require (Append_Ascii(mo->strand, RM_BAD_ERROR_FORMAT));
        }
    }

    // Form: ** File: filename
    //
    // !!! In order to conserve space in the system, filenames are interned.
    // Although interned strings are GC'd when no longer referenced, they can
    // only be used in ANY-WORD? values at the moment, so the filename is
    // not a FILE!.
    //
    if (not Is_Nulled(file)) {
        Append_Codepoint(mo->strand, '\n');
        require (Append_Ascii(mo->strand, RM_ERROR_FILE));
        if (Is_File(file))
            Form_Element(mo, cast(Element*, file));
        else {
            require (Append_Ascii(mo->strand, RM_BAD_ERROR_FORMAT));
        }
    }

    // Form: ** Line: line-number
    if (not Is_Nulled(line)) {
        Append_Codepoint(mo->strand, '\n');
        require (Append_Ascii(mo->strand, RM_ERROR_LINE));
        if (Is_Integer(line))
            Form_Element(mo, cast(Element*, line));
        else {
            require (Append_Ascii(mo->strand, RM_BAD_ERROR_FORMAT));
        }
    }

    return TRIPWIRE;
}
