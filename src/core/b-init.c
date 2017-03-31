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
// The primary routine for starting up Rebol is Init_Core().  It runs the
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
#include "mem-pools.h"

#define EVAL_DOSE 10000


//
//  Assert_Basics: C
//
static void Assert_Basics(void)
{
#if !defined(NDEBUG) && defined(SHOW_SIZEOFS)
    //
    // For debugging ports to some systems
    //
#if defined(__LP64__) || defined(__LLP64__)
    const char *fmt = "%lu %s\n";
#else
    const char *fmt = "%u %s\n";
#endif

    union Reb_Value_Payload *dummy_payload;

    printf(fmt, sizeof(dummy_payload->any_word), "any_word");
    printf(fmt, sizeof(dummy_payload->any_series), "any_series");
    printf(fmt, sizeof(dummy_payload->integer), "integer");
    printf(fmt, sizeof(dummy_payload->decimal), "decimal");
    printf(fmt, sizeof(dummy_payload->character), "char");
    printf(fmt, sizeof(dummy_payload->datatype), "datatype");
    printf(fmt, sizeof(dummy_payload->typeset), "typeset");
    printf(fmt, sizeof(dummy_payload->time), "time");
    printf(fmt, sizeof(dummy_payload->tuple), "tuple");
    printf(fmt, sizeof(dummy_payload->function), "function");
    printf(fmt, sizeof(dummy_payload->any_context), "any_context");
    printf(fmt, sizeof(dummy_payload->pair), "pair");
    printf(fmt, sizeof(dummy_payload->event), "event");
    printf(fmt, sizeof(dummy_payload->library), "library");
    printf(fmt, sizeof(dummy_payload->structure), "struct");
    printf(fmt, sizeof(dummy_payload->gob), "gob");
    printf(fmt, sizeof(dummy_payload->money), "money");
    printf(fmt, sizeof(dummy_payload->handle), "handle");
    printf(fmt, sizeof(dummy_payload->all), "all");
    fflush(stdout);
#endif

#if !defined(NDEBUG)
    //
    // Sanity check the platform byte-ordering sensitive flag macros
    //
    REBUPT flags;

    flags = FLAGIT_LEFT(0);
    unsigned char *ch = (unsigned char*)&flags;
    if (*ch != 128) {
        printf("Expected 128, got %d\n", *ch);
        panic ("Bad leftmost bit setting of platform unsigned integer.");
    }

    flags = FLAGIT_LEFT(0) | FLAGIT_LEFT(1) | FLAGBYTE_RIGHT(13);

    REBCNT left = LEFT_N_BITS(flags, 3); // == 6 (binary `110`)
    REBCNT right = RIGHT_N_BITS(flags, 3); // == 5 (binary `101`)
    if (left != 6 || right != 5) {
        printf("Expected 6 and 5, got %u and %u\n", left, right);
        panic ("Bad composed integer assignment for byte-ordering macro.");
    }
#endif

    // Although the system is designed to be able to function with REBVAL at
    // any size, the optimization of it being 4x(32-bit) on 32-bit platforms
    // and 4x(64-bit) on 64-bit platforms is a rather important performance
    // point.  For the moment we consider it to be essential enough to the
    // intended function of the system that it refuses to run if not true.
    //
    // But if someone is in an odd situation and understands why the size did
    // not work out as designed, it *should* be possible to comment this out
    // and keep running.
    //
    if (sizeof(void*) == 8) {
        if (sizeof(REBVAL) != 32 || sizeof(REBEVT) != 32)
            panic ("size of void* is 8 but REBVAL is not sizeof(void*)*4");

        assert(sizeof(REBGOB) == 88); // !!! REBGOB to be made a REBARR
    }
    else if (sizeof(void*) == 4) {
        if (sizeof(REBVAL) != 16 || sizeof(REBEVT) != 16)
            panic ("size of void* is 4 but REBVAL is not sizeof(void*)*4");

        assert(sizeof(REBGOB) == 64); // !!! REBGOB to be made a REBARR
    }
    else
        panic ("sizeof void* is neither 4 nor 8");

    assert(sizeof(REBDAT) == 4);
    assert(sizeof(REBEVT) == sizeof(REBVAL));

    // The REBSER is designed to place the `info` bits exactly after a REBVAL
    // so they can do double-duty as also a terminator for that REBVAL when
    // enumerated as an ARRAY.
    //
    if (
        offsetof(struct Reb_Series, info)
            - offsetof(struct Reb_Series, content)
        != sizeof(REBVAL)
    ){
        panic ("bad structure alignment for internal array termination");
    }

    // The END marker logic currently uses REB_MAX for the type bits.  That's
    // okay up until the REB_MAX bumps to 256.  If you hit this then some
    // other value needs to be chosen in the debug build to represent the
    // type value for END's bits.  (REB_TRASH is just a poor choice, because
    // you'd like to catch IS_END() tests on trash.)
    //
    assert(REB_MAX < 256);
}


//
//  Init_Base: C
//
// The code in "base" is the lowest level of Rebol initialization written as
// Rebol code.  This is where things like `+` being an infix form of ADD is
// set up, or FIRST being a specialization of PICK.  It's also where the
// definition of the locals-gathering FUNCTION currently lives.
//
static void Init_Base(REBARR *boot_base)
{
    RELVAL *head = ARR_HEAD(boot_base);

    // By this point, the Lib_Context contains basic definitions for things
    // like true, false, the natives, and the actions.  But before deeply
    // binding the code in the base block to those definitions, add all the
    // top-level SET-WORD! in the base block to Lib_Context as well.
    //
    // Without this shallow walk looking for set words, an assignment like
    // `function: func [...] [...]` would not have a slot in the Lib_Context
    // for FUNCTION to bind to.  So FUNCTION: would be an unbound SET-WORD!,
    // and give an error on the assignment.
    //
    Bind_Values_Set_Midstream_Shallow(head, Lib_Context);

    // With the base block's definitions added to the mix, deep bind the code
    // and execute it.  As a sanity check, it's expected the base block will
    // return no value when executed...hence it should end in `()`.

    Bind_Values_Deep(head, Lib_Context);

    DECLARE_LOCAL (result);
    if (Do_At_Throws(result, boot_base, 0, SPECIFIED))
        panic (result);

    if (!IS_VOID(result))
        panic (result);
}


