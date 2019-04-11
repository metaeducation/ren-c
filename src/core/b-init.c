//
//  File: %b-init.c
//  Summary: "initialization functions"
//  Section: bootstrap
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Rebol Open Source Contributors
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
//=////////////////////////////////////////////////////////////////////////=//
//
// The primary routine for starting up Rebol is Startup_Core().  It runs the
// bootstrap in phases, based on processing various portions of the data in
// %tmp-boot-block.r (which is the aggregated code from the %mezz/*.r files,
// packed into one file as part of the build preparation).
//
// As part of an effort to lock down the memory usage, Ren-C added a parallel
// Shutdown_Core() routine which would gracefully exit Rebol, with assurances
// that all accounting was done correctly.  This includes being sure that the
// number used to track memory usage for triggering garbage collections would
// balance back out to exactly zero.
//
// (Release builds can instead close only vital resources like files, and
// trust the OS exit() to reclaim memory more quickly.  However Ren-C's goal
// is to be usable as a library that may be initialized and shutdown within
// a process that's not exiting, so the ability to clean up is important.)
//


#include "sys-core.h"

#define EVAL_DOSE 10000


//
//  Ensure_Basics: C
//
// Initially these checks were in the debug build only.  However, they are so
// foundational that it's probably worth getting a coherent crash in any build
// where these tests don't work.
//
static void Ensure_Basics(void)
{
    //=//// CHECK REBVAL SIZE ////////////////////////////////////////////=//

    // The system is designed with the intent that REBVAL is 4x(32-bit) on
    // 32-bit platforms and 4x(64-bit) on 64-bit platforms.  It's a crtical
    // performance point.  For the moment we consider it to be essential
    // enough that the system that it refuses to run if not true.
    //
    // But if someone is in an odd situation with a larger sized cell--and
    // it's an even multiple of ALIGN_SIZE--it may still work.  For instance:
    // the DEBUG_TRACK_EXTEND_CELLS mode doubles the cell size to carry the
    // file, line, and tick of their initialization (or last TOUCH_CELL()).
    // Define UNUSUAL_REBVAL_SIZE to bypass this check.

    size_t sizeof_REBVAL = sizeof(REBVAL);
  #if defined(UNUSUAL_REBVAL_SIZE)
    if (sizeof_REBVAL % ALIGN_SIZE != 0)
        panic ("size of REBVAL does not evenly divide by ALIGN_SIZE");
  #else
    if (sizeof_REBVAL != sizeof(void*) * 4)
        panic ("size of REBVAL is not sizeof(void*) * 4");

    #if defined(DEBUG_SERIES_ORIGINS) || defined(DEBUG_COUNT_TICKS)
        assert(sizeof(REBSER) == sizeof(REBVAL) * 2 + sizeof(void*) * 2);
    #else
        assert(sizeof(REBSER) == sizeof(REBVAL) * 2);
    #endif
  #endif

    //=//// CHECK REBSER INFO PLACEMENT ///////////////////////////////////=//

    // The REBSER is designed to place the `info` bits exactly after a REBVAL
    // so they can do double-duty as also a terminator for that REBVAL when
    // enumerated as an ARRAY.  Put the offest into a variable to avoid the
    // constant-conditional-expression warning.

    size_t offsetof_REBSER_info = offsetof(REBSER, info);
    if (
        offsetof_REBSER_info - offsetof(REBSER, content) != sizeof(REBVAL)
    ){
        panic ("bad structure alignment for internal array termination");
    }

    //=//// CHECK BYTE-ORDERING SENSITIVE FLAGS //////////////////////////=//

    // See the %sys-node.h file for an explanation of what these are, and
    // why having them work is fundamental to the API.

    REBFLGS flags
        = FLAG_LEFT_BIT(5) | FLAG_SECOND_BYTE(21) | FLAG_SECOND_UINT16(1975);

    REBYTE m = FIRST_BYTE(flags);  // 6th bit from left set (0b00000100 is 4)
    REBYTE d = SECOND_BYTE(flags);
    uint16_t y = SECOND_UINT16(flags);
    if (m != 4 or d != 21 or y != 1975) {
      #if defined(DEBUG_STDIO_OK)
        printf("m = %u, d = %u, y = %u\n", m, d, y);
      #endif
        panic ("Bad composed integer assignment for byte-ordering macro.");
    }
}


#if !defined(OS_STACK_GROWS_UP) && !defined(OS_STACK_GROWS_DOWN)
    //
    // This is a naive guess with no guarantees.  If there *is* a "real"
    // answer, it would be fairly nuts:
    //
    // http://stackoverflow.com/a/33222085/211160
    //
    // Prefer using a build configuration #define, if possible (although
    // emscripten doesn't necessarily guarantee up or down):
    //
    // https://github.com/kripken/emscripten/issues/5410
    //
    bool Guess_If_Stack_Grows_Up(int *p) {
        int i;
        if (not p)
            return Guess_If_Stack_Grows_Up(&i); // RECURSION: avoids inlining
        if (p < &i) // !!! this comparison is undefined behavior
            return true; // upward
        return false; // downward
    }
#endif


//
//  Set_Stack_Limit: C
//
// See C_STACK_OVERFLOWING for remarks on this **non-standard** technique of
// stack overflow detection.  Note that each thread would have its own stack
// address limits, so this has to be updated for threading.
//
// Currently, this is called every time PUSH_TRAP() is called when Saved_State
// is NULL, and hopefully only one instance of it per thread will be in effect
// (otherwise, the bounds would add and be useless).
//
void Set_Stack_Limit(void *base) {
    //
    // !!! This could be made configurable.  However, it needs to be
    // initialized early in the boot process.  It may be that some small limit
    // is used enough for boot, that can be expanded by native calls later.
    //
    uintptr_t bounds = cast(uintptr_t, STACK_BOUNDS);

  #if defined(OS_STACK_GROWS_UP)
    TG_Stack_Limit = cast(uintptr_t, base) + bounds;
  #elif defined(OS_STACK_GROWS_DOWN)
    TG_Stack_Limit = cast(uintptr_t, base) - bounds;
  #else
    TG_Stack_Grows_Up = Guess_If_Stack_Grows_Up(NULL);
    if (TG_Stack_Grows_Up)
        TG_Stack_Limit = cast(uintptr_t, base) + bounds;
    else
        TG_Stack_Limit = cast(uintptr_t, base) - bounds;
  #endif
}


