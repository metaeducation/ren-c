//
//  file: %b-init.c
//  summary: "initialization functions"
//  section: bootstrap
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2025 Ren-C Open Source Contributors
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
// Initially these checks were #if RUNTIME_CHECKS only.  However, they are
// so foundational that it's probably worth getting a coherent crash in any
// build where these tests don't work.
//
static void Check_Basics(void)
{
  check_cell_size: {  // define UNUSUAL_CELL_SIZE to bypass this check!

  // The system is designed with the intent that a cell is 4x(32-bit) on
  // 32-bit platforms and 4x(64-bit) on 64-bit platforms.  It's a critical
  // performance point.  For the moment we consider it to be essential enough
  // that the system that it refuses to run if not true.
  //
  // But if someone is in an odd situation with a larger sized cell--and it's
  // an even multiple of ALIGN_SIZE--it may still work.  For instance: the
  // DEBUG_TRACK_EXTEND_CELLS mode doubles the cell size to carry the file,
  // line, and tick of their initialization (or last Touch_Cell()).

    Size cell_size = sizeof(Cell);  // in variable avoids warning

  #if UNUSUAL_CELL_SIZE  // e.g. if DEBUG_TRACK_EXTEND_CELLS
    if (cell_size % ALIGN_SIZE != 0)
        crash ("size of cell does not evenly divide by ALIGN_SIZE");
  #else
    if (cell_size != sizeof(void*) * 4)
        crash ("size of cell is not sizeof(void*) * 4");

    Size stub_size = sizeof(Cell) * 2;

    #if DEBUG_STUB_ORIGINS
      stub_size += sizeof(void*) * 2;
    #endif

    assert(sizeof(Stub) == stub_size);
    UNUSED(stub_size);
  #endif

} check_stub_info_placement: {  // (non-essential)

  // Stub historically placed the `info` bits exactly after `content` so
  // they could do double-duty as an array terminator when the content was a
  // singular Cell and enumerated as an Array.  But arrays are now
  // enumerated according to their stored length, and only have termination
  // if DEBUG_POISON_FLEX_TAILS.  But the phenomenon still has some leverage
  // by ensuring the BASE_FLAG_CELL bit is clear in the info field--which
  // helps catch a few stray reads or writes.

    Size offset = offsetof(Stub, info);  // variable avoids warning
    if (offset - offsetof(Stub, content) != sizeof(Cell))
        crash ("bad structure alignment for internal array termination");

} check_byte_ordering_sensitive_flags: {

  // See the %sys-base.h file for an explanation of what these are, and why
  // having them work is fundamental to the system.

    Flags flags
        = FLAG_LEFT_BIT(5) | FLAG_SECOND_BYTE(21) | FLAG_SECOND_UINT16(1975);

    Byte m = FIRST_BYTE(&flags);  // 6th bit from left set (0b00000100 is 4)
    Byte d = SECOND_BYTE(&flags);
    uint16_t y = SECOND_UINT16(&flags);
    if (m != 4 or d != 21 or y != 1975) {
      #if RUNTIME_CHECKS
        printf("m = %u, d = %u, y = %u\n", m, d, y);
      #endif
        crash ("Bad composed integer assignment for byte-ordering macro.");
    }
}}


//
//  Startup_Lib: C
//
// Since no good literal form exists, the %sysobj.r file uses the words.  They
// have to be defined before the point that it runs (along with the natives).
//
static void Startup_Lib(void)
{
    SeaOfVars* lib = Alloc_Sea_Core(BASE_FLAG_MANAGED);
    assert(Link_Inherit_Bind(lib) == nullptr);
    Tweak_Link_Inherit_Bind(lib, g_datatypes_context);

    assert(Is_Stub_Erased(&g_lib_patches[opt SYM_0]));  // leave invalid

    for (SymId16 id = 1; id <= MAX_SYM_LIB_PREMADE; ++id) {
        Patch* patch = &g_lib_patches[id];
        assert(Is_Stub_Erased(patch));  // pre-boot state

        patch->header.bits = STUB_MASK_PATCH;

        assert(INFO_PATCH_SEA(patch) == nullptr);
        assert(LINK_PATCH_RESERVED(patch) == nullptr);
        Tweak_Info_Patch_Sea(patch, lib);

        Symbol* symbol =  &g_symbols.builtin_canons[id];
        assert(Misc_Hitch(symbol) == symbol);  // no module patches yet
        Tweak_Misc_Hitch(symbol, patch);  // ...but now it has one!
        Tweak_Misc_Hitch(patch, symbol);  // link back for singly-linked-list

        Init_Ghost_For_Unset(Stub_Cell(patch));
    }

    known_nullptr(g_lib_context) = lib;
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
    assert(Is_Stub_Erased(&g_lib_patches[cast(int, SYM_0)]));

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
    KIND_BYTE(t) = TYPE_TAG;

    Force_Value_Frozen_Deep(t);
    return t;
}

