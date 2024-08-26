//
//  File: %f-extension.c
//  Summary: "support for extensions"
//  Section: functional
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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
    rebElide("for-each ext copy system.extensions [unload-extension ext]");
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
DECLARE_NATIVE(builtin_extensions)
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

    Array* list = Make_Array(g_num_builtin_extensions);
    REBLEN i;
    for (i = 0; i != g_num_builtin_extensions; ++i) {
        ExtensionCollator* collator = g_builtin_collators[i];

        Value* details = (*collator)(nullptr);  // don't pass g_librebol [1]
        assert(Is_Block(details));
        assert(Cell_Series_Len_At(details) == IDX_COLLATOR_MAX);

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
DECLARE_NATIVE(load_extension)
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

    // See IDX_COLLATOR_MAX for collated block contents, which include init
    // and shutdown functions, as well as Rebol script source, plus Dispatcher
    // functions for each native.
    //
    Array* collated;

    if (Is_Block(ARG(where))) {  // It's one of the BUILTIN-EXTENSIONS
        collated = Cell_Array_Ensure_Mutable(ARG(where));  // already "collated"
    }
    else {  // It's a DLL, must locate and call its RX_Collate() function
        assert(Is_File(ARG(where)));

        Value* library = rebValue("make library!", ARG(where));

        Value* collated_block = rebValue(
            "run-library-collator", library, "{RX_Collate}"
        );

        if (not collated_block or not Is_Block(collated_block)) {
            rebElide("close", library);
            fail (Error_Bad_Extension_Raw(ARG(where)));
        }

        collated = Cell_Array_Ensure_Mutable(collated_block);
        rebRelease(collated_block);

        rebRelease(library);  // should we hang onto it, and pass italong?
    }

    assert(Array_Len(collated) == IDX_COLLATOR_MAX);
    Push_GC_Guard(collated);

    const Cell* script_compressed
        = Array_At(collated, IDX_COLLATOR_SCRIPT);
    REBLEN script_num_codepoints
        = VAL_UINT32(Array_At(collated, IDX_COLLATOR_SCRIPT_NUM_CODEPOINTS));
    const Cell* cfuncs_handle
        = Array_At(collated, IDX_COLLATOR_CFUNCS);

    REBLEN num_natives = VAL_HANDLE_LEN(cfuncs_handle);
    CFunction* *cfuncs = VAL_HANDLE_POINTER(
        CFunction*,
        cfuncs_handle
    );

    // !!! used to use STD_EXT_CTX, now this would go in META OF

    Context* module_ctx = Alloc_Context_Core(REB_MODULE, 1, NODE_FLAG_MANAGED);
    node_LINK(NextVirtual, module_ctx) = Lib_Context;

    g_native_cfunc_pos = cfuncs;
    PG_Currently_Loading_Module = module_ctx;

    DECLARE_ATOM (module);
    Init_Context_Cell(module, REB_MODULE, module_ctx);
    Push_GC_Guard(module);  // !!! Is GC guard unnecessary due to references?

    size_t script_size;
    Byte* script_utf8 = Decompress_Alloc_Core(
        &script_size,
        VAL_HANDLE_POINTER(Byte, script_compressed),
        VAL_HANDLE_LEN(script_compressed),
        -1,  // max
        SYM_GZIP
    );

    // The decompress routine gives back a pointer which points directly into
    // a Binary Flex (e.g. a rebAlloc() product).  Get the BINARY! back so
    // we can pass it to import as a TEXT!.
    //
    Value* script = rebRepossess(script_utf8, script_size);

    // The rebRepossess() function gives us back a BINARY!.  But we happen to
    // know that the data is actually valid UTF-8.  The scanner does not
    // currently have mechanics to run any faster on already-valid UTF-8, but
    // it could.  Periodically shuffle the data between TEXT! and BINARY!, and
    // binary with the text flag set.
    //
    // !!! Adding at least one feature in the scanner that takes advantage of
    // prevalidated UTF-8 might be a good exploratory task, because until then
    // this *should* make no difference.
    //
    if (SPORADICALLY(2)) {
        Binary* bin = Cell_Binary_Ensure_Mutable(script);
        FLAVOR_BYTE(bin) = FLAVOR_STRING;
        Term_String_Len_Size(
            cast(String*, bin),  // legal for tweaking cached data
            script_num_codepoints,
            Binary_Len(bin)
        );
        LINK(Bookmarks, m_cast(Binary*, bin)) = nullptr;

        if (SPORADICALLY(2))
            Init_Text(script, cast(String*, bin));
    }

    // !!! We currently are pushing all extensions into the lib context so
    // they are seen by every module.  This is an interim step to keep things
    // running, but a better strategy is needed.
    //
    rebElide("sys.util.import*/into lib", script, module);

    // !!! Note: This does not get cleaned up in case of an error.
    //
    if (g_native_cfunc_pos != cfuncs + num_natives)
        panic ("NATIVE calls did not line up with stored C function count");
    g_native_cfunc_pos = nullptr;

    assert(PG_Currently_Loading_Module == module_ctx);
    PG_Currently_Loading_Module = nullptr;

    rebRelease(script);

    Drop_GC_Guard(module);
    Drop_GC_Guard(collated);

    rebElide("append system.extensions", CTX_ARCHETYPE(module_ctx));

    // !!! If modules are to be "unloadable", they would need some kind of
    // finalizer to clean up their resources.  There are shutdown actions
    // defined in a couple of extensions, but no protocol by which the
    // system will automatically call them on shutdown (yet)

    return Init_Context_Cell(OUT, REB_MODULE, module_ctx);
}