//
//  Startup_True_And_False: C
//
// !!! Rebol is firm on TRUE and FALSE being WORD!s, as opposed to the literal
// forms of logical true and false.  Not only does this frequently lead to
// confusion, but there's not consensus on what a good literal form would be.
// R3-Alpha used #[true] and #[false] (but often molded them as looking like
// the words true and false anyway).  $true and $false have been proposed,
// but would not be backward compatible in files read by bootstrap.
//
// Since no good literal form exists, the %sysobj.r file uses the words.  They
// have to be defined before the point that it runs (along with the natives).
//
static void Startup_True_And_False(void)
{
    REBVAL *true_value = Append_Context(Lib_Context, 0, Canon(SYM_TRUE));
    Init_True(true_value);
    assert(IS_TRUTHY(true_value) and VAL_LOGIC(true_value) == true);

    REBVAL *false_value = Append_Context(Lib_Context, 0, Canon(SYM_FALSE));
    Init_False(false_value);
    assert(IS_FALSEY(false_value) and VAL_LOGIC(false_value) == false);
}


//
//  generic: enfix native [
//
//  {Creates datatype action (currently for internal use only)}
//
//      return: [void!]
//      :verb [set-word!]
//      spec [block!]
//  ]
//
REBNATIVE(generic)
//
// The `generic` native is searched for explicitly by %make-natives.r and put
// in second place for initialization (after the `native` native).
//
// It is designed to be an enfix function that quotes its first argument,
// so when you write FOO: ACTION [...], the FOO: gets quoted to be the verb.
{
    INCLUDE_PARAMS_OF_GENERIC;

    REBVAL *spec = ARG(spec);

    REBARR *paramlist = Make_Paramlist_Managed_May_Fail(
        spec,
        MKF_KEYWORDS | MKF_RETURN  // return type checked only in debug build
    );

    // !!! There is no system yet for extension types to register which of
    // the generic actions they can handle.  So for the moment, we just say
    // that any custom type will have its action dispatcher run--and it's
    // up to the handler to give an error if there's a problem.  This works,
    // but it limits discoverability of types in HELP.  A better answeer would
    // be able to inventory which types had registered generic dispatchers
    // and list the appropriate types from HELP.
    //
    RELVAL *first_param = ARR_AT(paramlist, 1);
    TYPE_SET(first_param, REB_CUSTOM);
    if (SER(paramlist)->header.bits & PARAMLIST_FLAG_HAS_RETURN) {
        RELVAL *return_param = ARR_AT(paramlist, ARR_LEN(paramlist) - 1);
        assert(VAL_PARAM_SYM(return_param) == SYM_RETURN);
        TYPE_SET(return_param, REB_CUSTOM);
    }

    REBACT *generic = Make_Action(
        paramlist,
        &Generic_Dispatcher,  // return type is only checked in debug build
        nullptr,  // no underlying action (use paramlist)
        nullptr,  // no specialization exemplar (or inherited exemplar)
        IDX_NATIVE_MAX  // details array capacity
    );

    SET_ACTION_FLAG(generic, IS_NATIVE);

    REBARR *details = ACT_DETAILS(generic);
    Init_Word(ARR_AT(details, IDX_NATIVE_BODY), VAL_WORD_CANON(ARG(verb)));
    Init_Object(ARR_AT(details, IDX_NATIVE_CONTEXT), Lib_Context);

    REBVAL *verb_var = Sink_Var_May_Fail(ARG(verb), SPECIFIED);
    Init_Action_Unbound(verb_var, generic);  // set the word to the action

    return Init_Void(D_OUT);  // see ENFIX for why evaluate to void
}


//
//  Add_Lib_Keys_Bootstrap_R3_Cant_Make: C
//
// In order for the bootstrap to assign values to library words, they have to
// exist in the bootstrap context.  The way they get into the context is by
// a scan for top-level SET-WORD!s in the %sys-xxx.r and %mezz-xxx.r files.
//
// However, not all Rebols agree.  For instance, R3-Alpha didn't allow
// set-words like /: and <=:  Older Ren-C treated | as a BAR! datatype, and
// nto as a WORD!.  The words can be gotten with `pick [/] 1` or similar, but
// they cannot be SET because there's nothing in the context to bind them to,
// since no SET-WORD! was picked up in the scan.
//
// As a workaround, this just adds the words to the context manually.  Then,
// however the words are created, it will be possible to bind them and set
// them to things.  What needs to be put in the list depends on how old an
// executable can be used for bootstrap.
//
// !!! It would likely be better if the bootstrap executable weren't LOAD-ing
// and MOLD-ing out source in the first place, but doing whatever work it
// needed to do with PARSE on the strings directly.  That would avoid this.
//
static void Add_Lib_Keys_Bootstrap_R3_Cant_Make(void)
{
    const char *names[] = {
        "<",
        ">",

        "<=", // less than or equal !!! https://forum.rebol.info/t/349/11
        "=>", // Lambda function, quotes optional left argument

        ">=", // greater than or equal to
        "=<", // equal to or less than

        "<>", // not equal (the chosen meaning, as opposed to "empty tag")

        "->", // enfix path op, "SHOVE": https://trello.com/c/Kg9A45b5
        "<-", // Non-null implicit GROUP! begin, e.g. `7 = 1 + <- 2 * 3`

        "|>", // Evaluate to next single expression, but do ones afterward
        "<|", // Evaluate to previous expression, but do rest (like ALSO)

        "|", // Was a BAR! datatype, now returned to WORD!-space
        NULL
    };

    REBCNT i;
    for (i = 0; names[i] != NULL; ++i) {
        REBSTR *str = Intern_UTF8_Managed(cb_cast(names[i]), strlen(names[i]));
        REBVAL *val = Append_Context(Lib_Context, NULL, str);
        Init_Nulled(val); // functions will fill in (no-op, since void already)
    }
}


static REBVAL *Make_Locked_Tag(const char *utf8) { // helper
    REBVAL *t = rebText(utf8);
    mutable_KIND_BYTE(t) = REB_TAG;
    mutable_MIRROR_BYTE(t) = REB_TAG;

    REBSER *locker = nullptr;
    Ensure_Value_Frozen(t, locker);
    return t;
}

//
//  Init_Action_Spec_Tags: C
//
// FUNC and PROC search for these tags, like <opt> and <local>.  They are
// natives and run during bootstrap, so these string comparisons are
// needed.
//
static void Init_Action_Spec_Tags(void)
{
    Root_Void_Tag = Make_Locked_Tag("void");
    Root_With_Tag = Make_Locked_Tag("with");
    Root_Ellipsis_Tag = Make_Locked_Tag("...");
    Root_Opt_Tag = Make_Locked_Tag("opt");
    Root_End_Tag = Make_Locked_Tag("end");
    Root_Blank_Tag = Make_Locked_Tag("blank");
    Root_Local_Tag = Make_Locked_Tag("local");
    Root_Skip_Tag = Make_Locked_Tag("skip");
    Root_Dequote_Tag = Make_Locked_Tag("dequote");
    Root_Requote_Tag = Make_Locked_Tag("requote");
    Root_Const_Tag = Make_Locked_Tag("const");
}