//
//  Init_Action_Spec_Tags: C
//
// FUNC and PROC search for these tags, like ~null~ and <local>.  They are
// natives and run during bootstrap, so these string comparisons are
// needed.  (We can't just compare against UTF-8 strings like CANON(WITH)
// because at present, tags are series and have positions, and we need to
// weigh the position of the tag we're comparing to.)
//
// !!! These should be created by Rebol, specified in the %specs/ directory,
// along with most of the other random literals boot is creating right now.
//
static void Init_Action_Spec_Tags(void)
{
    known_nullptr(g_tag_variadic) = Make_Locked_Tag("variadic");
    known_nullptr(g_tag_end) = Make_Locked_Tag("end");
    known_nullptr(g_tag_opt_out) = Make_Locked_Tag("opt-out");
    known_nullptr(g_tag_opt) = Make_Locked_Tag("opt");
    known_nullptr(g_tag_const) = Make_Locked_Tag("const");
    known_nullptr(g_tag_divergent) = Make_Locked_Tag("divergent");
    known_nullptr(g_tag_unrun) = Make_Locked_Tag("unrun");
    known_nullptr(g_tag_null) = Make_Locked_Tag("null");
    known_nullptr(g_tag_void) = Make_Locked_Tag("void");
    known_nullptr(g_tag_dot_1) = Make_Locked_Tag(".");

    known_nullptr(g_tag_here) = Make_Locked_Tag("here");  // used by PARSE

  initialize_auto_trash_param: {

  // This is a bit of a tricky bootstrap issue because Set_Parameter_Spec()
  // that does spec analysis to fill in a PARAMETER! dpends on Get_Word(),
  // which in turn depends on the TWEAK mechanics, and that hasn't been
  // initialized yet.
  //
  // Build the [trash!] parameter spec array manually--zeroing out the
  // optimization bytes, and indicating that it checks for trash and doesn't
  // need to walk the array to look up types when checking.

    Source* a = Alloc_Singular(STUB_MASK_MANAGED_SOURCE);
    Element* w = Init_Word(Stub_Cell(a), CANON(TRASH_X));

    DECLARE_ELEMENT (spec);
    Init_Block(spec, a);

    Element* param = Init_Unconstrained_Parameter(
        Alloc_Value(),
        FLAG_PARAMCLASS_BYTE(PARAMCLASS_NORMAL)
    );
    CELL_PARAMETER_PAYLOAD_1_SPEC(param) = a;  // should GC protect array
    Clear_Cell_Flag(param, DONT_MARK_PAYLOAD_1);  // sync flag

    TypesetByte* optimized = a->misc.at_least_4;
    TypesetByte* optimized_tail = optimized + sizeof(uintptr_t);
    UNUSED(optimized_tail);
    Mem_Fill(optimized, 0, sizeof(uintptr_t));

    Set_Parameter_Flag(param, TRASH_DEFINITELY_OK);
    Set_Cell_Flag(w, PARAMSPEC_SPOKEN_FOR);

    Set_Parameter_Flag(param, AUTO_TRASH);

    Freeze_Source_Shallow(a);

    known_nullptr(g_auto_trash_param) = param;
}}