//
//  Init_Sys: C
//
// The SYS context contains supporting Rebol code for implementing "system"
// features.  The code has natives, actions, and the definitions from
// Init_Base() available for its implementation.
//
// (Note: The SYS context should not be confused with "the system object",
// which is a different thing.)
//
// The sys context has a #define constant for the index of every definition
// inside of it.  That means that you can access it from the C code for the
// core.  Any work the core C needs to have done that would be more easily
// done by delegating it to Rebol can use a function in sys as a service.
//
static void Init_Sys(REBARR *boot_sys) {
    RELVAL *head = ARR_HEAD(boot_sys);

    // Add all new top-level SET-WORD! found in the sys boot-block to Lib,
    // and then bind deeply all words to Lib and Sys.  See Init_Base() notes
    // for why the top-level walk is needed first.
    //
    Bind_Values_Set_Midstream_Shallow(head, Sys_Context);
    Bind_Values_Deep(head, Lib_Context);
    Bind_Values_Deep(head, Sys_Context);

    DECLARE_LOCAL (result);
    if (Do_At_Throws(result, boot_sys, 0, SPECIFIED))
        panic (result);

    if (!IS_VOID(result))
        panic (result);
}


//
//  Init_Datatypes: C
//
// Create library words for each type, (e.g. make INTEGER! correspond to
// the integer datatype value).  Returns an array of words for the added
// datatypes to use in SYSTEM/CATALOG/DATATYPES
//
// Note the type enum starts at 1 (REB_FUNCTION), given that REB_0 is used
// for special purposes and not correspond to a user-visible type.  REB_MAX is
// used for void, and also not value type.  Hence the total number of types is
// REB_MAX - 1.
//
static REBARR *Init_Datatypes(REBARR *boot_types, REBARR *boot_typespecs)
{
    if (ARR_LEN(boot_types) != REB_MAX - 1)
        panic (boot_types); // Every REB_XXX but REB_0 should have a WORD!

    RELVAL *word = ARR_HEAD(boot_types);

    if (VAL_WORD_SYM(word) != SYM_FUNCTION_X)
        panic (word); // First type should be FUNCTION!

    REBARR *catalog = Make_Array(REB_MAX - 1);

    REBINT n;
    for (n = 1; NOT_END(word); word++, n++) {
        assert(n < REB_MAX);

        REBVAL *value = Append_Context(Lib_Context, KNOWN(word), NULL);
        VAL_RESET_HEADER(value, REB_DATATYPE);
        VAL_TYPE_KIND(value) = cast(enum Reb_Kind, n);
        VAL_TYPE_SPEC(value) = VAL_ARRAY(ARR_AT(boot_typespecs, n - 1));

        // !!! The system depends on these definitions, as they are used by
        // Get_Type and Type_Of.  Lock it for safety...though consider an
        // alternative like using the returned types catalog and locking
        // that.  (It would be hard to rewrite lib to safely change a type
        // definition, given the code doing the rewriting would likely depend
        // on lib...but it could still be technically possible, even in
        // a limited sense.)
        //
        assert(value == Get_Type(cast(enum Reb_Kind, n)));
        SET_VAL_FLAG(CTX_VAR(Lib_Context, 1), VALUE_FLAG_PROTECTED);

        Append_Value(catalog, KNOWN(word));
    }

    return catalog;
}


//
//  Init_True_And_False: C
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
static void Init_True_And_False(void)
{
    REBVAL *true_value = Append_Context(Lib_Context, 0, Canon(SYM_TRUE));
    SET_TRUE(true_value);
    assert(VAL_LOGIC(true_value) == TRUE);
    assert(IS_CONDITIONAL_TRUE(true_value));

    REBVAL *false_value = Append_Context(Lib_Context, 0, Canon(SYM_FALSE));
    SET_FALSE(false_value);
    assert(VAL_LOGIC(false_value) == FALSE);
    assert(IS_CONDITIONAL_FALSE(false_value));
}


//
//  action: native [
//
//  {Creates datatype action (for internal usage only).}
//
//      return: [function!]
//      :verb [set-word! word!]
//      spec [block!]
//  ]
//
REBNATIVE(action)
//
// The `action` native is searched for explicitly by %make-natives.r and put
// in second place for initialization (after the `native` native).
//
// It is designed to be a lookback binding that quotes its first argument,
// so when you write FOO: ACTION [...], the FOO: gets quoted to be the verb.
// The SET/LOOKBACK is done by the bootstrap, after the natives are loaded.
{
    INCLUDE_PARAMS_OF_ACTION;

    REBVAL *spec = ARG(spec);

    // We only want to check the return type in the debug build.  In the
    // release build, we want to have as few argument slots as possible...
    // especially to get the optimization for 1 argument to go in the cell
    // and not need to push arguments.
    //
    REBFLGS flags = MKF_KEYWORDS | MKF_FAKE_RETURN;

    REBFUN *fun = Make_Function(
        Make_Paramlist_Managed_May_Fail(spec, flags),
        &Action_Dispatcher,
        NULL, // no underlying function--this is fundamental
        NULL // not providing a specialization
    );

    Move_Value(FUNC_BODY(fun), ARG(verb));

    // A lookback quoting function that quotes a SET-WORD! on its left is
    // responsible for setting the value if it wants it to change since the
    // SET-WORD! is not actually active.  But if something *looks* like an
    // assignment, it's good practice to evaluate the whole expression to
    // the result the SET-WORD! was set to, so `x: y: op z` makes `x = y`.
    //
    Move_Value(Sink_Var_May_Fail(ARG(verb), SPECIFIED), FUNC_VALUE(fun));
    Move_Value(D_OUT, FUNC_VALUE(fun));
    return R_OUT;
}