static void Shutdown_Action_Spec_Tags(void)
{
    rebRelease(Root_Void_Tag);
    rebRelease(Root_With_Tag);
    rebRelease(Root_Ellipsis_Tag);
    rebRelease(Root_Opt_Tag);
    rebRelease(Root_End_Tag);
    rebRelease(Root_Blank_Tag);
    rebRelease(Root_Local_Tag);
    rebRelease(Root_Skip_Tag);
    rebRelease(Root_Dequote_Tag);
    rebRelease(Root_Requote_Tag);
    rebRelease(Root_Const_Tag);
}


//
//  Init_Action_Meta_Shim: C
//
// Make_Paramlist_Managed_May_Fail() needs the object archetype ACTION-META
// from %sysobj.r, to have the keylist to use in generating the info used
// by HELP for the natives.  However, natives themselves are used in order
// to run the object construction in %sysobj.r
//
// To break this Catch-22, this code builds a field-compatible version of
// ACTION-META.  After %sysobj.r is loaded, an assert checks to make sure
// that this manual construction actually matches the definition in the file.
//
static void Init_Action_Meta_Shim(void) {
    REBSYM field_syms[6] = {
        SYM_SELF, SYM_DESCRIPTION, SYM_RETURN_TYPE, SYM_RETURN_NOTE,
        SYM_PARAMETER_TYPES, SYM_PARAMETER_NOTES
    };
    REBCTX *meta = Alloc_Context_Core(REB_OBJECT, 6, NODE_FLAG_MANAGED);
    REBCNT i = 1;
    for (; i != 7; ++i) // BLANK!, as `make object! [x: ()]` is illegal
        Init_Blank(Append_Context(meta, nullptr, Canon(field_syms[i - 1])));

    Init_Object(CTX_VAR(meta, 1), meta); // it's "selfish"

    Root_Action_Meta = Init_Object(Alloc_Value(), meta);

    REBSER *locker = nullptr;
    Ensure_Value_Frozen(Root_Action_Meta, locker);

}

static void Shutdown_Action_Meta_Shim(void) {
    rebRelease(Root_Action_Meta);
}


//
//  Make_Native: C
//
// Reused function in Startup_Natives() as well as extensions loading natives,
// which can be parameterized with a different context in which to look up
// bindings by deafault in the API when that native is on the stack.
//
// Each entry should be one of these forms:
//
//    some-name: native [spec content]
//
//    some-name: native/body [spec content] [equivalent user code]
//
// It is optional to put ENFIX between the SET-WORD! and the spec.
//
// If more refinements are added, this will have to get more sophisticated.
//
// Though the manual building of this table is not as "nice" as running the
// evaluator, the evaluator makes comparisons against native values.  Having
// all natives loaded fully before ever running Eval_Core() helps with
// stability and invariants...also there's "state" in keeping track of which
// native index is being loaded, which is non-obvious.  But these issues
// could be addressed (e.g. by passing the native index number / DLL in).
//
REBVAL *Make_Native(
    RELVAL **item, // the item will be advanced as necessary
    REBSPC *specifier,
    REBNAT dispatcher,
    REBVAL *module
){
    assert(specifier == SPECIFIED); // currently a requirement

    // Get the name the native will be started at with in Lib_Context
    //
    if (not IS_SET_WORD(*item))
        panic (*item);

    REBVAL *name = KNOWN(*item);
    ++*item;

    bool enfix;
    if (IS_WORD(*item) and VAL_WORD_SYM(*item) == SYM_ENFIX) {
        enfix = true;
        ++*item;
    }
    else
        enfix = false;

    // See if it's being invoked with NATIVE or NATIVE/BODY
    //
    bool has_body;
    if (IS_WORD(*item)) {
        if (VAL_WORD_SYM(*item) != SYM_NATIVE)
            panic (*item);
        has_body = false;
    }
    else {
        if (
            not IS_PATH(*item)
            or VAL_LEN_HEAD(*item) != 2
            or not IS_WORD(ARR_HEAD(VAL_ARRAY(*item)))
            or VAL_WORD_SYM(ARR_HEAD(VAL_ARRAY(*item))) != SYM_NATIVE
            or not IS_WORD(ARR_AT(VAL_ARRAY(*item), 1))
            or VAL_WORD_SYM(ARR_AT(VAL_ARRAY(*item), 1)) != SYM_BODY
        ){
            panic (*item);
        }
        has_body = true;
    }
    ++*item;

    REBVAL *spec = KNOWN(*item);
    ++*item;
    if (not IS_BLOCK(spec))
        panic (spec);

    // With the components extracted, generate the native and add it to
    // the Natives table.  The associated C function is provided by a
    // table built in the bootstrap scripts, `Native_C_Funcs`.

    REBARR *paramlist = Make_Paramlist_Managed_May_Fail(
        KNOWN(spec),
        MKF_KEYWORDS | MKF_RETURN  // return type checked only in debug build
    );

    REBACT *act = Make_Action(
        paramlist,
        dispatcher, // "dispatcher" is unique to this "native"
        nullptr, // no underlying action (use paramlist)
        nullptr, // no specialization exemplar (or inherited exemplar)
        IDX_NATIVE_MAX // details array capacity
    );

    SET_ACTION_FLAG(act, IS_NATIVE);

    REBARR *details = ACT_DETAILS(act);

    // If a user-equivalent body was provided, we save it in the native's
    // REBVAL for later lookup.
    //
    if (has_body) {
        if (not IS_BLOCK(*item))
            panic (*item);

        Derelativize(ARR_AT(details, IDX_NATIVE_BODY), *item, specifier);
        ++*item;
    }
    else
        Init_Blank(ARR_AT(details, IDX_NATIVE_BODY));

    // When code in the core calls APIs like `rebValue()`, it consults the
    // stack and looks to see where the native function that is running
    // says its "module" is.  For natives, we default to Lib_Context.
    //
    Move_Value(ARR_AT(details, IDX_NATIVE_CONTEXT), module);

    // Append the native to the module under the name given.
    //
    REBVAL *var = Append_Context(VAL_CONTEXT(module), name, 0);
    Init_Action_Unbound(var, act);
    if (enfix)
        SET_CELL_FLAG(var, ENFIXED);

    return var;
}


