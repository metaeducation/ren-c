/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  b-init.c
**  Summary: initialization functions
**  Section: bootstrap
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"

#define EVAL_DOSE 10000

// Boot Vars used locally:
static  REBCNT  Native_Count;
static  REBCNT  Native_Limit;
static  REBCNT  Action_Count;
static  REBCNT  Action_Marker;
static const REBFUN *Native_Functions;
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
    REBVAL val;

#if defined(SHOW_SIZEOFS)
    union Reb_Value_Data *dummy_data;
#endif

    // !!! This is actually undefined behavior.  There is no requirement for
    // the compiler to let you "image" the bits in a union in a way that
    // reveals the endianness of the processor.  (Intuitively speaking, if you
    // could do such a thing then you would be reaching beneath the abstraction
    // layer that the standard is seeking to ensure you are "protected" by!)
    //
    // So ultimately the build needs to just take the word of the #define
    // switches saying what the endianness is.  There is no way to implement
    // this check "correctly".  All that said, in the interim, this usually
    // works...but should be easy to turn off as it's standards-violating.
    //
    val.flags.all = 0;
    val.flags.bitfields.opts = 123;
    if (val.flags.all != 123)
        panic (Error(RE_REBVAL_ALIGNMENT));

    VAL_SET(&val, 123);
    if (VAL_TYPE(&val) != 123)
        panic (Error(RE_REBVAL_ALIGNMENT));

#if defined(SHOW_SIZEOFS)
    // For debugging ports to some systems:
    printf("%d %s\n", sizeof(dummy_data->word), "word");
    printf("%d %s\n", sizeof(dummy_data->series), "series");
    printf("%d %s\n", sizeof(dummy_data->logic), "logic");
    printf("%d %s\n", sizeof(dummy_data->integer), "integer");
    printf("%d %s\n", sizeof(dummy_data->unteger), "unteger");
    printf("%d %s\n", sizeof(dummy_data->decimal), "decimal");
    printf("%d %s\n", sizeof(dummy_data->character), "char");
    printf("%d %s\n", sizeof(dummy_data->error), "error");
    printf("%d %s\n", sizeof(dummy_data->datatype), "datatype");
    printf("%d %s\n", sizeof(dummy_data->frame), "frame");
    printf("%d %s\n", sizeof(dummy_data->typeset), "typeset");
    printf("%d %s\n", sizeof(dummy_data->symbol), "symbol");
    printf("%d %s\n", sizeof(dummy_data->time), "time");
    printf("%d %s\n", sizeof(dummy_data->tuple), "tuple");
    printf("%d %s\n", sizeof(dummy_data->func), "func");
    printf("%d %s\n", sizeof(dummy_data->object), "object");
    printf("%d %s\n", sizeof(dummy_data->pair), "pair");
    printf("%d %s\n", sizeof(dummy_data->event), "event");
    printf("%d %s\n", sizeof(dummy_data->library), "library");
    printf("%d %s\n", sizeof(dummy_data->structure), "struct");
    printf("%d %s\n", sizeof(dummy_data->gob), "gob");
    printf("%d %s\n", sizeof(dummy_data->utype), "utype");
    printf("%d %s\n", sizeof(dummy_data->money), "money");
    printf("%d %s\n", sizeof(dummy_data->handle), "handle");
    printf("%d %s\n", sizeof(dummy_data->all), "all");
