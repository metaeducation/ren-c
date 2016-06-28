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
// Copyright 2012-2016 Rebol Open Source Contributors
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
#include "mem-pools.h"

#define EVAL_DOSE 10000

// Boot Vars used locally:
static  REBCNT  Action_Index;
static  REBCNT  Action_Marker;
static  BOOT_BLK *Boot_Block;


#ifdef WATCH_BOOT
#define DOUT(s) puts(s)
#else
#define DOUT(s)
#endif


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
    printf(fmt, sizeof(dummy_payload->symbol), "symbol");
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
    if (sizeof(void *) == 8) {
        if (sizeof(REBVAL) != 32)
            panic (Error(RE_REBVAL_ALIGNMENT));
    }
    else {
        if (sizeof(REBVAL) != 16)
            panic (Error(RE_REBVAL_ALIGNMENT));
    }

    // In the original conception of R3-Alpha, performance of the graphics
    // layer was considered very important...and so the GUI would make use
    // of the custom memory-pooled heap for its "(G)raphic (OB)jects", even
    // though much of the GUI code itself was written in extensions.  With
    // the Ren-C branch, the focus is on a more "essential" core.  So the
    // hope is to either provide generic pooled memory services or have the
    // graphics layer implement its own allocator--or just use malloc()/new
    //
    // But given that external code depends on binary compatibility with an
    // understanding of what size a GOB is, this helps enforce that by
    // checking that the size is what the linked-to code is expecting.
    //
    if (sizeof(void *) == 8) {
        if (sizeof(REBGOB) != 88)
            panic (Error(RE_BAD_SIZE));
    }
    else {
        if (sizeof(REBGOB) != 64)
            panic (Error(RE_BAD_SIZE));
    }

    // This checks the size of the `struct reb_date`.
    // !!! Why this, in particular?
    //
    if (sizeof(REBDAT) != 4)
        panic (Error(RE_BAD_SIZE));

    // The REBSER is designed to place the `info` bits exactly after a REBVAL
    // so they can do double-duty as also a terminator for that REBVAL when
    // enumerated as an ARRAY.
    //
    if (
        offsetof(struct Reb_Series, info)
            - offsetof(struct Reb_Series, content)
        != sizeof(REBVAL)
    ){
        panic (Error(RE_MISC));
    }

    // Check special return values used "in-band" in an unsigned integer that
    // is otherwise used for indices.  Make sure they don't overlap.
    //
    assert(THROWN_FLAG != END_FLAG);
    assert(NOT_FOUND != END_FLAG);
    assert(NOT_FOUND != THROWN_FLAG);

    // The END marker logic currently uses REB_MAX for the type bits.  That's
    // okay up until the REB_MAX bumps to 256.  If you hit this then some
    // other value needs to be chosen in the debug build to represent the
    // type value for END's bits.  (REB_TRASH is just a poor choice, because
    // you'd like to catch IS_END() tests on trash.)
    //
    assert(REB_MAX < 256);

    // The "Indexor" type in the C++ build has some added checking to make
    // sure that the special values used for indicating THROWN_FLAG or
    // END_FLAG etc. don't leak out into the REBCNT stored in things like
    // blocks.  We want the C++ class to be the same size as the C build.
    //
    assert(sizeof(REBIXO) == sizeof(REBUPT));

    // Types that are used for memory pooled allocations are required to be
    // multiples of 8 bytes in size.  This way it's possible to reliably align
    // 64-bit values using the node's allocation pointer as a baseline that
    // is known to be 64-bit aligned.  (Rounding internally to the allocator
    // would be possible, but that would add calculation as well as leading
    // to wasting space--whereas this way any padding is visible.)
    //
    // This check is reinforced in the pool initialization itself.
    //
    assert(sizeof(REBI64) == 8);
    assert(sizeof(REBSER) % 8 == 0);
    assert(sizeof(REBGOB) % 8 == 0);
    assert(sizeof(REBRIN) % 8 == 0);
}


//
//  Print_Banner: C
//
static void Print_Banner(REBARGS *rargs)
{
    if (rargs->options & RO_VERS) {
        Debug_Fmt(Str_Banner, REBOL_VER, REBOL_REV, REBOL_UPD, REBOL_SYS, REBOL_VAR);
        OS_EXIT(0);
    }
}


//
//  Do_Global_Block: C
// 
// Bind and evaluate a global block.
// Rebind:
//     0: bind set into sys or lib
//    -1: bind shallow into sys (for ACTION)
//     1: add new words to LIB, bind/deep to LIB
//     2: add new words to SYS, bind/deep to LIB
// 
// Expects result to be void
//
static void Do_Global_Block(
    REBARR *block,
    REBCNT index,
    REBINT rebind,
    REBVAL *opt_toplevel_word
) {
    RELVAL *item = ARR_AT(block, index);
    struct Reb_State state;

    REBVAL result;

    Bind_Values_Set_Midstream_Shallow(
        item, rebind > 1 ? Sys_Context : Lib_Context
    );

    if (rebind < 0) Bind_Values_Shallow(item, Sys_Context);
    if (rebind > 0) Bind_Values_Deep(item, Lib_Context);
    if (rebind > 1) Bind_Values_Deep(item, Sys_Context);

    // !!! In the block, ACTION words are bound but paths would not bind.
    // So you could do `action [spec]` but not `action/xxx [spec]`
    // because in the later case, it wouldn't have descended into the path
    // to bind `native`.  This is apparently intentional to avoid binding
    // deeply in the system context.
    //
    // Here we hackily walk over all the paths and look for any that start
    // with the symbol of the passed in word (SYM_ACTION) and just overwrite
    // the word with the bound vesion.  :-/  Review.
    //
    if (opt_toplevel_word) {
        item = ARR_AT(block, index);
        for (; NOT_END(item); item++) {
            if (IS_PATH(item)) {
                RELVAL *path_item = VAL_ARRAY_HEAD(item);
                if (
                    IS_WORD(path_item) && (
                        VAL_WORD_CASED(path_item)
                        == VAL_WORD_CASED(opt_toplevel_word)
                    )
                ) {
                    // Steal binding, but keep the same word type
                    //
                    enum Reb_Kind kind = VAL_TYPE(path_item);
                    *path_item = *opt_toplevel_word;
                    VAL_SET_TYPE_BITS(path_item, kind);
                }
            }
        }
    }

    SNAP_STATE(&state);

    if (Do_At_Throws(&result, block, index, SPECIFIED))
        panic (Error_No_Catch_For_Throw(&result));

    ASSERT_STATE_BALANCED(&state);

    if (!IS_VOID(&result))
        panic (Error(RE_MISC));
}