//
//  Startup_Natives: C
//
// Create native functions.  In R3-Alpha this would go as far as actually
// creating a NATIVE native by hand, and then run code that would call that
// native for each function.  Ren-C depends on having the native table
// initialized to run the evaluator (for instance to test functions against
// the UNWIND native's FUNC signature in definitional returns).  So it
// "fakes it" just by calling a C function for each item...and there is no
// actual "native native".
//
// If there *were* a REBNATIVE(native) this would be its spec:
//
//  native: native [
//      spec [block!]
//      /body
//          {Body of user code matching native's behavior (for documentation)}
//      code [block!]
//  ]
//
// Returns an array of words bound to natives for SYSTEM/CATALOG/NATIVES
//
static REBARR *Startup_Natives(const REBVAL *boot_natives)
{
    // Must be called before first use of Make_Paramlist_Managed_May_Fail()
    //
    Init_Action_Meta_Shim();

    assert(VAL_INDEX(boot_natives) == 0); // should be at head, sanity check
    RELVAL *item = VAL_ARRAY_AT(boot_natives);
    REBSPC *specifier = VAL_SPECIFIER(boot_natives);

    // Although the natives are not being "executed", there are typesets
    // being built from the specs.  So to process `foo: native [x [integer!]]`
    // the INTEGER! word must be bound to its datatype.  Deep walk the
    // natives in order to bind these datatypes.
    //
    Bind_Values_Deep(item, Lib_Context);

    REBARR *catalog = Make_Array(Num_Natives);

    REBCNT n = 0;
    REBVAL *generic_word = nullptr; // gives clear error if GENERIC not found

    while (NOT_END(item)) {
        if (n >= Num_Natives)
            panic (item);

        REBVAL *name = KNOWN(item);
        assert(IS_SET_WORD(name));

        REBVAL *native = Make_Native(
            &item,
            specifier,
            Native_C_Funcs[n],
            CTX_ARCHETYPE(Lib_Context)
        );

        // While the lib context natives can be overwritten, the system
        // currently depends on having a permanent list of the natives that
        // does not change, see uses via NAT_VALUE() and NAT_ACT().
        //
        Prep_Non_Stack_Cell(&Natives[n]);
        Move_Value(&Natives[n], native); // Note: Loses enfixedness (!)
        SET_CELL_FLAG(&Natives[n], PROTECTED);

        REBVAL *catalog_item = Move_Value(Alloc_Tail_Array(catalog), name);
        mutable_KIND_BYTE(catalog_item) = REB_WORD;
        mutable_MIRROR_BYTE(catalog_item) = REB_WORD;

        if (VAL_WORD_SYM(name) == SYM_GENERIC)
            generic_word = name;

        ++n;
    }

    if (n != Num_Natives)
        panic ("Incorrect number of natives found during processing");

    if (not generic_word)
        panic ("GENERIC native not found during boot block processing");

    return catalog;
}


//
//  Startup_Generics: C
//
// Returns an array of words bound to generics for SYSTEM/CATALOG/ACTIONS
//
static REBARR *Startup_Generics(const REBVAL *boot_generics)
{
    assert(VAL_INDEX(boot_generics) == 0); // should be at head, sanity check
    RELVAL *head = VAL_ARRAY_AT(boot_generics);
    REBSPC *specifier = VAL_SPECIFIER(boot_generics);

    // Add SET-WORD!s that are top-level in the generics block to the lib
    // context, so there is a variable for each action.  This means that the
    // assignments can execute.
    //
    Bind_Values_Set_Midstream_Shallow(head, Lib_Context);

    // The above actually does bind the GENERIC word to the GENERIC native,
    // since the GENERIC word is found in the top-level of the block.  But as
    // with the natives, in order to process `foo: generic [x [integer!]]` the
    // INTEGER! word must be bound to its datatype.  Deep bind the code in
    // order to bind the words for these datatypes.
    //
    Bind_Values_Deep(head, Lib_Context);

    DECLARE_LOCAL (result);
    if (Do_Any_Array_At_Throws(result, boot_generics, SPECIFIED))
        panic (result);

    if (not IS_BLANK(result))
        panic (result);

    // Sanity check the symbol transformation
    //
    if (0 != strcmp("open", STR_UTF8(Canon(SYM_OPEN))))
        panic (Canon(SYM_OPEN));

    REBDSP dsp_orig = DSP;

    RELVAL *item = head;
    for (; NOT_END(item); ++item)
        if (IS_SET_WORD(item)) {
            Derelativize(DS_PUSH(), item, specifier);
            mutable_KIND_BYTE(DS_TOP) = REB_WORD; // change pushed to WORD!
            mutable_MIRROR_BYTE(DS_TOP) = REB_WORD;
        }

    return Pop_Stack_Values(dsp_orig); // catalog of generics
}


//
//  Startup_End_Node: C
//
// We can't actually put an end value in the middle of a block, so we poke
// this one into a program global.  It is not legal to bit-copy an END (you
// always use SET_END), so we can make it unwritable.
//
static void Startup_End_Node(void)
{
    PG_End_Node.header = Endlike_Header(0); // no NODE_FLAG_CELL, R/O
    TRACK_CELL_IF_DEBUG(&PG_End_Node, __FILE__, __LINE__);
    assert(IS_END(END_NODE)); // sanity check that it took
}


//
//  Startup_Empty_Array: C
//
// Generic read-only empty array, which will be put into EMPTY_BLOCK when
// Alloc_Value() is available.  Note it's too early for ARRAY_HAS_FILE_LINE.
//
// Warning: GC must not run before Init_Root_Vars() puts it in an API node!
//
static void Startup_Empty_Array(void)
{
    PG_Empty_Array = Make_Array_Core(0, NODE_FLAG_MANAGED);
    SET_SERIES_INFO(PG_Empty_Array, FROZEN);
}