static void Shutdown_Action_Spec_Tags(void)
{
    rebReleaseAndNull(&g_auto_trash_param);

    rebReleaseAndNull(&g_tag_variadic);
    rebReleaseAndNull(&g_tag_end);
    rebReleaseAndNull(&g_tag_opt_out);
    rebReleaseAndNull(&g_tag_opt);
    rebReleaseAndNull(&g_tag_const);
    rebReleaseAndNull(&g_tag_divergent);
    rebReleaseAndNull(&g_tag_unrun);
    rebReleaseAndNull(&g_tag_null);
    rebReleaseAndNull(&g_tag_void);
    rebReleaseAndNull(&g_tag_dot_1);

    rebReleaseAndNull(&g_tag_here);  // used by PARSE
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
  make_bounce_signals: {

    Init_Bounce_Wild(g_bounce_thrown, C_THROWN);
    Init_Bounce_Wild(g_bounce_redo_unchecked, C_REDO_UNCHECKED);
    Init_Bounce_Wild(g_bounce_redo_checked, C_REDO_CHECKED);
    Init_Bounce_Wild(g_bounce_downshifted, C_DOWNSHIFTED);
    Init_Bounce_Wild(g_bounce_continuation, C_CONTINUATION);
    Init_Bounce_Wild(g_bounce_delegation, C_DELEGATION);
    Init_Bounce_Wild(g_bounce_suspend, C_SUSPEND);

} make_empty_block: {

    g_empty_array = Make_Source_Managed(0);
    Freeze_Source_Deep(g_empty_array);

    known_nullptr(g_empty_block) = Init_Block(
        Alloc_Value(),
        g_empty_array  // holds empty array alive
    );
    Force_Value_Frozen_Deep(g_empty_block);

} make_empty_object: {

    Length len = 0;
    Array* a = Make_Array_Core(
        STUB_MASK_VARLIST
            | BASE_FLAG_MANAGED, // Note: Rebind below requires managed context
        1 + len  // needs room for rootvar
    );
    Set_Flex_Len(a, 1 + len);
    Tweak_Misc_Varlist_Adjunct_Raw(a, nullptr);
    Tweak_Link_Inherit_Bind_Raw(a, nullptr);

    require (
      KeyList* keylist = u_downcast Make_Flex(
        STUB_MASK_KEYLIST | BASE_FLAG_MANAGED,
        len  // no terminator, 0-based
    ));

    Set_Flex_Used(keylist, len);

    Tweak_Bonus_Keylist_Unique(a, keylist);
    Tweak_Link_Keylist_Ancestor(keylist, keylist);  // terminate in self

    Tweak_Non_Frame_Varlist_Rootvar(a, TYPE_OBJECT);

    g_empty_varlist = cast(VarList*, a);

    known_nullptr(g_empty_object) = Init_Object(
        Alloc_Value(),
        g_empty_varlist  // holds empty varlist alive
    );
    Force_Value_Frozen_Deep(g_empty_object);

} make_heavy_null: {

  // keep array alive via stable API handle (META PACK, not PACK)

    Source* a = Alloc_Singular(STUB_MASK_MANAGED_SOURCE);
    Init_Quasi_Null(Stub_Cell(a));
    Freeze_Source_Deep(a);
    known_nullptr(g_1_quasi_null_array) = a;
    known_nullptr(g_lifted_heavy_null) = Init_Lifted_Pack(Alloc_Value(), a);
    Force_Value_Frozen_Deep(g_lifted_heavy_null);

} make_other_things: {

    known_nullptr(Root_Feed_Null_Substitute) = Init_Quasi_Null(Alloc_Value());
    Set_Cell_Flag(Root_Feed_Null_Substitute, FEED_HINT_ANTIFORM);
    Protect_Cell(Root_Feed_Null_Substitute);

    require (
      Strand* nulled_uni = Make_Strand(1)  // rebText() can't run yet, review
    );

  #if RUNTIME_CHECKS
    Codepoint test_nul;
    Utf8_Next(&test_nul, Strand_At(nulled_uni, 0));
    assert(test_nul == '\0');
    assert(Strand_Len(nulled_uni) == 0);
  #endif

    known_nullptr(g_empty_text) = Init_Text(Alloc_Value(), nulled_uni);
    Force_Value_Frozen_Deep(g_empty_text);

    Binary* bzero = Make_Binary(0);
    known_nullptr(g_empty_blob) = Init_Blob(Alloc_Value(), bzero);
    Force_Value_Frozen_Deep(g_empty_blob);

    known_nullptr(g_quasi_null) = Init_Quasi_Null(Alloc_Value());
    Protect_Cell(g_quasi_null);

    known_nullptr(g_tripwire) = Init_Tripwire(Alloc_Value());
    Protect_Cell(g_tripwire);

    require (
      known_nullptr(g_dispatcher_table) = Make_Flex(
        FLAG_FLAVOR(FLAVOR_DISPATCHERTABLE) | STUB_FLAG_DYNAMIC,
        15
    ));
}}