//
//  Load_Boot: C
// 
// Decompress and scan in the boot block structure.  Can
// only be called at the correct point because it will
// create new symbols.
//
static void Load_Boot(void)
{
    REBARR *boot;
    REBSER *text;

    // Decompress binary data in Native_Specs to get the textual source
    // of the function specs for the native routines into `boot` series.
    //
    // (Native_Specs array is in b-boot.c, auto-generated by make-boot.r)

    text = Decompress(
        Native_Specs, NAT_COMPRESSED_SIZE, NAT_UNCOMPRESSED_SIZE, FALSE, FALSE
    );

    if (!text || (SER_LEN(text) != NAT_UNCOMPRESSED_SIZE))
        panic (Error(RE_BOOT_DATA));

    boot = Scan_Source(BIN_HEAD(text), NAT_UNCOMPRESSED_SIZE);
    Free_Series(text);

    // Do not let it get GC'd
    //
    Set_Root_Series(ROOT_BOOT, ARR_SERIES(boot));

    Boot_Block = cast(BOOT_BLK *, VAL_ARRAY_HEAD(ARR_HEAD(boot)));

    // There should be a datatype word for every REB_XXX type except REB_0
    //
    if (VAL_LEN_HEAD(&Boot_Block->types) != REB_MAX_0 - 1)
        panic (Error(RE_BAD_BOOT_TYPE_BLOCK));

    // First type should be BLANK! (Note: Init_Symbols() hasn't run yet, so
    // cannot check this via VAL_WORD_SYM())
    //
    if (0 != COMPARE_BYTES(
        cb_cast("blank!"),
        STR_HEAD(VAL_WORD_CASED(VAL_ARRAY_HEAD(&Boot_Block->types)))
    )){
        Debug_Fmt(cs_cast(STR_HEAD(VAL_WORD_CASED(VAL_ARRAY_HEAD(&Boot_Block->types)))));
        panic (Error(RE_BAD_BOOT_TYPE_BLOCK));
    }

    // Create low-level string pointers (used by RS_ constants):
    {
        PG_Boot_Strs = ALLOC_N(REBYTE *, RS_MAX);
        *ROOT_STRINGS = Boot_Block->strings;

        REBYTE *cp = VAL_BIN(ROOT_STRINGS);
        REBINT i;
        for (i = 0; i < RS_MAX; i++) {
            PG_Boot_Strs[i] = cp;
            while (*cp++);
        }
    }

    if (COMPARE_BYTES(cb_cast("newline"), BOOT_STR(RS_SCAN, 1)) != 0)
        panic (Error(RE_BAD_BOOT_STRING));
}


//
//  Init_Datatypes: C
// 
// Create the datatypes.
//
static void Init_Datatypes(void)
{
    RELVAL *word = VAL_ARRAY_HEAD(&Boot_Block->types);
    REBARR *specs = VAL_ARRAY(&Boot_Block->typespecs);

    REBINT n;

    for (n = 1; NOT_END(word); word++, n++) {
        assert(n < REB_MAX_0);

        REBVAL *value = Append_Context(Lib_Context, KNOWN(word), NULL);
        VAL_RESET_HEADER(value, REB_DATATYPE);
        VAL_TYPE_KIND(value) = KIND_FROM_0(n);
        VAL_TYPE_SPEC(value) = VAL_ARRAY(ARR_AT(specs, n - 1));

        // !!! The system depends on these definitions, as they are used by
        // Get_Type and Type_Of.  Although it is convenient to be able to
        // get a REBVAL of a type and not have to worry about its lifetime
        // management or stack-allocate the value and fill it, that's
        // probably not a frequent enough need to justify putting it in
        // a public slot and either leave it mutable (dangerous) or lock it
        // (inflexible).  Better to create a new type value each time.
        //
        // (Another possibility would be to use the "datatypes catalog",
        // which appears to copy this subset out from the Lib_Context.)

        assert(value == Get_Type(KIND_FROM_0(n)));
        SET_VAL_FLAG(CTX_KEY(Lib_Context, 1), TYPESET_FLAG_LOCKED);
    }
}


//
//  Init_Constants: C
// 
// Init constant words.
// 
// WARNING: Do not create direct pointers into the Lib_Context
// because it may get expanded and the pointers will be invalid.
//
static void Init_Constants(void)
{
    REBVAL *value;
    extern const double pi1;

    value = Append_Context(Lib_Context, 0, Canon(SYM_BLANK));
    SET_BLANK(value);
    assert(IS_BLANK(value));
    assert(IS_CONDITIONAL_FALSE(value));

    value = Append_Context(Lib_Context, 0, Canon(SYM_TRUE));
    SET_TRUE(value);
    assert(VAL_LOGIC(value));
    assert(IS_CONDITIONAL_TRUE(value));

    value = Append_Context(Lib_Context, 0, Canon(SYM_FALSE));
    SET_FALSE(value);
    assert(!VAL_LOGIC(value));
    assert(IS_CONDITIONAL_FALSE(value));

    value = Append_Context(Lib_Context, 0, Canon(SYM_PI));
    SET_DECIMAL(value, pi1);
    assert(IS_DECIMAL(value));
    assert(IS_CONDITIONAL_TRUE(value));
}


//
//  has-type?: native [
//
//  {Checks to see if a value has a type or is in a typeset.}
//
//      type [datatype! typeset!]
//      value [<opt> any-value!]
//  ]
//
REBNATIVE(has_type_q)
{
    PARAM(1, type);
    PARAM(2, value);

    REBVAL *value = ARG(value);
    if (IS_VOID(value))
        return R_FALSE;

    REBVAL *type = ARG(type);

    if (IS_DATATYPE(type)) {
        if (VAL_TYPE(value) == VAL_TYPE_KIND(type))
            return R_TRUE;
    }
    else {
        assert(IS_TYPESET(type));
        if (TYPE_CHECK(type, VAL_TYPE(value)))
            return R_TRUE;
    }

    return R_FALSE;
}


//
//  action: native [
//
//  {Creates datatype action (for internal usage only).}
//
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
    PARAM(1, verb);
    PARAM(2, spec);

    REBVAL *spec = ARG(spec);

    REBFUN *fun = Make_Function(
        Make_Paramlist_Managed_May_Fail(spec, MKF_KEYWORDS),
        &Action_Dispatcher,
        NULL // no underlying function--this is fundamental
    );

    *FUNC_BODY(fun) = *ARG(verb);

    *D_OUT = *FUNC_VALUE(fun);
    return R_OUT;
}


