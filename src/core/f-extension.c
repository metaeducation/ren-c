//
//  file: %f-extension.c
//  summary: "support for extensions"
//  section: functional
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2021 Ren-C Open Source Contributors
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
// "Extensions" refers to the idea of a module which has associated native
// code--in a DLL or .so file.  The extension machinery allows for the loading
// and unloading of a mixture of natives and usermode functions that can use
// those added natives.
//
// See the %extensions/ directory for some current (evolving) examples.
//

#include "sys-core.h"


//
//  Startup_Extension_Loader: C
//
void Startup_Extension_Loader(void)
{
    assert(rebUnboxLogic("empty? system.extensions"));
}


//
// Shutdown_Extension_Loader: C
//
void Shutdown_Extension_Loader(void)
{
    // The UNLOAD-EXTENSION will remove the extension from the list, so we
    // enumerate over a copy of the list.  (See remarks on making this more
    // efficient.)
    //
    rebElide("for-each 'ext copy system.extensions [unload-extension ext]");
}


//
//  builtin-extensions: native [
//
//  "Gets the list of (uninitialized) builtin extensions for the executable"
//
//      return: "Block of extension specifications ('collations')"
//          [block!]
//  ]
//
DECLARE_NATIVE(BUILTIN_EXTENSIONS)
//
// The config file used by %make.r marks extensions:
//
//    (`+`): Build into the executable
//    (`*`): Build as a dynamic library
//    (`-`): Don't build at all
//
// Command-line processing or other code that uses Rebol may need to make
// decisions on when to initialize these built-in extensions.  Also, building
// Rebol as a library may still entail a desire to ship that library with
// built-in extensions (e.g. building libr3.js wants to have JavaScript
// natives as an extension).
//
// So rebStartup() doesn't initialize extensions automatically.  Instead, this
// merely returns the list of descriptions of the extensions, which can then
// be loaded with the LOAD-EXTENSION function.
//
// 1. Built-in extensions do not receive the RebolApiTable, because they are
//    able to use direct calls to the API_rebXXX() versions, which is faster.
{
    INCLUDE_PARAMS_OF_BUILTIN_EXTENSIONS;

    Source* list = Make_Source_Managed(g_num_builtin_extensions);
    REBLEN i;
    for (i = 0; i != g_num_builtin_extensions; ++i) {
        ExtensionCollator* collator = g_builtin_collators[i];

        Value* details = (*collator)(nullptr);  // don't pass g_librebol [1]
        assert(Is_Block(details));
        assert(Cell_Series_Len_At(details) == MAX_COLLATOR + 1);

        Copy_Cell(Alloc_Tail_Array(list), Ensure_Element(details));
        rebRelease(details);
    }
    return Init_Block(OUT, list);
}


