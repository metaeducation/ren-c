//
//  File: %b-init.c
//  Summary: "initialization functions"
//  Section: bootstrap
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2021 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
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
//=//// NOTES //////////////////////////////////////////////////////////////=//
//
// * The core language startup process does not include any command-line
//   processing.  That is left up to the API client and whether such processing
//   is relevant.  If it is, then tools like PARSE are available to use.  So
//   if any switches are needed to affect the boot process itself, those are
//   currently done with environment variables.
//
// * In order to make sure startup and shutdown can balance, during shutdown
//   the libRebol API will call shutdown, then startup, then shutdown again.
//   So if you're seeing slow performance on shutdown, check the debug flag.
//

#include "sys-core.h"


//
//  Check_Basics: C
//
// Initially these checks were in the debug build only.  However, they are so
// foundational that it's probably worth getting a coherent crash in any build
// where these tests don't work.
//
static void Check_Basics(void)
{
    //=//// CHECK CELL SIZE ///////////////////////////////////////////////=//

    // The system is designed with the intent that a cell is 4x(32-bit) on
    // 32-bit platforms and 4x(64-bit) on 64-bit platforms.  It's a critical
    // performance point.  For the moment we consider it to be essential
    // enough that the system that it refuses to run if not true.
    //
    // But if someone is in an odd situation with a larger sized cell--and
    // it's an even multiple of ALIGN_SIZE--it may still work.  For instance:
    // the DEBUG_TRACK_EXTEND_CELLS mode doubles the cell size to carry the
    // file, line, and tick of their initialization (or last Touch_Cell()).
    // Define UNUSUAL_CELL_SIZE to bypass this check.

    Size cell_size = sizeof(Cell);  // in variable avoids warning

  #if UNUSUAL_CELL_SIZE
    if (cell_size % ALIGN_SIZE != 0)
        panic ("size of cell does not evenly divide by ALIGN_SIZE");
  #else
    if (cell_size != sizeof(void*) * 4)
        panic ("size of cell is not sizeof(void*) * 4");

    Size stub_size = sizeof(Cell) * 2;

    #if DEBUG_FLEX_ORIGINS || DEBUG_COUNT_TICKS
      stub_size += sizeof(void*) * 2;
    #endif

    assert(sizeof(Stub) == stub_size);
    UNUSED(stub_size);
  #endif

    //=//// CHECK STUB INFO PLACEMENT ///////////////////////////////////=//

    // Stub historically placed the `info` bits exactly after `content` so
    // they could do double-duty as an array terminator when the content
    // was a singular Cell and enumerated as an Array.  But arrays are now
    // enumerated according to their stored length, and only have termination
    // in some debug builds.  However the phenomenon still has some leverage
    // by ensuring the bit corresponding to "not a cell" is set in the
    // info field is set--which helps catch a few stray reads or writes.

  blockscope {
    Size offset = offsetof(Stub, info);  // variable avoids warning
    if (offset - offsetof(Stub, content) != sizeof(Cell))
        panic ("bad structure alignment for internal array termination"); }

    //=//// CHECK BYTE-ORDERING SENSITIVE FLAGS //////////////////////////=//

    // See the %sys-node.h file for an explanation of what these are, and
    // why having them work is fundamental to the API.

    Flags flags
        = FLAG_LEFT_BIT(5) | FLAG_SECOND_BYTE(21) | FLAG_SECOND_UINT16(1975);

    Byte m = FIRST_BYTE(&flags);  // 6th bit from left set (0b00000100 is 4)
    Byte d = SECOND_BYTE(&flags);
    uint16_t y = SECOND_UINT16(&flags);
    if (m != 4 or d != 21 or y != 1975) {
      #if !defined(NDEBUG)
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
            return Guess_If_Stack_Grows_Up(&i);  // RECURSION: avoids inlining
        if (p < &i)  // !!! this comparison is undefined behavior
            return true;  // upward
        return false;  // downward
    }
#endif


//
//  Startup_Lib: C
//
// Since no good literal form exists, the %sysobj.r file uses the words.  They
// have to be defined before the point that it runs (along with the natives).
//
static void Startup_Lib(void)
{
    Context* lib = Alloc_Context_Core(REB_MODULE, 1, NODE_FLAG_MANAGED);
    ensure(nullptr, Lib_Context_Value) = Alloc_Element();
    Init_Context_Cell(Lib_Context_Value, REB_MODULE, lib);
    ensure(nullptr, Lib_Context) = VAL_CONTEXT(Lib_Context_Value);

  //=//// INITIALIZE LIB PATCHES ///////////////////////////////////////////=//

    assert(FIRST_BYTE(&PG_Lib_Patches[0]) == 0);  // pre-boot state
    FIRST_BYTE(&PG_Lib_Patches[0]) = FREE_POOLUNIT_BYTE;

    for (REBLEN i = 1; i < LIB_SYMS_MAX; ++i) {  // skip SYM_0
        Array* patch = Make_Array_Core_Into(
            &PG_Lib_Patches[i],
            1,
            FLAG_FLAVOR(PATCH)  // checked when setting INODE(PatchContext)
            | NODE_FLAG_MANAGED
            //
            // Note: While it may seem that context keeps the lib alive and
            // not vice-versa (which marking the context in link might suggest)
            // the reason for this is when patches are cached in variables;
            // then the variable no longer refers directly to the module.
            //
            | FLEX_FLAG_LINK_NODE_NEEDS_MARK
            | FLEX_FLAG_INFO_NODE_NEEDS_MARK
        );

        INODE(PatchContext, patch) = nullptr;  // signals unused
        LINK(PatchReserved, patch) = nullptr;
        MISC(PatchHitch, patch) = nullptr;
        assert(Is_Cell_Poisoned(Stub_Cell(patch)));
        TRACK(Erase_Cell(Stub_Cell(patch)));  // Lib(XXX) starts unreadable
    }

  //=//// INITIALIZE EARLY BOOT USED VALUES ////////////////////////////////=//

    // These have various applications, such as BLANK! is used during scanning
    // to build a path like `/a/b` out of an array with [_ a b] in it.  Since
    // the scanner is also what would load code like (blank: '_), we need to
    // seed the values to get the ball rolling.

    Set_Cell_Flag(Init_Nulled(force_Lib(NULL)), PROTECTED);
    assert(Is_Inhibitor(Lib(NULL)) and Is_Nulled(Lib(NULL)));

    Set_Cell_Flag(Init_Okay(force_Lib(OKAY)), PROTECTED);
    assert(Is_Trigger(Lib(OKAY)) and Is_Okay(Lib(OKAY)));

    Set_Cell_Flag(Init_Quasi_Void(force_Lib(QUASI_VOID)), PROTECTED);
    assert(Is_Trigger(Lib(QUASI_VOID)));

    Set_Cell_Flag(Init_Blank(force_Lib(BLANK)), PROTECTED);
    assert(Is_Trigger(Lib(BLANK)) and Is_Blank(Lib(BLANK)));

    Set_Cell_Flag(
        Init_Quasi_Null(force_Lib(QUASI_NULL)),
        PROTECTED
    );
    assert(
        Is_Trigger(Lib(QUASI_NULL))
        and Is_Quasi_Null(Lib(QUASI_NULL))
    );

    // !!! Other constants are just initialized as part of Startup_Base().
}


//
//  Shutdown_Lib: C
//
static void Shutdown_Lib(void)
{
    // !!! Since PG_Lib_Patches are array stubs that live outside the pools,
    // the Shutdown_GC() will not kill them off.  We want to make sure the
    // variables are Freshen_Cell() and that the patches look empty in case the
    // Startup() gets called again.
    //
    assert(Is_Node_Free(&PG_Lib_Patches[0]));
    FIRST_BYTE(&PG_Lib_Patches[0]) = 0;  // pre-boot state

    for (REBLEN i = 1; i < LIB_SYMS_MAX; ++i) {
        Array* patch = &PG_Lib_Patches[i];

        if (INODE(PatchContext, patch) == nullptr)
            continue;  // was never initialized !!! should it not be in lib?

        Erase_Cell(Stub_Cell(patch));  // may be PROTECTED, can't Freshen_Cell()
        Decay_Flex(patch);

        // !!! Typically nodes aren't zeroed out when they are freed.  Since
        // this one is a global, it is set to nullptr just to indicate that
        // the freeing process happened.  Should all nodes be zeroed?
        //
        INODE(PatchContext, patch) = nullptr;
        LINK(PatchReserved, patch) = nullptr;
        MISC(PatchHitch, patch) = nullptr;
    }

    rebReleaseAndNull(&Lib_Context_Value);
    Lib_Context = nullptr;
}


static Element* Make_Locked_Tag(const char *utf8) { // helper
    Element* t = cast(Element*, rebText(utf8));
    HEART_BYTE(t) = REB_TAG;

    Force_Value_Frozen_Deep(t);
    return t;
}

//
//  Init_Action_Spec_Tags: C
//
// FUNC and PROC search for these tags, like ~null~ and <local>.  They are
// natives and run during bootstrap, so these string comparisons are
// needed.
//
static void Init_Action_Spec_Tags(void)
{
    ensure(nullptr, Root_With_Tag) = Make_Locked_Tag("with");
    ensure(nullptr, Root_Variadic_Tag) = Make_Locked_Tag("variadic");
    ensure(nullptr, Root_End_Tag) = Make_Locked_Tag("end");
    ensure(nullptr, Root_Maybe_Tag) = Make_Locked_Tag("maybe");
    ensure(nullptr, Root_Local_Tag) = Make_Locked_Tag("local");
    ensure(nullptr, Root_Const_Tag) = Make_Locked_Tag("const");
    ensure(nullptr, Root_Unrun_Tag) = Make_Locked_Tag("unrun");

    ensure(nullptr, Root_Here_Tag) = Make_Locked_Tag("here");  // used by PARSE
}

static void Shutdown_Action_Spec_Tags(void)
{
    rebReleaseAndNull(&Root_With_Tag);
    rebReleaseAndNull(&Root_Variadic_Tag);
    rebReleaseAndNull(&Root_End_Tag);
    rebReleaseAndNull(&Root_Maybe_Tag);
    rebReleaseAndNull(&Root_Local_Tag);
    rebReleaseAndNull(&Root_Const_Tag);
    rebReleaseAndNull(&Root_Unrun_Tag);

    rebReleaseAndNull(&Root_Here_Tag);  // used by PARSE
}


//
//  Startup_Empty_Arrays: C
//
// Generic read-only empty array, which will be put into EMPTY_BLOCK when
// Alloc_Value() is available.  Note it's too early for ARRAY_HAS_FILE_LINE.
//
// Warning: GC must not run before Init_Root_Vars() puts it in an API node!
//
static void Startup_Empty_Arrays(void)
{
  blockscope {
    Array* a = Make_Array_Core(1, NODE_FLAG_MANAGED);
    Set_Flex_Len(a, 1);
    Init_Quasi_Null(Array_At(a, 0));
    Freeze_Array_Deep(a);
    PG_1_Quasi_Null_Array = a;
  }

  blockscope {
    Array* a = Make_Array_Core(1, NODE_FLAG_MANAGED);
    Set_Flex_Len(a, 1);
    Init_Quasi_Void(Array_At(a, 0));
    Freeze_Array_Deep(a);
    PG_1_Quasi_Void_Array = a;
  }

    // "Empty" PATH!s that look like `/` are actually a WORD! cell format
    // under the hood.  This allows them to have bindings and do double-duty
    // for actions like division or other custom purposes.  But when they
    // are accessed as an array, they give two blanks `[_ _]`.
    //
  blockscope {
    Array* a = Make_Array_Core(2, NODE_FLAG_MANAGED);
    Set_Flex_Len(a, 2);
    Init_Blank(Array_At(a, 0));
    Init_Blank(Array_At(a, 1));
    Freeze_Array_Deep(a);
    PG_2_Blanks_Array = a;
  }
}

static void Shutdown_Empty_Arrays(void) {
    PG_2_Blanks_Array = nullptr;
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
    // Simple isolated values, not available via lib, e.g. not Lib(BLANK)

    Init_Nothing(&PG_Nothing_Value);
    Set_Cell_Flag(&PG_Nothing_Value, PROTECTED);  // prevent overwriting
    assert(Is_Nothing(NOTHING_VALUE));

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

    Init_Return_Signal(&PG_R_Thrown, C_THROWN);
    Init_Return_Signal(&PG_R_Redo_Unchecked, C_REDO_UNCHECKED);
    Init_Return_Signal(&PG_R_Redo_Checked, C_REDO_CHECKED);
    Init_Return_Signal(&PG_R_Continuation, C_CONTINUATION);
    Init_Return_Signal(&PG_R_Delegation, C_DELEGATION);
    Init_Return_Signal(&PG_R_Suspend, C_SUSPEND);

    ensure(nullptr, Root_Empty_Block) = Init_Block(
        Alloc_Value(),
        PG_Empty_Array
    );
    Force_Value_Frozen_Deep(Root_Empty_Block);

    // Note: has to be a BLOCK!, 2-element blank paths use SYM__SLASH_1_
    //
    ensure(nullptr, Root_2_Blanks_Block) = Init_Block(
        Alloc_Value(),
        PG_2_Blanks_Array
      );
    Force_Value_Frozen_Deep(Root_2_Blanks_Block);

    ensure(nullptr, Root_Heavy_Null) = Init_Block(
        Alloc_Value(),
        PG_1_Quasi_Null_Array
      );
    Force_Value_Frozen_Deep(Root_Heavy_Null);

    ensure(nullptr, Root_Heavy_Void) = Init_Block(
        Alloc_Value(),
        PG_1_Quasi_Void_Array
      );
    Force_Value_Frozen_Deep(Root_Heavy_Void);

    ensure(nullptr, Root_Feed_Null_Substitute) = Init_Quasi_Null(Alloc_Value());
    Set_Cell_Flag(Root_Feed_Null_Substitute, FEED_NOTE_META);
    Force_Value_Frozen_Deep(Root_Feed_Null_Substitute);

    // Note: rebText() can't run yet, review.
    //
    String* nulled_uni = Make_String(1);

  #if !defined(NDEBUG)
    Codepoint test_nul;
    Utf8_Next(&test_nul, String_At(nulled_uni, 0));
    assert(test_nul == '\0');
    assert(String_Len(nulled_uni) == 0);
  #endif

    ensure(nullptr, Root_Empty_Text) = Init_Text(Alloc_Value(), nulled_uni);
    Force_Value_Frozen_Deep(Root_Empty_Text);

    Binary* bzero = Make_Binary(0);
    ensure(nullptr, Root_Empty_Binary) = Init_Blob(Alloc_Value(), bzero);
    Force_Value_Frozen_Deep(Root_Empty_Binary);
}

static void Shutdown_Root_Vars(void)
{
    Erase_Cell(&PG_Nothing_Value);

    Erase_Cell(&PG_R_Thrown);
    Erase_Cell(&PG_R_Redo_Unchecked);
    Erase_Cell(&PG_R_Redo_Checked);
    Erase_Cell(&PG_R_Continuation);
    Erase_Cell(&PG_R_Delegation);
    Erase_Cell(&PG_R_Suspend);

    rebReleaseAndNull(&Root_Empty_Text);
    rebReleaseAndNull(&Root_Empty_Block);
    rebReleaseAndNull(&Root_2_Blanks_Block);
    rebReleaseAndNull(&Root_Heavy_Null);
    rebReleaseAndNull(&Root_Heavy_Void);
    rebReleaseAndNull(&Root_Feed_Null_Substitute);
    rebReleaseAndNull(&Root_Empty_Binary);
}


//
//  Init_System_Object: C
//
// Evaluate the system object and create the global SYSTEM word.  We do not
// BIND_ALL here to keep the internal system words out of the global context.
// (See also N_context() which creates the subobjects of the system object.)
//
static void Init_System_Object(
    const Element* boot_sysobj_spec,
    Array* datatypes_catalog,
    Array* natives_catalog,
    Array* generics_catalog,
    Context* errors_catalog
) {
    assert(VAL_INDEX(boot_sysobj_spec) == 0);
    const Element* spec_tail;
    Element* spec_head
        = Cell_List_At_Known_Mutable(&spec_tail, boot_sysobj_spec);

    // Create the system object from the sysobj block (defined in %sysobj.r)
    //
    Context* system = Make_Context_Detect_Managed(
        REB_OBJECT, // type
        spec_head, // scan for toplevel set-words
        spec_tail,
        nullptr  // parent
    );

    // Create a global value for it in the Lib context, so we can say things
    // like `system.contexts` (also protects newly made context from GC).
    //
    // We also make a shorthand synonym for this as SYS.  In R3-Alpha, SYS
    // was a context containing some utility functions, some of which were
    // meant to be called from the core when writing those utilities in pure
    // C would be tedious.  But we put those functions in a module called
    // UTIL in SYSTEM, and then abbreviate SYS as a synonym for SYSTEM.
    // Hence the utilities are available as SYS.UTIL
    //
    Init_Object(force_Lib(SYSTEM), system);
    Init_Object(force_Lib(SYS), system);

    DECLARE_VALUE (sysobj_spec_virtual);
    Copy_Cell(sysobj_spec_virtual, boot_sysobj_spec);

    Virtual_Bind_Deep_To_Existing_Context(
        sysobj_spec_virtual,
        system,
        nullptr,  // !!! no binder made at present
        REB_WORD  // all internal refs are to the object
    );

    // Evaluate the block (will eval CONTEXTs within).
    //
    DECLARE_ATOM (result);
    if (Do_Any_List_At_Throws(result, sysobj_spec_virtual, SPECIFIED))
        panic (result);
    if (not Is_Anti_Word_With_Id(result, SYM_DONE))  // ~done~ sanity check
        panic (result);

    // Init_Action_Adjunct_Shim() made Root_Action_Adjunct as a bootstrap hack
    // since it needed to make function adjunct information for natives before
    // %sysobj.r's code could run using those natives.  But make sure what it
    // made is actually identical to the definition in %sysobj.r.
    //
    assert(
        0 == CT_Context(
            Get_System(SYS_STANDARD, STD_ACTION_ADJUNCT),
            Root_Action_Adjunct,
            true  // "strict equality"
        )
    );

    // Create SYSTEM.CATALOG.* for datatypes, natives, generics, errors
    //
    Init_Block(Get_System(SYS_CATALOG, CAT_DATATYPES), datatypes_catalog);
    Init_Block(Get_System(SYS_CATALOG, CAT_NATIVES), natives_catalog);
    Init_Block(Get_System(SYS_CATALOG, CAT_ACTIONS), generics_catalog);
    Init_Object(Get_System(SYS_CATALOG, CAT_ERRORS), errors_catalog);

    // Create SYSTEM.CODECS object
    //
    Init_Object(
        Freshen_Cell(Get_System(SYS_CODECS, 0)),
        Alloc_Context_Core(REB_OBJECT, 10, NODE_FLAG_MANAGED)
    );

    // The "standard error" template was created as an OBJECT!, because the
    // `make error!` functionality is not ready when %sysobj.r runs.  Fix
    // up its archetype so that it is an actual ERROR!.
    //
  blockscope {
    Value* std_error = Get_System(SYS_STANDARD, STD_ERROR);
    Context* c = VAL_CONTEXT(std_error);
    HEART_BYTE(std_error) = REB_ERROR;

    Value* rootvar = CTX_ROOTVAR(c);
    assert(Get_Cell_Flag(rootvar, PROTECTED));
    HEART_BYTE(rootvar) = REB_ERROR;
  }
}


//
//  Init_Contexts_Object: C
//
// This sets up the system.contexts object.
//
// !!! One of the critical areas in R3-Alpha that was not hammered out
// completely was the question of how the binding process gets started, and
// how contexts might inherit or relate.
//
// However, the basic model for bootstrap is that the "user context" is the
// default area for new code evaluation.  It starts out as a copy of an
// initial state set up in the lib context.  When native routines or other
// content gets overwritten in the user context, it can be borrowed back
// from `system.contexts.lib` (typically aliased as "lib" in the user context).
//
static void Init_Contexts_Object(void)
{
    Copy_Cell(Get_System(SYS_CONTEXTS, CTX_LIB), Lib_Context_Value);

    // We don't initialize the USER context...yet.  Make it more obvious what
    // is wrong if it's used during boot.
    //
    const char *label = "startup-mezz-not-finished-yet";
    Init_Anti_Word(
        Get_System(SYS_CONTEXTS, CTX_USER),
        Intern_UTF8_Managed(cb_cast(label), strsize(label))
    );
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
void Startup_Core(void)
{

//=//// INITIALIZE TICK COUNT /////////////////////////////////////////////=//

  #if DEBUG_COUNT_TICKS
    TG_tick = 0;
  #endif

//=//// INITIALIZE BASIC DIAGNOSTICS //////////////////////////////////////=//

  #if defined(TEST_EARLY_BOOT_PANIC)
    panic ("early panic test"); // should crash
  #elif defined(TEST_EARLY_BOOT_FAIL)
    fail ("early fail test"); // same as panic (crash)
  #endif

  #if DEBUG_HAS_PROBE
    PG_Probe_Failures = false;
  #endif

    // Globals
    PG_Boot_Phase = BOOT_START;

    Check_Basics();

//=//// INITIALIZE MEMORY AND ALLOCATORS //////////////////////////////////=//

    Startup_Signals();  // allocation can set signal flags for recycle/etc.

    Startup_Pools(0);  // performs allocation, calls Set_Trampoline_Flag()
    Startup_GC();

    Startup_Raw_Print();
    Startup_Scanner();
    Startup_String();

//=//// INITIALIZE API ////////////////////////////////////////////////////=//

    // The API is one means by which variables can be made whose lifetime is
    // indefinite until program shutdown.  In R3-Alpha this was done with
    // boot code that laid out some fixed structure arrays, but it's more
    // general to do it this way.

    Init_Char_Cases();
    Startup_CRC();             // For word hashing
    Set_Random(0);
    Startup_Interning();

    Startup_Feeds();

    Startup_Collector();
    Startup_Mold(MIN_COMMON / 4);

    Startup_Data_Stack(STACK_MIN / 4);
    Startup_Trampoline();  // uses Canon() in File_Of_Level() currently

    Startup_Api();

    Startup_Symbols();
    Startup_Empty_Arrays();

//=//// CREATE GLOBAL OBJECTS /////////////////////////////////////////////=//

    Init_Root_Vars();    // Special REBOL values per program

    Init_Action_Spec_Tags();  // Note: requires mold buffer be initialized

//=//// CREATE SYSTEM MODULES //////////////////////////////////////////////=//

    Startup_Lib();  // establishes Lib_Context and Lib_Context_Value

  #if !defined(NDEBUG)
    Assert_Pointer_Detection_Working();  // uses root Flex/Values to test
  #endif


//=//// LOAD BOOT BLOCK ///////////////////////////////////////////////////=//

    // The %make-boot.r process takes all the various definitions and
    // mezzanine code and packs it into one compressed string in
    // %tmp-boot-block.c which gets embedded into the executable.  This
    // includes the type list, word list, error message templates, system
    // object, mezzanines, etc.

    size_t utf8_size;
    const int max = -1;  // trust size in gzip data
    Byte* utf8 = Decompress_Alloc_Core(
        &utf8_size,
        Boot_Block_Compressed,
        Boot_Block_Compressed_Size,
        max,
        SYM_GZIP
    );

    // The boot code contains portions that are supposed to be interned to the
    // SYS.UTIL context instead of the LIB context.  But the Base and Mezzanine
    // are interned to the Lib, so go ahead and take advantage of that.
    //
    // (We could separate the text of the SYS.UTIL portion out, and scan that
    // separately to avoid the extra work.  Not a high priority.)
    //
    const String* tmp_boot = Intern_Unsized_Managed("tmp-boot.r");  // const
    Push_GC_Guard(tmp_boot);  // recycle torture frees on scanner first push!
    Array* boot_array = Scan_UTF8_Managed(
        tmp_boot,
        utf8,
        utf8_size
    );
    Drop_GC_Guard(tmp_boot);
    Push_GC_Guard(boot_array); // managed, so must be guarded

    rebFree(utf8); // don't need decompressed text after it's scanned

    BOOT_BLK *boot = cast(BOOT_BLK*,
        Array_Head(Cell_Array_Known_Mutable(Array_Head(boot_array)))
    );

    // Symbol_Id(), Cell_Word_Id() and Canon(XXX) now available

    PG_Boot_Phase = BOOT_LOADED;

//=//// CREATE BASIC VALUES ///////////////////////////////////////////////=//

    // Before any code can start running (even simple bootstrap code), some
    // basic words need to be defined.  For instance: You can't run %sysobj.r
    // unless `true` and `false` have been added to the Lib_Context--they'd be
    // undefined.  And while analyzing the function specs during the
    // definition of natives, things like the <maybe> tag are needed as a
    // basis for comparison to see if a usage matches that.

    Array* datatypes_catalog = Startup_Datatypes(
        Cell_Array_Known_Mutable(&boot->typespecs)
    );
    Manage_Flex(datatypes_catalog);
    Push_GC_Guard(datatypes_catalog);

    // Startup_Type_Predicates() uses symbols, data stack, and adds words
    // to lib--not available until this point in time.
    //
    Startup_Type_Predicates();

//=//// RUN CODE BEFORE ERROR HANDLING INITIALIZED ////////////////////////=//

    // boot->natives is from the automatically gathered list of natives found
    // by scanning comments in the C sources for `native: ...` declarations.
    //
    INIT_SPECIFIER(&boot->natives, Lib_Context);
    Array* natives_catalog = Startup_Natives(&boot->natives);
    Manage_Flex(natives_catalog);
    Push_GC_Guard(natives_catalog);

    // boot->generics is the list in %generics.r
    //
    INIT_SPECIFIER(&boot->generics, Lib_Context);
    Array* generics_catalog = Startup_Generics(&boot->generics);
    Manage_Flex(generics_catalog);
    Push_GC_Guard(generics_catalog);

    // boot->errors is the error definition list from %errors.r
    //
    Context* errors_catalog = Startup_Errors(&boot->errors);
    Push_GC_Guard(errors_catalog);

    INIT_SPECIFIER(&boot->sysobj, Lib_Context);
    Init_System_Object(
        &boot->sysobj,
        datatypes_catalog,
        natives_catalog,
        generics_catalog,
        errors_catalog
    );

    Drop_GC_Guard(errors_catalog);
    Drop_GC_Guard(generics_catalog);
    Drop_GC_Guard(natives_catalog);
    Drop_GC_Guard(datatypes_catalog);

    Init_Contexts_Object();

    PG_Boot_Phase = BOOT_ERRORS;

  #if defined(TEST_MID_BOOT_PANIC)
    panic (EMPTY_ARRAY); // panics should be able to give some details by now
  #elif defined(TEST_MID_BOOT_FAIL)
    fail ("mid boot fail"); // DEBUG->assert, RELEASE->panic
  #endif

    // Pre-make the stack overflow error (so it doesn't need to be made
    // during a stack overflow).  Error creation machinery depends heavily
    // on the system object being initialized, so this can't be done until
    // now.
    //
    Startup_Stackoverflow();

    assert(TOP_INDEX == 0 and TOP_LEVEL == BOTTOM_LEVEL);

//=//// RUN MEZZANINE CODE NOW THAT ERROR HANDLING IS INITIALIZED /////////=//

    // By this point, the Lib_Context contains basic definitions for things
    // like true, false, the natives, and the generics.
    //
    // There is theoretically some level of error recovery that could be done
    // here.  e.g. the evaluator works, it just doesn't have many functions you
    // would expect.  How bad it is depends on whether base and sys ran, so
    // perhaps only errors running "mezz" should be tolerated.  But the
    // console may-or-may-not run.
    //
    // For now, assume any failure in code running doing boot is fatal.
    //
    // (Handling of Ctrl-C is an issue...if halt cannot be handled cleanly,
    // it should be set up so that the user isn't even *able* to request a
    // halt at this boot phase.)

    PG_Boot_Phase = BOOT_MEZZ;

 //=//// BASE STARTUP ////////////////////////////////////////////////////=//

    // The code in "base" is the lowest level of initialization written as
    // Rebol code.  This is where things like `+` being an infix form of ADD is
    // set up, or FIRST being a specialization of PICK.  It also has wrappers
    // for more basic natives that handle aspects that are easier to write in
    // usermode than in C.

    rebElide(
        //
        // Create actual variables for top-level SET-WORD!s only, and run.
        //
        "bind/only/set", &boot->base, Lib_Context_Value,
        "evaluate inside", Lib_Context_Value, rebQ(&boot->base)
        //
        // Note: ENSURE not available yet.
    );

  //=//// SYSTEM.UTIL STARTUP /////////////////////////////////////////////=//

    // The SYSTEM.UTIL context contains supporting Rebol code for implementing
    // "system" features.  It is lower-level than the LIB context, but has
    // natives, generics, and the definitions from Startup_Base() available.
    //
    // See the helper SysUtil() for a quick way of getting at the functions by
    // their symbol.
    //
    // (Note: The SYSTEM.UTIL context was renamed from just "SYS" to avoid
    //  being confused with "the system object", which is a different thing.
    //  Better was to say SYS was just an abbreviation for SYSTEM.)

    Context* util = Alloc_Context_Core(REB_MODULE, 1, NODE_FLAG_MANAGED);
    node_LINK(NextVirtual, util) = Lib_Context;
    ensure(nullptr, Sys_Util_Module) = Alloc_Element();
    Init_Context_Cell(Sys_Util_Module, REB_MODULE, util);
    ensure(nullptr, Sys_Context) = VAL_CONTEXT(Sys_Util_Module);

    rebElide(
        //
        // The scan of the boot block interned everything to Lib_Context, but
        // we want to overwrite that with the Sys_Context here.
        //
        "sys.util:", Sys_Util_Module,

        "bind/only/set", &boot->system_util, Sys_Util_Module,
        "if not equal? '~done~",
          "^ evaluate inside", Sys_Util_Module, rebQ(&boot->system_util),
            "[fail {sys.util}]",

        // SYS contains the implementation of the module machinery itself, so
        // we don't have MODULE or EXPORT available.  Do the exports manually,
        // and then import the results to lib.
        //
        "set-adjunct sys.util make object! [",
            "Name: 'System",  // this is MAKE OBJECT!, not MODULE, must quote
            "Exports: [module load load-value decode encode encoding-of]",
        "]",
        "sys.util/import*", Lib_Context_Value, Sys_Util_Module
    );

    // !!! It was a stated goal at one point that it should be possible to
    // protect the entire system object and still run the interpreter.  That
    // was commented out in R3-Alpha
    //
    //    comment [if :get $lib/secure [protect-system-object]]

  //=//// MEZZ STARTUP /////////////////////////////////////////////////////=//

    rebElide(
        // (It's not necessarily the greatest idea to have LIB be this
        // flexible.  But as it's not hardened from mutations altogether then
        // prohibiting it doesn't offer any real security...and only causes
        // headaches when trying to do something weird.)

        // Create actual variables for top-level SET-WORD!s only, and run.
        //
        "bind/only/set", &boot->mezz, Lib_Context_Value,
        "evaluate inside", Lib_Context_Value, rebQ(&boot->mezz)
    );

  //=//// MAKE USER CONTEXT ////////////////////////////////////////////////=//

    // None of the above code should have needed the "user" context, which is
    // purely application-space.  We probably shouldn't even create it during
    // boot at all.  But at the moment, code like JS-NATIVE or TCC natives
    // need to bind the code they run somewhere.  It's also where API called
    // code runs if called from something like an int main() after boot.
    //
    // Doing this as a proper module creation gives us IMPORT and INTERN (as
    // well as EXPORT...?  When do you export from the user context?)
    //
    // rebElide() here runs in the Lib_Context by default, which means the
    // block we are passing evaluatively as the module body will evaluate
    // and carry the lib context.  This achieves the desired inheritance,
    // because when we say EVAL INSIDE SYSTEM.CONTEXTS.USER CODE we want the
    // code to find definitions in user as well as in lib.
    //
    rebElide(
        "system.contexts.user: module [Name: User] []"
    );

    ensure(nullptr, User_Context_Value) = cast(Element*, Copy_Cell(
        Alloc_Value(),
        Get_System(SYS_CONTEXTS, CTX_USER)
    ));
    rebUnmanage(User_Context_Value);
    User_Context = VAL_CONTEXT(User_Context_Value);

  //=//// FINISH UP ///////////////////////////////////////////////////////=//

    assert(TOP_INDEX == 0 and TOP_LEVEL == BOTTOM_LEVEL);

    Drop_GC_Guard(boot_array);

    PG_Boot_Phase = BOOT_DONE;

  #if !defined(NDEBUG)
    Check_Memory_Debug(); // old R3-Alpha check, call here to keep it working
  #endif

    // We don't actually load any extensions during the core startup.  The
    // builtin extensions can be selectively loaded in whatever order the
    // API client wants (they may not want to load all extensions that are
    // built in that were available all the time).
    //
    Startup_Extension_Loader();

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
void Shutdown_Core(bool clean)
{
    assert(g_ts.jump_list == nullptr);

    // Shutting down extensions is currently considered semantically mandatory,
    // as it may flush writes to files (filesystem extension) or do other
    // work.  If you really want to do a true "unclean shutdown" you can always
    // call exit().
    //
    Shutdown_Extension_Loader();

    Run_All_Handle_Cleaners();  // there may be rebFree() and other API code

  #if !defined(NDEBUG)
    Check_Memory_Debug(); // old R3-Alpha check, call here to keep it working
  #endif

    if (not clean)
        return;

    // !!! Currently the molding logic uses a test of the Boot_Phase to know
    // if it's safe to check the system object for how many digits to mold.
    // This isn't ideal, but if we are to be able to use PROBE() or other
    // molding-based routines during shutdown, we have to signal not to look
    // for that setting in the system object.
    //
    PG_Boot_Phase = BOOT_START;

    Shutdown_Data_Stack();

    Shutdown_Stackoverflow();
    Shutdown_Typesets();

    Shutdown_Natives();
    Shutdown_Action_Spec_Tags();
    Shutdown_Root_Vars();

    Shutdown_Datatypes();

    Shutdown_Lib();

    rebReleaseAndNull(&Sys_Util_Module);
    Sys_Context = nullptr;

    rebReleaseAndNull(&User_Context_Value);
    User_Context = nullptr;

    Shutdown_Feeds();

    Shutdown_Trampoline();  // all API calls (e.g. rebRelease()) before this
    Shutdown_Api();

//=//// ALL MANAGED STUBS MUST HAVE THE KEEPALIVE REFERENCES GONE NOW /////=//

    Sweep_Stubs();  // go ahead and free all managed Stubs

    assert(Is_Cell_Erased(&g_ts.thrown_arg));
    assert(Is_Cell_Erased(&g_ts.thrown_label));
    assert(g_ts.unwind_level == nullptr);

    Shutdown_Mold();
    Shutdown_Collector();
    Shutdown_Raw_Print();
    Shutdown_CRC();
    Shutdown_String();
    Shutdown_Scanner();

    Shutdown_Symbols();
    Shutdown_Interning();

    Shutdown_Char_Cases();  // case needed for hashes in Shutdown_Symbols()

    Shutdown_GC();

    Shutdown_Empty_Arrays();  // should have been freed.

    // Shutting down the memory manager must be done after all the Free_Mem
    // calls have been made to balance their Alloc_Mem calls.
    //
    Shutdown_Pools();
}