//
//  Init_Ops: C
//
// This contains the weird words that the lexer doesn't easily let us make
// set-words out of, like /: and //: and <>:.  These shouldn't be processed
// by %make-boot.r if the legal exceptions are going to expand, because that
// would mean R3-Alpha couldn't be used to build Ren-C.  So it's better to
// have the exceptions hardcoded here.
//
static void Init_Ops(void)
{
    const char *names[] = {
        "<",
        "<=",
        ">",
        ">=",
        "<>", // may ultimately be targeted for empty tag in Ren-Cs
        "->", // FUNCTION-style lambda ("reaches in")
        "<-", // FUNC-style lambda ("reaches out"),
        "|>", // Evaluate to next single expression, but do ones afterward
        "<|", // Evaluate to previous expression, but do rest (like ALSO)
        "/",
        "//", // is remainder in R3-Alpha :-/
        NULL
    };

    REBINT i = 0;
    while (names[i]) {
        REBSTR *str = Intern_UTF8_Managed(cb_cast(names[i]), strlen(names[i]));

        // Append the operator name to the lib frame:
        REBVAL *val = Append_Context(Lib_Context, NULL, str);

        // leave void, functions will be filled in later...
        cast(void, cast(REBUPT, val));
        ++i;
    }
}


//
//  Init_Natives: C
// 
// Create native functions.  In R3-Alpha this would go as far as actually
// creating a NATIVE native by hand, and then run code that would call that
// native for each function.  Ren-C depends on having the native table
// initialized to run the evaluator (for instance to test functions against
// the RETURN native's FUNC signature in definitional returns).  So it
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
static void Init_Natives(void)
{
    // !!! See notes on FUNCTION-META in %sysobj.r
    {
        REBCTX *function_meta = Alloc_Context(3);
        Append_Context(function_meta, NULL, Canon(SYM_DESCRIPTION));
        Append_Context(function_meta, NULL, Canon(SYM_PARAMETER_TYPES));
        Append_Context(function_meta, NULL, Canon(SYM_PARAMETER_NOTES));
        REBVAL *rootvar = CTX_VALUE(function_meta);
        VAL_RESET_HEADER(rootvar, REB_OBJECT);
        rootvar->extra.binding = NULL;
        Val_Init_Object(ROOT_FUNCTION_META, function_meta);
    }

    // !!! Same, we want to have SPECIALIZE before %sysobj.r loaded
    {
        REBCTX *specialized_meta = Alloc_Context(3);
        Append_Context(specialized_meta, NULL, Canon(SYM_DESCRIPTION));
        Append_Context(specialized_meta, NULL, Canon(SYM_SPECIALIZEE));
        Append_Context(specialized_meta, NULL, Canon(SYM_SPECIALIZEE_NAME));
        REBVAL *rootvar = CTX_VALUE(specialized_meta);
        VAL_RESET_HEADER(rootvar, REB_OBJECT);
        rootvar->extra.binding = NULL;
        Val_Init_Object(ROOT_SPECIALIZED_META, specialized_meta);
    }

    RELVAL *item = VAL_ARRAY_HEAD(&Boot_Block->natives);
    REBCNT n = 0;
    REBVAL *action_word;

    while (NOT_END(item)) {
        if (n >= NUM_NATIVES)
            fail (Error(RE_MAX_NATIVES));

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
            panic (Error(RE_NATIVE_BOOT));

        REBVAL *name = KNOWN(item);
        ++item;

        // See if it's being invoked with NATIVE or NATIVE/BODY
        //
        REBOOL has_body;
        if (IS_WORD(item)) {
            if (VAL_WORD_SYM(item) != SYM_NATIVE)
                panic (Error(RE_NATIVE_BOOT));
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
                panic (Error(RE_NATIVE_BOOT));
            }
            has_body = TRUE;
        }
        ++item;

        // Grab the spec, and turn any <opt> into _ (the spec processor is low
        // level and does not understand <opt>)
        //
        if (!IS_BLOCK(item))
            panic (Error(RE_NATIVE_BOOT));

        REBVAL *spec = KNOWN(item);
        assert(VAL_INDEX(spec) == 0); // must be at head (we don't copy)
        ++item;

        // With the components extracted, generate the native and add it to
        // the Natives table.  The associated C function is provided by a
        // table built in the bootstrap scripts, `Native_C_Funcs`.

        REBFUN *fun = Make_Function(
            Make_Paramlist_Managed_May_Fail(KNOWN(spec), MKF_KEYWORDS),
            Native_C_Funcs[n], // "dispatcher" is unique to this "native"
            NULL // no underlying function, this is fundamental
        );

        // If a user-equivalent body was provided, we save it in the native's
        // REBVAL for later lookup.
        //
        if (has_body) {
            if (!IS_BLOCK(item))
                panic (Error(RE_NATIVE_BOOT));
            *FUNC_BODY(fun) = *KNOWN(item); // !!! handle relative?
            ++item;
        }

        Natives[n] = *FUNC_VALUE(fun);

        // Append the native to the Lib_Context under the name given.  Do
        // special case SET/LOOKBACK=TRUE (using Append_Context_Core) so
        // that SOME-ACTION: ACTION [...] allows ACTION to see the SOME-ACTION
        // symbol, and know to use it.
        //
        if (VAL_WORD_SYM(name) == SYM_ACTION) {
            *Append_Context_Core(Lib_Context, name, 0, TRUE) = Natives[n];
             action_word = name; // was bound by Append_Context_Core
        }
        else
            *Append_Context(Lib_Context, name, 0) = Natives[n];

        ++n;
    }

    if (n != NUM_NATIVES)
        fail (Error(RE_NATIVE_BOOT));

    // Should have found and bound `action:` among the natives
    //
    if (!action_word)
        panic (Error(RE_NATIVE_BOOT));

    // Save index for action words.  This is used by Get_Action_Sym().  We
    // have to subtract to account for the natives.
    //
    Action_Marker = CTX_LEN(Lib_Context);

    // With the natives registered (including ACTION), it's now safe to
    // run the evaluator to register the actions.
    //
    Do_Global_Block(VAL_ARRAY(&Boot_Block->actions), 0, -1, action_word);

    // Sanity check the symbol transformation
    //
    if (0 != strcmp("open", cs_cast(STR_HEAD(Canon(SYM_OPEN)))))
        panic (Error(RE_NATIVE_BOOT));
}


