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
    REBFLGS flags;

    flags = FLAGIT_LEFT(0);
    unsigned char *ch = (unsigned char*)&flags;
    if (*ch != 128) {
        printf("Expected 128, got %d\n", *ch);
        panic ("Bad leftmost bit setting of platform unsigned integer.");
    }

    flags = FLAGIT_LEFT(0) | FLAGIT_LEFT(1) | FLAGBYTE_RIGHT(13);

    unsigned int left = LEFT_N_BITS(flags, 3); // == 6 (binary `110`)
    unsigned int right = RIGHT_N_BITS(flags, 3); // == 5 (binary `101`)
    if (left != 6 or right != 5) {
        printf("Expected 6 and 5, got %u and %u\n", left, right);
        panic ("Bad composed integer assignment for byte-ordering macro.");
    }
  #endif

    // !!! Should runtime debug be double-checking all of <stdint.h>?
    //
    assert(sizeof(uint32_t) == 4);

    // Although the system is designed to be able to function with REBVAL at
    // any size, the optimization of it being 4x(32-bit) on 32-bit platforms
    // and 4x(64-bit) on 64-bit platforms is a rather important performance
    // point.  For the moment we consider it to be essential enough to the
    // intended function of the system that it refuses to run if not true.
    //
    // But if someone is in an odd situation and understands why the size did
    // not work out as designed, defining UNUSUAL_REBVAL_SIZE should still
    // work, so long as that size is an even multiple of ALIGN_SIZE.
    //
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

    assert(sizeof(REBEVT) == sizeof(REBVAL));
  #endif

    // The REBSER is designed to place the `info` bits exactly after a REBVAL
    // so they can do double-duty as also a terminator for that REBVAL when
    // enumerated as an ARRAY.  Put the offest into a variable to avoid the
    // constant-conditional-expression warning.
    //
    size_t offsetof_REBSER_info = offsetof(REBSER, info);
    if (
        offsetof_REBSER_info - offsetof(REBSER, content) != sizeof(REBVAL)
    ){
        panic ("bad structure alignment for internal array termination");
    }

    // Void cells currently use REB_MAX for the type bits, and the debug
    // build uses REB_MAX + 1 for signaling "trash".  At most 64 "Reb_Kind"
    // types are used at the moment, yet the type is a byte for efficient
    // reading, so there's little danger of hitting this unless there's
    // a big change.
    //
    assert(REB_MAX_PLUS_ONE_TRASH < 256);

    // Make sure tricks for "internal END markers" are lined up as expected.
    //
    assert(SERIES_INFO_0_IS_TRUE == NODE_FLAG_NODE);
    assert(SERIES_INFO_1_IS_FALSE == NODE_FLAG_FREE);
    assert(SERIES_INFO_4_IS_TRUE == NODE_FLAG_END);
    assert(SERIES_INFO_7_IS_FALSE == NODE_FLAG_CELL);

    assert(DO_FLAG_0_IS_TRUE == NODE_FLAG_NODE);
    assert(DO_FLAG_1_IS_FALSE == NODE_FLAG_FREE);
    assert(DO_FLAG_4_IS_TRUE == NODE_FLAG_END);
    assert(DO_FLAG_7_IS_FALSE == NODE_FLAG_CELL);
}


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
    // like true, false, the natives, and the actions.  But before deeply
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
    // and execute it.  As a sanity check, it's expected the base block will
    // return no value when executed...hence it should end in `()`.

    Bind_Values_Deep(head, Lib_Context);

    DECLARE_LOCAL (result);
    if (Do_At_Throws(result, boot_base, 0, SPECIFIED))
        panic (result);

    if (not IS_VOID(result))
        panic (result);
}


//
//  Startup_Sys: C
//
// The SYS context contains supporting Rebol code for implementing "system"
// features.  The code has natives, actions, and the definitions from
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
    if (Do_At_Throws(result, boot_sys, 0, SPECIFIED))
        panic (result);

    if (not IS_VOID(result))
        panic (result);
}