//
//  Init_Root_Vars: C
//
// Create some global variables that are useful, and need to be safe from
// garbage collection.  This relies on the mechanic from the API, where
// handles are kept around until they are rebRelease()'d.
//
// This is called early, so there are some special concerns to building the
// values that would not apply later in boot.
//
static void Init_Root_Vars(void)
{
    // Simple isolated VOID, NONE, TRUE, and FALSE values.
    //
    // They should only be accessed by macros which retrieve their values
    // as `const`, to avoid the risk of accidentally changing them.  (This
    // rule is broken by some special system code which `m_cast`s them for
    // the purpose of using them as directly recognizable pointers which
    // also look like values.)
    //
    // It is presumed that these types will never need to have GC behavior,
    // and thus can be stored safely in program globals without mention in
    // the root set.  Should that change, they could be explicitly added
    // to the GC's root set.

    Prep_Non_Stack_Cell(&PG_Nulled_Cell);
    Init_Nulled(&PG_Nulled_Cell);

    Prep_Non_Stack_Cell(&PG_Blank_Value);
    Init_Blank(&PG_Blank_Value);

    Prep_Non_Stack_Cell(&PG_False_Value);
    Init_False(&PG_False_Value);

    Prep_Non_Stack_Cell(&PG_True_Value);
    Init_True(&PG_True_Value);

    Prep_Non_Stack_Cell(&PG_Void_Value);
    Init_Void(&PG_Void_Value);

    Prep_Non_Stack_Cell(&PG_R_Thrown);
    RESET_CELL(&PG_R_Thrown, REB_R_THROWN, CELL_MASK_NONE);

    Prep_Non_Stack_Cell(&PG_R_Invisible);
    RESET_CELL(&PG_R_Invisible, REB_R_INVISIBLE, CELL_MASK_NONE);

    Prep_Non_Stack_Cell(&PG_R_Immediate);
    RESET_CELL(&PG_R_Immediate, REB_R_IMMEDIATE, CELL_MASK_NONE);

    Prep_Non_Stack_Cell(&PG_R_Redo_Unchecked);
    RESET_CELL(&PG_R_Redo_Unchecked, REB_R_REDO, CELL_MASK_NONE);
    EXTRA(Any, &PG_R_Redo_Unchecked).flag = false;  // "unchecked"

    Prep_Non_Stack_Cell(&PG_R_Redo_Checked);
    RESET_CELL(&PG_R_Redo_Checked, REB_R_REDO, CELL_MASK_NONE);
    EXTRA(Any, &PG_R_Redo_Checked).flag = true;  // "checked"

    Prep_Non_Stack_Cell(&PG_R_Reference);
    RESET_CELL(&PG_R_Reference, REB_R_REFERENCE, CELL_MASK_NONE);

    REBSER *locker = nullptr;

    Root_Empty_Block = Init_Block(Alloc_Value(), PG_Empty_Array);
    Ensure_Value_Frozen(Root_Empty_Block, locker);

    // Note: rebText() can't run yet, review.
    //
    REBSTR *nulled_uni = Make_Unicode(1);

  #if !defined(NDEBUG)
    REBUNI test_nul;
    NEXT_CHR(&test_nul, STR_AT(nulled_uni, 0));
    assert(test_nul == '\0');
    assert(STR_LEN(nulled_uni) == 0);
  #endif

    Root_Empty_Text = Init_Text(Alloc_Value(), nulled_uni);
    Ensure_Value_Frozen(Root_Empty_Text, locker);

    Root_Empty_Binary = Init_Binary(Alloc_Value(), Make_Binary(0));
    Ensure_Value_Frozen(Root_Empty_Binary, locker);

    Root_Space_Char = rebChar(' ');
    Root_Newline_Char = rebChar('\n');

    // !!! Putting the stats map in a root object is a temporary solution
    // to allowing a native coded routine to have a static which is guarded
    // by the GC.  While it might seem better to move the stats into a
    // mostly usermode implementation that hooks apply, this could preclude
    // doing performance analysis on boot--when it would be too early for
    // most user code to be running.  It may be that the debug build has
    // this form of mechanism that can diagnose boot, while release builds
    // rely on a usermode stats module.
    //
    Root_Stats_Map = Init_Map(Alloc_Value(), Make_Map(10));
}

static void Shutdown_Root_Vars(void)
{
    rebRelease(Root_Stats_Map);
    Root_Stats_Map = nullptr;

    rebRelease(Root_Space_Char);
    Root_Space_Char = nullptr;
    rebRelease(Root_Newline_Char);
    Root_Newline_Char = nullptr;

    rebRelease(Root_Empty_Text);
    Root_Empty_Text = nullptr;
    rebRelease(Root_Empty_Block);
    Root_Empty_Block = nullptr;
    rebRelease(Root_Empty_Binary);
    Root_Empty_Binary = nullptr;
}


//
//  Init_System_Object: C
//
// Evaluate the system object and create the global SYSTEM word.  We do not
// BIND_ALL here to keep the internal system words out of the global context.
// (See also N_context() which creates the subobjects of the system object.)
//
static void Init_System_Object(
    const REBVAL *boot_sysobj_spec,
    REBARR *datatypes_catalog,
    REBARR *natives_catalog,
    REBARR *generics_catalog,
    REBCTX *errors_catalog
) {
    assert(VAL_INDEX(boot_sysobj_spec) == 0);
    RELVAL *spec_head = VAL_ARRAY_AT(boot_sysobj_spec);

    // Create the system object from the sysobj block (defined in %sysobj.r)
    //
    REBCTX *system = Make_Selfish_Context_Detect_Managed(
        REB_OBJECT, // type
        VAL_ARRAY_AT(boot_sysobj_spec), // scan for toplevel set-words
        NULL // parent
    );

    Bind_Values_Deep(spec_head, Lib_Context);

    // Bind it so CONTEXT native will work (only used at topmost depth)
    //
    Bind_Values_Shallow(spec_head, system);

    // Evaluate the block (will eval CONTEXTs within).  Expects void result.
    //
    DECLARE_LOCAL (result);
    if (Do_Any_Array_At_Throws(result, boot_sysobj_spec, SPECIFIED))
        panic (result);
    if (not IS_BLANK(result))
        panic (result);

    // Create a global value for it.  (This is why we are able to say `system`
    // and have it bound in lines like `sys: system/contexts/sys`)
    //
    Init_Object(
        Append_Context(Lib_Context, NULL, Canon(SYM_SYSTEM)),
        system
    );

    // Make the system object a root value, to protect it from GC.  (Someone
    // could say `system: blank` in the Lib_Context, otherwise!)
    //
    Root_System = Init_Object(Alloc_Value(), system);

    // Init_Action_Meta_Shim() made Root_Action_Meta as a bootstrap hack
    // since it needed to make function meta information for natives before
    // %sysobj.r's code could run using those natives.  But make sure what it
    // made is actually identical to the definition in %sysobj.r.
    //
    assert(
        0 == CT_Context(
            Get_System(SYS_STANDARD, STD_ACTION_META),
            Root_Action_Meta,
            1 // "strict equality"
        )
    );

    // Create system/catalog/* for datatypes, natives, generics, errors
    //
    Init_Block(Get_System(SYS_CATALOG, CAT_DATATYPES), datatypes_catalog);
    Init_Block(Get_System(SYS_CATALOG, CAT_NATIVES), natives_catalog);
    Init_Block(Get_System(SYS_CATALOG, CAT_ACTIONS), generics_catalog);
    Init_Object(Get_System(SYS_CATALOG, CAT_ERRORS), errors_catalog);

    // Create system/codecs object
    //
    Init_Object(
        Get_System(SYS_CODECS, 0),
        Alloc_Context_Core(REB_OBJECT, 10, NODE_FLAG_MANAGED)
    );

    // The "standard error" template was created as an OBJECT!, because the
    // `make error!` functionality is not ready when %sysobj.r runs.  Fix
    // up its archetype so that it is an actual ERROR!.
    //
    REBVAL *std_error = Get_System(SYS_STANDARD, STD_ERROR);
    assert(IS_OBJECT(std_error));
    mutable_KIND_BYTE(std_error) = REB_ERROR;
    mutable_MIRROR_BYTE(std_error) = REB_ERROR;
    mutable_KIND_BYTE(CTX_ARCHETYPE(VAL_CONTEXT(std_error))) = REB_ERROR;
    mutable_MIRROR_BYTE(CTX_ARCHETYPE(VAL_CONTEXT(std_error))) = REB_ERROR;
    assert(CTX_KEY_SYM(VAL_CONTEXT(std_error), 1) == SYM_SELF);
    mutable_KIND_BYTE(VAL_CONTEXT_VAR(std_error, 1)) = REB_ERROR;
    mutable_MIRROR_BYTE(VAL_CONTEXT_VAR(std_error, 1)) = REB_ERROR;
}