//
//  Get_Action_Value: C
// 
// Return the value (function) for a given Action number.
//
REBVAL *Get_Action_Value(REBSYM action)
{
    return CTX_VAR(Lib_Context, Action_Marker + cast(REBCNT, action));
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

    SET_ARR_FLAG(CTX_VARLIST(root), SERIES_FLAG_FIXED_SIZE);
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
    ARR_SERIES(CTX_VARLIST(root))->link.keylist = NULL;
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
        SET_END(var);
        SET_ARRAY_LEN(CTX_VARLIST(root), ROOT_MAX);
    }

    // These values are simple isolated VOID, NONE, TRUE, and FALSE values
    // that can be used in lieu of initializing them.  They are initialized
    // as two-element series in order to ensure that their address is not
    // treated as an array.  They are unsettable (in debug builds), to avoid
    // their values becoming overwritten.
    //
    // It is presumed that these types will never need to have GC behavior,
    // and thus can be stored safely in program globals without mention in
    // the root set.  Should that change, they could be explicitly added
    // to the GC's root set.

    SET_VOID(&PG_Void_Cell[0]);
    SET_TRASH_IF_DEBUG(&PG_Void_Cell[1]);
    MARK_CELL_UNWRITABLE_IF_DEBUG(&PG_Void_Cell[1]);

    SET_BLANK(&PG_Blank_Value[0]);
    SET_TRASH_IF_DEBUG(&PG_Blank_Value[1]);
    MARK_CELL_UNWRITABLE_IF_DEBUG(&PG_Blank_Value[1]);

    SET_BAR(&PG_Bar_Value[0]);
    SET_TRASH_IF_DEBUG(&PG_Bar_Value[1]);
    MARK_CELL_UNWRITABLE_IF_DEBUG(&PG_Bar_Value[1]);

    SET_FALSE(&PG_False_Value[0]);
    SET_TRASH_IF_DEBUG(&PG_False_Value[1]);
    MARK_CELL_UNWRITABLE_IF_DEBUG(&PG_False_Value[1]);

    SET_TRUE(&PG_True_Value[0]);
    SET_TRASH_IF_DEBUG(&PG_True_Value[1]);
    MARK_CELL_UNWRITABLE_IF_DEBUG(&PG_True_Value[1]);

    // We can't actually put an end value in the middle of a block, so we poke
    // this one into a program global.  It is not legal to bit-copy an
    // END (you always use SET_END), so we can make it unwritable.
    //
    PG_End_Cell.header.bits = 0; // read-only end
    assert(IS_END(END_CELL)); // sanity check that it took

    // The EMPTY_BLOCK provides EMPTY_ARRAY.  It is locked for protection.
    //
    Val_Init_Block(ROOT_EMPTY_BLOCK, Make_Array(0));
    SET_SER_FLAG(VAL_SERIES(ROOT_EMPTY_BLOCK), SERIES_FLAG_LOCKED);
    SET_SER_FLAG(VAL_SERIES(ROOT_EMPTY_BLOCK), SERIES_FLAG_FIXED_SIZE);

    REBSER *empty_series = Make_Binary(1);
    *BIN_AT(empty_series, 0) = '\0';
    Val_Init_String(ROOT_EMPTY_STRING, empty_series);
    SET_SER_FLAG(VAL_SERIES(ROOT_EMPTY_STRING), SERIES_FLAG_LOCKED);
    SET_SER_FLAG(VAL_SERIES(ROOT_EMPTY_STRING), SERIES_FLAG_FIXED_SIZE);

    // Used by REBNATIVE(print)
    //
    SET_CHAR(ROOT_SPACE_CHAR, ' ');
    MARK_CELL_UNWRITABLE_IF_DEBUG(ROOT_SPACE_CHAR);

    // Can't ASSERT_CONTEXT here; no keylist yet...
}


//
//  Set_Root_Series: C
// 
// Used to set block and string values in the ROOT context.
//
void Set_Root_Series(REBVAL *value, REBSER *ser)
{
    // Note that the Val_Init routines call Manage_Series and make the
    // series GC Managed.  They will hence be freed on shutdown
    // automatically when the root set is removed from consideration.

    if (Is_Array_Series(ser))
        Val_Init_Block(value, AS_ARRAY(ser));
    else {
        assert(SER_WIDE(ser) == 1 || SER_WIDE(ser) == 2);
        Val_Init_String(value, ser);
    }
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

    SET_ARR_FLAG(CTX_VARLIST(task), SERIES_FLAG_FIXED_SIZE);
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
    ARR_SERIES(CTX_VARLIST(task))->link.keylist = NULL;

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
        SET_END(var);
        SET_ARRAY_LEN(CTX_VARLIST(task), TASK_MAX);
    }

    // Initialize a few fields:
    SET_INTEGER(TASK_BALLAST, MEM_BALLAST);
    SET_INTEGER(TASK_MAX_BALLAST, MEM_BALLAST);

    // The thrown arg is not intended to ever be around long enough to be
    // seen by the GC.
    //
    SET_TRASH_IF_DEBUG(&TG_Thrown_Arg);

    // Can't ASSERT_CONTEXT here; no keylist yet...
}


//
//  Init_System_Object: C
// 
// Evaluate the system object and create the global SYSTEM word.  We do not
// BIND_ALL here to keep the internal system words out of the global context.
// (See also N_context() which creates the subobjects of the system object.)
//
static void Init_System_Object(void)
{
    REBCTX *system;
    REBARR *array;
    REBVAL *value;
    REBCNT n;

    REBVAL result;

    // Create the system object from the sysobj block (defined in %sysobj.r)
    //
    system = Make_Selfish_Context_Detect(
        REB_OBJECT, // type
        NULL, // body
        VAL_ARRAY_HEAD(&Boot_Block->sysobj), // scan for toplevel set-words
        NULL // parent
    );

    Bind_Values_Deep(VAL_ARRAY_HEAD(&Boot_Block->sysobj), Lib_Context);

    // Bind it so CONTEXT native will work (only used at topmost depth)
    //
    Bind_Values_Shallow(VAL_ARRAY_HEAD(&Boot_Block->sysobj), system);

    // Evaluate the block (will eval CONTEXTs within).  Expects void result.
    //
    if (DO_VAL_ARRAY_AT_THROWS(&result, &Boot_Block->sysobj))
        panic (Error_No_Catch_For_Throw(&result));
    if (!IS_VOID(&result))
        panic (Error(RE_MISC));

    // Create a global value for it.  (This is why we are able to say `system`
    // and have it bound in lines like `sys: system/contexts/sys`)
    //
    value = Append_Context(Lib_Context, 0, Canon(SYM_SYSTEM));
    Val_Init_Object(value, system);

    // We also add the system object under the root, to ensure it can't be
    // garbage collected and be able to access it from the C code.  (Someone
    // could say `system: blank` in the Lib_Context and then it would be a
    // candidate for garbage collection otherwise!)
    //
    Val_Init_Object(ROOT_SYSTEM, system);

    // Create system/datatypes block.  Start at 1 (REB_BLANK), given that 0
    // is REB_0 and does not correspond to a value type.
    //
    value = Get_System(SYS_CATALOG, CAT_DATATYPES);
    array = VAL_ARRAY(value);
    Extend_Series(ARR_SERIES(array), REB_MAX_0 - 1);
    for (n = 1; n < REB_MAX_0; n++) {
        Append_Value(array, CTX_VAR(Lib_Context, n));
    }

    // Create system/catalog/actions block
    //
    value = Get_System(SYS_CATALOG, CAT_ACTIONS);
    Val_Init_Block(
        value,
        Collect_Set_Words(VAL_ARRAY_HEAD(&Boot_Block->actions))
    );

    // Create system/catalog/natives block
    //
    value = Get_System(SYS_CATALOG, CAT_NATIVES);
    Val_Init_Block(
        value,
        Collect_Set_Words(VAL_ARRAY_HEAD(&Boot_Block->natives))
    );

    // Create system/codecs object
    //
    {
        REBCTX *codecs = Alloc_Context(10);

        value = Get_System(SYS_CODECS, 0);
        SET_BLANK(CTX_ROOTKEY(codecs));
        VAL_RESET_HEADER(CTX_VALUE(codecs), REB_OBJECT);
        CTX_VALUE(codecs)->extra.binding = NULL;
        Val_Init_Object(value, codecs);
    }
}