//
//  Add_Lib_Keys_R3Alpha_Cant_Make: C
//
// In order for the bootstrap to assign values to library words, they have to
// exist in the bootstrap context.  The way they get into the context is by
// a scan for top-level SET-WORD!s in the %sys-xxx.r and %mezz-xxx.r files.
//
// However, R3-Alpha doesn't allow set-words like /: and <=:  The words can
// be gotten with `pick [/] 1` or similar, but they cannot be SET because
// there's nothing in the context to bind them to...since no SET-WORD! was
// picked up in the scan.
//
// As a workaround, this just adds the words to the context manually.  Then,
// however the words are created, it will be possible to bind them and set
// them to things.
//
// !!! Even as Ren-C becomes more permissive in letting SET-WORDs for these
// items be created, they should not be seen by %make-boot.r so long as the
// code expects to be bootstrapped with R3-Alpha.  This is because as part
// of the bootstrap, the code is loaded/processed and molded out as one
// giant file.  Ren-C being able to read `=>:` would not be able to help
// retroactively make old R3-Alphas read it too.
//
static void Add_Lib_Keys_R3Alpha_Cant_Make(void)
{
    const char *names[] = {
        "<",
        ">",

        "<=", // less than or equal to
        "=>", // no current system meaning

        ">=", // greater than or equal to
        "=<",

        "<>", // may ultimately be targeted for empty tag in Ren-C

        "->", // FUNCTION-style lambda ("reaches in")
        "<-", // FUNC-style lambda ("reaches out"),

        "|>", // Evaluate to next single expression, but do ones afterward
        "<|", // Evaluate to previous expression, but do rest (like ALSO)

        "/",
        "//", // is remainder in R3-Alpha, not ideal

        NULL
    };

    REBINT i = 0;
    while (names[i]) {
        REBSTR *str = Intern_UTF8_Managed(cb_cast(names[i]), strlen(names[i]));
        REBVAL *val = Append_Context(Lib_Context, NULL, str);
        SET_VOID(val); // functions will fill in (no-op, since void already)
        ++i;
    }
}


//
// Init_Function_Tag: C
//
// !!! It didn't seem there was a "compare UTF8 byte array to arbitrary
// decoded REB_TAG which may or may not be REBUNI" routine, but there was
// an easy way to compare tags to each other.  So pre-fabricating these was
// quick, but a better solution should be reviewed in terms of an overall
// string and UTF8 rethinking.
//
static void Init_Function_Tag(const char *name, REBVAL *slot)
{
    Init_Tag(slot, Make_UTF8_May_Fail(name));
    Freeze_Sequence(VAL_SERIES(slot));
}


//
//  Init_Function_Tags: C
//
// FUNC and PROC search for these tags, like <opt> and <local>.  They are
// natives and run during bootstrap, so these string comparisons are
// needed.  This routine does not use a table directly, because the slots
// it initializes are not constants...and older TCCs don't support local
// struct arrays of that form.
//
static void Init_Function_Tags(void)
{
    Init_Function_Tag("with", ROOT_WITH_TAG);
    Init_Function_Tag("...", ROOT_ELLIPSIS_TAG);
    Init_Function_Tag("opt", ROOT_OPT_TAG);
    Init_Function_Tag("end", ROOT_END_TAG);
    Init_Function_Tag("local", ROOT_LOCAL_TAG);
    Init_Function_Tag("durable", ROOT_DURABLE_TAG);
}


//
//  Init_Function_Meta_Shim: C
//
// Make_Paramlist_Managed_May_Fail() needs the object archetype FUNCTION-META
// from %sysobj.r, to have the keylist to use in generating the info used
// by HELP for the natives.  However, natives themselves are used in order
// to run the object construction in %sysobj.r
//
// To break this Catch-22, this code builds a field-compatible version of
// FUNCTION-META.  After %sysobj.r is loaded, an assert checks to make sure
// that this manual construction actually matches the definition in the file.
//
static void Init_Function_Meta_Shim(void) {
    REBSYM field_syms[6] = {
        SYM_SELF, SYM_DESCRIPTION, SYM_RETURN_TYPE, SYM_RETURN_NOTE,
        SYM_PARAMETER_TYPES, SYM_PARAMETER_NOTES
    };
    REBCTX *function_meta = Alloc_Context(6);
    REBCNT i = 1;
    for (; i <= 6; ++i) {
        //
        // BLANK! is used for the fields instead of void (required for
        // R3-Alpha compatibility to load the object)
        //
        SET_BLANK(
            Append_Context(function_meta, NULL, Canon(field_syms[i - 1]))
        );
    }

    REBVAL *rootvar = CTX_VALUE(function_meta);
    VAL_RESET_HEADER(rootvar, REB_OBJECT);
    rootvar->extra.binding = NULL;
    Init_Object(CTX_VAR(function_meta, 1), function_meta); // it's "selfish"

    Init_Object(ROOT_FUNCTION_META, function_meta);
}