void Shutdown_System_Object(void)
{
    rebRelease(Root_System);
    Root_System = NULL;
}


//
//  Init_Contexts_Object: C
//
// This sets up the system/contexts object.
//
// !!! One of the critical areas in R3-Alpha that was not hammered out
// completely was the question of how the binding process gets started, and
// how contexts might inherit or relate.
//
// However, the basic model for bootstrap is that the "user context" is the
// default area for new code evaluation.  It starts out as a copy of an
// initial state set up in the lib context.  When native routines or other
// content gets overwritten in the user context, it can be borrowed back
// from `system/contexts/lib` (typically aliased as "lib" in the user context).
//
static void Init_Contexts_Object(void)
{
    DROP_GC_GUARD(Sys_Context);
    Init_Object(Get_System(SYS_CONTEXTS, CTX_SYS), Sys_Context);

    DROP_GC_GUARD(Lib_Context);
    Init_Object(Get_System(SYS_CONTEXTS, CTX_LIB), Lib_Context);
    Init_Object(Get_System(SYS_CONTEXTS, CTX_USER), Lib_Context);
}


//
//  Startup_Task: C
//
// !!! Prior to the release of R3-Alpha, there had apparently been some amount
// of effort to take single-threaded assumptions and globals, and move to a
// concept where thread-local storage was used for some previously assumed
// globals.  This would be a prerequisite for concurrency but not enough: the
// memory pools would need protection from one thread to share any series with
// others, due to contention between reading and writing.
//
// Ren-C kept the separation, but if threading were to be a priority it would
// likely be approached a different way.  A nearer short-term feature would be
// "isolates", where independent interpreters can be loaded in the same
// process, just not sharing objects with each other.
//
void Startup_Task(void)
{
    Trace_Level = 0;
    Saved_State = 0;

    Eval_Cycles = 0;
    Eval_Dose = EVAL_DOSE;
    Eval_Count = Eval_Dose;
    Eval_Signals = 0;
    Eval_Sigmask = ALL_BITS;
    Eval_Limit = 0;

    TG_Ballast = MEM_BALLAST; // or overwritten by debug build below...
    TG_Max_Ballast = MEM_BALLAST;

  #ifndef NDEBUG
    const char *env_recycle_torture = getenv("R3_RECYCLE_TORTURE");
    if (env_recycle_torture and atoi(env_recycle_torture) != 0)
        TG_Ballast = 0;

    if (TG_Ballast == 0) {
        printf(
            "**\n" \
            "** R3_RECYCLE_TORTURE is nonzero in environment variable!\n" \
            "** (or TG_Ballast is set to 0 manually in the init code)\n" \
            "** Recycling on EVERY evaluator step, *EXTREMELY* SLOW!...\n" \
            "** Useful in finding bugs before you can run RECYCLE/TORTURE\n" \
            "** But you might only want to do this with -O2 debug builds.\n"
            "**\n"
        );
        fflush(stdout);
     }
  #endif

    // The thrown arg is not intended to ever be around long enough to be
    // seen by the GC.
    //
    Prep_Non_Stack_Cell(&TG_Thrown_Arg);
  #if !defined(NDEBUG)
    SET_END(&TG_Thrown_Arg);

    Prep_Non_Stack_Cell(&TG_Thrown_Label_Debug);
    SET_END(&TG_Thrown_Label_Debug); // see notes, only used "SPORADICALLY()"
  #endif

    Startup_Raw_Print();
    Startup_Scanner();
    Startup_String();
}


#if !defined(NDEBUG)
    //
    // The C language initializes global variables to zero:
    //
    // https://stackoverflow.com/q/2091499
    //
    // For some values this may risk them being consulted and interpreted as
    // the 0 carrying information, as opposed to them not being ready yet.
    // Any variables that should be trashed up front should do so here.
    //
    static void Startup_Trash_Debug(void) {
        assert(not TG_Top_Frame);
        TRASH_POINTER_IF_DEBUG(TG_Top_Frame);
        assert(not TG_Bottom_Frame);
        TRASH_POINTER_IF_DEBUG(TG_Bottom_Frame);

        // ...add more on a case-by-case basis if the case seems helpful...
    }
#endif


//
//  Startup_Base: C
//
// The code in "base" is the lowest level of Rebol initialization written as
// Rebol code.  This is where things like `+` being an infix form of ADD is
// set up, or FIRST being a specialization of PICK.  It's also where the
// definition of the locals-gathering FUNCTION currently lives.
//
static void Startup_Base(REBARR *boot_base)
{
    RELVAL *head = ARR_HEAD(boot_base);

    // By this point, the Lib_Context contains basic definitions for things
    // like true, false, the natives, and the generics.  But before deeply
    // binding the code in the base block to those definitions, add all the
    // top-level SET-WORD! in the base block to Lib_Context as well.
    //
    // Without this shallow walk looking for set words, an assignment like
    // `foo: func [...] [...]` would not have a slot in the Lib_Context
    // for FOO to bind to.  So FOO: would be an unbound SET-WORD!,
    // and give an error on the assignment.
    //
    Bind_Values_Set_Midstream_Shallow(head, Lib_Context);

    // With the base block's definitions added to the mix, deep bind the code
    // and execute it.

    Bind_Values_Deep(head, Lib_Context);

    DECLARE_LOCAL (result);
    if (Do_At_Mutable_Throws(result, boot_base, 0, SPECIFIED))
        panic (result);

    if (not IS_BLANK(result))  // sanity check...script ends with `_`
        panic (result);
}


//
//  Startup_Sys: C
//
// The SYS context contains supporting Rebol code for implementing "system"
// features.  The code has natives, generics, and the definitions from
// Startup_Base() available for its implementation.
//
// (Note: The SYS context should not be confused with "the system object",
// which is a different thing.)
//
// The sys context has a #define constant for the index of every definition
// inside of it.  That means that you can access it from the C code for the
// core.  Any work the core C needs to have done that would be more easily
// done by delegating it to Rebol can use a function in sys as a service.
//
static void Startup_Sys(REBARR *boot_sys) {
    RELVAL *head = ARR_HEAD(boot_sys);

    // Add all new top-level SET-WORD! found in the sys boot-block to Lib,
    // and then bind deeply all words to Lib and Sys.  See Startup_Base() notes
    // for why the top-level walk is needed first.
    //
    Bind_Values_Set_Midstream_Shallow(head, Sys_Context);
    Bind_Values_Deep(head, Lib_Context);
    Bind_Values_Deep(head, Sys_Context);

    DECLARE_LOCAL (result);
    if (Do_At_Mutable_Throws(result, boot_sys, 0, SPECIFIED))
        panic (result);

    if (not IS_BLANK(result))
        panic (result);
}