#endif

    if (sizeof(void *) == 8) {
        if (sizeof(REBVAL) != 32) panic (Error(RE_REBVAL_ALIGNMENT));
        if (sizeof(REBGOB) != 84) panic (Error(RE_BAD_SIZE));
    } else {
        if (sizeof(REBVAL) != 16) panic (Error(RE_REBVAL_ALIGNMENT));
        if (sizeof(REBGOB) != 64) panic (Error(RE_BAD_SIZE));
    }
    if (sizeof(REBDAT) != 4) panic (Error(RE_BAD_SIZE));

    // The RXIARG structure mirrors the layouts of several value types
    // for clients who want to extend Rebol but not depend on all of
    // its includes.  This mirroring is brittle and is counter to the
    // idea of Ren/C.  It is depended on by the host-extensions (crypto)
    // as well as R3/View's way of picking apart Rebol data.
    //
    // Best thing to do would be to get rid of it and link those clients
    // directly to Ren/C's API.  But until then, even adapting the RXIARG
    // doesn't seem to get everything to work...so there are bugs.  The
    // struct layout of certain things must line up, notably that the
    // frame of an ANY-CONTEXT! is as the same location as the series
    // of a ANY-SERIES!
    //
    // In the meantime, this limits flexibility which might require the
    // answer to VAL_FRAME() to be different from VAL_SERIES(), and
    // lead to trouble if one call were used in lieu of the other.
    // Revisit after RXIARG dependencies have been eliminated.

    if (
        offsetof(struct Reb_Context, frame)
        != offsetof(struct Reb_Position, series)
    ) {
        panic (Error(RE_MISC));
    }

    // Check special return values used to make sure they don't overlap
    assert(THROWN_FLAG != END_FLAG);
    assert(NOT_FOUND != END_FLAG);
    assert(NOT_FOUND != THROWN_FLAG);
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
//    -1: bind shallow into sys (for NATIVE and ACTION)
//     1: add new words to LIB, bind/deep to LIB
//     2: add new words to SYS, bind/deep to LIB
// 
// Expects result to be UNSET!
//
static void Do_Global_Block(REBSER *block, REBCNT index, REBINT rebind)
{
    REBVAL result;
    REBVAL *item = BLK_SKIP(block, index);

    Bind_Values_Set_Forward_Shallow(
        item, rebind > 1 ? Sys_Context : Lib_Context
    );

    if (rebind < 0) Bind_Values_Shallow(item, Sys_Context);
    if (rebind > 0) Bind_Values_Deep(item, Lib_Context);
    if (rebind > 1) Bind_Values_Deep(item, Sys_Context);

    // !!! The words NATIVE and ACTION were bound but paths would not bind.
    // So you could do `native [spec]` but not `native/frameless [spec]`
    // because in the later case, it wouldn't have descended into the path
    // to bind `native`.  This is apparently intentional to avoid binding
    // deeply in the system context.  This hacky workaround serves to get
    // the boot binding working well enough to use refinements, but should
    // be given a review.
    {
        REBVAL *word = NULL;
        for (; NOT_END(item); item++) {
            if (
                IS_WORD(item) && (
                    VAL_WORD_SYM(item) == SYM_NATIVE
                    || VAL_WORD_SYM(item) == SYM_ACTION
                )
            ) {
                // Get the bound value from the first `native` or `action`
                // toplevel word that we find
                //
                word = item;
            }
        }

        // Now restart the search so we are sure to pick up any paths that
        // might come *before* the first bound word.
        //
        item = BLK_SKIP(block, index);
        for (; NOT_END(item); item++) {
            if (IS_PATH(item)) {
                REBVAL *path_item = BLK_HEAD(VAL_SERIES(item));
                if (
                    IS_WORD(path_item) && (
                        VAL_WORD_SYM(path_item) == SYM_NATIVE
                        || VAL_WORD_SYM(path_item) == SYM_ACTION
                    )
                ) {
                    // overwrite with bound form (we shouldn't have any calls
                    // to NATIVE in the actions block or to ACTION in the
                    // block of natives...)
                    //
                    assert(word);
                    assert(VAL_WORD_SYM(word) == VAL_WORD_SYM(path_item));
                    *path_item = *word;
                }
            }
        }
    }

    if (Do_At_Throws(&result, block, index))
        panic (Error_No_Catch_For_Throw(&result));

    if (!IS_UNSET(&result))
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
    REBSER *boot;
    REBSER *text;

    // Decompress binary data in Native_Specs to get the textual source
    // of the function specs for the native routines into `boot` series.
    //
    // (Native_Specs array is in b-boot.c, auto-generated by make-boot.r)

    text = Decompress(
        Native_Specs, NAT_COMPRESSED_SIZE, NAT_UNCOMPRESSED_SIZE, FALSE, FALSE
    );

    if (!text || (STR_LEN(text) != NAT_UNCOMPRESSED_SIZE))
        panic (Error(RE_BOOT_DATA));

    boot = Scan_Source(STR_HEAD(text), NAT_UNCOMPRESSED_SIZE);
    Free_Series(text);

    Set_Root_Series(ROOT_BOOT, boot, "boot block"); // Do not let it get GC'd

    Boot_Block = cast(BOOT_BLK *, VAL_BLK_HEAD(BLK_HEAD(boot)));

    if (VAL_TAIL(&Boot_Block->types) != REB_MAX)
        panic (Error(RE_BAD_BOOT_TYPE_BLOCK));
    if (VAL_WORD_SYM(VAL_BLK_HEAD(&Boot_Block->types)) != SYM_TRASH_TYPE)
        panic (Error(RE_BAD_TRASH_TYPE));

    // Create low-level string pointers (used by RS_ constants):
    {
        REBYTE *cp;
        REBINT i;

        PG_Boot_Strs = ALLOC_ARRAY(REBYTE *, RS_MAX);
        *ROOT_STRINGS = Boot_Block->strings;
        cp = VAL_BIN(ROOT_STRINGS);
        for (i = 0; i < RS_MAX; i++) {
            PG_Boot_Strs[i] = cp;
            while (*cp++);
        }
    }

    if (COMPARE_BYTES(cb_cast("trash!"), Get_Sym_Name(SYM_TRASH_TYPE)) != 0)
        panic (Error(RE_BAD_TRASH_CANON));
    if (COMPARE_BYTES(cb_cast("true"), Get_Sym_Name(SYM_TRUE)) != 0)
        panic (Error(RE_BAD_TRUE_CANON));
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
    REBVAL *word = VAL_BLK_HEAD(&Boot_Block->types);
    REBSER *specs = VAL_SERIES(&Boot_Block->typespecs);
    REBVAL *value;
    REBINT n;

    for (n = 0; NOT_END(word); word++, n++) {
        assert(n < REB_MAX);
        value = Append_Frame(Lib_Context, word, 0);
        VAL_SET(value, REB_DATATYPE);
        VAL_TYPE_KIND(value) = cast(enum Reb_Kind, n);
        VAL_TYPE_SPEC(value) = VAL_SERIES(BLK_SKIP(specs, n));
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

    value = Append_Frame(Lib_Context, 0, SYM_NONE);
    SET_NONE(value);
    assert(IS_NONE(value));
    assert(IS_CONDITIONAL_FALSE(value));

    value = Append_Frame(Lib_Context, 0, SYM_TRUE);
    SET_TRUE(value);
    assert(VAL_LOGIC(value));
    assert(IS_CONDITIONAL_TRUE(value));

    value = Append_Frame(Lib_Context, 0, SYM_FALSE);
    SET_FALSE(value);
    assert(!VAL_LOGIC(value));
    assert(IS_CONDITIONAL_FALSE(value));

    value = Append_Frame(Lib_Context, 0, SYM_PI);
    SET_DECIMAL(value, pi1);
    assert(IS_DECIMAL(value));
    assert(IS_CONDITIONAL_TRUE(value));
}


//
//  native: native [
//
//  {Creates native function (for internal usage only).}
//
//      spec [block!]
//      /frameless
//          {Native wants delegation to eval its own args and extend DO state}
//  ]
//
REBNATIVE(native)
//
// The `native` native is searched for explicitly by %make-natives.r and put
// in first place for initialization.  This is a special bootstrap function
// created manually within the C code, as it cannot "run to create itself".
{
    PARAM(1, spec);
    REFINE(2, frameless);

    if (
        (Native_Limit != 0 || !*Native_Functions)
        && (Native_Count >= Native_Limit)
    ) {
        fail (Error(RE_MAX_NATIVES));
    }

    Make_Native(D_OUT, VAL_SERIES(ARG(spec)), *Native_Functions++, REB_NATIVE);

    if (REF(frameless))
        VAL_SET_EXT(D_OUT, EXT_FUNC_FRAMELESS);

    Native_Count++;
    return R_OUT;
}


//
//  action: native [
//
//  {Creates datatype action (for internal usage only).}
//
//      spec [block!]
//      /typecheck typenum [integer! datatype!]
//  ]
//
REBNATIVE(action)
//
// The `action` native is searched for explicitly by %make-natives.r and put
// in second place for initialization (after the `native` native).
//
// If /TYPECHECK is used then you can get a fast checker for a datatype:
//
//     string?: action/typecheck [value [unset! any-value!]] string!
//
// Because words are not bound to the datatypes at the time of action building
// it accepts integer numbers for bootstrapping.
{
    PARAM(1, spec);
    REFINE(2, typecheck);
    PARAM(3, typenum);

    if (Action_Count >= A_MAX_ACTION) panic (Error(RE_ACTION_OVERFLOW));

    // The boot generation process is set up so that the action numbers will
    // conveniently line up to match the type checks to keep the numbers
    // from overlapping with actions, but the refinement makes it more clear
    // exactly what is going on.
    //
    if (REF(typecheck)) {
        assert(VAL_INT32(ARG(typenum)) == cast(REBINT, Action_Count));

        // All the type checks run frameless, so we set that flag (as it is
        // already being checked)
        //
        VAL_SET_EXT(D_OUT, EXT_FUNC_FRAMELESS);
    }

    Make_Native(
        D_OUT,
        VAL_SERIES(ARG(spec)),
        cast(REBFUN, cast(REBUPT, Action_Count)),
        REB_ACTION
    );

    Action_Count++;
    return R_OUT;
}


//
//  context: native [
//  
//  "Defines a unique object."
//  
//      spec [block!] "Object words and values (modified)"
//  ]
//
REBNATIVE(context)
//
// The spec block has already been bound to Lib_Context, to
// allow any embedded values and functions to evaluate.
// 
// Note: Overlaps MAKE OBJECT! code (REBTYPE(Object)'s A_MAKE)
{
    PARAM(1, spec);

    Val_Init_Object(
        D_OUT,
        Make_Frame_Detect(
            REB_OBJECT, // kind
            NULL, // spec
            NULL, // body
            VAL_BLK_HEAD(ARG(spec)), // values to scan for toplevel SET_WORDs
            NULL // parent
        )
    );

    // !!! This mutates the bindings of the spec block passed in, should it
    // be making a copy instead (at least by default, perhaps with performance
    // junkies saying `object/bind` or something like that?
    //
    Bind_Values_Deep(VAL_BLK_HEAD(ARG(spec)), VAL_FRAME(D_OUT));

    // The evaluative result of running the spec is ignored and done into a
    // scratch cell, but needs to be returned if a throw happens.
    //
    if (DO_ARRAY_THROWS(D_CELL, ARG(spec))) {
        *D_OUT = *D_CELL;
        return R_OUT_IS_THROWN;
    }

    // On success, return the object (common case)
    //
    return R_OUT;
}


//
//  Init_Ops: C
//
static void Init_Ops(void)
{
    REBVAL *word;
    REBVAL *val;

    for (word = VAL_BLK_HEAD(&Boot_Block->ops); NOT_END(word); word++) {
        // Append the operator name to the lib frame:
        val = Append_Frame(Lib_Context, word, 0);

        // leave UNSET!, functions will be filled in later...
        cast(void, cast(REBUPT, val));
    }
}


//
//  Init_Natives: C
// 
// Create native functions.
//
static void Init_Natives(void)
{
    REBVAL *item = BLK_HEAD(VAL_SERIES(&Boot_Block->natives));
    REBVAL *val;

    Action_Count = 1; // Skip A_TRASH_Q
    Native_Count = 0;
    Native_Limit = MAX_NATS;
    Native_Functions = Native_Funcs;

    // Construct first native, which is the NATIVE function creator itself:
    //
    //     native: native [spec [block!]]
    //
    if (!IS_SET_WORD(item) || VAL_WORD_SYM(item) != SYM_NATIVE)
        panic (Error(RE_NATIVE_BOOT));

    val = Append_Frame(Lib_Context, item, 0);

    item++; // skip `native:`
    assert(IS_WORD(item) && VAL_WORD_SYM(item) == SYM_NATIVE);
    item++; // skip `native` so we're on the `[spec [block!]]`
    Make_Native(val, VAL_SERIES(item), *Native_Functions++, REB_NATIVE);
    Native_Count++;
    item++; // skip spec

    // Construct second native, which is the ACTION function creator:
    //
    //     action: native [spec [block!]]
    //
    if (!IS_SET_WORD(item) || VAL_WORD_SYM(item) != SYM_ACTION)
        panic (Error(RE_NATIVE_BOOT));

    val = Append_Frame(Lib_Context, item, 0);

    item++; // skip `action:`
    assert(IS_WORD(item) && VAL_WORD_SYM(item) == SYM_NATIVE);
    item++; // skip `native`
    Make_Native(val, VAL_SERIES(item), *Native_Functions++, REB_NATIVE);
    Native_Count++;
    item++; // skip spec

    // Save index for action words.  This is used by Get_Action_Sym().  We have
    // to subtract tone to account for our skipped TRASH? which should not be
    // exposed to the user.
    //
    Action_Marker = FRAME_LEN(Lib_Context);
    Do_Global_Block(VAL_SERIES(&Boot_Block->actions), 0, -1);

    // Sanity check the symbol transformation
    //
    if (0 != strcmp("open", cs_cast(Get_Sym_Name(Get_Action_Sym(A_OPEN)))))
        panic (Error(RE_NATIVE_BOOT));

    // Do native construction, but start from after NATIVE: and ACTION: as we
    // built those by hand
    //
    Do_Global_Block(
        VAL_SERIES(&Boot_Block->natives),
        item - BLK_HEAD(VAL_SERIES(&Boot_Block->natives)),
        -1
    );
}


//
//  Get_Action_Sym: C
// 
// Return the word symbol for a given Action number.
//
REBCNT Get_Action_Sym(REBCNT action)
{
    return FRAME_KEY_SYM(Lib_Context, Action_Marker + action);
}


//
//  Get_Action_Value: C
// 
// Return the value (function) for a given Action number.
//
REBVAL *Get_Action_Value(REBCNT action)
{
    return FRAME_VAR(Lib_Context, Action_Marker+action);
}


//
//  Init_Root_Context: C
// 
// Hand-build the root context where special REBOL values are
// stored. Called early, so it cannot depend on any other
// system structures or values.
// 
// Note that the Root_Context's word table is unset!
// None of its values are exported.
//
static void Init_Root_Context(void)
{
    REBVAL *value;
    REBINT n;
    REBFRM *frame;

    // Only half the context! (No words)
    frame = AS_FRAME(Make_Series(
        ROOT_MAX + 1, sizeof(REBVAL), MKS_ARRAY | MKS_FRAME
    ));
    SET_END(BLK_HEAD(&frame->series)); // !!! Need since using Make_Series?

    LABEL_SERIES(frame, "root context");
    LOCK_SERIES(FRAME_VARLIST(frame));
    Root_Context = cast(ROOT_CTX*, BLK_HEAD(FRAME_VARLIST(frame)));

    // Get first value (the SELF for the context):
    value = ROOT_SELF;

    // No keylist of words (at first)
    // !!! Also no `body` (or `spec`, not yet implemented); revisit
    VAL_SET(value, REB_OBJECT);
    FRAME_CONTEXT(frame)->data.context.frame = frame; // VAL_FRAME() asserts
    VAL_CONTEXT_SPEC(value) = NULL;
    VAL_CONTEXT_BODY(value) = NULL;

    // Set all other values to NONE:
    for (n = 1; n < ROOT_MAX; n++) SET_NONE(value + n);
    SET_END(value + ROOT_MAX);
    SERIES_TAIL(FRAME_VARLIST(frame)) = ROOT_MAX;

    // Set the UNSET_VAL to UNSET!, so we have a sample UNSET! value
    // to pass as an arg if we need an UNSET but don't want to pay for making
    // a new one.  (There is also a NONE_VALUE for this purpose for NONE!s,
    // and an empty block as well.)
    SET_UNSET(ROOT_UNSET_VAL);
    assert(IS_NONE(NONE_VALUE));
    assert(IS_UNSET(UNSET_VALUE));

    Val_Init_Block(ROOT_EMPTY_BLOCK, Make_Array(0));
    SERIES_SET_FLAG(VAL_SERIES(ROOT_EMPTY_BLOCK), SER_PROTECT);
    SERIES_SET_FLAG(VAL_SERIES(ROOT_EMPTY_BLOCK), SER_LOCK);

    // Used by FUNC and CLOS generators: RETURN:
    Val_Init_Word_Unbound(ROOT_RETURN_SET_WORD, REB_SET_WORD, SYM_RETURN);

    // Make a series that's just [return:], that is made often in function
    // spec blocks (when the original spec was just []).  Unlike the paramlist
    // a function spec doesn't need unique mutable identity, so a shared
    // series saves on allocation time and space...
    Val_Init_Block(ROOT_RETURN_BLOCK, Make_Array(1));
    Append_Value(VAL_SERIES(ROOT_RETURN_BLOCK), ROOT_RETURN_SET_WORD);
    SERIES_SET_FLAG(VAL_SERIES(ROOT_RETURN_BLOCK), SER_PROTECT);
    SERIES_SET_FLAG(VAL_SERIES(ROOT_RETURN_BLOCK), SER_LOCK);

    // We can't actually put an end value in the middle of a block, so we poke
    // this one into a program global.  We also dynamically allocate it in
    // order to get uninitialized memory for everything but the header (if
    // we used a global, C zero-initializes that space)
    //
    PG_End_Val = cast(REBVAL*, malloc(sizeof(REBVAL)));
    SET_END(PG_End_Val);
    assert(IS_END(END_VALUE));

    // Initially the root context is a series but has no keylist to officially
    // make it an object.  Start it out as a block (change it later in boot)
    //
    Val_Init_Block(ROOT_ROOT, FRAME_VARLIST(frame));
}


//
//  Set_Root_Series: C
// 
// Used to set block and string values in the ROOT context.
//
void Set_Root_Series(REBVAL *value, REBSER *ser, const char *label)
{
    LABEL_SERIES(ser, label);

    // Note that the Val_Init routines call Manage_Series and make the
    // series GC Managed.  They will hence be freed on shutdown
    // automatically when the root set is removed from consideration.

    if (Is_Array_Series(ser))
        Val_Init_Block(value, ser);
    else {
        assert(SERIES_WIDE(ser) == 1 || SERIES_WIDE(ser) == 2);
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
    REBVAL *value;
    REBINT n;
    REBFRM *frame;
    REBSER *task_words;

    //Print_Str("Task Context");

    frame = AS_FRAME(
        Make_Series(TASK_MAX + 1, sizeof(REBVAL), MKS_ARRAY | MKS_FRAME)
    );
    SET_END(BLK_HEAD(&frame->series)); // !!! Needed since using Make_Series?
    Task_Frame = frame;

    LABEL_SERIES(frame, "task context");
    LOCK_SERIES(FRAME_VARLIST(frame));
    MANAGE_SERIES(FRAME_VARLIST(frame));
    Task_Context = cast(TASK_CTX*, BLK_HEAD(FRAME_VARLIST(frame)));

    // Get first value (the SELF for the context):
    value = TASK_SELF;

    // No keylist of words (at first)
    // !!! Also no `body` (or `spec`, not yet implemented); revisit
    VAL_SET(value, REB_OBJECT);
    FRAME_CONTEXT(frame)->data.context.frame = frame; // VAL_FRAME() asserts
    VAL_CONTEXT_SPEC(value) = NULL;
    VAL_CONTEXT_BODY(value) = NULL;

    // Set all other values to NONE:
    for (n = 1; n < TASK_MAX; n++) SET_NONE(value+n);
    SET_END(value+TASK_MAX);
    SERIES_TAIL(FRAME_VARLIST(frame)) = TASK_MAX;

    // Initialize a few fields:
    SET_INTEGER(TASK_BALLAST, MEM_BALLAST);
    SET_INTEGER(TASK_MAX_BALLAST, MEM_BALLAST);

    // The thrown arg is not intended to ever be around long enough to be
    // seen by the GC.
    //
    SET_TRASH_IF_DEBUG(&TG_Thrown_Arg);
}


//
//  Init_System_Object: C
// 
// The system object is defined in boot.r.
//
static void Init_System_Object(void)
{
    REBFRM *frame;
    REBSER *series;
    REBVAL *value;
    REBCNT n;
    REBVAL result;

    // Evaluate the system object and create the global SYSTEM word.
    // We do not BIND_ALL here to keep the internal system words out
    // of the global context. See also N_context() which creates the
    // subobjects of the system object.

    // Create the system object from the sysobj block and bind its fields:
    frame = Make_Frame_Detect(
        REB_OBJECT, // type
        NULL, // spec
        NULL, // body
        VAL_BLK_HEAD(&Boot_Block->sysobj), // scan for toplevel set-words
        NULL // parent
    );

    Bind_Values_Deep(VAL_BLK_HEAD(&Boot_Block->sysobj), Lib_Context);

    // Bind it so CONTEXT native will work (only used at topmost depth):
    Bind_Values_Shallow(VAL_BLK_HEAD(&Boot_Block->sysobj), frame);

    // Evaluate the block (will eval FRAMEs within):
    if (DO_ARRAY_THROWS(&result, &Boot_Block->sysobj))
        panic (Error_No_Catch_For_Throw(&result));

    // Expects UNSET! by convention
    if (!IS_UNSET(&result))
        panic (Error(RE_MISC));

    // Create a global value for it:
    value = Append_Frame(Lib_Context, 0, SYM_SYSTEM);
    Val_Init_Object(value, frame);
    Val_Init_Object(ROOT_SYSTEM, frame);

    // Create system/datatypes block:
//  value = Get_System(SYS_DATATYPES, 0);
    value = Get_System(SYS_CATALOG, CAT_DATATYPES);
    series = VAL_SERIES(value);
    Extend_Series(series, REB_MAX - 1);
    for (n = 1; n <= REB_MAX; n++) {
        Append_Value(series, FRAME_VAR(Lib_Context, n));
    }

    // Create system/catalog/datatypes block:
//  value = Get_System(SYS_CATALOG, CAT_DATATYPES);
//  Val_Init_Block(value, Copy_Blk(VAL_SERIES(&Boot_Block->types)));

    // Create system/catalog/actions block:
    value = Get_System(SYS_CATALOG, CAT_ACTIONS);
    Val_Init_Block(value, Collect_Set_Words(VAL_BLK_HEAD(&Boot_Block->actions)));

    // Create system/catalog/actions block:
    value = Get_System(SYS_CATALOG, CAT_NATIVES);
    Val_Init_Block(value, Collect_Set_Words(VAL_BLK_HEAD(&Boot_Block->natives)));

    // Create system/codecs object:
    value = Get_System(SYS_CODECS, 0);
    frame = Alloc_Frame(10, TRUE);
    VAL_SET(FRAME_CONTEXT(frame), REB_OBJECT);
    FRAME_SPEC(frame) = NULL;
    FRAME_BODY(frame) = NULL;
    Val_Init_Object(value, frame);

    // Set system/words to be the main context:
//  value = Get_System(SYS_WORDS, 0);
//  Val_Init_Object(value, Lib_Context);
}


//
//  Init_Contexts_Object: C
//
static void Init_Contexts_Object(void)
{
    REBVAL *value;
//  REBFRM *frame;

    value = Get_System(SYS_CONTEXTS, CTX_SYS);
    Val_Init_Object(value, Sys_Context);

    value = Get_System(SYS_CONTEXTS, CTX_LIB);
    Val_Init_Object(value, Lib_Context);

    value = Get_System(SYS_CONTEXTS, CTX_USER);  // default for new code evaluation
    Val_Init_Object(value, Lib_Context);

    // Make the boot context - used to store values created
    // during boot, but processed in REBOL code (e.g. codecs)
//  value = Get_System(SYS_CONTEXTS, CTX_BOOT);
//  frame = Alloc_Frame(4, TRUE);
//  Val_Init_Object(value, frame);
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
// le: little endian
//
REBINT Codec_UTF16(REBCDI *codi, int le)
{
    codi->error = 0;

    if (codi->action == CODI_ACT_IDENTIFY) {
        return CODI_CHECK; // error code is inverted result
    }

    if (codi->action == CODI_ACT_DECODE) {
        REBSER *ser = Make_Unicode(codi->len);
        REBINT size = Decode_UTF16(UNI_HEAD(ser), codi->data, codi->len, le, FALSE);
        SERIES_TAIL(ser) = size;
        if (size < 0) { //ASCII
            REBSER *dst = Make_Binary((size = -size));
            Append_Uni_Bytes(dst, UNI_HEAD(ser), size);
            ser = dst;
        }
        codi->data = SERIES_DATA(ser);
        codi->len = SERIES_TAIL(ser);
        codi->w = SERIES_WIDE(ser);
        return CODI_TEXT;
    }

    if (codi->action == CODI_ACT_ENCODE) {
        u16 * data = ALLOC_ARRAY(u16, codi->len);
        if (codi->w == 1) {
            /* in ASCII */
            REBCNT i = 0;
            for (i = 0; i < codi->len; i ++) {
#ifdef ENDIAN_LITTLE
                if (le) {
                    data[i] = cast(char*, codi->extra.other)[i];
                } else {
                    data[i] = cast(char*, codi->extra.other)[i] << 8;
                }
#elif defined (ENDIAN_BIG)
                if (le) {
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
            if (le) {
                memcpy(data, codi->extra.other, codi->len * sizeof(u16));
            } else {
                REBCNT i = 0;
                for (i = 0; i < codi->len; i ++) {
                    REBUNI uni = cast(REBUNI*, codi->extra.other)[i];
                    data[i] = ((uni & 0xff) << 8) | ((uni & 0xff00) >> 8);
                }
            }
#elif defined (ENDIAN_BIG)
            if (le) {
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
//  Codec_Markup: C
//
REBINT Codec_Markup(REBCDI *codi)
{
    codi->error = 0;

    if (codi->action == CODI_ACT_IDENTIFY) {
        return CODI_CHECK; // error code is inverted result
    }

    if (codi->action == CODI_ACT_DECODE) {
        codi->extra.other = Load_Markup(codi->data, codi->len);
        return CODI_BLOCK;
    }

    codi->error = CODI_ERR_NA;
    return CODI_ERROR;
}


//
//  Register_Codec: C
// 
// Internal function for adding a codec.
//
void Register_Codec(const REBYTE *name, codo dispatcher)
{
    REBVAL *value = Get_System(SYS_CODECS, 0);
    REBCNT sym = Make_Word(name, LEN_BYTES(name));

    value = Append_Frame(VAL_FRAME(value), 0, sym);
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
    Register_Codec(cb_cast("markup"), Codec_Markup);
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

static REBCNT Set_Option_Word(REBCHR *str, REBCNT field)
{
    REBVAL *val;
    REBYTE *bp;
    REBYTE buf[40]; // option words always short ASCII strings
    REBCNT n = 0;

    if (str) {
        n = OS_STRLEN(str); // WC correct
        if (n > 38) return 0;
        bp = &buf[0];
        while ((*bp++ = cast(REBYTE, OS_CH_VALUE(*(str++))))); // clips unicode
        n = Make_Word(buf, n);
        val = Get_System(SYS_OPTIONS, field);
        Val_Init_Word_Unbound(val, REB_WORD, n);
    }
    return n;
}

//
//  Init_Main_Args: C
// 
// The system object is defined in boot.r.
//
static void Init_Main_Args(REBARGS *rargs)
{
    REBVAL *val;
    REBSER *ser;
    REBCHR *data;
    REBCNT n;


    ser = Make_Array(3);
    n = 2; // skip first flag (ROF_EXT)
    val = Get_System(SYS_CATALOG, CAT_BOOT_FLAGS);
    for (val = VAL_BLK_HEAD(val); NOT_END(val); val++) {
        VAL_CLR_OPT(val, OPT_VALUE_LINE);
        if (rargs->options & n) Append_Value(ser, val);
        n <<= 1;
    }
    val = Alloc_Tail_Array(ser);
    SET_TRUE(val);
    val = Get_System(SYS_OPTIONS, OPTIONS_FLAGS);
    Val_Init_Block(val, ser);

    // For compatibility:
    if (rargs->options & RO_QUIET) {
        val = Get_System(SYS_OPTIONS, OPTIONS_QUIET);
        SET_TRUE(val);
    }

    // Print("script: %s", rargs->script);
    if (rargs->script) {
        ser = To_REBOL_Path(rargs->script, 0, OS_WIDE, 0);
        val = Get_System(SYS_OPTIONS, OPTIONS_SCRIPT);
        Val_Init_File(val, ser);
    }

    if (rargs->exe_path) {
        ser = To_REBOL_Path(rargs->exe_path, 0, OS_WIDE, 0);
        val = Get_System(SYS_OPTIONS, OPTIONS_BOOT);
        Val_Init_File(val, ser);
    }

    // Print("home: %s", rargs->home_dir);
    if (rargs->home_dir) {
        ser = To_REBOL_Path(rargs->home_dir, 0, OS_WIDE, TRUE);
        val = Get_System(SYS_OPTIONS, OPTIONS_HOME);
        Val_Init_File(val, ser);
    }

    n = Set_Option_Word(rargs->boot, OPTIONS_BOOT_LEVEL);
    if (n >= SYM_BASE && n <= SYM_MODS)
        PG_Boot_Level = n - SYM_BASE; // 0 - 3

    if (rargs->args) {
        n = 0;
        while (rargs->args[n++]) NOOP;
        // n == number_of_args + 1
        ser = Make_Array(n);
        Val_Init_Block(Get_System(SYS_OPTIONS, OPTIONS_ARGS), ser);
        SERIES_TAIL(ser) = n - 1;
        for (n = 0; (data = rargs->args[n]); ++n)
            Val_Init_String(
                BLK_SKIP(ser, n), Copy_OS_Str(data, OS_STRLEN(data))
            );
        TERM_ARRAY(ser);
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
    Eval_Signals = 0;
    Eval_Sigmask = ALL_BITS;

    // errors? problem with PG_Boot_Phase shared?

    Init_Pools(-4);
    Init_GC();
    Init_Task_Context();    // Special REBOL values per task

    Init_Raw_Print();
    Init_Words(TRUE);
    Init_Stacks(STACK_MIN/4);
    Init_Scanner();
    Init_Mold(MIN_COMMON/4);
    Init_Frame();
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
    REBFRM *error;
    REBOL_STATE state;
    REBVAL out;

    const REBYTE transparent[] = "transparent";
    const REBYTE infix[] = "infix";
    const REBYTE local[] = "local";

#if defined(TEST_EARLY_BOOT_PANIC)
    // This is a good place to test if the "pre-booting panic" is working.
    // It should be unable to present a format string, only the error code.
    panic (Error(RE_NO_VALUE, NONE_VALUE));
#elif defined(TEST_EARLY_BOOT_FAIL)
    // A fail should have the same behavior as a panic at this boot phase.
    fail (Error(RE_NO_VALUE, NONE_VALUE));
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

    // Thread locals:
    Trace_Level = 0;
    Saved_State = 0;
    Eval_Dose = EVAL_DOSE;
    Eval_Limit = 0;
    Eval_Signals = 0;
    Eval_Sigmask = ALL_BITS; /// dups Init_Task

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
    Init_Words(FALSE);      // Symbol table
    Init_Stacks(STACK_MIN * 4);
    Init_Scanner();
    Init_Mold(MIN_COMMON);  // Output buffer
    Init_Frame();           // Frames

    // !!! Have MAKE-BOOT compute # of words
    //
    Lib_Context = Alloc_Frame(600, TRUE);
    VAL_SET(FRAME_CONTEXT(Lib_Context), REB_OBJECT);
    FRAME_SPEC(Lib_Context) = NULL;
    FRAME_BODY(Lib_Context) = NULL;
    Sys_Context = Alloc_Frame(50, TRUE);
    VAL_SET(FRAME_CONTEXT(Sys_Context), REB_OBJECT);
    FRAME_SPEC(Sys_Context) = NULL;
    FRAME_BODY(Sys_Context) = NULL;

    DOUT("Level 2");
    Load_Boot();            // Protected strings now available
    PG_Boot_Phase = BOOT_LOADED;
    //Debug_Str(BOOT_STR(RS_INFO,0)); // Booting...

    // Get the words of the ROOT context (to avoid it being an exception case)
    PG_Root_Words = Collect_Frame(
        NULL, VAL_BLK_HEAD(&Boot_Block->root), BIND_ALL
    );
    LABEL_SERIES(PG_Root_Words, "root words");
    MANAGE_SERIES(PG_Root_Words);
    FRAME_KEYLIST(VAL_FRAME(ROOT_SELF)) = PG_Root_Words;
    VAL_CONTEXT_SPEC(ROOT_SELF) = NULL;
    VAL_CONTEXT_BODY(ROOT_SELF) = NULL;

    // and convert ROOT_ROOT from a BLOCK! to an OBJECT!
    Val_Init_Object(ROOT_ROOT, AS_FRAME(VAL_SERIES(ROOT_ROOT)));

    // Get the words of the TASK context (to avoid it being an exception case)
    TG_Task_Words = Collect_Frame(
        NULL, VAL_BLK_HEAD(&Boot_Block->task), BIND_ALL
    );
    LABEL_SERIES(ds, "task words");
    MANAGE_SERIES(TG_Task_Words);
    FRAME_KEYLIST(VAL_FRAME(TASK_SELF)) = TG_Task_Words;
    VAL_CONTEXT_SPEC(TASK_SELF) = NULL;
    VAL_CONTEXT_BODY(TASK_SELF) = NULL;

    // Is it necessary to put the above into an object like for ROOT?
    /*Val_Init_Object(ROOT_ROOT, VAL_SERIES(ROOT_ROOT));*/


    // Create main values:
    DOUT("Level 3");
    Init_Datatypes();       // Create REBOL datatypes
    Init_Typesets();        // Create standard typesets
    Init_Constants();       // Constant values

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
    SET_UNSET(&Callback_Error);
    PG_Boot_Phase = BOOT_ERRORS;

#if defined(TEST_MID_BOOT_PANIC)
    // At this point panics should be able to present the full message.
    panic (Error(RE_NO_VALUE, NONE_VALUE));
#elif defined(TEST_MID_BOOT_FAIL)
    // With no PUSH_TRAP yet, fail should give a localized assert in a debug
    // build but act like panic does in a release build.
    fail (Error(RE_NO_VALUE, NONE_VALUE));
#endif

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

    Val_Init_Tag(
        ROOT_TRANSPARENT_TAG,
        Append_UTF8(NULL, transparent, LEN_BYTES(transparent))
    );
    SERIES_SET_FLAG(VAL_SERIES(ROOT_TRANSPARENT_TAG), SER_LOCK);
    SERIES_SET_FLAG(VAL_SERIES(ROOT_TRANSPARENT_TAG), SER_PROTECT);

    Val_Init_Tag(
        ROOT_INFIX_TAG,
        Append_UTF8(NULL, infix, LEN_BYTES(infix))
    );
    SERIES_SET_FLAG(VAL_SERIES(ROOT_INFIX_TAG), SER_LOCK);
    SERIES_SET_FLAG(VAL_SERIES(ROOT_INFIX_TAG), SER_PROTECT);

    Val_Init_Tag(
        ROOT_LOCAL_TAG,
        Append_UTF8(NULL, local, LEN_BYTES(local))
    );
    SERIES_SET_FLAG(VAL_SERIES(ROOT_LOCAL_TAG), SER_LOCK);
    SERIES_SET_FLAG(VAL_SERIES(ROOT_LOCAL_TAG), SER_PROTECT);

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
        Do_Global_Block(VAL_SERIES(&Boot_Block->base), 0, 1);
        Do_Global_Block(VAL_SERIES(&Boot_Block->sys), 0, 2);
    }

    *FRAME_VAR(Sys_Context, SYS_CTX_BOOT_MEZZ) = Boot_Block->mezz;
    *FRAME_VAR(Sys_Context, SYS_CTX_BOOT_PROT) = Boot_Block->protocols;

    // No longer needs protecting:
    SET_NONE(ROOT_BOOT);
    Boot_Block = NULL;
    PG_Boot_Phase = BOOT_MEZZ;

    assert(DSP == -1 && !DSF);

    if (Do_Sys_Func_Throws(&out, SYS_CTX_FINISH_INIT_CORE, 0)) {
        // Note: You shouldn't be able to throw any uncaught values during
        // Init_Core() startup, including throws implementing QUIT or EXIT.
        assert(FALSE);
        fail (Error_No_Catch_For_Throw(&out));
    }

    // Success of the 'finish-init-core' Rebol code is signified by returning
    // a UNSET! (all other return results indicate an error state)

    if (!IS_UNSET(&out)) {
        Debug_Fmt("** 'finish-init-core' returned non-none!: %r", &out);
        panic (Error(RE_MISC));
    }

    assert(DSP == -1 && !DSF);

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

    FREE_ARRAY(REBYTE*, RS_MAX, PG_Boot_Strs);

    // Free our end value, that was allocated instead of global in order to
    // get good trip-ups on anyone trying to use anything but the header out
    // of it based on uninitialized memory warnings in ASAN/Valgrind.
    //
    free(PG_End_Val);

    Shutdown_Ports();
    Shutdown_Event_Scheme();
    Shutdown_CRC();
    Shutdown_Mold();
    Shutdown_Scanner();
    Shutdown_Char_Cases();
    Shutdown_GC();

    // !!! Need to review the relationship between Open_StdIO (which the host
    // does) and Init_StdIO...they both open, and both close.

    Shutdown_StdIO();

    // !!! Ideally we would free all the manual series by calling them out by
    // name and not "cheat" here, to be sure everything is under control.
    // But for the moment we use the same sweep as the garbage collector,
    // except sweeping the series it *wasn't* responsible for freeing.
    {
        REBSEG *seg = Mem_Pools[SERIES_POOL].segs;
        REBCNT n;

        for (; seg != NULL; seg = seg->next) {
            REBSER *series = cast(REBSER*, seg + 1);
            for (n = Mem_Pools[SERIES_POOL].units; n > 0; n--, series++) {
                if (SERIES_FREED(series))
                    continue;

                // Free_Series asserts that a manual series is freed from
                // the manuals list.  But the GC_Manuals series was never
                // added to itself (it couldn't be!)
                if (series != GC_Manuals)
                    Free_Series(series);
            }
        }
    }

    FREE(REB_OPTS, Reb_Opts);

    // Shutting down the memory manager must be done after all the Free_Mem
    // calls have been made to balance their Alloc_Mem calls.
    //
    Shutdown_Pools();
}