//
//  Init_Contexts_Object: C
//
static void Init_Contexts_Object(void)
{
    REBVAL *value;

    value = Get_System(SYS_CONTEXTS, CTX_SYS);
    Val_Init_Object(value, Sys_Context);

    value = Get_System(SYS_CONTEXTS, CTX_LIB);
    Val_Init_Object(value, Lib_Context);

    value = Get_System(SYS_CONTEXTS, CTX_USER);  // default for new code evaluation
    Val_Init_Object(value, Lib_Context);
}

//
//  Codec_Text: C
//
REBINT Codec_Text(REBCDI *codi)
{
    codi->error = 0;

    if (codi->action == CODI_ACT_IDENTIFY) {
        return CODI_CHECK; // error code is inverted result
    }

    if (codi->action == CODI_ACT_DECODE) {
        return CODI_TEXT;
    }

    if (codi->action == CODI_ACT_ENCODE) {
        return CODI_BINARY;
    }

    codi->error = CODI_ERR_NA;
    return CODI_ERROR;
}

//
//  Codec_UTF16: C
//
REBINT Codec_UTF16(REBCDI *codi, REBOOL little_endian)
{
    codi->error = 0;

    if (codi->action == CODI_ACT_IDENTIFY) {
        return CODI_CHECK; // error code is inverted result
    }

    if (codi->action == CODI_ACT_DECODE) {
        REBSER *ser = Make_Unicode(codi->len);
        REBINT size = Decode_UTF16(
            UNI_HEAD(ser), codi->data, codi->len, little_endian, FALSE
        );
        SET_SERIES_LEN(ser, size);
        if (size < 0) { //ASCII
            REBSER *dst = Make_Binary((size = -size));
            Append_Uni_Bytes(dst, UNI_HEAD(ser), size);
            ser = dst;
        }
        codi->data = SER_DATA_RAW(ser); // !!! REBUNI?  REBYTE?
        codi->len = SER_LEN(ser);
        codi->w = SER_WIDE(ser);
        return CODI_TEXT;
    }

    if (codi->action == CODI_ACT_ENCODE) {
        u16 * data = ALLOC_N(u16, codi->len);
        if (codi->w == 1) {
            /* in ASCII */
            REBCNT i = 0;
            for (i = 0; i < codi->len; i ++) {
            #ifdef ENDIAN_LITTLE
                if (little_endian) {
                    data[i] = cast(char*, codi->extra.other)[i];
                } else {
                    data[i] = cast(char*, codi->extra.other)[i] << 8;
                }
            #elif defined (ENDIAN_BIG)
                if (little_endian) {
                    data[i] = cast(char*, codi->extra.other)[i] << 8;
                } else {
                    data[i] = cast(char*, codi->extra.other)[i];
                }
            #else
                #error "Unsupported CPU endian"
            #endif
            }
        } else if (codi->w == 2) {
            /* already in UTF16 */
        #ifdef ENDIAN_LITTLE
            if (little_endian) {
                memcpy(data, codi->extra.other, codi->len * sizeof(u16));
            } else {
                REBCNT i = 0;
                for (i = 0; i < codi->len; i ++) {
                    REBUNI uni = cast(REBUNI*, codi->extra.other)[i];
                    data[i] = ((uni & 0xff) << 8) | ((uni & 0xff00) >> 8);
                }
            }
        #elif defined (ENDIAN_BIG)
            if (little_endian) {
                REBCNT i = 0;
                for (i = 0; i < codi->len; i ++) {
                    REBUNI uni = cast(REBUNI*, codi->extra.other)[i];
                    data[i] = ((uni & 0xff) << 8) | ((uni & 0xff00) >> 8);
                }
            } else {
                memcpy(data, codi->extra.other, codi->len * sizeof(u16));
            }
        #else
            #error "Unsupported CPU endian"
        #endif
        } else {
            /* RESERVED for future unicode expansion */
            codi->error = CODI_ERR_NA;
            return CODI_ERROR;
        }

        codi->len *= sizeof(u16);

        return CODI_BINARY;
    }

    codi->error = CODI_ERR_NA;
    return CODI_ERROR;
}


//
//  Codec_UTF16LE: C
//
REBINT Codec_UTF16LE(REBCDI *codi)
{
    return Codec_UTF16(codi, TRUE);
}


//
//  Codec_UTF16BE: C
//
REBINT Codec_UTF16BE(REBCDI *codi)
{
    return Codec_UTF16(codi, FALSE);
}


//
//  Register_Codec: C
// 
// Internal function for adding a codec.
//
void Register_Codec(const REBYTE *name, codo dispatcher)
{
    REBVAL *value = Get_System(SYS_CODECS, 0);
    REBSTR *sym = Intern_UTF8_Managed(name, LEN_BYTES(name));

    value = Append_Context(VAL_CONTEXT(value), 0, sym);
    SET_HANDLE_CODE(value, cast(CFUNC*, dispatcher));
}


//
//  Init_Codecs: C
//
static void Init_Codecs(void)
{
    Register_Codec(cb_cast("text"), Codec_Text);
    Register_Codec(cb_cast("utf-16le"), Codec_UTF16LE);
    Register_Codec(cb_cast("utf-16be"), Codec_UTF16BE);
    Init_BMP_Codec();
    Init_GIF_Codec();
    Init_PNG_Codec();
    Init_JPEG_Codec();
}


static void Set_Option_String(REBCHR *str, REBCNT field)
{
    REBVAL *val;
    if (str) {
        val = Get_System(SYS_OPTIONS, field);
        Val_Init_String(val, Copy_OS_Str(str, OS_STRLEN(str)));
    }
}