// By this point in the boot, it's possible to trap failures and exit in
// a graceful fashion.  This is the routine protected by rebRescue() so that
// initialization can handle exceptions.
//
static REBVAL *Startup_Mezzanine(BOOT_BLK *boot)
{
    Startup_Base(VAL_ARRAY(&boot->base));

    Startup_Sys(VAL_ARRAY(&boot->sys));

    REBVAL *finish_init = CTX_VAR(Sys_Context, SYS_CTX_FINISH_INIT_CORE);
    assert(IS_ACTION(finish_init));

    // The FINISH-INIT-CORE function should likely do very little.  But right
    // now it is where the user context is created from the lib context (a
    // copy with some omissions), and where the mezzanine definitions are
    // bound to the lib context and DO'd.
    //
    DECLARE_LOCAL (result);
    if (RunQ_Throws(
        result,
        true, // fully = true (error if all arguments aren't consumed)
        rebU1(finish_init), // %sys-start.r function to call
        KNOWN(&boot->mezz), // boot-mezz argument
        rebEND
    )){
        fail (Error_No_Catch_For_Throw(result));
    }

    if (not IS_VOID(result))
        panic (result); // FINISH-INIT-CORE is a PROCEDURE, returns void

    return NULL;
}


//
//  Startup_Core: C
//
// Initialize the interpreter core.
//
// !!! This will either succeed or "panic".  Panic currently triggers an exit
// to the OS.  The code is not currently written to be able to cleanly shut
// down from a partial initialization.  (It should be.)
//
// The phases of initialization are tracked by PG_Boot_Phase.  Some system
// functions are unavailable at certain phases.
//
// Though most of the initialization is run as C code, some portions are run
// in Rebol.  For instance, GENERIC is a function registered very early on in
// the boot process, which is run from within a block to register more
// functions.
//
// At the tail of the initialization, `finish-init-core` is run.  This Rebol
// function lives in %sys-start.r.   It should be "host agnostic" and not
// assume things about command-line switches (or even that there is a command
// line!)  Converting the code that made such assumptions ongoing.
//
void Startup_Core(void)
{
  #if !defined(NDEBUG)
    Startup_Trash_Debug();
  #endif

//=//// INITIALIZE TICK COUNT /////////////////////////////////////////////=//

    // The timer tick starts at 1, not 0.  This is because the debug build
    // uses signed timer ticks to double as an extra bit of information in
    // REB_BLANK cells to indicate they are "unreadable".
    //
  #if defined(DEBUG_COUNT_TICKS)
    TG_Tick = 1;
  #endif

//=//// INITIALIZE STACK MARKER METRICS ///////////////////////////////////=//

    // !!! See notes on Set_Stack_Limit() about the dodginess of this
    // approach.  Note also that even with a single evaluator used on multiple
    // threads, you have to trap errors to make sure an attempt is not made
    // to longjmp the state to an address from another thread--hence every
    // thread switch must also be a site of trapping all errors.  (Or the
    // limit must be saved in thread local storage.)

    int dummy; // variable whose address acts as base of stack for below code
    Set_Stack_Limit(&dummy);

//=//// INITIALIZE BASIC DIAGNOSTICS //////////////////////////////////////=//

  #if defined(TEST_EARLY_BOOT_PANIC)
    panic ("early panic test"); // should crash
  #elif defined(TEST_EARLY_BOOT_FAIL)
    fail (Error_No_Value_Raw(BLANK_VALUE)); // same as panic (crash)
  #endif

  #ifndef NDEBUG
    PG_Always_Malloc = false;
  #endif

  #ifdef DEBUG_HAS_PROBE
    PG_Probe_Failures = false;
  #endif

    // Globals
    PG_Boot_Phase = BOOT_START;
    PG_Boot_Level = BOOT_LEVEL_FULL;
    PG_Mem_Usage = 0;
    PG_Mem_Limit = 0;
    Reb_Opts = ALLOC(REB_OPTS);
    CLEAR(Reb_Opts, sizeof(REB_OPTS));
    Saved_State = NULL;

    Ensure_Basics();
    PG_Boot_Time = OS_DELTA_TIME(0);

//=//// INITIALIZE MEMORY AND ALLOCATORS //////////////////////////////////=//

    Startup_Pools(0);
    Startup_GC();

    Startup_StdIO();

//=//// INITIALIZE API ////////////////////////////////////////////////////=//

    // The API is one means by which variables can be made whose lifetime is
    // indefinite until program shutdown.  In R3-Alpha this was done with
    // boot code that laid out some fixed structure arrays, but it's more
    // general to do it this way.

    Init_Char_Cases();
    Startup_CRC();             // For word hashing
    Set_Random(0);
    Startup_Interning();

    Startup_End_Node();
    Startup_Empty_Array();

    Startup_Collector();
    Startup_Mold(MIN_COMMON / 4);

    Startup_Data_Stack(STACK_MIN / 4);
    Startup_Frame_Stack(); // uses Canon() in FRM_FILE() currently

    Startup_Api();

//=//// CREATE GLOBAL OBJECTS /////////////////////////////////////////////=//

    Init_Root_Vars();    // Special REBOL values per program

  #if !defined(NDEBUG)
    Assert_Pointer_Detection_Working();  // uses root series/values to test
  #endif

//=//// INITIALIZE (SINGULAR) TASK ////////////////////////////////////////=//

    Startup_Task();

    Init_Action_Spec_Tags(); // Note: uses MOLD_BUF, not available until here

//=//// LOAD BOOT BLOCK ///////////////////////////////////////////////////=//

    // The %make-boot.r process takes all the various definitions and
    // mezzanine code and packs it into one compressed string in
    // %tmp-boot-block.c which gets embedded into the executable.  This
    // includes the type list, word list, error message templates, system
    // object, mezzanines, etc.

    size_t utf8_size;
    const int max = -1; // trust size in gzip data
    REBYTE *utf8 = cast(REBYTE*, rebGunzipAlloc(
        &utf8_size,
        Native_Specs,
        Nat_Compressed_Size,
        max
    ));

    REBARR *boot_array = Scan_UTF8_Managed(
        Intern("tmp-boot.r"),
        utf8,
        utf8_size
    );
    PUSH_GC_GUARD(boot_array); // managed, so must be guarded

    rebFree(utf8); // don't need decompressed text after it's scanned

    BOOT_BLK *boot = cast(BOOT_BLK*, VAL_ARRAY_HEAD(ARR_HEAD(boot_array)));

    Startup_Symbols(VAL_ARRAY(&boot->words));

    // STR_SYMBOL(), VAL_WORD_SYM() and Canon(SYM_XXX) now available

    PG_Boot_Phase = BOOT_LOADED;

//=//// CREATE BASIC VALUES ///////////////////////////////////////////////=//

    // Before any code can start running (even simple bootstrap code), some
    // basic words need to be defined.  For instance: You can't run %sysobj.r
    // unless `true` and `false` have been added to the Lib_Context--they'd be
    // undefined.  And while analyzing the function specs during the
    // definition of natives, things like the <opt> tag are needed as a basis
    // for comparison to see if a usage matches that.

    // !!! Have MAKE-BOOT compute # of words
    //
    Lib_Context = Alloc_Context_Core(REB_OBJECT, 600, NODE_FLAG_MANAGED);
    PUSH_GC_GUARD(Lib_Context);

    Sys_Context = Alloc_Context_Core(REB_OBJECT, 50, NODE_FLAG_MANAGED);
    PUSH_GC_GUARD(Sys_Context);

    REBARR *datatypes_catalog = Startup_Datatypes(
        VAL_ARRAY(&boot->types), VAL_ARRAY(&boot->typespecs)
    );
    Manage_Array(datatypes_catalog);
    PUSH_GC_GUARD(datatypes_catalog);

    // !!! REVIEW: Startup_Typesets() uses symbols, data stack, and
    // adds words to lib--not available untilthis point in time.
    //
    Startup_Typesets();

    Startup_True_And_False();
    Add_Lib_Keys_Bootstrap_R3_Cant_Make();

//=//// RUN CODE BEFORE ERROR HANDLING INITIALIZED ////////////////////////=//

    // Initialize eval handler and ACTION! dispatcher to the default internal
    // routines.  These routines have no tracing, no debug handling, etc.  If
    // those features are needed, augmented functions must be substituted.
    //
    PG_Eval_Maybe_Stale_Throws = &Eval_Internal_Maybe_Stale_Throws;
    PG_Dispatch = &Dispatch_Internal;

    // boot->natives is from the automatically gathered list of natives found
    // by scanning comments in the C sources for `native: ...` declarations.
    //
    REBARR *natives_catalog = Startup_Natives(KNOWN(&boot->natives));
    Manage_Array(natives_catalog);
    PUSH_GC_GUARD(natives_catalog);

    // boot->generics is the list in %generics.r
    //
    REBARR *generics_catalog = Startup_Generics(KNOWN(&boot->generics));
    Manage_Array(generics_catalog);
    PUSH_GC_GUARD(generics_catalog);

    // boot->errors is the error definition list from %errors.r
    //
    REBCTX *errors_catalog = Startup_Errors(KNOWN(&boot->errors));
    PUSH_GC_GUARD(errors_catalog);

    Init_System_Object(
        KNOWN(&boot->sysobj),
        datatypes_catalog,
        natives_catalog,
        generics_catalog,
        errors_catalog
    );

    DROP_GC_GUARD(errors_catalog);
    DROP_GC_GUARD(generics_catalog);
    DROP_GC_GUARD(natives_catalog);
    DROP_GC_GUARD(datatypes_catalog);

    Init_Contexts_Object();

    PG_Boot_Phase = BOOT_ERRORS;

  #if defined(TEST_MID_BOOT_PANIC)
    panic (EMPTY_ARRAY); // panics should be able to give some details by now
  #elif defined(TEST_MID_BOOT_FAIL)
    fail (Error_No_Value_Raw(BLANK_VALUE)); // DEBUG->assert, RELEASE->panic
  #endif

    // Pre-make the stack overflow error (so it doesn't need to be made
    // during a stack overflow).  Error creation machinery depends heavily
    // on the system object being initialized, so this can't be done until
    // now.
    //
    Startup_Stackoverflow();

    Startup_Library_Datatype();  // see notes on why LIBRARY! isn't Built-In

//=//// RUN MEZZANINE CODE NOW THAT ERROR HANDLING IS INITIALIZED /////////=//

    PG_Boot_Phase = BOOT_MEZZ;

    assert(DSP == 0 and FS_TOP == FS_BOTTOM);

    REBVAL *error = rebRescue(cast(REBDNG*, &Startup_Mezzanine), boot);
    if (error) {
        //
        // There is theoretically some level of error recovery that could
        // be done here.  e.g. the evaluator works, it just doesn't have
        // many functions you would expect.  How bad it is depends on
        // whether base and sys ran, so perhaps only errors running "mezz"
        // should be returned.
        //
        // For now, assume any failure to declare the functions in those
        // sections is a critical one.  It may be desirable to tell the
        // caller that the user halted (quitting may not be appropriate if
        // the app is more than just the interpreter)
        //
        // !!! If halt cannot be handled cleanly, it should be set up so
        // that the user isn't even *able* to request a halt at this boot
        // phase.
        //
        panic (error);
    }

    assert(DSP == 0 and FS_TOP == FS_BOTTOM);

    DROP_GC_GUARD(boot_array);

    PG_Boot_Phase = BOOT_DONE;

  #if !defined(NDEBUG)
    Check_Memory_Debug(); // old R3-Alpha check, call here to keep it working
  #endif

    Recycle(); // necessary?
}