//
//  Startup_Datatypes: C
//
// Create library words for each type, (e.g. make INTEGER! correspond to
// the integer datatype value).  Returns an array of words for the added
// datatypes to use in SYSTEM/CATALOG/DATATYPES
//
// Note the type enum starts at 1 (REB_ACTION), given that REB_0 is used
// for special purposes and not correspond to a user-visible type.  REB_MAX is
// used for void, and also not value type.  Hence the total number of types is
// REB_MAX - 1.
//
static REBARR *Startup_Datatypes(REBARR *boot_types, REBARR *boot_typespecs)
{
    if (ARR_LEN(boot_types) != REB_MAX - 1)
        panic (boot_types); // Every REB_XXX but REB_0 should have a WORD!

    RELVAL *word = ARR_HEAD(boot_types);

    if (VAL_WORD_SYM(word) != SYM_ACTION_X)
        panic (word); // First type should be ACTION!

    REBARR *catalog = Make_Array(REB_MAX - 1);

    REBINT n;
    for (n = 1; NOT_END(word); word++, n++) {
        assert(n < REB_MAX);

        REBVAL *value = Append_Context(Lib_Context, KNOWN(word), NULL);
        RESET_VAL_HEADER(value, REB_DATATYPE);
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
        assert(value == Datatype_From_Kind(cast(enum Reb_Kind, n)));
        SET_VAL_FLAG(CTX_VAR(Lib_Context, n), CELL_FLAG_PROTECTED);

        Append_Value(catalog, KNOWN(word));
    }

    return catalog;
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
    Init_Logic(true_value, true);
    assert(VAL_LOGIC(true_value) == true);
    assert(IS_TRUTHY(true_value));

    REBVAL *false_value = Append_Context(Lib_Context, 0, Canon(SYM_FALSE));
    Init_Logic(false_value, false);
    assert(VAL_LOGIC(false_value) == false);
    assert(IS_FALSEY(false_value));
}


//
//  action: enfix native [
//
//  {Creates datatype action (for internal usage only).}
//
//      return: [action!]
//      :verb [set-word! word!]
//      spec [block!]
//  ]
//
REBNATIVE(action)
//
// The `action` native is searched for explicitly by %make-natives.r and put
// in second place for initialization (after the `native` native).
//
// It is designed to be an enfix function that quotes its first argument,
// so when you write FOO: ACTION [...], the FOO: gets quoted to be the verb.
// The SET/ENFIX is done by the bootstrap, after the natives are loaded.
{
    INCLUDE_PARAMS_OF_ACTION;

    REBVAL *spec = ARG(spec);

    // We only want to check the return type in the debug build.  In the
    // release build, we want to have as few argument slots as possible...
    // especially to get the optimization for 1 argument to go in the cell
    // and not need to push arguments.
    //
    REBFLGS flags = MKF_KEYWORDS | MKF_FAKE_RETURN;

    REBACT *a = Make_Action(
        Make_Paramlist_Managed_May_Fail(spec, flags),
        &Type_Action_Dispatcher,
        NULL, // no facade (use paramlist)
        NULL // no specialization exemplar (or inherited exemplar)
    );

    Init_Word(ACT_BODY(a), VAL_WORD_CANON(ARG(verb)));

    // A lookback quoting function that quotes a SET-WORD! on its left is
    // responsible for setting the value if it wants it to change since the
    // SET-WORD! is not actually active.  But if something *looks* like an
    // assignment, it's good practice to evaluate the whole expression to
    // the result the SET-WORD! was set to, so `x: y: op z` makes `x = y`.
    //
    Move_Value(Sink_Var_May_Fail(ARG(verb), SPECIFIED), ACT_ARCHETYPE(a));
    Move_Value(D_OUT, ACT_ARCHETYPE(a));

    // !!! A very hacky (yet less hacky than R3-Alpha) re-dispatch of APPEND
    // as WRITE/APPEND on ports requires knowing what the WRITE action is.
    // Rather than track an entire table of all the actions in order to
    // support that and thus endorse this hack being used other places, just
    // save the write action into a global.
    //
    if (VAL_WORD_SYM(ARG(verb)) == SYM_WRITE) {
        Prep_Non_Stack_Cell(&PG_Write_Action);
        Move_Value(&PG_Write_Action, D_OUT);
    }

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

        "<=", // less than or equal !!! https://forum.rebol.info/t/349/11
        "=>", // Lambda function, quotes optional left argument

        ">=", // greater than or equal to
        "=<", // equal to or less than

        "<>", // not equal (the chosen meaning, as opposed to "empty tag")

        "->", // no current meaning
        "<-", // Non-null implicit GROUP! begin, e.g. `7 = 1 + <- 2 * 3`

        "|>", // Evaluate to next single expression, but do ones afterward
        "<|", // Evaluate to previous expression, but do rest (like ALSO)

        "/",

        NULL
    };

    REBCNT i;
    for (i = 0; names[i] != NULL; ++i) {
        REBSTR *str = Intern_UTF8_Managed(cb_cast(names[i]), strlen(names[i]));
        REBVAL *val = Append_Context(Lib_Context, NULL, str);
        Init_Void(val); // functions will fill in (no-op, since void already)
    }
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
    Root_With_Tag = rebLock(rebTag("with"), END);
    Root_Ellipsis_Tag = rebLock(rebTag("..."), END);
    Root_Opt_Tag = rebLock(rebTag("opt"), END);
    Root_End_Tag = rebLock(rebTag("end"), END);
    Root_Local_Tag = rebLock(rebTag("local"), END);
}