//
//  Init_Natives: C
//
// Create native functions.  In R3-Alpha this would go as far as actually
// creating a NATIVE native by hand, and then run code that would call that
// native for each function.  Ren-C depends on having the native table
// initialized to run the evaluator (for instance to test functions against
// the EXIT native's FUNC signature in definitional returns).  So it
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
static REBARR *Init_Natives(REBARR *boot_natives)
{
    // Must be called before first use of Make_Paramlist_Managed_May_Fail()
    //
    Init_Function_Meta_Shim();

    RELVAL *item = ARR_HEAD(boot_natives);

    // Although the natives are not being "executed", there are typesets
    // being built from the specs.  So to process `foo: native [x [integer!]]`
    // the INTEGER! word must be bound to its datatype.  Deep walk the
    // natives in order to bind these datatypes.
    //
    Bind_Values_Deep(item, Lib_Context);

    REBARR *catalog = Make_Array(NUM_NATIVES);

    REBCNT n = 0;
    REBVAL *action_word = NULL;

    while (NOT_END(item)) {
        if (n >= NUM_NATIVES)
            panic (item);

        // Each entry should be one of these forms:
        //
        //    some-name: native [spec content]
        //
        //    some-name: native/body [spec content] [equivalent user code]
        //
        // If more refinements are added, this code will have to be made
        // more sophisticated.
        //
        // Though the manual building of this table is not as nice as running
        // the evaluator, the evaluator makes comparisons against native
        // values.  Having all natives loaded fully before ever running
        // Do_Core() helps with stability and invariants.

        // Get the name the native will be started at with in Lib_Context
        //
        if (!IS_SET_WORD(item))
            panic (item);

        REBVAL *name = KNOWN(item);
        ++item;

        // See if it's being invoked with NATIVE or NATIVE/BODY
        //
        REBOOL has_body;
        if (IS_WORD(item)) {
            if (VAL_WORD_SYM(item) != SYM_NATIVE)
                panic (item);
            has_body = FALSE;
        }
        else {
            if (
                !IS_PATH(item)
                || VAL_LEN_HEAD(item) != 2
                || !IS_WORD(ARR_HEAD(VAL_ARRAY(item)))
                || VAL_WORD_SYM(ARR_HEAD(VAL_ARRAY(item))) != SYM_NATIVE
                || !IS_WORD(ARR_AT(VAL_ARRAY(item), 1))
                || VAL_WORD_SYM(ARR_AT(VAL_ARRAY(item), 1)) != SYM_BODY
            ) {
                panic (item);
            }
            has_body = TRUE;
        }
        ++item;

        REBVAL *spec = KNOWN(item);
        ++item;
        if (!IS_BLOCK(spec))
            panic (spec);

        // With the components extracted, generate the native and add it to
        // the Natives table.  The associated C function is provided by a
        // table built in the bootstrap scripts, `Native_C_Funcs`.

        // We only want to check the return type in the debug build.  In the
        // release build, we want to have as few argument slots as possible...
        // especially to get the optimization for 1 argument to go in the cell
        // and not need to push arguments.
        //
        REBFLGS flags = MKF_KEYWORDS | MKF_FAKE_RETURN;

        REBFUN *fun = Make_Function(
            Make_Paramlist_Managed_May_Fail(KNOWN(spec), flags),
            Native_C_Funcs[n], // "dispatcher" is unique to this "native"
            NULL, // no underlying function, this is fundamental
            NULL // not providing a specialization
        );

        // If a user-equivalent body was provided, we save it in the native's
        // REBVAL for later lookup.
        //
        if (has_body) {
            REBVAL *body = KNOWN(item); // !!! handle relative?
            ++item;
            if (!IS_BLOCK(body))
                panic (body);
            *FUNC_BODY(fun) = *body;
        }

        Prep_Global_Cell(&Natives[n]);
        Move_Value(&Natives[n], FUNC_VALUE(fun));

        // Append the native to the Lib_Context under the name given.
        //
        REBVAL *var = Append_Context(Lib_Context, name, 0);
        Move_Value(var, &Natives[n]);

        // Do special case SET/LOOKBACK=TRUE so that SOME-ACTION: ACTION [...]
        // allows ACTION to see the SOME-ACTION symbol, and know to use it.
        //
        if (VAL_WORD_SYM(name) == SYM_ACTION) {
            SET_VAL_FLAG(var, VALUE_FLAG_ENFIXED);
            action_word = name;
        }

        REBVAL *catalog_item = Alloc_Tail_Array(catalog);
        Move_Value(catalog_item, name);
        VAL_SET_TYPE_BITS(catalog_item, REB_WORD);

        ++n;
    }

    if (n != NUM_NATIVES)
        panic ("Incorrect number of natives found during processing");

    if (action_word == NULL)
        panic ("ACTION native not found during boot block processing");

    return catalog;
}


//
//  Init_Actions: C
//
// Returns an array of words bound to actions for SYSTEM/CATALOG/ACTIONS
//
static REBARR *Init_Actions(REBARR *boot_actions)
{
    RELVAL *head = ARR_HEAD(boot_actions);

    // Add SET-WORD!s that are top-level in the actions block to the lib
    // context, so there is a variable for each action.  This means that the
    // assignments can execute.
    //
    Bind_Values_Set_Midstream_Shallow(head, Lib_Context);

    // The above code actually does bind the ACTION word to the ACTION native,
    // since the action word is found in the top-level of the block.  But as
    // with the natives, in order to process `foo: action [x [integer!]]` the
    // INTEGER! word must be bound to its datatype.  Deep bind the code in
    // order to bind the words for these datatypes.
    //
    Bind_Values_Deep(head, Lib_Context);

    DECLARE_LOCAL (result);
    if (Do_At_Throws(result, boot_actions, 0, SPECIFIED))
        panic (result);

    if (!IS_VOID(result))
        panic (result);

    // Sanity check the symbol transformation
    //
    if (0 != strcmp("open", cs_cast(STR_HEAD(Canon(SYM_OPEN)))))
        panic (Canon(SYM_OPEN));

    REBDSP dsp_orig = DSP;

    RELVAL *item = head;
    for (; NOT_END(item); ++item)
        if (IS_SET_WORD(item)) {
            DS_PUSH_RELVAL(item, SPECIFIED);
            VAL_SET_TYPE_BITS(DS_TOP, REB_WORD); // change pushed to WORD!
        }

    return Pop_Stack_Values(dsp_orig); // catalog of actions
}