static void Shutdown_Root_Vars(void)
{
    Free_Unmanaged_Flex(g_dispatcher_table);
    g_dispatcher_table = nullptr;

    Erase_Bounce_Wild(g_bounce_thrown);
    Erase_Bounce_Wild(g_bounce_redo_unchecked);
    Erase_Bounce_Wild(g_bounce_redo_checked);
    Erase_Bounce_Wild(g_bounce_downshifted);
    Erase_Bounce_Wild(g_bounce_continuation);
    Erase_Bounce_Wild(g_bounce_delegation);
    Erase_Bounce_Wild(g_bounce_suspend);

    rebReleaseAndNull(&g_empty_text);
    rebReleaseAndNull(&g_empty_block);
    g_empty_array = nullptr;
    rebReleaseAndNull(&g_empty_object);
    g_empty_varlist = nullptr;
    rebReleaseAndNull(&g_lifted_heavy_null);
    g_1_quasi_null_array = nullptr;
    rebReleaseAndNull(&Root_Feed_Null_Substitute);
    rebReleaseAndNull(&g_empty_blob);
    rebReleaseAndNull(&g_quasi_null);
    rebReleaseAndNull(&g_tripwire);
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
    assert(Series_Index(boot_sysobj_spec) == 0);
    const Element* spec_tail;
    Element* spec_head = List_At_Known_Mutable(&spec_tail, boot_sysobj_spec);

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
    Init_Object(Sink_LIB(SYSTEM), system);
    Init_Object(Sink_LIB(SYS), system);

    require (
      Use* use = Alloc_Use_Inherits(List_Binding(boot_sysobj_spec))
    );
    Copy_Cell(Stub_Cell(use), Varlist_Archetype(system));

    DECLARE_ELEMENT (sysobj_spec_virtual);
    Copy_Cell(sysobj_spec_virtual, boot_sysobj_spec);
    Tweak_Cell_Binding(sysobj_spec_virtual, use);

    // Evaluate the block (will eval CONTEXTs within).
    //
    DECLARE_VALUE (result);
    if (Eval_Any_List_At_Throws(result, sysobj_spec_virtual, SPECIFIED))
        crash (result);

    require (
      Stable* result_value = Decay_If_Unstable(result)
    );
    if (not Is_Quasi_Word_With_Id(result_value, SYM_END))
        crash (result_value);

    // Store pointer to errors catalog (for GC protection)
    //
    Init_Object(
        Slot_Init_Hack(Get_System(SYS_CATALOG, CAT_ERRORS)),
        errors_catalog
    );

    // Create SYSTEM.CODECS object
    //
    Init_Object(
        Slot_Init_Hack(Get_System(SYS_CODECS, 0)),
        Alloc_Varlist_Core(BASE_FLAG_MANAGED, TYPE_OBJECT, 10)
    );

  fix_standard_error: {

  // The "standard error" template was created as an OBJECT!, because the
  // `make warning!` functionality is not ready when %sysobj.r runs.  Fix
  // up its archetype so that it is an actual ERROR!.

    Slot* std_error_slot = Get_System(SYS_STANDARD, STD_ERROR);
    assert(KIND_BYTE(std_error_slot) == TYPE_OBJECT);
    assert(LIFT_BYTE_RAW(std_error_slot) == NOQUOTE_2);
    VarList* varlist = Cell_Varlist(u_cast(Element*, std_error_slot));
    KIND_BYTE(std_error_slot) = TYPE_WARNING;

    Stable* rootvar = Rootvar_Of_Varlist(varlist);
    assert(Get_Cell_Flag(rootvar, PROTECTED));
    KIND_BYTE(rootvar) = TYPE_WARNING;
}}