static void Shutdown_Action_Spec_Tags(void)
{
    rebRelease(Root_With_Tag);
    rebRelease(Root_Ellipsis_Tag);
    rebRelease(Root_Opt_Tag);
    rebRelease(Root_End_Tag);
    rebRelease(Root_Local_Tag);
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
    REBCTX *meta = Alloc_Context(REB_OBJECT, 6);
    REBCNT i = 1;
    for (; i != 7; ++i) {
        //
        // void is used as the default for fields (R3-Alpha makes them as
        // BLANK! on initialization, e.g. `make object! [x: ()]` is illegal).
        //
        Init_Void(Append_Context(meta, NULL, Canon(field_syms[i - 1])));
    }

    Init_Object(CTX_VAR(meta, 1), meta); // it's "selfish"

    Root_Action_Meta = Init_Object(Alloc_Value(), meta);
    rebLock(Root_Action_Meta, END);
}

static void Shutdown_Action_Meta_Shim(void) {
    rebRelease(Root_Action_Meta);
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
static REBARR *Startup_Natives(REBARR *boot_natives)
{
    // Must be called before first use of Make_Paramlist_Managed_May_Fail()
    //
    Init_Action_Meta_Shim();

    RELVAL *item = ARR_HEAD(boot_natives);

    // Although the natives are not being "executed", there are typesets
    // being built from the specs.  So to process `foo: native [x [integer!]]`
    // the INTEGER! word must be bound to its datatype.  Deep walk the
    // natives in order to bind these datatypes.
    //
    Bind_Values_Deep(item, Lib_Context);

    REBARR *catalog = Make_Array(Num_Natives);

    REBCNT n = 0;
    REBVAL *action_word = NULL; // for giving clear error if ACTION not found

    while (NOT_END(item)) {
        if (n >= Num_Natives)
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
        if (not IS_SET_WORD(item))
            panic (item);

        REBVAL *name = KNOWN(item);
        ++item;

        REBOOL enfix;
        if (IS_WORD(item) and VAL_WORD_SYM(item) == SYM_ENFIX) {
            enfix = true;
            ++item;
        }
        else
            enfix = false;

        // See if it's being invoked with NATIVE or NATIVE/BODY
        //
        REBOOL has_body;
        if (IS_WORD(item)) {
            if (VAL_WORD_SYM(item) != SYM_NATIVE)
                panic (item);
            has_body = false;
        }
        else {
            if (
                not IS_PATH(item)
                or VAL_LEN_HEAD(item) != 2
                or not IS_WORD(ARR_HEAD(VAL_ARRAY(item)))
                or VAL_WORD_SYM(ARR_HEAD(VAL_ARRAY(item))) != SYM_NATIVE
                or not IS_WORD(ARR_AT(VAL_ARRAY(item), 1))
                or VAL_WORD_SYM(ARR_AT(VAL_ARRAY(item), 1)) != SYM_BODY
            ){
                panic (item);
            }
            has_body = true;
        }
        ++item;

        REBVAL *spec = KNOWN(item);
        ++item;
        if (not IS_BLOCK(spec))
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

        REBACT *act = Make_Action(
            Make_Paramlist_Managed_May_Fail(KNOWN(spec), flags),
            Native_C_Funcs[n], // "dispatcher" is unique to this "native"
            NULL, // no facade (use paramlist)
            NULL // no specialization exemplar (or inherited exemplar)
        );

        // If a user-equivalent body was provided, we save it in the native's
        // REBVAL for later lookup.
        //
        if (has_body) {
            REBVAL *body = KNOWN(item); // !!! handle relative?
            ++item;
            if (not IS_BLOCK(body))
                panic (body);
            Move_Value(ACT_BODY(act), body);
        }

        Prep_Non_Stack_Cell(&Natives[n]);
        Move_Value(&Natives[n], ACT_ARCHETYPE(act));

        // Append the native to the Lib_Context under the name given.
        //
        REBVAL *var = Append_Context(Lib_Context, name, 0);
        Move_Value(var, &Natives[n]);
        if (enfix)
            SET_VAL_FLAG(var, VALUE_FLAG_ENFIXED);

        REBVAL *catalog_item = Alloc_Tail_Array(catalog);
        Move_Value(catalog_item, name);
        VAL_SET_TYPE_BITS(catalog_item, REB_WORD);

        if (VAL_WORD_SYM(name) == SYM_ACTION)
            action_word = name;

        ++n;
    }

    if (n != Num_Natives)
        panic ("Incorrect number of natives found during processing");

    if (not action_word)
        panic ("ACTION native not found during boot block processing");

    return catalog;
}


//
//  Startup_Actions: C
//
// Returns an array of words bound to actions for SYSTEM/CATALOG/ACTIONS
//
static REBARR *Startup_Actions(REBARR *boot_actions)
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

    if (not IS_VOID(result))
        panic (result);

    // Sanity check the symbol transformation
    //
    if (0 != strcmp("open", STR_HEAD(Canon(SYM_OPEN))))
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

    Prep_Non_Stack_Cell(&PG_Void_Cell[0]);
    Prep_Non_Stack_Cell(&PG_Void_Cell[1]);
    Init_Void(&PG_Void_Cell[0]);
    TRASH_CELL_IF_DEBUG(&PG_Void_Cell[1]);

    Prep_Non_Stack_Cell(&PG_Blank_Value[0]);
    Prep_Non_Stack_Cell(&PG_Blank_Value[1]);
    Init_Blank(&PG_Blank_Value[0]);
    TRASH_CELL_IF_DEBUG(&PG_Blank_Value[1]);

    Prep_Non_Stack_Cell(&PG_Bar_Value[0]);
    Prep_Non_Stack_Cell(&PG_Bar_Value[1]);
    Init_Bar(&PG_Bar_Value[0]);
    TRASH_CELL_IF_DEBUG(&PG_Bar_Value[1]);

    Prep_Non_Stack_Cell(&PG_False_Value[0]);
    Prep_Non_Stack_Cell(&PG_False_Value[1]);
    Init_Logic(&PG_False_Value[0], false);
    TRASH_CELL_IF_DEBUG(&PG_False_Value[1]);

    Prep_Non_Stack_Cell(&PG_True_Value[0]);
    Prep_Non_Stack_Cell(&PG_True_Value[1]);
    Init_Logic(&PG_True_Value[0], true);
    TRASH_CELL_IF_DEBUG(&PG_True_Value[1]);

    // We can't actually put an end value in the middle of a block, so we poke
    // this one into a program global.  It is not legal to bit-copy an
    // END (you always use SET_END), so we can make it unwritable.
    //
    Init_Endlike_Header(&PG_End_Node.header, 0); // mutate to read-only end
  #if defined(DEBUG_TRACK_CELLS)
    Set_Track_Payload_Debug(&PG_End_Node, __FILE__, __LINE__);
  #endif
    assert(IS_END(END)); // sanity check that it took
    assert(VAL_TYPE_RAW(END) == REB_0); // this implicit END marker has this

    // Note: Not only can rebBlock() not be used yet (because there is no
    // data stack), PG_Unbound_Singular must exist before calling Init_Block()
    //
    // !!! Users should never see this, and if you panic() on this series it
    // will point to this line of code to identify it.  Still, review putting
    // something in the block to make it stand out in a debug dump, like a
    // `#PG_Unbound_Singular` ISSUE! (mold buffer not initialized yet)
    //
    PG_Unbound_Singular = Alloc_Singular_Array();
    SET_SER_INFO(PG_Unbound_Singular, SERIES_INFO_FROZEN);
    SET_END(ARR_SINGLE(PG_Unbound_Singular));
    Root_Unbound = Init_Block(Alloc_Value(), PG_Unbound_Singular);
    rebLock(Root_Unbound, END);

    // Generic read-only empty array, and locked block containing it.
    //
    PG_Empty_Array = Make_Array_Core(0, SERIES_FLAG_FIXED_SIZE);
    SET_SER_INFO(PG_Empty_Array, SERIES_INFO_FROZEN);
    Root_Empty_Block = Init_Block(Alloc_Value(), PG_Empty_Array);
    rebLock(Root_Empty_Block, END);

    // Note: rebText() can't run yet, review.
    //
    REBSER *nulled_uni = Make_Unicode(1);
    assert(CHR_CODE(UNI_AT(nulled_uni, 0)) == '\0');
    assert(UNI_LEN(nulled_uni) == 0);
    Root_Empty_String = Init_Text(Alloc_Value(), nulled_uni);
    rebLock(Root_Empty_String, END);

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
    Root_Stats_Map = NULL;

    rebRelease(Root_Space_Char);
    Root_Space_Char = NULL;
    rebRelease(Root_Newline_Char);
    Root_Newline_Char = NULL;

    rebRelease(Root_Unbound);
    Root_Unbound = NULL;
    rebRelease(Root_Empty_String);
    Root_Empty_String = NULL;
    rebRelease(Root_Empty_Block);
    Root_Empty_Block = NULL;
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
    if (not IS_VOID(result))
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

    // Create system/catalog/* for datatypes, natives, actions, errors
    //
    Init_Block(Get_System(SYS_CATALOG, CAT_DATATYPES), datatypes_catalog);
    Init_Block(Get_System(SYS_CATALOG, CAT_NATIVES), natives_catalog);
    Init_Block(Get_System(SYS_CATALOG, CAT_ACTIONS), actions_catalog);
    Init_Object(Get_System(SYS_CATALOG, CAT_ERRORS), errors_catalog);

    // Create system/codecs object
    //
    Init_Object(Get_System(SYS_CODECS, 0), Alloc_Context(REB_OBJECT, 10));

    // The "standard error" template was created as an OBJECT!, because the
    // `make error!` functionality is not ready when %sysobj.r runs.  Fix
    // up its archetype so that it is an actual ERROR!.
    //
    REBVAL *std_error = Get_System(SYS_STANDARD, STD_ERROR);
    assert(IS_OBJECT(std_error));
    VAL_SET_TYPE_BITS(std_error, REB_ERROR);
    VAL_SET_TYPE_BITS(CTX_ARCHETYPE(VAL_CONTEXT(std_error)), REB_ERROR);
    assert(CTX_KEY_SYM(VAL_CONTEXT(std_error), 1) == SYM_SELF);
    VAL_SET_TYPE_BITS(VAL_CONTEXT_VAR(std_error, 1), REB_ERROR);
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
    DROP_GUARD_CONTEXT(Sys_Context);
    Init_Object(Get_System(SYS_CONTEXTS, CTX_SYS), Sys_Context);

    DROP_GUARD_CONTEXT(Lib_Context);
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

    Startup_Stacks(STACK_MIN / 4);

    TG_Ballast = MEM_BALLAST;
    TG_Max_Ballast = MEM_BALLAST;

    // The thrown arg is not intended to ever be around long enough to be
    // seen by the GC.
    //
    Prep_Non_Stack_Cell(&TG_Thrown_Arg);
    Init_Unreadable_Blank(&TG_Thrown_Arg);

    Startup_Raw_Print();
    Startup_Scanner();
    Startup_Mold(MIN_COMMON / 4);
    Startup_String();
    Startup_Collector();
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
    REBOOL Guess_If_Stack_Grows_Up(int *p) {
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

static REBVAL *Startup_Mezzanine(BOOT_BLK *boot);


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
// in Rebol.  For instance, ACTION is a function registered very early on in
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

//==//////////////////////////////////////////////////////////////////////==//
//
// INITIALIZE STACK MARKER METRICS
//
//==//////////////////////////////////////////////////////////////////////==//

    // !!! See notes on Set_Stack_Limit() about the dodginess of this
    // approach.  Note also that even with a single evaluator used on multiple
    // threads, you have to trap errors to make sure an attempt is not made
    // to longjmp the state to an address from another thread--hence every
    // thread switch must also be a site of trapping all errors.  (Or the
    // limit must be saved in thread local storage.)

    int dummy; // variable whose address acts as base of stack for below code
    Set_Stack_Limit(&dummy);

//==//////////////////////////////////////////////////////////////////////==//
//
// INITIALIZE BASIC DIAGNOSTICS
//
//==//////////////////////////////////////////////////////////////////////==//

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

    Startup_StdIO();

    Assert_Basics();
    PG_Boot_Time = OS_DELTA_TIME(0);

//==//////////////////////////////////////////////////////////////////////==//
//
// INITIALIZE MEMORY AND ALLOCATORS
//
//==//////////////////////////////////////////////////////////////////////==//

    Startup_Pools(0);          // Memory allocator
    Startup_GC();

//==//////////////////////////////////////////////////////////////////////==//
//
// INITIALIZE API
//
//==//////////////////////////////////////////////////////////////////////==//

    // The API is one means by which variables can be made whose lifetime is
    // indefinite until program shutdown.  In R3-Alpha this was done with
    // boot code that laid out some fixed structure arrays, but it's more
    // general to do it this way.

    Init_Char_Cases();
    Startup_CRC();             // For word hashing
    Set_Random(0);
    Startup_Interning();

    Startup_Api();

//==//////////////////////////////////////////////////////////////////////==//
//
// CREATE GLOBAL OBJECTS
//
//==//////////////////////////////////////////////////////////////////////==//

    Init_Root_Vars();    // Special REBOL values per program

//==//////////////////////////////////////////////////////////////////////==//
//
// INITIALIZE (SINGULAR) TASK
//
//==//////////////////////////////////////////////////////////////////////==//

    Startup_Task();

    Init_Action_Spec_Tags(); // Note: uses BUF_UTF8, not available until here

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

    REBCNT utf8_size;
    const REBINT max = -1; // trust size in gzip data
    REBYTE *utf8 = rebGunzipAlloc(
        &utf8_size,
        Native_Specs,
        Nat_Compressed_Size,
        max
    );

    REBARR *boot_array = Scan_UTF8_Managed(
        Intern("tmp-boot.r"),
        utf8,
        utf8_size
    );
    PUSH_GUARD_ARRAY(boot_array); // managed, so must be guarded

    rebFree(utf8); // don't need decompressed text after it's scanned

    BOOT_BLK *boot = cast(BOOT_BLK*, VAL_ARRAY_HEAD(ARR_HEAD(boot_array)));

    Startup_Symbols(VAL_ARRAY(&boot->words));

    // STR_SYMBOL(), VAL_WORD_SYM() and Canon(SYM_XXX) now available

    PG_Boot_Phase = BOOT_LOADED;

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
    Lib_Context = Alloc_Context(REB_OBJECT, 600);
    MANAGE_ARRAY(CTX_VARLIST(Lib_Context));
    PUSH_GUARD_CONTEXT(Lib_Context);

    Sys_Context = Alloc_Context(REB_OBJECT, 50);
    MANAGE_ARRAY(CTX_VARLIST(Sys_Context));
    PUSH_GUARD_CONTEXT(Sys_Context);

    REBARR *datatypes_catalog = Startup_Datatypes(
        VAL_ARRAY(&boot->types), VAL_ARRAY(&boot->typespecs)
    );
    MANAGE_ARRAY(datatypes_catalog);
    PUSH_GUARD_ARRAY(datatypes_catalog);

    // !!! REVIEW: Startup_Typesets() uses symbols, data stack, and
    // adds words to lib--not available untilthis point in time.
    //
    Startup_Typesets();

    Startup_True_And_False();
    Add_Lib_Keys_R3Alpha_Cant_Make();

//==//////////////////////////////////////////////////////////////////////==//
//
// RUN CODE BEFORE ERROR HANDLING INITIALIZED
//
//==//////////////////////////////////////////////////////////////////////==//

    // Initialize the "Do" handler to the default, Do_Core(), and the "Apply"
    // of an ACTION! handler to Apply_Core().  These routines have no
    // tracing, no debug handling, etc.  If those features are needed, an
    // augmented function must be substituted.
    //
    PG_Do = &Do_Core;
    PG_Apply = &Apply_Core;

    // boot->natives is from the automatically gathered list of natives found
    // by scanning comments in the C sources for `native: ...` declarations.
    //
    REBARR *natives_catalog = Startup_Natives(VAL_ARRAY(&boot->natives));
    MANAGE_ARRAY(natives_catalog);
    PUSH_GUARD_ARRAY(natives_catalog);

    // boot->actions is the list in %actions.r
    //
    REBARR *actions_catalog = Startup_Actions(VAL_ARRAY(&boot->actions));
    MANAGE_ARRAY(actions_catalog);
    PUSH_GUARD_ARRAY(actions_catalog);

    // boot->errors is the error definition list from %errors.r
    //
    REBCTX *errors_catalog = Startup_Errors(VAL_ARRAY(&boot->errors));
    PUSH_GUARD_CONTEXT(errors_catalog);

    Init_System_Object(
        VAL_ARRAY(&boot->sysobj),
        datatypes_catalog,
        natives_catalog,
        actions_catalog,
        errors_catalog
    );

    DROP_GUARD_CONTEXT(errors_catalog);
    DROP_GUARD_ARRAY(actions_catalog);
    DROP_GUARD_ARRAY(natives_catalog);
    DROP_GUARD_ARRAY(datatypes_catalog);

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

//==//////////////////////////////////////////////////////////////////////==//
//
// RUN MEZZANINE CODE NOW THAT ERROR HANDLING IS INITIALIZED
//
//==//////////////////////////////////////////////////////////////////////==//

    PG_Boot_Phase = BOOT_MEZZ;

    assert(DSP == 0 and FS_TOP == NULL);

    REBVAL *error = rebRescue(cast(REBDNG*, &Startup_Mezzanine), boot);
    if (error != NULL) {
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

      #ifdef RETURN_ERRORS_FROM_INIT_CORE
        REBCNT err_num = VAL_ERR_NUM(error);
        Shutdown_Core(); // In good enough state to shutdown cleanly by now
        return err_num;
      #endif

        panic (error);
    }

    assert(DSP == 0 and FS_TOP == NULL);

    DROP_GUARD_ARRAY(boot_array);

    PG_Boot_Phase = BOOT_DONE;

  #if !defined(NDEBUG)
    Check_Memory_Debug(); // old R3-Alpha check, call here to keep it working
    Assert_Pointer_Detection_Working(); // can't be done too early in boot
  #endif

    Recycle(); // necessary?
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
    if (Apply_Only_Throws(
        result,
        true, // fully = true (error if all arguments aren't consumed)
        finish_init, // %sys-start.r function to call
        KNOWN(&boot->mezz), // boot-mezz argument
        END
    )){
        fail (Error_No_Catch_For_Throw(result));
    }

    if (not IS_VOID(result))
        panic (result); // FINISH-INIT-CORE returns void by convention

    return NULL;
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

    Shutdown_Stacks();

    Shutdown_Stackoverflow();
    Shutdown_System_Object();
    Shutdown_Typesets();

    Shutdown_Action_Meta_Shim();
    Shutdown_Action_Spec_Tags();
    Shutdown_Root_Vars();

    const REBOOL shutdown = true; // go ahead and free all managed series
    Recycle_Core(shutdown, NULL);

    Shutdown_Mold();
    Shutdown_Collector();
    Shutdown_Raw_Print();
    Shutdown_Event_Scheme();
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