//
//  load-extension: native [
//
//  "Extension module loader (for DLLs or built-in extensions)"
//
//      return: [module!]
//      where "Path to extension file or block of builtin extension details"
//          [file! block!]  ; !!! Should it take a LIBRARY! instead?
//  ]
//
DECLARE_NATIVE(LOAD_EXTENSION)
//
// An "Extension" is a form of module which has associated native code.  There
// are two ways of getting that native code: one is through a "DLL", and
// another is by means of having it passed in through a HANDLE! of information
// that was "collated" together to build the extension into the executable.
//
// !!! In the initial design, extensions were distinct from modules, and could
// in fact load several different modules from the same DLL.  But that confused
// matters in terms of whether there was any requirement for the user to know
// what an "extension" was.
//
// !!! The DLL form has not been tested and maintained, so it needs to be
// hammered back into shape and tested.  However, higher priority is to make
// the extension mechanism work in the WebAssembly build with so-called
// "side modules", so that extra bits of native code functionality can be
// pulled into web sessions that want them.
{
    INCLUDE_PARAMS_OF_LOAD_EXTENSION;

    // See MAX_COLLATOR for collated block contents, which include init
    // and shutdown functions, as well as Rebol script source, plus Dispatcher
    // functions for each native.
    //
    Array* collated;

    if (Is_Block(ARG(WHERE))) {  // It's one of the BUILTIN-EXTENSIONS
        collated = Cell_Array_Ensure_Mutable(ARG(WHERE));  // already "collated"
    }
    else {  // It's a DLL, must locate and call its RX_Collate() function
        assert(Is_File(ARG(WHERE)));

        Value* library = rebValue("make library!", ARG(WHERE));

        Value* collated_block = rebValue(
            "run-library-collator", library, "-[RX_Collate]-"
        );

        if (not collated_block or not Is_Block(collated_block)) {
            rebElide("close", library);
            return PANIC(Error_Bad_Extension_Raw(ARG(WHERE)));
        }

        collated = Cell_Array_Ensure_Mutable(collated_block);
        rebRelease(collated_block);

        rebRelease(library);  // should we hang onto it, and pass italong?
    }

    assert(Array_Len(collated) == MAX_COLLATOR + 1);
    Push_Lifeguard(collated);

    const Cell* binding_ref_handle
        = Array_At(collated, COLLATOR_BINDING_REF);
    const Cell* script_compressed
        = Array_At(collated, COLLATOR_SCRIPT);
    REBLEN script_num_codepoints
        = VAL_UINT32(Array_At(collated, COLLATOR_SCRIPT_NUM_CODEPOINTS));
    const Cell* cfuncs_handle
        = Array_At(collated, COLLATOR_CFUNCS);

    REBLEN num_natives = Cell_Handle_Len(cfuncs_handle);
    CFunction* *cfuncs = Cell_Handle_Pointer(
        CFunction*,
        cfuncs_handle
    );
    g_current_uses_librebol = Get_Cell_Flag(
        cfuncs_handle, CFUNCS_NOTE_USE_LIBREBOL
    );

    // !!! used to use STD_EXT_CTX, now this would go in META OF

    SeaOfVars* sea = Alloc_Sea_Core(NODE_FLAG_MANAGED);
    Tweak_Link_Inherit_Bind(sea, g_lib_context);

    g_native_cfunc_pos = cfuncs;
    g_currently_loading_module = sea;

    Element* module = Init_Module(OUT, sea);  // guards lifetime

    Size script_size;
    Byte* script_utf8 = Decompress_Alloc_Core(
        &script_size,
        Cell_Handle_Pointer(Byte, script_compressed),
        Cell_Handle_Len(script_compressed),
        -1,  // max
        SYM_GZIP
    );

    // The decompress routine gives back a pointer which points directly into
    // a Binary Flex (e.g. a rebAlloc() product).  Get the BLOB! back so
    // we can pass it to import as a TEXT!.
    //
    Value* script = rebRepossess(script_utf8, script_size);

    // The rebRepossess() function gives us back a BLOB!.  But we happen to
    // know that the data is actually valid UTF-8.  The scanner does not
    // currently have mechanics to run any faster on already-valid UTF-8, but
    // it could.  Periodically shuffle the data between TEXT! and BLOB!, and
    // binary with the text flag set.
    //
    // !!! Adding at least one feature in the scanner that takes advantage of
    // prevalidated UTF-8 might be a good exploratory task, because until then
    // this *should* make no difference.
    //
    if (SPORADICALLY(2)) {
        Binary* b = Cell_Binary_Ensure_Mutable(script);
        TASTE_BYTE(b) = FLAVOR_0;  // set to FLAVOR_NONSYMBOL by FLEX_MASK
        b->leader.bits |= FLEX_MASK_STRING;
        Term_String_Len_Size(
            cast(String*, b),  // legal for tweaking cached data
            script_num_codepoints,
            Binary_Len(b)
        );
        Tweak_Link_Bookmarks(cast(String*, b), nullptr);

        if (SPORADICALLY(2))
            Init_Text(script, cast(String*, b));
    }

    // !!! We currently are pushing all extensions into the lib context so
    // they are seen by every module.  This is an interim step to keep things
    // running, but a better strategy is needed.
    //
    rebElide("sys.util/import*:into lib", script, module);

    // !!! Note: This does not get cleaned up in case of an error.
    //
    if (g_native_cfunc_pos != cfuncs + num_natives)
        crash ("NATIVE calls did not line up with stored C function count");
    g_native_cfunc_pos = nullptr;

    assert(g_currently_loading_module == sea);
    g_currently_loading_module = nullptr;

    rebRelease(script);

    Drop_Lifeguard(collated);

    rebElide("append system.extensions", module);

    RebolContext** binding_ref
        = Cell_Handle_Pointer(RebolContext*, binding_ref_handle);
    *binding_ref = cast(RebolContext*, g_currently_loading_module);

    // !!! If modules are to be "unloadable", they would need some kind of
    // finalizer to clean up their resources.  There are shutdown actions
    // defined in a couple of extensions, but no protocol by which the
    // system will automatically call them on shutdown (yet)

    assert(Cell_Module_Sea(OUT) == sea);
    return OUT;
}