static REBSTR *Set_Option_Word(REBCHR *str, REBCNT field)
{
    if (!str) return NULL;

    REBYTE buf[40]; // option words always short ASCII strings

    REBCNT len = OS_STRLEN(str); // WC correct
    assert(len <= 38);

    REBYTE *bp = &buf[0];
    while ((*bp++ = cast(REBYTE, OS_CH_VALUE(*(str++))))); // clips unicode

    REBSTR *name = Intern_UTF8_Managed(buf, len);

    REBVAL *val = Get_System(SYS_OPTIONS, field);
    Val_Init_Word(val, REB_WORD, name);

    return name;
}


//
//  Init_Main_Args: C
// 
// The system object is defined in boot.r.
//
static void Init_Main_Args(REBARGS *rargs)
{
    REBVAL *val;
    RELVAL *item;
    REBARR *array;
    REBCHR *data;
    REBCNT n;

    array = Make_Array(3);
    n = 2; // skip first flag (ROF_EXT)
    val = Get_System(SYS_CATALOG, CAT_BOOT_FLAGS);
    for (item = VAL_ARRAY_HEAD(val); NOT_END(item); item++) {
        CLEAR_VAL_FLAG(item, VALUE_FLAG_LINE);
        if (rargs->options & n)
            Append_Value(array, KNOWN(item)); // no relatives in BOOT_FLAGS (?)
        n <<= 1;
    }
    val = Alloc_Tail_Array(array);
    SET_TRUE(val);
    val = Get_System(SYS_OPTIONS, OPTIONS_FLAGS);
    Val_Init_Block(val, array);

    // For compatibility:
    if (rargs->options & RO_QUIET) {
        val = Get_System(SYS_OPTIONS, OPTIONS_QUIET);
        SET_TRUE(val);
    }

    // Print("script: %s", rargs->script);
    if (rargs->script) {
        REBSER *ser = To_REBOL_Path(
            rargs->script, 0, OS_WIDE ? PATH_OPT_UNI_SRC : 0
        );
        val = Get_System(SYS_OPTIONS, OPTIONS_SCRIPT);
        Val_Init_File(val, ser);
    }

    if (rargs->exe_path) {
        REBSER *ser = To_REBOL_Path(
            rargs->exe_path, 0, OS_WIDE ? PATH_OPT_UNI_SRC : 0
        );
        val = Get_System(SYS_OPTIONS, OPTIONS_BOOT);
        Val_Init_File(val, ser);
    }

    // Print("home: %s", rargs->home_dir);
    if (rargs->home_dir) {
        REBSER *ser = To_REBOL_Path(
            rargs->home_dir,
            0,
            PATH_OPT_SRC_IS_DIR | (OS_WIDE ? PATH_OPT_UNI_SRC : 0)
        );
        val = Get_System(SYS_OPTIONS, OPTIONS_HOME);
        Val_Init_File(val, ser);
    }

    REBSTR *name = Set_Option_Word(rargs->boot, OPTIONS_BOOT_LEVEL);
    if (name != NULL)
        n = cast(REBCNT, STR_SYMBOL(name));
    else
        n = 0;
    if (n >= SYM_BASE && n <= SYM_MODS)
        PG_Boot_Level = n - SYM_BASE; // 0 - 3

    if (rargs->args) {
        n = 0;
        while (rargs->args[n++]) NOOP;
        // n == number_of_args + 1
        array = Make_Array(n);
        Val_Init_Block(Get_System(SYS_OPTIONS, OPTIONS_ARGS), array);
        SET_ARRAY_LEN(array, n - 1);
        for (n = 0; (data = rargs->args[n]); ++n)
            Val_Init_String(
                ARR_AT(array, n), Copy_OS_Str(data, OS_STRLEN(data))
            );
        TERM_ARRAY(array);
    }

    Set_Option_String(rargs->debug, OPTIONS_DEBUG);
    Set_Option_String(rargs->version, OPTIONS_VERSION);
    Set_Option_String(rargs->import, OPTIONS_IMPORT);

    // !!! The argument to --do exists in REBCHR* form in rargs->do_arg,
    // hence platform-specific encoding.  The host_main.c executes the --do
    // directly instead of using the Rebol-Value string set here.  Ultimately,
    // the Ren/C core will *not* be taking responsibility for setting any
    // "do-arg" variable in the system/options context...if a client of the
    // library has a --do option and wants to expose it, then it will have
    // to do so itself.  We'll leave this non-INTERN'd block here for now.
    Set_Option_String(rargs->do_arg, OPTIONS_DO_ARG);

    Set_Option_Word(rargs->secure, OPTIONS_SECURE);

    if ((data = OS_GET_LOCALE(0))) {
        val = Get_System(SYS_LOCALE, LOCALE_LANGUAGE);
        Val_Init_String(val, Copy_OS_Str(data, OS_STRLEN(data)));
        OS_FREE(data);
    }

    if ((data = OS_GET_LOCALE(1))) {
        val = Get_System(SYS_LOCALE, LOCALE_LANGUAGE_P);
        Val_Init_String(val, Copy_OS_Str(data, OS_STRLEN(data)));
        OS_FREE(data);
    }

    if ((data = OS_GET_LOCALE(2))) {
        val = Get_System(SYS_LOCALE, LOCALE_LOCALE);
        Val_Init_String(val, Copy_OS_Str(data, OS_STRLEN(data)));
        OS_FREE(data);
    }

    if ((data = OS_GET_LOCALE(3))) {
        val = Get_System(SYS_LOCALE, LOCALE_LOCALE_P);
        Val_Init_String(val, Copy_OS_Str(data, OS_STRLEN(data)));
        OS_FREE(data);
    }
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
    //Inspect_Series(0);

    SET_TRASH_SAFE(&TG_Thrown_Arg);
}


//
//  Init_Year: C
//
void Init_Year(void)
{
    REBOL_DAT dat;

    OS_GET_TIME(&dat);
    Current_Year = dat.year;
}