//
//  Init_Root_Context: C
//
// Hand-build the root context where special REBOL values are
// stored. Called early, so it cannot depend on any other
// system structures or values.
//
// Note that the Root_Vars's word table is unset!
// None of its values are exported.
//
static void Init_Root_Context(void)
{
    REBCTX *root = Alloc_Context(ROOT_MAX - 1);
    PG_Root_Context = root;

    SET_SER_FLAG(CTX_VARLIST(root), SERIES_FLAG_FIXED_SIZE);
    Root_Vars = cast(ROOT_VARS*, ARR_HEAD(CTX_VARLIST(root)));

    // Get rid of the keylist, we will make another one later in the boot.
    // (You can't ASSERT_CONTEXT(PG_Root_Context) until that happens.)  The
    // new keylist will be managed so we manage the varlist to match.
    //
    // !!! We let it get GC'd, which is a bit wasteful, but the interface
    // for Alloc_Context() wants to manage the keylist in general.  This
    // is done for convenience.
    //
    /*Free_Array(CTX_KEYLIST(root));*/
    /*INIT_CTX_KEYLIST_UNIQUE(root, NULL);*/ // can't use with NULL
    AS_SERIES(CTX_VARLIST(root))->link.keylist = NULL;
    MANAGE_ARRAY(CTX_VARLIST(root));

    // !!! Also no `stackvars` (or `spec`, not yet implemented); revisit
    //
    VAL_RESET_HEADER(CTX_VALUE(root), REB_OBJECT);
    CTX_VALUE(root)->extra.binding = NULL;

    // Set all other values to blank
    {
        REBINT n = 1;
        REBVAL *var = CTX_VARS_HEAD(root);
        for (; n < ROOT_MAX; n++, var++) SET_BLANK(var);
        TERM_ARRAY_LEN(CTX_VARLIST(root), ROOT_MAX);
    }

    // These values are simple isolated VOID, NONE, TRUE, and FALSE values
    // that can be used in lieu of initializing them.  They are initialized
    // as two-element series in order to ensure that their address is not
    // treated as an array.
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

    Prep_Global_Cell(&PG_Void_Cell[0]);
    Prep_Global_Cell(&PG_Void_Cell[1]);
    SET_VOID(&PG_Void_Cell[0]);
    SET_TRASH_IF_DEBUG(&PG_Void_Cell[1]);

    Prep_Global_Cell(&PG_Blank_Value[0]);
    Prep_Global_Cell(&PG_Blank_Value[1]);
    SET_BLANK(&PG_Blank_Value[0]);
    SET_TRASH_IF_DEBUG(&PG_Blank_Value[1]);

    Prep_Global_Cell(&PG_Bar_Value[0]);
    Prep_Global_Cell(&PG_Bar_Value[1]);
    SET_BAR(&PG_Bar_Value[0]);
    SET_TRASH_IF_DEBUG(&PG_Bar_Value[1]);

    Prep_Global_Cell(&PG_False_Value[0]);
    Prep_Global_Cell(&PG_False_Value[1]);
    SET_FALSE(&PG_False_Value[0]);
    SET_TRASH_IF_DEBUG(&PG_False_Value[1]);

    Prep_Global_Cell(&PG_True_Value[0]);
    Prep_Global_Cell(&PG_True_Value[1]);
    SET_TRUE(&PG_True_Value[0]);
    SET_TRASH_IF_DEBUG(&PG_True_Value[1]);

    // We can't actually put an end value in the middle of a block, so we poke
    // this one into a program global.  It is not legal to bit-copy an
    // END (you always use SET_END), so we can make it unwritable.
    //
    Init_Endlike_Header(&PG_End_Node.header, 0); // mutate to read-only end
#if !defined(NDEBUG)
    Set_Track_Payload_Debug(&PG_End_Node, __FILE__, __LINE__);
#endif
    assert(IS_END(END)); // sanity check that it took
    assert(VAL_TYPE_RAW(END) == REB_0); // this implicit END marker has this

    // The EMPTY_BLOCK provides EMPTY_ARRAY.  It is locked for protection.
    //
    Init_Block(ROOT_EMPTY_BLOCK, Make_Array(0));
    Deep_Freeze_Array(VAL_ARRAY(ROOT_EMPTY_BLOCK));

    REBSER *empty_series = Make_Binary(1);
    *BIN_AT(empty_series, 0) = '\0';
    Init_String(ROOT_EMPTY_STRING, empty_series);
    Freeze_Sequence(VAL_SERIES(ROOT_EMPTY_STRING));

    SET_CHAR(ROOT_SPACE_CHAR, ' ');
    SET_CHAR(ROOT_NEWLINE_CHAR, '\n');

    // Can't ASSERT_CONTEXT here; no keylist yet...
}


//
//  Init_Task_Context: C
//
// See above notes (same as root context, except for tasks)
//
static void Init_Task_Context(void)
{
    REBCTX *task = Alloc_Context(TASK_MAX - 1);
    TG_Task_Context = task;

    SET_SER_FLAG(CTX_VARLIST(task), SERIES_FLAG_FIXED_SIZE);
    Task_Vars = cast(TASK_VARS*, ARR_HEAD(CTX_VARLIST(task)));

    // Get rid of the keylist, we will make another one later in the boot.
    // (You can't ASSERT_CONTEXT(TG_Task_Context) until that happens.)  The
    // new keylist will be managed so we manage the varlist to match.
    //
    // !!! We let it get GC'd, which is a bit wasteful, but the interface
    // for Alloc_Context() wants to manage the keylist in general.  This
    // is done for convenience.
    //
    /*Free_Array(CTX_KEYLIST(task));*/
    /*INIT_CTX_KEYLIST_UNIQUE(task, NULL);*/ // can't use with NULL
    AS_SERIES(CTX_VARLIST(task))->link.keylist = NULL;

    MANAGE_ARRAY(CTX_VARLIST(task));

    // !!! Also no `body` (or `spec`, not yet implemented); revisit
    //
    VAL_RESET_HEADER(CTX_VALUE(task), REB_OBJECT);
    CTX_VALUE(task)->extra.binding = NULL;

    // Set all other values to NONE:
    {
        REBINT n = 1;
        REBVAL *var = CTX_VARS_HEAD(task);
        for (; n < TASK_MAX; n++, var++) SET_BLANK(var);
        TERM_ARRAY_LEN(CTX_VARLIST(task), TASK_MAX);
    }

    // Initialize a few fields:
    SET_INTEGER(TASK_BALLAST, MEM_BALLAST);
    SET_INTEGER(TASK_MAX_BALLAST, MEM_BALLAST);

    // The thrown arg is not intended to ever be around long enough to be
    // seen by the GC.
    //
    Prep_Global_Cell(&TG_Thrown_Arg);
    SET_UNREADABLE_BLANK(&TG_Thrown_Arg);

    Prep_Global_Cell(&PG_Va_List_Pending);

    // Can't ASSERT_CONTEXT here; no keylist yet...
}