//
//  Unloaded_Dispatcher: C
//
// This will be the dispatcher for the natives in an extension after the
// extension is unloaded.
//
static const Value* Unloaded_Dispatcher(Level* L)
{
    UNUSED(L);

    fail (Error_Native_Unloaded_Raw(Phase_Archetype(Level_Phase(L))));
}


//
//  unload-extension: native [
//
//  "Unload an extension"
//
//      return: [~]
//      extension "The extension to be unloaded"
//          [module!]
//  ]
//
DECLARE_NATIVE(unload_extension)
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

    Value* extension = ARG(extension);

    Value* pos = rebValue(Canon(FIND), "system.extensions", extension);

    // Remove the extension from the loaded extensions list.
    //
    // !!! This is inefficient in the Shutdown_Extensions() case, because we
    // have to walk a copy of the array.  This likely calls for making the
    // "unload_extension 'all" walk a copy of the array here in the native or
    // some other optimization.  Review.
    //
    if (not pos)
        fail ("Could not find extension in loaded extensions list");
    rebElide(Canon(TAKE), rebR(pos));

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
            fail (PARAM(ext));

        if (IS_LIB_CLOSED(VAL_LIBRARY(lib)))
            fail (Error_Bad_Library_Raw());

        OS_CLOSE_LIBRARY(VAL_LIBRARY_FD(lib));
    */

   Value* shutdown_action = MOD_VAR(
       VAL_CONTEXT(extension),
       Canon(SHUTDOWN_P),
       true
    );
   if (shutdown_action == nullptr)
        return TRASH;

   rebElide(rebRUN(shutdown_action));

   return TRASH;
}


//
//  Extend_Generics_Someday: C
//
// !!! R3-Alpha's "generics" (like APPEND or TAKE) dispatched to code based on
// the first argument.  So APPEND to a BLOCK! would call the list dispatcher,
// while APPEND to a GOB! would call the gob dispatcher.  The list of legal
// datatypes that could be operated on was fixed as part of the declaration
// in %generics.r (though R3-Alpha called them "actions").
//
// Ren-C attempts to streamline the core so it can be used for more purposes,
// where suppport code for GOB! (or IMAGE!) may be redundant or
// otherwise wasteful.  These types are moved to extensions, which may be
// omitted from the build (or optionally loaded as DLLs).   That means that
// when the system is booting, it might not know what a GOB! is...and other
// extensions may wish to add types to the generic after-the-fact as well.
//
// Hence extension types are taken off the generic definitions.  The concept
// is that they would be added dynamically.  How this would be done is not
// known at this time...as an extensible generics system hasn't been made yet.
// What's done instead is the hack of just saying that all generics are
// willing to dispatch to a custom type, and it's the job of the handler to
// raise an error if it doesn't know what the generic means.  The key downside
// of this is that HELP doesn't give you information about what specific
// generics are applicable to extension types.
//
// This function is a placeholder to keep track of the unimplemented feature.
// What was done is that for all the definitions in %generics.r that took an
// extension type previously, a bit of that spec was copied into the extension
// and then passed in to the type registration routine as a block.  In theory
// this *kind* of information could be used to more strategically update the
// type specs and help to reflect the legal operations.
//
// (It would be expected that the ability to extend generics via usermode
// functions would be done through whatever this mechanism for extending them
// with native code would be.)
//
void Extend_Generics_Someday(Value* block) {
    UNUSED(block);
}