//
//  Init_Core: C
// 
// Initialize the interpreter core.  The initialization will
// either succeed or "panic".
// 
// !!! Panic currently triggers an exit to the OS.  Offering a
// hook to cleanly fail would be ideal--but the code is not
// currently written to be able to cleanly shut down from a
// partial initialization.
// 
// The phases of initialization are tracked by PG_Boot_Phase.
// Some system functions are unavailable at certain phases.
// 
// Though most of the initialization is run as C code, some
// portions are run in Rebol.  Small bits are run during the
// loading of natives and actions (for instance, NATIVE and
// ACTION are functions that are registered very early on in
// the booting process, which are run during boot to register
// each of the natives and actions).
// 
// At the tail of the initialization, `finish_init_core` is run.
// This Rebol function lives in %sys-start.r, and it should be
// "host agnostic".  Hence it should not assume things about
// command-line switches (or even that there is a command line!)
// Converting the code that made such assumptions is an
// ongoing process.
//
void Init_Core(REBARGS *rargs)
{
    REBCTX *error;
    struct Reb_State state;
    REBARR *keylist;

    REBVAL result;

#if defined(TEST_EARLY_BOOT_PANIC)
    // This is a good place to test if the "pre-booting panic" is working.
    // It should be unable to present a format string, only the error code.
    panic (Error(RE_NO_VALUE, BLANK_VALUE));
#elif defined(TEST_EARLY_BOOT_FAIL)
    // A fail should have the same behavior as a panic at this boot phase.
    fail (Error(RE_NO_VALUE, BLANK_VALUE));
#endif

    DOUT("Main init");

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

    DOUT("Level 0");
    Init_Pools(0);          // Memory allocator
    Init_GC();
    Init_Root_Context();    // Special REBOL values per program
    Init_Task_Context();    // Special REBOL values per task

    Init_Raw_Print();       // Low level output (Print)

    Print_Banner(rargs);

    DOUT("Level 1");
    Init_Char_Cases();
    Init_CRC();             // For word hashing
    Set_Random(0);
    Init_Words();
    Init_Stacks(STACK_MIN * 4);
    Init_Scanner();
    Init_Mold(MIN_COMMON);  // Output buffer
    Init_Collector();           // Frames

    // !!! Have MAKE-BOOT compute # of words
    //
    // Must manage, else Expand_Context() looks like a leak
    //
    Lib_Context = Alloc_Context(600);
    MANAGE_ARRAY(CTX_VARLIST(Lib_Context));

    VAL_RESET_HEADER(CTX_VALUE(Lib_Context), REB_OBJECT);
    CTX_VALUE(Lib_Context)->extra.binding = NULL;

    // Must manage, else Expand_Context() looks like a leak
    //
    Sys_Context = Alloc_Context(50);
    MANAGE_ARRAY(CTX_VARLIST(Sys_Context));

    VAL_RESET_HEADER(CTX_VALUE(Sys_Context), REB_OBJECT);
    CTX_VALUE(Sys_Context)->extra.binding = NULL;

    DOUT("Level 2");

    Load_Boot();

    // Data in %boot-code.r now available as Boot_Block.  This includes the
    // type list, word list, error message templates, system object, etc.

    Init_Symbols(VAL_ARRAY(&Boot_Block->words));

    // STR_SYMBOL(), VAL_WORD_SYM() and Canon(SYM_XXX) now available

    PG_Boot_Phase = BOOT_LOADED;
    //Debug_Str(BOOT_STR(RS_INFO,0)); // Booting...

    // Get the words of the ROOT context (to avoid it being an exception case)
    //
    keylist = Collect_Keylist_Managed(
        NULL, VAL_ARRAY_HEAD(&Boot_Block->root), NULL, COLLECT_ANY_WORD);
    INIT_CTX_KEYLIST_UNIQUE(PG_Root_Context, keylist);
    ASSERT_CONTEXT(PG_Root_Context);

    // Get the words of the TASK context (to avoid it being an exception case)
    //
    keylist = Collect_Keylist_Managed(
        NULL, VAL_ARRAY_HEAD(&Boot_Block->task), NULL, COLLECT_ANY_WORD
    );
    INIT_CTX_KEYLIST_UNIQUE(TG_Task_Context, keylist);
    ASSERT_CONTEXT(TG_Task_Context);

    // Create main values:
    DOUT("Level 3");
    Init_Datatypes();       // Create REBOL datatypes
    Init_Typesets();        // Create standard typesets
    Init_Constants();       // Constant values

    // Although the goal is for the core not to depend on any specific
    // "keywords", there are some native-optimized generators that are not
    // conceptually "part of the core".  Hence, they rely on some keywords,
    // but a dissatisfied user could rewrite them with different ones
    // (at only a cost in performance).
    //
    // We need these tags around to compare to the tags we find in function
    // specs.  There may be a better place to put them or a better way to do
    // it, but it didn't seem there was a "compare UTF8 byte array to
    // arbitrary decoded REB_TAG which may or may not be REBUNI" routine.

    // !!! These need to find a new home, and preferably a way to be read
    // as constants declared in Rebol files.  Hasn't been done yet due to
    // a desire to keep this as an obvious TBD for remembering to do it
    // right, but also to protect the values from changing.

    const REBYTE no_return[] = "no-return";
    Val_Init_Tag(
        ROOT_NO_RETURN_TAG,
        Append_UTF8_May_Fail(NULL, no_return, LEN_BYTES(no_return))
    );
    SET_SER_FLAG(VAL_SERIES(ROOT_NO_RETURN_TAG), SERIES_FLAG_FIXED_SIZE);
    SET_SER_FLAG(VAL_SERIES(ROOT_NO_RETURN_TAG), SERIES_FLAG_LOCKED);

    const REBYTE no_leave[] = "no-leave";
    Val_Init_Tag(
        ROOT_NO_LEAVE_TAG,
        Append_UTF8_May_Fail(NULL, no_leave, LEN_BYTES(no_leave))
    );
    SET_SER_FLAG(VAL_SERIES(ROOT_NO_LEAVE_TAG), SERIES_FLAG_FIXED_SIZE);
    SET_SER_FLAG(VAL_SERIES(ROOT_NO_LEAVE_TAG), SERIES_FLAG_LOCKED);

    const REBYTE punctuates[] = "punctuates";
    Val_Init_Tag(
        ROOT_PUNCTUATES_TAG,
        Append_UTF8_May_Fail(NULL, punctuates, LEN_BYTES(punctuates))
    );
    SET_SER_FLAG(VAL_SERIES(ROOT_PUNCTUATES_TAG), SERIES_FLAG_FIXED_SIZE);
    SET_SER_FLAG(VAL_SERIES(ROOT_PUNCTUATES_TAG), SERIES_FLAG_LOCKED);

    const REBYTE ellipsis[] = "...";
    Val_Init_Tag(
        ROOT_ELLIPSIS_TAG,
        Append_UTF8_May_Fail(NULL, ellipsis, LEN_BYTES(ellipsis))
    );
    SET_SER_FLAG(VAL_SERIES(ROOT_ELLIPSIS_TAG), SERIES_FLAG_FIXED_SIZE);
    SET_SER_FLAG(VAL_SERIES(ROOT_ELLIPSIS_TAG), SERIES_FLAG_LOCKED);

    const REBYTE opt[] = "opt";
    Val_Init_Tag(
        ROOT_OPT_TAG,
        Append_UTF8_May_Fail(NULL, opt, LEN_BYTES(ellipsis))
    );
    SET_SER_FLAG(VAL_SERIES(ROOT_OPT_TAG), SERIES_FLAG_FIXED_SIZE);
    SET_SER_FLAG(VAL_SERIES(ROOT_OPT_TAG), SERIES_FLAG_LOCKED);

    const REBYTE end[] = "end";
    Val_Init_Tag(
        ROOT_END_TAG,
        Append_UTF8_May_Fail(NULL, end, LEN_BYTES(ellipsis))
    );
    SET_SER_FLAG(VAL_SERIES(ROOT_END_TAG), SERIES_FLAG_FIXED_SIZE);
    SET_SER_FLAG(VAL_SERIES(ROOT_END_TAG), SERIES_FLAG_LOCKED);

    const REBYTE local[] = "local";
    Val_Init_Tag(
        ROOT_LOCAL_TAG,
        Append_UTF8_May_Fail(NULL, local, LEN_BYTES(local))
    );
    SET_SER_FLAG(VAL_SERIES(ROOT_LOCAL_TAG), SERIES_FLAG_FIXED_SIZE);
    SET_SER_FLAG(VAL_SERIES(ROOT_LOCAL_TAG), SERIES_FLAG_LOCKED);

    const REBYTE durable[] = "durable";
    Val_Init_Tag(
        ROOT_DURABLE_TAG,
        Append_UTF8_May_Fail(NULL, durable, LEN_BYTES(durable))
    );
    SET_SER_FLAG(VAL_SERIES(ROOT_DURABLE_TAG), SERIES_FLAG_FIXED_SIZE);
    SET_SER_FLAG(VAL_SERIES(ROOT_DURABLE_TAG), SERIES_FLAG_LOCKED);

    // Run actual code:
    DOUT("Level 4");
    Init_Natives();         // Built-in native functions
    Init_Ops();             // Built-in operators
    Init_System_Object();
    Init_Contexts_Object();
    Init_Main_Args(rargs);
    Init_Ports();
    Init_Codecs();
    Init_Errors(&Boot_Block->errors); // Needs system/standard/error object

    SET_VOID(&Callback_Error);

    PG_Boot_Phase = BOOT_ERRORS;

#if defined(TEST_MID_BOOT_PANIC)
    // At this point panics should be able to present the full message.
    panic (Error(RE_NO_VALUE, BLANK_VALUE));
#elif defined(TEST_MID_BOOT_FAIL)
    // With no PUSH_TRAP yet, fail should give a localized assert in a debug
    // build but act like panic does in a release build.
    fail (Error(RE_NO_VALUE, BLANK_VALUE));
#endif

    // Special pre-made errors:
    Val_Init_Error(TASK_STACK_ERROR, Error(RE_STACK_OVERFLOW));
    Val_Init_Error(TASK_HALT_ERROR, Error(RE_HALT));

    // With error trapping enabled, set up to catch them if they happen.
    PUSH_UNHALTABLE_TRAP(&error, &state);

// The first time through the following code 'error' will be NULL, but...
// `fail` can longjmp here, so 'error' won't be NULL *if* that happens!

    if (error) {
        REBVAL temp;
        Val_Init_Error(&temp, error);

        // You shouldn't be able to halt during Init_Core() startup.
        // The only way you should be able to stop Init_Core() is by raising
        // an error, at which point the system will Panic out.
        // !!! TBD: Enforce not being *able* to trigger HALT
        assert(ERR_NUM(error) != RE_HALT);

        // If an error was raised during startup, print it and crash.
        Print_Value(&temp, 1024, FALSE);
        panic (Error(RE_MISC));
    }

    Init_Year();

    // Initialize mezzanine functions:
    DOUT("Level 5");
    if (PG_Boot_Level >= BOOT_LEVEL_SYS) {
        Do_Global_Block(VAL_ARRAY(&Boot_Block->base), 0, 1, NULL);
        Do_Global_Block(VAL_ARRAY(&Boot_Block->sys), 0, 2, NULL);
    }

    *CTX_VAR(Sys_Context, SYS_CTX_BOOT_MEZZ) = Boot_Block->mezz;
    *CTX_VAR(Sys_Context, SYS_CTX_BOOT_PROT) = Boot_Block->protocols;

    // No longer needs protecting:
    SET_BLANK(ROOT_BOOT);
    Boot_Block = NULL;
    PG_Boot_Phase = BOOT_MEZZ;

    assert(DSP == 0 && FS_TOP == NULL);

    if (Apply_Only_Throws(
        &result, Sys_Func(SYS_CTX_FINISH_INIT_CORE), END_CELL
    )) {
        // Note: You shouldn't be able to throw any uncaught values during
        // Init_Core() startup, including throws implementing QUIT or EXIT.
        assert(FALSE);
        fail (Error_No_Catch_For_Throw(&result));
    }

    // Success of the 'finish-init-core' Rebol code is signified by returning
    // void (all other return results indicate an error state)

    if (!IS_VOID(&result)) {
        Debug_Fmt("** 'finish-init-core' returned value: %r", &result);
        panic (Error(RE_MISC));
    }

    assert(DSP == 0 && FS_TOP == NULL);

    DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

    PG_Boot_Phase = BOOT_DONE;

    Recycle(); // necessary?

    DOUT("Boot done");
}