//
//  Init_System_Object: C
//
// Evaluate the system object and create the global SYSTEM word.  We do not
// BIND_ALL here to keep the internal system words out of the global context.
// (See also N_context() which creates the subobjects of the system object.)
//
static void Init_System_Object(
    REBARR *boot_sysobj_spec,
    REBARR *datatypes_catalog,
    REBARR *natives_catalog,
    REBARR *actions_catalog,
    REBCTX *errors_catalog
) {
    RELVAL *spec_head = ARR_HEAD(boot_sysobj_spec);

    // Create the system object from the sysobj block (defined in %sysobj.r)
    //
    REBCTX *system = Make_Selfish_Context_Detect(
        REB_OBJECT, // type
        NULL, // binding
        spec_head, // scan for toplevel set-words
        NULL // parent
    );

    Bind_Values_Deep(spec_head, Lib_Context);

    // Bind it so CONTEXT native will work (only used at topmost depth)
    //
    Bind_Values_Shallow(spec_head, system);

    // Evaluate the block (will eval CONTEXTs within).  Expects void result.
    //
    DECLARE_LOCAL (result);
    if (Do_At_Throws(result, boot_sysobj_spec, 0, SPECIFIED))
        panic (result);
    if (!IS_VOID(result))
        panic (result);

    // Create a global value for it.  (This is why we are able to say `system`
    // and have it bound in lines like `sys: system/contexts/sys`)
    //
    Init_Object(
        Append_Context(Lib_Context, NULL, Canon(SYM_SYSTEM)),
        system
    );

    // We also add the system object under the root, to ensure it can't be
    // garbage collected and be able to access it from the C code.  (Someone
    // could say `system: blank` in the Lib_Context and then it would be a
    // candidate for garbage collection otherwise!)
    //
    Init_Object(ROOT_SYSTEM, system);

    // Init_Function_Meta_Shim() made ROOT_FUNCTION_META as a bootstrap hack
    // since it needed to make function meta information for natives before
    // %sysobj.r's code could run using those natives.  But make sure what it
    // made is actually identical to the definition in %sysobj.r.
    //
    assert(
        0 == CT_Context(
            Get_System(SYS_STANDARD, STD_FUNCTION_META),
            ROOT_FUNCTION_META,
            TRUE
        )
    );

    // Create system/catalog/* for datatypes, natives, actions, errors
    //
    Init_Block(Get_System(SYS_CATALOG, CAT_DATATYPES), datatypes_catalog);
    Init_Block(Get_System(SYS_CATALOG, CAT_NATIVES), natives_catalog);
    Init_Block(Get_System(SYS_CATALOG, CAT_ACTIONS), actions_catalog);
    Init_Object(Get_System(SYS_CATALOG, CAT_ERRORS), errors_catalog);

    // Create system/codecs object
    //
    {
        REBCTX *codecs = Alloc_Context(10);
        SET_BLANK(CTX_ROOTKEY(codecs));
        VAL_RESET_HEADER(CTX_VALUE(codecs), REB_OBJECT);
        CTX_VALUE(codecs)->extra.binding = NULL;
        Init_Object(Get_System(SYS_CODECS, 0), codecs);
    }
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
    DROP_GUARD_CONTEXT(Sys_Context);
    Init_Object(Get_System(SYS_CONTEXTS, CTX_SYS), Sys_Context);

    DROP_GUARD_CONTEXT(Lib_Context);
    Init_Object(Get_System(SYS_CONTEXTS, CTX_LIB), Lib_Context);
    Init_Object(Get_System(SYS_CONTEXTS, CTX_USER), Lib_Context);
}


//
//  Init_Task: C
//
void Init_Task(void)
{
    // Thread locals:
    Trace_Level = 0;
    Saved_State = 0;

    Eval_Cycles = 0;
    Eval_Dose = EVAL_DOSE;
    Eval_Count = Eval_Dose;
    Eval_Signals = 0;
    Eval_Sigmask = ALL_BITS;

    // errors? problem with PG_Boot_Phase shared?

    Init_Pools(-4);
    Init_GC();
    Init_Task_Context();    // Special REBOL values per task

    Init_Raw_Print();
    Init_Stacks(STACK_MIN/4);
    Init_Scanner();
    Init_Mold(MIN_COMMON/4);
    Init_Collector();
}


//
//  Init_Locale: C
//
void Init_Locale(void)
{
    REBCHR *data;

    if ((data = OS_GET_LOCALE(0))) {
        Init_String(
            Get_System(SYS_LOCALE, LOCALE_LANGUAGE),
            Copy_OS_Str(data, OS_STRLEN(data))
        );
        OS_FREE(data);
    }

    if ((data = OS_GET_LOCALE(1))) {
        Init_String(
            Get_System(SYS_LOCALE, LOCALE_LANGUAGE_P),
            Copy_OS_Str(data, OS_STRLEN(data))
        );
        OS_FREE(data);
    }

    if ((data = OS_GET_LOCALE(2))) {
        Init_String(
            Get_System(SYS_LOCALE, LOCALE_LOCALE),
            Copy_OS_Str(data, OS_STRLEN(data))
        );
        OS_FREE(data);
    }

    if ((data = OS_GET_LOCALE(3))) {
        Init_String(
            Get_System(SYS_LOCALE, LOCALE_LOCALE_P),
            Copy_OS_Str(data, OS_STRLEN(data))
        );
        OS_FREE(data);
    }
}