//
//  Startup_Core: C
//
// Initialize the interpreter core.
//
// !!! This will either succeed or crash(), triggering an exit to the OS.
// The code is not currently written to be able to cleanly shut down from a
// partial initialization.  (It should be.)
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
    assert(PG_Boot_Phase == BOOT_START_0);

  seed_random_number_generator: {

  #if ALLOW_SPORADICALLY_NON_DETERMINISTIC
    srand(time(nullptr));
  #endif

} perform_early_diagnostics: {

  #if defined(TEST_EARLY_BOOT_CRASH)
    crash ("early crash test");  // should crash
  #elif defined(TEST_EARLY_BOOT_PANIC)
    panic ("early panic test");  // same as crash (crash)
  #endif

  #if DEBUG_HAS_PROBE
    g_probe_panics = false;
  #endif

    Check_Basics();

} startup_memory_and_allocators: {

    Startup_Signals();  // allocation can set signal flags for recycle etc.

    Startup_Pools(0);  // performs allocation, calls Set_Trampoline_Flag()
    Startup_GC();

    Startup_Raw_Print();
    Startup_Scanner();
    Startup_String();

    Init_Char_Cases();
    Startup_CRC();  // For word hashing
    Set_Random(0);

    Startup_Mold(MIN_COMMON / 4);

    Startup_Feeds();

    Startup_Collector();

    Startup_Data_Stack(STACK_MIN / 4);
    Startup_Trampoline();  // uses CANON() in File_Of_Level() currently

} startup_api: {

  // The API contains functionality for memory allocation, decompression, and
  // other things needed to generate LIB.  So it has to be initialized first,
  // but you can't call any variadic APIs until LIB is available for binding.

    Startup_Api();

} startup_interning_and_builtin_symbols: {

  // The build process makes a list of Symbol ID numbers (SymId) which are
  // given fixed values.  e.g. SYM_LENGTH for the word `length` has an integer
  // enum value you can use in a C switch() statement.  Stubs for these
  // built-in symbols are constructed in a global array and stay valid for
  // the duration of the program.

    Startup_Interning();

    Startup_Builtin_Symbols(  // requires API for allocations in decompress
        g_symbol_names_compressed,
        g_symbol_names_compressed_size
    );

} startup_datatypes: {

  // Builtin datatypes no longer live in LIB, but in SYS.CONTEXTS.DATATYPES
  // which is inherited by LIB.  This is also where extension datatypes are
  // put, so that the module Patch can serve as the canon ExtraHeart.

    Startup_Datatypes();

    known_nullptr(g_datatypes_module) = Alloc_Element();
    Init_Module(g_datatypes_module, g_datatypes_context);  // GC protect

} startup_lib: {

  // For many of the built-in symbols, we know there will be variables in
  // the Lib module for them.  e.g. since FOR-EACH is in the list of native
  // functions, we know Startup_Natives() will run (for-each: native [...])
  // during the boot.
  //
  // Since we know that, variables for the built-in symbols are constructed
  // in a global array.  This array is quickly indexable by the symbol ID,
  // so that core code can do lookups like Lib_Var(APPEND) to beeline to the
  // address of that library variable as a compile-time constant.
  //
  // After Startup_Lib(), all the builtin library variables will exist, but
  // they will be unset.  Startup_Natives() and Startup_Generics() can take
  // their existence for granted, without having to walk their init code to
  // collect the variables before running it.

    Startup_Lib();

    known_nullptr(g_lib_module) = Alloc_Element();
    Init_Module(g_lib_module, g_lib_context);  // GC protect

} initialize_core_api_binding: {

  // If you call a librebol API function from an arbitrary point in the core,
  // it will do its lookups in the lib context.
  //
  // (We have to cast it because API RebolContext* is a typedef of void*.)

    known_nullptr(librebol_binding) = cast(RebolContext*, g_lib_context);

} create_global_objects: {

  // The API is one means by which variables can be made whose lifetime is
  // indefinite until program shutdown.  In R3-Alpha this was done with boot
  // code that laid out some fixed structure arrays, but it's more general to
  // do it this way.

    Init_Root_Vars();  // States that can't (or aren't) held in Lib variables
    Init_Action_Spec_Tags();  // Note: requires mold buffer be initialized

  #if RUNTIME_CHECKS
    Assert_Pointer_Detection_Working();  // uses root Flex/Values to test
  #endif

} load_boot_block: {

  // 1. %make-boot.r takes all the various definitions and mezzanine code and
  //    packs it into one compressed string in %tmp-boot-block.c which gets
  //    embedded into the executable.  This includes the type list, word list,
  //    error message templates, system object, mezzanines, etc.

    Size utf8_size;
    const int max = -1;  // trust size in gzip data
    Byte* utf8 = Decompress_Alloc_Core(
        &utf8_size,
        g_boot_block_compressed,  // from %tmp-boot-block.c [1]
        g_boot_block_compressed_size,
        max,
        SYM_GZIP
    );

    assume (  // !!! can't put dots in Symbol*, should be using Strand here
        const Symbol* tmp_boot = Intern_Unsized_Managed("tmp-boot-r")
    );
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

    // Symbol_Id(), Word_Id() and CANON(XXX) now available

    PG_Boot_Phase = BOOT_LOADED;

 register_builtin_dispatchers: {

  // We need to be able to navigate from dispatcher to querier.  It would be
  // too costly to store queriers in stubs, and we'd double dereference the
  // dispatcher to get one function to imply another without a global
  // sidestructure of some kind.

    Register_Dispatcher(&Func_Dispatcher, &Func_Details_Querier);
    Register_Dispatcher(&Adapter_Dispatcher, &Adapter_Details_Querier);
    Register_Dispatcher(&Encloser_Dispatcher, &Encloser_Details_Querier);
    Register_Dispatcher(&Lambda_Dispatcher, &Lambda_Details_Querier);
    Register_Dispatcher(&Arrow_Dispatcher, &Arrow_Details_Querier);
    Register_Dispatcher(&Cascader_Executor, &Cascader_Details_Querier);
    Register_Dispatcher(&Inliner_Dispatcher, &Inliner_Details_Querier);
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

} startup_type_predicates: {

  // Startup_Type_Predicates() uses symbols, data stack, adds words to lib.
  // Not possible until this point in time.

    Startup_Type_Predicates();

} startup_natives: {

  // boot->natives is from the automatically gathered list of natives found
  // by scanning comments in the C sources for `native: ...` declarations.

    Startup_Natives(&boot->natives);

    Protect_LIB(UNIMPLEMENTED);  // can't hijack it

} startup_evaluator: {

    Startup_Evaluator();

} startup_constants: {  // like NULL, SPACE, etc.

  // Before any code can start running (even simple bootstrap code), some
  // basic words need to be defined.  For instance: You can't run %sysobj.r
  // unless `null` and `okay` have been added to the g_lib_context--they'd be
  // undefined.  And while analyzing the function specs during the definition
  // of natives, things like the <opt-out> tag are needed as a basis for
  // comparison to see if a usage matches that.
  //
  // These may be used in the system object definition.  At one time code
  // manually added definitions like NULL to LIB, but having it expressed as
  // simply (null: ~null~) in usermode code is clearer.
  //
  // Note that errors are not initialized yet (they are accessed through the
  // system object).  So this code should stay pretty simple.

    rebElide(
        "wrap*", g_lib_module, rebQ(&boot->constants),
        "evaluate inside", g_lib_module, rebQ(&boot->constants)
    );

    Protect_LIB(NULL);
    Protect_LIB(SPACE);
    Protect_LIB(QUASAR);
    Protect_LIB(NUL);

} startup_errors: {

  // 1. boot->errors is the error definition list from %errors.r
  //
  // 2. Pre-make the stack overflow error (so it doesn't need to be made
  //    during a stack overflow).  Error creation machinery depends heavily
  //    on the system object, so this can't be done until now.

    VarList* errors_catalog = Startup_Errors(&boot->errors);  // %errors.r [1]
    Push_Lifeguard(errors_catalog);

    Tweak_Cell_Binding(&boot->sysobj, g_lib_context);
    Init_System_Object(&boot->sysobj, errors_catalog);

    Drop_Lifeguard(errors_catalog);

    PG_Boot_Phase = BOOT_ERRORS;

  #if defined(TEST_MID_BOOT_PANIC)
    crash (g_empty_array);  // crashes should be able to give details by now
  #elif defined(TEST_MID_BOOT_PANIC)
    panic ("mid boot panic");  // if RUNTIME_CHECKS assert, else crash
  #endif

    Startup_Stackoverflow();  // can't create *during* a stack overflow [2]

    Startup_Utf8_Errors();  // pre-make so UTF-8 failures aren't slow

    Startup_Yielder_Errors();
    Startup_Reduce_Errors();

    assert(TOP_INDEX == 0 and TOP_LEVEL == BOTTOM_LEVEL);

} initialize_lib: {  // SYSTEM.CONTEXTS.LIB

  // The basic model for bootstrap is that the "user context" is the default
  // area for new code evaluation.  It starts out as a copy of an initial
  // state set up in the lib context.  When native routines or other content
  // gets overwritten in the user context, it can be borrowed back from
  // `system.contexts.lib` (aliased as "lib" in the user context).

    Copy_Cell(
        Slot_Init_Hack(Get_System(SYS_CONTEXTS, CTX_DATATYPES)),
        g_datatypes_module
    );
    Copy_Cell(
        Slot_Init_Hack(Get_System(SYS_CONTEXTS, CTX_LIB)),
        g_lib_module
    );
    Api(Stable*) trash = rebStable(
        "~#[SYS.CONTEXTS.USER unavailable: Mezzanine Startup not finished]#~"
    );
    Copy_Cell(
        Slot_Init_Hack(Get_System(SYS_CONTEXTS, CTX_USER)),
        trash
    );
    rebRelease(trash);

} update_boot_phase: {  // Note: error handling initialized

  // By this point, the g_lib_context contains basic definitions for things
  // like null, space, the natives, and the generics.  `system` is set up.
  //
  // There is theoretically some level of error recovery that could be done
  // here.  e.g. the evaluator works, it just doesn't have many functions you
  // would expect.  How bad it is depends on whether base and sys ran, so
  // perhaps only errors running "mezz" should be tolerated.  But the
  // console may-or-may-not run.
  //
  // For now, assume any panic in code running doing boot is fatal.
  //
  // (Handling of Ctrl-C is an issue...if halt cannot be handled cleanly, it
  // should be set up so that the user isn't even *able* to request a halt at
  // this boot phase.)

    PG_Boot_Phase = BOOT_MEZZ;

} startup_base: {

  // The code in "base" is the lowest level of initialization written as
  // Rebol code.  This is where things like `+` being an infix form of ADD is
  // set up, or FIRST being a specialization of PICK.  It also has wrappers
  // for more basic natives that handle aspects that are easier to write in
  // usermode than in C.
  //
  // 1. Create actual variables for top-level SET-WORD!s only.

    rebElide(
        "wrap*", g_lib_module, rebQ(&boot->base),  // top-level variables [1]
        "evaluate inside", g_lib_module, rebQ(&boot->base)  // no ENSURE yet
    );

} startup_sys_util: {

  // The SYSTEM.UTIL context contains supporting Rebol code for implementing
  // "system" features.  It is lower-level than the LIB context, but has
  // natives, generics, and the definitions from Startup_Base() available.
  //
  // (Note: The SYSTEM.UTIL context was renamed from just "SYS" to avoid
  //  being confused with "the system object", which is a different thing.
  //  Better was to say SYS was just an abbreviation for SYSTEM.)
  //
  // 1. The scan of the boot block interned everything to g_lib_context, but
  //    we want to overwrite that with the g_sys_util_context here.
  //
  // 2. SYS contains the implementation of the module machinery itself, so
  //    we don't have MODULE or EXPORT available.  Do the exports manually,
  //    and then import the results to lib.

    SeaOfVars* util = Alloc_Sea_Core(BASE_FLAG_MANAGED);
    Tweak_Link_Inherit_Bind(util, g_lib_context);
    known_nullptr(g_sys_util_module) = Alloc_Element();
    Init_Module(g_sys_util_module, util);
    known_nullptr(g_sys_util_context) = util;

    rebElide(
        "sys.util:", g_sys_util_module,  // overwrite [1]

        "wrap*", g_sys_util_module, rebQ(&boot->system_util),
        "if not equal? '~end~",
          "evaluate inside", g_sys_util_module, rebQ(&boot->system_util),
            "[panic ~#[sys.util]#~]",

        "set-adjunct sys.util make object! [",  // no MODULE/EXPORT yet [2]
            "name: 'System",  // this is MAKE OBJECT!, not MODULE, must quote
            "exports: [do module load decode encode encoding-of]",
        "]",
        "sys.util/import*", g_lib_module, g_sys_util_module
    );

} protect_system_object: {

  // !!! It was a stated goal at one point that it should be possible to
  // protect the entire system object and still run the interpreter.  That
  // was commented out in R3-Alpha
  //
  //    comment [if get $lib/secure [protect-system-object]]

} startup_mezzanine: {

  // (It's not necessarily the greatest idea to have LIB be this flexible.
  // But as it's not hardened from mutations altogether then prohibiting it
  // doesn't offer any real security...and only causes headaches when trying
  // to do something weird.)
  //
  // 1. Create actual variables for top-level SET-WORD!s only.

    Startup_Parse3();

    rebElide(
        "wrap*", g_lib_module, rebQ(&boot->mezz),  // top-level variables [1]
        "evaluate inside", g_lib_module, rebQ(&boot->mezz)
    );

} make_user_context: {

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

    rebElide(
        "system.contexts.user: module [Name: User] []"
    );

    Sink(Stable) user = Alloc_Value();
    assume (
      Read_Slot(user, Get_System(SYS_CONTEXTS, CTX_USER))
    );

    g_user_module = Known_Element(user);
    rebUnmanage(g_user_module);

    g_user_context = Cell_Module_Sea(g_user_module);

} startup_extension_loader: {

  // We don't actually load any extensions during the core startup.  The
  // builtin extensions can be selectively loaded in whatever order the API
  // client wants (they may not want to load all extensions that are built in
  // that were available all the time).

    Startup_Extension_Loader();

} finished_startup: {

    assert(TOP_INDEX == 0 and TOP_LEVEL == BOTTOM_LEVEL);

    Drop_Lifeguard(boot_array);

    PG_Boot_Phase = BOOT_DONE;

  #if RUNTIME_CHECKS
    Check_Memory_Debug();  // old R3-Alpha check, call here to keep it working
  #endif

    Recycle(); // necessary?
}}}


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

  shutdown_extensions: {

  // Shutting down extensions is currently considered semantically mandatory,
  // as it may flush writes to files (filesystem extension) or do other work.
  // If you really want to do a true "unclean shutdown" you can call exit().

    Shutdown_Extension_Loader();

} shutdown_more: {

    Run_All_Handle_Cleaners();  // there may be rebFree() and other API code

  #if RUNTIME_CHECKS
    Check_Memory_Debug();  // old R3-Alpha check, call here to keep it working
  #endif

    if (not clean)
        return;

    PG_Boot_Phase = BOOT_START_0;

    Shutdown_Parse3();

    Shutdown_Data_Stack();

    Shutdown_Reduce_Errors();
    Shutdown_Yielder_Errors();
    Shutdown_Utf8_Errors();
    Shutdown_Stackoverflow();
    Shutdown_Typesets();

    Shutdown_Natives();

    rebReleaseAndNull(&g_sys_util_module);
    g_sys_util_context = nullptr;

    rebReleaseAndNull(&g_user_module);
    g_user_context = nullptr;

    Shutdown_Action_Spec_Tags();
    Shutdown_Root_Vars();

} shutdown_core_api_binding: {

    assert(cast(Context*, librebol_binding) == g_lib_context);
    librebol_binding = nullptr;

} free_api_handles_protecting_lib_and_datatypes: {

    rebReleaseAndNull(&g_lib_module);
    dont(g_lib_context = nullptr);  // do at end of Shutdown_Lib()

    rebReleaseAndNull(&g_datatypes_module);
    dont(g_datatypes_context = nullptr);  // do at end of Shutdown_Datatypes()

} sweep_stubs: {

    Sweep_Stubs();  // free all managed Stubs, no more GC [1]

} shutdown_lib_and_datatypes: {

  // The lib module and datatypes module both have premade stubs that are not
  // subject to garbage collection.  This means that after all the managed
  // Stubs are released, Shutdown_Datatypes() and Shutdown_Lib() have to
  // manually free the premade stubs.

    Shutdown_Lib();
    Shutdown_Datatypes();

} shutdown_rest: {

    Shutdown_Builtin_Symbols();
    Shutdown_Interning();

    Shutdown_Api();

    Shutdown_Feeds();

    Shutdown_Trampoline();  // all API calls (e.g. rebRelease()) before this

} shutdown_after_keepalive_refs_to_managed_stubs_gone: {

  // ALL MANAGED STUBS HAVE THEIR KEEPALIVE REFERENCES GONE NOW!

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

} shutdown_memory_pools: {

  // Shutting down the memory manager must be done after all the Free_Memory()
  // calls have been made to balance their Alloc_On_Heap() calls.

    Shutdown_Pools();
}}