//
//  Shutdown_Core: C
// 
// The goal of Shutdown_Core() is to release all memory and
// resources that the interpreter has accrued since Init_Core().
// 
// Clients may wish to force an exit to the OS instead of calling
// Shutdown_Core in a release build, in order to save time.  It
// should be noted that when used as a library this doesn't
// necessarily work, because Rebol may be initialized and shut
// down multiple times during a program run.
// 
// Using a tool like Valgrind or Leak Sanitizer, it is possible
// to verify that all the allocations have indeed been freed.
// Being able to have a report that they have is a good sanity
// check on not just the memory lost by leaks, but the semantic
// errors and bugs that such leaks may indicate.
//
void Shutdown_Core(void)
{
    assert(!Saved_State);

    Shutdown_Stacks();

    // Run Recycle, but the TRUE flag indicates we want every series
    // that is managed to be freed.  (Only unmanaged should be left.)
    //
    Recycle_Core(TRUE);

    FREE_N(REBYTE*, RS_MAX, PG_Boot_Strs);

    Shutdown_Ports();
    Shutdown_Event_Scheme();
    Shutdown_CRC();
    Shutdown_Mold();
    Shutdown_Scanner();
    Shutdown_Char_Cases();

    assert(PG_Num_Canon_Slots_In_Use - PG_Num_Canon_Deleteds == 0);
    Free_Series(PG_Canons_By_Hash);
    Free_Series(PG_Symbol_Canons);

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