//
//  Shutdown_Core: C
//
// The goal of Shutdown_Core() is to release all memory and resources that the
// interpreter has accrued since Startup_Core().  This is a good "sanity check"
// that there aren't unaccounted-for leaks (or semantic errors which such
// leaks may indicate).
//
// Also, being able to clean up is important for a library...which might be
// initialized and shut down multiple times in the same program run.  But
// clients wishing a speedy exit may force an exit to the OS instead of doing
// a clean shut down.  (Note: There still might be some system resources
// that need to be waited on, such as asynchronous writes.)
//
// While some leaks are detected by the debug build during shutdown, even more
// can be found with a tool like Valgrind or Address Sanitizer.
//
void Shutdown_Core(void)
{
  #if !defined(NDEBUG)
    Check_Memory_Debug(); // old R3-Alpha check, call here to keep it working
  #endif

    assert(Saved_State == NULL);

    Shutdown_Data_Stack();

    Shutdown_Library_Datatype();

    Shutdown_Stackoverflow();
    Shutdown_System_Object();
    Shutdown_Typesets();

    Shutdown_Action_Meta_Shim();
    Shutdown_Action_Spec_Tags();
    Shutdown_Root_Vars();

    Shutdown_Frame_Stack();

    Shutdown_Datatypes();

//=//// ALL MANAGED SERIES MUST HAVE THE KEEPALIVE REFERENCES GONE NOW ////=//

    const bool shutdown = true; // go ahead and free all managed series
    Recycle_Core(shutdown, NULL);

    Shutdown_Mold();
    Shutdown_Collector();
    Shutdown_Raw_Print();
    Shutdown_CRC();
    Shutdown_String();
    Shutdown_Scanner();
    Shutdown_Char_Cases();

    // This calls through the Host_Lib table, which Shutdown_Api() nulls out.
    //
    Shutdown_StdIO();

    Shutdown_Api();

    Shutdown_Symbols();
    Shutdown_Interning();

    Shutdown_GC();

    FREE(REB_OPTS, Reb_Opts);

    // Shutting down the memory manager must be done after all the Free_Mem
    // calls have been made to balance their Alloc_Mem calls.
    //
    Shutdown_Pools();
}