//
//  Unloaded_Dispatcher: C
//
// This will be the dispatcher for the natives in an extension after the
// extension is unloaded.
//
static Bounce Unloaded_Dispatcher(Level* level_)
{
    Details* details = Ensure_Level_Details(level_);
    return PANIC(Error_Native_Unloaded_Raw(Phase_Archetype(details)));
}


//
//  unload-extension: native [
//
//  "Unload an extension"
//
//      return: []
//      extension "The extension to be unloaded"
//          [module!]
//  ]
//
DECLARE_NATIVE(UNLOAD_EXTENSION)
//
// !!! The initial extension model had support for not just loading extensions
// from a DLL, but also unloading them.  It raises a lot of questions that are
// somewhat secondary to any known use cases, and the semantics of the system
// were not pinned down well enough to support it.
//
// But one important feature it achieved was that if an extension initialized
// something (perhaps e.g. initializing memory) then it could call code to free
// that memory.  For the moment this is done by calling a method named
// `shutdown*` if it exists in the module.
{
    INCLUDE_PARAMS_OF_UNLOAD_EXTENSION;

    Value* extension = ARG(EXTENSION);

    Value* pos = rebValue(CANON(FIND), "system.extensions", extension);

    // Remove the extension from the loaded extensions list.
    //
    // !!! This is inefficient in the Shutdown_Extensions() case, because we
    // have to walk a copy of the array.  This likely calls for making the
    // "unload_extension 'all" walk a copy of the array here in the native or
    // some other optimization.  Review.
    //
    if (not pos)
        return PANIC("Could not find extension in loaded extensions list");
    rebElide(CANON(TAKE), rebR(pos));

    // There is a murky issue about how to disconnect DECLARE_NATIVE()s from
    // dispatchers that have been unloaded.  If an extension is unloaded
    // and reloaded again, should old ACTION! values work again?  If so, how
    // would this deal with a recompiled extension which might have changed
    // the parameters--thus breaking any specializations, etc?
    //
    // Idea: Check for a match, and if it is a match wire it up compatibly.
    // If not warn the user, leave a non-running stub in the place of the
    // old function...and they can reboot if they need to or unload and
    // reload the dependent modules.  Note that this would happen more often
    // than one might think for any locals declared as part of the frame, as
    // adding a local changes the "interface"--affecting downstream frames.
    //
    UNUSED(&Unloaded_Dispatcher);

    // Note: The mechanical act of unloading a DLL involved these calls.
    /*
        if (not IS_LIBRARY(lib))
            return PANIC(PARAM(EXT));

        if (Is_Library_Closed(Cell_Library(lib)))
            return PANIC(Error_Bad_Library_Raw());

        OS_CLOSE_LIBRARY(Cell_Library_FD(lib));
    */

   Value* shutdown_action = Sea_Var(
       Cell_Module_Sea(extension),
       CANON(SHUTDOWN_P),
       true
    );
   if (shutdown_action != nullptr)
        rebElide(rebRUN(shutdown_action));

   Value* unregister_extension_action = Sea_Var(
       Cell_Module_Sea(extension),
       CANON(UNREGISTER_EXTENSION_P),
       true
    );
   if (unregister_extension_action != nullptr)
        rebElide(rebRUN(unregister_extension_action));

   return TRASH;
}
