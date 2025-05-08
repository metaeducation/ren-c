//
//  file: %b-init.c
//  summary: "initialization functions"
//  section: bootstrap
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// 1. Initially these checks were #if RUNTIME_CHECKS only.  However, they are
//    so foundational that it's probably worth getting a coherent crash in any
//    build where these tests don't work.
//
// 2. The system is designed with the intent that a cell is 4x(32-bit) on
//    32-bit platforms and 4x(64-bit) on 64-bit platforms.  It's a critical
//    performance point.  For the moment we consider it to be essential
//    enough that the system that it refuses to run if not true.
//
//    But if someone is in an odd situation with a larger sized cell--and
//    it's an even multiple of ALIGN_SIZE--it may still work.  For instance:
//    the DEBUG_TRACK_EXTEND_CELLS mode doubles the cell size to carry the
//    file, line, and tick of their initialization (or last Touch_Cell()).
//    Define UNUSUAL_CELL_SIZE to bypass this check.
//
// 3. Stub historically placed the `info` bits exactly after `content` so
//    they could do double-duty as an array terminator when the content was a
//    singular Cell and enumerated as an Array.  But arrays are now
//    enumerated according to their stored length, and only have termination
//    if DEBUG_POISON_FLEX_TAILS.  But the phenomenon still has some leverage
//    by ensuring the NODE_FLAG_CELL bit is clear in the info field--which
//    helps catch a few stray reads or writes.
//
// 4. See the %sys-node.h file for an explanation of what these are, and
//    why having them work is fundamental to the API.
//
static void Check_Basics(void)  // included even if NO_RUNTIME_CHECKS [1]
{
    Size cell_size = sizeof(Cell);  // in variable avoids warning

   //=//// CHECK CELL SIZE [2] ////////////////////////////////////////////=//

  #if UNUSUAL_CELL_SIZE  // e.g. if DEBUG_TRACK_EXTEND_CELLS
    if (cell_size % ALIGN_SIZE != 0)
        panic ("size of cell does not evenly divide by ALIGN_SIZE");
  #else
    if (cell_size != sizeof(void*) * 4)
        panic ("size of cell is not sizeof(void*) * 4");

    Size stub_size = sizeof(Cell) * 2;

    #if DEBUG_STUB_ORIGINS
      stub_size += sizeof(void*) * 2;
    #endif

    assert(sizeof(Stub) == stub_size);
    UNUSED(stub_size);
  #endif

   //=//// CHECK STUB INFO PLACEMENT (non-essential) [3] //////////////////=//

    Size offset = offsetof(Stub, info);  // variable avoids warning
    if (offset - offsetof(Stub, content) != sizeof(Cell))
        panic ("bad structure alignment for internal array termination");

   //=//// CHECK BYTE-ORDERING SENSITIVE FLAGS [4] ////////////////////////=//

    Flags flags
        = FLAG_LEFT_BIT(5) | FLAG_SECOND_BYTE(21) | FLAG_SECOND_UINT16(1975);

    Byte m = FIRST_BYTE(&flags);  // 6th bit from left set (0b00000100 is 4)
    Byte d = SECOND_BYTE(&flags);
    uint16_t y = SECOND_UINT16(&flags);
    if (m != 4 or d != 21 or y != 1975) {
      #if RUNTIME_CHECKS
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
    SeaOfVars* lib = Alloc_Sea_Core(NODE_FLAG_MANAGED);
    assert(Link_Inherit_Bind(lib) == nullptr);
    Tweak_Link_Inherit_Bind(lib, g_datatypes_context);

    assert(Is_Stub_Erased(&g_lib_patches[SYM_0]));  // leave invalid

    for (SymId16 id = 1; id <= MAX_SYM_LIB_PREMADE; ++id) {
        Patch* patch = &g_lib_patches[id];
        assert(Is_Stub_Erased(patch));  // pre-boot state

        patch->leader.bits = STUB_MASK_PATCH;

        assert(INFO_PATCH_SEA(patch) == nullptr);
        assert(LINK_PATCH_RESERVED(patch) == nullptr);
        Tweak_Info_Patch_Sea(patch, lib);

        Symbol* symbol =  &g_symbols.builtin_canons[id];
        assert(Misc_Hitch(symbol) == symbol);  // no module patches yet
        Tweak_Misc_Hitch(symbol, patch);  // ...but now it has one!
        Tweak_Misc_Hitch(patch, symbol);  // link back for singly-linked-list

        Init_Trash(Stub_Cell(patch));  // start as unset variable
    }

    ensure(nullptr, g_lib_context) = lib;
}


//
//  Shutdown_Lib: C
//
// Since g_lib_patches are array stubs that live outside the pools,
// Shutdown_GC() will not kill them off.  We want to make sure the variables
// are Erase_Cell() and that the patches are Erase_Stub() in case the
// Startup_Core() gets called again.
//
// 1. The managed g_lib_context SeaOfVars was GC'd in Sweep_Stubs() prior to
//    this function being called.  It wasn't nulled out so it could be used
//    in an assert here.  BUT...the C spec isn't completely clear on whether
//    comparing just the value of a free pointer is undefined behavior or not.
//    Some optimization levels might assume freed pointers are irrelevant and
//    do something implementation-defined.  (?)  Assume it works.
//
// 2. Since the GC never frees the builtin Lib patches, they don't get
//    "diminished" and unlinked from the Symbol's hitch list.  Rather than do
//    a Diminish_Stub() here, we can take the opportunity to make sure that
//    the lib patch really is the last hitch stuck on the symbol (otherwise
//    there was some kind of leak).
//
static void Shutdown_Lib(void)
{
    assert(Is_Stub_Erased(&g_lib_patches[SYM_0]));

    for (SymId16 id = 1; id <= MAX_SYM_LIB_PREMADE; ++id) {
        Patch* patch = &g_lib_patches[id];

        Force_Erase_Cell(Stub_Cell(patch));  // re-init to 0, overwrite PROTECT

        assert(INFO_PATCH_SEA(patch) == g_lib_context);  // note: freed [1]
        INFO_PATCH_SEA(patch) = nullptr;

        assert(LINK_PATCH_RESERVED(patch) == nullptr);

        Symbol* symbol = &g_symbols.builtin_canons[id];

        assert(Misc_Hitch(patch) == symbol);  // assert no other patches [2]
        assert(Misc_Hitch(symbol) == patch);
        Tweak_Misc_Hitch(symbol, symbol);

        Erase_Stub(patch);
    }

    g_lib_context = nullptr;  // do this last to have freed value on hand [1]
}


static Element* Make_Locked_Tag(const char *utf8) { // helper
    Element* t = cast(Element*, rebText(utf8));
    HEART_BYTE(t) = TYPE_TAG;

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
    ensure(nullptr, Root_Opt_Out_Tag) = Make_Locked_Tag("opt-out");
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
    rebReleaseAndNull(&Root_Opt_Out_Tag);
    rebReleaseAndNull(&Root_Local_Tag);
    rebReleaseAndNull(&Root_Const_Tag);
    rebReleaseAndNull(&Root_Unrun_Tag);

    rebReleaseAndNull(&Root_Here_Tag);  // used by PARSE
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
    // Return signals should only be accessed by macros which cast them as
    // as `const`, to avoid the risk of accidentally changing them.  (This
    // rule is broken by some special system code which `m_cast`s them for
    // the purpose of using them as directly recognizable pointers which
    // also look like values.)
    //
    // It is presumed that these types will never need to have GC behavior,
    // and thus can be stored safely in program globals without mention in
    // the root set.  Should that change, they could be explicitly added
    // to the GC's root set.

    Init_Bounce_Wild(g_bounce_thrown, C_THROWN);
    Init_Bounce_Wild(g_bounce_fail, C_FAIL);
    Init_Bounce_Wild(g_bounce_redo_unchecked, C_REDO_UNCHECKED);
    Init_Bounce_Wild(g_bounce_redo_checked, C_REDO_CHECKED);
    Init_Bounce_Wild(g_bounce_downshifted, C_DOWNSHIFTED);
    Init_Bounce_Wild(g_bounce_continuation, C_CONTINUATION);
    Init_Bounce_Wild(g_bounce_delegation, C_DELEGATION);
    Init_Bounce_Wild(g_bounce_suspend, C_SUSPEND);
    Init_Bounce_Wild(g_bounce_okay, C_OKAY);
    Init_Bounce_Wild(g_bounce_bad_intrinsic_arg, C_BAD_INTRINSIC_ARG);

    g_empty_array = Make_Source_Managed(0);
    Freeze_Source_Deep(g_empty_array);

    ensure(nullptr, g_empty_block) = Init_Block(
        Alloc_Value(),
        g_empty_array  // holds empty array alive
    );
    Force_Value_Frozen_Deep(g_empty_block);

  blockscope {
    Length len = 0;
    Array* a = Make_Array_Core(
        FLEX_MASK_VARLIST
            | NODE_FLAG_MANAGED, // Note: Rebind below requires managed context
        1 + len  // needs room for rootvar
    );
    Set_Flex_Len(a, 1 + len);
    Tweak_Misc_Varlist_Adjunct(a, nullptr);
    Tweak_Link_Inherit_Bind(a, nullptr);

    KeyList* keylist = Make_Flex(
        FLEX_MASK_KEYLIST | NODE_FLAG_MANAGED,
        KeyList,
        len  // no terminator, 0-based
    );

    Set_Flex_Used(keylist, len);

    Tweak_Bonus_Keylist_Unique(a, keylist);
    Tweak_Link_Keylist_Ancestor(keylist, keylist);  // terminate in self

    Tweak_Non_Frame_Varlist_Rootvar(a, TYPE_OBJECT);

    g_empty_varlist = cast(VarList*, a);

    ensure(nullptr, g_empty_object) = Init_Object(
        Alloc_Value(),
        g_empty_varlist  // holds empty varlist alive
    );
    Force_Value_Frozen_Deep(g_empty_object);
  }

  blockscope {  // keep array alive via stable API handle (META PACK, not PACK)
    Source* a = Alloc_Singular(FLEX_MASK_MANAGED_SOURCE);
    Init_Quasi_Null(Stub_Cell(a));
    Freeze_Source_Deep(a);
    ensure(nullptr, g_1_quasi_null_array) = a;
    ensure(nullptr, g_meta_heavy_null) = Init_Meta_Pack(Alloc_Value(), a);
    Force_Value_Frozen_Deep(g_meta_heavy_null);
  }

    ensure(nullptr, Root_Feed_Null_Substitute) = Init_Quasi_Null(Alloc_Value());
    Set_Cell_Flag(Root_Feed_Null_Substitute, FEED_NOTE_META);
    Protect_Cell(Root_Feed_Null_Substitute);

    // Note: rebText() can't run yet, review.
    //
    String* nulled_uni = Make_String(1);

  #if RUNTIME_CHECKS
    Codepoint test_nul;
    Utf8_Next(&test_nul, String_At(nulled_uni, 0));
    assert(test_nul == '\0');
    assert(String_Len(nulled_uni) == 0);
  #endif

    ensure(nullptr, g_empty_text) = Init_Text(Alloc_Value(), nulled_uni);
    Force_Value_Frozen_Deep(g_empty_text);

    Binary* bzero = Make_Binary(0);
    ensure(nullptr, g_empty_blob) = Init_Blob(Alloc_Value(), bzero);
    Force_Value_Frozen_Deep(g_empty_blob);

    ensure(nullptr, g_quasi_null) = Init_Quasi_Null(Alloc_Value());
    Protect_Cell(g_quasi_null);

    ensure(nullptr, g_trash) = Init_Trash(Alloc_Value());
    Protect_Cell(g_trash);

    ensure(nullptr, g_dispatcher_table) = Make_Flex(
        FLAG_FLAVOR(DISPATCHERTABLE) | STUB_FLAG_DYNAMIC,
        Flex,
        15
    );
}

static void Shutdown_Root_Vars(void)
{
    Free_Unmanaged_Flex(g_dispatcher_table);
    g_dispatcher_table = nullptr;

    Erase_Bounce_Wild(g_bounce_thrown);
    Erase_Bounce_Wild(g_bounce_fail);
    Erase_Bounce_Wild(g_bounce_redo_unchecked);
    Erase_Bounce_Wild(g_bounce_redo_checked);
    Erase_Bounce_Wild(g_bounce_downshifted);
    Erase_Bounce_Wild(g_bounce_continuation);
    Erase_Bounce_Wild(g_bounce_delegation);
    Erase_Bounce_Wild(g_bounce_suspend);
    Erase_Bounce_Wild(g_bounce_okay);
    Erase_Bounce_Wild(g_bounce_bad_intrinsic_arg);

    rebReleaseAndNull(&g_empty_text);
    rebReleaseAndNull(&g_empty_block);
    g_empty_array = nullptr;
    rebReleaseAndNull(&g_empty_object);
    g_empty_varlist = nullptr;
    rebReleaseAndNull(&g_meta_heavy_null);
    g_1_quasi_null_array = nullptr;
    rebReleaseAndNull(&Root_Feed_Null_Substitute);
    rebReleaseAndNull(&g_empty_blob);
    rebReleaseAndNull(&g_quasi_null);
    rebReleaseAndNull(&g_trash);
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
    VarList* errors_catalog
) {
    assert(VAL_INDEX(boot_sysobj_spec) == 0);
    const Element* spec_tail;
    Element* spec_head
        = Cell_List_At_Known_Mutable(&spec_tail, boot_sysobj_spec);

    // Create the system object from the sysobj block (defined in %sysobj.r)
    //
    VarList* system = Make_Varlist_Detect_Managed(
        COLLECT_ONLY_SET_WORDS,
        TYPE_OBJECT, // type
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
    Init_Object(Sink_Lib_Var(SYM_SYSTEM), system);
    Init_Object(Sink_Lib_Var(SYM_SYS), system);

    Use* use = Alloc_Use_Inherits(Cell_List_Binding(boot_sysobj_spec));
    Copy_Cell(Stub_Cell(use), Varlist_Archetype(system));

    DECLARE_ELEMENT (sysobj_spec_virtual);
    Copy_Cell(sysobj_spec_virtual, boot_sysobj_spec);
    Tweak_Cell_Binding(sysobj_spec_virtual, use);

    // Evaluate the block (will eval CONTEXTs within).
    //
    DECLARE_ATOM (result);
    if (Eval_Any_List_At_Throws(result, sysobj_spec_virtual, SPECIFIED))
        panic (result);
    if (not Is_Quasi_Word_With_Id(Decay_If_Unstable(result), SYM_END))
        panic (result);

    // Startup_Action_Adjunct_Shim() made Root_Action_Adjunct as bootstrap hack
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

    // Store pointer to errors catalog (for GC protection)
    //
    Init_Object(Get_System(SYS_CATALOG, CAT_ERRORS), errors_catalog);

    // Create SYSTEM.CODECS object
    //
    Init_Object(
        Get_System(SYS_CODECS, 0),
        Alloc_Varlist_Core(NODE_FLAG_MANAGED, TYPE_OBJECT, 10)
    );

    // The "standard error" template was created as an OBJECT!, because the
    // `make error!` functionality is not ready when %sysobj.r runs.  Fix
    // up its archetype so that it is an actual ERROR!.
    //
  blockscope {
    Value* std_error = Get_System(SYS_STANDARD, STD_ERROR);
    VarList* c = Cell_Varlist(std_error);
    HEART_BYTE(std_error) = TYPE_ERROR;

    Value* rootvar = Rootvar_Of_Varlist(c);
    assert(Get_Cell_Flag(rootvar, PROTECTED));
    HEART_BYTE(rootvar) = TYPE_ERROR;
  }
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
  #if ALLOW_SPORADICALLY_NON_DETERMINISTIC
    srand(time(nullptr));  // seed random number generator
  #endif

  //=//// INITIALIZE BASIC DIAGNOSTICS ////////////////////////////////////=//

    assert(PG_Boot_Phase == BOOT_START_0);

  #if defined(TEST_EARLY_BOOT_PANIC)
    panic ("early panic test"); // should crash
  #elif defined(TEST_EARLY_BOOT_FAIL)
    fail ("early fail test"); // same as panic (crash)
  #endif

  #if DEBUG_HAS_PROBE
    PG_Probe_Failures = false;
  #endif

    Check_Basics();

  //=//// INITIALIZE MEMORY AND ALLOCATORS ////////////////////////////////=//

    Startup_Signals();  // allocation can set signal flags for recycle etc.

    Startup_Pools(0);  // performs allocation, calls Set_Trampoline_Flag()
    Startup_GC();

    Startup_Raw_Print();
    Startup_Scanner();
    Startup_String();

    Init_Char_Cases();
    Startup_CRC();             // For word hashing
    Set_Random(0);

    Startup_Mold(MIN_COMMON / 4);

    Startup_Feeds();

    Startup_Collector();

    Startup_Data_Stack(STACK_MIN / 4);
    Startup_Trampoline();  // uses CANON() in File_Of_Level() currently

  //=//// INITIALIZE API //////////////////////////////////////////////////=//

    // The API contains functionality for memory allocation, decompression,
    // and other things needed to generate Lib.  So it has to be initialized
    // first...but you can't call any variadic APIs until Lib is available
    // to do binding.

    Startup_Api();

  //=//// STARTUP INTERNING AND BUILT-IN SYMBOLS //////////////////////////=//

    // The build process makes a list of Symbol ID numbers (SymId) which
    // are given fixed values.  e.g. SYM_LENGTH for the word `length` has an
    // integer enum value you can use in a C switch() statement.  Stubs for
    // these built-in symbols are constructed in a global array and stay
    // valid for the duration of the program.

    Startup_Interning();

    Startup_Builtin_Symbols(  // requires API for allocations in decompress
        Symbol_Strings_Compressed,
        Symbol_Strings_Compressed_Size
    );

  //=//// MAKE DATATYPES MODULE AND VARIABLES FOR BUILT-IN TYPES //////////=//

    // Builtin datatypes no longer live in LIB, but in SYS.CONTEXTS.DATATYPES
    // which is inherited by LIB.  This is also where extension datatypes are
    // put, so that the module Patch can serve as the canon ExtraHeart.

    Startup_Datatypes();

  //=//// MAKE LIB MODULE AND VARIABLES FOR BUILT-IN SYMBOLS //////////////=//

    // For many of the built-in symbols, we know there will be variables in
    // the Lib module for them.  e.g. since FOR-EACH is in the list of native
    // functions, we know Startup_Natives() will run (for-each: native [...])
    // during the boot.
    //
    // Since we know that, variables for the built-in symbols are constructed
    // in a global array.  This array is quickly indexable by the symbol ID,
    // so that core code can do lookups like Lib_Var(APPEND) to beeline to
    // the address of that library variable as a compile-time constant.
    //
    // After Startup_Lib(), all the builtin library variables will exist, but
    // they will be unset.  Startup_Natives() and Startup_Generics() can
    // take their existence for granted, without having to walk their init
    // code to collect the variables before running it.

    Startup_Lib();

  //=//// MAKE API HANDLES TO GC PROTECT LIB AND DATATYPES ///////////////=//

    ensure(nullptr, g_datatypes_module) = Alloc_Element();
    Init_Module(g_datatypes_module, g_datatypes_context);

    ensure(nullptr, g_lib_module) = Alloc_Element();
    Init_Module(g_lib_module, g_lib_context);

  //=//// INITIALIZE THE API BINDING FOR CORE /////////////////////////////=//

    // If you call a librebol API function from an arbitrary point in the
    // core, it will do its lookups in the lib context.
    //
    // (We have to cast it because API RebolContext* is a typedef of void*.)

    ensure(nullptr, librebol_binding) = cast(RebolContext*, g_lib_context);

  //=//// CREATE GLOBAL OBJECTS ///////////////////////////////////////////=//

    // The API is one means by which variables can be made whose lifetime is
    // indefinite until program shutdown.  In R3-Alpha this was done with
    // boot code that laid out some fixed structure arrays, but it's more
    // general to do it this way.

    Init_Root_Vars();  // States that can't (or aren't) held in Lib variables
    Init_Action_Spec_Tags();  // Note: requires mold buffer be initialized

  #if RUNTIME_CHECKS
    Assert_Pointer_Detection_Working();  // uses root Flex/Values to test
  #endif

  //=//// LOAD BOOT BLOCK /////////////////////////////////////////////////=//

    // The %make-boot.r process takes all the various definitions and
    // mezzanine code and packs it into one compressed string in
    // %tmp-boot-block.c which gets embedded into the executable.  This
    // includes the type list, word list, error message templates, system
    // object, mezzanines, etc.

    Size utf8_size;
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
    Push_Lifeguard(tmp_boot);  // recycle torture frees on scanner first push!
    Array* boot_array = Scan_UTF8_Managed(
        tmp_boot,
        utf8,
        utf8_size
    );
    Drop_Lifeguard(tmp_boot);
    Push_Lifeguard(boot_array); // managed, so must be guarded

    rebFree(utf8); // don't need decompressed text after it's scanned

    BOOT_BLK *boot = cast(BOOT_BLK*,
        Array_Head(Cell_Array_Known_Mutable(Array_Head(boot_array)))
    );

    Source* typespecs = Cell_Array_Known_Mutable(&boot->typespecs);
    assert(Array_Len(typespecs) == MAX_TYPE_BYTE);  // exclude TYPE_0 (custom)
    UNUSED(typespecs);  // not used at this time

    // Symbol_Id(), Cell_Word_Id() and CANON(XXX) now available

    PG_Boot_Phase = BOOT_LOADED;

  //=//// REGISTER BUILT-IN DISPATCHERS ///////////////////////////////////=//

    // We need to be able to navigate from dispatcher to querier.  It would
    // be too costly to store queriers in stubs, and we'd have to double
    // dereference the dispatcher to get one function to imply another
    // without a global sidestructure of some kind.

    Register_Dispatcher(&Func_Dispatcher, &Func_Details_Querier);
    Register_Dispatcher(&Adapter_Dispatcher, &Adapter_Details_Querier);
    Register_Dispatcher(&Encloser_Dispatcher, &Encloser_Details_Querier);
    Register_Dispatcher(&Lambda_Dispatcher, &Lambda_Details_Querier);
    Register_Dispatcher(&Arrow_Dispatcher, &Arrow_Details_Querier);
    Register_Dispatcher(&Cascader_Executor, &Cascader_Details_Querier);
    Register_Dispatcher(&Macro_Dispatcher, &Macro_Details_Querier);
    Register_Dispatcher(&Combinator_Dispatcher, &Combinator_Details_Querier);
    Register_Dispatcher(&Yielder_Dispatcher, &Yielder_Details_Querier);
    Register_Dispatcher(&Typechecker_Dispatcher, &Typechecker_Details_Querier);
    Register_Dispatcher(&Hijacker_Dispatcher, &Hijacker_Details_Querier);
    Register_Dispatcher(&Reframer_Dispatcher, &Reframer_Details_Querier);
    Register_Dispatcher(&Upshot_Dispatcher, &Oneshot_Details_Querier);
    Register_Dispatcher(&Reorderer_Dispatcher, &Reorderer_Details_Querier);
    Register_Dispatcher(&Downshot_Dispatcher, &Oneshot_Details_Querier);
    Register_Dispatcher(
        &Api_Function_Dispatcher,
        &Api_Function_Details_Querier
      );
    Register_Dispatcher(
        &Unimplemented_Dispatcher,
        &Unimplemented_Details_Querier
    );

  //=//// CREATE BASIC VALUES /////////////////////////////////////////////=//

    // Before any code can start running (even simple bootstrap code), some
    // basic words need to be defined.  For instance: You can't run %sysobj.r
    // unless `true` and `false` have been added to the g_lib_context--they'd be
    // undefined.  And while analyzing the function specs during the
    // definition of natives, things like the <opt-out> tag are needed as a
    // basis for comparison to see if a usage matches that.
    //
    // Startup_Type_Predicates() uses symbols, data stack, and adds words
    // to lib--not available until this point in time.
    //
    Startup_Type_Predicates();

  //=//// RUN CODE BEFORE ERROR HANDLING INITIALIZED //////////////////////=//

    // boot->natives is from the automatically gathered list of natives found
    // by scanning comments in the C sources for `native: ...` declarations.

    Startup_Action_Adjunct_Shim();  // make the shim for the action spec

    Startup_Natives(&boot->natives);

  //=//// STARTUP CONSTANTS (like NULL, BLANK, etc.) //////////////////////=//

    // These may be used in the system object definition.  At one time code
    // manually added definitions like NULL to LIB, but having it expressed
    // as simply (null: ~null~) in usermode code is clearer.
    //
    // Note that errors are not initialized yet (they are accessed through
    // the system object).  So this code should stay pretty simple.

    rebElide(
        "wrap*", g_lib_module, rebQ(&boot->constants),
        "evaluate inside", g_lib_module, rebQ(&boot->constants)
    );

    Protect_Cell(Mutable_Lib_Var(SYM_NULL));
    Protect_Cell(Mutable_Lib_Var(SYM_BLANK));
    Protect_Cell(Mutable_Lib_Var(SYM_QUASAR));
    Protect_Cell(Mutable_Lib_Var(SYM_NUL));

  //=//// STARTUP ERRORS AND SYSTEM OBJECT ////////////////////////////////=//

    // boot->errors is the error definition list from %errors.r
    //
    VarList* errors_catalog = Startup_Errors(&boot->errors);
    Push_Lifeguard(errors_catalog);

    Tweak_Cell_Binding(&boot->sysobj, g_lib_context);
    Init_System_Object(&boot->sysobj, errors_catalog);

    Drop_Lifeguard(errors_catalog);

    PG_Boot_Phase = BOOT_ERRORS;

  #if defined(TEST_MID_BOOT_PANIC)
    panic (g_empty_array); // panics should be able to give some details by now
  #elif defined(TEST_MID_BOOT_FAIL)
    fail ("mid boot fail"); // CHECKED->assert, RELEASE->panic
  #endif

    // Pre-make the stack overflow error (so it doesn't need to be made
    // during a stack overflow).  Error creation machinery depends heavily
    // on the system object being initialized, so this can't be done until
    // now.
    //
    Startup_Stackoverflow();

    // Pre-make UTF-8 decoding errors so that UTF-8 failures aren't slow
    //
    Startup_Utf8_Errors();

    Startup_Yielder_Errors();

    assert(TOP_INDEX == 0 and TOP_LEVEL == BOTTOM_LEVEL);

  //=//// INITIALIZE SYSTEM.CONTEXTS.LIB //////////////////////////////////=//

    // The basic model for bootstrap is that the "user context" is the
    // default area for new code evaluation.  It starts out as a copy of an
    // initial state set up in the lib context.  When native routines or other
    // content gets overwritten in the user context, it can be borrowed back
    // from `system.contexts.lib` (aliased as "lib" in the user context).
    //
    // Set up the alias for lib, but put a "tripwire" (antiform tag) in the
    // slot for the user context to give a more obvious error message if
    // something tries to use it before startup finishes.

  blockscope {
    Copy_Cell(Get_System(SYS_CONTEXTS, CTX_DATATYPES), g_datatypes_module);
    Copy_Cell(Get_System(SYS_CONTEXTS, CTX_LIB), g_lib_module);
    RebolValue* tripwire = rebValue(
        "~<SYS.CONTEXTS.USER not available: Mezzanine Startup not finished>~"
    );
    Copy_Cell(Get_System(SYS_CONTEXTS, CTX_USER), tripwire);
    rebRelease(tripwire);
  }

  //=//// RUN MEZZANINE CODE NOW THAT ERROR HANDLING IS INITIALIZED ///////=//

    // By this point, the g_lib_context contains basic definitions for things
    // like null, blank, the natives, and the generics.  `system` is set up.
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
        "wrap*", g_lib_module, rebQ(&boot->base),
        "evaluate inside", g_lib_module, rebQ(&boot->base)
        //
        // Note: ENSURE not available yet.
    );

  //=//// SYSTEM.UTIL STARTUP /////////////////////////////////////////////=//

    // The SYSTEM.UTIL context contains supporting Rebol code for implementing
    // "system" features.  It is lower-level than the LIB context, but has
    // natives, generics, and the definitions from Startup_Base() available.
    //
    // See the helper SYS_UTIL() for a quick way of getting the functions by
    // their symbol.
    //
    // (Note: The SYSTEM.UTIL context was renamed from just "SYS" to avoid
    //  being confused with "the system object", which is a different thing.
    //  Better was to say SYS was just an abbreviation for SYSTEM.)

    SeaOfVars* util = Alloc_Sea_Core(NODE_FLAG_MANAGED);
    Tweak_Link_Inherit_Bind(util, g_lib_context);
    ensure(nullptr, g_sys_util_module) = Alloc_Element();
    Init_Module(g_sys_util_module, util);
    ensure(nullptr, g_sys_util_context) = util;

    rebElide(
        //
        // The scan of the boot block interned everything to g_lib_context, but
        // we want to overwrite that with the g_sys_util_context here.
        //
        "sys.util:", g_sys_util_module,

        "wrap*", g_sys_util_module, rebQ(&boot->system_util),
        "if not equal? '~end~",
          "evaluate inside", g_sys_util_module, rebQ(&boot->system_util),
            "[fail -[sys.util]-]",

        // SYS contains the implementation of the module machinery itself, so
        // we don't have MODULE or EXPORT available.  Do the exports manually,
        // and then import the results to lib.
        //
        "set-adjunct sys.util make object! [",
            "name: 'System",  // this is MAKE OBJECT!, not MODULE, must quote
            "exports: [do module load decode encode encoding-of]",
        "]",
        "sys.util/import*", g_lib_module, g_sys_util_module
    );

    // !!! It was a stated goal at one point that it should be possible to
    // protect the entire system object and still run the interpreter.  That
    // was commented out in R3-Alpha
    //
    //    comment [if get $lib/secure [protect-system-object]]

  //=//// MEZZ STARTUP /////////////////////////////////////////////////////=//

    rebElide(
        // (It's not necessarily the greatest idea to have LIB be this
        // flexible.  But as it's not hardened from mutations altogether then
        // prohibiting it doesn't offer any real security...and only causes
        // headaches when trying to do something weird.)

        // Create actual variables for top-level SET-WORD!s only, and run.
        //
        "wrap*", g_lib_module, rebQ(&boot->mezz),
        "evaluate inside", g_lib_module, rebQ(&boot->mezz)
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
    // rebElide() here runs in the g_lib_context by default, which means the
    // block we are passing evaluatively as the module body will evaluate
    // and carry the lib context.  This achieves the desired inheritance,
    // because when we say EVAL INSIDE SYSTEM.CONTEXTS.USER CODE we want the
    // code to find definitions in user as well as in lib.
    //
    rebElide(
        "system.contexts.user: module [Name: User] []"
    );

    ensure(nullptr, g_user_module) = cast(Element*, Copy_Cell(
        Alloc_Value(),
        Get_System(SYS_CONTEXTS, CTX_USER)
    ));
    rebUnmanage(g_user_module);
    g_user_context = Cell_Module_Sea(g_user_module);

  //=//// FINISH UP ///////////////////////////////////////////////////////=//

    assert(TOP_INDEX == 0 and TOP_LEVEL == BOTTOM_LEVEL);

    Drop_Lifeguard(boot_array);

    PG_Boot_Phase = BOOT_DONE;

  #if RUNTIME_CHECKS
    Check_Memory_Debug();  // old R3-Alpha check, call here to keep it working
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
// While some leaks are detected by RUNTIME_CHECKS during shutdown, even more
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

  #if RUNTIME_CHECKS
    Check_Memory_Debug();  // old R3-Alpha check, call here to keep it working
  #endif

    if (not clean)
        return;

    PG_Boot_Phase = BOOT_START_0;

    Shutdown_Data_Stack();

    Shutdown_Yielder_Errors();
    Shutdown_Utf8_Errors();
    Shutdown_Stackoverflow();
    Shutdown_Typesets();

    Shutdown_Natives();

    Shutdown_Action_Adjunct_Shim();

    rebReleaseAndNull(&g_sys_util_module);
    g_sys_util_context = nullptr;

    rebReleaseAndNull(&g_user_module);
    g_user_context = nullptr;

    Shutdown_Action_Spec_Tags();
    Shutdown_Root_Vars();

  //=//// SHUTDOWN THE API BINDING FOR CORE //////////////////////////////=//

    assert(librebol_binding == g_lib_context);
    librebol_binding = nullptr;

  //=//// FREE API HANDLES PROTECTING DATATYPES AND LIB, SWEEP STUBS //////=//

    rebReleaseAndNull(&g_lib_module);
    dont(g_lib_context = nullptr);  // do at end of Shutdown_Lib()

    rebReleaseAndNull(&g_datatypes_module);
    dont(g_datatypes_context = nullptr);  // do at end of Shutdown_Datatypes()

    Sweep_Stubs();  // free all managed Stubs, no more GC [1]

  //=//// SHUTDOWN LIB AND DATATYPES //////////////////////////////////////=//

    // The lib module and datatypes module both have premade stubs that are
    // not subject to garbage collection.  This means that after all the
    // managed Stubs are released, Shutdown_Datatypes() and Shutdown_Lib()
    // have to manually free the premade stubs.

    Shutdown_Lib();
    Shutdown_Datatypes();

  //=//////////////////////////////////////////////////////////////////////=//

    Shutdown_Builtin_Symbols();
    Shutdown_Interning();

    Shutdown_Api();

    Shutdown_Feeds();

    Shutdown_Trampoline();  // all API calls (e.g. rebRelease()) before this

//=//// ALL MANAGED STUBS MUST HAVE THE KEEPALIVE REFERENCES GONE NOW /////=//

    assert(Is_Cell_Erased(&g_ts.thrown_arg));
    assert(Is_Cell_Erased(&g_ts.thrown_label));
    assert(g_ts.unwind_level == nullptr);

    Shutdown_Mold();
    Shutdown_Collector();
    Shutdown_Raw_Print();
    Shutdown_CRC();
    Shutdown_String();
    Shutdown_Scanner();

    Shutdown_Char_Cases();  // case needed for hashes in Shutdown_Symbols()

    Shutdown_GC();

    // Shutting down the memory manager must be done after all the Free_Mem
    // calls have been made to balance their Alloc_Mem calls.
    //
    Shutdown_Pools();
}