//
//  Init_Core: C
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
// in Rebol.  For instance, ACTION is a function registered very early on in
// the boot process, which is run from within a block to register more
// functions.
//
// At the tail of the initialization, `finish-init-core` is run.  This Rebol
// function lives in %sys-start.r.   It should be "host agnostic" and not
// assume things about command-line switches (or even that there is a command
// line!)  Converting the code that made such assumptions ongoing.
//
void Init_Core(void)
{

//==//////////////////////////////////////////////////////////////////////==//
//
// INITIALIZE STACK MARKER METRICS
//
//==//////////////////////////////////////////////////////////////////////==//

    // See C_STACK_OVERFLOWING for remarks on this **non-standard** technique
    // of stack overflow detection.  Note that each thread would have its
    // own stack address limits, so this has to be updated for threading.
    //
    // Note that R3-Alpha tried to use a trick (which it got wrong) to
    // determine whether the stack grew up or down.  This doesn't work, and
    // the solutions that might actually work are too wacky to justify using:
    //
    // http://stackoverflow.com/a/33222085/211160
    //
    // So it's better to go with a build configuration #define.  Note that
    // stacks growing up is uncommon (e.g. Debian hppa architecture)

    REBUPT bounds;
    bounds = cast(REBUPT, OS_CONFIG(1, 0));
    if (bounds == 0)
        bounds = cast(REBUPT, STACK_BOUNDS);

#ifdef OS_STACK_GROWS_UP
    Stack_Limit = cast(REBUPT, &bounds) + bounds;
#else
    Stack_Limit = cast(REBUPT, &bounds) - bounds;
#endif

//==//////////////////////////////////////////////////////////////////////==//
//
// TEST EARLY BOOT PANIC AND FAIL
//
//==//////////////////////////////////////////////////////////////////////==//

    // It should be legal to panic at any time (especially given that the
    // low bar for behavior is "crash out").  fail() is more complex since it
    // uses error objects which require the system to be initialized, so it
    // should fall back to being a panic at early boot phases.

#if defined(TEST_EARLY_BOOT_PANIC)
    panic ("early panic test");
#elif defined(TEST_EARLY_BOOT_FAIL)
    fail (Error_No_Value_Raw(BLANK_VALUE));
#endif

//==//////////////////////////////////////////////////////////////////////==//
//
// INITIALIZE BASIC DIAGNOSTICS
//
//==//////////////////////////////////////////////////////////////////////==//

#ifndef NDEBUG
    PG_Always_Malloc = FALSE;
#endif

    // Globals
    PG_Boot_Phase = BOOT_START;
    PG_Boot_Level = BOOT_LEVEL_FULL;
    PG_Mem_Usage = 0;
    PG_Mem_Limit = 0;
    Reb_Opts = ALLOC(REB_OPTS);
    CLEAR(Reb_Opts, sizeof(REB_OPTS));
    Saved_State = NULL;

    // Thread locals.
    //
    // !!! This code duplicates work done in Init_Task
    //
    Trace_Level = 0;
    Saved_State = 0;
    Eval_Dose = EVAL_DOSE;
    Eval_Count = Eval_Dose;
    Eval_Limit = 0;
    Eval_Signals = 0;
    Eval_Sigmask = ALL_BITS;

    Init_StdIO();

    Assert_Basics();
    PG_Boot_Time = OS_DELTA_TIME(0, 0);

//==//////////////////////////////////////////////////////////////////////==//
//
// INITIALIZE MEMORY AND ALLOCATORS
//
//==//////////////////////////////////////////////////////////////////////==//

    Init_Pools(0);          // Memory allocator
    Init_GC();
    Init_Root_Context();    // Special REBOL values per program
    Init_Task_Context();    // Special REBOL values per task

    Init_Raw_Print();       // Low level output (Print)

//==//////////////////////////////////////////////////////////////////////==//
//
// CREATE GLOBAL OBJECTS
//
//==//////////////////////////////////////////////////////////////////////==//

    Init_Char_Cases();
    Init_CRC();             // For word hashing
    Set_Random(0);
    Init_Words();
    Init_Stacks(STACK_MIN * 4);
    Init_Scanner();
    Init_Mold(MIN_COMMON);  // Output buffer
    Init_Collector();

//==//////////////////////////////////////////////////////////////////////==//
//
// LOAD BOOT BLOCK
//
//==//////////////////////////////////////////////////////////////////////==//

    // The %make-boot.r process takes all the various definitions and
    // mezzanine code and packs it into one compressed string in
    // %tmp-boot-block.c which gets embedded into the executable.  This
    // includes the type list, word list, error message templates, system
    // object, mezzanines, etc.

    REBSER *utf8 = Decompress(
        Native_Specs, NAT_COMPRESSED_SIZE, NAT_UNCOMPRESSED_SIZE, FALSE, FALSE
    );
    if (utf8 == NULL || SER_LEN(utf8) != NAT_UNCOMPRESSED_SIZE)
        panic ("decompressed native specs size mismatch (try `make clean`)");

    REBARR *boot_array = Scan_UTF8_Managed(
        BIN_HEAD(utf8), NAT_UNCOMPRESSED_SIZE
    );
    PUSH_GUARD_ARRAY(boot_array); // managed, so must be guarded

    Free_Series(utf8); // don't need decompressed text after it's scanned

    BOOT_BLK *boot = cast(BOOT_BLK*, VAL_ARRAY_HEAD(ARR_HEAD(boot_array)));

    Init_Symbols(VAL_ARRAY(&boot->words));

    // STR_SYMBOL(), VAL_WORD_SYM() and Canon(SYM_XXX) now available

    PG_Boot_Phase = BOOT_LOADED;

    // Get the words of the ROOT context (to avoid it being an exception case)
    //
    REBARR *root_keylist = Collect_Keylist_Managed(
        NULL,
        VAL_ARRAY_HEAD(&boot->root),
        NULL,
        COLLECT_ANY_WORD
    );
    INIT_CTX_KEYLIST_UNIQUE(PG_Root_Context, root_keylist);
    ASSERT_CONTEXT(PG_Root_Context);

    AS_SERIES(CTX_VARLIST(PG_Root_Context))->header.bits
        |= NODE_FLAG_ROOT;

    // Get the words of the TASK context (to avoid it being an exception case)
    //
    REBARR *task_keylist = Collect_Keylist_Managed(
        NULL,
        VAL_ARRAY_HEAD(&boot->task),
        NULL,
        COLLECT_ANY_WORD
    );
    INIT_CTX_KEYLIST_UNIQUE(TG_Task_Context, task_keylist);
    ASSERT_CONTEXT(TG_Task_Context);

    AS_SERIES(CTX_VARLIST(TG_Task_Context))->header.bits
        |= NODE_FLAG_ROOT;

//==//////////////////////////////////////////////////////////////////////==//
//
// CREATE BASIC VALUES
//
//==//////////////////////////////////////////////////////////////////////==//

    // Before any code can start running (even simple bootstrap code), some
    // basic words need to be defined.  For instance: You can't run %sysobj.r
    // unless `true` and `false` have been added to the Lib_Context--they'd be
    // undefined.  And while analyzing the function specs during the
    // definition of natives, things like the <opt> tag are needed as a basis
    // for comparison to see if a usage matches that.

    // !!! Have MAKE-BOOT compute # of words
    //
    Lib_Context = Alloc_Context(600);
    VAL_RESET_HEADER(CTX_VALUE(Lib_Context), REB_OBJECT);
    CTX_VALUE(Lib_Context)->extra.binding = NULL;
    MANAGE_ARRAY(CTX_VARLIST(Lib_Context));
    PUSH_GUARD_CONTEXT(Lib_Context);

    Sys_Context = Alloc_Context(50);
    VAL_RESET_HEADER(CTX_VALUE(Sys_Context), REB_OBJECT);
    CTX_VALUE(Sys_Context)->extra.binding = NULL;
    MANAGE_ARRAY(CTX_VARLIST(Sys_Context));
    PUSH_GUARD_CONTEXT(Sys_Context);

    REBARR *datatypes_catalog = Init_Datatypes(
        VAL_ARRAY(&boot->types), VAL_ARRAY(&boot->typespecs)
    );
    Init_Typesets();
    Init_True_And_False();
    Init_Function_Tags();
    Add_Lib_Keys_R3Alpha_Cant_Make();

    Prep_Global_Cell(&Callback_Error);
    SET_UNREADABLE_BLANK(&Callback_Error);

//==//////////////////////////////////////////////////////////////////////==//
//
// RUN CODE BEFORE ERROR HANDLING INITIALIZED
//
//==//////////////////////////////////////////////////////////////////////==//

    // boot->natives is from the automatically gathered list of natives found
    // by scanning comments in the C sources for `native: ...` declarations.
    //
    REBARR *natives_catalog = Init_Natives(VAL_ARRAY(&boot->natives));

    // boot->actions is the list in %actions.r
    //
    REBARR *actions_catalog = Init_Actions(VAL_ARRAY(&boot->actions));

    // boot->errors is the error definition list from %errors.r
    //
    REBCTX *errors_catalog = Init_Errors(VAL_ARRAY(&boot->errors));

    Init_System_Object(
        VAL_ARRAY(&boot->sysobj),
        datatypes_catalog,
        natives_catalog,
        actions_catalog,
        errors_catalog
    );

    Init_Contexts_Object();
    Init_Locale();

    Move_Value(ROOT_ERROBJ, Get_System(SYS_STANDARD, STD_ERROR));

    PG_Boot_Phase = BOOT_ERRORS;

#if defined(TEST_MID_BOOT_PANIC)
    //
    // At this point panics should be able to do a reasonable job of giving
    // details on Rebol types.
    //
    panic (EMPTY_ARRAY);
#elif defined(TEST_MID_BOOT_FAIL)
    //
    // With no PUSH_TRAP yet, fail should give a localized assert in a debug
    // build, and panic the release build.
    //
    fail (Error_No_Value_Raw(BLANK_VALUE));
#endif

    // Special pre-made errors:
    Init_Error(TASK_STACK_ERROR, Error_Stack_Overflow_Raw());
    Init_Error(TASK_HALT_ERROR, Error_Halt_Raw());

//==//////////////////////////////////////////////////////////////////////==//
//
// RUN MEZZANINE CODE NOW THAT ERROR HANDLING IS INITIALIZED
//
//==//////////////////////////////////////////////////////////////////////==//

    REBCTX *error;
    struct Reb_State state;

    // Older compilers at high warning levels may not realize that boot->base
    // and boot->sys do not change, e.g. the level of indirection confuses
    // them.  This can result in warnings about "clobbering" variables via
    // longjmp.  Extract into locals to avoid that.

    REBARR *base_array = VAL_ARRAY(&boot->base);
    REBARR *sys_array = VAL_ARRAY(&boot->sys);
    REBVAL *mezz_block = &boot->mezz;

    // With error trapping enabled, set up to catch them if they happen.
    PUSH_UNHALTABLE_TRAP(&error, &state);

    boot = NULL;

// The first time through the following code 'error' will be NULL, but...
// `fail` can longjmp here, so 'error' won't be NULL *if* that happens!

    if (error) {
        //
        // You shouldn't be able to halt during Init_Core() startup.
        // The only way you should be able to stop Init_Core() is by raising
        // an error, at which point the system will Panic out.
        //
        // !!! TBD: Enforce not being *able* to trigger HALT
        //
        assert(ERR_NUM(error) != RE_HALT);
        panic (error);
    }

    Init_Base(base_array);

    Init_Sys(sys_array);

    PG_Boot_Phase = BOOT_MEZZ;

    assert(DSP == 0 && FS_TOP == NULL);

    // The FINISH-INIT-CORE function should likely do very little.  But right
    // now it is where the user context is created from the lib context (a
    // copy with some omissions), and where the mezzanine definitions are
    // bound to the lib context and DO'd.
    //
    DECLARE_LOCAL (result);
    if (Apply_Only_Throws(
        result,
        TRUE, // generate error if all arguments aren't consumed
        Sys_Func(SYS_CTX_FINISH_INIT_CORE), // %sys-start.r function to call
        mezz_block, // boot-mezz argument
        END
    )) {
        // You shouldn't be able to throw any uncaught values during
        // Init_Core() startup, including throws implementing QUIT or EXIT.
        // A fail() would just jump up to the error delivery above, so a
        // panic here more clearly indicates the moment of the problem.
        //
        panic (result);
    }

    if (!IS_VOID(result)) {
        //
        // !!! `finish-init-core` Rebol code should return void, but it may be
        // that more graceful error delivery than a panic should be given if
        // it does not.  It may be that fairly legitimate circumstances which
        // the user could fix would cause a more ordinary message delivery.
        // For the moment, though, we panic on any non-void return result.
        //
        panic (result);
    }

    DROP_GUARD_ARRAY(boot_array);

    assert(DSP == 0 && FS_TOP == NULL);

    DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

    PG_Boot_Phase = BOOT_DONE;

#if !defined(NDEBUG)
    //
    // This memory check from R3-Alpha is somewhat superfluous, but include a
    // call to it during Init in the debug build, because otherwise no one
    // will think to keep it up to date and working.
    //
    Check_Memory_Debug();

    // We can only do a check of the pointer detection service after the
    // system is somewhat initialized.
    //
    Assert_Pointer_Detection_Working();
#endif

    Recycle(); // necessary?
}


//
//  Shutdown_Core: C
//
// The goal of Shutdown_Core() is to release all memory and resources that the
// interpreter has accrued since Init_Core().  This is a good "sanity check"
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
    //
    // This memory check from R3-Alpha is somewhat superfluous, but include a
    // call to it during Shutdown in the debug build, because otherwise no one
    // will think to keep it up to date and working.
    //
    Check_Memory_Debug();
#endif

    assert(!Saved_State);

    Shutdown_Stacks();

    // Run Recycle, but the TRUE flag indicates we want every series
    // that is managed to be freed.  (Only unmanaged should be left.)
    // We remove the only two root contexts that the Init_Core process added
    // -however- there may be other roots.  But by this point, the roots
    // created by Alloc_Pairing() with an owning context should be freed.
    //
    AS_SERIES(CTX_VARLIST(PG_Root_Context))->header.bits
        &= (~NODE_FLAG_ROOT);
    AS_SERIES(CTX_VARLIST(TG_Task_Context))->header.bits
        &= (~NODE_FLAG_ROOT);
    Recycle_Core(TRUE, NULL);

    Shutdown_Event_Scheme();
    Shutdown_CRC();
    Shutdown_Mold();
    Shutdown_Scanner();
    Shutdown_Char_Cases();

    Shutdown_Symbols();

    Shutdown_GC();

    // !!! Need to review the relationship between Open_StdIO (which the host
    // does) and Init_StdIO...they both open, and both close.

    Shutdown_StdIO();

    FREE(REB_OPTS, Reb_Opts);

    // Shutting down the memory manager must be done after all the Free_Mem
    // calls have been made to balance their Alloc_Mem calls.
    //
    Shutdown_Pools();
}
